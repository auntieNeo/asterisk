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

#ifndef _BLA_STATION_H
#define _BLA_STATION_H

/* Forward declarations */
struct ao2_container;

#include "asterisk.h"

#include "asterisk/channel.h"

struct bla_station {
	struct ao2_container *_trunks;  /* Actually bla_trunk_ref's */
	char *_name;
	char *_device;
};

/*!
 * \brief Accessor for bla_station object's name
 * \return bla_station name as char array
 *
 * This accessor function simply returns the bla_station object's name.
 */
static force_inline const char *bla_station_name(const struct bla_station *self)
{
	return self->_name;
}

/*!
 * \brief Accessor for setting bla_station object's name
 *
 * This accessor function simply sets the bla_station object's name.
 */
static force_inline void bla_station_set_name(struct bla_station *self, const char *name)
{
	strncpy(self->_name, name, AST_MAX_CONTEXT);
}

int bla_station_init(struct bla_station *self);

int bla_station_destroy(struct bla_station *self);

/*!
 * \brief Allocate a bla_station object
 * \return Pointer to bla_station object allocated with ao2_alloc()
 *
 * This is a convenience function for allocating a bla_station object. The
 * returned bla_station object is an astobj2 object with one reference count on
 * it.
 */
static force_inline struct bla_station *bla_station_alloc(void)
{
	return ao2_alloc(sizeof(struct bla_station), (ao2_destructor_fn)bla_station_destroy);
}

void bla_station_add_trunk(struct bla_station *self, const char *trunk_name);

int bla_station_hash(void *arg, int flags);

int bla_station_cmp(
	const struct bla_station *self,
	const struct bla_station *other,
	int flags);

#endif
