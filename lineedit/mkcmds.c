/* Yash: yet another shell */
/* mkcmds.c: outputs string for 'commands.in' contents */
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
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *commands[] = {
    "noop",
    "alert",
    "self-insert",
    "insert-tab",
    "expect-verbatim",
    "digit-argument",
    "bol-or-digit",
    "accept-line",
    "abort-line",
    "eof-if-empty",
    "eof-or-delete",
    "accept-with-hash",
    "setmode-viinsert",
    "setmode-vicommand",
    "setmode-emacs",
    "expect-char",
    "abort-expect-char",
    "redraw-all",
    "clear-and-redraw-all",
    "forward-char",
    "backward-char",
    "forward-bigword",
    "end-of-bigword",
    "backward-bigword",
    "forward-semiword",
    "end-of-semiword",
    "backward-semiword",
    "forward-viword",
    "end-of-viword",
    "backward-viword",
    "forward-emacsword",
    "backward-emacsword",
    "beginning-of-line",
    "end-of-line",
    "go-to-column",
    "first-nonblank",
    "find-char",
    "find-char-rev",
    "till-char",
    "till-char-rev",
    "refind-char",
    "refind-char-rev",
    "delete-char",
    "delete-bigword",
    "delete-semiword",
    "delete-viword",
    "delete-emacsword",
    "backward-delete-char",
    "backward-delete-bigword",
    "backward-delete-semiword",
    "backward-delete-viword",
    "backward-delete-emacsword",
    "delete-line",
    "forward-delete-line",
    "backward-delete-line",
    "kill-char",
    "kill-bigword",
    "kill-semiword",
    "kill-viword",
    "kill-emacsword",
    "backward-kill-char",
    "backward-kill-bigword",
    "backward-kill-semiword",
    "backward-kill-viword",
    "backward-kill-emacsword",
    "kill-line",
    "forward-kill-line",
    "backward-kill-line",
    "put-before",
    "put",
    "put-left",
    "put-pop",
    "undo",
    "undo-all",
    "cancel-undo",
    "cancel-undo-all",
    "redo",
    "vi-replace-char",
    "vi-insert-beginning",
    "vi-append",
    "vi-append-end",
    "vi-replace",
    "vi-switch-case",
    "vi-switch-case-char",
    "vi-yank",
    "vi-yank-to-eol",
    "vi-delete",
    "vi-delete-to-eol",
    "vi-change",
    "vi-change-to-eol",
    "vi-change-all",
    "vi-yank-and-change",
    "vi-yank-and-change-to-eol",
    "vi-yank-and-change-all",
    "vi-substitute",
    "vi-append-last-bigword",
    "vi-exec-alias",
    "vi-edit-and-accept",
    "vi-search-forward",
    "vi-search-backward",
    "emacs-transpose-chars",
    "emacs-transpose-words",
    "emacs-upcase-word",
    "emacs-downcase-word",
    "emacs-capitalize-word",
    "emacs-delete-horizontal-space",
    "emacs-just-one-space",
    "emacs-search-forward",
    "emacs-search-backward",
    "oldest-history",
    "newest-history",
    "return-history",
    "oldest-history-eol",
    "newest-history-eol",
    "return-history-eol",
    "next-history",
    "prev-history",
    "next-history-eol",
    "prev-history-eol",
    "srch-self-insert",
    "srch-backward-delete-char",
    "srch-backward-delete-line",
    "srch-continue-forward",
    "srch-continue-backward",
    "srch-accept-search",
    "srch-abort-search",
    "search-again",
    "search-again-rev",
    "search-again-forward",
    "search-again-backward",
};

int sorter(const void *p1, const void *p2)
{
    return strcmp(*(const char **) p1, *(const char **) p2);
}

int main(void)
{
    char buf[80];

    setlocale(LC_ALL, "");

    qsort(commands,
	    sizeof commands / sizeof *commands,
	    sizeof *commands,
	    sorter);

    for (size_t i = 0; i < sizeof commands / sizeof *commands; i++) {
	strcpy(buf, commands[i]);
	for (char *s = buf; *s != '\0'; s++)
	    if (*s == '-')
		*s = '_';
	printf("{ \"%s\", cmd_%s, },\n", commands[i], buf);
    }

    return 0;
}


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
