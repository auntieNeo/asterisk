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

#ifndef _BLA_TRUNK_REF_H
#define _BLA_TRUNK_REF_H

struct bla_trunk_ref {
	char *_name;
};

/*!
 * \brief Accessor for bla_trunk_ref object's name
 * \return bla_trunk_ref name as char array
 *
 * This accessor function simply returns the bla_trunk_ref object's name.
 */
static force_inline const char *bla_trunk_ref_name(const struct bla_trunk_ref *self)
{
	return self->_name;
}

/*!
 * \brief Accessor for setting bla_trunk_ref object's name
 *
 * This accessor function simply sets the bla_trunk_ref object's name.
 */
static force_inline void bla_trunk_ref_set_name(const struct bla_trunk_ref *self, const char *name)
{
	strncpy(self->_name, name, AST_MAX_CONTEXT);
}

int bla_trunk_ref_init(struct bla_trunk_ref *self);

int bla_trunk_ref_destroy(struct bla_trunk_ref *self);

/*!
 * \brief Allocate a bla_trunk_ref object
 * \return Pointer to bla_trunk_ref object allocated with ao2_alloc()
 *
 * This is a convenience function for allocating a bla_trunk_ref object. The
 * returned bla_trunk_ref object is an astobj2 object with one reference count
 * on it.
 */
static force_inline struct bla_trunk_ref *bla_trunk_ref_alloc(void)
{
	return ao2_alloc(sizeof(struct bla_trunk_ref), (ao2_destructor_fn)bla_trunk_ref_destroy);
}

int bla_trunk_ref_hash(void *arg, int flags);

int bla_trunk_ref_cmp(
	const struct bla_trunk_ref *self,
	void *arg,
	int flags);

#endif
