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
struct ast_dial;
struct bla_application;

#include "asterisk.h"

#include "asterisk/channel.h"

struct bla_station {
	struct ast_channel *_channel;
	struct ast_dial *_dial;
	struct ao2_container *_trunk_refs;
	char _name[AST_MAX_CONTEXT];

	/* NOTE: _device_string is actually a character buffer that holds both
	 * _tech and _device. The '/' separator is set to '\0' with strsep().
	 */
	char _device_string[AST_MAX_CONTEXT];  /* FIXME: I'm not sure AST_MAX_CONTEXT is the length limit for attributes... */
	char *_tech;
	char *_device;
};

/*!
 * \brief Accessor for bla_station object's name
 * \param self Pointer to the bla_station object
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
 * \param self Pointer to the bla_station object
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

/*!
 * \brief Accessor for setting bla_station object's device string
 * \param self Pointer to the bla_station object
 * \param device_string Character array to copy for the station's device string
 *
 * This accessor function sets the bla_station object's device string.
 *
 * The device string is the string specified for the 'device' attribute of
 * stations in the bla.conf file. It is a combination of the station tech and
 * station device separated by a '/' (forward-slash) character.
 *
 * Use the bla_station_tech() and bla_station_device() functions to split the
 * device string and get the tech and device of the station respectively.
 */
static force_inline void bla_station_set_device_string(
	struct bla_station *self, const char *device_string)
{
	/* Copy the entire device string into our buffer */
	/* FIXME: I'm not sure AST_MAX_CONTEXT is the length limit for attributes... */
	strncpy(self->_device_string, device_string, AST_MAX_CONTEXT);
	self->_device_string[AST_MAX_CONTEXT - 1] = '\0';

	/* Split the device string buffer into tech and device */
	self->_device = self->_device_string;
	self->_tech = strsep(&self->_device, "/");
}

/*!
 * \brief Accessor for bla_station object's tech
 * \param self Pointer to the bla_station object
 * \return The station's tech as a character array
 *
 * This accessor function return's the tech (e.g. SIP, Local, IAX) that BLA
 * uses to connect to this station. The tech is split from the station's device
 * string, which must be set by bla_station_set_device_string().
 */
static force_inline const char *bla_station_tech(
	const struct bla_station *self)
{
	return self->_tech;
}

/*!
 * \brief Accessor for bla_station object's device
 * \param self Pointer to the bla_station object
 * \return The station's device as a character array
 *
 * This accessor function return's the device that BLA uses to connect to this
 * station (e.g. 'station3' in 'SIP/station3'). The device is split from the
 * station's device string, which must be set by
 * bla_station_set_device_string().
 */
static force_inline const char *bla_station_device(
	const struct bla_station *self)
{
	return self->_device;
}

/*!
 * \brief Accessor for bla_station object's dial handle
 * \param self Pointer to the bla_station object
 * \return Pointer to the bla_station object's ast_dial object or NULL
 *
 * This accessor function returns a pointer to the ast_dial object, which is
 * a handle for the dialing to the station in progress. If the station is not
 * being dialed at the moment, then this function returns NULL.
 */
/* FIXME: I'm not sure I want to store bla_dial with the station; I might not need it */
static force_inline struct ast_dial *bla_station_get_dial(const struct bla_station *self)
{
	return self->_dial;
}

/*!
 * \brief Accessor for setting bla_station object's dial handle
 * \param self Pointer to the bla_station object
 * \param dial Pointer to the ast_dial object to set as the handle
 *
 * This accessor function sets the station's handle to the current dialing in
 * progress to the station.
 */
static force_inline void bla_station_set_dial(struct bla_station *self, struct ast_dial *dial)
{
	/* Make sure we aren't overwriting an existing dial handle */
	/* FIXME: How do I set the dial handle to NULL? This might be silly. */
	ast_assert(!bla_station_is_ringing(self));

	self->_dial = dial;
}

/*!
 * \brief Initialize a bla_station object
 * \param self Pointer to the bla_station object to initialize
 *
 * This function initializes the internal structures of a bla_station object.
 *
 * The bla_station object should be allocated with the bla_station_alloc()
 * function before calling this function.
 */
int bla_station_init(struct bla_station *self);

/*!
 * \brief Destroy a bla_station object
 * \param self Pointer to the bla_station object to destroy
 *
 * This function cleans up the internal structures of a bla_station object.
 *
 * Note that the memory for the bla_station structure itself is not freed; if
 * the structure was allocated with bla_station_alloc(), then that memory is
 * managed by astobj2's reference counter.
 */
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

int bla_station_handle_ring_event(
	struct bla_station *self,
	struct bla_trunk *trunk,
	struct timeval timestamp);

int bla_station_handle_dial_state_event(
	struct bla_station *self,
	struct bla_trunk *trunk,
	struct ast_dial *dial,
	struct timeval timestamp);

int bla_station_ring(
	struct bla_station *self,
	struct bla_trunk *trunk);

int bla_station_is_busy(struct bla_station *self);

int bla_station_is_ringing(struct bla_station *self);

int bla_station_is_failed(struct bla_station *self);

int bla_station_is_cooldown(struct bla_station *self);

int bla_station_is_timeout(struct bla_station *self, struct bla_trunk *trunk);

void bla_station_stop_ringing(struct bla_station *self);

int bla_station_hash(void *arg, int flags);

/*!
 * \brief Answer the trunk on behalf of this station
 * \param self Pointer to the bla_station object
 * \param trunk Pointer to the bla_trunk that initiated the call
 *
 * This function spawns a thread that answers the trunk channel, notifies the
 * trunk's thread, and joins the station's channel to the trunk's bridge.
 *
 * This function is non-blocking; everything is done in the spawned thread.
 */
int bla_station_answer_trunk(
	struct bla_station *self,
	struct bla_trunk *trunk);

int bla_station_cmp(
	const struct bla_station *self,
	void *arg,
	int flags);

#endif
