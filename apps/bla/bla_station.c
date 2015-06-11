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

#include "bla_trunk.h"

#include "bla_station.h"

int bla_station_init(struct bla_station *self)
{
	self->_name = malloc(AST_MAX_CONTEXT);
	self->_name[0] = '\0';
	self->_trunks = ao2_container_alloc(  /* FIXME: Make a convenience function for this */
		  1,
		  (ao2_hash_fn*)bla_trunk_hash,
		  (ao2_callback_fn*)bla_trunk_cmp);

	return 0;
}

int bla_station_destroy(struct bla_station *self)
{
	ao2_ref(self->_trunks, -1);
	free(self->_name);

	return 0;
}

int bla_station_hash(const struct bla_station *self, int flags)
{
	return ast_str_hash(bla_station_name(self));
}

int bla_station_cmp(
	const struct bla_station *self,
	const struct bla_station *other,
	int flags)
{
	if(strncmp(bla_station_name(self), bla_station_name(other), AST_MAX_CONTEXT) == 0)
		return CMP_MATCH | CMP_STOP;
	return 0;
}
