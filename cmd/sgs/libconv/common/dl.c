/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)dl.c	1.1	92/09/03 SMI"

/* LINTLIBRARY */

/*
 * String conversion routine for dlopen() attributes.
 */
#include	<string.h>
#include	<stdio.h>
#include	"_libconv.h"

const char *
conv_dlmode_str(int mode)
{
	static	char	string[28] = { '\0' };

	if (mode == 0)
		return ("0");
	else {
		(void) strcpy(string, "[");
		if (mode & RTLD_GLOBAL) {
			(void) strcat(string, " RTLD_GLOBAL ");
			mode &= ~RTLD_GLOBAL;
		}
		if (mode == RTLD_LAZY)
			(void) strcat(string, " RTLD_LAZY ");
		else
			(void) strcat(string, " RTLD_NOW ");
		(void) strcat(string, "]");

		return ((const char *) string);
	}
}
