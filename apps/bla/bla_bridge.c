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

#include "asterisk/bridge.h"
#include "asterisk/channel.h"

#include "bla_station.h"
#include "bla_trunk.h"

#include "bla_bridge.h"

int bla_bridge_init(struct bla_bridge *self, const char *name)
{
	strncpy(self->_name, name, AST_MAX_CONTEXT);
	self->_name[AST_MAX_CONTEXT - 1] = '\0';

	/* TODO: Initialize the bridge? Glancing at the API I'm not sure there's much to do... */
	return 0;
}

int bla_bridge_destroy(struct bla_bridge *self)
{
	/* TODO: Destroy the bridge? */
	return 0;
}

int bla_bridge_join_trunk(struct bla_bridge *self, struct bla_trunk *trunk)
{
	struct ast_channel *chan; 

	/* BLA does not support joining trunks to any other bridges (as of yet) */
	ast_assert(self == &trunk->_bridge);

	/* Make sure the trunk's channel is valid */
	if ((chan = bla_trunk_channel(trunk)) == NULL) {
		ast_log(LOG_ERROR, "BLA trunk '%s' failed to join bridge; trunk channel not connected",
			bla_trunk_name(trunk));
	}
	
	/* Join the trunk's channel to the bridge */
	ast_log(LOG_NOTICE, "Joining BLA trunk '%s' to bridge",
		bla_trunk_name(trunk));
	ast_bridge_join(&self->_bridge, bla_trunk_channel(trunk), NULL,
		NULL, NULL, 0);
	ast_log(LOG_NOTICE, "BLA trunk '%s' left bridge",
		bla_trunk_name(trunk));
	
	return 0;
}

int bla_bridge_join_station(struct bla_bridge *self, struct bla_station *station)
{
	struct ast_channel *chan; 

	/* TODO: Make sure the station's channel is valid */
	if ((chan = bla_station_channel(station)) == NULL) {
		ast_log(LOG_ERROR, "BLA station '%s' failed to join BLA bridge '%s': station channel not connected",
			bla_station_name(station), bla_bridge_name(self));
	}
	
	/* Join the station's channel to the bridge */
	ast_log(LOG_NOTICE, "Joining BLA station '%s' to bridge",
		bla_station_name(station));
	ast_bridge_join(&self->_bridge, bla_station_channel(station), NULL,
		NULL, NULL, 0);
	ast_log(LOG_NOTICE, "BLA station '%s' left bridge",
		bla_station_name(station));

	return 0;
}
