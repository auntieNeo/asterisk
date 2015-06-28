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

#ifndef _BLA_APPLICATION_H
#define _BLA_APPLICATION_H

/* Forward declarations */
struct ast_channel;
struct bla_event_queue;
struct bla_trunk;

#include "asterisk.h"

#include "asterisk/astobj2.h"

struct bla_application {
	struct ao2_container *_stations;
	struct ao2_container *_trunks;
	struct bla_event_queue *_event_queue;
};

/*!
 * \brief Access bla_application singleton object
 * \returns Pointer to the bla_application singleton
 *
 * This function gets a pointer to the bla_application singleton object. The
 * bla_application_singleton_create() function must be called sometime before
 * calling this function.
 *
 * The reference count on the bla_application singleton is incremented by this
 * function, so callers are responsible for freeing that reference when they are
 * done using the singleton.
 */
struct bla_application *bla_application_singleton(void);

/*!
 * \brief Create the bla_application singleton object
 * \retval 0 on success
 * \retval non-zero on failure
 *
 * This function allocates and initializese the bla_application singleton
 * object. This function must be called before calling
 * bla_application_singleton().
 */
int bla_application_singleton_create(void);

/*!
 * \brief Release the reference on bla_application singleton object
 *
 * This function releases the global reference on the bla_application singleton
 * object so that it can be destroyed. bla_application_singleton() should not be
 * called after the global reference is released.
 *
 * This function only affects the global reference; other references to the
 * bla_application singleton will persist.
 */
void bla_application_singleton_release(void);

/*!
 * \brief Initializes a BLA application
 * \param self Pointer to the BLA application object
 *
 * This function initializes the internal structures of a BLA application
 * object.
 */
int bla_application_init(struct bla_application *self);

/*!
 * \brief BLA application destructor
 * \param self Pointer to the BLA application object
 *
 * The BLA application destructor decrements the reference counts of key
 * objects to signal their destruction. Some care is needed because of the
 * presence of some circular references.
 */
int bla_application_destroy(struct bla_application *self);

/*!
 * \brief Allocate a BLA application
 * \return Pointer to BLA application object allocated with ao2_alloc()
 *
 * This is a convenience function for allocating a BLA application. The
 * returned BLA application object is an astobj2 object with one reference
 * count on it.
 */
static force_inline struct bla_application *bla_application_alloc(void)
{
	return ao2_alloc(sizeof(struct bla_application), (ao2_destructor_fn)bla_application_destroy);
}

/*!
 * \brief Read config for BLA application
 * \param self Pointer to the BLA application object
 * \retval 0 on success
 * \retval non-zero on failure
 *
 * This function reads the config file "bla.conf" for the app_bla.so
 * application, and sets the BLA application's state if the config file was
 * read and parsed successfully.
 */
int bla_application_read_config(struct bla_application *self);

/*!
 * \brief Register BLA's CLI commands
 * \param self Pointer to the bla_application object
 * \retval 0 on success
 * \retval non-zero on failure
 *
 * This function registers the various app_bla CLI commands to the Asterisk CLI
 * command API.
 */
int bla_application_register_cli(struct bla_application *self);

/* TODO: Document bla_application_exec_station() */
int bla_application_exec_station(
	struct bla_application *self,
	struct ast_channel *chan,
	const char *station_name,
	const char *trunk_name);

/*!
 * \brief Entry point from BLATrunk() dialplan call into BLA application
 * \param self Pointer to the BLA application object
 * \param chan Pointer to the channel of the incoming trunk
 * \param trunk_name Name of the trunk
 * \retval 0 on success
 * \retval non-zero on failure
 *
 * TODO: Document bla_application_exec_trunk()
 */
int bla_application_exec_trunk(
	struct bla_application *self,
	struct ast_channel *chan,
	const char *trunk_name);

/* TODO: Document bla_application_ring_trunk_stations() */
/* TODO: Move bla_application_ring_trunk_stations to internal linkage */
int bla_application_ring_trunk_stations(
	struct bla_application *self,
	struct bla_trunk *trunk);

/*!
 * \brief Connect the given BLA station to any available trunk
 * \param self Pointer to the BLA application object
 * \param chan Channel of the connecting station
 * \param station_name Name of the connecting station
 * \retval 0 on success
 * \retval non-zero on failure
 *
 * This function selects a trunk from the trunks available to the given station
 * and connects the station to that trunk. The function blocks while ringing the
 * trunk, bridging the trunk's channel with the station's channel, and until
 * the station hangs up.
 *
 * This function corresponds with the BLAStation() application called with a
 * station parameter but without a trunk parameter.
 *
 * \sa bla_application_connect_station_trunk()
 */
int bla_application_connect_station(
	struct bla_application *self,
	struct ast_channel *chan,
	const char *station_name);

int bla_application_connect_station_trunk(
	struct bla_application *self,
	struct ast_channel *chan,
	const char *station_name,
	const char *trunk_name);

int bla_application_connect_trunk(
	struct bla_application *self,
	struct ast_channel *chan,
	const char *trunk_name);

/*!
 * \brief Find the BLA station with the given name
 * \param self Pointer to the BLA application object
 * \param station_name Name of the station to look for
 * \retval Pointer to the BLA station object on success
 * \retval NULL on failure
 *
 * Finds the BLA station with the given station name. If a station with that
 * name is found, the returned value is a pointer to that station. The
 * reference count on the ao2 object is increased by one, as in the behavior of
 * the ao2_find() function.
 */
struct bla_station *bla_application_find_station(
	struct bla_application *self,
	const char *station_name);

/*!
 * \brief Find the BLA trunk with the given name
 * \param self Pointer to the BLA application object
 * \param trunk_name Name of the trunk to look for
 * \retval Pointer to the BLA trunk object on success
 * \retval NULL on failure
 *
 * Finds the BLA trunk with the given trunk name. If a trunk with that
 * name is found, the returned value is a pointer to that trunk. The
 * reference count on the ao2 object is increased by one, as in the behavior of
 * the ao2_find() function.
 */
struct bla_trunk *bla_application_find_trunk(
	struct bla_application *self,
	const char *trunk_name);

/*!
 * \brief Accessor for bla_application object's event queue
 * \param self Pointer to the bla_application object
 * \return Pointer to the bla_event_queue object
 *
 * This accessor function returns a pointer to the BLA application's event queue
 * object.
 */
static force_inline struct bla_event_queue *bla_application_event_queue(
	struct bla_application *self)
{
	return self->_event_queue;
}

#endif
