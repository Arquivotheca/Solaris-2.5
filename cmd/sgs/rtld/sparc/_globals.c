/*
 *	Copyright (c) 1994 by Sun Microsystems, Inc.
 *
 * Declarations of all diagnostic strings used by the runtime linker.
 */
#pragma ident	"@(#)_globals.c	1.3	95/03/03 SMI"

#include	"_rtld.h"

/*
 * Error message diagnostics (routed via eprintf()).
 */
const char
	* Errmsg_rvob =	"relocation error: value 0x%x overflows %d "
			"bits at 0x%x: referenced in %s";
