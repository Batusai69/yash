/* Yash: yet another shell */
/* display.c: display control */
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
#if HAVE_GETTEXT
# include <libintl.h>
#endif
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>
#include "../history.h"
#include "../job.h"
#include "../option.h"
#include "../strbuf.h"
#include "../util.h"
#include "display.h"
#include "editing.h"
#include "terminfo.h"


/* Characters displayed on the screen by lineedit are divided into three parts:
 * the prompt, the edit line and the info area.
 * The prompt is immediately followed by the edit line (on the same line) but
 * the info area are always on separate lines.
 * The three parts are printed in this order, from upper to lower.
 * When history search is active, the edit line is replaced by the temporary
 * search result line and the search target line.
 *
 * We have to track the cursor position each time we print a character on the
 * screen, so that we can correctly reposition the cursor later. To do this, we
 * use a buffer accompanied by a position data (`lebuf'). The `lebuf_putwchar'
 * function appends a wide character and updates the position data accordingly.
 *
 * In most terminals, when as many characters are printed as the number of
 * the columns, the cursor temporarily sticks to the end of the line. The cursor
 * moves to the next line immediately before the next character is printed. So
 * we must take care not to move the cursor (by the "cub1" capability, etc.)
 * when the cursor is sticking, or we cannot track the cursor position
 * correctly. To deal with this problem, if we finish printing a text at the end
 * of a line, we print a dummy space character and erase it to ensure the cursor
 * is no longer sticking. */


#if !HAVE_WCWIDTH
# undef wcwidth
# define wcwidth(c) (iswprint(c) ? 1 : 0)
#endif


/********** The Print Buffer **********/

static void lebuf_wprintf(bool convert_cntrl, const wchar_t *format, ...)
    __attribute__((nonnull(2)));

/* The print buffer. */
struct lebuf_T lebuf;

/* Initializes the print buffer with the specified position data. */
void lebuf_init(le_pos_T p)
{
    lebuf.pos = p;
    sb_init(&lebuf.buf);
}

/* Appends the specified byte to the print buffer without updating the position
 * data. Returns the appended character `c'. */
/* The signature of this function is intentionally aligned to that of the
 * `putchar' function. */
int lebuf_putchar(int c)
{
    sb_ccat(&lebuf.buf, TO_CHAR(c));
    return c;
}

/* Appends the specified wide character to the print buffer without updating the
 * position data. */
void lebuf_putwchar_raw(wchar_t c)
{
    sb_printf(&lebuf.buf, "%lc", (wint_t) c);
}

/* Appends the specified wide character to the print buffer and updates the
 * position data accordingly.
 * The buffer and `le_columns' must have been initialized.
 * If `convert_cntrl' is true, a non-printable character is converted to the
 * '^'-prefixed form or the bracketed form. */
void lebuf_putwchar(wchar_t c, bool convert_cntrl)
{
    int width = wcwidth(c);
    if (width > 0) {
	/* printable character */
	int new_column = lebuf.pos.column + width;
	if (le_ti_xenl ? new_column <= le_columns : new_column < le_columns)
	    lebuf.pos.column = new_column;
	else
	    lebuf.pos.line++, lebuf.pos.column = width;
	lebuf_putwchar_raw(c);
    } else {
	/* non-printable character */
	if (!convert_cntrl) {
	    switch (c) {
		case L'\a':
		    lebuf_print_alert(false);
		    return;
		case L'\n':
		    lebuf_print_nel();
		    return;
		case L'\r':
		    lebuf_print_cr();
		    return;
		default:
		    lebuf_putwchar_raw(c);
		    return;
	    }
	} else {
	    if (c < L'\040') {
		lebuf_putwchar(L'^',        false);
		lebuf_putwchar(c + L'\100', true);
	    } else if (c == L'\177') {
		lebuf_putwchar(L'^',        false);
		lebuf_putwchar(L'?',        false);
	    } else {
		lebuf_wprintf(false, L"<%jX>", (uintmax_t) c);
	    }
	}
    }
}

/* Appends the specified string to the print buffer. */
void lebuf_putws(const wchar_t *s, bool convert_cntrl)
{
    while (*s != L'\0') {
	lebuf_putwchar(*s, convert_cntrl);
	s++;
    }
}

/* Appends the formatted string to the print buffer. */
void lebuf_wprintf(bool convert_cntrl, const wchar_t *format, ...)
{
    va_list args;
    wchar_t *s;

    va_start(args, format);
    s = malloc_vwprintf(format, args);
    va_end(args);

    lebuf_putws(s, convert_cntrl);
    free(s);
}


/********** Displaying **********/

static void clean_up(void);
static void clear_to_end_of_screen(void);
static void clear_editline(void);
static void maybe_print_promptsp(void);
static void print_prompt(const wchar_t *s)
    __attribute__((nonnull));
static const wchar_t *print_color_seq(const wchar_t *s)
    __attribute__((nonnull));
static void update_editline(void);
static void update_right_prompt(void);
static void print_search(void);
static void go_to(le_pos_T p);
static void go_to_index(size_t index);
static void fillip_cursor(void);


/* True when the prompt is displayed on the screen. */
/* The print buffer, `current_editline', `cursor_positions', and `rprompt' are
 * valid iff `display_active' is true. */
static bool display_active = false;
/* The current cursor position. */
static le_pos_T currentp;
/* The maximum line count. */
static int line_max;

/* The string printed as the prompt, right prompt, after prompt.
 * May contain escape sequences. */
static wchar_t *prompt, *right_prompt, *after_prompt;

/* The position of the first character of the edit line, just after the prompt.
 */
static le_pos_T editbasep;
/* The content of the edit line that is currently displayed on the screen. */
static wchar_t *current_editline = NULL;
/* An array of cursor positions of each character in the edit line.
 * If the nth character of `current_editline' is positioned at line `l', column
 * `c', then cursor_positions[n] == l * le_columns + c. */
static int *cursor_positions = NULL;
/* The line number of the last edit line (or the search buffer). */
static int last_edit_line;

/* The line on which the right prompt is displayed.
 * The value is -1 when the right prompt is not displayed. */
static int rprompt_line;
/* The value of the right prompt where escape sequences have been processed. */
static struct {
    char *value;
    size_t length;  /* number of bytes in `value' */
    int width;      /* width of right prompt on screen */
} rprompt = {
    .value = NULL
};


/* Initializes the display module.
 * The arguments are the main, right, and after prompt, which are freed when the
 * display is finalized.
 * Must be called after `le_editing_init'. */
void le_display_init(
	wchar_t *prompt_, wchar_t *right_prompt_, wchar_t *after_prompt_)
{
    prompt = prompt_;
    right_prompt = right_prompt_;
    after_prompt = after_prompt_;

    currentp.line = currentp.column = 0;
}

/* Finalizes the display module.
 * Must be called before `le_editing_finalize'. */
void le_display_finalize(void)
{
    assert(le_search_buffer.contents == NULL);

    le_main_index = le_main_buffer.length;
    le_display_update();
    //TODO after prompt

    free(prompt);
    free(right_prompt);
    free(after_prompt);

    if (lebuf.pos.column != 0 || lebuf.pos.line == 0)
	lebuf_print_nel();
    while (lebuf.pos.line <= rprompt_line)
	lebuf_print_nel();
    clean_up();
}

/* Clears prompt, edit line and info area on the screen. */
void le_display_clear(void)
{
    if (display_active) {
	lebuf_init(currentp);
	go_to((le_pos_T) { 0, 0 });
	clean_up();
    }
}

void clean_up(void)
{
    assert(display_active);

    clear_to_end_of_screen();

    free(current_editline), current_editline = NULL;
    free(cursor_positions), cursor_positions = NULL;
    free(rprompt.value),    rprompt.value = NULL;

    le_display_flush();
    display_active = false;
}

/* Flushes the contents of the print buffer to the standard error and destroys
 * the buffer. */
void le_display_flush(void)
{
    currentp = lebuf.pos;
    fwrite(lebuf.buf.contents, 1, lebuf.buf.length, stderr);
    fflush(stderr);
    sb_destroy(&lebuf.buf);
}

/* Clears the screen area below the cursor.
 * The cursor must be at the beginning of a line. */
void clear_to_end_of_screen(void)
{
    assert(lebuf.pos.column == 0);
    if (lebuf_print_ed()) /* if the terminal has "ed" capability, just use it */
	return;

    int saveline = lebuf.pos.line;
    for (;;) {
	lebuf_print_el();
	if (lebuf.pos.line >= line_max)
	    break;
	lebuf_print_nel();
    }
    go_to((le_pos_T) { saveline, 0 });
}

/* Clears (part of) the edit line on the screen: from the current cursor
 * position to the end of the edit line.
 * The prompt and the info area are not cleared.
 * When this function is called, the cursor must be positioned within the edit
 * line. When this function returns, the cursor is moved back to that position.
 */
void clear_editline(void)
{
    assert(lebuf.pos.line > editbasep.line
	    || (lebuf.pos.line == editbasep.line
		&& lebuf.pos.column >= editbasep.column));
    assert(lebuf.pos.line <= last_edit_line);

    rprompt_line = -1;

    le_pos_T save_pos = lebuf.pos;

    for (;;) {
	lebuf_print_el();
	if (lebuf.pos.line >= last_edit_line)
	    break;
	lebuf_print_nel();
    }

    go_to(save_pos);
}

/* (Re)prints the display appropriately and moves the cursor to the proper
 * position. The print buffer must not have been initialized.
 * The output is sent to the print buffer. */
void le_display_update(void)
{
    if (!display_active) {
	display_active = true;
	last_edit_line = line_max = 0;
	rprompt_line = -1;

	/* prepare the right prompt */
	lebuf_init((le_pos_T) { 0, 0 });
	print_prompt(right_prompt);
	if (lebuf.pos.line != 0) {  /* right prompt must be one line */
	    sb_truncate(&lebuf.buf, 0);
	    /* lebuf.pos.line = */ lebuf.pos.column = 0;
	}
	rprompt.value = lebuf.buf.contents;
	rprompt.length = lebuf.buf.length;
	rprompt.width = lebuf.pos.column;

	/* print main prompt */
	lebuf_init((le_pos_T) { 0, 0 });
	maybe_print_promptsp();
	print_prompt(prompt);
	editbasep = lebuf.pos;
	if (!le_ti_msgr)
	    lebuf_print_sgr0();
    } else {
	lebuf_init(currentp);
    }

    if (le_search_buffer.contents == NULL) {
	/* print edit line */
	update_editline();
    } else {
	/* print search line */
	print_search();
	return;
    }

    /* print right prompt */
    update_right_prompt();

    // TODO: print info area

    /* set cursor position */
    assert(le_main_index <= le_main_buffer.length);
    go_to_index(le_main_index);
}

/* Prints a dummy string that moves the cursor to the first column of the next
 * line if the cursor is not at the first column.
 * This function does nothing if the "le-promptsp" option is not set. */
void maybe_print_promptsp(void)
{
    if (shopt_le_promptsp) {
	lebuf_print_smso();
	lebuf_putchar('$');
	lebuf_print_sgr0();
	for (int i = le_ti_xenl ? 1 : 2; i < le_columns; i++)
	    lebuf_putchar(' ');
	lebuf_print_cr();
	lebuf_print_ed();
    }
}

/* Prints the specified prompt string.
 * Escape sequences, which are defined in "../input.c", are handled in this
 * function. */
void print_prompt(const wchar_t *s)
{
    le_pos_T save_pos = lebuf.pos;

    while (*s != L'\0') {
	if (*s != L'\\') {
	    lebuf_putwchar(*s, false);
	} else switch (*++s) {
	    default:     lebuf_putwchar(*s,      false);  break;
	    case L'\0':  lebuf_putwchar(L'\\',   false);  goto done;
//	    case L'\\':  lebuf_putwchar(L'\\',   false);  break;
	    case L'a':   lebuf_putwchar(L'\a',   false);  break;
	    case L'e':   lebuf_putwchar(L'\033', false);  break;
	    case L'n':   lebuf_putwchar(L'\n',   false);  break;
	    case L'r':   lebuf_putwchar(L'\r',   false);  break;
	    case L'$':   lebuf_putwchar(geteuid() ? L'$' : L'#', false);  break;
	    case L'j':   lebuf_wprintf(false, L"%zu", job_count());       break;
	    case L'!':   lebuf_wprintf(false, L"%u",  hist_next_number);  break;
	    case L'[':   save_pos = lebuf.pos;    break;
	    case L']':   lebuf.pos = save_pos;    break;
	    case L'f':   s = print_color_seq(s);  continue;
	}
	s++;
    }
done:;
}

/* Prints a sequence to change the terminal font.
 * When this function is called, `*s' must be L'f' after the backslash.
 * This function returns a pointer to the character just after the escape
 * sequence. */
const wchar_t *print_color_seq(const wchar_t *s)
{
    assert(s[-1] == L'\\');
    assert(s[ 0] == L'f');

#define SETFG(color) \
	lebuf_print_setfg(LE_COLOR_##color + (s[1] == L't' ? 8 : 0))
#define SETBG(color) \
	lebuf_print_setbg(LE_COLOR_##color + (s[1] == L't' ? 8 : 0))

    for (;;) switch (*++s) {
	case L'k':  SETFG(BLACK);    break;
	case L'r':  SETFG(RED);      break;
	case L'g':  SETFG(GREEN);    break;
	case L'y':  SETFG(YELLOW);   break;
	case L'b':  SETFG(BLUE);     break;
	case L'm':  SETFG(MAGENTA);  break;
	case L'c':  SETFG(CYAN);     break;
	case L'w':  SETFG(WHITE);    break;
	case L'K':  SETBG(BLACK);    break;
	case L'R':  SETBG(RED);      break;
	case L'G':  SETBG(GREEN);    break;
	case L'Y':  SETBG(YELLOW);   break;
	case L'B':  SETBG(BLUE);     break;
	case L'M':  SETBG(MAGENTA);  break;
	case L'C':  SETBG(CYAN);     break;
	case L'W':  SETBG(WHITE);    break;
	case L'd':  lebuf_print_op();     break;
	case L'D':  lebuf_print_sgr0();   break;
	case L's':  lebuf_print_smso();   break;
	case L'u':  lebuf_print_smul();   break;
	case L'v':  lebuf_print_rev();    break;
	case L'n':  lebuf_print_blink();  break;
	case L'i':  lebuf_print_dim();    break;
	case L'o':  lebuf_print_bold();   break;
	case L'x':  lebuf_print_invis();  break;
	default:    if (!iswalnum(*s)) goto done;
    }
done:
    if (*s == L'.')
	s++;
    return s;

#undef SETFG
#undef SETBG
}

/* Prints the content of the edit line.
 * The cursor may be anywhere when this function is called.
 * The cursor is left at an unspecified position when this function returns. */
void update_editline(void)
{
    size_t index = 0;

    if (current_editline != NULL) {
	/* We only reprint what have been changed from the last update:
	 * skip the unchanged part at the beginning of the line. */
	assert(cursor_positions != NULL);

	while (current_editline[index] != L'\0'
		&& current_editline[index] == le_main_buffer.contents[index])
	    index++;

	/* return if nothing has changed */
	if (current_editline[index] == L'\0'
		&& le_main_buffer.contents[index] == L'\0')
	    return;

	go_to_index(index);
	if (current_editline[index] != L'\0')
	    clear_editline();
    } else {
	/* print the whole edit line */
	go_to(editbasep);
	clear_editline();
    }

    current_editline = xrealloc(current_editline,
	    sizeof *current_editline * (le_main_buffer.length + 1));
    cursor_positions = xrealloc(cursor_positions,
	    sizeof *cursor_positions * (le_main_buffer.length + 1));
    while (index < le_main_buffer.length) {
	wchar_t c = le_main_buffer.contents[index];
	current_editline[index] = c;
	cursor_positions[index]
	    = lebuf.pos.line * le_columns + lebuf.pos.column;
	lebuf_putwchar(c, true);
	index++;
    }
    assert(index == le_main_buffer.length);
    current_editline[index] = L'\0';
    cursor_positions[index] = lebuf.pos.line * le_columns + lebuf.pos.column;

    fillip_cursor();
    last_edit_line = lebuf.pos.line;

    /* clear the right prompt if the edit line reaches it. */
    if (rprompt_line == lebuf.pos.line
	    && lebuf.pos.column > le_columns - rprompt.width - 2) {
	lebuf_print_el();
	rprompt_line = -1;
    }
}

/* Prints the right prompt if there is enough room in the edit line.
 * The edit line must have been printed when this function is called. */
void update_right_prompt(void)
{
    if (rprompt_line >= 0)
	return;
    if (rprompt.width == 0)
	return;
    int c = cursor_positions[le_main_buffer.length] % le_columns;
    if (c > le_columns - rprompt.width - 2)
	return;

    go_to_index(le_main_buffer.length);
    lebuf_print_el();
    lebuf_print_cuf(le_columns - rprompt.width - lebuf.pos.column - 1);
    sb_ncat_force(&lebuf.buf, rprompt.value, rprompt.length);
    lebuf.pos.column += rprompt.width;
    rprompt_line = lebuf.pos.line;
}

/* Prints the current search result and the search line.
 * The cursor may be anywhere when this function is called.
 * Characters after the prompt are cleared in this function.
 * When this function returns, the cursor is left after the search line. */
void print_search(void)
{
    assert(le_search_buffer.contents != NULL);

    free(current_editline), current_editline = NULL;
    free(cursor_positions), cursor_positions = NULL;

    go_to(editbasep);
    clear_editline();

    if (le_search_result != Histlist)
	lebuf_wprintf(true, L"%s", le_search_result->value);
    if (lebuf.pos.column > 0)
	lebuf_print_nel();
    clear_to_end_of_screen();

    const char *text;
    switch (le_search_type) {
	case SEARCH_VI:
	    switch (le_search_direction) {
		case FORWARD:   lebuf_putwchar(L'?', false);  break;
		case BACKWARD:  lebuf_putwchar(L'/', false);  break;
		default:        assert(false);
	    }
	    break;
	case SEARCH_EMACS:
	    switch (le_search_direction) {
		case FORWARD:   text = "Forward search: ";   break;
		case BACKWARD:  text = "Backward search: ";  break;
		default:        assert(false);
	    }
	    lebuf_wprintf(false, L"%s", gt(text));
	    break;
    }
    lebuf_putws(le_search_buffer.contents, true);

    fillip_cursor();
    last_edit_line = lebuf.pos.line;
}

/* Moves the cursor to the specified position.
 * The target column must be less than `le_columns'. */
void go_to(le_pos_T p)
{
    if (line_max < lebuf.pos.line)
	line_max = lebuf.pos.line;

    assert(p.line <= line_max);
    assert(p.column < le_columns);

    if (p.line == lebuf.pos.line) {
	if (lebuf.pos.column < p.column)
	    lebuf_print_cuf(p.column - lebuf.pos.column);
	else if (lebuf.pos.column > p.column)
	    lebuf_print_cub(lebuf.pos.column - p.column);
	return;
    }

    lebuf_print_cr();
    if (lebuf.pos.line < p.line)
	lebuf_print_cud(p.line - lebuf.pos.line);
    else if (lebuf.pos.line > p.line)
	lebuf_print_cuu(lebuf.pos.line - p.line);
    if (p.column > 0)
	lebuf_print_cuf(p.column);
}

/* Moves the cursor to the character of the specified index in the main buffer.
 * This function relies on `cursor_positions', so `update_editline()' must have
 * been called beforehand. */
void go_to_index(size_t index)
{
    int p = cursor_positions[index];
    go_to((le_pos_T) { .line = p / le_columns, .column = p % le_columns });
}

/* If the cursor is sticking to the end of line, moves it to the next line. */
void fillip_cursor(void)
{
    if (lebuf.pos.column >= le_columns) {
	lebuf_putwchar(L' ',  false);
	lebuf_putwchar(L'\r', false);
	lebuf_print_el();
    }
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
