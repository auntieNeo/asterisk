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
struct bla_application;

#include "asterisk.h"

#include "asterisk/channel.h"

struct bla_station {
	struct ao2_container *_trunk_refs;
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

/*!
 * \brief Accessor for bla_station object's trunk references
 * \return ao2_container of trunk_ref objects
 *
 * This accessor function returns a pointer to the container holding this
 * station's trunk references. This pointer should not be stored and the
 * container should not be modified directly.
 */
static force_inline const struct ao2_container *bla_station_trunk_refs(const struct bla_station *self)
{
	return self->_trunk_refs;
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

/*!
 * \brief Find a trunk that is idle on this station
 * \param self Pointer to the BLA station object
 * \param app Pointer to the BLA application object (used to resolve trunk names)
 * \retval Pointer to an idle bla_trunk on success
 * \retval NULL on failure to find suitable trunk
 *
 * This function uses various criteria (order of the trunks assigned to this
 * station, which trunks are not in use, etc.) to determine the best possible
 * trunk for this station to connect to. If no suitable trunks are found.
 */
struct bla_trunk *bla_station_find_idle_trunk(
	struct bla_station *self,
	struct bla_application *app);

int bla_station_hash(void *arg, int flags);

int bla_station_cmp(
	const struct bla_station *self,
	const struct bla_station *other,
	int flags);

#endif
