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

#include "asterisk/time.h"
#include "asterisk/utils.h"

#include "bla_event.h"
#include "bla_station.h"
#include "bla_trunk.h"

#include "bla_event_queue.h"

int bla_event_queue_init(struct bla_event_queue *self)
{
	/* TODO: Initialize the linked list of event objects? */

	return 0;
}

int bla_event_queue_destroy(struct bla_event_queue *self)
{
	/* TODO: Destroy the linked list of event objects */
	return 0;
}

static void *bla_event_queue_thread(struct bla_event_queue *self)
{
	ast_log(LOG_NOTICE, "Entering BLA event thread");

	ast_mutex_lock(&self->_lock);

	/* Loop to wait for and handle events */
	while (1)
	{
		/* Wait for events */
		/* TODO: Check for timeouts before waiting */
		ast_cond_wait(&self->_cond, &self->_lock);

		/* Check for stop signal */
		if (self->_stop)
			/* FIXME: Should we exit immediately without flushing events? */
			break;

		struct bla_event *event;
		/* Loop through every event on the queue */
		while ((event = bla_event_queue_dequeue(self))) {
			/* Dispatch every event to type-specific handlers */
			if (bla_event_dispatch(event)) {
				ast_log(LOG_ERROR, "Failed to dispatch '%s' BLA event",
					bla_event_type_as_string(event));
			}
			ao2_ref(event, -1);
		}
	}

	ast_mutex_unlock(&self->_lock);

	ast_log(LOG_NOTICE, "Leaving BLA event thread");

	return NULL;
}

int bla_event_queue_start(struct bla_event_queue *self)
{
	/* Prepare the event thread resources */
	ast_mutex_init(&self->_lock);
	ast_cond_init(&self->_cond, NULL);
	self->_stop = 0;

	/* Start the event thread */
	ast_log(LOG_NOTICE, "Starting BLA event thread");
	ast_pthread_create_detached_background(
		&self->_thread, NULL, (void *(*)(void*))bla_event_queue_thread, self);

	return 0;
}

void bla_event_queue_join(struct bla_event_queue *self)
{
	/* Signal the event thread to stop */
	ast_mutex_lock(&self->_lock);
	self->_stop = 1;
	ast_cond_signal(&self->_cond);
	ast_mutex_unlock(&self->_lock);

	/* Join the event thread */
	pthread_join(self->_thread, NULL);

	/* Destroy the event thread resources */
	ast_cond_destroy(&self->_cond);
	ast_mutex_destroy(&self->_lock);
}

void bla_event_queue_enqueue(struct bla_event_queue *self, struct bla_event *event)
{
	ast_mutex_lock(&self->_lock);

	ast_log(LOG_NOTICE, "BLA added '%s' event to its event queue",
	       bla_event_type_as_string(event));

	AST_LIST_INSERT_TAIL(&self->_events, event, _list_entry);

	/* Signal the event thread */
	ast_cond_signal(&self->_cond);

	ast_mutex_unlock(&self->_lock);
}

struct bla_event *bla_event_queue_dequeue(struct bla_event_queue *self)
{
	struct bla_event *event;

	event = AST_LIST_REMOVE_HEAD(&self->_events, _list_entry);

	if (event != NULL)
		ast_log(LOG_NOTICE, "BLA removed '%s' event from its event queue",
			bla_event_type_as_string(event));

	return event;
}

int bla_event_queue_ring_station(
	struct bla_event_queue *self,
	struct bla_station *station,
	struct bla_trunk *trunk)
{
	struct bla_event *event;
	union bla_event_data data;

	ast_log(LOG_NOTICE, "Creating ring event for BLA station '%s' from BLA trunk '%s'",
	       bla_station_name(station), bla_trunk_name(trunk));

	/* Build the ring station event */
	data.ring_station_event.station = station;
	data.ring_station_event.trunk = trunk;

	event = bla_event_alloc();  /* FIXME: Make sure someone handles the bla_event reference */
	if ((event == NULL) || bla_event_init(event, BLA_RING_STATION_EVENT, &data, ast_tvnow())) {
		ast_log(LOG_ERROR, "Failed to create ring event for BLA station '%s' from BLA trunk '%s'",
			bla_station_name(station), bla_trunk_name(trunk));
		return -1;
	}

	/* Queue up this event */
	bla_event_queue_enqueue(self, event);

	return 0;
}

int bla_event_queue_station_dial_state(
	struct bla_event_queue *self,
	struct bla_station *station,
	struct bla_trunk *trunk,
	struct ast_dial *dial)
{
	struct bla_event *event;
	union bla_event_data data;

	ast_log(LOG_NOTICE, "Creating dial state event for BLA station '%s'",
	       bla_station_name(station));

	/* Build the station dial state event */
	data.station_dial_state_event.station = station;
	data.station_dial_state_event.trunk = trunk;
	data.station_dial_state_event.dial = dial;

	event = bla_event_alloc();  /* FIXME: Make sure someone handles the bla_event reference */
	if ((event == NULL) || bla_event_init(event, BLA_STATION_DIAL_STATE_EVENT, &data, ast_tvnow())) {
		ast_log(LOG_ERROR, "Failed to create dial state event for BLA station '%s'",
			bla_station_name(station));
		return -1;
	}

	/* Queue up this event */
	bla_event_queue_enqueue(self, event);

	return 0;
}

int bla_event_queue_process_ringing_stations(
	struct bla_event_queue *self)
{
	struct bla_event *event;
	/* NOTE: No data for this event */

	ast_log(LOG_NOTICE, "Creating process ringing stations event for BLA");

	event = bla_event_alloc();  /* FIXME: Make sure someone handles the bla_event reference */
	if ((event == NULL) || bla_event_init(event, BLA_PROCESS_RINGING_STATIONS_EVENT, NULL, ast_tvnow())) {
		ast_log(LOG_ERROR, "Failed to create process ringing stations event for BLA");
		return -1;
	}

	/* Queue up this event */
	bla_event_queue_enqueue(self, event);

	return 0;
}
