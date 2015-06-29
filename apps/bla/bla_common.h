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
enum ast_dial_result;

/*!
 * \brief Convert ast_dial_result value to a string
 * \param dial_result The ast_dial_result enum value to convert
 * \returns String representation of the dial result
 *
 * This function converts an ast_dial_result enum value to a string, which is
 * useful for printing debug messages related to dial states.
 */
const char *bla_dial_result_as_string(enum ast_dial_result dial_result);
