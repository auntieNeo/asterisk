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

#ifndef _BLA_STATION_REF_H
#define _BLA_STATION_REF_H

struct bla_station_ref {
	char *_name;
};

/*!
 * \brief Accessor for bla_station_ref object's name
 * \return bla_station_ref name as char array
 *
 * This accessor function simply returns the bla_station_ref object's name.
 */

static force_inline const char *bla_station_ref_name(const struct bla_station_ref *self)
{
	return self->_name;
}

int bla_station_ref_init(struct bla_station_ref *self);

int bla_station_ref_destroy(struct bla_station_ref *self);

int bla_station_ref_hash(void *arg, int flags);

int bla_station_ref_cmp(
	const struct bla_station_ref *self,
	void *arg,
	int flags);

#endif
