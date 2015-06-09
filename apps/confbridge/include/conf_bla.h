#ifndef _CONF_BLA_H
#define _CONF_BLA_H

#define BLA_CONFIG_FILE "bla.conf"

#include "asterisk.h"
#include "asterisk/linkedlists.h"
#include "asterisk/stringfields.h"

#include "confbridge.h"

/* Forward declarations */
struct ast_channel;
struct ast_cli_entry;
struct ast_cli_args;

/* BLA Application Strings */
extern const char bla_station_app[];
extern const char bla_trunk_app[];
extern const char bla_registrar[];

/* BLA Function Prototypes */
int bla_load_config(int reload);
void bla_destroy(void);
int bla_trunk_exec(struct ast_channel *chan, const char *data);
int bla_station_exec(struct ast_channel *chan, const char *data);
char *bla_show_stations(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
char *bla_show_trunks(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a);
enum ast_device_state bla_devstate(const char *data);
/* BLA Stasis Debugging Prototypes */
int bla_stasis_init(void);
void bla_stasis_shutdown(void);


/* TODO: Move most of the following into a header file for each translation unit */
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
			AST_STRING_FIELD(user_profile);
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
	struct ast_channel *chan;  /* FIXME: I am not sure why this is used and not trunk->chan. Must document better. */
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

struct bla_hold_event_args {
	struct bla_station *station;
	struct bla_trunk_ref *trunk_ref;
};

/* Static BLA Function Prototypes */
int bla_build_trunk(struct ast_config *cfg, const char *cat);
int bla_build_station(struct ast_config *cfg, const char *cat);
/* BLA trunk methods */
int bla_trunk_create(void);
void bla_trunk_destroy(struct bla_trunk *self);
int bla_trunk_release_refs(struct bla_trunk *self, void *arg, int flags);
int bla_trunk_hash(const struct bla_trunk *self, const int flags);
int bla_trunk_cmp(const struct bla_trunk *self, const struct bla_trunk *arg, int flags);
/* BLA trunk_ref methods */
struct bla_trunk_ref *bla_trunk_ref_create(struct bla_trunk *trunk);
void bla_trunk_ref_destroy(struct bla_trunk_ref *self);
/* BLA station methods */
int bla_station_create(void);
void bla_station_destroy(struct bla_station *self);
int bla_station_release_refs(struct bla_station *self, void *arg, int flags);
int bla_station_hash(const struct bla_station *self, const int flags);
int bla_station_cmp(const struct bla_station *self, const struct bla_station *arg, int flags);
/* BLA station_ref methods */
struct bla_station_ref *bla_station_ref_create(struct bla_station *station);
void bla_station_ref_destroy(struct bla_station_ref *self);
/* BLA helper functions */
/* TODO: Many of these "helper" functions could be class methods. Need to move these to a better object model. */
/* FIXME: I think I got these all out of order. Oh well. */
int bla_in_use(void);
enum ast_device_state bla_state_to_devstate(enum bla_trunk_state state);
int bla_check_device(const char *dev);
int bla_check_station_hold_access(const struct bla_trunk *trunk, const struct bla_station *station);
struct bla_ringing_trunk *bla_queue_ringing_trunk(struct bla_trunk *trunk);
struct bla_station *bla_find_station(const char *name);
struct bla_trunk *bla_find_trunk(const char *name);
struct bla_trunk_ref *bla_choose_idle_trunk(struct bla_station *station);
struct bla_trunk_ref *bla_find_trunk_ref_byname(const struct bla_station *station, const char *name);
void bla_add_trunk_to_station(struct bla_station *self, struct ast_variable *var);
void bla_change_trunk_state(const struct bla_trunk *trunk, enum bla_trunk_state state, enum bla_which_trunk_refs inactive_only, const struct bla_trunk_ref *exclude);
void bla_queue_event(enum bla_event_type type);
void bla_queue_event_full(enum bla_event_type type, struct bla_trunk_ref *trunk_ref, struct bla_station *station, int lock);
void bla_queue_event_nolock(enum bla_event_type type);
void bla_answer_trunk_chan(struct ast_channel *chan);
void bla_ring_stations(void);
int bla_check_inuse_station(const struct bla_station *station);
int bla_check_failed_station(const struct bla_station *station);
int bla_check_ringing_station(const struct bla_station *station);
int bla_check_timed_out_station(const struct bla_ringing_trunk *ringing_trunk, const struct bla_station *station);
int bla_check_station_delay(struct bla_station *station, struct bla_ringing_trunk *ringing_trunk);
int bla_ring_station(struct bla_ringing_trunk *ringing_trunk, struct bla_station *station);
struct bla_failed_station *bla_create_failed_station(struct bla_station *station);
void bla_failed_station_destroy(struct bla_failed_station *failed_station);
struct bla_ringing_trunk *bla_choose_ringing_trunk(struct bla_station *station, struct bla_trunk_ref **trunk_ref, int rm);
struct bla_trunk_ref *bla_find_trunk_ref(const struct bla_station *station, const struct bla_trunk *trunk);
void bla_dial_state_callback(struct ast_dial *dial);
struct bla_ringing_station *bla_create_ringing_station(struct bla_station *station);
void bla_ringing_station_destroy(struct bla_ringing_station *ringing_station);
void bla_event_destroy(struct bla_event *event);
void bla_stop_ringing_trunk(struct bla_ringing_trunk *ringing_trunk);
void bla_ringing_trunk_destroy(struct bla_ringing_trunk *ringing_trunk);
void bla_stop_ringing_station(struct bla_ringing_station *ringing_station, enum bla_station_hangup hangup);
struct bla_station_ref *bla_create_station_ref(struct bla_station *station);
void bla_station_ref_destructor(struct bla_station_ref *station_ref);
void bla_station_user_profile_name(const struct bla_station *station, const struct bla_trunk *trunk, char *user_profile_name);
void bla_trunk_user_profile_name(const struct bla_trunk *trunk, char *user_profile_name);
void bla_trunk_bridge_profile_name(const struct bla_trunk *trunk, char *bridge_profile_name);
void bla_trunk_conference_name(const struct bla_trunk *trunk, char *conference_name);
void bla_hangup_stations(void);
int bla_hold_consume_callback(struct bla_station *station, enum ast_frame_type type);
struct ast_frame *bla_hold_event_callback(struct ast_channel *chan, struct ast_frame *frame, enum ast_framehook_event event, struct bla_hold_event_args *args);
/* BLA Thread Callback Prototypes */
void *bla_dial_trunk(struct bla_dial_trunk_args *args);
void *bla_run_station(struct bla_run_station_args *args);
void *bla_thread(void *data);
/* BLA Event Function Prototypes */
int bla_process_timers(struct timespec *ts);
int bla_calc_trunk_timeouts(unsigned int *timeout);
int bla_calc_station_timeouts(unsigned int *timeout);
int bla_calc_station_delays(unsigned int *timeout);
void bla_handle_hold_event(struct bla_event *event);
void bla_handle_dial_state_event(void);
void bla_handle_ringing_trunk_event(void);
/* BLA CLI Function Prototypes */
const char *bla_hold_access_str(enum bla_hold_access hold_access);
const char *bla_trunk_state_str(enum bla_trunk_state state);
/* BLA Stasis Debugging Prototypes */
struct stasis_message_type *bla_ringing_type(void);
void bla_publish_manager_event(struct stasis_message *message, const char *event, struct ast_str *extra_text);
void bla_publish_manager_event(struct stasis_message *message, const char *event, struct ast_str *extra_text);
void bla_ringing_cb(void *data, struct stasis_subscription *sub, struct stasis_message *message);
void bla_send_stasis(struct stasis_message_type *type, struct ast_json *extras);
void bla_send_ringing_ami_event(struct bla_trunk *trunk);

#endif
