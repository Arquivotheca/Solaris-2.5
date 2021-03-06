/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fold.c	1.13	95/03/28 SMI"	/* SVr4.0 1.1	*/

/*
 * *****************************************************************

		PROPRIETARY NOTICE(Combined)

This source code is unpublished proprietary information
constituting, or derived under license from AT&T's UNIX(r) System V.
In addition, portions of such source code were derived from Berkeley
4.3 BSD under license from the Regents of the University of
California.



		Copyright Notice

Notice of copyright on this source code product does not indicate
publication.

	(c) 1986, 1987, 1988, 1989  Sun Microsystems, Inc
	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
		    All rights reserved.
 * *****************************************************************
 */

#include <stdio.h>
#include <locale.h>
#include <wchar.h>
#include <euc.h>
#include <stdlib.h>		/* XCU4 */
#include <limits.h>
#include <libintl.h>
#include <langinfo.h>
#include <utime.h>
#include <widec.h>
#include <wctype.h>
#include <errno.h>


/*
 * fold - fold long lines for finite output devices
 *
 */

static int fold =  80;
static int bflg = 0;
static int sflg = 0;
static int wflg = 0;
static int lastc = 0;
static int col = 0;
static int ncol = 0;
static int spcol = 0;
static wchar_t line[LINE_MAX];
static wchar_t *lastout = line;
static wchar_t *curc = line;
static wchar_t *lastsp = NULL;
#define	MAXARG _POSIX_ARG_MAX

/*
 * Fix lint errors
 */
void exit();
static void Usage();
static void putch();
static void newline_init();
static int chr_width();
extern int errno;
static int get_foldw();


void
main(argc, argv)
	int argc;
	char *argv[];
{
	register int c, narg;
	register int ch;
	char *cmdline[MAXARG];
	int	new_argc;
	int w;
	extern int optind;
	extern char *optarg;


	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	/*
	 * Parse -width separately and build
	 * the new command line without -width.
	 * Next, use getopt() to parse this
	 * new command line.
	 */

	for (narg = new_argc = 0; narg < argc; narg ++) {
		if (argv[narg][0] == '-' &&
			isdigit(argv[narg][1])) {

			if (get_foldw((char *)&argv[narg][1],
					&w) < 0)
				exit(1);

			fold = w;	/* Update with new width */
		} else {
			/* Build the new command line */
			cmdline[new_argc++] = argv[narg];

			/*
			 * Check to make sure the option with
			 * required arg should have arg.
			 * This would check errors introduced in
			 * mixing non-getopt() options and that of
			 * getopt()'s due to stripping non-getopt
			 * options.
			 */
			if ((argv[narg][0] == '-') &&
				(argv[narg][1] == 'w')) {
				if (((narg+1) < argc) &&
					(argv[narg+1][0] == '-')) {
					(void) fprintf(stderr, "fold");
					(void) fprintf(stderr, gettext(
				": option requires an argument -- w\n"));
					Usage();
					exit(1);
				}
			}
		}
	}

	while ((ch = getopt(new_argc, cmdline, "w:bs")) != EOF) {
		switch (ch) {
			case 'b':
				bflg++;
				break;
			case 's':
				sflg++;
				break;
			case 'w':
				wflg++;
				/* No required arg ? */
				if ((optarg == (char *)NULL) ||
					((optarg != (char *)NULL) &&
					(*optarg == '-'))) {
						(void) fprintf(stderr, "fold");
						(void) fprintf(stderr, gettext(
				": option requires an argument -- w\n"));
						Usage();
						exit(1);
				}
				/* Bad number ? */
				if (get_foldw(optarg, &w) < 0)
					exit(1);

				fold = w;
				break;
			default:
				/*
				 * Errors should be filtered in previous
				 * pass.
				 */
				Usage();
				exit(1);
		} /* switch */
	} /* while */

	do {
		if (new_argc > optind) {
			if (freopen(cmdline[optind], "r", stdin) == NULL) {
				perror(cmdline[optind]);
				Usage();
				exit(1);
			}
			optind++;
		}

		for (;;) {
			c = getwc(stdin);
			if (c == EOF)
				break;
			(void) putch(c);
			lastc = c;
		}
	} while (new_argc > optind);
	exit(0);
}

static void
putch(c)
	register int c;
{
	wchar_t tline[LINE_MAX];

	switch (c) {
		case '\n':
			ncol = 0;
			break;
		case '\t':
			if (bflg)
				ncol = col + chr_width(c);
			else
				ncol = (col + 8) &~ 7;
			break;
		case '\b':
			if (bflg)
				ncol = col + chr_width(c);
			else
				ncol = col ? col - 1 : 0;
			break;
		case '\r':
			if (bflg)
				ncol = col + chr_width(c);
			else
				ncol = 0;
			break;

		default:
			if (bflg)
				ncol = col + chr_width(c);
			else
				ncol = col + 1;

	}

	/*
	 * Special processing when -b is not specified
	 * for backspace, and carriage return.
	 * No newline is inseted before or after the
	 * special character: backspace, or cr.
	 * See man page for the handling of backspace
	 * and CR when there is no -b.
	 */
	if ((ncol > fold) && (bflg ||
		(!bflg && (lastc != '\b') && (c != '\b') &&
		(lastc != '\n') && (c != '\n'))))  {
		/*
		 * Need to check the last position for blank
		 */
		if (sflg && lastsp) {
			/*
			 * Save the output buffer
			 * as NULL has to be insert into the last
			 * sp position.
			 */
			(void) wscpy(tline, line);
			*lastsp = (wchar_t) NULL;
			(void) fputws(lastout, stdout);
			putwchar('\n');
			/*
			 * Restore the output buffer to stuff
			 * NULL into the last sp position
			 * for the new line.
			 */
			(void) wscpy(line, tline);
			lastout = lastsp;
			lastsp = NULL;	/* Reset the last sp */
			ncol -= spcol;	/* Reset column positions */
			col -= spcol;
		} else {
			(void) newline_init();
			putwchar('\n');
			lastout = curc;
		}
	}
	/* Output buffer is full ? */
	if ((curc + 1) >= (line + LINE_MAX)) {
		/* Reach buffer limit */
		if (col > 0) {
			*curc = (wchar_t) NULL;
			(void) fputws(lastout, stdout);
			lastsp = NULL;
		}
		curc = lastout = line;

	}

	/* Store in output buffer */
	*curc++ = (wchar_t) c;

	switch (c) {
		case '\n':
			(void)  newline_init();
			curc = lastout = line;
			break;
		case '\t':
			col = ncol;
			if (sflg && iswspace(c)) {
				lastsp = curc;
				spcol = ncol;
			}

			break;
		case '\b':
			if (bflg)
				col = ncol;
			else {
				if (col)
					col--;
			}
			break;
		case '\r':
			col = 0;
			break;
		default:
			if (sflg && iswspace(c)) {
				lastsp = curc;
				spcol = ncol;
			}

			if (bflg)
				col += chr_width(c);
			else
				col += 1;

			break;
	}
}
static
void
Usage()
{
	(void) fprintf(stderr, gettext(
	    "Usage: fold [-bs] [-w width | -width ] [file...]\n"));
}

static
void
newline_init()
{
	*curc = (wchar_t) NULL;
	(void) fputws(lastout, stdout);
	ncol = col = spcol = 0;
	lastsp = NULL;
}

static int
chr_width(c)
register int c;
{
	char chr[MB_LEN_MAX+1];
	register int	n;

	n = wctomb(chr, (wchar_t) c);

	return (n > 0 ? n : 0);
}

static int
get_foldw(toptarg, width)
char	*toptarg;
int		*width;
{
	char	*p;

	if (!toptarg)
		goto badno;

	*width = 0;
	errno = 0;
	*width = strtoul(toptarg, &p, 10);
	if (*width == -1)
		goto badno;

	if (*p)
		goto badno;

	if (!*width)
		goto badno;

	return (0);

badno:
	/* fold error message */
	(void) fprintf(stderr, gettext(
		"Bad number for fold\n"));
	Usage();
	return (-1);
}
