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

#include "asterisk.h"

#include "asterisk/astobj2.h"

#include "bla_station.h"
#include "bla_trunk.h"

#include "bla_application.h"

int bla_application_init(struct bla_application *self)
{
  self->_stations = ao2_container_alloc(1, (ao2_hash_fn*)bla_station_hash, (ao2_callback_fn*)bla_station_cmp);
  self->_trunks = ao2_container_alloc(1, (ao2_hash_fn*)bla_trunk_hash, (ao2_callback_fn*)bla_trunk_cmp);

  return 0;
}

int bla_application_destroy(struct bla_application *self)
{
  ao2_ref(self->_trunks, -1);
  ao2_ref(self->_stations, -1);
  // TODO: assert that these refcounts are now zero

  return 0;
}

int bla_application_read_config(struct bla_application *self)
{
  // TODO
  return 0;
}
