/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)lsearch.c	1.8	93/03/02 SMI"	/* SVr4.0 1.15	*/

/*LINTLIBRARY*/
/*
 * Linear search algorithm, generalized from Knuth (6.1) Algorithm Q.
 *
 * This version no longer has anything to do with Knuth's Algorithm Q,
 * which first copies the new element into the table, then looks for it.
 * The assumption there was that the cost of checking for the end of the
 * table before each comparison outweighed the cost of the comparison, which
 * isn't true when an arbitrary comparison function must be called and when the
 * copy itself takes a significant number of cycles.
 * Actually, it has now reverted to Algorithm S, which is "simpler."
 */

#ifdef __STDC__
	#pragma weak lsearch = _lsearch
#endif
#include "synonyms.h"
#include <stddef.h>
#include <string.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#ifdef _REENTRANT
mutex_t __lsearch_lock = DEFAULTMUTEX;
#endif _REENTRANT

VOID * 
lsearch(ky, bs, nelp, width, compar)
const VOID *ky;			/* Key to be located */
register const VOID *bs;	/* Beginning of table */
size_t *nelp;			/* Pointer to current table size */
register size_t width;		/* Width of an element (bytes) */
int (*compar)();		/* Comparison function */
{
	typedef char *POINTER;
        register POINTER key = (char *)ky;
        register POINTER base = (char *)bs;
	register POINTER next = base + *nelp * width;	/* End of table */
	VOID *res;

	_mutex_lock(&__lsearch_lock);
	for ( ; base < next; base += width)
		if ((*compar)(key, base) == 0) {
			_mutex_unlock(&__lsearch_lock);
			return (base);	/* Key found */
		}
	++*nelp;			/* Not found, add to table */
	res = memcpy(base, key, (int)width);	/* base now == next */
	_mutex_unlock(&__lsearch_lock);
	return (res);
}
