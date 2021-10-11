/*
 * Copyright (c) 1989,1991,1992 Sun Microsystems, Inc.
 */
#pragma ident	"@(#)dl.c	1.9	93/10/26 SMI"

/* LINTLIBRARY */

/*
 * Stub library for programmer's interface to the dynamic linker.  Used
 * to satisfy ld processing, and serves as a precedence place-holder at
 * execution-time.  These routines are never actually called.
 */
#include <dlfcn.h>
#include <link.h>

#pragma	weak	dlopen = _dlopen
/* ARGSUSED */
void *
_dlopen(const char * pathname, int mode)
{
	return ((void *)0);
}

#pragma	weak	dlsym = _dlsym
/* ARGSUSED */
void *
_dlsym(void * handle, const char * name)
{
	return ((void *)0);
}

#pragma	weak	dlclose = _dlclose
/* ARGSUSED */
int
_dlclose(void * handle)
{
	return (0);
}

#pragma	weak	dlerror = _dlerror
char *
_dlerror()
{
	return ((char *)0);
}

#pragma	weak	dladdr = _dladdr
/* ARGSUSED */
int
_dladdr(void * addr, Dl_info * dlip)
{
	return (0);
}

/* ARGSUSED */
void
_ld_concurrency(Ld_concurrency * funcs)
{
}
