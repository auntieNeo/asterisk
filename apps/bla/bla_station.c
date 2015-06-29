/*
 * Asterisk app_bla -- Bridged Line Appearances for Asterisk
 *
 * Copyright 2015, Jonathan Glines
 *
 * Jonathan Glines <auntieNeo@gmail.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#include "asterisk.h"

#include "asterisk/channel.h"
#include "asterisk/dial.h"
#include "asterisk/logger.h"
#include "asterisk/strings.h"

#include "bla_application.h"
#include "bla_bridge.h"
#include "bla_common.h"
#include "bla_event_queue.h"
#include "bla_trunk.h"
#include "bla_trunk_ref.h"

#include "bla_station.h"

int bla_station_init(struct bla_station *self)
{
	ast_log(LOG_NOTICE, "Initializing BLA station");

	self->_name[0] = '\0';
	self->_device_string[0] = '\0';
	self->_tech = NULL;
	self->_device = NULL;
	self->_trunk_refs = ao2_container_alloc(
		  1,
		  (ao2_hash_fn*)bla_trunk_ref_hash,
		  (ao2_callback_fn*)bla_trunk_ref_cmp);

	return 0;
}

int bla_station_destroy(struct bla_station *self)
{
	ao2_ref(self->_trunk_refs, -1);

	return 0;
}

void bla_station_add_trunk_ref(struct bla_station *self, const char *trunk_name)
{
	struct bla_trunk_ref *trunk_ref;

	/* Prevent adding duplicate references */
	if ((trunk_ref = ao2_find(self->_trunk_refs, trunk_name, OBJ_SEARCH_KEY))) {
		ao2_ref(trunk_ref, -1);
		return;
	}

	ast_log(LOG_NOTICE, "Adding reference to BLA trunk '%s' for BLA station '%s'",
		trunk_name, bla_station_name(self));

	trunk_ref = bla_trunk_ref_alloc();
	bla_trunk_ref_init(trunk_ref);
	bla_trunk_ref_set_name(trunk_ref, trunk_name);

	ao2_link(self->_trunk_refs, trunk_ref);
	ao2_ref(trunk_ref, -1);
}

struct bla_trunk_ref *bla_station_find_trunk_ref(struct bla_station *self, const char *trunk_name)
{
	return ao2_find(self->_trunk_refs, trunk_name, OBJ_SEARCH_KEY);
}

struct bla_trunk *bla_station_find_idle_trunk(struct bla_station *self, struct bla_application *app)
{
	struct bla_trunk *result = NULL;

	/* Iterate over this station's trunks */
	struct bla_trunk_ref *trunk_ref;
	struct ao2_iterator i;
	i = ao2_iterator_init(self->_trunk_refs, 0);
	while ((trunk_ref = ao2_iterator_next(&i))) {
		struct bla_trunk *trunk = bla_trunk_ref_resolve(trunk_ref, app);
		/* Return with the first trunk that is not in use */
		if (bla_trunk_is_idle(trunk)) {
			ao2_ref(trunk_ref, -1);
			result = trunk;
			break;
		}
		ao2_ref(trunk, -1);
		ao2_ref(trunk_ref, -1);
	}

	return result;
}


struct bla_dial_trunk_wait_args {
	struct bla_station *station;
	struct bla_trunk *trunk;
	/* Mutex/cond pair to wait for dial state change from dialed trunk */
	ast_cond_t cond;
	ast_mutex_t lock;
	int done;
	int state;
};
static void bla_station_dial_trunk_wait(struct ast_dial *dial)
{
	int done = 0;
	int state = 0;
	enum ast_dial_result dial_state;

	struct bla_dial_trunk_wait_args *args = ast_dial_get_user_data(dial);

	ast_mutex_lock(&args->lock);
	if (args->station == NULL) {  /* Controlling thread signaled us to stop */
		ast_dial_set_state_callback(dial, NULL);
		return;
	}

	dial_state = ast_dial_state(dial);
	ast_log(LOG_NOTICE, "BLA trunk '%s' dial state: '%d'",
		bla_trunk_name(args->trunk), dial_state);
	switch (dial_state) {
		case AST_DIAL_RESULT_ANSWERED:
			ast_log(LOG_NOTICE, "BLA trunk '%s' answered call from station '%s'",
				bla_trunk_name(args->trunk), bla_station_name(args->station));
			bla_trunk_set_channel(args->trunk, ast_dial_answered(dial));
		case AST_DIAL_RESULT_FAILED:
		case AST_DIAL_RESULT_HANGUP:
		case AST_DIAL_RESULT_INVALID:
		case AST_DIAL_RESULT_TIMEOUT:
		case AST_DIAL_RESULT_UNANSWERED:
			done = 1;
			break;
		case AST_DIAL_RESULT_TRYING:
			state = AST_CONTROL_PROGRESS;
			break;
		case AST_DIAL_RESULT_PROCEEDING:
		case AST_DIAL_RESULT_PROGRESS:
		case AST_DIAL_RESULT_RINGING:
			state = AST_CONTROL_RINGING;
			break;
		default:
			ast_log(LOG_WARNING, "BLA encountered unknown dial state '%d' while dialing BLA trunk '%s'",
			dial_state, bla_trunk_name(args->trunk));
	}

	/* Signal the controlling thread */
	args->done = done;
	args->state = state;
	ast_cond_signal(&args->cond);
	ast_mutex_unlock(&args->lock);
}

struct bla_dial_trunk_args {
	struct bla_station *station;
	struct bla_trunk *trunk;
  	/* Mutex/cond pair to signal station thread from trunk thread */
	ast_cond_t cond;
	ast_mutex_t lock;
};
static void *bla_station_dial_trunk_thread(struct bla_dial_trunk_args *args)
{
	struct ast_dial *dial;

	ast_log(LOG_NOTICE, "Entered dial trunk thread for station '%s' dialing trunk '%s'",
		bla_station_name(args->station),
		bla_trunk_name(args->trunk));

  /* FIXME: Don't dial the trunk if the trunk channel isn't NULL (i.e. it's already connected) */

	/* Prepare the dial object to dial the trunk */
	char *device = ast_strdupa(bla_trunk_device(args->trunk));
	char *tech = strsep(&device, "/");
	dial = ast_dial_create();
	if (ast_dial_append(dial, tech, device, NULL) == -1) {
		ast_log(LOG_ERROR, "Failed to dial BLA trunk '%s'",
			bla_trunk_name(args->trunk));
		/* Signal the station thread to continue */
		ast_mutex_lock(&args->lock);
		ast_cond_signal(&args->cond);
		ast_mutex_unlock(&args->lock);
		return NULL;
	}
	ast_log(LOG_NOTICE, "Dialing BLA trunk '%s' with tech '%s' and device '%s'",
		bla_trunk_name(args->trunk), tech, device);

	/* Set wait callback to notify us of changes to dial state */
	struct bla_dial_trunk_wait_args wait_args;
	wait_args.station = args->station;
	wait_args.trunk = args->trunk;
	ast_mutex_init(&wait_args.lock);
	ast_cond_init(&wait_args.cond, NULL);
	ast_dial_set_user_data(dial, &wait_args);
	ast_dial_set_state_callback(dial, bla_station_dial_trunk_wait);

	/* Asynchronously dial the trunk */
	ast_mutex_lock(&wait_args.lock);
	if (ast_dial_run(dial, bla_station_channel(args->station), 1) != AST_DIAL_RESULT_TRYING) {
		ast_log(LOG_ERROR, "Failed to dial BLA trunk '%s'",
			bla_trunk_name(args->trunk));

		/* Signal the station thread to continue */
		ast_mutex_lock(&args->lock);
		ast_cond_signal(&args->cond);
		ast_mutex_unlock(&args->lock);

		/* Clean up the dial thread */
		ast_dial_destroy(dial);
		/* Clean up the dial thread resources */
		ast_cond_destroy(&wait_args.cond);
		ast_mutex_destroy(&wait_args.lock);

		return NULL;
	}
	int last_state = 0;
	wait_args.state = 0;
	while (1) {
		/* Wait for signal from dial state callback */
		ast_cond_wait(&wait_args.cond, &wait_args.lock);
		if (wait_args.state && (wait_args.state != last_state)) {
			/* Notify the station channel of the dial state change */
			ast_indicate(bla_station_channel(args->station), wait_args.state);
			last_state = wait_args.state;
		}
		if (wait_args.done)
			break;
		/* TODO: Check that the station is still alive */
	}
	/* Signal the dial state wait thread to stop */
	wait_args.station = NULL;
	wait_args.trunk = NULL;
	ast_mutex_unlock(&wait_args.lock);

	/* Check if the trunk connected */
	/* FIXME: I'm not 100% sure this check is safe
	 * (e.g. some other thread might be dialing this trunk? */
	if (bla_trunk_channel(args->trunk) == NULL) {
		ast_log(LOG_NOTICE, "BLA trunk '%s' did not answer",
			bla_trunk_name(args->trunk));

		/* Signal the station thread to continue */
		ast_cond_signal(&args->cond);
		ast_mutex_unlock(&args->lock);

		/* Clean up the dial thread */
		ast_dial_join(dial);
		ast_dial_destroy(dial);
		/* Clean up the dial thread resources */
		ast_cond_destroy(&wait_args.cond);
		ast_mutex_destroy(&wait_args.lock);

		return NULL;
	}

	/* Signal the station thread to continue */
	ast_mutex_lock(&args->lock);
	ast_cond_signal(&args->cond);
	ast_mutex_unlock(&args->lock);

	/* Answer the trunk channel */
	ast_answer(bla_trunk_channel(args->trunk));

	/* Join the trunk to the bridge */
	bla_bridge_join_trunk(bla_trunk_bridge(args->trunk), args->trunk);

	/* Clean up the dial thread */
	ast_dial_join(dial);
	ast_dial_destroy(dial);
	/* Clean up the dial thread resources */
	ast_cond_destroy(&wait_args.cond);
	ast_mutex_destroy(&wait_args.lock);
	/* Clean up the trunk channel */
	bla_trunk_set_channel(args->trunk, NULL);

	return NULL;
}

int bla_station_dial_trunk(
	struct bla_station *self,
	struct bla_trunk *trunk)
{
	pthread_t thread;
	struct bla_dial_trunk_args args = {
		.station = self,
		.trunk = trunk,
	};

	/* Create a thread to dial, ring, and join the trunk to our bridge */
	ast_mutex_init(&args.lock);
	ast_cond_init(&args.cond, NULL);
	ast_mutex_lock(&args.lock);
	ast_pthread_create_detached_background(
		&thread, NULL, (void *(*)(void*))bla_station_dial_trunk_thread, &args);
	ast_autoservice_start(bla_station_channel(self));
	/* FIXME: Wait for... what exactly are we waiting for here? */
	/* FIXME: Implement station timeouts here? We can't wait forever... */
	ast_cond_wait(&args.cond, &args.lock);
	ast_autoservice_stop(bla_station_channel(self));
	ast_log(LOG_NOTICE, "Station '%s' thread finished waiting for BLA dial trunk thread",
		bla_station_name(self));
	ast_mutex_unlock(&args.lock);
	ast_mutex_destroy(&args.lock);
	ast_cond_destroy(&args.cond);

	/* FIXME: We never seem to join this dial trunk thread anywhere...
	 * Somehow we need a way to signal it to stop and join it (not here
	 * though).
	 */

	return 0;
}

int bla_station_handle_ring_event(
	struct bla_station *self,
	struct bla_trunk *trunk,
	struct timeval timestamp)
{
	/* NOTE: This function is only ever called by the BLA event thread,
	 * so all of these checks are effectively synchronous, even without
	 * much locking.
	 */

	/* TODO: Check if the station is busy */
	if (bla_station_is_busy(self)) {
		ast_log(LOG_NOTICE, "Not ringing BLA station '%s'; station is busy",
			bla_station_name(self));
		return 0;
	}

	/* Check if the station is already ringing */
	if (bla_station_is_ringing(self)) {
		ast_log(LOG_NOTICE, "Not ringing BLA station '%s'; station is already ringing",
			bla_station_name(self));
		return 0;
	}

	/* TODO: Check if the station has failed recently */
	if (bla_station_is_failed(self)) {
		ast_log(LOG_NOTICE, "Not ringing BLA station '%s'; failed to dial station recently",
			bla_station_name(self));
		return 0;
	}

	/* TODO: Check if the station's ring cooldown is in effect */
	if (bla_station_is_cooldown(self)) {
		ast_log(LOG_NOTICE, "Not ringing BLA station '%s'; station ring cooldown in effect",
			bla_station_name(self));
		return 0;
	}

	/* TODO: Check if this trunk has already timed out for this station */
	if (bla_station_is_timeout(self, trunk)) {
		ast_log(LOG_NOTICE, "Not ringing BLA station '%s'; BLA trunk '%s' reached timeout for station recently",
			bla_station_name(self), bla_trunk_name(trunk));
		return 0;
	}

	/* TODO: Ring the station */
	return bla_station_ring(self, trunk);
}

int bla_station_handle_dial_state_event(
	struct bla_station *self,
	struct bla_trunk *trunk,
	struct ast_dial *dial,
	struct timeval timestamp)
{
	enum ast_dial_result dial_result;

	/* TODO: Decide what to do given the current dial state */
	dial_result = ast_dial_state(dial);
	ast_log(LOG_NOTICE, "BLA station '%s' has dial state '%s'",
		bla_station_name(self),
		bla_dial_result_as_string(dial_result));
	switch (dial_result) {
		case AST_DIAL_RESULT_ANSWERED:
			{
				struct ast_channel *station_chan;
				/* Get the channel that answered */
				station_chan = ast_dial_answered(dial);

				/* TODO: Make sure station's channel is NULL */

				/* TODO: Set the station's channel */
				bla_station_set_channel(self, station_chan);

				/* TODO: Free the dial object? */

				/* TODO: Answer the trunk (and bridge the station) */
				return bla_station_answer_trunk(self, trunk);
			}
			break;
		case AST_DIAL_RESULT_INVALID:
		case AST_DIAL_RESULT_FAILED:
		case AST_DIAL_RESULT_TIMEOUT:
		case AST_DIAL_RESULT_HANGUP:
		case AST_DIAL_RESULT_UNANSWERED:
			/* TODO: Stop dialing here? I think it might stop automatically. */
			/* TODO: Free the dial object? */
			/* TODO: Mark the station as not ringing */
			/* TODO: Set appropriate timestamps for calculating cooldown and timeouts */
			break;
		case AST_DIAL_RESULT_TRYING:
		case AST_DIAL_RESULT_RINGING:
		case AST_DIAL_RESULT_PROGRESS:
		case AST_DIAL_RESULT_PROCEEDING:
			/* TODO: Just chill here */
			break;
	}

	return 0;
}

struct bla_station_dial_state_args {
	struct bla_station *station;
	struct bla_trunk *trunk;
/*	struct bla_event_queue *event_queue; */  /* TODO */
};
static void bla_station_dial_state_callback(struct ast_dial *dial)
{
	/* FIXME: This wouldn't need to access the app singleton if we just passed the event queue with the dial user data */
	RAII_VAR(struct bla_application *, app, bla_application_singleton(), ao2_cleanup);
	struct bla_station_dial_state_args *args = ast_dial_get_user_data(dial);

	ast_log(LOG_NOTICE, "Inside dial state callback for BLA station '%s'",
		bla_station_name(args->station));

	/* Queue up a station dial state event */
	bla_event_queue_station_dial_state(
		bla_application_event_queue(app),
		args->station, args->trunk, dial);
}

int bla_station_ring(
	struct bla_station *self,
	struct bla_trunk *trunk)
{
	struct ast_dial *dial;
	struct bla_station_dial_state_args *args;

	ast_log(LOG_NOTICE, "Ringing BLA station '%s'",
		bla_station_name(self));

	/* TODO: Build a dial object */
	if ((dial = ast_dial_create()) == NULL)
		return -1;
	/* Append the station channel we are dialing */
	ast_dial_append(dial, bla_station_tech(self), bla_station_device(self),
		NULL);  /* TODO: Giving the channel assigned ID's might be useful for debugging */

	/* Add a callback for dial state changes */
	args = ast_malloc(sizeof(struct bla_station_dial_state_args));
	args->station = self;
	args->trunk = trunk;
	ast_dial_set_user_data(dial, args);
	ast_dial_set_state_callback(dial, bla_station_dial_state_callback);

	/* Store the dial object in the station */
	/* This lets us know that the station is currently ringing, and also
	 * FIXME: There must be some other reason, otherwise I would rather
	 * avoid the dial/station circular reference. */
	bla_station_set_dial(self, dial);

	/* TODO: Actually dial the station */
	enum ast_dial_result dial_result;
	if ((dial_result = ast_dial_run(dial, bla_trunk_channel(trunk), 1 /*async*/)) != AST_DIAL_RESULT_TRYING)
	{
		ast_log(LOG_ERROR, "Failed to dial BLA station '%s': ast_dial_run() returned '%s'",
			bla_station_name(self),
			bla_dial_result_as_string(dial_result));
		ast_free(ast_dial_get_user_data(dial));
		ast_dial_destroy(dial);
		return -1;
	}

	return 0;
}

int bla_station_is_busy(struct bla_station *self)
{
	/* TODO */
	return 0;
}

int bla_station_is_ringing(struct bla_station *self)
{
	/* The station must be ringing if it still has a dial handle object */
	if (bla_station_get_dial(self) != NULL)
		return 1;
	return 0;
}

int bla_station_is_failed(struct bla_station *self)
{
	/* TODO */
	return 0;
}

int bla_station_is_cooldown(struct bla_station *self)
{
	/* TODO */
	return 0;
}

int bla_station_is_timeout(
	struct bla_station *self,
	struct bla_trunk *trunk)
{
	/* TODO */
	return 0;
}

struct bla_station_answer_trunk_args {
	struct bla_station *station;
	struct bla_trunk *trunk;
	ast_cond_t cond;
	ast_mutex_t lock;
};
static void *bla_station_answer_trunk_thread(
	struct bla_station_answer_trunk_args *args)
{
	struct bla_station *station;
	struct bla_trunk *trunk;

	station = args->station;
	trunk = args->trunk;

	/* Signal the controlling thread to continue now that we have copied our
	 * arguments */
	ast_mutex_lock(&args->lock);
	ast_cond_signal(&args->cond);
	ast_mutex_unlock(&args->lock);

	ast_log(LOG_NOTICE, "Entering thread for BLA station '%s' answering BLA trunk '%s'",
		bla_station_name(station),
		bla_trunk_name(trunk));

	/* TODO: Answer the trunk's channel */
	ast_answer(bla_trunk_channel(trunk));

	ast_log(LOG_NOTICE, "About to notify BLA trunk '%s' thread from BLA station '%s'",
		bla_trunk_name(trunk),
		bla_station_name(station));

	/* TODO: Notify the trunk thread that it can join the bridge */
	bla_trunk_station_responding(trunk, station);

	ast_log(LOG_NOTICE, "Just notified BLA trunk '%s' thread from BLA station '%s'",
		bla_trunk_name(trunk),
		bla_station_name(station));

	/* TODO: Stop the ringing for stations that no longer have any ringing trunks */

	/* TODO: Join the station to the trunk's bridge */

	return NULL;
}

int bla_station_answer_trunk(
	struct bla_station *self,
	struct bla_trunk *trunk)
{
	pthread_t thread;
	struct bla_station_answer_trunk_args args = {
		.station = self,
		.trunk = trunk,
	};

	/* TODO: Create a thread to answer the trunk */
	ast_mutex_init(&args.lock);
	ast_cond_init(&args.cond, NULL);
	ast_mutex_lock(&args.lock);
	ast_pthread_create_detached_background(
		&thread, NULL, (void *(*)(void*))bla_station_answer_trunk_thread, &args);
	/* TODO: Wait for the thread to finish copying its arguments from our
	 * stack */
	ast_cond_wait(&args.cond, &args.lock);
	ast_mutex_unlock(&args.lock);
	ast_mutex_destroy(&args.lock);
	ast_cond_destroy(&args.cond);

	return 0;
}

int bla_station_hash(void *arg, int flags)
{
	const struct bla_station *self = arg;
	const char *name = arg;

	switch (flags) {
		case OBJ_SEARCH_OBJECT:
			return ast_str_hash(bla_station_name(self));
		case OBJ_SEARCH_KEY: 
			return ast_str_hash(name);
	}

	ast_assert(0);
	return 0;
}


int bla_station_cmp(
	const struct bla_station *self,
	void *arg,
	int flags)
{
	const struct bla_station *other = arg;
	const char *name = arg;
	int found = 0;

	switch (flags) {
		case OBJ_SEARCH_OBJECT:
			name = bla_station_name(other);
		case OBJ_SEARCH_KEY:
			if (strncmp(bla_station_name(self), name, AST_MAX_CONTEXT) == 0)
				found = 1;
			break;
		case OBJ_SEARCH_PARTIAL_KEY:
			if (strncmp(bla_station_name(self), name, strnlen(name, AST_MAX_CONTEXT)) == 0)
				found = 1;
			break;
		default:
			ast_assert(0);
	}

	return found ? (CMP_MATCH | CMP_STOP) : 0;
}
