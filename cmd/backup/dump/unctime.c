/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ident	"@(#)unctime.c 1.9 90/10/20 SMI" /* from UCB 5.1 6/5/85 */

#ident	"@(#)unctime.c 1.4 91/12/20"

#include <sys/types.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/*
 * Convert a ctime(3) format string into a system format date.
 * Return the date thus calculated.
 *
 * Return -1 if the string is not in ctime format.
 */

/*
 * Offsets into the ctime string to various parts.
 */

#define	E_MONTH		4
#define	E_DAY		8
#define	E_HOUR		11
#define	E_MINUTE	14
#define	E_SECOND	17
#define	E_YEAR		20

#ifdef __STDC__
static int lookup(char *);
static time_t emitl(struct tm *);
static long dcmp(struct tm *, struct tm *);
#else
static int lookup();
static time_t emitl();
static long dcmp();
#endif

time_t
unctime(str)
	char *str;
{
	struct tm then;
	char dbuf[30];

	if (strlen(str) != 25)
		str[25] = 0;
	(void) strcpy(dbuf, str);
	dbuf[E_MONTH+3] = 0;
	if ((then.tm_mon = lookup(&dbuf[E_MONTH])) < 0) {
		return (-1);
	}
	then.tm_mday = atoi(&dbuf[E_DAY]);
	then.tm_hour = atoi(&dbuf[E_HOUR]);
	then.tm_min = atoi(&dbuf[E_MINUTE]);
	then.tm_sec = atoi(&dbuf[E_SECOND]);
	then.tm_year = atoi(&dbuf[E_YEAR]) - 1900;
	return (emitl(&then));
}

static char months[] =
	"JanFebMarAprMayJunJulAugSepOctNovDec";

static int
lookup(str)
	char *str;
{
	register char *cp, *cp2;

	for (cp = months, cp2 = str; *cp != 0; cp += 3)
		if (strncmp(cp, cp2, 3) == 0)
			return ((cp-months) / 3);
	return (-1);
}
/*
 * Routine to convert a localtime(3) format date back into
 * a system format date.
 *
 *	Use iterative search.
 */
static time_t
emitl(dp)
	struct tm *dp;
{
	static time_t conv;
	long dt;

	while ((dt = dcmp(localtime(&conv), dp)) != 0)
		conv -= dt;
	return (conv);
}

/*
 * Compare two localtime dates, return result.
 */

#define	DIFF(a) (dp->a - dp2->a)

static long
dcmp(dp, dp2)
	register struct tm *dp, *dp2;
{

	return (((((DIFF(tm_year) * 12
		+ DIFF(tm_mon))  * 31
		+ DIFF(tm_mday)) * 24L
		+ DIFF(tm_hour)) * 60L
		+ DIFF(tm_min))  * 60L
		+ DIFF(tm_sec) + DIFF(tm_year));
}
