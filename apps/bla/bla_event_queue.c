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

#include "asterisk/time.h"
#include "asterisk/utils.h"

#include "bla_event.h"
#include "bla_station.h"

#include "bla_event_queue.h"

int bla_event_queue_init(struct bla_event_queue *self)
{
	self->_thread_args = NULL;

	/* TODO: Initialize the linked list of event objects? */
	return 0;
}

int bla_event_queue_destroy(struct bla_event_queue *self)
{
	/* TODO: Destroy the linked list of event objects */
	return 0;
}

struct bla_event_queue_thread_args {
	int todo;
};
static void *bla_event_queue_thread(struct bla_event_queue_thread_args *args)
{
	ast_log(LOG_NOTICE, "Entering BLA event thread");

	/* TODO: Loop to wait for and handle events */

	ast_log(LOG_NOTICE, "Leaving BLA event thread");

	return NULL;
}

int bla_event_queue_start(struct bla_event_queue *self)
{
	/* Prepare the event thread resources */
	ast_assert(self->_thread_args == NULL);
	self->_thread_args = ast_malloc(sizeof(struct bla_event_queue_thread_args));
	self->_thread_args->todo = 0;

	/* Start the event thread */
	ast_log(LOG_NOTICE, "Starting BLA event thread");
	ast_pthread_create_detached_background(
		&self->_thread, NULL, (void *(*)(void*))bla_event_queue_thread, self->_thread_args);

	return 0;
}

void bla_event_queue_join(struct bla_event_queue *self)
{
	/* TODO: Signal the event thread to stop */

	/* Join the event thread */
	pthread_join(self->_thread, NULL);

	/* Destroy the event thread resources */
	ast_free(self->_thread_args);
	self->_thread_args = NULL;
}

int bla_event_queue_ring_station(
	struct bla_event_queue *self,
	struct bla_station *station,
	struct bla_trunk *trunk)
{
	struct bla_event *event;
	union bla_event_data data;

	/* Build the ring station event */
	data.station_ring_event.station = station;
	data.station_ring_event.trunk = trunk;

	event = bla_event_alloc();  /* FIXME: Make sure someone handles the bla_event reference */
	if (bla_event_init(event, BLA_RING_STATION_EVENT, &data, ast_tvnow())) {
		ast_log(LOG_ERROR, "Failed to init ring event for BLA station '%s'",
			bla_station_name(station));
		return -1;
	}

	/* TODO: Queue up this event */
/*	bla_event_queue_enqueue(self, event); */

	return 0;
}
