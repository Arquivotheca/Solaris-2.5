/*	Copyright (c) 1992 SMI	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)tsdalloc.c	1.1"

#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include <errno.h>


int *
_tsdalloc(thread_key_t *key, int size)
{
	extern void *free();
	extern int *malloc();
	int *loc = 0;

	if (_thr_getspecific(*key, &loc) != 0) {
		if (_thr_keycreate(key, free) != 0) {
			return (0);
		}
	}
	if (!loc) {
		if (_thr_setspecific(*key, (void **)(loc = malloc(size)))
				!= 0) {
			if (loc)
				(void) free(loc);
			return (0);
		}
	}
	return (loc);
}
