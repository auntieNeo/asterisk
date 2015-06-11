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

struct bla_config {
	struct ao2_container *_stations;
	struct ao2_container *_trunks;
};

int bla_config_init(struct bla_config *self);

int bla_config_destroy(struct bla_config *self);

int bla_config_read(struct bla_config *self);

#endif
