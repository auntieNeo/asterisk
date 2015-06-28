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

#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"

#include "bla_bridge.h"
#include "bla_config.h"
#include "bla_event_queue.h"
#include "bla_station.h"
#include "bla_station_ref.h"
#include "bla_trunk.h"
#include "bla_trunk_ref.h"

#include "bla_application.h"

static AO2_GLOBAL_OBJ_STATIC(bla_app_singleton);

struct bla_application *bla_application_singleton(void)
{
	struct bla_application *app = ao2_global_obj_ref(bla_app_singleton);

	ast_assert(app != NULL);

	return app;
}

int bla_application_singleton_create(void)
{
	struct bla_application *app;

	ast_assert(ao2_global_obj_ref(bla_app_singleton) == NULL);

	app = bla_application_alloc();
	if (bla_application_init(app))
		return -1;
	ao2_global_obj_replace_unref(bla_app_singleton, app);

	/* TODO: Assert reference count on app is two */
	ao2_ref(app, -1);

	return 0;
}

void bla_application_singleton_release(void)
{
	ao2_global_obj_release(bla_app_singleton);
}

int bla_application_init(struct bla_application *self)
{
	ast_log(LOG_NOTICE, "Initializing BLA application");

	self->_stations = NULL;
	self->_trunks = NULL;

	/* Initialize the event queue */
	self->_event_queue = bla_event_queue_alloc();
	bla_event_queue_init(self->_event_queue);
	/* Start the event queue thread */
	bla_event_queue_start(self->_event_queue);

	return 0;
}

int bla_application_destroy(struct bla_application *self)
{
	/* Stop the event queue thread */
	bla_event_queue_join(self->_event_queue);
	/* Destroy the event queue */
	bla_event_queue_destroy(self->_event_queue);
	ao2_ref(self->_event_queue, -1);

	if (self->_trunks != NULL)
		ao2_ref(self->_trunks, -1);
	if (self->_stations != NULL)
		ao2_ref(self->_stations, -1);
	/* TODO: Assert that these refcounts are now zero */

	return 0;
}

int bla_application_read_config(struct bla_application *self)
{
	RAII_VAR(struct bla_config *, config, bla_config_alloc(), bla_config_destroy);

	ast_log(LOG_NOTICE, "Application reading BLA config");

	if (bla_config_init(config))
	{
		ast_log(LOG_ERROR, "Failed to initialize bla_config object");
		return -1;
	}

	if(bla_config_read(config))
	{
		ast_log(LOG_ERROR, "Failed to read/parse bla.conf BLA config");
		return -1;
	}

	/* Steal the stations and trunks from config before it leaves scope */
	self->_stations = bla_config_stations(config);
	ao2_ref(self->_stations, 1);
	self->_trunks = bla_config_trunks(config);
	ao2_ref(self->_trunks, 1);

	return 0;
}

static char *bla_application_show_stations(
	struct ast_cli_entry *entry,
	int cmd,
	struct ast_cli_args *args)
{
	switch (cmd) {
		case CLI_INIT:
			entry->command = "bla show stations";
			entry->usage =
				"Usage: bla show stations\n"
				"       List the BLA stations\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;  /* Takes no arguments */
		default:
			{
				RAII_VAR(struct bla_application *, app, bla_application_singleton(), ao2_cleanup);
				/* Iterate over all of our stations */
				struct bla_station *station;
				struct ao2_iterator i;
				i = ao2_iterator_init(app->_stations, 0);
				while ((station = ao2_iterator_next(&i))) {
					/* Print information about this station */
					ast_cli(args->fd,
						"Station Name: %s\n"
						"  Device: %s\n"
						"  Trunk(s):\n",
						bla_station_name(station),
						bla_station_device(station));
					/* Iterate over this station's trunk references */
					struct ao2_iterator j;
					struct bla_trunk_ref *trunk_ref;
					/* NOTE: No choice here but to cast away const for container; don't modify anything! */
					j = ao2_iterator_init((struct ao2_container *)bla_station_trunk_refs(station), 0);
					while ((trunk_ref = ao2_iterator_next(&j))) {
						struct bla_trunk *trunk;
						trunk = bla_trunk_ref_resolve(trunk_ref, app);
						ao2_ref(trunk_ref, -1);
						/* Print terse information about this trunk */
						ast_cli(args->fd,
							"    Trunk Name: %s\n",
							bla_trunk_name(trunk));
						ao2_ref(trunk, -1);
					}
					ao2_iterator_destroy(&j);
					ao2_ref(station, -1);
				}
				ao2_iterator_destroy(&i);
			}
			return CLI_SUCCESS;
	}
}

static struct ast_cli_entry bla_application_cli[] = {
	AST_CLI_DEFINE(bla_application_show_stations,
		"List the BLA stations")
};

int bla_application_register_cli(struct bla_application *self)
{
	if (ast_cli_register_multiple(bla_application_cli, ARRAY_LEN(bla_application_cli))) {
		ast_log(LOG_ERROR, "Failed to register BLA command line interface");
		return -1;
	}

	return 0;
}

/* TODO: Pepper all error conditions in exec_station with pbx_builtin_setvar_helper(chan, "BLA_RESULT", "FAILED"); */
int bla_application_exec_station(
	struct bla_application *self,
	struct ast_channel *chan,
	const char *station_name,
	const char *trunk_name)
{
	struct bla_station *station;
	struct bla_trunk *trunk;

	ast_log(LOG_NOTICE,
		"Inside BLAStation() for station '%s'",
		station_name);

	/* Look for the station; make sure it exists */
	station = bla_application_find_station(self, station_name);
	if (station == NULL) {
		ast_log(LOG_ERROR,
			"Error executing BLAStation(): station named '%s' does not exist",
			station_name);
		pbx_builtin_setvar_helper(chan, "BLA_RESULT", "FAILED");
		return -1;
	}

	/* Check if the trunk name is empty */
	if ((trunk_name == NULL) || ast_strlen_zero(trunk_name)) {
		/* Look for an idle trunk */
		trunk = bla_station_find_idle_trunk(station, self);
		if (trunk == NULL)
		{
			ast_log(LOG_ERROR,
				"Error executing BLAStation(): no idle trunks available for station '%s'",
				station_name);
      pbx_builtin_setvar_helper(chan, "BLA_RESULT", "FAILED");
      return -1;
		}
		ast_log(LOG_NOTICE,
			"Found idle trunk '%s' for station '%s' in BLAStation()",
			bla_trunk_name(trunk), bla_station_name(station));
	} else {
		/* TODO: If the trunk name is not empty, make sure it exists and is available */
			/* TODO: If the trunk is on hold by us, take it off hold */
			/* TODO: If the trunk is not idle, make sure it has the barge option set */
		ast_log(LOG_ERROR, "FIXME: specific trunk connection hasn't been implemented for BLA");
		pbx_builtin_setvar_helper(chan, "BLA_RESULT", "FAILED");
		return -1;
	}

	/* TODO: Decide what to do with the station "off the hook" */
	/* TODO: Check for the phone on hold here? */
	/* TODO: Check if the trunk is ringing here? */

	/* FIXME: Do something if the station channel is already non-null */
	bla_station_set_channel(station, chan);

	/* Ring (and bridge) the trunk */
	/* FIXME: Check for failure to dial trunk here */
	bla_station_dial_trunk(station, trunk);

	/* Answer the station channel */
	ast_answer(bla_station_channel(station));

	/* Join the station to the trunk's bridge */
	bla_bridge_join_station(bla_trunk_bridge(trunk), station);

	/* Clean up the station channel */
	bla_station_set_channel(station, NULL);

	return 0;
}

/* TODO: Pepper all error conditions in exec_station with pbx_builtin_setvar_helper(chan, "BLA_RESULT", "FAILED"); */
int bla_application_exec_trunk(
	struct bla_application *self,
	struct ast_channel *chan,
	const char *trunk_name)
{
	struct bla_trunk *trunk;

	/* Look for the trunk; make sure it exists */
	trunk = bla_application_find_trunk(self, trunk_name);
	if (trunk == NULL) {
		ast_log(LOG_ERROR,
			"Error executing BLATrunk(): trunk named '%s' does not exist",
			trunk_name);
		pbx_builtin_setvar_helper(chan, "BLA_RESULT", "FAILED");
		return -1;
	}

	/* TODO: Decide what to do with an incoming trunk call */
	/* TODO: Make sure the trunk doesn't already have a call (i.e. check for trunk->_chan == NULL) */

	/* Associate the trunk with the incoming channel */
	bla_trunk_set_channel(trunk, chan);

	/* Start ringing stations */
	bla_application_ring_trunk_stations(self, trunk);

	/* TODO: Wait for a station to answer or for us to timeout */
	ast_safe_sleep(chan, 9000);

	return 0;
}

int bla_application_ring_trunk_stations(
	struct bla_application *self,
	struct bla_trunk *trunk)
{
	/* Iterate through all of this trunk's stations */
	struct ao2_iterator i;
	struct bla_station_ref *station_ref;
	/* NOTE: No choice here but to cast away const for container; don't modify anything! */
	i = ao2_iterator_init((struct ao2_container *)bla_trunk_station_refs(trunk), 0);
	while ((station_ref = ao2_iterator_next(&i))) {
		struct bla_station *station;
		station = bla_station_ref_resolve(station_ref, self);
		ast_log(LOG_NOTICE, "Queuing up BLA ring event for station '%s' from trunk '%s'",
			bla_station_name(station), bla_trunk_name(trunk));
		/* Queue up a ring event for each station */
		bla_event_queue_ring_station(self->_event_queue, station, trunk);
		ao2_ref(station, -1);
		ao2_ref(station_ref, -1);
	}
	ao2_iterator_destroy(&i);

	return 0;
}

/*
int bla_application_connect_station(
	struct bla_application *self,
	struct ast_channel *chan,
	const char *station_name)
	struct bla_trunk *trunk;

	trunk = bla_station_find_available_trunk(station, self);

	if (trunk == NULL) {
	}
}

int bla_application_connect_station_trunk(
	struct bla_application *self,
	struct ast_channel *chan,
	const char *station_name,
	const char *trunk_name)
{
}

int _bla_application_connect_station_trunk(
	struct bla_application *self,
)

int bla_application_connect_trunk(
	struct bla_application *self,
	struct ast_channel *chan,
	const char *trunk_name)
{
}
*/

struct bla_station *bla_application_find_station(
	struct bla_application *self,
	const char *station_name)
{
	return ao2_find(self->_stations, station_name, OBJ_SEARCH_KEY);
}

struct bla_trunk *bla_application_find_trunk(
	struct bla_application *self,
	const char *trunk_name)
{
	return ao2_find(self->_trunks, trunk_name, OBJ_SEARCH_KEY);
}
