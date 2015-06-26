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

#ifndef _BLA_TRUNK_H
#define _BLA_TRUNK_H

/* Forward declarations */
struct ao2_container;
struct bla_bridge;

#include "asterisk.h"

#include "asterisk/channel.h"

struct bla_trunk {
	struct ast_channel *_channel;
	struct bla_bridge *_bridge;
	struct ao2_container *_station_refs;
	unsigned int _internal_sample_rate;
	char _name[AST_MAX_CONTEXT];
	char _device[AST_MAX_CONTEXT];
};

/*!
 * \brief Accessor for bla_trunk object's name
 * \return bla_trunk name as char array
 *
 * This accessor function simply returns the bla_trunk object's name.
 */
static force_inline const char *bla_trunk_name(const struct bla_trunk *self)
{
	return self->_name;
}

/*!
 * \brief Accessor for setting bla_trunk object's name
 *
 * This accessor function simply sets the bla_trunk object's name.
 */
static force_inline void bla_trunk_set_name(struct bla_trunk *self, const char *name)
{
	strncpy(self->_name, name, AST_MAX_CONTEXT);
	self->_name[AST_MAX_CONTEXT - 1] = '\0';
}

/*!
 * \brief Accessor for bla_trunk object's device
 * \return bla_trunk device as char array
 *
 * This accessor function simply returns the bla_trunk object's device.
 */
static force_inline const char *bla_trunk_device(const struct bla_trunk *self)
{
	return self->_device;
}

/*!
 * \brief Accessor for bla_trunk object's channel
 * \return Pointer to this trunk's channel
 *
 * This accessor function simply returns the bla_trunk object's channel.
 */
static force_inline struct ast_channel *bla_trunk_channel(const struct bla_trunk *self)
{
	return self->_channel;
}

/*!
 * \brief Accessor for setting bla_trunk object's channel
 *
 * This accessor function simply sets the bla_trunk object's channel.
 */
static force_inline void bla_trunk_set_channel(struct bla_trunk *self, struct ast_channel *channel)
{
  /* FIXME: Check to make sure we aren't doing something bad
   * (e.g. Don't overwrite an existing channel prematurely)
   */
  self->_channel = channel;
}

/*!
 * \brief Accessor for bla_trunk object's internal sample rate
 * \param self Pointer to the bla_trunk object
 * \return bla_trunk internal sample rate
 *
 * This accessor function simply returns the bla_trunk object's sample rate
 * for mixing audio channels.
 *
 * A value of zero indicates that it will use whatever default sample rate that
 * the bridging API provides.
 */
static force_inline unsigned int bla_trunk_internal_sample_rate(const struct bla_trunk *self)
{
	return self->_internal_sample_rate;
}

/*!
 * \brief Accessor for setting bla_trunk object's internal sample rate
 * \param self Pointer to the bla_trunk object
 * \param internal_sample_rate The sample rate to set for mixing audio
 *
 * This accessor function simply sets the bla_trunk object's internal sample
 * rate, which is used to configure the trunk's bridge sample rate when mixing
 * channel audio.
 *
 * A value of zero for the sample rate tells the bridging API to choose a
 * default sample rate.
 */
static force_inline void bla_trunk_set_internal_sample_rate(struct bla_trunk *self, unsigned int sample_rate)
{
	/* TODO: Make it possible to set the sample rate on the fly? For reloading configuration. */
	self->_internal_sample_rate = sample_rate;
}

int bla_trunk_init(struct bla_trunk *self);

int bla_trunk_destroy(struct bla_trunk *self);

/*!
 * \brief Allocate a bla_trunk object
 * \return Pointer to bla_trunk object allocated with ao2_alloc()
 *
 * This is a convenience function for allocating a bla_trunk object. The
 * returned bla_trunk object is an astobj2 object with one reference count on
 * it.
 */
static force_inline struct bla_trunk *bla_trunk_alloc(void)
{
	return ao2_alloc(sizeof(struct bla_trunk), (ao2_destructor_fn)bla_trunk_destroy);
}

void bla_trunk_add_station(struct bla_trunk *self, const char *station_name);

/*!
 * \brief Determine if a trunk is idle
 * \param self Pointer to the BLA trunk object
 * \retval non-zero if trunk is indeed idle
 * \retval zero if the trunk is not idle
 *
 * This function determines if the given trunk is idle (i.e. not on hold or
 * already bridged with a station).
 */
int bla_trunk_is_idle(struct bla_trunk *self);

/*!
 * \brief Dials a BLA trunk
 * \param self Pointer to the BLA trunk object
 * \retval zero when the trunk responds to the call 
 * \retval non-zero if the trunk fails to respond before a timeout is reached
 *
 * TODO: Determine if this function is a good idea after implementing it outside of a function.
 */
int bla_trunk_dial(struct bla_trunk *self);

/*!
 * \brief Accessor for bla_bridge associated with a bla_trunk object
 * \param self Pointer to the bla_trunk object
 * \return Pointer to the bla_bridge object
 *
 * This accessor function returns a pointer the bla_bridge object associated
 * with the given BLA trunk. If the BLA bridge has not been accessed before,
 * it is initialized in this function.
 */
struct bla_bridge *bla_trunk_bridge(struct bla_trunk *self);

int bla_trunk_hash(void *arg, int flags);

int bla_trunk_cmp(
	const struct bla_trunk *self,
	void *arg,
	int flags);

#endif
