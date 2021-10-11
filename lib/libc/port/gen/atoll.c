/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ident	"@(#)atoll.c	1.1	91/08/19 SMI"

/*LINTLIBRARY*/

#ifdef __STDC__
#pragma weak atoll = _atoll
#endif

#include "synonyms.h"
#include "shlib.h"
#include <ctype.h>

#define	ATOLL

#if defined	ATOI
typedef int TYPE;
#define	NAME	atoi
#elif defined ATOL
typedef long TYPE;
#define	NAME	atol
#else
typedef long long TYPE;
#define	NAME	atoll
#endif

TYPE
NAME(p)
#ifdef __STDC__
register const char *p;
#else
register  char *p;
#endif /* __STDC__ */
{
	register TYPE n;
	register int c, neg = 0;

	if (!isdigit(c = *p)) {
		while (isspace(c))
			c = *++p;
		switch (c) {
		case '-':
			neg++;
			/* FALLTHROUGH */
		case '+':
			c = *++p;
		}
		if (!isdigit(c))
			return (0);
	}
	for (n = '0' - c; isdigit(c = *++p); ) {
		n *= 10; /* two steps to avoid unnecessary overflow */
		n += '0' - c; /* accum neg to avoid surprises at MAX */
	}
	return (neg ? n : -n);
}
