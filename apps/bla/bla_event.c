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
				src = &data->station_ring_event;
				dest = &self->_data.station_ring_event;
				dest->station = src->station;
				dest->trunk = src->trunk;
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
#define _BLA_EVENT_STRING(type) case type: return #type ;
#define BLA_EVENT_STRING(type) _BLA_EVENT_STRING( BLA_ ## type ## _EVENT )
	switch (self->_type) {
		BLA_EVENT_STRING(RING_STATION)
	}

	return "BLA_UNKNOWN_EVENT";
}

int bla_event_dispatch(struct bla_event *self)
{
	/* Determine what to do based on the event type */
	ast_log(LOG_NOTICE, "Dispatching BLA event of type '%s'",
		bla_event_type_as_string(self));
	switch (self->_type) {
		case BLA_RING_STATION_EVENT:
			/* TODO: Handle BLA ring station event */
			break;
		default:
			ast_log(LOG_ERROR, "Unknown BLA event type '%s'",
				bla_event_type_as_string(self));
			return -1;
	}

	return 0;
}
