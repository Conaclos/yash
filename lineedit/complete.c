/* Yash: yet another shell */
/* complete.c: command line completion */
/* (C) 2007-2010 magicant */

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
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "../option.h"
#include "../util.h"
#include "complete.h"
#include "display.h"
#include "editing.h"
#include "lineedit.h"
#include "terminfo.h"


static void free_candidate(void *c)
    __attribute__((nonnull));
static void calculate_common_prefix_length(void);
static void set_candidate(void);
static void finish_word(void);
static void expand_to_all_candidates(void);
static void quote(xwcsbuf_T *buf, const wchar_t *s, le_quote_T quotetype)
    __attribute__((nonnull));
static void compdebug(const char *format, ...)
    __attribute__((nonnull,format(printf,1,2)));

/* A list that contains the current completion candidates.
 * The elements pointed to by `le_candidates.contains[*]' are of type
 * `le_candidate_T'. */
plist_T le_candidates = { .contents = NULL };
/* The index of the currently selected candidate in `le_candidates'.
 * When no candidate is selected, the index is `le_candidates.length'. */
size_t le_selected_candidate_index;

/* The context in which completion takes place. */
le_context_T le_context;
/* The start index of the source word in the main buffer. */
size_t le_source_word_index;
/* The index of the cursor before completion started. */
static size_t insertion_index;
/* The type of quotation at the current position. */
le_quote_T le_quote;

/* The length of the expanded source word. */
static size_t expanded_source_word_length;
/* The length of the longest common prefix of the current candidates. */
static size_t common_prefix_length;


/* Performs command line completion.
 * Existing candidates are deleted, if any, and candidates are computed from
 * the current command line. */
void le_complete(void)
{
    if (shopt_le_compdebug) {
	/* If the `le-compdebug' option is set, the command line is temporarily
	 * cleared during completion.
	 * Note that `shopt_le_compdebug' is referenced only here. During the
	 * completion, we check the value of `le_state' to test if the option
	 * is set. The value of `shopt_le_compdebug' might be changed by a
	 * candidate generator code. */
	le_display_finalize();
	le_restore_terminal();
	le_state = LE_STATE_SUSPENDED_COMPDEBUG;
	compdebug("completion start");
    }

    le_complete_cleanup();
    pl_init(&le_candidates);

    le_context = CTXT_NORMAL;  // TODO
    bool expand_all = false;  // TODO
    le_source_word_index = 0;  // TODO
    le_quote = QUOTE_NORMAL;  // TODO
    insertion_index = le_main_index;
    expanded_source_word_length = 0;  // TODO

    //TODO
    // this is test implementation
    for (int i = 0; i < (int) le_main_index; i++) {
	le_candidate_T *cand = xmalloc(sizeof *cand);
	cand->type = CT_WORD;
	cand->value = malloc_wprintf(L"cand%d", i + 5);
	cand->rawvalue = NULL;
	cand->width = 0;
	pl_add(&le_candidates, cand);
    }
    compdebug("got %zu candidate(s)", le_candidates.length);

    if (le_candidates.length == 0) {
	le_selected_candidate_index = 0;
    } else if (expand_all) {
	le_selected_candidate_index = 0;
	expand_to_all_candidates();
	le_complete_cleanup();
    } else if (le_candidates.length == 1) {
	le_selected_candidate_index = 0;
	set_candidate();
	finish_word();
	le_complete_cleanup();
    } else {
	calculate_common_prefix_length();
	if (le_state == LE_STATE_SUSPENDED_COMPDEBUG) {
	    const le_candidate_T *cand = le_candidates.contents[0];
	    wchar_t *common_prefix
		= xwcsndup(cand->value, common_prefix_length);
	    compdebug("candidate common prefix: \"%ls\"", common_prefix);
	    free(common_prefix);
	}
	le_selected_candidate_index = le_candidates.length;
	set_candidate();
    }

    if (le_state == LE_STATE_SUSPENDED_COMPDEBUG) {
	compdebug("completion end");
	le_setupterm(false);
	le_set_terminal();
	le_state = LE_STATE_ACTIVE;
    }
}

/* Increases `le_selected_candidate_index' by `offset', selecting the `offset'th
 * next candidate. If there are no candidates, simply calls `le_complete' to
 * produce candidates. */
void le_complete_select(int offset)
{
    if (le_candidates.contents == NULL) {
	le_complete();
	return;
    } else if (le_candidates.length == 0) {
	return;
    }

    assert(le_selected_candidate_index <= le_candidates.length);
    offset %= (int) le_candidates.length + 1;
    if (offset < 0)
	offset += (int) le_candidates.length + 1;
    assert(offset >= 0);
    le_selected_candidate_index += offset;
    le_selected_candidate_index %= le_candidates.length + 1;

    set_candidate();
}

/* Clears the current candidates. */
void le_complete_cleanup(void)
{
    le_display_complete_cleanup();
    if (le_candidates.contents != NULL) {
	recfree(pl_toary(&le_candidates), free_candidate);
	le_candidates.contents = NULL;
    }
}

/* Frees a completion candidate.
 * The argument must point to a `le_candidate_T' value. */
void free_candidate(void *c)
{
    le_candidate_T *cand = c;
    free(cand->value);
    free(cand->rawvalue);
    free(cand);
}

/* Calculates the value of `common_prefix_length' for the current candidates.
 * There must be at least one candidate in `le_candidates'. */
void calculate_common_prefix_length(void)
{
    assert(le_candidates.contents != NULL);
    assert(le_candidates.length > 0);

    const le_candidate_T *cand = le_candidates.contents[0];
    const wchar_t *value = cand->value;
    size_t cpl = wcslen(value);
    for (size_t i = 1; i < le_candidates.length; i++) {
	const le_candidate_T *cand2 = le_candidates.contents[i];
	const wchar_t *value2 = cand2->value;
	for (size_t j = 0; j < cpl; j++)
	    if (value[j] != value2[j])  // XXX comparison is case-sensitive
		cpl = j;
    }
    common_prefix_length = cpl;
}

/* Sets the contents of the main buffer to the currently selected candidate.
 * When no candidate is selected, sets to the longest common prefix of the
 * candidates. There must be at least one candidate. */
void set_candidate(void)
{
    const le_candidate_T *cand;
    wchar_t *value;
    xwcsbuf_T buf;

    wb_init(&buf);
    if (le_selected_candidate_index >= le_candidates.length) {
	cand = le_candidates.contents[0];
	value = xwcsndup(cand->value + expanded_source_word_length,
		common_prefix_length - expanded_source_word_length);
	quote(&buf, value, le_quote);
	free(value);
    } else {
	cand = le_candidates.contents[le_selected_candidate_index];
	quote(&buf, cand->value + expanded_source_word_length, le_quote);
    }

    wb_replace_force(&le_main_buffer,
	    insertion_index, le_main_index - insertion_index,
	    buf.contents, buf.length);
    le_main_index = insertion_index + buf.length;
    wb_destroy(&buf);

    // TODO
    if (cand->type == CT_DIR) {
	size_t len = wcslen(cand->value);
	if (len > 0 && cand->value[len - 1] != L'/') {
	    wb_ninsert_force(&le_main_buffer, le_main_index, L"/", 1);
	    le_main_index += 1;
	}
    } else {
	if (le_context == CTXT_NORMAL
		&& le_selected_candidate_index < le_candidates.length) {
	    switch (le_quote) {
		case QUOTE_NONE:
		case QUOTE_NORMAL:
		    break;
		case QUOTE_SINGLE:
		    wb_ninsert_force(&le_main_buffer, le_main_index, L"'", 1);
		    le_main_index += 1;
		    break;
		case QUOTE_DOUBLE:
		    wb_ninsert_force(&le_main_buffer, le_main_index, L"\"", 1);
		    le_main_index += 1;
		    break;
	    }
	}
    }
}

/* Appends a space, slash, or something that should come after the completed
 * word. Must be called only when there is exactly one candidate. */
void finish_word(void)
{
    //TODO
    assert(le_candidates.length == 1);

    wb_ninsert_force(&le_main_buffer, le_main_index, L" ", 1);
    le_main_index += 1;
}

/* Substitutes the source word in the main buffer with all of the current
 * candidates. */
void expand_to_all_candidates(void)
{
    /* remove source word */
    wb_remove(&le_main_buffer, le_source_word_index,
	    le_main_index - le_source_word_index);
    le_main_index = le_source_word_index;

    /* insert candidates */
    xwcsbuf_T buf;
    wb_init(&buf);
    for (size_t i = 0; i < le_candidates.length; i++) {
	const le_candidate_T* cand = le_candidates.contents[i];

	quote(wb_clear(&buf), cand->value, QUOTE_NORMAL);
	wb_ninsert_force(&le_main_buffer, le_main_index,
		buf.contents, buf.length);
	le_main_index += buf.length;

	wb_ninsert_force(&le_main_buffer, le_main_index, L" ", 1);
	le_main_index += 1;
    }
    wb_destroy(&buf);
}

/* Quotes characters in the specified string that are not treated literally
 * according to `quotetype'.
 * The result is appended to the specified buffer, which must have been
 * initialized by the caller. */
void quote(xwcsbuf_T *buf, const wchar_t *s, le_quote_T quotetype)
{
    switch (quotetype) {
	case QUOTE_NONE:
	    wb_cat(buf, s);
	    return;
	case QUOTE_NORMAL:
	    while (*s != L'\0') {
		if (*s == L'\n') {
		    wb_ncat_force(buf, L"'\n'", 3);
		} else {
		    if (wcschr(L"|&;<>()$`\\\"'*?[#~=", *s) || iswspace(*s))
			wb_wccat(buf, L'\\');
		    wb_wccat(buf, *s);
		}
		s++;
	    }
	    return;
	case QUOTE_SINGLE:
	    while (*s != L'\0') {
		if (*s != L'\'')
		    wb_wccat(buf, *s);
		else
		    wb_ncat_force(buf, L"'\\''", 4);
		s++;
	    }
	    return;
	case QUOTE_DOUBLE:
	    while (*s != L'\0') {
		if (wcschr(L"$`\"\\", *s))
		    wb_wccat(buf, L'\\');
		wb_wccat(buf, *s);
		s++;
	    }
	    return;
    }
    assert(false);
}

/* Prints the formatted string to the standard error if the completion debugging
 * option is on.
 * The string is preceded by "[compdebug] " and followed by a newline. */
void compdebug(const char *format, ...)
{
    if (le_state != LE_STATE_SUSPENDED_COMPDEBUG)
	return;

    fputs("[compdebug] ", stderr);

    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);

    fputc('\n', stderr);
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
