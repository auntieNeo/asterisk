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

ASTERISK_REGISTER_FILE()
#include "asterisk/config_options.h"
#include "asterisk/logger.h"

#include "bla_station.h"
#include "bla_trunk.h"

#include "bla_config.h"

/*** DOCUMENTATION
	<configInfo name="app_bla" language="en_US">
		<synopsis>Bridged Line Appearances Application</synopsis>
			<configObject name="global">
				<synopsis>Unused, but reserved.</synopsis>
			</configObject>
			<configObject name="station">
				<synopsis>A single station (typically a phone terminal) in a BLA system.</synopsis>
				<configOption name="type">
				</configOption>
			</configObject>
	</configInfo>
***/

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
	.item_offset = offsetof(struct bla_config, _stations),
};
static struct aco_type *bla_station_types[] = { &bla_station_type };

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
	.item_offset = offsetof(struct bla_config, _trunks),
};
static struct aco_type *bla_trunk_types[] = { &bla_trunk_type };

static struct aco_file bla_conf = {
	.filename = "bla.conf",
	.types = ACO_TYPES(&bla_station_type, &bla_trunk_type),
};

static AO2_GLOBAL_OBJ_STATIC(bla_global_config);

CONFIG_INFO_STANDARD(bla_config_info, bla_global_config, NULL,
	.files = ACO_FILES(&bla_conf),
)

/* The following emulates a sort of lambda pattern given only this C callback
 * from config_options.h:
 *
 * typedef void *(*aco_snapshot_alloc)(void);
 *
 * This is all so that we can allocate a local bla_config structure rather than
 * a static one. I'm not super happy about this. If config reloading is ever to
 * be implemented, this hack will need to be more clever than this, possibly
 * with locks.
 */
static struct bla_config *dummy_config = NULL;
static void *bla_config_alloc_dummy(void) {
	ast_assert(dummy_config != NULL);
	return dummy_config;
}
static aco_snapshot_alloc bla_config_get_dummy_alloc(struct bla_config *self)
{
	ast_assert(dummy_config == NULL);
	dummy_config = self;

	return bla_config_alloc_dummy;
}

int bla_config_init(struct bla_config *self)
{
	/* FIXME: Make bla_config a singleton. config_options.h is too difficult to use otherwise. */
	ast_log(LOG_NOTICE, "Initializing BLA config");

	self->_stations = ao2_container_alloc(  /* FIXME: Make a convenience function for this */
		  1,
		  (ao2_hash_fn*)bla_station_hash,
		  (ao2_callback_fn*)bla_station_cmp);
	self->_trunks = ao2_container_alloc(  /* FIXME: Make a convenience function for this */
		  1,
		  (ao2_hash_fn*)bla_trunk_hash,
		  (ao2_callback_fn*)bla_trunk_cmp);

	/* We aren't using the CONFIG_INFO_STANDARD macro directly here because
	 * it creates a static structure.
	 */
	bla_config_info.snapshot_alloc = bla_config_get_dummy_alloc(self);

	if (aco_info_init(&bla_config_info))
		return -1;

	/* FIXME: This can't handle multiple trunk strings */
/*	aco_option_register(&bla_config_info, "trunk", ACO_EXACT, bla_station_types, "", OPT_CHAR_ARRAY_T, 1, CHARFLDSET(struct bla_station, _trunk)); */

	aco_option_register(&bla_config_info, "device", ACO_EXACT, bla_trunk_types, "", OPT_CHAR_ARRAY_T, 1, CHARFLDSET(struct bla_trunk, _device));


	return 0;
}

int bla_config_destroy(struct bla_config *self)
{
	ao2_ref(self->_trunks, -1);
	ao2_ref(self->_stations, -1);
	// TODO: assert that these refcounts are now one and not zero

	aco_info_destroy(&bla_config_info);

	return 0;
}

int bla_config_read(struct bla_config *self)
{
	ast_log(LOG_NOTICE, "Reading and parsing bla.conf");

	if (aco_process_config(&bla_config_info, 0) == ACO_PROCESS_ERROR)
		return -1;

	return 0;
}

static struct bla_station *bla_config_alloc_station(
	const char *category)
{
	struct bla_station *station = bla_station_alloc();
	/* FIXME: Who is responsible for the +1 reference count here? */

	bla_station_init(station);

	bla_station_set_name(station, category);

	return station;
}

static struct bla_station *bla_config_find_station(
	struct ao2_container *container,
	const char *category)
{
	return ao2_find(container, category, OBJ_SEARCH_KEY);
}

static struct bla_trunk *bla_config_alloc_trunk(
	const char *category)
{
	struct bla_trunk *trunk = bla_trunk_alloc();
	/* FIXME: Who is responsible for the +1 reference count here? */

	bla_trunk_init(trunk);

	bla_trunk_set_name(trunk, category);

	return NULL;
}

static struct bla_trunk *bla_config_find_trunk(
	struct ao2_container *container,
	const char *category)
{
	return ao2_find(container, category, OBJ_SEARCH_KEY);
}
