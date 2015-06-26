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

struct bla_event_queue {
	AST_LIST_HEAD_NOLOCK(, bla_event) _events;
};

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

#endif
