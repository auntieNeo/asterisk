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

#include "bla_station.h"

#include "bla_trunk.h"

int bla_trunk_init(struct bla_trunk *self)
{
	self->_name = malloc(AST_MAX_CONTEXT);
	self->_name[0] = '\0';
	self->_stations = ao2_container_alloc(  /* FIXME: Make a convenience function for this */
		  1,
		  (ao2_hash_fn*)bla_station_hash,
		  (ao2_callback_fn*)bla_station_cmp);

	return 0;
}

int bla_trunk_destroy(struct bla_trunk *self)
{
	ao2_ref(self->_stations, -1);
	free(self->_name);

	return 0;
}

int bla_trunk_hash(const struct bla_trunk *self, int flags)
{
	return ast_str_hash(bla_trunk_name(self));
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
	}

	return found ? (CMP_MATCH | CMP_STOP) : 0;
}
