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
  struct ast_channel *_channel;
	struct ao2_container *_trunk_refs;
	char _name[AST_MAX_CONTEXT];
	char _device[AST_MAX_CONTEXT];
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
	strncpy((char *)self->_name, name, AST_MAX_CONTEXT);
  self->_name[AST_MAX_CONTEXT - 1] = '\0';
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

/*!
 * \brief Accessor for bla_station object's channel
 * \return Pointer to this station's channel
 *
 * This accessor function simply returns the bla_station object's channel.
 */
static force_inline struct ast_channel *bla_station_channel(const struct bla_station *self)
{
	return self->_channel;
}

/*!
 * \brief Accessor for setting bla_station object's channel
 *
 * This accessor function simply sets the bla_station object's channel.
 */
static force_inline void bla_station_set_channel(struct bla_station *self, struct ast_channel *channel)
{
  /* FIXME: Check to make sure we aren't doing something bad
   * (e.g. Don't overwrite an existing channel prematurely)
   */
  self->_channel = channel;
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

/* TODO: Add documentation for bla_station_add_trunk_ref() */
void bla_station_add_trunk_ref(struct bla_station *self, const char *trunk_name);

/*!
 * \brief Get station trunk ref with the given name
 * \retval Pointer to the bla_trunk_ref object when found
 * \retval NULL on failure to find trunk ref
 *
 * This function looks for the station's trunk ref with the given name. If such
 * a trunk ref can be found, the ao2 reference count on the trunk ref is
 * incremented and a pointer to the trunk ref is returned.
 */
struct bla_trunk_ref *bla_station_find_trunk_ref(struct bla_station *self, const char *name);


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

/*!
 * \brief Dial a trunk on behalf of this station
 * \param self Pointer to the BLA station object
 * \param station_chan Pointer to the channel connecting as this station
 * \param trunk Pointer to the BLA trunk that will be dialed
 * \param app pointer to the BLA application 
 * \retval 0 on successful 
 * \retval non-zero on failure to reach the trunk
 *
 * This function dials the given trunk on behalf of this station. This function
 * blocks for as long as the trunk is ringing. If the trunk answers the call,
 * as soon as the trunk answers it is bridged with BLA (on a new thread) and
 * this function returns.
 *
 * When this function returns zero, the caller knows that the trunk has
 * successfully bridged with BLA.
 */
int bla_station_dial_trunk(
	struct bla_station *self,
	struct bla_trunk *trunk);

int bla_station_hash(void *arg, int flags);

int bla_station_cmp(
	const struct bla_station *self,
	void *arg,
	int flags);

#endif
