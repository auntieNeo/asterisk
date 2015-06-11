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

struct bla_trunk {
	char *_name;
	struct ao2_container *_stations;
};

static force_inline const char *bla_trunk_name(const struct bla_trunk *self)
{
	return self->_name;
}

int bla_trunk_hash(const struct bla_trunk *self, int flags);

int bla_trunk_cmp(
	const struct bla_trunk *self,
	const struct bla_trunk *other,
	int flags);
