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

#include "asterisk/config.h"
#include "asterisk/config_options.h"
#include "asterisk/logger.h"

#include "bla_station.h"
#include "bla_trunk.h"
#include "bla_trunk_ref.h"

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
static int bla_config_handle_station_trunk(
	const struct aco_option *opt,
	struct ast_variable *var,
	struct bla_station *station);

static struct bla_trunk *bla_config_alloc_trunk(
	const char *category);
static struct bla_trunk *bla_config_find_trunk(
	struct ao2_container *container,
	const char *category);
static int bla_trunk_type_prelink(void *newitem);
static int bla_config_handle_trunk_internal_sample_rate(
	const struct aco_option *opt,
	struct ast_variable *var,
	struct bla_trunk *trunk);
static int bla_config_handle_trunk_mixing_interval(
	const struct aco_option *opt,
	struct ast_variable *var,
	struct bla_trunk *trunk);


static int bla_config_check_references(struct bla_config *self);

static int bla_config_pre_apply_config(void);

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

static struct aco_type bla_trunk_type = {
	.type = ACO_ITEM,
	.name = "trunk",
	.category = "^general$",
	.matchfield = "type",
	.matchvalue = "trunk",
	.item_alloc = (aco_type_item_alloc)bla_config_alloc_trunk,
	.item_find = (aco_type_item_find)bla_config_find_trunk,
	.item_offset = offsetof(struct bla_config, _trunks),
	/* FIXME: Adding item_prelink just seems to segfault */
	.item_prelink = bla_trunk_type_prelink,
};
static struct aco_type *bla_trunk_types[] = { &bla_trunk_type };

static struct aco_file bla_conf = {
	.filename = "bla.conf",
	.types = ACO_TYPES(&bla_station_type, &bla_trunk_type),
};

static AO2_GLOBAL_OBJ_STATIC(bla_global_config);

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

CONFIG_INFO_STANDARD(bla_config_info, bla_global_config, bla_config_alloc_dummy,
	.files = ACO_FILES(&bla_conf),
	.pre_apply_config = bla_config_pre_apply_config,
	.hidden = 1,  /* FIXME: This is a hack to avoid having to get the XML documentation working */
)

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

	/* BLA station options */
	aco_option_register(&bla_config_info, "type", ACO_EXACT, bla_station_types, NULL, OPT_NOOP_T, 0, 0);
	/* FIXME: This can't handle multiple trunk strings */
/*	aco_option_register(&bla_config_info, "trunk", ACO_EXACT, bla_station_types, "", OPT_CHAR_ARRAY_T, 1, CHARFLDSET(struct bla_station, _trunk)); */
	aco_option_register_custom(&bla_config_info, "trunk", ACO_EXACT, bla_station_types, "", (aco_option_handler)bla_config_handle_station_trunk, 0);
	aco_option_register(&bla_config_info, "device", ACO_EXACT, bla_station_types, "", OPT_CHAR_ARRAY_T, 1, CHARFLDSET(struct bla_station, _device));

	/* BLA trunk options */
	aco_option_register(&bla_config_info, "type", ACO_EXACT, bla_trunk_types, NULL, OPT_NOOP_T, 0, 0);
	aco_option_register(&bla_config_info, "device", ACO_EXACT, bla_trunk_types, "", OPT_CHAR_ARRAY_T, 1, CHARFLDSET(struct bla_trunk, _device));
	aco_option_register_custom(&bla_config_info, "internal_sample_rate", ACO_EXACT, bla_trunk_types, "auto", (aco_option_handler)bla_config_handle_trunk_internal_sample_rate, 0);
	aco_option_register_custom(&bla_config_info, "mixing_interval", ACO_EXACT, bla_trunk_types, "auto", (aco_option_handler)bla_config_handle_trunk_mixing_interval, 0);
	/* TODO: video_mode? */
	/* TODO: music_on_hold? */

	return 0;
}

int bla_config_destroy(struct bla_config *self)
{
	ao2_ref(self->_trunks, -1);
	ao2_ref(self->_stations, -1);
	/* TODO: Assert that these refcounts are now one and not zero */

	aco_info_destroy(&bla_config_info);

	return 0;
}

int bla_config_read(struct bla_config *self)
{
	ast_log(LOG_NOTICE, "Reading and parsing bla.conf");

	if (aco_process_config(&bla_config_info, 0) == ACO_PROCESS_ERROR)
		return -1;

	bla_config_check_references(self);

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

static int bla_config_handle_station_trunk(
	const struct aco_option *opt,
	struct ast_variable *var,
	struct bla_station *station)
{
	/* Add a trunk ref to the station */
	const char *trunk_name = var->value;
	bla_station_add_trunk_ref(station, trunk_name);

	/* NOTE: We validate the existance of this trunk _after_ parsing the
	 * entire configuration. There is no other way to resolve the name of
	 * the trunk until after all the trunks have been parsed.
	 *
	 * We also add our station references at a later time for the same
	 * reason.
	 */

	return 0;
}

static struct bla_trunk *bla_config_alloc_trunk(
	const char *category)
{
	struct bla_trunk *trunk = bla_trunk_alloc();
	/* FIXME: Who is responsible for the +1 reference count here? */

	bla_trunk_init(trunk);

	bla_trunk_set_name(trunk, category);

	return trunk;
}

static struct bla_trunk *bla_config_find_trunk(
	struct ao2_container *container,
	const char *category)
{
	return ao2_find(container, category, OBJ_SEARCH_KEY);
}

static int bla_trunk_type_prelink(void *newitem)
{
	struct bla_trunk *trunk = (struct bla_trunk *)newitem;

	/* TODO: Make sure trunk device is set (it is required) */
	if ((bla_trunk_device(trunk) == NULL) || ast_strlen_zero(bla_trunk_device(trunk))) {
		ast_log(LOG_ERROR, "Trunk device not specified for BLA trunk '%s'",
			bla_trunk_name(trunk));
		return -1;
	}

	return 0;
}

static int bla_config_handle_trunk_internal_sample_rate(
	const struct aco_option *opt,
	struct ast_variable *var,
	struct bla_trunk *trunk)
{
	const char *value = var->value;
	unsigned int sample_rate = 0;

	/* Check for special string "auto" */
	if (strcasecmp("auto", value) == 0) {
		/* The bridging API interprets zero as the default sample rate */
		sample_rate = 0;
	} else {
		/* Convert string into an unsigned integer */
		if (ast_parse_arg(value, PARSE_UINT32, &sample_rate)) {
			ast_log(LOG_ERROR, "Could not parse internal_sample_rate of '%s' for BLA trunk '%s': need unsigned integer or 'auto'",
				value, bla_trunk_name(trunk));
			return -1;
		}
	}

	bla_trunk_set_internal_sample_rate(trunk, sample_rate);

	return 0;
}

static int bla_config_handle_trunk_mixing_interval(
	const struct aco_option *opt,
	struct ast_variable *var,
	struct bla_trunk *trunk)
{
	const char *value = var->value;
	unsigned int mixing_interval = 0;

	if (strcasecmp("auto", value) == 0) {
		/* The bridging API interprets zero as the default mixing interval */
		mixing_interval = 0;
	} else {
		/* Convert string into an unsigned integer */
		if (ast_parse_arg(value, PARSE_UINT32, &mixing_interval)) {
			ast_log(LOG_ERROR, "Could not parse mixing_interval of '%s' for BLA trunk '%s': need unsigned integer or 'auto'",
				value, bla_trunk_name(trunk));
			return -1;
		}
	}

	/* Check for valid mixing_interval value */
	switch (mixing_interval) {
		case 0:
		case 10:
		case 20:
		case 40:
		case 80:
			break;
		default:
			ast_log(LOG_ERROR, "Invalid mixing_interval of '%u' for BLA trunk '%s': valid values are '10', '20', '40', '80', and 'auto'",
				mixing_interval, bla_trunk_name(trunk));
			return -1;
	}

	bla_trunk_set_mixing_interval(trunk, mixing_interval);

	return 0;
}

static int bla_config_check_references(struct bla_config *self)
{
	/* Iterate through all of the stations */
	struct ao2_iterator i;
	struct bla_station *station;
	i = ao2_iterator_init(self->_stations, 0);
	while ((station = ao2_iterator_next(&i))) {
		/* Iterate through every station's trunk references */
		struct ao2_iterator j;
		struct bla_trunk_ref *trunk_ref;
		/* NOTE: No choice here but to cast away const for container; don't modify anything! */
		j = ao2_iterator_init((struct ao2_container *)bla_station_trunk_refs(station), 0);
		while ((trunk_ref = ao2_iterator_next(&j))) {
			struct bla_trunk *trunk;
			trunk = ao2_find(
				self->_trunks,
				bla_trunk_ref_name(trunk_ref),
				OBJ_SEARCH_KEY);
			ao2_ref(trunk_ref, -1);
			if (trunk == NULL) {
				/* Found a bad trunk reference; just bail out */
				ast_log(LOG_ERROR,
					"Could not find BLA trunk '%s' for BLA station '%s'",
					bla_trunk_ref_name(trunk_ref),
					bla_station_name(station));
				return -1;
			}
			/* Since the trunk exists, give it a reference to the station */
			bla_trunk_add_station(trunk, bla_station_name(station));
		}
        	ao2_iterator_destroy(&j);
		ao2_ref(station, -1);
	}
        ao2_iterator_destroy(&i);

	return 0;
}

static int bla_config_pre_apply_config(void)
{
	struct bla_config *config = aco_pending_config(&bla_config_info);

	/* Make sure all trunk and station references check out */
	if (bla_config_check_references(config)) {
		ast_log(LOG_ERROR,
			"Error while parsing trunk/station references in BLA config");
		return -1;
	}

	return 0;
}
