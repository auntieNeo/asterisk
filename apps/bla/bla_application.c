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
#include "asterisk/logger.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"

#include "bla_config.h"
#include "bla_station.h"
#include "bla_trunk.h"

#include "bla_application.h"

int bla_application_init(struct bla_application *self)
{
  ast_log(LOG_NOTICE, "Initializing BLA application");

	self->_stations = NULL;
	self->_trunks = NULL;

	return 0;
}

int bla_application_destroy(struct bla_application *self)
{
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

int bla_application_exec_station(
	struct bla_application *self,
	struct ast_channel *chan,
	const char *station_name,
	const char *trunk_name)
{
	struct bla_station *station;
	struct bla_trunk *trunk;

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
				"Error executing BLAStation(): no idle trunks for station '%s'",
				station_name);
		}
	} else {
		/* TODO: If the trunk name is not empty, make sure it exists and is available */
			/* TODO: If the trunk is on hold by us, take it off hold */
			/* TODO: If the trunk is not idle, make sure it has the barge option set */
	}

	/* TODO: Ring the trunk */

	/* TODO: Bridge the station and trunk channels */

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
