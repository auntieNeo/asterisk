#include "asterisk.h"

#include "asterisk/app.h"
#include "asterisk/astobj2.h"
#include "asterisk/cli.h"
#include "asterisk/config.h"
#include "asterisk/devicestate.h"
#include "asterisk/dial.h"
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

#include "include/confbridge.h"

#include "include/conf_bla.h"

/*** DOCUMENTATION
	<managerEvent language="en_US" name="ConfbridgeBLARinging">
		<managerEventInstance class="EVENT_FLAG_CALL">
			<synopsis>Raised when a BLA trunk starts ringing.</synopsis>
			<syntax>
				<parameter name="Trunk">
					<para>The name of the BLA trunk.</para>
				</parameter>
			</syntax>
			<see-also>
				<ref type="application">ConfBridge</ref>
			</see-also>
		</managerEventInstance>
	</managerEvent>
***/

/* Bridged Line Appearances (BLA) implementation */

/* TODO:
 * [X] Implement BLA event processing thread
 * [X] Implement ringing stations
 * [ ] Implement autocontext and friends
 * [ ] Implement (or don't implement) reloading
 * [X] Implement bla cli
 *   [X] Implement bla_show_stations()
 *   [X] Implement bla_show_trunks()
 * [ ] Add a lot of debugging statements
 * [ ] Figure out what sla_queue_event_conf() does and implement that for BLA
 * [ ] Test ringing stations
 * [ ] Clean up the TODO/FIXME tags
 * [ ] Document EVERYTHING
 */

/* BLA Application Strings */
const char bla_station_app[] = "BLAStation";
const char bla_trunk_app[] = "BLATrunk";

/* BLA enums */
enum bla_hold_access {
	/*! This means that any station can put it on hold, and any station
	 * can retrieve the call from hold. */
	BLA_HOLD_OPEN,
	/*! This means that only the station that put the call on hold may
	 * retrieve it from hold. */
	BLA_HOLD_PRIVATE,
};

/*! \brief Event types that can be queued up for the BLA thread */
enum bla_event_type {
	/*! A station has put the call on hold */
	BLA_EVENT_HOLD,
	/*! The state of a dial has changed */
	BLA_EVENT_DIAL_STATE,
	/*! The state of a ringing trunk has changed */
	BLA_EVENT_RINGING_TRUNK,
};

struct bla_event {
	enum bla_event_type type;
	struct bla_station *station;
	struct bla_trunk_ref *trunk_ref;
	AST_LIST_ENTRY(bla_event) entry;
};

enum bla_trunk_state {
	BLA_TRUNK_STATE_IDLE,
	BLA_TRUNK_STATE_RINGING,
	BLA_TRUNK_STATE_UP,
	BLA_TRUNK_STATE_ONHOLD,
	BLA_TRUNK_STATE_ONHOLD_BYME,
};

enum bla_which_trunk_refs {
	ALL_TRUNK_REFS,
	INACTIVE_TRUNK_REFS,
};

enum bla_station_hangup {
	BLA_STATION_HANGUP_NORMAL,
	BLA_STATION_HANGUP_TIMEOUT,
};

/* BLA internal structures */
struct bla_station_ref;

struct bla_trunk {
	AST_DECLARE_STRING_FIELDS(
			AST_STRING_FIELD(name);
			AST_STRING_FIELD(device);
			AST_STRING_FIELD(autocontext);	
			AST_STRING_FIELD(trunk_user_profile);
			AST_STRING_FIELD(station_user_profile);
			AST_STRING_FIELD(bridge_profile);
			);
	AST_LIST_HEAD_NOLOCK(, bla_station_ref) stations;
	/*! Number of stations that use this trunk */
	unsigned int num_stations;
	/*! Number of stations currently on a call with this trunk */
	unsigned int active_stations;
	/*! Number of stations that have this trunk on hold. */
	unsigned int hold_stations;
	struct ast_channel *chan;
	unsigned int ring_timeout;
	/*! If set to 1, no station will be able to join an active call with
	 *  this trunk. */
	unsigned int barge_disabled:1;
	/*! This option uses the values in the bla_hold_access enum and sets the
	 * access control type for hold on this trunk. */
	unsigned int hold_access:1;
	/*! Whether this trunk is currently on hold, meaning that once a station
	 *  connects to it, the trunk channel needs to have UNHOLD indicated to it. */
	unsigned int on_hold:1;
	/*! Mark used during reload processing */
	unsigned int mark:1;
	/*! The Bridge Configuration Profile for this trunk */
	struct bridge_profile b_profile;
};

/*!
 * \brief A station's reference to a trunk
 *
 * An bla_station keeps a list of trunk_refs.  This holds metadata about the
 * stations usage of the trunk.
 */
struct bla_trunk_ref {
	AST_LIST_ENTRY(bla_trunk_ref) entry;
	struct bla_trunk *trunk;
	enum bla_trunk_state state;
	struct ast_channel *chan;
	/*! Ring timeout to use when this trunk is ringing on this specific
	 *  station.  This takes higher priority than a ring timeout set at
	 *  the station level. */
	unsigned int ring_timeout;
	/*! Ring delay to use when this trunk is ringing on this specific
	 *  station.  This takes higher priority than a ring delay set at
	 *  the station level. */
	unsigned int ring_delay;
	/*! Mark used during reload processing */
	unsigned int mark:1;
};

/*! \brief A trunk that is ringing */
struct bla_ringing_trunk {
	struct bla_trunk *trunk;
	/*! The time that this trunk started ringing */
	struct timeval ring_begin;
	AST_LIST_HEAD_NOLOCK(, bla_station_ref) timed_out_stations;
	AST_LIST_ENTRY(bla_ringing_trunk) entry;
};

struct bla_station {
	AST_RWLIST_ENTRY(bla_station) entry;
	AST_DECLARE_STRING_FIELDS(
			AST_STRING_FIELD(name);	
			AST_STRING_FIELD(device);	
			AST_STRING_FIELD(autocontext);	
			AST_STRING_FIELD(user_profile);
			);
	AST_LIST_HEAD_NOLOCK(, bla_trunk_ref) trunks;
	struct ast_dial *dial;
	/*! Ring timeout for this station, for any trunk.  If a ring timeout
	 *  is set for a specific trunk on this station, that will take
	 *  priority over this value. */
	unsigned int ring_timeout;
	/*! Ring delay for this station, for any trunk.  If a ring delay
	 *  is set for a specific trunk on this station, that will take
	 *  priority over this value. */
	unsigned int ring_delay;
	/*! This option uses the values in the bla_hold_access enum and sets the
	 * access control type for hold on this station. */
	unsigned int hold_access:1;
	/*! Mark used during reload processing */
	unsigned int mark:1;
	/*! User Configuration Profile for this station */
	struct user_profile u_profile;
};

/*!
 * \brief A reference to a station
 *
 * This struct looks near useless at first glance.  However, its existence
 * in the list of stations in bla_trunk means that this station references
 * that trunk.  We use the mark to keep track of whether it needs to be
 * removed from the bla_trunk's list of stations during a reload.
 */
struct bla_station_ref {
	AST_LIST_ENTRY(bla_station_ref) entry;
	struct bla_station *station;
	/*! Mark used during reload processing */
	unsigned int mark:1;
};

/*! \brief A station that is ringing */
struct bla_ringing_station {
	struct bla_station *station;
	/*! The time that this station started ringing */
	struct timeval ring_begin;
	AST_LIST_ENTRY(bla_ringing_station) entry;
};

/*! \brief A station that failed to be dialed 
 * \note Only used by the BLA thread. */
struct bla_failed_station {
	struct bla_station *station;
	struct timeval last_try;
	AST_LIST_ENTRY(bla_failed_station) entry;
};

/*!
 * \brief A structure for data used by the bla thread
 */
static struct {
	pthread_t thread;
	ast_cond_t cond;
	ast_mutex_t lock;
	AST_LIST_HEAD_NOLOCK(, bla_ringing_trunk) ringing_trunks;
	AST_LIST_HEAD_NOLOCK(, bla_ringing_station) ringing_stations;
	AST_LIST_HEAD_NOLOCK(, bla_failed_station) failed_stations;
	AST_LIST_HEAD_NOLOCK(, bla_event) event_q;
	unsigned int stop:1;
	/*! Attempt to handle CallerID, even though it is known not to work
	 *  properly in some situations. */
	/* FIXME: Caller id should be enabled by default */
	unsigned int attempt_callerid:1;
} bla = {
	.thread = AST_PTHREADT_NULL,
};

struct bla_dial_trunk_args {
	struct bla_trunk_ref *trunk_ref;
	struct bla_station *station;
	ast_mutex_t *cond_lock;
	ast_cond_t *cond;
};

struct bla_run_station_args {
	struct bla_station *station;
	struct bla_trunk_ref *trunk_ref;
	ast_mutex_t *cond_lock;
	ast_cond_t *cond;
};

/* BLA variables */
static struct ao2_container *bla_stations;
static struct ao2_container *bla_trunks;

static const char bla_registrar[] = "BLA";

/* Static BLA Function Prototypes */
static int bla_build_trunk(struct ast_config *cfg, const char *cat);
static int bla_build_station(struct ast_config *cfg, const char *cat);
/* BLA trunk methods */
static int bla_trunk_create(void);
static void bla_trunk_destroy(struct bla_trunk *self);
static int bla_trunk_release_refs(struct bla_trunk *self, void *arg, int flags);
static int bla_trunk_hash(const struct bla_trunk *self, const int flags);
static int bla_trunk_cmp(const struct bla_trunk *self, const struct bla_trunk *arg, int flags);
/* BLA trunk_ref methods */
static struct bla_trunk_ref *bla_trunk_ref_create(struct bla_trunk *trunk);
static void bla_trunk_ref_destroy(struct bla_trunk_ref *self);
/* BLA station methods */
static int bla_station_create(void);
static void bla_station_destroy(struct bla_station *self);
static int bla_station_release_refs(struct bla_station *self, void *arg, int flags);
static int bla_station_hash(const struct bla_station *self, const int flags);
static int bla_station_cmp(const struct bla_station *self, const struct bla_station *arg, int flags);
/* BLA station_ref methods */
static struct bla_station_ref *bla_station_ref_create(struct bla_station *station);
static void bla_station_ref_destroy(struct bla_station_ref *self);
/* BLA helper functions */
/* TODO: Many of these "helper" functions could be class methods. Need to move these to a better object model. */
/* FIXME: I think I got these all out of order. Oh well. */
int bla_in_use(void);
static enum ast_device_state bla_state_to_devstate(enum bla_trunk_state state);
static int bla_check_device(const char *dev);
static int bla_check_station_hold_access(const struct bla_trunk *trunk, const struct bla_station *station);
static struct bla_ringing_trunk *bla_queue_ringing_trunk(struct bla_trunk *trunk);
static struct bla_station *bla_find_station(const char *name);
static struct bla_trunk *bla_find_trunk(const char *name);
static struct bla_trunk_ref *bla_choose_idle_trunk(struct bla_station *station);
static struct bla_trunk_ref *bla_find_trunk_ref_byname(const struct bla_station *station, const char *name);
static void bla_add_trunk_to_station(struct bla_station *self, struct ast_variable *var);
static void bla_change_trunk_state(const struct bla_trunk *trunk, enum bla_trunk_state state, enum bla_which_trunk_refs inactive_only, const struct bla_trunk_ref *exclude);
static void bla_queue_event(enum bla_event_type type);
static void bla_queue_event_full(enum bla_event_type type, struct bla_trunk_ref *trunk_ref, struct bla_station *station, int lock);
static void bla_queue_event_nolock(enum bla_event_type type);
static void bla_answer_trunk_chan(struct ast_channel *chan);
static void bla_ring_stations(void);
static int bla_check_inuse_station(const struct bla_station *station);
static int bla_check_failed_station(const struct bla_station *station);
static int bla_check_ringing_station(const struct bla_station *station);
static int bla_check_timed_out_station(const struct bla_ringing_trunk *ringing_trunk, const struct bla_station *station);
static int bla_check_station_delay(struct bla_station *station, struct bla_ringing_trunk *ringing_trunk);
static int bla_ring_station(struct bla_ringing_trunk *ringing_trunk, struct bla_station *station);
static struct bla_failed_station *bla_create_failed_station(struct bla_station *station);
static void bla_failed_station_destroy(struct bla_failed_station *failed_station);
static struct bla_ringing_trunk *bla_choose_ringing_trunk(struct bla_station *station, struct bla_trunk_ref **trunk_ref, int rm);
static struct bla_trunk_ref *bla_find_trunk_ref(const struct bla_station *station, const struct bla_trunk *trunk);
static void bla_dial_state_callback(struct ast_dial *dial);
static struct bla_ringing_station *bla_create_ringing_station(struct bla_station *station);
static void bla_ringing_station_destroy(struct bla_ringing_station *ringing_station);
static void bla_event_destroy(struct bla_event *event);
static void bla_stop_ringing_trunk(struct bla_ringing_trunk *ringing_trunk);
static void bla_ringing_trunk_destroy(struct bla_ringing_trunk *ringing_trunk);
static void bla_stop_ringing_station(struct bla_ringing_station *ringing_station, enum bla_station_hangup hangup);
static struct bla_station_ref *bla_create_station_ref(struct bla_station *station);
static void bla_station_ref_destructor(struct bla_station_ref *station_ref);
static void bla_station_user_profile_name(const struct bla_station *station, const struct bla_trunk *trunk, char *user_profile_name);
static void bla_trunk_user_profile_name(const struct bla_trunk *trunk, char *user_profile_name);
static void bla_trunk_bridge_profile_name(const struct bla_trunk *trunk, char *bridge_profile_name);
static void bla_trunk_conference_name(const struct bla_trunk *trunk, char *conference_name);
static void bla_hangup_stations(void);
/* BLA Thread Callback Prototypes */
static void *bla_dial_trunk(struct bla_dial_trunk_args *args);
static void *bla_run_station(struct bla_run_station_args *args);
static void *bla_thread(void *data);
/* BLA Event Function Prototypes */
static int bla_process_timers(struct timespec *ts);
static int bla_calc_trunk_timeouts(unsigned int *timeout);
static int bla_calc_station_timeouts(unsigned int *timeout);
static int bla_calc_station_delays(unsigned int *timeout);
static void bla_handle_hold_event(struct bla_event *event);
static void bla_handle_dial_state_event(void);
static void bla_handle_ringing_trunk_event(void);
/* BLA CLI Function Prototypes */
static const char *bla_hold_access_str(enum bla_hold_access hold_access);
static const char *bla_trunk_state_str(enum bla_trunk_state state);
/* BLA Stasis Debugging Prototypes */
struct stasis_message_type *bla_ringing_type(void);
static void bla_publish_manager_event(struct stasis_message *message, const char *event, struct ast_str *extra_text);
static void bla_publish_manager_event(struct stasis_message *message, const char *event, struct ast_str *extra_text);
static void bla_ringing_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message);
static void bla_send_stasis(struct stasis_message_type *type, struct ast_json *extras);
static void bla_send_ringing_ami_event(struct bla_trunk *trunk);

/*!
 * \breif Load and parse the BLA config file (bla.conf)
 *
 * This also initializes any BLA resources that are needed. The corresponding
 * function that frees BLA resources is bla_destroy().
 */
int bla_load_config(int reload)
{
	struct ast_config *cfg;
	struct ast_flags config_flags = { reload ? CONFIG_FLAG_FILEUNCHANGED : 0 };
	const char *cat = NULL;
	int res = 0;

	ast_debug(1, "DELETEME: nix is working");

	if (!reload) {
		/* Initialize the bla structure */
		/* FIXME: Initialization of bla should happen in a constructor function */
		ast_mutex_init(&bla.lock);
		ast_cond_init(&bla.cond, NULL);
		/* TODO: Add typedefs here for readability */
		bla_trunks = ao2_container_alloc(1, (int (*)(const void *, int))bla_trunk_hash, (int (*)(void *, void*, int))bla_trunk_cmp);
		bla_stations = ao2_container_alloc(1, (int (*)(const void *, int))bla_station_hash, (int (*)(void *, void*, int))bla_station_cmp);
	}

	if (!(cfg = ast_config_load(BLA_CONFIG_FILE, config_flags))) {
		return 0;  /* Treat no config as normal */
	} else if (cfg == CONFIG_STATUS_FILEUNCHANGED) {
		return 0;
	} else if (cfg == CONFIG_STATUS_FILEINVALID) {
		ast_log(LOG_ERROR, "Config file " BLA_CONFIG_FILE " is in an invalid format.  Aborting.\n");
		return 1;
	}

	if (reload) {
		// TODO: mark the trunks and stations for deletion in a callback
	}

	// TODO: check for "attemptcallerid" setting
	// (NOTE: might not be needed; callerid should be working by default)

	while ((cat = ast_category_browse(cfg, cat)) && !res) {
		const char *type;
		if (!strcasecmp(cat, "general"))
			continue;
		if (!(type = ast_variable_retrieve(cfg, cat, "type"))) {
			ast_log(LOG_WARNING, "Invalid entry in %s defined with no type!\n",
					BLA_CONFIG_FILE);
			continue;
		}
		if (!strcasecmp(type, "trunk"))
			res = bla_build_trunk(cfg, cat);  // TODO: build trunk
		else if (!strcasecmp(type, "station"))
			res = bla_build_station(cfg, cat);  // TODO: build station
		else {
			ast_log(LOG_WARNING, "Entry in %s defined with invalid type '%s'!\n",
					BLA_CONFIG_FILE, type);
		}
	}

	ast_config_destroy(cfg);

	if (reload) {
		// TODO: check that trunks/stations are still marked... if so, delete them?
	}

	/* Start BLA event processing thread now that everything has been configured */
	if (bla.thread == AST_PTHREADT_NULL && bla_in_use()) {
		ast_pthread_create(&bla.thread, NULL, bla_thread, NULL);
	}

	return res;
}

void bla_destroy(void) // TODO
{
	ast_debug(1, "Cleaning up BLA");

	/* FIXME: Destruction of bla structure should happen in a destructor routine */

	/* Stop and join the event thread */
	if (bla.thread != AST_PTHREADT_NULL) {
		ast_mutex_lock(&bla.lock);
		bla.stop = 1;
		ast_cond_signal(&bla.cond);
		ast_mutex_unlock(&bla.lock);
		pthread_join(bla.thread, NULL);
	}

	/* TODO: Drop any contexts we created from the dialplan */

	ast_mutex_destroy(&bla.lock);
	ast_cond_destroy(&bla.cond);

	/* Destroy objects stored in containers */
	ao2_callback(bla_trunks, 0, (ao2_callback_fn*)bla_trunk_release_refs, NULL);
	ao2_callback(bla_stations, 0, (ao2_callback_fn*)bla_station_release_refs, NULL);
	/* Destroy containers */
	ao2_ref(bla_trunks, -1);
	bla_trunks = NULL;
	ao2_ref(bla_stations, -1);
	bla_stations = NULL;
}

enum {
	BLA_TRUNK_OPT_MOH = (1 << 0),
};

enum {
	BLA_TRUNK_OPT_ARG_MOH_CLASS = 0,
	BLA_TRUNK_OPT_ARG_ARRAY_SIZE,
};

AST_APP_OPTIONS(bla_trunk_opts, BEGIN_OPTIONS
		AST_APP_OPTION_ARG('M', BLA_TRUNK_OPT_MOH, BLA_TRUNK_OPT_ARG_MOH_CLASS),
		END_OPTIONS );

/*! \breif Called when BLATrunk application is invoked in dialplan */
int bla_trunk_exec(struct ast_channel *chan, const char *data)
{
	char conf_name[MAX_CONF_NAME];
	char user_profile_name[MAX_PROFILE_NAME];
	char bridge_profile_name[MAX_PROFILE_NAME];
	RAII_VAR(struct bla_trunk *, trunk, NULL, ao2_cleanup);
	struct bla_ringing_trunk *ringing_trunk;
	AST_DECLARE_APP_ARGS(args,
			AST_APP_ARG(trunk_name);
			AST_APP_ARG(options);
			);
	char *opts[BLA_TRUNK_OPT_ARG_ARRAY_SIZE] = { NULL, };
	struct ast_flags opt_flags = { 0 };
	char *parse;

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "The BLATrunk application requires the trunk name as an argument\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);
	if (args.argc == 2) {
		if (ast_app_parse_options(bla_trunk_opts, &opt_flags, opts, args.options)) {
			ast_log(LOG_ERROR, "Error parsing options for BLATrunk\n");
			return -1;
		}
	}

	/* FIXME: do we need to check that trunk_name was provided? */
	trunk = bla_find_trunk(args.trunk_name);

	if (!trunk) {
		ast_log(LOG_ERROR, "BLA Trunk '%s' not found!\n", args.trunk_name);
		pbx_builtin_setvar_helper(chan, "BLATRUNK_STATUS", "FAILURE");
		return 0;  /* FIXME: why do we return zero here? */
	}

	/* FIXME: I thought the idea was that channels can be mixed... why doesn't this work? Can this be supported? */
	if (trunk->chan) {
		ast_log(LOG_ERROR, "Call came in on '%s', but the trunk is already in use!\n",
				args.trunk_name);
		pbx_builtin_setvar_helper(chan, "BLATRUNK_STATUS", "FAILURE");
		return 0;  /* FIXME: why do we return zero here? */
	}

	trunk->chan = chan;

	/* FIXME: Document what these ringing trunks are */
	if (!(ringing_trunk = bla_queue_ringing_trunk(trunk))) {
		pbx_builtin_setvar_helper(chan, "BLATRUNK_STATUS", "FAILURE");
		return 0;  /* FIXME: why do we return zero here? */
	}

	/* Find the bridge profile, user profile, and conference names */
	/* These determine the properties of the conference we join/create */
	bla_trunk_conference_name(trunk, conf_name);
	bla_trunk_user_profile_name(trunk, user_profile_name);
	bla_trunk_bridge_profile_name(trunk, bridge_profile_name);

	/* FIXME: Need some option to ring with MOH here
	   if (ast_test_flag(&opt_flags, SLA_TRUNK_OPT_MOH)) {
	   ast_indicate(chan, -1);
	   ast_set_flag64(&conf_flags, CONFFLAG_MOH);
	   } else
	   ast_indicate(chan, AST_CONTROL_RINGING);
	 */

	/* Actually join the conference */
	ast_debug(1, "Joining the conference in BLATrunk() '%s' thread.",
			trunk->name);
	/* FIXME: Do we need to check the return status of confbridge_init_and_join?
	 * It should handle its own errors just fine... */
	confbridge_init_and_join(trunk->chan,
			conf_name,
			user_profile_name,
			bridge_profile_name,
			NULL);  /* FIXME: Do we really need a menu profile? */

	/* Clean up now that we've left the conference */
	trunk->chan = NULL;
	trunk->on_hold = 0;
	bla_change_trunk_state(trunk, BLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);

	if (!pbx_builtin_getvar_helper(chan, "BLATRUNK_STATUS"))
		pbx_builtin_setvar_helper(chan, "BLATRUNK_STATUS", "SUCCESS");

	/* Remove the entry from the list of ringing trunks if it is still there. */
	ast_mutex_lock(&bla.lock);
	AST_LIST_TRAVERSE_SAFE_BEGIN(&bla.ringing_trunks, ringing_trunk, entry) {
		if (ringing_trunk->trunk == trunk) {
			AST_LIST_REMOVE_CURRENT(entry);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
	ast_mutex_unlock(&bla.lock);
	if (ringing_trunk) {
		bla_ringing_trunk_destroy(ringing_trunk);
		pbx_builtin_setvar_helper(chan, "BLATRUNK_STATUS", "UNANSWERED");
		/* Queue reprocessing of ringing trunks to make stations stop ringing
		 * that shouldn't be ringing after this trunk stopped. */
		bla_queue_event(BLA_EVENT_RINGING_TRUNK);
	}

	return 0;
}

/*! \breif Called when BLAStation application is invoked in dialplan */
int bla_station_exec(struct ast_channel *chan, const char *data)
{
	char *station_name, *trunk_name;
	RAII_VAR(struct bla_station *, station, NULL, ao2_cleanup);
	RAII_VAR(struct bla_trunk_ref *, trunk_ref, NULL, ao2_cleanup);
	char conf_name[MAX_CONF_NAME];
	char user_profile_name[MAX_PROFILE_NAME];
	char bridge_profile_name[MAX_PROFILE_NAME];

	ast_debug(3, "Entering BLAStation() application");

	if(!ast_channel_get_up_time(chan)) {
		ast_debug(3, "The channel '%s' has not been answered yet!", ast_channel_name(chan));
	}

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "Invalid Arguments to BLAStation!\n");
		pbx_builtin_setvar_helper(chan, "BLASTATION_STATUS", "FAILURE");
		return 0;
	}

	trunk_name = ast_strdupa(data);
	station_name = strsep(&trunk_name, "_");

	if (ast_strlen_zero(station_name)) {
		ast_log(LOG_WARNING, "Invalid Arguments to BLAStation!\n");
		pbx_builtin_setvar_helper(chan, "BLASTATION_STATUS", "FAILURE");
		return 0;
	}

	ast_debug(3, "Looking for station '%s'", station_name);
	station = bla_find_station(station_name);

	if (!station) {
		ast_log(LOG_WARNING, "Station '%s' not found!\n", station_name);
		pbx_builtin_setvar_helper(chan, "BLASTATION_STATUS", "FAILURE");
		return 0;
	}

	ao2_lock(station);
	if (!ast_strlen_zero(trunk_name)) {
		ast_debug(3, "Looking for trunk '%s' on station '%s'", trunk_name, station->name);
		trunk_ref = bla_find_trunk_ref_byname(station, trunk_name);
	} else {
		ast_debug(3, "Looking for any idle trunk on station '%s'", station->name);
		/* No trunk name after underscore; get idle trunk from station */
		trunk_ref = bla_choose_idle_trunk(station);
	}
	ao2_unlock(station);

	if (!trunk_ref) {
		if (ast_strlen_zero(trunk_name))
			ast_log(LOG_NOTICE, "No trunks available for call.\n");
		else {
			ast_log(LOG_NOTICE, "Can't join existing call on trunk "
					"'%s' due to access controls.\n", trunk_name);
		}
		pbx_builtin_setvar_helper(chan, "BLASTATION_STATUS", "CONGESTION");
		return 0;
	}

	/* Determine what to do now that the phone is "off the hook":
	 * If the trunk is on hold, we take it off hold.
	 * If the trunk is ringing, we answer it.
	 * If the trunk is not ringing, then we need to dial out.
	 */
	if (trunk_ref->state == BLA_TRUNK_STATE_ONHOLD_BYME) {
		/* FIXME: Document onhold trunks */
		if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->hold_stations) == 1)
			bla_change_trunk_state(trunk_ref->trunk, BLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
		else {
			trunk_ref->state = BLA_TRUNK_STATE_UP;
			ast_devstate_changed(AST_DEVICE_INUSE, AST_DEVSTATE_CACHABLE,
					"BLA:%s_%s", station->name, trunk_ref->trunk->name);
		}
	} else if (trunk_ref->state == BLA_TRUNK_STATE_RINGING) {
		/* Answer a ringing trunk */
		struct bla_ringing_trunk *ringing_trunk;

		ast_mutex_lock(&bla.lock);
		AST_LIST_TRAVERSE_SAFE_BEGIN(&bla.ringing_trunks, ringing_trunk, entry) {
			if (ringing_trunk->trunk == trunk_ref->trunk) {
				AST_LIST_REMOVE_CURRENT(entry);
				break;
			}
		}
		AST_LIST_TRAVERSE_SAFE_END;
		ast_mutex_unlock(&bla.lock);

		if (ringing_trunk) {
			bla_answer_trunk_chan(ringing_trunk->trunk->chan);
			bla_change_trunk_state(ringing_trunk->trunk, BLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);

			bla_ringing_trunk_destroy(ringing_trunk);

			/* Queue up reprocessing ringing trunks, and then ringing stations again */
			bla_queue_event(BLA_EVENT_RINGING_TRUNK);
			bla_queue_event(BLA_EVENT_DIAL_STATE);
		}
	}

	trunk_ref->chan = chan;

	/* FIXME: This thread should be created in a routine */
	if (!trunk_ref->trunk->chan) {
		ast_mutex_t cond_lock;
		ast_cond_t cond;
		pthread_t thread;
		struct bla_dial_trunk_args args = {
			.trunk_ref = trunk_ref,
			.station = station,
			.cond_lock = &cond_lock,
			.cond = &cond,
		};
		ao2_ref(trunk_ref, 1);
		ao2_ref(station, 1);
		bla_change_trunk_state(trunk_ref->trunk, BLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
		/* Create a thread to dial the trunk and dump it into the conference.
		 * However, we want to wait until the trunk has been dialed and the
		 * conference is created before continuing on here. */
		ast_autoservice_start(chan);  /* FIXME: I'm not clear on why this is needed... */
		ast_mutex_init(&cond_lock);
		ast_cond_init(&cond, NULL);
		ast_mutex_lock(&cond_lock);
		ast_debug(3, "Starting bla_dial_trunk() thread for trunk '%s'", trunk_ref->trunk->name);
		ast_pthread_create_detached_background(&thread, NULL, (void*(*)(void*))bla_dial_trunk, &args);
		ast_debug(3, "Waiting for trunk '%s' thread...", trunk_ref->trunk->name);
		ast_cond_wait(&cond, &cond_lock);
		ast_debug(3, "Finished waiting for trunk '%s' thread", trunk_ref->trunk->name);
		ast_mutex_unlock(&cond_lock);
		ast_mutex_destroy(&cond_lock);
		ast_cond_destroy(&cond);
		ast_autoservice_stop(chan);
		if (!trunk_ref->trunk->chan) {
			/* FIXME: Something is causing chan to be 0 here */
			ast_debug(1, "Trunk didn't get created. chan: %lx\n", (unsigned long) trunk_ref->trunk->chan);
			pbx_builtin_setvar_helper(chan, "BLASTATION_STATUS", "CONGESTION");
			bla_change_trunk_state(trunk_ref->trunk, BLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
			trunk_ref->chan = NULL;
			return 0;
		}
	}

	/* FIXME: What does this do? */
	if (ast_atomic_fetchadd_int((int *) &trunk_ref->trunk->active_stations, 1) == 0 &&
			trunk_ref->trunk->on_hold) {
		trunk_ref->trunk->on_hold = 0;
		ast_indicate(trunk_ref->trunk->chan, AST_CONTROL_UNHOLD);
		bla_change_trunk_state(trunk_ref->trunk, BLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
	}

	/* Find the bridge profile, user profile, and conference names */
	/* These determine the properties of the conference we join/create */
	bla_trunk_conference_name(trunk_ref->trunk, conf_name);
	bla_station_user_profile_name(station, trunk_ref->trunk, user_profile_name);
	bla_trunk_bridge_profile_name(trunk_ref->trunk, bridge_profile_name);

	/* Answer the station's channel */
	ast_answer(chan);

	/* Actually join the conference.
	 * It should already be created by the trunk thread. */
	ast_debug(3, "Station '%s' is joining conference '%s'", station->name, conf_name);
	confbridge_init_and_join(chan,
			conf_name,
			user_profile_name,
			bridge_profile_name,
			NULL);  /* FIXME: Bridge profile here? */

	/* TODO: Clean up now that we've left the conference */
	trunk_ref->chan = NULL;
	if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->active_stations) &&
			trunk_ref->state != BLA_TRUNK_STATE_ONHOLD_BYME) {
		/* TODO: Kick everyone from the channel? */
		//		strncat(conf_name, ",K", sizeof(conf_name) - strlen(conf_name) - 1);
		//		admin_exec(NULL, conf_name);  // FIXME: This is the MeetMeAdmin() application. Commands are passed with the channel name.
		trunk_ref->trunk->hold_stations = 0;
		bla_change_trunk_state(trunk_ref->trunk, BLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
	}

	ast_debug(1, "Made it all the way down here.");

	// TODO: many things...
	return 0;
}

static int bla_build_trunk(struct ast_config *cfg, const char *cat)
{
	RAII_VAR(struct bla_trunk *, trunk, NULL, ao2_cleanup);
	struct ast_variable *var;
	const char *dev;
	int existing_trunk = 0;
	struct user_profile u_profile;
	struct bridge_profile b_profile;

	ast_debug(1, "Building BLA trunk");

	/* Make sure the "device" argument is defined and valid */
	if (!(dev = ast_variable_retrieve(cfg, cat, "device"))) {
		ast_log(LOG_ERROR, "BLA Trunk '%s' defined with no device!\n", cat);
		return -1;
	}
	if (bla_check_device(dev)) {
		ast_log(LOG_ERROR, "BLA Trunk '%s' defined with invalid device '%s'!\n",
				cat, dev);
		return -1;
	}

	if ((trunk = bla_find_trunk(cat))) {
		trunk->mark = 0;
		existing_trunk = 1;
	} else if ((trunk = ao2_alloc(sizeof(*trunk), (ao2_destructor_fn)bla_trunk_destroy))) {
		if (ast_string_field_init(trunk, 32)) {
			return -1;
		}
		ast_string_field_set(trunk, name, cat);
	} else {
		return -1;
	}

	ao2_lock(trunk);

	ast_string_field_set(trunk, device, dev);

	/* iterate over the config variables */
	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "autocontext"))
			ast_string_field_set(trunk, autocontext, var->value);
		else if (!strcasecmp(var->name, "ringtimeout")) {
			if (sscanf(var->value, "%30u", &trunk->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout '%s' specified for trunk '%s'\n",
						var->value, trunk->name);
				trunk->ring_timeout = 0;
			}
		} else if (!strcasecmp(var->name, "barge"))
			trunk->barge_disabled = ast_false(var->value);
		else if (!strcasecmp(var->name, "hold")) {
			if (!strcasecmp(var->value, "private"))
				trunk->hold_access = BLA_HOLD_PRIVATE;
			else if (!strcasecmp(var->value, "open"))
				trunk->hold_access = BLA_HOLD_OPEN;
			else {
				ast_log(LOG_WARNING, "Invalid value '%s' for hold on trunk %s\n",
						var->value, trunk->name);
			}
		} else if (!strcasecmp(var->name, "user_profile")) {
			/* Look for the user profile for users dialing into this trunk
       * through the BLATrunk() application
			 * (it must have been specified in confbridge.conf) */
			if (!(conf_find_user_profile(NULL, var->value, &u_profile))) {
				ast_log(LOG_WARNING, "Nonexistant user_profile '%s' specified for trunk %s\n",
						var->value, trunk->name);
			} else {
				ast_string_field_set(trunk, trunk_user_profile, var->value);
				ast_debug(3, "Set user_profile to '%s' for trunk '%s'",
						var->value, trunk->name);
			}
		} else if (!strcasecmp(var->name, "bridge_profile")) {
			/* Look for the bridge profile
			 * (it must have been specified in confbridge.conf) */
			if (!(conf_find_bridge_profile(NULL, var->value, &b_profile))) {
				ast_log(LOG_WARNING, "Nonexistant bridge_profile '%s' specified for trunk %s\n",
						var->value, trunk->name);
			} else {
				ast_string_field_set(trunk, bridge_profile, var->value);
				ast_debug(3, "Set bridge_profile to '%s' for trunk '%s'",
						var->value, trunk->name);
			}
		}
		else if (strcasecmp(var->name, "type") && strcasecmp(var->name, "device")) {
			ast_log(LOG_ERROR, "Invalid option '%s' specified at line %d of %s!\n",
					var->name, var->lineno, BLA_CONFIG_FILE);
		}
	}

  /* TODO: Give warning if default user/bridge profile is specified (i.e. null string) but not defined */

	ao2_unlock(trunk);

	if (!ast_strlen_zero(trunk->autocontext)) {
		struct ast_context *context;
		context = ast_context_find_or_create(NULL, NULL, trunk->autocontext, bla_registrar);
		if (!context) {
			ast_log(LOG_ERROR, "Failed to automatically find or create "
				"context '%s' for BLA!\n", trunk->autocontext);
			return -1;
		}
		/* Extension for calls coming in on this line.
		 * exten => s,1,BLATrunk(line1) */
		if (ast_add_extension2(context, 0 /* don't replace */, "s", 1,
			NULL, NULL, bla_trunk_app, ast_strdup(trunk->name), ast_free_ptr, bla_registrar)) {
			ast_log(LOG_ERROR, "Failed to automatically create extension "
				"for trunk '%s'!\n", trunk->name);
			return -1;
		}
	}

	/* increment the reference count on bla_trunks (if we need to) */
	if (!existing_trunk) {
		ao2_link(bla_trunks, trunk);
	}

	return 0;
}

static int bla_build_station(struct ast_config *cfg, const char *cat)
{
	RAII_VAR(struct bla_station *, station, NULL, ao2_cleanup);
	struct ast_variable *var;
	const char *dev;
	int existing_station = 0;
	struct user_profile u_profile;

	ast_debug(1, "Building BLA station");

	if (!(dev = ast_variable_retrieve(cfg, cat, "device"))) {
		ast_log(LOG_ERROR, "BLA Station '%s' defined with no device!\n", cat);
		return -1;
	}

	if ((station = bla_find_station(cat))) {
		station->mark = 0;
		existing_station = 1;
	} else if ((station = ao2_alloc(sizeof(*station), (ao2_destructor_fn)bla_station_destroy))) {
		if (ast_string_field_init(station, 32)) {
			return -1;
		}
		ast_string_field_set(station, name, cat);
	} else {
		return -1;
	}

	ao2_lock(station);

	ast_string_field_set(station, device, dev);

	/* Loop to add variables to station */
	for (var = ast_variable_browse(cfg, cat); var; var = var->next) {
		if (!strcasecmp(var->name, "trunk")) {
			ao2_unlock(station);
			/* Loop to add comma separated trunk options */
			bla_add_trunk_to_station(station, var);
			ao2_lock(station);
		} else if (!strcasecmp(var->name, "autocontext")) {
			ast_string_field_set(station, autocontext, var->value);
		} else if (!strcasecmp(var->name, "ringtimeout")) {
			if (sscanf(var->value, "%30u", &station->ring_timeout) != 1) {
				ast_log(LOG_WARNING, "Invalid ringtimeout '%s' specified for station '%s'\n",
						var->value, station->name);
				station->ring_timeout = 0;
			}
		} else if (!strcasecmp(var->name, "hold")) {
			if (!strcasecmp(var->value, "private"))
				station->hold_access = BLA_HOLD_PRIVATE;
			else if (!strcasecmp(var->value, "open"))
				station->hold_access = BLA_HOLD_OPEN;
			else {
				ast_log(LOG_WARNING, "Invalid value '%s' for hold on station %s\n",
						var->value, station->name);
			}
		} else if (!strcasecmp(var->name, "user_profile")) {
			/* Look for the user profile
			 * (it must have been specified in confbridge.conf) */
			if(!(conf_find_user_profile(NULL, var->value, &u_profile))) {
				ast_log(LOG_WARNING, "Nonexistant user_profile '%s' specified for station %s\n",
						var->value, station->name);
			} else {
				ast_string_field_set(station, user_profile, var->value);
				ast_debug(3, "Set user_profile to '%s' for station '%s'",
						var->value, station->name);
			}
		} else if (strcasecmp(var->name, "type") && strcasecmp(var->name, "device")) {
			ast_log(LOG_ERROR, "Invalid option '%s' specified at line %d of %s!\n",
					var->name, var->lineno, BLA_CONFIG_FILE);
		}
	}

  /* TODO: Give warning if default user profile is specified (i.e. null string) but not defined */

	ao2_unlock(station);

	if (!ast_strlen_zero(station->autocontext)) {
		struct ast_context *context;
		struct bla_trunk_ref *trunk_ref;
		context = ast_context_find_or_create(NULL, NULL, station->autocontext, bla_registrar);
		if (!context) {
			ast_log(LOG_ERROR, "Failed to automatically find or create "
				"context '%s' for BLA!\n", station->autocontext);
			return -1;
		}
		/* The extension for when the handset goes off-hook.
		 * exten => station1,1,BLAStation(station1) */
		if (ast_add_extension2(context, 0 /* don't replace */, station->name, 1,
			NULL, NULL, bla_station_app, ast_strdup(station->name), ast_free_ptr, bla_registrar)) {
			ast_log(LOG_ERROR, "Failed to automatically create extension "
				"for station '%s'!\n", station->name);
			return -1;
		}
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			char exten[AST_MAX_EXTENSION];
			char hint[AST_MAX_APP];
			snprintf(exten, sizeof(exten), "%s_%s", station->name, trunk_ref->trunk->name);
			snprintf(hint, sizeof(hint), "BLA:%s", exten);
			/* Extension for this line button 
			 * exten => station1_line1,1,SLAStation(station1_line1) */
			if (ast_add_extension2(context, 0 /* don't replace */, exten, 1,
				NULL, NULL, bla_station_app, ast_strdup(exten), ast_free_ptr, bla_registrar)) {
				ast_log(LOG_ERROR, "Failed to automatically create extension "
					"for station '%s'!\n", station->name);
				return -1;
			}
			/* FIXME: figure out what this does, and write a test to check that it works */
			/* Hint for this line button 
			 * exten => station1_line1,hint,SLA:station1_line1 */
			if (ast_add_extension2(context, 0 /* don't replace */, exten, PRIORITY_HINT,
				NULL, NULL, hint, NULL, NULL, bla_registrar)) {
				ast_log(LOG_ERROR, "Failed to automatically create hint "
					"for station '%s'!\n", station->name);
				return -1;
			}
		}
	}

	if (!existing_station) {
		ao2_link(bla_stations, station);
	}

	return 0;
}

/* BLA trunk method implementations */
static int bla_trunk_create(void)
{
	return 0;  // TODO
}

static void bla_trunk_destroy(struct bla_trunk *self)
{
	ast_debug(1, "Destroying bla_trunk '%s'\n", self->name);

  if (!ast_strlen_zero(self->autocontext)) {
    ast_context_remove_extension(self->autocontext, "s", 1, bla_registrar);
  }

	bla_trunk_release_refs(self, NULL, 0);

	ast_string_field_free_memory(self);
}

/*!
 * The arg and flags arguments are only to satisfy the function signature for
 * the ao2_callback() function.
 *
 * FIXME: I'm not sure why this is needed in lieu of the destructor. This
 * should be calling the destructor.
 */
static int bla_trunk_release_refs(struct bla_trunk *self, void *arg, int flags)
{
	struct bla_station_ref *station_ref;

	while ((station_ref = AST_LIST_REMOVE_HEAD(&self->stations, entry))) {
		ao2_ref(station_ref, -1);
	}

	return 0;
}

static int bla_trunk_hash(const struct bla_trunk *self, const int flags)
{
	return ast_str_case_hash(self->name);
}

static int bla_trunk_cmp(const struct bla_trunk *self,
		const struct bla_trunk *arg, int flags)
{
	return !strcasecmp(self->name, arg->name) ? CMP_MATCH | CMP_STOP : 0;
}

/* BLA trunk_ref method implementations */
static struct bla_trunk_ref *bla_trunk_ref_create(struct bla_trunk *trunk)
{
	struct bla_trunk_ref *self;

	if (!(self = ao2_alloc(sizeof(*self), (ao2_destructor_fn)bla_trunk_ref_destroy))) {
		return NULL;
	}

	ao2_ref(trunk, 1);
	self->trunk = trunk;

	return self;
}

static void bla_trunk_ref_destroy(struct bla_trunk_ref *self)
{
	if (self->trunk) {
		ao2_ref(self->trunk, -1);
		self->trunk = NULL;
	}
}

/* BLA station method implementations */
static int bla_station_create(void)
{
	return 0;  // TODO
}

static void bla_station_destroy(struct bla_station *self)
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
static int bla_station_release_refs(struct bla_station *self, void *arg, int flags)
{
	struct bla_trunk_ref *trunk_ref;

	while ((trunk_ref = AST_LIST_REMOVE_HEAD(&self->trunks, entry))) {
		ao2_ref(trunk_ref, -1);
	}

	return 0;
}

static int bla_station_hash(const struct bla_station *self, const int flags)
{
	return ast_str_case_hash(self->name);
}

static int bla_station_cmp(const struct bla_station *self,
		const struct bla_station *arg, int flags)
{
	return !strcasecmp(self->name, arg->name) ? CMP_MATCH | CMP_STOP : 0;
}

/* BLA station_ref method implementations */
static struct bla_station_ref *bla_station_ref_create(struct bla_station *station)
{
	struct bla_station_ref *self;

	if (!(self = ao2_alloc(sizeof(*self), (ao2_destructor_fn)bla_station_ref_destroy))) {
		return NULL;
	}

	ao2_ref(station, 1);
	self->station = station;

	return self;
}

static void bla_station_ref_destroy(struct bla_station_ref *self)
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
static void bla_add_trunk_to_station(struct bla_station *station, struct ast_variable *var)  // TODO
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

static void bla_change_trunk_state(const struct bla_trunk *trunk,
		enum bla_trunk_state state, enum bla_which_trunk_refs inactive_only,
		const struct bla_trunk_ref *exclude)
{
	struct bla_station *station;
	struct bla_trunk_ref *trunk_ref;
	struct ao2_iterator i;

	i = ao2_iterator_init(bla_stations, 0);
	while ((station = ao2_iterator_next(&i))) {
		ao2_lock(station);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (trunk_ref->trunk != trunk || (inactive_only ? trunk_ref->chan : 0)  /* FIXME: The "bla_which_trunk_refs" enum is used poorly here. All non-zero values are assumed to be inactive. */
					|| trunk_ref == exclude) {
				continue;
			}
			trunk_ref->state = state;
			ast_devstate_changed(bla_state_to_devstate(state), AST_DEVSTATE_CACHABLE,
					"BLA:%s_%s", station->name, trunk->name);
			break;
		}
		ao2_unlock(station);
		ao2_ref(station, -1);
	}
	ao2_iterator_destroy(&i);
}

/*!
 * Returns 0 if the given string is a valid device string.
 * Returns non-zero otherwise.
 */
static int bla_check_device(const char *dev)
{
	char *tech, *tech_data;

	tech_data = ast_strdupa(dev);
	tech = strsep(&tech_data, "/");

	if (ast_strlen_zero(tech) || ast_strlen_zero(tech_data))
		return -1;

	return 0;
}

static int bla_check_station_hold_access(const struct bla_trunk *trunk,
		const struct bla_station *station)
{
	const struct bla_station_ref *station_ref;
	const struct bla_trunk_ref *trunk_ref;

	/* FIXME: Not sure I understand this. Needs testing. */
	/* For each station that has this call on hold, check for private hold. */
	AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
		AST_LIST_TRAVERSE(&station_ref->station->trunks, trunk_ref, entry) {
			if (trunk_ref->trunk != trunk || station_ref->station == station)
				continue;
			if (trunk_ref->state == BLA_TRUNK_STATE_ONHOLD_BYME &&
					station_ref->station->hold_access == BLA_HOLD_PRIVATE)
				return 1;
			return 0;
		}
	}

	return 0;
}

/*!
 * \brief For a given station, choose the highest priority idle trunk
 * \pre bla_station is locked
 */
static struct bla_trunk_ref *bla_choose_idle_trunk(struct bla_station *station)
{
	struct bla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->state == BLA_TRUNK_STATE_IDLE) {
			ao2_ref(trunk_ref, 1);  /* FIXME: should document ownership of these refs */
			break;
		}
	}

	return trunk_ref;
}

/*!
 * \internal
 * \brief Find a BLA station by name
 */
static struct bla_station *bla_find_station(const char *name)
{
	struct bla_station tmp_station = {
		.name = name,
	};

	return ao2_find(bla_stations, &tmp_station, OBJ_POINTER);
}

/* FIXME: document this */
static struct bla_trunk *bla_find_trunk(const char *name)
{
	struct bla_trunk tmp_trunk = {
		.name = name,
	};

	return ao2_find(bla_trunks, &tmp_trunk, OBJ_POINTER);
}

/* FIXME: Document this */
static struct bla_trunk_ref *bla_find_trunk_ref_byname(const struct bla_station *station,
		const char *name)
{
	struct bla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (strcasecmp(trunk_ref->trunk->name, name))
			continue;

		/* FIXME: I'm not so sure about a lot of these options... needs testing. */
		if ( (trunk_ref->trunk->barge_disabled 
					&& trunk_ref->state == BLA_TRUNK_STATE_UP) ||
				(trunk_ref->trunk->hold_stations 
				 && trunk_ref->trunk->hold_access == BLA_HOLD_PRIVATE
				 && trunk_ref->state != BLA_TRUNK_STATE_ONHOLD_BYME) ||
				bla_check_station_hold_access(trunk_ref->trunk, station) ) 
		{
			trunk_ref = NULL;
		}

		break;
	}

	if (trunk_ref) {  /* FIXME: Not sure we can be responsible for the reference in this context. */
		ao2_ref(trunk_ref, 1);
	}

	return trunk_ref;
}

static void bla_queue_event(enum bla_event_type type)
{
	bla_queue_event_full(type, NULL, NULL, 1);
}

static void bla_queue_event_full(enum bla_event_type type, struct bla_trunk_ref *trunk_ref, struct bla_station *station, int lock)
{
	struct bla_event *event;

	/* Don't queue up events if the thread isn't running */
	if (bla.thread == AST_PTHREADT_NULL) {
		/* FIXME: For whom are we freeing these references? Hmm... */
		ao2_ref(station, -1);
		ao2_ref(trunk_ref, -1);
		return;
	}

	if (!(event = ast_calloc(1, sizeof(*event)))) {
		/* FIXME: For whom are we freeing these references? Hmm... */
		ao2_ref(station, -1);
		ao2_ref(trunk_ref, -1);
		return;
	}

	event->type = type;
	event->trunk_ref = trunk_ref;
	event->station = station;

	if (!lock) {
		AST_LIST_INSERT_TAIL(&bla.event_q, event, entry);
		return;
	}

	ast_mutex_lock(&bla.lock);
	AST_LIST_INSERT_TAIL(&bla.event_q, event, entry);
	ast_cond_signal(&bla.cond);
	ast_mutex_unlock(&bla.lock);
}

static void bla_queue_event_nolock(enum bla_event_type type)
{
	bla_queue_event_full(type, NULL, NULL, 0);
}

/* FIXME: document this */
/* FIXME: I'm... not sure why this needs to be a function call */
static void bla_answer_trunk_chan(struct ast_channel *chan)
{
	ast_answer(chan);
	ast_indicate(chan, -1);
}

static void bla_ring_stations(void)
{
	struct bla_station_ref *station_ref;
	struct bla_ringing_trunk *ringing_trunk;

	/* Make sure that every station that uses at least one of the ringing
	 * trunks, is ringing. */
	AST_LIST_TRAVERSE(&bla.ringing_trunks, ringing_trunk, entry) {
		AST_LIST_TRAVERSE(&ringing_trunk->trunk->stations, station_ref, entry) {
			int time_left;

			/* Is this station already ringing? */
			if (bla_check_ringing_station(station_ref->station))
				continue;

			/* Is this station already in a call? */
			if (bla_check_inuse_station(station_ref->station))
				continue;

			/* Did we fail to dial this station earlier?  If so, has it been
			 * a minute since we tried? */
			if (bla_check_failed_station(station_ref->station))
				continue;

			/* If this station already timed out while this trunk was ringing,
			 * do not dial it again for this ringing trunk. */
			if (bla_check_timed_out_station(ringing_trunk, station_ref->station))
				continue;

			/* Check for a ring delay in progress */
			time_left = bla_check_station_delay(station_ref->station, ringing_trunk);
			if (time_left != INT_MAX && time_left > 0)
				continue;

			/* It is time to make this station begin to ring.  Do it! */
			bla_ring_station(ringing_trunk, station_ref->station);
		}
	}
	/* Now, all of the stations that should be ringing, are ringing. */
}

/*! \brief Check to see if a station is in use
 */
static int bla_check_inuse_station(const struct bla_station *station)
{
	struct bla_trunk_ref *trunk_ref;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->chan)
			return 1;
	}

	return 0;
}

/*! \brief Check to see if this station has failed to be dialed in the past minute
 * \note assumes bla.lock is locked
 */
static int bla_check_failed_station(const struct bla_station *station)
{
	struct bla_failed_station *failed_station;
	int res = 0;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&bla.failed_stations, failed_station, entry) {
		if (station != failed_station->station)
			continue;
		if (ast_tvdiff_ms(ast_tvnow(), failed_station->last_try) > 1000) {  /* FIXME: How is 1000ms one minute? */
			AST_LIST_REMOVE_CURRENT(entry);
			bla_failed_station_destroy(failed_station);
			break;
		}
		res = 1;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return res;
}

/*! \brief Check to see if this station is already ringing 
 * \note Assumes bla.lock is locked 
 */
static int bla_check_ringing_station(const struct bla_station *station)
{
	struct bla_ringing_station *ringing_station;

	AST_LIST_TRAVERSE(&bla.ringing_stations, ringing_station, entry) {
		if (station == ringing_station->station)
			return 1;
	}

	return 0;
}

/*! \brief Check to see if dialing this station already timed out for this ringing trunk
 * \note Assumes bla.lock is locked
 */
static int bla_check_timed_out_station(const struct bla_ringing_trunk *ringing_trunk,
		const struct bla_station *station)
{
	struct bla_station_ref *timed_out_station;

	AST_LIST_TRAVERSE(&ringing_trunk->timed_out_stations, timed_out_station, entry) {
		if (station == timed_out_station->station)
			return 1;
	}

	return 0;
}

/*! \brief Calculate the ring delay for a given ringing trunk on a station
 * \param station the station
 * \param ringing_trunk the trunk.  If NULL, the highest priority ringing trunk will be used
 * \return the number of ms left before the delay is complete, or INT_MAX if there is no delay
 */
static int bla_check_station_delay(struct bla_station *station,
		struct bla_ringing_trunk *ringing_trunk)
{
	RAII_VAR(struct bla_trunk_ref *, trunk_ref, NULL, ao2_cleanup);
	unsigned int delay = UINT_MAX;
	int time_left, time_elapsed;

	if (!ringing_trunk)
		ringing_trunk = bla_choose_ringing_trunk(station, &trunk_ref, 0);
	else
		trunk_ref = bla_find_trunk_ref(station, ringing_trunk->trunk);

	if (!ringing_trunk || !trunk_ref)
		return delay;

	/* If this station has a ring delay specific to the highest priority
	 * ringing trunk, use that.  Otherwise, use the ring delay specified
	 * globally for the station. */
	delay = trunk_ref->ring_delay;
	if (!delay)
		delay = station->ring_delay;
	if (!delay)
		return INT_MAX;

	time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_trunk->ring_begin);
	time_left = (delay * 1000) - time_elapsed;

	return time_left;
}

/*! \brief Ring a station
 * \note Assumes bla.lock is locked
 */
static int bla_ring_station(struct bla_ringing_trunk *ringing_trunk, struct bla_station *station)
{
	char *tech, *tech_data;
	struct ast_dial *dial;
	struct bla_ringing_station *ringing_station;
	enum ast_dial_result res;
	int caller_is_saved;
	struct ast_party_caller caller;

	if (!(dial = ast_dial_create()))
		return -1;

	/* Ask Asterisk to let our event thread know when the dial state changes */
	ast_dial_set_state_callback(dial, bla_dial_state_callback);

	tech_data = ast_strdupa(station->device);
	tech = strsep(&tech_data, "/");

	/* Dial station (and only station) */
	if (ast_dial_append(dial, tech, tech_data, NULL) == -1) {
		ast_dial_destroy(dial);
		return -1;
	}

	/* FIXME: Caller ID support should be default */
	/* Do we need to save off the caller ID data? */
	caller_is_saved = 0;
	if (!bla.attempt_callerid) {
		caller_is_saved = 1;
		caller = *ast_channel_caller(ringing_trunk->trunk->chan);
		ast_party_caller_init(ast_channel_caller(ringing_trunk->trunk->chan));
	}

	res = ast_dial_run(dial, ringing_trunk->trunk->chan, 1);

	/* FIXME: Caller ID support should be default */
	/* Restore saved caller ID */
	if (caller_is_saved) {
		ast_party_caller_free(ast_channel_caller(ringing_trunk->trunk->chan));  /* FIXME: segfault here (ringing_trunk->trunk->chan is NULL) */
		ast_channel_caller_set(ringing_trunk->trunk->chan, &caller);
	}

	if (res != AST_DIAL_RESULT_TRYING) {
		struct bla_failed_station *failed_station;
		ast_dial_destroy(dial);
		if ((failed_station = bla_create_failed_station(station))) {
			AST_LIST_INSERT_HEAD(&bla.failed_stations, failed_station, entry);
		}
		return -1;
	}
	if (!(ringing_station = bla_create_ringing_station(station))) {
		ast_dial_join(dial);
		ast_dial_destroy(dial);
		return -1;
	}

	station->dial = dial;

	AST_LIST_INSERT_HEAD(&bla.ringing_stations, ringing_station, entry);

	return 0;
}

static struct bla_failed_station *bla_create_failed_station(struct bla_station *station)
{
	struct bla_failed_station *failed_station;

	if (!(failed_station = ast_calloc(1, sizeof(*failed_station)))) {
		return NULL;
	}

	ao2_ref(station, 1);
	failed_station->station = station;
	failed_station->last_try = ast_tvnow();

	return failed_station;
}

static void bla_failed_station_destroy(struct bla_failed_station *failed_station)
{
	if (failed_station->station) {
		ao2_ref(failed_station->station, -1);
		failed_station->station = NULL;
	}

	ast_free(failed_station);
}

/*! \brief Choose the highest priority ringing trunk for a station
 * \param station the station
 * \param rm remove the ringing trunk once selected
 * \param trunk_ref a place to store the pointer to this station's reference to
 *        the selected trunk
 * \return a pointer to the selected ringing trunk, or NULL if none found
 * \note Assumes that bla.lock is locked
 */
static struct bla_ringing_trunk *bla_choose_ringing_trunk(struct bla_station *station, struct bla_trunk_ref **trunk_ref, int rm)
{
	struct bla_trunk_ref *s_trunk_ref;
	struct bla_ringing_trunk *ringing_trunk = NULL;

	AST_LIST_TRAVERSE(&station->trunks, s_trunk_ref, entry) {
		AST_LIST_TRAVERSE_SAFE_BEGIN(&bla.ringing_trunks, ringing_trunk, entry) {
			/* Make sure this is the trunk we're looking for */
			if (s_trunk_ref->trunk != ringing_trunk->trunk)
				continue;

			/* This trunk on the station is ringing.  But, make sure this station
			 * didn't already time out while this trunk was ringing. */
			if (bla_check_timed_out_station(ringing_trunk, station))
				continue;  /* FIXME: What if we wanted to rm here? Who will remove it? I'm not sure... */

			if (rm)
				AST_LIST_REMOVE_CURRENT(entry);

			if (trunk_ref) {
				ao2_ref(s_trunk_ref, 1);
				*trunk_ref = s_trunk_ref;
			}

			/* We already found s_trunk_ref in bla.ringing_trunks */
			break;
		}
		AST_LIST_TRAVERSE_SAFE_END;

		if (ringing_trunk)
			/* We already delt with the highest priority ringing trunk */
			break;
	}

	return ringing_trunk;
}

static struct bla_trunk_ref *bla_find_trunk_ref(const struct bla_station *station,
		const struct bla_trunk *trunk)
{
	struct bla_trunk_ref *trunk_ref = NULL;

	AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
		if (trunk_ref->trunk == trunk)
			break;
	}

	ao2_ref(trunk_ref, 1);

	return trunk_ref;
}

static void bla_dial_state_callback(struct ast_dial *dial)
{
	bla_queue_event(BLA_EVENT_DIAL_STATE);
}

static struct bla_ringing_station *bla_create_ringing_station(struct bla_station *station)
{
	struct bla_ringing_station *ringing_station;

	if (!(ringing_station = ast_calloc(1, sizeof(*ringing_station))))
		return NULL;

	ao2_ref(station, 1);
	ringing_station->station = station;
	ringing_station->ring_begin = ast_tvnow();

	return ringing_station;
}

static void bla_ringing_station_destroy(struct bla_ringing_station *ringing_station)
{
	if (ringing_station->station) {
		ao2_ref(ringing_station->station, -1);
		ringing_station->station = NULL;
	}

	ast_free(ringing_station);
}

static void bla_event_destroy(struct bla_event *event)
{
	if (event->trunk_ref) {
		ao2_ref(event->trunk_ref, -1);
		event->trunk_ref = NULL;
	}

	if (event->station) {
		ao2_ref(event->station, -1);
		event->station = NULL;
	}

	ast_free(event);
}

static void bla_stop_ringing_trunk(struct bla_ringing_trunk *ringing_trunk)
{
	//	char buf[80];
	struct bla_station_ref *station_ref;

	/* FIXME: admin_exec() is an app_meetme.so application. We need to kill the confbridge conference here.
	   snprintf(buf, sizeof(buf), "SLA_%s,K", ringing_trunk->trunk->name);
	   admin_exec(NULL, buf);
	   sla_change_trunk_state(ringing_trunk->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
	 */

	/* FIXME: comment this */
	/* FIXME: why is this not part of bla_ringing_trunk_destroy() ? */
	while ((station_ref = AST_LIST_REMOVE_HEAD(&ringing_trunk->timed_out_stations, entry))) {
		ao2_ref(station_ref, -1);
	}

	bla_ringing_trunk_destroy(ringing_trunk);
}

static void bla_ringing_trunk_destroy(struct bla_ringing_trunk *ringing_trunk)
{
	if (ringing_trunk->trunk) {
		ao2_ref(ringing_trunk->trunk, -1);
		ringing_trunk->trunk = NULL;
	}

	ast_free(ringing_trunk);
}

static void bla_stop_ringing_station(struct bla_ringing_station *ringing_station,
		enum bla_station_hangup hangup)
{
	struct bla_ringing_trunk *ringing_trunk;
	struct bla_trunk_ref *trunk_ref;
	struct bla_station_ref *station_ref;

	/* FIXME: comment this */
	ast_dial_join(ringing_station->station->dial);
	ast_dial_destroy(ringing_station->station->dial);
	ringing_station->station->dial = NULL;

	if (hangup == BLA_STATION_HANGUP_NORMAL)
		/* FIXME: this goto is trivial to avoid */
		goto done;

	/* If the station is being hung up because of a timeout, then add it to the
	 * list of timed out stations on each of the ringing trunks.  This is so
	 * that when doing further processing to figure out which stations should be
	 * ringing, which trunk to answer, determining timeouts, etc., we know which
	 * ringing trunks we should ignore. */
	AST_LIST_TRAVERSE(&bla.ringing_trunks, ringing_trunk, entry) {
		AST_LIST_TRAVERSE(&ringing_station->station->trunks, trunk_ref, entry) {
			if (ringing_trunk->trunk == trunk_ref->trunk)
				break;
		}
		if (!trunk_ref)
			continue;
		if (!(station_ref = bla_create_station_ref(ringing_station->station)))
			continue;
		AST_LIST_INSERT_TAIL(&ringing_trunk->timed_out_stations, station_ref, entry);
	}

done:
	bla_ringing_station_destroy(ringing_station);
}

static struct bla_station_ref *bla_create_station_ref(struct bla_station *station)
{
	struct bla_station_ref *station_ref;

	if (!(station_ref = ao2_alloc(sizeof(*station_ref), (ao2_destructor_fn)bla_station_ref_destructor))) {
		return NULL;
	}

	ao2_ref(station, 1);
	station_ref->station = station;

	return station_ref;
}

static void bla_station_ref_destructor(struct bla_station_ref *station_ref)
{
	if (station_ref->station) {
		ao2_ref(station_ref->station, -1);
		station_ref->station = NULL;
	}

	/* FIXME: we called calloc in bla_station_ref_create()... why don't we free that here? */
}

/*!
 * \breif Determine the user profile to use for a station
 * \param[in] station The station whose user profile we are looking for
 * \param[in] trunk The trunk that this station is connecting to
 * \param[out] user_profile_name Result is copied into this string buffer. The
 *                               buffer must be at least MAX_PROFILE_NAME bytes
 *                               long.
 *
 * \sa confbridge_init_and_join()
 */
static void bla_station_user_profile_name(const struct bla_station *station, const struct bla_trunk *trunk, char *user_profile_name)
{
	/* Determine the user profile for this station */
	/* When determining the user profile for a station, the
	 * settings are checked in this order:
	 *   1. user_profile set for the station in bla.conf
	 *   2. station_user_profile set for the trunk in bla.conf
	 *   3. The DEFAULT_STATION_USER_PROFILE macro
	 */
	/* TODO: Maybe check station_user_profile set by CONFBRIDGE() ? */
	if (!ast_strlen_zero(station->user_profile))
		ast_copy_string(user_profile_name, station->user_profile, MAX_PROFILE_NAME);
	else if (!ast_strlen_zero(trunk->station_user_profile))
		ast_copy_string(user_profile_name, trunk->station_user_profile, MAX_PROFILE_NAME);
	else
		ast_copy_string(user_profile_name, DEFAULT_TRUNK_USER_PROFILE, MAX_PROFILE_NAME);
}

/*!
 * \breif Determine the user profile to use for a trunk
 * \param[in] trunk The trunk whose user profile we are looking for
 * \param[out] user_profile_name Result is copied into this string buffer. The
 *                               buffer must be at least MAX_PROFILE_NAME bytes
 *                               long.
 *
 * \sa confbridge_init_and_join()
 */
static void bla_trunk_user_profile_name(const struct bla_trunk *trunk, char *user_profile_name)
{
	/* Determine the user profile for this trunk */
	/* When determining the user profile for a trunk, the
	 * settings are checked in this order:
	 *   1. trunk_user_profile set for the trunk in bla.conf
	 *   2. The DEFAULT_TRUNK_USER_PROFILE macro
	 */
	/* TODO: Maybe check trunk_user_profile set by CONFBRIDGE() ? */
	if (!ast_strlen_zero(trunk->trunk_user_profile))
		ast_copy_string(user_profile_name, trunk->trunk_user_profile, MAX_PROFILE_NAME);
	else
		ast_copy_string(user_profile_name, DEFAULT_TRUNK_USER_PROFILE, MAX_PROFILE_NAME);
}

/*!
 * \breif Determine the bridge profile to use for a trunk
 * \param[in] trunk The trunk whose user profile we are looking for
 * \param[out] bridge_profile_name Result is copied into this string buffer. The
 *                                 buffer must be at least MAX_PROFILE_NAME
 *                                 bytes long.
 *
 * \sa confbridge_init_and_join()
 */
static void bla_trunk_bridge_profile_name(const struct bla_trunk *trunk, char *bridge_profile_name)
{
	/* Determine the bridge profile for this trunk */
	/* When determining the bridge profile for a trunk, the
	 * settings are checked in this order:
	 *   1. bridge_profile set for the trunk in bla.conf
	 *   2. DEFAULT_TRUNK_BRIDGE_PROFILE
	 */
	// TODO: Maybe check bridge_profile set by CONFBRIDGE() ?
	if (!ast_strlen_zero(trunk->bridge_profile))
		ast_copy_string(bridge_profile_name, trunk->bridge_profile, MAX_PROFILE_NAME);
	else
		ast_copy_string(bridge_profile_name, DEFAULT_TRUNK_BRIDGE_PROFILE, MAX_PROFILE_NAME);
}

/*!
 * \breif Build name of conference for the given trunk
 * \param[in] trunk The trunk whose conference name we are looking for
 * \param[out] conference_name Result is copied into this string buffer. The
 *                             buffer must be at least MAX_CONF_NAME bytes
 *                             long.
 *
 * \sa confbridge_init_and_join()
 */
static void bla_trunk_conference_name(const struct bla_trunk *trunk, char *conference_name)
{
	snprintf(conference_name, MAX_CONF_NAME, "BLA_%s", trunk->name);
}

/* FIXME: document this */
static void bla_hangup_stations(void)
{
	struct bla_trunk_ref *trunk_ref;
	struct bla_ringing_station *ringing_station;

	/* FIXME: comment this */
	AST_LIST_TRAVERSE_SAFE_BEGIN(&bla.ringing_stations, ringing_station, entry) {
		AST_LIST_TRAVERSE(&ringing_station->station->trunks, trunk_ref, entry) {
			struct bla_ringing_trunk *ringing_trunk;
			ast_mutex_lock(&bla.lock);
			AST_LIST_TRAVERSE(&bla.ringing_trunks, ringing_trunk, entry) {
				if (trunk_ref->trunk == ringing_trunk->trunk)
					break;
			}
			ast_mutex_unlock(&bla.lock);
			if (ringing_trunk)
				break;  /* FIXME: comment this */
		}
		if (!trunk_ref) {
			AST_LIST_REMOVE_CURRENT(entry);
			ast_dial_join(ringing_station->station->dial);
			ast_dial_destroy(ringing_station->station->dial);
			ringing_station->station->dial = NULL;
			bla_ringing_station_destroy(ringing_station);
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
}

static struct bla_ringing_trunk *bla_queue_ringing_trunk(struct bla_trunk *trunk)
{
	struct bla_ringing_trunk *ringing_trunk;

	if (!(ringing_trunk = ast_calloc(1, sizeof(*ringing_trunk)))) {
		return NULL;
	}

	bla_send_ringing_ami_event(trunk);

	ao2_ref(trunk, 1);
	ringing_trunk->trunk = trunk;
	ringing_trunk->ring_begin = ast_tvnow();

	bla_change_trunk_state(trunk, BLA_TRUNK_STATE_RINGING, ALL_TRUNK_REFS, NULL);

	ast_mutex_lock(&bla.lock);
	AST_LIST_INSERT_HEAD(&bla.ringing_trunks, ringing_trunk, entry);
	ast_mutex_unlock(&bla.lock);

	bla_queue_event(BLA_EVENT_RINGING_TRUNK);

	return ringing_trunk;
}

/*
 * \breif Returns a non-zero value when BLA is being used.
 */
int bla_in_use(void)
{
	return ao2_container_count(bla_trunks) || ao2_container_count(bla_stations);
}

static enum ast_device_state bla_state_to_devstate(enum bla_trunk_state state)
{
	switch (state) {
		case BLA_TRUNK_STATE_IDLE:
			return AST_DEVICE_NOT_INUSE;
		case BLA_TRUNK_STATE_RINGING:
			return AST_DEVICE_RINGING;
		case BLA_TRUNK_STATE_UP:
			return AST_DEVICE_INUSE;
		case BLA_TRUNK_STATE_ONHOLD:
		case BLA_TRUNK_STATE_ONHOLD_BYME:
			return AST_DEVICE_ONHOLD;
	}

	return AST_DEVICE_UNKNOWN;
}

static void *bla_dial_trunk(struct bla_dial_trunk_args *args)
{
	struct ast_dial *dial;
	char *tech, *tech_data;
	enum ast_dial_result dial_res;
	enum ast_device_state device_state;
	char conf_name[MAX_CONF_NAME];
	char user_profile_name[MAX_PROFILE_NAME];
	char bridge_profile_name[MAX_PROFILE_NAME];
	RAII_VAR(struct bla_trunk_ref *, trunk_ref, args->trunk_ref, ao2_cleanup);
	RAII_VAR(struct bla_station *, station, args->station, ao2_cleanup);
	int caller_is_saved;
	struct ast_party_caller caller;
	int last_state = 0;  /* FIXME: use known enum values */
	int current_state = 0;

	ast_debug(3, "In dial trunk thread for trunk '%s' and station '%s'",
			trunk_ref->trunk->name, station->name);

	if (!(dial = ast_dial_create())) {
		ast_debug(3, "Could not create dial object in dial trunk thread.");
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		return NULL;
	}

	tech_data = ast_strdupa(trunk_ref->trunk->device);
	tech = strsep(&tech_data, "/");
	ast_debug(3, "Trunk tech: '%s' and tech_data: '%s'",
			tech, tech_data);
	if (ast_dial_append(dial, tech, tech_data, NULL) == -1) {  /* FIXME: do we need the chan from this? */
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		ast_dial_destroy(dial);
		return NULL;
	}

	/* Do we need to save the caller ID data? */
	caller_is_saved = 0;
	if (!bla.attempt_callerid) {  /* FIXME: Get rid of the callerid conditional */
		caller_is_saved = 1;
		caller = *ast_channel_caller(trunk_ref->chan);
		ast_party_caller_init(ast_channel_caller(trunk_ref->chan));
	}


	ast_debug(3, "Dialing '%s/%s' for channel '%s'",
			tech, tech_data, ast_channel_name(trunk_ref->chan));
	dial_res = ast_dial_run(dial, trunk_ref->chan, 1);

	/* Restore saved caller ID */
	if (caller_is_saved) {
		ast_party_caller_free(ast_channel_caller(trunk_ref->chan));
		ast_channel_caller_set(trunk_ref->chan, &caller);
	}

	if (dial_res != AST_DIAL_RESULT_TRYING) {
		ast_debug(3, "Dialing '%s/%s' for channel '%s' failed: %u",
				tech, tech_data, ast_channel_name(trunk_ref->chan), dial_res);
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		ast_dial_destroy(dial);
		return NULL;
	}

	for (;;) {
		unsigned int done = 0;
		switch ((dial_res = ast_dial_state(dial))) {
			case AST_DIAL_RESULT_ANSWERED:
				ast_debug(3, "'%s/%s' answered call from channel '%s'",
						tech, tech_data, ast_channel_name(trunk_ref->chan));
				trunk_ref->trunk->chan = ast_dial_answered(dial);
			case AST_DIAL_RESULT_HANGUP:
			case AST_DIAL_RESULT_INVALID:
			case AST_DIAL_RESULT_FAILED:
			case AST_DIAL_RESULT_TIMEOUT:
			case AST_DIAL_RESULT_UNANSWERED:
				done = 1;
				break;
			case AST_DIAL_RESULT_TRYING:
				current_state = AST_CONTROL_PROGRESS;
				break;
			case AST_DIAL_RESULT_RINGING:
			case AST_DIAL_RESULT_PROGRESS:
			case AST_DIAL_RESULT_PROCEEDING:
				current_state = AST_CONTROL_RINGING;
				break;
		}
		if (done)
			break;

		/* Check that BLA station that originated trunk call is still alive */
		if (station) {
			device_state = ast_device_state(station->device);
			ast_debug(3, "Station '%s' device '%s' state: '%s'\n",
					station->name, station->device, ast_devstate_str(device_state));
			if(device_state == AST_DEVICE_NOT_INUSE) {
				/* FIXME: This condition is falling through with SIP/hakase no longer active */
				ast_debug(3, "Originating station device '%s' no longer active\n", station->device);
				/*  FIXME: This condition ALWAYS fails. Somehow need to change device state of SIP device. */
				/*
				   trunk_ref->trunk->chan = NULL;
				   break;
				 */
			}
		}

		/* If trunk line state changed, send indication back to originating BLA Station channel */
		if (current_state != last_state) {
			ast_debug(3, "Indicating State Change '%d' to channel '%s'\n",
					current_state, ast_channel_name(trunk_ref->chan));
			ast_indicate(trunk_ref->chan, current_state);
			last_state = current_state;
		}

		/* Avoid tight loop; sleep for 1/10th second */
		ast_safe_sleep(trunk_ref->chan, 100);
	}

	if (!trunk_ref->trunk->chan) {
		ast_debug(3, "Trunk channel is NULL; trunk did not answer");
		ast_mutex_lock(args->cond_lock);
		ast_cond_signal(args->cond);
		ast_mutex_unlock(args->cond_lock);
		ast_dial_join(dial);
		ast_dial_destroy(dial);
		return NULL;
	}

	/* Find the bridge profile, user profile, and conference names */
	/* These determine the properties of the conference we join/create */
	bla_trunk_conference_name(trunk_ref->trunk, conf_name);
	bla_trunk_user_profile_name(trunk_ref->trunk, user_profile_name);
	bla_trunk_bridge_profile_name(trunk_ref->trunk, bridge_profile_name);

	/* Signal to the station's channel thread that the trunk
	 * channel is ready */
	ast_debug(3, "Trunk '%s' thread signaling station '%s' thread to continue",
			trunk_ref->trunk->name, station->name);
	ast_mutex_lock(args->cond_lock);
	ast_cond_signal(args->cond);
	ast_mutex_unlock(args->cond_lock);

	/* Actually join the conference */
	ast_debug(1, "Joining the conference '%s' in trunk '%s' thread",
			conf_name, trunk_ref->trunk->name);
	/* FIXME: Do we need to check the return status of confbridge_init_and_join?
	 * It should handle its own errors just fine... */
	confbridge_init_and_join(trunk_ref->trunk->chan,
			conf_name,
			user_profile_name,
			bridge_profile_name,
			NULL);  /* FIXME: Do we really need a menu profile? */

	ast_debug(1, "Trunk '%s' thread left the conference '%s'",
			trunk_ref->trunk->name, conf_name);

	/* If the trunk is going away, it is definitely now IDLE. */
	bla_change_trunk_state(trunk_ref->trunk, BLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);

	trunk_ref->trunk->chan = NULL;
	trunk_ref->trunk->on_hold = 0;

	ast_dial_join(dial);
	ast_dial_destroy(dial);

	ast_debug(1, "Leaving trunk '%s' thread", trunk_ref->trunk->name);

	return NULL;
}

/* FIXME: document this */
static void *bla_run_station(struct bla_run_station_args *args)
{
	RAII_VAR(struct bla_station *, station, NULL, ao2_cleanup);
	RAII_VAR(struct bla_trunk_ref *, trunk_ref, NULL, ao2_cleanup);
	/* FIXME: Maybe we don't need to use this much stack? Maybe I don't care that much... */
	char conf_name[MAX_CONF_NAME];
	char user_profile_name[MAX_PROFILE_NAME];
	char bridge_profile_name[MAX_PROFILE_NAME];

	station = args->station;
	trunk_ref = args->trunk_ref;
	ast_mutex_lock(args->cond_lock);
	ast_cond_signal(args->cond);
	ast_mutex_unlock(args->cond_lock);

	/* FIXME: comment this */
	ast_atomic_fetchadd_int((int *) &trunk_ref->trunk->active_stations, 1);

	/* FIXME: comment this */
	bla_answer_trunk_chan(trunk_ref->chan);

	/* Find the bridge profile, user profile, and conference names */
	/* These determine the properties of the conference we join/create */
	bla_trunk_conference_name(trunk_ref->trunk, conf_name);
	bla_station_user_profile_name(station, trunk_ref->trunk, user_profile_name);
	bla_trunk_bridge_profile_name(trunk_ref->trunk, bridge_profile_name);

	/* Actually join the conference */
	ast_debug(3, "Joining the conference in station '%s' thread.",
			station->name);
	/* FIXME: Do we need to check the return status of confbridge_init_and_join?
	 * It should handle its own errors just fine... */
	confbridge_init_and_join(trunk_ref->trunk->chan,
			conf_name,
			user_profile_name,
			bridge_profile_name,
			NULL);  /* FIXME: Do we really need a menu profile? */

	/* Clean up now that we've exited the conference */
	trunk_ref->chan = NULL;
	/* FIXME: Kick everyone from the channel here?
	   if (ast_atomic_dec_and_test((int *) &trunk_ref->trunk->active_stations) &&
	   trunk_ref->state != SLA_TRUNK_STATE_ONHOLD_BYME) {
	   ast_str_append(&conf_name, 0, ",K");
	   admin_exec(NULL, ast_str_buffer(conf_name));
	   trunk_ref->trunk->hold_stations = 0;
	   sla_change_trunk_state(trunk_ref->trunk, SLA_TRUNK_STATE_IDLE, ALL_TRUNK_REFS, NULL);
	   }
	 */

	ast_dial_join(station->dial);
	ast_dial_destroy(station->dial);
	station->dial = NULL;

	return NULL;
}

static void *bla_thread(void *data)
{
	struct bla_failed_station *failed_station;
	struct bla_ringing_station *ringing_station;

	ast_mutex_lock(&bla.lock);

	while (!bla.stop) {
		struct bla_event *event;
		struct timespec ts = { 0, };
		unsigned int have_timeout = 0;

		/* Wait for events while the event queue is empty */
		if (AST_LIST_EMPTY(&bla.event_q)) {
			/* Check various timers for timeouts */
			if ((have_timeout = bla_process_timers(&ts)))
				ast_cond_timedwait(&bla.cond, &bla.lock, &ts);
			else
				ast_cond_wait(&bla.cond, &bla.lock);
			if (bla.stop)
				break;
		}

		if (have_timeout)
			/* FIXME: Document this */
			bla_process_timers(NULL);

		while ((event = AST_LIST_REMOVE_HEAD(&bla.event_q, entry))) {
			ast_mutex_unlock(&bla.lock);
			switch (event->type) {
				case BLA_EVENT_HOLD:
					bla_handle_hold_event(event);
					break;
				case BLA_EVENT_DIAL_STATE:
					bla_handle_dial_state_event();
					break;
				case BLA_EVENT_RINGING_TRUNK:
					bla_handle_ringing_trunk_event();
					break;
			}
			bla_event_destroy(event);
			ast_mutex_lock(&bla.lock);
		}
	}

	ast_mutex_unlock(&bla.lock);

	/* Clean up before leaving the thread */
	/* FIXME: These resources should be allocated and destroyed in the same
	 * thread, for clarity. */
	while ((ringing_station = AST_LIST_REMOVE_HEAD(&bla.ringing_stations, entry))) {
		bla_ringing_station_destroy(ringing_station);
	}
	while ((failed_station = AST_LIST_REMOVE_HEAD(&bla.failed_stations, entry))) {
		bla_failed_station_destroy(failed_station);
	}

	return NULL;
}


/* BLA Event Functions */

/*! \brief Calculate the time until the next known event
 *  \note Called with sla.lock locked */
static int bla_process_timers(struct timespec *ts)
{
	unsigned int timeout = UINT_MAX;
	struct timeval wait;
	unsigned int change_made = 0;

	/* Check for ring timeouts on ringing trunks */
	if (bla_calc_trunk_timeouts(&timeout))
		change_made = 1;

	/* Check for ring timeouts on ringing stations */
	if (bla_calc_station_timeouts(&timeout))
		change_made = 1;

	/* Check for station ring delays */
	if (bla_calc_station_delays(&timeout))
		change_made = 1;

	/* Queue reprocessing of ringing trunks */
	if (change_made)
		bla_queue_event_nolock(BLA_EVENT_RINGING_TRUNK);

	/* No timeout */
	if (timeout == UINT_MAX)
		return 0;

	if (ts) {
		wait = ast_tvadd(ast_tvnow(), ast_samp2tv(timeout, 1000));
		ts->tv_sec = wait.tv_sec;
		ts->tv_nsec = wait.tv_usec * 1000;
	}

	return 1;
}

/*! \brief Process trunk ring timeouts
 * \note Called with bla.lock locked
 * \return non-zero if a change to the ringing trunks was made
 */
static int bla_calc_trunk_timeouts(unsigned int *timeout)
{
	struct bla_ringing_trunk *ringing_trunk;
	int res = 0;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&bla.ringing_trunks, ringing_trunk, entry) {
		int time_left, time_elapsed;
		if (!ringing_trunk->trunk->ring_timeout)
			continue;
		time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_trunk->ring_begin);
		time_left = (ringing_trunk->trunk->ring_timeout * 1000) - time_elapsed;
		if (time_left <= 0) {
			pbx_builtin_setvar_helper(ringing_trunk->trunk->chan, "BLATRUNK_STATUS", "RINGTIMEOUT");
			AST_LIST_REMOVE_CURRENT(entry);
			bla_stop_ringing_trunk(ringing_trunk);
			res = 1;
			continue;
		}
		if (time_left < *timeout)
			*timeout = time_left;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return res;
}

/*! \brief Process station ring timeouts
 * \note Called with bla.lock locked
 * \return non-zero if a change to the ringing stations was made
 */
static int bla_calc_station_timeouts(unsigned int *timeout)
{
	struct bla_ringing_trunk *ringing_trunk;
	struct bla_ringing_station *ringing_station;
	int res = 0;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&bla.ringing_stations, ringing_station, entry) {
		unsigned int ring_timeout = 0;
		int time_elapsed, time_left = INT_MAX, final_trunk_time_left = INT_MIN;
		struct bla_trunk_ref *trunk_ref;

		/* If there are any ring timeouts specified for a specific trunk
		 * on the station, then use the highest per-trunk ring timeout.
		 * Otherwise, use the ring timeout set for the entire station. */
		AST_LIST_TRAVERSE(&ringing_station->station->trunks, trunk_ref, entry) {
			struct bla_station_ref *station_ref;
			int trunk_time_elapsed, trunk_time_left;

			AST_LIST_TRAVERSE(&bla.ringing_trunks, ringing_trunk, entry) {
				if (ringing_trunk->trunk == trunk_ref->trunk)
					break;
			}
			if (!ringing_trunk)
				continue;

			/* If there is a trunk that is ringing without a timeout, then the
			 * only timeout that could matter is a global station ring timeout. */
			if (!trunk_ref->ring_timeout)
				break;

			/* This trunk on this station is ringing and has a timeout.
			 * However, make sure this trunk isn't still ringing from a
			 * previous timeout.  If so, don't consider it. */
			AST_LIST_TRAVERSE(&ringing_trunk->timed_out_stations, station_ref, entry) {
				if (station_ref->station == ringing_station->station)
					break;
			}
			if (station_ref)
				continue;

			trunk_time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_trunk->ring_begin);
			trunk_time_left = (trunk_ref->ring_timeout * 1000) - trunk_time_elapsed;
			if (trunk_time_left > final_trunk_time_left)
				final_trunk_time_left = trunk_time_left;
		}

		/* No timeout was found for ringing trunks, and no timeout for the entire station */
		if (final_trunk_time_left == INT_MIN && !ringing_station->station->ring_timeout)
			continue;

		/* Compute how much time is left for a global station timeout */
		if (ringing_station->station->ring_timeout) {
			ring_timeout = ringing_station->station->ring_timeout;
			time_elapsed = ast_tvdiff_ms(ast_tvnow(), ringing_station->ring_begin);
			time_left = (ring_timeout * 1000) - time_elapsed;
		}

		/* If the time left based on the per-trunk timeouts is smaller than the
		 * global station ring timeout, use that. */
		if (final_trunk_time_left > INT_MIN && final_trunk_time_left < time_left)
			time_left = final_trunk_time_left;

		/* If there is no time left, the station needs to stop ringing */
		if (time_left <= 0) {
			AST_LIST_REMOVE_CURRENT(entry);
			bla_stop_ringing_station(ringing_station, BLA_STATION_HANGUP_TIMEOUT);
			res = 1;
			continue;
		}

		/* There is still some time left for this station to ring, so save that
		 * timeout if it is the first event scheduled to occur */
		if (time_left < *timeout)
			*timeout = time_left;
	}
	AST_LIST_TRAVERSE_SAFE_END;

	return res;
}

/*! \brief Calculate the ring delay for a station
 * \note Assumes bla.lock is locked
 */
static int bla_calc_station_delays(unsigned int *timeout)
{
	struct bla_station *station;
	int res = 0;
	struct ao2_iterator i;

	i = ao2_iterator_init(bla_stations, 0);
	for (; (station = ao2_iterator_next(&i)); ao2_ref(station, -1)) {
		struct bla_ringing_trunk *ringing_trunk;
		int time_left;

		/* Ignore stations already running */
		if (bla_check_ringing_station(station))
			continue;

		/* Ignore stations already on a call */
		if (bla_check_inuse_station(station))
			continue;

		/* Ignore stations that don't have one of their trunks ringing */
		if (!(ringing_trunk = bla_choose_ringing_trunk(station, NULL, 0)))
			continue;

		if ((time_left = bla_check_station_delay(station, ringing_trunk)) == INT_MAX)
			continue;

		/* FIXME: grammar */
		/* If there is no time left, then the station needs to start ringing.
		 * Return non-zero so that an event will be queued up an event to 
		 * make that happen. */
		if (time_left <= 0) {
			res = 1;
			continue;
		}

		if (time_left < *timeout)
			*timeout = time_left;
	}
	ao2_iterator_destroy(&i);

	return res;
}

/* FIXME: document this */
static void bla_handle_hold_event(struct bla_event *event)
{
	/* FIXME: document this */
	ast_atomic_fetchadd_int((int *) &event->trunk_ref->trunk->hold_stations, 1);
	event->trunk_ref->state = BLA_TRUNK_STATE_ONHOLD_BYME;
	ast_devstate_changed(AST_DEVICE_ONHOLD, AST_DEVSTATE_CACHABLE, "BLA:%s_%s",
			event->station->name, event->trunk_ref->trunk->name);
	bla_change_trunk_state(event->trunk_ref->trunk, BLA_TRUNK_STATE_ONHOLD, 
			INACTIVE_TRUNK_REFS, event->trunk_ref);

	if (event->trunk_ref->trunk->active_stations == 1) {
		/* The station putting it on hold is the only one on the call, so start
		 * Music on hold to the trunk. */
		event->trunk_ref->trunk->on_hold = 1;
		ast_indicate(event->trunk_ref->trunk->chan, AST_CONTROL_HOLD);
	}

	/* FIXME: document this */
	ast_softhangup(event->trunk_ref->chan, AST_SOFTHANGUP_DEV);
	event->trunk_ref->chan = NULL;
}

/* FIXME: document this */
static void bla_handle_dial_state_event(void)
{
	struct bla_ringing_station *ringing_station;

	AST_LIST_TRAVERSE_SAFE_BEGIN(&bla.ringing_stations, ringing_station, entry) {
		RAII_VAR(struct bla_trunk_ref *, s_trunk_ref, NULL, ao2_cleanup);
		struct bla_ringing_trunk *ringing_trunk = NULL;
		struct bla_run_station_args args;
		enum ast_dial_result dial_res;
		pthread_t thread;
		ast_mutex_t cond_lock;
		ast_cond_t cond;

		switch ((dial_res = ast_dial_state(ringing_station->station->dial))) {
			case AST_DIAL_RESULT_HANGUP:
			case AST_DIAL_RESULT_INVALID:
			case AST_DIAL_RESULT_FAILED:
			case AST_DIAL_RESULT_TIMEOUT:
			case AST_DIAL_RESULT_UNANSWERED:
				AST_LIST_REMOVE_CURRENT(entry);
				bla_stop_ringing_station(ringing_station, BLA_STATION_HANGUP_NORMAL);
				break;
			case AST_DIAL_RESULT_ANSWERED:
				AST_LIST_REMOVE_CURRENT(entry);
				/* Find the appropriate trunk to answer. */
				ast_mutex_lock(&bla.lock);
				ringing_trunk = bla_choose_ringing_trunk(ringing_station->station, &s_trunk_ref, 1);
				ast_mutex_unlock(&bla.lock);
				if (!ringing_trunk) {
					/* This case happens in a bit of a race condition.  If two stations answer
					 * the outbound call at the same time, the first one will get connected to
					 * the trunk.  When the second one gets here, it will not see any trunks
					 * ringing so we have no idea what to conect it to.  So, we just hang up
					 * on it. */
					ast_debug(1, "Found no ringing trunk for station '%s' to answer!\n",
							ringing_station->station->name);
					ast_dial_join(ringing_station->station->dial);
					ast_dial_destroy(ringing_station->station->dial);
					ringing_station->station->dial = NULL;
					bla_ringing_station_destroy(ringing_station);
					break;
				}
				/* Track the channel that answered this trunk */
				s_trunk_ref->chan = ast_dial_answered(ringing_station->station->dial);
				/* Actually answer the trunk */
				bla_answer_trunk_chan(ringing_trunk->trunk->chan);
				bla_change_trunk_state(ringing_trunk->trunk, BLA_TRUNK_STATE_UP, ALL_TRUNK_REFS, NULL);
				/* Now, start a thread that will connect this station to the trunk.  The rest of
				 * the code here sets up the thread and ensures that it is able to save the arguments
				 * before they are no longer valid since they are allocated on the stack. */
				ao2_ref(s_trunk_ref, 1);
				args.trunk_ref = s_trunk_ref;
				ao2_ref(ringing_station->station, 1);
				args.station = ringing_station->station;
				args.cond = &cond;
				args.cond_lock = &cond_lock;
				bla_ringing_trunk_destroy(ringing_trunk);
				bla_ringing_station_destroy(ringing_station);
				ast_mutex_init(&cond_lock);
				ast_cond_init(&cond, NULL);
				ast_mutex_lock(&cond_lock);
				ast_pthread_create_detached_background(&thread, NULL, (void *(*)(void *))bla_run_station, &args);
				ast_cond_wait(&cond, &cond_lock);
				ast_mutex_unlock(&cond_lock);
				ast_mutex_destroy(&cond_lock);
				ast_cond_destroy(&cond);
				break;
			case AST_DIAL_RESULT_TRYING:
			case AST_DIAL_RESULT_RINGING:
			case AST_DIAL_RESULT_PROGRESS:
			case AST_DIAL_RESULT_PROCEEDING:
				break;
		}
		if (dial_res == AST_DIAL_RESULT_ANSWERED) {
			/* Queue up reprocessing ringing trunks, and then ringing stations again */
			bla_queue_event(BLA_EVENT_RINGING_TRUNK);
			bla_queue_event(BLA_EVENT_DIAL_STATE);
			break;
		}
	}
	AST_LIST_TRAVERSE_SAFE_END;
}

static void bla_handle_ringing_trunk_event(void)
{
	ast_mutex_lock(&bla.lock);
	bla_ring_stations();
	ast_mutex_unlock(&bla.lock);

	/* Find stations that shouldn't be ringing anymore */
	bla_hangup_stations();
}

char *bla_show_stations(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator i;
	struct bla_station *station;

	switch (cmd) {
		case CLI_INIT:
			e->command = "bla show stations";
			e->usage =
				"Usage: bla show stations\n"
				"       This will list all stations defined in bla.conf\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;
	}

	ast_cli(a->fd, "\n"
			"=============================================================\n"
			"=== Configured BLA Stations =================================\n"
			"=============================================================\n"
			"===\n");
	i = ao2_iterator_init(bla_stations, 0);

	for (; (station = ao2_iterator_next(&i)); ao2_ref(station, -1)) {
		struct bla_trunk_ref *trunk_ref;
		char ring_timeout[16] = "(none)";
		char ring_delay[16] = "(none)";

		ao2_lock(station);

		if (station->ring_timeout) {
			snprintf(ring_timeout, sizeof(ring_timeout),
					"%u", station->ring_timeout);
		}
		if (station->ring_delay) {
			snprintf(ring_delay, sizeof(ring_delay),
					"%u", station->ring_delay);
		}
		ast_cli(a->fd,
				"=== ---------------------------------------------------------\n"
				"=== Station Name:    %s\n"
				"=== ==> Device:      %s\n"
				"=== ==> AutoContext: %s\n"
				"=== ==> RingTimeout: %s\n"
				"=== ==> RingDelay:   %s\n"
				"=== ==> HoldAccess:  %s\n"
				"=== ==> UserProfile:  %s\n"
				"=== ==> Trunks ...\n",
				station->name, station->device,
				S_OR(station->autocontext, "(none)"),
				ring_timeout, ring_delay,
				bla_hold_access_str(station->hold_access),
				station->user_profile);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (trunk_ref->ring_timeout) {
				snprintf(ring_timeout, sizeof(ring_timeout),
						"%u", trunk_ref->ring_timeout);
			} else
				strcpy(ring_timeout, "(none)");
			if (trunk_ref->ring_delay) {
				snprintf(ring_delay, sizeof(ring_delay),
						"%u", trunk_ref->ring_delay);
			} else
				strcpy(ring_delay, "(none)");
			ast_cli(a->fd,
					"===    ==> Trunk Name: %s\n"
					"===       ==> State:       %s\n"
					"===       ==> RingTimeout: %s\n"
					"===       ==> RingDelay:   %s\n",
					trunk_ref->trunk->name,
					bla_trunk_state_str(trunk_ref->state),
					ring_timeout, ring_delay);
		}
		ast_cli(a->fd,
				"=== ---------------------------------------------------------\n"
				"===\n");
		ao2_unlock(station);
	}
	ao2_iterator_destroy(&i);
	ast_cli(a->fd,
			"============================================================\n"
			"\n");

	return CLI_SUCCESS;
}

char *bla_show_trunks(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ao2_iterator i;
	struct bla_trunk *trunk;

	switch (cmd) {
		case CLI_INIT:
			e->command = "bla show trunks";
			e->usage =
				"Usage: bla show trunks\n"
				"       This will list all trunks defined in bla.conf\n";
			return NULL;
		case CLI_GENERATE:
			return NULL;
	}

	ast_cli(a->fd, "\n"
			"=============================================================\n"
			"=== Configured SLA Trunks ===================================\n"
			"=============================================================\n"
			"===\n");
	i = ao2_iterator_init(bla_trunks, 0);
	for (; (trunk = ao2_iterator_next(&i)); ao2_ref(trunk, -1)) {
		struct bla_station_ref *station_ref;
		char ring_timeout[16] = "(none)";

		ao2_lock(trunk);

		if (trunk->ring_timeout) {
			snprintf(ring_timeout, sizeof(ring_timeout), "%u Seconds", trunk->ring_timeout);
		}

		ast_cli(a->fd,
				"=== ---------------------------------------------------------\n"
				"=== Trunk Name:       %s\n"
				"=== ==> Device:       %s\n"
				"=== ==> AutoContext:  %s\n"
				"=== ==> RingTimeout:  %s\n"
				"=== ==> BargeAllowed: %s\n"
				"=== ==> HoldAccess:   %s\n"
				"=== ==> BridgeProfile:   %s\n"
				"=== ==> TrunkUserProfile:   %s\n"
				"=== ==> StationUserProfile:   %s\n"
				"=== ==> Stations ...\n",
				trunk->name, trunk->device, 
				S_OR(trunk->autocontext, "(none)"), 
				ring_timeout,
				trunk->barge_disabled ? "No" : "Yes",
				bla_hold_access_str(trunk->hold_access),
				trunk->bridge_profile,
				trunk->trunk_user_profile,
				trunk->station_user_profile);

		AST_LIST_TRAVERSE(&trunk->stations, station_ref, entry) {
			ast_cli(a->fd, "===    ==> Station name: %s\n", station_ref->station->name);
		}

		ast_cli(a->fd, "=== ---------------------------------------------------------\n===\n");

		ao2_unlock(trunk);
	}
	ao2_iterator_destroy(&i);
	ast_cli(a->fd, "=============================================================\n\n");

	return CLI_SUCCESS;
}

static const char *bla_hold_access_str(enum bla_hold_access hold_access)
{
	const char *hold = "Unknown";

	switch (hold_access) {
		case BLA_HOLD_OPEN:
			hold = "Open";
			break;
		case BLA_HOLD_PRIVATE:
			hold = "Private";
		default:
			break;
	}

	return hold;
}

static const char *bla_trunk_state_str(enum bla_trunk_state state)
{
#define S(e) case e: return #e
	switch (state) {
		S(BLA_TRUNK_STATE_IDLE);
		S(BLA_TRUNK_STATE_RINGING);
		S(BLA_TRUNK_STATE_UP);
		S(BLA_TRUNK_STATE_ONHOLD);
		S(BLA_TRUNK_STATE_ONHOLD_BYME);
	}
	return "Unknown State";
#undef S
}

/* FIXME: document this */
enum ast_device_state bla_devstate(const char *data)
{
	char *buf, *station_name, *trunk_name;
	RAII_VAR(struct bla_station *, station, NULL, ao2_cleanup);
	struct bla_trunk_ref *trunk_ref;
	enum ast_device_state res = AST_DEVICE_INVALID;

	trunk_name = buf = ast_strdupa(data);
	station_name = strsep(&trunk_name, "_");

	ast_debug(3, "In bla_devstate callback for trunk '%s' on station '%s'",
			trunk_name, station_name);

	station = bla_find_station(station_name);
	if (station) {
		ao2_lock(station);
		AST_LIST_TRAVERSE(&station->trunks, trunk_ref, entry) {
			if (!strcasecmp(trunk_name, trunk_ref->trunk->name)) {
				res = bla_state_to_devstate(trunk_ref->state);
				break;
			}
		}
		ao2_unlock(station);
	}

	ast_debug(3, "Found state '%s' for trunk '%s' on station '%s'",
			ast_devstate_str(res), trunk_name, station_name);

	if (res == AST_DEVICE_INVALID) {
		ast_log(LOG_ERROR, "Could not determine state for trunk '%s' on station '%s'\n",
				trunk_name, station_name);
	}

	return res;
}

static struct stasis_message_router *bridge_state_router;
static struct stasis_message_router *channel_state_router;

STASIS_MESSAGE_TYPE_DEFN(bla_ringing_type);

int bla_stasis_init(void)
{
	STASIS_MESSAGE_TYPE_INIT(bla_ringing_type);

	bridge_state_router = stasis_message_router_create(
		ast_bridge_topic_all_cached());
	if (!bridge_state_router) {
		return -1;
	}

	if (stasis_message_router_add(bridge_state_router,
			bla_ringing_type(),
			bla_ringing_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}

	channel_state_router = stasis_message_router_create(
		ast_channel_topic_all_cached());
	if (!channel_state_router) {
		manager_confbridge_shutdown();
		return -1;
	}

	if (stasis_message_router_add(channel_state_router,
			bla_ringing_type(),
			bla_ringing_cb,
			NULL)) {
		manager_confbridge_shutdown();
		return -1;
	}
}

void bla_stasis_shutdown(void)
{
	STASIS_MESSAGE_TYPE_CLEANUP(bla_ringing_type);

	if (bridge_state_router) {
		stasis_message_router_unsubscribe(bridge_state_router);
		bridge_state_router = NULL;
	}

	if (channel_state_router) {
		stasis_message_router_unsubscribe(channel_state_router);
		channel_state_router = NULL;
	}
}

/**
 * This function was adapted from the confbridge_publish_manager_event()
 * function in confbridge_manager.c. BLA events are not always
 * associated with a Conference, and they are often associated with a BLA
 * station, a BLA trunk, or both.
 */
static void bla_publish_manager_event(
		struct stasis_message *message,
		const char *event,
		struct ast_str *extra_text)
{
	/* TODO: Define a bla_blob */
//	struct ast_bridge_blob *blob = stasis_message_data(message);
	RAII_VAR(struct ast_str *, bridge_text, NULL, ast_free);
	RAII_VAR(struct ast_str *, channel_text, NULL, ast_free);

	manager_event(EVENT_FLAG_CALL, event, "Something: %s\r\n", "FIXME");
}

static void bla_ringing_cb(void *data, struct stasis_subscription *sub,
		struct stasis_message *message)
{
	/*
	RAII_VAR(struct ast_str *, extra_text, NULL, ast_free);

	if (!get_admin_header(&extra_text, message)) {
		confbridge_publish_manager_event(message, "BLARinging", extra_text);
	}
	*/
	bla_publish_manager_event(message, "BLARinging", NULL);
}

/**
 * \breif Asynchronously send stasis events for BLA
 *
 * This function was adapted from the send_conf_stasis() function found in
 * app_confbridge.c. Unlike typical ConfBridge events, some BLA events can
 * happen outside the context of any conference (e.g. when a trunk rings,
 * before a conference is even created).
 */
static void bla_send_stasis(struct stasis_message_type *type, struct ast_json *extras)
{
	RAII_VAR(struct stasis_message *, msg, NULL, ao2_cleanup);
	RAII_VAR(struct ast_json *, json_object, NULL, ast_json_unref);

	json_object = ast_json_pack("{}");
	if (!json_object) {
		return;
	}

	if (extras) {
		ast_json_object_update(json_object, extras);
	}

	msg = ast_bridge_blob_create(type,
		NULL,
		NULL,
		json_object);
	if (!msg) {
		return;
	}

	/* No bridge is associated with this event yet; publish
	 * to ast_bridge_topic_all() */
	stasis_publish(ast_bridge_topic_all(), msg);
}

static void bla_send_ringing_ami_event(struct bla_trunk *trunk)
{
	/*
	struct ast_json *json_object;

	json_object = ast_json_pack("{s: b}",
		"admin", ast_test_flag(&user->u_profile, USER_OPT_ADMIN)
	);
	if (!json_object) {
		return;
	}
	send_conf_stasis(conference, user->chan, confbridge_unmute_type(), json_object, 1);
	ast_json_unref(json_object);
	*/
	bla_send_stasis(bla_ringing_type(), NULL);
}
