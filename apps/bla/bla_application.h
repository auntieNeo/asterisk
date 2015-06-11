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

#include "asterisk/astobj2.h"

struct bla_application {
	struct ao2_container *_stations;
	struct ao2_container *_trunks;
	struct bla_event_queue *_event_queue;
};

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

int bla_application_exec_station(struct bla_application *self, struct ast_channel *chan);

int bla_application_exec_trunk(struct bla_application *self, struct ast_channel *chan);

#endif
