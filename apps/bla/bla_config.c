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

#include "asterisk/config_options.h"

#include "bla_station.h"
#include "bla_trunk.h"

#include "bla_config.h"

static struct bla_station *bla_config_alloc_station(
	const char *category);
static struct bla_station *bla_config_find_station(
	struct ao2_container *container,
	const char *category);
static struct aco_type bla_station_type = {
	.type = ACO_ITEM,
	.name = "station",
	.category = "^general$",
	.matchfield = "type",
	.matchvalue = "station",
	.item_alloc = (aco_type_item_alloc)bla_config_alloc_station,
	.item_find = (aco_type_item_find)bla_config_find_station,
};

static struct bla_trunk *bla_config_alloc_trunk(
	const char *category);
static struct bla_trunk *bla_config_find_trunk(
	struct ao2_container *container,
	const char *category);
static struct aco_type bla_trunk_type = {
	.type = ACO_ITEM,
	.name = "trunk",
	.category = "^general$",
	.matchfield = "type",
	.matchvalue = "trunk",
	.item_alloc = (aco_type_item_alloc)bla_config_alloc_trunk,
	.item_find = (aco_type_item_find)bla_config_find_trunk,
};

static struct aco_file bla_conf = {
	.filename = "bla.conf",
	.types = ACO_TYPES(&bla_station_type, &bla_trunk_type),
};

/* We don't use static globals; provide a dummy instead */
static AO2_GLOBAL_OBJ_STATIC(dummy);
static void *bla_config_alloc_dummy(void) { return NULL; }
CONFIG_INFO_STANDARD(bla_config_info, dummy, bla_config_alloc_dummy,
	.files = ACO_FILES(&bla_conf),
);

int bla_config_init(struct bla_config *self)
{
  self->_stations = ao2_container_alloc(
		  1,
		  (ao2_hash_fn*)bla_station_hash,
		  (ao2_callback_fn*)bla_station_cmp);
  self->_trunks = ao2_container_alloc(
		  1,
		  (ao2_hash_fn*)bla_trunk_hash,
		  (ao2_callback_fn*)bla_trunk_cmp);

  return 0;
}

int bla_config_destroy(struct bla_config *self)
{
  ao2_ref(self->_trunks, -1);
  ao2_ref(self->_stations, -1);
  // TODO: assert that these refcounts are now one and not zero

  return 0;
}

int bla_config_read(struct bla_config *self)
{
	if (aco_info_init(&bla_config_info))  // FIXME: Use a local variable for bla_config_info
		return -1;

	aco_info_destroy(&bla_config_info);

	return 0;
}

static struct bla_station *bla_config_alloc_station(
	const char *category)
{
	// TODO
	return NULL;
}

static struct bla_station *bla_config_find_station(
	struct ao2_container *newcontainer,
	const char *category)
{
	// TODO
	return NULL;
}

static struct bla_trunk *bla_config_alloc_trunk(
	const char *category)
{
	// TODO
	return NULL;
}

static struct bla_trunk *bla_config_find_trunk(
	struct ao2_container *newcontainer,
	const char *category)
{
	// TODO
	return NULL;
}
