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

#ifndef _BLA_EVENT_H
#define _BLA_EVENT_H

/* Forward declarations */
struct ast_dial;
struct bla_station;
struct bla_trunk;

#include "asterisk.h"

#include "asterisk/astobj2.h"
#include "asterisk/time.h"

enum bla_event_types {
	BLA_RING_STATION_EVENT = 1,
	BLA_STATION_DIAL_STATE_EVENT,
};

union bla_event_data {
	struct bla_ring_station_event {
		struct bla_station *station;
		struct bla_trunk *trunk;
	} ring_station_event;
	struct bla_station_dial_state_event {
		struct bla_station *station;
		struct bla_trunk *trunk;
		struct ast_dial *dial;
	} station_dial_state_event;
};

struct bla_event {
	int _type;
	struct timeval _timestamp;
	union bla_event_data _data;
	AST_LIST_ENTRY(bla_event) _list_entry;
};

/*!
 * \brief Initialize a bla_event object
 * \param self Pointer to the bla_event object to initialize
 * \param type Type of the event, a bla_event_types enum value
 * \param bla_event_data The type-specific data to include with the event
 * \param timestamp The realtime timestamp for the event
 * \retval 0 on success
 * \retval non-zero on failure
 *
 * This function initializes a BLA event object. The caller must supply the
 * event type and all of the event data at initialization. Different event
 * types can be found in the bla_event_types enum, and their associated data
 * can be found in the bla_event_data union.
 *
 * The timestamp value can influence the priority of the event in the queue or
 * the behavior of event handlers. The timestamp is usually the current time,
 * but not necessarily.
 */
int bla_event_init(
	struct bla_event *self,
	int type,
	union bla_event_data *data,
	struct timeval timestamp);

int bla_event_destroy(struct bla_event *self);

/*!
 * \brief Allocate a bla_event object
 * \return Pointer to bla_event object allocated with ao2_alloc()
 *
 * This is a convenience function for allocating a bla_event object. The
 * returned bla_event object is an astobj2 object with one reference count on
 * it.
 */
static force_inline struct bla_event *bla_event_alloc(void)
{
	return ao2_alloc(sizeof(struct bla_event), (ao2_destructor_fn)bla_event_destroy);
}

/*!
 * \brief Get BLA event type as character string.
 * \param Pointer to the bla_event object
 * \return Name of event type as character string
 *
 * This function returns the name of the type of this event as a character
 * string. Useful for debugging.
 */
const char *bla_event_type_as_string(struct bla_event *self);

/*!
 * \brief Dispatch this BLA event
 * \param self Pointer to the bla_event object
 *
 * This function executes whatever routines need to be executed for events of
 * this type. This amounts to dispatching this event to whichever BLA objects
 * are associated with it.
 */
int bla_event_dispatch(struct bla_event *self);

#endif
