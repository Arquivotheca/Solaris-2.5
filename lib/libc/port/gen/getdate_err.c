/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident	"@(#)getdate_err.c	1.1	93/09/20 SMI"	/* SVr4.0 1.8	*/

#ifdef __STDC__
	#pragma weak getdate_err = _getdate_err
#endif

#include <time.h>
#include <thread.h>

int _getdate_err = 0;

#ifdef _REENTRANT
int *
_getdate_err_addr(void)
{
	static thread_key_t gde_key = 0;

	if (_thr_main())
		return (&_getdate_err);
	return ((int*)_tsdalloc(&gde_key, sizeof (int)));
}
#endif /* _REENTRANT */
