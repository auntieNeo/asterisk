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

#include "asterisk/app.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"

#include "bla/bla_application.h"

static const char bla_station_app[] = "BLAStation";
static const char bla_trunk_app[] = "BLATrunk";

static struct bla_application *app;

static int bla_exec_station(struct ast_channel *chan, const char *data)
{
	char *parse;
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(station_name);
		AST_APP_ARG(trunk_name);
	);

	/* Parse the application arguments */
	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	/* Check for missing station argument */
	if ((args.station_name == NULL) || ast_strlen_zero(args.station_name)) {
		pbx_builtin_setvar_helper(chan, "BLA_RESULT", "FAILED");
		return -1;
	}

	return bla_application_exec_station(app, chan, args.station_name, args.trunk_name);
}

static int bla_exec_trunk(struct ast_channel *chan, const char *args)
{
	return -1;  /* TODO */
}

static int load_module(void)
{
	ast_log(LOG_NOTICE, "Loading BLA module");

	app = bla_application_alloc();

	bla_application_init(app);

	if (bla_application_read_config(app)) {
		ast_log(LOG_ERROR, "Failed to read BLA config; refusing to load app_bla module");
		ao2_ref(app, -1);
		return AST_MODULE_LOAD_DECLINE;
	}

	int result = 0;

	result |= ast_register_application(bla_station_app, bla_exec_station,
		"BLA application for stations dialing out",
		"This is the application that BLA stations should execute "
		"in the dialplan when taken off the hook or dialing a trunk");

	result |= ast_register_application(bla_trunk_app, bla_exec_trunk,
		"BLA application for trunks dialing in",
		"This is the application that BLA trunks should execute "
		"in the dialplan when dialing into Asterisk.");

	if (result != 0)
		/* FIXME: Might need to free resources ourselves here? */
		return AST_MODULE_LOAD_FAILURE;

	return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void) {
	ao2_ref(app, -1);
	// TODO: assert that the app refcount is now zero

	return 0;
}

static int reload_module(void)
{
	return 0;
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_LOAD_ORDER, "Bridged Line Appearances",
	.support_level = AST_MODULE_SUPPORT_EXTENDED,
	.load = load_module,
	.unload = unload_module,
	.reload = reload_module,
	.load_pri = AST_MODPRI_DEFAULT,
);
