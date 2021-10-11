/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)globals.c	1.10	95/06/20 SMI"

/*
 * Global variables
 */
#include	<stdio.h>
#include	"_ld.h"

/*
 * Debugging mask (refer to include/debug.h).
 */
int	dbg_mask;

/*
 * List of support libraries specified (-S option).
 */
List	lib_support;

/*
 * Global error messages.
 */
const char
	* Errmsg_file =	"file %s: cannot %s file; errno=%d";

/*
 * Special symbol definitions.
 */
const char
	* Fini_usym =	"_fini",
	* Init_usym =	"_init",
	* Libv_usym =	"_lib_version";

/*
 * Paths and directories for library searches.  These are used to set up
 * linked lists of directories which are maintained in the ofl structure.
 */
char *		Plibpath;	/* User specified -YP or defaults to LIBPATH */
char *		Llibdir;	/* User specified -YL */
char *		Ulibdir;	/* User specified -YU */
Listnode *	insert_lib;	/* insertion point for -L libraries */
