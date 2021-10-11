/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

/*
 * Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)users.c	1.5	94/09/30 SMI"	/* SVr4.0 1.1	*/

/*
 * users
 */
char	*malloc();

#include <stdio.h>
#include <sys/types.h>
#include <utmp.h>

static	char	*strndup(char *p, int n);

#define	NMAX sizeof (utmp.ut_name)

struct utmp utmp;
char	**names;
char	**namp;

main(argc, argv)
char **argv;
{
	register char *tp, *s;
	register FILE *fi;
	int	nusers = 0;
	int	bufflen = BUFSIZ;

	s = UTMP_FILE;
	if (argc == 2)
		s = argv[1];
	if ((fi = fopen(s, "r")) == NULL) {
		perror(s);
		exit(1);
	}

	names = namp = (char **)realloc((void *)NULL, BUFSIZ * sizeof (char *));

	while (fread((char *)&utmp, sizeof (utmp), 1, fi) == 1) {
		if (utmp.ut_name[0] == '\0')
			continue;
		if (utmp.ut_type != USER_PROCESS)
			continue;
		if (nonuser(utmp))
			continue;
		if (nusers == bufflen) {
			bufflen *= 2;
			names = (char **)realloc(names,
						bufflen * sizeof (char *));
			namp = names + nusers;
		}
		*namp++ = strndup(utmp.ut_name, sizeof (utmp.ut_name));
		nusers++;
	}

	summary();
	exit(0);
}

static	char	*
strndup(char *p, int n)
{

	register char	*x;
	x = malloc(n + 1);
	strncpy(x, p, n);
	*(x + n) = '\0';
	return (x);

}

scmp(p, q)
char **p, **q;
{
	return (strcmp(*p, *q));
}

summary()
{
	register char **p;

	qsort(names, namp - names, sizeof (names[0]), scmp);
	for (p = names; p < namp; p++) {
		if (p != names)
			putchar(' ');
		fputs(*p, stdout);
	}
	if (namp != names)		/* at least one user */
		putchar('\n');
}
