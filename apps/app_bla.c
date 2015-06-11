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

/*! \file
 *
 * \brief Bridged Line Appearances application
 *
 * \author\verbatim Jonathan Glines <auntieNeo@gmail.com> \endverbatim
 *
 * This application implements Bridged Line Appearances (also known as Shared
 * Line Appearances) using the Asterisk bridging core API.
 *
 * \ingroup applications
 */

#include "asterisk.h"

#include "asterisk/logger.h"
#include "asterisk/module.h"

#include "bla/bla_application.h"

static struct bla_application *app;

static int load_module(void) {
	app = ao2_alloc(sizeof(struct bla_application), (ao2_destructor_fn)bla_application_destroy);

	bla_application_init(app);

	bla_application_read_config(app);

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void) {
	ao2_ref(app, -1);
	// TODO: assert that the app refcount is now zero

	return 0;
}

static int reload_module(void) {
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Bridged Line Appearances",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_DEFAULT,
);
