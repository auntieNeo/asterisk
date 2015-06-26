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
#include "asterisk/strings.h"

#include "bla_bridge.h"
#include "bla_station.h"
#include "bla_station_ref.h"

#include "bla_trunk.h"

int bla_trunk_init(struct bla_trunk *self)
{
	ast_log(LOG_NOTICE, "Initializing BLA trunk");

	self->_channel = NULL;
	self->_bridge = NULL;
	self->_name[0] = '\0';
	self->_device[0] = '\0';
	self->_station_refs = ao2_container_alloc(
		  1,
		  (ao2_hash_fn*)bla_station_ref_hash,
		  (ao2_callback_fn*)bla_station_ref_cmp);

	/* A sample rate of zero tells the bridging API to use a reasonable default */
	self->_internal_sample_rate = 0;

	return 0;
}

int bla_trunk_destroy(struct bla_trunk *self)
{
	ao2_ref(self->_station_refs, -1);

	if (self->_bridge != NULL) {
		/* FIXME: Do we need to do anything else to destroy the bridge? */
		ao2_ref(self->_bridge, -1);
	}

	return 0;
}

void bla_trunk_add_station(struct bla_trunk *self, const char *station_name)
{
	struct bla_station_ref *station_ref;

	station_ref = bla_station_ref_alloc();
	bla_station_ref_init(station_ref);
	bla_station_ref_set_name(station_ref, station_name);

	ao2_link(self->_station_refs, station_ref);
	ao2_ref(station_ref, -1);
}

int bla_trunk_is_idle(struct bla_trunk *self)
{
	/* FIXME: Actually check if the trunk is idle here */
	return 1;
}

struct bla_bridge *bla_trunk_bridge(struct bla_trunk *self)
{
	/* FIXME: Probably need to lock the bla_trunk object here? */

	if (self->_bridge == NULL) {
		/* Initialize the bridge object */
		self->_bridge = bla_bridge_alloc();
		bla_bridge_init(self->_bridge, bla_trunk_name(self));
		/* Set the parameters for our bridge */
		ast_log(LOG_NOTICE, "Creating bridge for BLA trunk '%s' with internal sample rate '%u'",
			bla_trunk_name(self), bla_trunk_internal_sample_rate(self));
		bla_bridge_set_internal_sample_rate(self->_bridge, bla_trunk_internal_sample_rate(self));
	}

	return self->_bridge;
}

int bla_trunk_hash(void *arg, int flags)
{
	const struct bla_trunk *self = arg;
	const char *name = arg;

	switch (flags) {
		case OBJ_SEARCH_OBJECT:
			return ast_str_hash(bla_trunk_name(self));
		case OBJ_SEARCH_KEY: 
			return ast_str_hash(name);
	}

	ast_assert(0);
	return 0;
}

int bla_trunk_cmp(
	const struct bla_trunk *self,
	void *arg,
	int flags)
{
	const struct bla_trunk *other = arg;
	const char *name = arg;
	int found = 0;

	switch (flags) {
		case OBJ_SEARCH_OBJECT:
			name = bla_trunk_name(other);
		case OBJ_SEARCH_KEY:
			if (strncmp(bla_trunk_name(self), name, AST_MAX_CONTEXT) == 0)
				found = 1;
			break;
		case OBJ_SEARCH_PARTIAL_KEY:
			if (strncmp(bla_trunk_name(self), name, strnlen(name, AST_MAX_CONTEXT)) == 0)
				found = 1;
			break;
		default:
			ast_assert(0);
	}

	return found ? (CMP_MATCH | CMP_STOP) : 0;
}
