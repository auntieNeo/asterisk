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

#ifndef _BLA_CONFIG_H
#define _BLA_CONFIG_H

#include "asterisk.h"

#include "asterisk/config_options.h"

struct bla_config {
	struct ao2_container *_stations;
	struct ao2_container *_trunks;
};

/*!
 * \brief Accessor for bla_config object's stations
 * \return Pointer to ao2_container holding bla_station objects
 *
 * This accessor function is used to get the list of stations read from the
 * config file; the list of stations is necessarily empty before
 * bla_config_read() is called.
 */
static force_inline struct ao2_container *bla_config_stations(const struct bla_config *self)
{
	return self->_stations;
}

/*!
 * \brief Accessor for bla_config object's trunks
 * \return Pointer to ao2_container holding bla_trunk objects
 *
 * This accessor function is used to get the list of trunks read from the
 * config file; the list of trunks is necessarily empty before
 * bla_config_read() is called.
 */
static force_inline struct ao2_container *bla_config_trunks(const struct bla_config *self)
{
	return self->_trunks;
}

int bla_config_init(struct bla_config *self);

int bla_config_destroy(struct bla_config *self);

/*!
 * \brief Allocate a bla_config object
 * \return Pointer to bla_config object allocated with ao2_alloc()
 *
 * This is a convenience function for allocating a bla_config object. The
 * returned bla_config object is an astobj2 object with one reference count on
 * it.
 */
static force_inline struct bla_config *bla_config_alloc(void)
{
	return ao2_alloc(sizeof(struct bla_config), (ao2_destructor_fn)bla_config_destroy);
}

int bla_config_read(struct bla_config *self);

#endif
