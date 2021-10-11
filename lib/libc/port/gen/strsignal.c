/*	Copyright (c) 1991 Sun Microsystems, Inc. */
/*	  All Rights Reserved  	*/
/*LINTLIBRARY*/

#pragma weak strsignal = _strsignal

#include "synonyms.h"
#include "_libc_gettext.h"
#include <string.h>
#include <stddef.h>

extern const char	**_sys_siglistp;
extern const int	_sys_siglistn;

char *
_strsignal(signum)
int signum;
{
	if (signum < _sys_siglistn && signum >= 0)
		return (_libc_gettext((char *)_sys_siglistp[signum]));
	else
		return (NULL);
}
