/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)comm.c	1.11	94/08/25 SMI"	/* SVr4.0 1.3	*/
/*
**	process common lines of two files
*/

#include 	<locale.h>
#include	<stdio.h>
#include	<stdlib.h>

static int compare(char *, char *);
static int rd(FILE *, char *);
static FILE *openfil(char *);
static void copy(FILE *, char *, int);
static void usage(void);
static void wr(char *, int);

#define	LB	2050	/* P1003.2 minimum (2048) + 2 */

static int	one;
static int	two;
static int	three;

static char	*ldr[3];

static FILE	*ib1;
static FILE	*ib2;

void
main(argc, argv)
char **argv;
{
	int	l = 1;
	int	c;		/* used for getopt() */
	char	lb1[LB], lb2[LB];

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);
	ldr[0] = "";
	ldr[1] = "\t";
	ldr[2] = "\t\t";
	while ((c = getopt(argc, argv, "123")) != EOF)
		switch (c) {
		case '1':
			if (!one) {
				one = 1;
				ldr[1][0] = ldr[2][l--] = '\0';
			}
			break;
		case '2':
			if (!two) {
				two = 1;
				ldr[2][l--] = '\0';
			}
			break;
		case '3':
			three = 1;
			break;

		default:
			usage();
		}

	argc -= optind;
	argv  = &argv[optind];

	if (argc != 2)
		usage();
	ib1 = openfil(argv[0]);
	ib2 = openfil(argv[1]);
	if (rd(ib1, lb1) < 0) {
		if (rd(ib2, lb2) < 0)
			exit(0);
		copy(ib2, lb2, 2);
	}
	if (rd(ib2, lb2) < 0)
		copy(ib1, lb1, 1);
	/*LINTED*/
	while (1) {
		switch (compare(lb1, lb2)) {
			case 0:
				wr(lb1, 3);
				if (rd(ib1, lb1) < 0) {
					if (rd(ib2, lb2) < 0)
						exit(0);
					copy(ib2, lb2, 2);
				}
				if (rd(ib2, lb2) < 0)
					copy(ib1, lb1, 1);
				continue;

			case 1:
				wr(lb1, 1);
				if (rd(ib1, lb1) < 0)
					copy(ib2, lb2, 2);
				continue;

			case 2:
				wr(lb2, 2);
				if (rd(ib2, lb2) < 0)
					copy(ib1, lb1, 1);
				continue;
		}
	}
}

static int
rd(file, buf)
FILE *file;
char *buf;
{

	register int i, j;
	i = j = 0;
	while ((j = getc(file)) != EOF) {
		*buf = j;
		if (*buf == '\n' || i > LB-2) {
			*buf = '\0';
			return (0);
		}
		i++;
		buf++;
	}
	return (-1);
}

static void
wr(str, n)
char *str;
int n;
{
	switch (n) {
		case 1:
			if (one)
				return;
			break;

		case 2:
			if (two)
				return;
			break;

		case 3:
			if (three)
				return;
	}
	(void) printf("%s%s\n", ldr[n-1], str);
}

static void
copy(ibuf, lbuf, n)
FILE *ibuf;
char *lbuf;
int n;
{
	do {
		wr(lbuf, n);
	} while (rd(ibuf, lbuf) >= 0);

	exit(0);
}

static int
compare(a, b)
char *a, *b;
{
	register char *ra, *rb;

	ra = --a;
	rb = --b;
	while (*++ra == *++rb)
		if (*ra == '\0')
			return (0);
	if (*ra < *rb)
		return (1);
	return (2);
}

static FILE *
openfil(s)
char *s;
{
	FILE *b;
	if (s[0] == '-' && s[1] == 0)
		b = stdin;
	else if ((b = fopen(s, "r")) == NULL) {
		(void) fprintf(stderr, "comm: ");
		perror(s);
		exit(2);
	}
	return (b);
}

static void
usage()
{
	(void) fprintf(stderr, gettext("usage: comm [-123] file1 file2\n"));
	exit(2);
}
