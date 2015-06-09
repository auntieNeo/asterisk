#include "asterisk.h"

#include "include/conf_bla.h"

/* FIXME: Trim down these includes to what is actually needed */
#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/devicestate.h"
#include "asterisk/dial.h"
#include "asterisk/framehook.h"
#include "asterisk/json.h"
#include "asterisk/linkedlists.h"
#include "asterisk/lock.h"
#include "asterisk/pbx.h"
#include "asterisk/stasis.h"
#include "asterisk/stasis_bridges.h"
#include "asterisk/stasis_channels.h"
#include "asterisk/stasis_message_router.h"
#include "asterisk/strings.h"
#include "asterisk/time.h"
#include "asterisk/utils.h"

/* BLA station method implementations */
int bla_station_create(void)
{
	return 0;  // TODO
}

void bla_station_destroy(struct bla_station *self)
{
	ast_debug(1, "Destroying bla_station '%s'\n", self->name);

	if (!ast_strlen_zero(self->autocontext)) {
		struct bla_trunk_ref *trunk_ref;

		AST_LIST_TRAVERSE(&self->trunks, trunk_ref, entry) {
			char exten[AST_MAX_EXTENSION];
			char hint[AST_MAX_APP];
			snprintf(exten, sizeof(exten), "%s_%s", self->name, trunk_ref->trunk->name);
			snprintf(hint, sizeof(hint), "BLA:%s", exten);
			ast_context_remove_extension(self->autocontext, exten, 
				1, bla_registrar);
			ast_context_remove_extension(self->autocontext, hint, 
				PRIORITY_HINT, bla_registrar);
		}
	}

	bla_station_release_refs(self, NULL, 0);

	ast_string_field_free_memory(self);
}

/*!
 * The arg and flags arguments are only to satisfy the function signature for
 * the ao2_callback() function.
 *
 * FIXME: I'm not sure why this is needed in lieu of the destructor. This
 * should be calling the destructor.
 */
int bla_station_release_refs(struct bla_station *self, void *arg, int flags)
{
	struct bla_trunk_ref *trunk_ref;

	while ((trunk_ref = AST_LIST_REMOVE_HEAD(&self->trunks, entry))) {
		ao2_ref(trunk_ref, -1);
	}

	return 0;
}

int bla_station_hash(const struct bla_station *self, const int flags)
{
	return ast_str_case_hash(self->name);
}

int bla_station_cmp(const struct bla_station *self,
		const struct bla_station *arg, int flags)
{
	return !strcasecmp(self->name, arg->name) ? CMP_MATCH | CMP_STOP : 0;
}

/* BLA station_ref method implementations */
struct bla_station_ref *bla_station_ref_create(struct bla_station *station)
{
	struct bla_station_ref *self;

	if (!(self = ao2_alloc(sizeof(*self), (ao2_destructor_fn)bla_station_ref_destroy))) {
		return NULL;
	}

	ao2_ref(station, 1);
	self->station = station;

	return self;
}

void bla_station_ref_destroy(struct bla_station_ref *self)
{
	if (self->station) {
		ao2_ref(self->station, -1);
		self->station = NULL;
	}
}

/*!
 * Adds a trunk to the station.
 *
 * The var argument is a string in CSV format.
 * (FIXME: I'm not sure what the arguments are)
 */
void bla_add_trunk_to_station(struct bla_station *station, struct ast_variable *var)  // TODO
{
	RAII_VAR(struct bla_trunk *, trunk, NULL, ao2_cleanup);
	struct bla_trunk_ref *trunk_ref = NULL;
	struct bla_station_ref *station_ref = NULL;
	char *trunk_name, *options, *cur;
	int existing_trunk_ref = 0;
	int existing_station_ref = 0;

	options = ast_strdupa(var->value);
	trunk_name = strsep(&options, ",");

	trunk = bla_find_trunk(trunk_name);
	if (!trunk) {
		ast_log(LOG_ERROR, "Trunk '%s' not found!\n", var->value);
		return;
	}

	/* Un-mark existing trunks to support reload logic */
	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->trunk == trunk) {
			trunk_ref->mark = 0;
			existing_trunk_ref = 1;
			break;
		}
	}

	if (!trunk_ref && !(trunk_ref = bla_trunk_ref_create(trunk))) {
		return;  // FIXME: maybe error if we can't create a trunk ref?
	}

	trunk_ref->state = BLA_TRUNK_STATE_IDLE;

	/* Iterate over station trunk options */
	while ((cur = strsep(&options, ","))) {
		char *name, *value = cur;
		name = strsep(&value, "=");
		if (!strcasecmp(name, "ringtimeout")) {
			if (sscanf(value, "%30u", &trunk_ref->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout value '%s' for "
						"trunk '%s' on station '%s'\n", value, trunk->name, station->name);
				trunk_ref->ring_timeout = 0;
			}
		} else if (!strcasecmp(name, "ringdelay")) {
			if (sscanf(value, "%30u", &trunk_ref->ring_delay) != 1) {
				ast_log(LOG_WARNING, "Invalid ringdelay value '%s' for "
						"trunk '%s' on station '%s'\n", value, trunk->name, station->name);
				trunk_ref->ring_delay = 0;
			}
		} else {
			ast_log(LOG_WARNING, "Invalid option '%s' for "
					"trunk '%s' on station '%s'\n", name, trunk->name, station->name);
		}
	}

	/*
	 * Un-mark the corresponding reference to this station in the trunk
	 * to support reloading logic.
	 */
	AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
		if (station_ref->station == station) {
			station_ref->mark = 0;
			existing_station_ref = 1;
			break;
		}
	}

	/*
	 * FIXME: This is confusing. Need to document how all this reference counting
	 * works. Maybe just remove the reload logic if it's too hairy.
	 */
	if (!station_ref && !(station_ref = bla_station_ref_create(station))) {
		if (!existing_trunk_ref) {
			ao2_ref(trunk_ref, -1);
		} else {
			trunk_ref->mark = 1;
		}
		return;
	}

	if (!existing_station_ref) {
		ao2_lock(trunk);
		AST_LIST_INSERT_TAIL(&trunk->stations, station_ref, entry);
		ast_atomic_fetchadd_int((int *) &trunk->num_stations, 1);
		ao2_unlock(trunk);
	}

	if (!existing_trunk_ref) {
		ao2_lock(station);
		AST_LIST_INSERT_TAIL(&station->trunks, trunk_ref, entry);
		ao2_unlock(station);
	}
}


