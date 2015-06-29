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

#include "bla_station.h"
#include "bla_trunk.h"

#include "bla_event.h"

int bla_event_init(
	struct bla_event *self,
	int type,
	union bla_event_data *data,
	struct timeval timestamp)
{
	self->_type = type;

	/* Initialize the event data based on its type */
	switch (type) {
		case BLA_RING_STATION_EVENT :
			{
				struct bla_ring_station_event *src, *dest;
				src = &data->ring_station_event;
				dest = &self->_data.ring_station_event;
				dest->station = src->station;
				dest->trunk = src->trunk;
			}
			break;
		case BLA_STATION_DIAL_STATE_EVENT:
			{
				struct bla_station_dial_state_event *src, *dest;
				src = &data->station_dial_state_event;
				dest = &self->_data.station_dial_state_event;
				dest->station = src->station;
				dest->trunk = src->trunk;
				dest->dial = src->dial;
			}
			break;
		default:
			ast_log(LOG_ERROR, "Unknown BLA event type '%d'",
				type);
			return -1;
	}

	self->_timestamp = timestamp;

	return 0;
}

int bla_event_destroy(struct bla_event *self)
{
	/* TODO: Destroy our event? Depends on what the event type is. */
	return 0;
}


const char *bla_event_type_as_string(struct bla_event *self)
{
	switch (self->_type) {
#define _BLA_EVENT_STRING(type) case type: return #type ;
#define BLA_EVENT_STRING(type) _BLA_EVENT_STRING( BLA_ ## type ## _EVENT )
		BLA_EVENT_STRING(RING_STATION)
		BLA_EVENT_STRING(STATION_DIAL_STATE)
	}

	ast_log(LOG_ERROR, "No string for event type '%d'",
		self->_type);

	return "BLA_UNKNOWN_EVENT";
}

int bla_event_dispatch(struct bla_event *self)
{
	/* Determine what to do based on the event type */
	ast_log(LOG_NOTICE, "Dispatching BLA event of type '%s'",
		bla_event_type_as_string(self));
	switch (self->_type) {
		case BLA_RING_STATION_EVENT:
			{
				struct bla_ring_station_event *event;
				event = &self->_data.ring_station_event;
				ast_log(LOG_NOTICE, "Dispatching '%s' event for station '%s' from trunk '%s'",
					bla_event_type_as_string(self),
					bla_station_name(event->station),
					bla_trunk_name(event->trunk));
				/* Dispatch ring event to station object */
				return bla_station_handle_ring_event(
					event->station,
					event->trunk,
					self->_timestamp);
			}
		case BLA_STATION_DIAL_STATE_EVENT:
			{
				struct bla_station_dial_state_event *event;
				event = &self->_data.station_dial_state_event;
				ast_log(LOG_NOTICE, "Dispatching '%s' event for station '%s'",
					bla_event_type_as_string(self),
					bla_station_name(event->station));
				/* Dispatch dial state event to station object */
				return bla_station_handle_dial_state_event(
					event->station,
					event->trunk,
					event->dial,
					self->_timestamp);
			}
	}

	ast_log(LOG_ERROR, "Tried to dispatch BLA event of unknown type '%s'",
			bla_event_type_as_string(self));
	return -1;
}
