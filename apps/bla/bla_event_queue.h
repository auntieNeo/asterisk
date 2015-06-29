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

#ifndef _BLA_EVENT_QUEUE_H
#define _BLA_EVENT_QUEUE_H

#include <pthread.h>

/* Forward declarations */
struct ast_dial;
struct bla_event_queue_thread_args;

struct bla_event_queue {
	pthread_t _thread;
	struct bla_event_queue_thread_args *_thread_args;
	AST_LIST_HEAD_NOLOCK(, bla_event) _events;
};

/*!
 * \brief Initialize a bla_event_queue object
 * \param self Pointer to the bla_event_queue object to initialize
 *
 * This function initializes the internal structures of a bla_event_queue object.
 *
 * The bla_event_queue object should be allocated with the bla_event_queue_alloc()
 * function before calling this function.
 */
int bla_event_queue_init(struct bla_event_queue *self);

/*!
 * \brief Destroy a bla_event_queue object
 * \param self Pointer to the bla_event_queue object to destroy
 *
 * This function cleans up the internal structures of a bla_event_queue object.
 *
 * Note that the memory for the bla_event_queue structure itself is not freed;
 * if the structure was allocated with bla_event_queue_alloc(), then that memory
 * is managed by astobj2's reference counter.
 *
 * It is assumed that the event queue thread has been stopped with the
 * bla_event_queue_join() function by the time bla_event_queue_destroy() is
 * called.
 */
int bla_event_queue_destroy(struct bla_event_queue *self);

/*!
 * \brief Allocate a bla_event_queue object
 * \return Pointer to bla_event_queue object allocated with ao2_alloc()
 *
 * This is a convenience function for allocating a bla_event_queue object. The
 * returned bla_event_queue object is an astobj2 object with one reference count
 * on it.
 */
static force_inline struct bla_event_queue *bla_event_queue_alloc(void)
{
	return ao2_alloc(sizeof(struct bla_event_queue), (ao2_destructor_fn)bla_event_queue_destroy);
}

/*!
 * \brief Start the event thread for a bla_event_queue object
 * \param self Pointer to the bla_event_queue object
 * \retval 0 on success
 * \retval non-zero on failure
 *
 * This function starts a new thread that regularly dispatches BLA events in a
 * loop. This function does not block.
 *
 * When the event thread needs to be stopped, call the bla_event_queue_join()
 * function.
 */
int bla_event_queue_start(struct bla_event_queue *self);

/*!
 * \brief Join the event thread for a bla_event_queue object
 * \param self Pointer to the bla_event_queue object
 *
 * This function signals the bla_event_queue object's event thread to stop, and
 * it blocks until the thread safely exits.
 */
void bla_event_queue_join(struct bla_event_queue *self);

/*!
 * \brief Add an event to the end of the event queue
 * \param self Pointer to the bla_event_queue object
 * \param event Pointer to the bla_event object to add to the queue
 *
 * This function queues up a new event to the end of the event queue. The queue
 * is FIFO by order added (regardless of event timestamp).
 */
void bla_event_queue_enqueue(struct bla_event_queue *self, struct bla_event *event);

/*!
 * \brief Remove an event from the end of the event queue
 * \param self Pointer to the bla_event_queue object
 * \retval Pointer to the bla_event object removed from the queue
 * \retval NULL if the queue was empty
 *
 * This function removes the oldest bla_event object in the queue and returns a
 * pointer to that object. The queue is FIFO by order added (regardless of event
 * timestamp).
 */
struct bla_event *bla_event_queue_dequeue(struct bla_event_queue *self);

/*!
 * \brief Schedule a ring event for a station
 * \param self Pointer to the bla_event_queue object
 * \param station Pointer to the station being rung
 * \param trunk Pointer to the trunk that initiated the ringing
 *
 * This function schedules a station ring event in the event queue.
 *
 * Some time after this function is called, the event queue thread will notify
 * the station that the given trunk is trying to ring the station. The station
 * can at that time decide what to do based on ring thresholds, timeouts,
 * cooldown, etc.
 */
int bla_event_queue_ring_station(
	struct bla_event_queue *self,
	struct bla_station *station,
	struct bla_trunk *trunk);

/*!
 * \brief Schedule a dial state event for a station
 * \param self Pointer to the bla_event_queue object
 * \param station Pointer to the bla_station whose dial state has changed
 * \param trunk Pointer to the bla_trunk that dialed
 * \param dial Pointer to the ast_dial object that knows the dial state
 *
 * This function schedules a station dial state event. These events are created
 * by a callback set with the ast_dial_set_state_callback() function. Stations
 * handle dial state events to transition themselves from ringing to connected.
 */
int bla_event_queue_station_dial_state(
	struct bla_event_queue *self,
	struct bla_station *station,
	struct bla_trunk *trunk,
	struct ast_dial *dial);

#endif
