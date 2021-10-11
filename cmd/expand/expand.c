/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)expand.c	1.7	94/12/14 SMI"

/*
 *
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		All rights reserved.
 */

#include <stdio.h>
#include <euc.h>
#include <getwidth.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

/*
 * expand - expand tabs to equivalent spaces
 */
static int		nstops = 0;
static int		tabstops[100];
static eucwidth_t	wp;
static int		isClocale;

static void getstops(const char *);
static void usage(void);

int
main(argc, argv)
int argc;
char *argv[];
{
	register int	c, column;
	register int	n;
	register int	i, j;
	char		*locale;
	int		flag, tflag = 0;

	(void) setlocale(LC_ALL, "");
	locale = setlocale(LC_CTYPE, NULL);
	isClocale = (strcmp(locale, "C") == 0);
#if !defined(TEXT_DOMAIN)		/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	getwidth(&wp);

	/*
	 * First, look for and extract any "-<number>" args then pass
	 * them to getstops().
	 */
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--") == 0)
			break;

		if (*argv[i] != '-')
			continue;
		if (!isdigit(*(argv[i]+1)))
			continue;

		getstops(argv[i]+1);
		tflag++;

		/* Pull this arg from list */
		for (j = i; j < (argc-1); j++)
			argv[j] = argv[j+1];
		argc--;
	}

	while ((flag = getopt(argc, argv, "t:")) != EOF) {
		switch (flag) {
		case 't':
			if (tflag)
				usage();

			getstops(optarg);
			break;

		default:
			usage();
			break;
		}
	}

	argc -= optind;
	argv = &argv[optind];

	do {
		if (argc > 0) {
			if (freopen(argv[0], "r", stdin) == NULL) {
				perror(argv[0]);
				exit(1);
				/* NOTREACHED */
			}
			argc--;
			argv++;
		}

		column = 0;
		for (;;) {
			c = getc(stdin);
			if (c == -1)
				break;
			switch (c) {
			case '\t':
				if (nstops == 0) {
					do {
						(void) putchar(' ');
						column++;
					} while (column & 07);
					continue;
				}
				if (nstops == 1) {
					do {
						(void) putchar(' ');
						column++;
					} while (
					    ((column - 1) % tabstops[0]) !=
						(tabstops[0] - 1));
					continue;
				}
				for (n = 0; n < nstops; n++)
					if (tabstops[n] > column)
						break;
				if (n == nstops) {
					(void) putchar(' ');
					column++;
					continue;
				}
				while (column < tabstops[n]) {
					(void) putchar(' ');
					column++;
				}
				continue;

			case '\b':
				if (column)
					column--;
				(void) putchar('\b');
				continue;

			default:
				(void) putchar(c);
				if (isClocale) {
					column++;
				} else if (ISSET2(c)) {
					column += wp._scrw2;
					for (i = 0; i < wp._eucw2; i++)
						(void) putchar(getc(stdin));
				} else if (ISSET3(c)) {
					column += wp._scrw3;
					for (i = 0; i < wp._eucw3; i++)
						(void) putchar(getc(stdin));
				} else if (c >= 0200) {
					column += wp._scrw1;
					for (i = 0; i < wp._eucw1-1; i++)
						(void) putchar(getc(stdin));
				} else
					column++;
				continue;

			case '\n':
				(void) putchar(c);
				column = 0;
				continue;
			}
		}
	} while (argc > 0);

	return (0);
	/* NOTREACHED */
}

static void
getstops(const char *cp)
{
	register int i;

	for (;;) {
		i = 0;
		while (*cp >= '0' && *cp <= '9')
			i = i * 10 + *cp++ - '0';

		if (i <= 0 || i > INT_MAX) {
			(void) fprintf(stderr, gettext(
				"expand: invalid tablist\n"));
			usage();
		}

		if (nstops > 0 && i <= tabstops[nstops-1]) {
			(void) fprintf(stderr, gettext(
				"expand: tablist must be increasing\n"));
			usage();
		}

		tabstops[nstops++] = i;
		if (*cp == 0)
			break;

		if (*cp != ',' && *cp != ' ') {
			(void) fprintf(stderr, gettext(
				"expand: invalid tablist\n"));
			usage();
		}
		cp++;
	}
}

static void
usage(void)
{
	(void) fprintf(stderr, gettext(
		"usage: expand [-t tablist] [file ...]\n"
		"       expand [-tabstop] [-tab1,tab2,...,tabn] [file ...]\n"));
	exit(2);
	/* NOTREACHED */
}
