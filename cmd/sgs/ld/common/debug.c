/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)debug.c	1.5	93/01/04 SMI"

#include	<stdio.h>
#include	<stdarg.h>
#include	<dlfcn.h>
#include	"debug.h"
#include	"_ld.h"

/*
 * Its possible that dbg_setup may be called more than once.
 */
static int	dbg_init = 0;

#ifndef	DEBUG

int
/* ARGSUSED0 */
dbg_setup(const char * options)
{
	if (!dbg_init) {
		eprintf(ERR_WARNING, "debugging is not enabled; "
			"recompile %sld with -DDEBUG", SGS);
		dbg_init++;
	}
	return (0);
}

#else

static const char
	* Dbg_library =	DBG_LIBRARY,
	* Dbg_setup =	DBG_SETUP,
	* Errmsg_updl =	"debugging disabled; unable to "
			"process debug library:\n\t%s";
int
dbg_setup(const char * options)
{
	void *	handle;
	int (* fptr)();

	if (dbg_init)
		return (0);

	/*
	 * Open the linker debugging library
	 */
	if ((handle = dlopen(Dbg_library, (RTLD_LAZY | RTLD_GLOBAL))) == NULL) {
		eprintf(ERR_WARNING, Errmsg_updl, dlerror());
		dbg_init++;
		return (0);
	}

	/*
	 * Obtain the Dbg_setup() symbol
	 */
	if ((fptr = (int (*)())dlsym(handle, Dbg_setup)) == NULL) {
		eprintf(ERR_WARNING, Errmsg_updl, dlerror());
		dbg_init++;
		return (0);
	}

	/*
	 * Call the debugging setup routine to initialize the mask and
	 * debug function array.
	 */
	return ((* fptr)(options));
}

/* VARARGS1 */
void
dbg_print(const char * format, ...)
{
	va_list		args;

	(void) fputs("debug: ", stderr);
	va_start(args, format);
	(void) vfprintf(stderr, format, args);
	(void) fprintf(stderr, "\n");
	va_end(args);
}

#endif
