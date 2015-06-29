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

/* Forward declarations */
struct ast_assigned_ids;  /* FIXME: asterisk/dial.h needs this forward declaration */

#include "asterisk.h"

#include "asterisk/dial.h"

#include "bla_common.h"

/* FIXME: This should probably be somewhere in dial.c */
const char *bla_dial_result_as_string(enum ast_dial_result dial_result)
{
	switch (dial_result) {
#define _DIAL_RESULT_STRING(type) case type: return #type ;
#define DIAL_RESULT_STRING(type) _DIAL_RESULT_STRING( AST_DIAL_RESULT_ ## type )
		DIAL_RESULT_STRING(INVALID)
		DIAL_RESULT_STRING(FAILED)
		DIAL_RESULT_STRING(TRYING)
		DIAL_RESULT_STRING(RINGING)
		DIAL_RESULT_STRING(PROGRESS)
		DIAL_RESULT_STRING(PROCEEDING)
		DIAL_RESULT_STRING(ANSWERED)
		DIAL_RESULT_STRING(TIMEOUT)
		DIAL_RESULT_STRING(HANGUP)
		DIAL_RESULT_STRING(UNANSWERED)
	}

	return "UNKNOWN";
}
