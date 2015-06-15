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
#include "asterisk/utils.h"

#include "bla_station_ref.h"

int bla_station_ref_init(struct bla_station_ref *self)
{
	self->_name = ast_malloc(AST_MAX_CONTEXT);
	return 0;
};

int bla_station_ref_destroy(struct bla_station_ref *self)
{
	ast_free(self->_name);
	return 0;
};

int bla_station_ref_hash(void *arg, int flags)
{
	const struct bla_station_ref *self = arg;
	const char *name = arg;

	switch (flags) {
		case OBJ_SEARCH_OBJECT:
			return ast_str_hash(bla_station_ref_name(self));
		case OBJ_SEARCH_KEY: 
			return ast_str_hash(name);
	}

	ast_assert(0);
	return 0;
}

int bla_station_ref_cmp(
	const struct bla_station_ref *self,
	void *arg,
	int flags)
{
	const struct bla_station_ref *other = arg;
	const char *name = arg;
	int found = 0;

	switch (flags) {
		case OBJ_SEARCH_OBJECT:
			name = bla_station_ref_name(other);
		case OBJ_SEARCH_KEY:
			if (strncmp(bla_station_ref_name(self), name, AST_MAX_CONTEXT) == 0)
				found = 1;
			break;
		case OBJ_SEARCH_PARTIAL_KEY:
			if (strncmp(bla_station_ref_name(self), name, strnlen(name, AST_MAX_CONTEXT)) == 0)
				found = 1;
			break;
		default:
			ast_assert(0);
	}

	return found ? (CMP_MATCH | CMP_STOP) : 0;
}
