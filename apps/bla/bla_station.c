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
#include "asterisk/logger.h"
#include "asterisk/strings.h"

#include "bla_trunk.h"
#include "bla_trunk_ref.h"

#include "bla_station.h"

int bla_station_init(struct bla_station *self)
{
	ast_log(LOG_NOTICE, "Initializing BLA station");

	self->_name = malloc(AST_MAX_CONTEXT);
	self->_name[0] = '\0';
	self->_trunk_refs = ao2_container_alloc(
		  1,
		  (ao2_hash_fn*)bla_trunk_ref_hash,
		  (ao2_callback_fn*)bla_trunk_ref_cmp);

	return 0;
}

int bla_station_destroy(struct bla_station *self)
{
	ao2_ref(self->_trunk_refs, -1);
	free(self->_name);

	return 0;
}

void bla_station_add_trunk(struct bla_station *self, const char *trunk_name)
{
	struct bla_trunk_ref *trunk_ref;

	trunk_ref = bla_trunk_ref_alloc();
	bla_trunk_ref_init(trunk_ref);
	bla_trunk_ref_set_name(trunk_ref, trunk_name);

	ao2_link(self->_trunk_refs, trunk_ref);
	ao2_ref(trunk_ref, -1);
}

struct bla_trunk *bla_station_find_idle_trunk(struct bla_station *self, struct bla_application *app)
{
	struct bla_trunk *result = NULL;

	/* Iterate over this station's trunks */
	struct bla_trunk_ref *trunk_ref;
	struct ao2_iterator i;
	i = ao2_iterator_init(self->_trunk_refs, 0);
	while ((trunk_ref = ao2_iterator_next(&i))) {
		struct bla_trunk *trunk = bla_trunk_ref_deref(trunk_ref, app);
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
	const struct bla_station *other,
	int flags)
{
	if(strncmp(bla_station_name(self), bla_station_name(other), AST_MAX_CONTEXT) == 0)
		return CMP_MATCH | CMP_STOP;
	return 0;
}
