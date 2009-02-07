/* Yash: yet another shell */
/* keymap.c: mappings from keys to functions */
/* (C) 2007-2009 magicant */

/* This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  */


#include "../common.h"
#include <assert.h>
#include "../hashtable.h"
#include "../strbuf.h"
#include "display.h"
#include "key.h"
#include "keymap.h"
#include "lineedit.h"
#include "terminfo.h"
#include "trie.h"


/* Definition of editing modes. */
static yle_mode_T yle_modes[YLE_MODE_N];

/* The current editing mode.
 * Points to one of `yle_modes'. */
const yle_mode_T *yle_current_mode;

/* The keymap state. */
static struct {
    int count;
} state;


/* Initializes `yle_modes'.
 * Must not be called more than once. */
void yle_keymap_init(void)
{
    trie_T *t;

    yle_modes[YLE_MODE_VI_INSERT].default_command = cmd_self_insert;
    t = trie_create();
    t = trie_setw(t, Key_backslash, CMDENTRY(cmd_insert_backslash));
    t = trie_setw(t, Key_c_lb, CMDENTRY(cmd_setmode_vicommand));
    t = trie_setw(t, Key_c_j, CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_c_m, CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_interrupt, CMDENTRY(cmd_abort_line));
    //TODO
    yle_modes[YLE_MODE_VI_INSERT].keymap = t;

    yle_modes[YLE_MODE_VI_COMMAND].default_command = cmd_alert;
    t = trie_create();
    t = trie_setw(t, L"i", CMDENTRY(cmd_setmode_viinsert));
    t = trie_setw(t, Key_c_j, CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_c_m, CMDENTRY(cmd_accept_line));
    t = trie_setw(t, Key_interrupt, CMDENTRY(cmd_abort_line));
    //TODO
    yle_modes[YLE_MODE_VI_COMMAND].keymap = t;
}

/* Sets the editing mode to the one specified by `id'. */
void yle_set_mode(yle_mode_id_T id)
{
    assert(id < YLE_MODE_N);
    yle_current_mode = &yle_modes[id];
    yle_keymap_reset();
}

/* Resets the state of keymap. */
void yle_keymap_reset(void)
{
    state.count = -1;
}

/* Invokes the command `cmd' with the argument `c'. */
void yle_keymap_invoke(yle_command_func_T *cmd, wchar_t c)
{
    cmd(state.count, c);
}


/********** Basic Commands **********/

/* Does nothing. */
void cmd_noop(
	int count __attribute__((unused)), wchar_t c __attribute__((unused)))
{
}

/* Same as `cmd_noop', but causes alert. */
void cmd_alert(
	int count __attribute__((unused)), wchar_t c __attribute__((unused)))
{
    yle_alert();
}

/* Inserts one character in the buffer. */
void cmd_self_insert(
	int count __attribute__((unused)), wchar_t c)
{
    if (c != L'\0') {
	wb_ninsert_force(&yle_main_buffer, yle_main_index++, &c, 1);
	yle_display_reprint_buffer();
    }
}

void cmd_insert_backslash(
	int count __attribute__((unused)), wchar_t c __attribute__((unused)))
{
    wb_ninsert_force(&yle_main_buffer, yle_main_index++, L"\\", 1);
    yle_display_reprint_buffer();
}

/* Accepts the current line.
 * `yle_state' is set to YLE_STATE_DONE and `yle_readline' returns. */
void cmd_accept_line(
	int count __attribute__((unused)), wchar_t c __attribute__((unused)))
{
    yle_state = YLE_STATE_DONE;
}

/* Aborts the current line.
 * `yle_state' is set to YLE_STATE_INTERRUPTED and `yle_readline' returns. */
void cmd_abort_line(
	int count __attribute__((unused)), wchar_t c __attribute__((unused)))
{
    yle_state = YLE_STATE_INTERRUPTED;
}

/* Changes the editing mode to "vi insert". */
void cmd_setmode_viinsert(
	int count __attribute__((unused)), wchar_t c __attribute__((unused)))
{
    yle_set_mode(YLE_MODE_VI_INSERT);
}

/* Changes the editing mode to "vi command". */
void cmd_setmode_vicommand(
	int count __attribute__((unused)), wchar_t c __attribute__((unused)))
{
    yle_set_mode(YLE_MODE_VI_COMMAND);
}


/* vim: set ts=8 sts=4 sw=4 noet: */
