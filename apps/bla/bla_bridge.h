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

#ifndef _BLA_BRIDGE_H
#define _BLA_BRIDGE_H

/* Forward declarations */
struct bla_trunk;

#include "asterisk.h"

#include "asterisk/bridge.h"

struct bla_bridge {
	struct ast_bridge _bridge;
	char _trunk_name[AST_MAX_CONTEXT];
};

int bla_bridge_init(struct bla_bridge *self);

int bla_bridge_destroy(struct bla_bridge *self);

/*!
 * \brief Allocate a bla_bridge object
 * \return Pointer to bla_bridge object allocated with ao2_alloc()
 *
 * This is a convenience function for allocating a bla_bridge object. The
 * returned bla_bridge object is an astobj2 object with one reference count on
 * it.
 */
static force_inline struct bla_bridge *bla_bridge_alloc(void)
{
	return ao2_alloc(sizeof(struct bla_bridge), (ao2_destructor_fn)bla_bridge_destroy);
}

/*!
 * \brief Join the trunk's channel to the bridge (blocking)
 * \param self Pointer to the bla_bridge object to join to
 * \param trunk The trunk to join to this bridge
 *
 * This function uses the Asterisk bridging API to join the given trunk to this
 * bridge. This includes setting the bridging features appropriate for this
 * particular trunk. This function will block until the trunk has left the
 * bridge.
 *
 * Typically, a trunk will only ever join its own bridge.
 */
int bla_bridge_join_trunk(struct bla_bridge *self, struct bla_trunk *trunk);

#endif
