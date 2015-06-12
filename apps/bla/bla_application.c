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

#include "asterisk/astobj2.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"

#include "bla_config.h"
#include "bla_station.h"
#include "bla_trunk.h"

#include "bla_application.h"

int bla_application_init(struct bla_application *self)
{
  ast_log(LOG_NOTICE, "Initializing BLA application");

	self->_stations = NULL;
	self->_trunks = NULL;

	return 0;
}

int bla_application_destroy(struct bla_application *self)
{
	if (self->_trunks != NULL)
		ao2_ref(self->_trunks, -1);
	if (self->_stations != NULL)
		ao2_ref(self->_stations, -1);
	/* TODO: Assert that these refcounts are now zero */

	return 0;
}

int bla_application_read_config(struct bla_application *self)
{
	RAII_VAR(struct bla_config *, config, bla_config_alloc(), bla_config_destroy);

  ast_log(LOG_NOTICE, "Application reading BLA config");

	bla_config_init(config);

	if(bla_config_read(config))
		return -1;

	/* Steal the stations and trunks from config before it leaves scope */
	self->_stations = bla_config_stations(config);
	ao2_ref(self->_stations, 1);
	self->_trunks = bla_config_trunks(config);
	ao2_ref(self->_trunks, 1);

	return 0;
}
