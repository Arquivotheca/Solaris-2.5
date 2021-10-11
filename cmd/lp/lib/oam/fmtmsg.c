/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fmtmsg.c	1.8	93/11/19 SMI"	/* SVr4.0 1.8	*/
/* LINTLIBRARY */

#include "stdio.h"
#include "string.h"

#include "oam.h"
#include <widec.h>
#include <locale.h>

#define LINE_LEN 70

#define SHORT_S 80
#define LONG_S  2000

static char		*severity_names[MAX_SEVERITY-MIN_SEVERITY+1] = {
	"HALT",
	"ERROR",
	"WARNING",
	"INFO"
};

static char		*TOFIX	= "TO FIX";

static int		wrap(wchar_t *, wchar_t *, int, wchar_t *);

/**
 ** fmtmsg()
 **/

void
fmtmsg(char *label, int severity, char *text, char *action)
{
	int	tofix_len, indent_len;
	wchar_t	wtofix[SHORT_S], wlabel[SHORT_S], wsev[SHORT_S], wtext[LONG_S],
		null[1] = {0};

	/*
	 * Return if the severity isn't recognized.
	 */
	if (severity < MIN_SEVERITY || MAX_SEVERITY < severity)
		return;

	mbstowcs(wtofix, gettext(TOFIX), SHORT_S);
	mbstowcs(wlabel, label, SHORT_S);
	mbstowcs(wsev, gettext(severity_names[severity]), SHORT_S);
	mbstowcs(wtext, text, LONG_S);

	tofix_len = wscol(wtofix),
	indent_len = wscol(wlabel) + wscol(wsev) + 2;
	if (indent_len < tofix_len)
		indent_len = tofix_len;

	if (wrap(wlabel, wsev, indent_len, wtext) <= 0)
		return;

	if (action && *action) {
		if (fputc('\n', stderr) == EOF)
			return;

		mbstowcs(wtext, action, LONG_S);
		if (wrap(wtofix, null, indent_len, wtext) <= 0)
			return;
	}

	if (fputc('\n', stderr) == EOF)
		return;

	fflush (stderr);
}

/**
 ** wrap() - PUT OUT "STUFF: string", WRAPPING string AS REQUIRED
 **/

static int
wrap(wchar_t *prefix, wchar_t *suffix, int indent_len, wchar_t *str)
{
	int	len, n, col;
	wchar_t	*p, tmp = 0, *ptmp, eol[3];

	/*
	 * Display the initial stuff followed by a colon.
	 */
	if ((len = wslen(suffix)))
		n = fprintf(stderr, gettext("%*ws: %ws: "),
			indent_len - len - 2, prefix, suffix);
	else
		n = fprintf(stderr, gettext("%*ws: "), indent_len, prefix);
	if (n <= 0)
		return (-1);

	wsprintf(eol, "\r\n");
	/*
	 * Loop once for each line of the string to display.
	 */
	for (p = str; *p; ) {

		/*
		 * Display the next "len" bytes of the string, where
		 * "len" is the smallest of:
		 *
		 *	- LINE_LEN
		 *	- # bytes before control character
		 *	- # bytes left in string
		 *
		 */

		len = wscspn(p, eol);
		/* calc how many columns the string will take */
		ptmp = p + len;
		tmp = *ptmp;
		*ptmp = (wchar_t) 0;
		col = wscol(p);
		if (col > (LINE_LEN - indent_len - 1)) {
			wchar_t	*pw;

			len = LINE_LEN - indent_len - 1;
			/*
			 * Don't split words
			 */
			for (pw = p + len; pw > p && !iswspace(*pw); pw--);
			if (pw != p)
				len = pw - p;
		}

		if (fprintf(stderr, "%.*ws", len, p) <= 0) {
			*ptmp = tmp;
			return (-1);
		}

		*ptmp = tmp;

		/*
		 * If we displayed up to a control character,
		 * put out the control character now; otherwise,
		 * put out a newline unless we've put out all
		 * the text.
		 */
		p += len;
		if (iswspace(*p))
			p++;

		if (*p == (wchar_t) '\r' || *p == (wchar_t) '\n') {

			if (fputc(*p, stderr) == EOF)
				return (-1);
			p++;

		} else if (*p) {
			if (fputc('\n', stderr) == EOF)
				return (-1);
		}

		/*
		 * If the loop won't end this time (because we
		 * have more stuff to display) put out leading
		 * blanks to align the next line with the previous
		 * lines.
		 */
		if (*p)
			if (fprintf(stderr, "%*s", indent_len + 2, " ") <= 0)
				return (-1);
	}

	return (1);
}
