/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)globals.c	1.21	94/12/13 SMI"

/* LINTLIBRARY */

/*
 * Global variables
 */
#include	<stdio.h>
#include	"_libld.h"

/*
 * Global error messages.
 */
const char
	* Errmsg_ebgn =	"file %s: elf_begin",
	* Errmsg_egdt =	"file %s: elf_getdata",
	* Errmsg_egeh =	"file %s: elf_getehdr",
	* Errmsg_egsc =	"file %s: elf_getscn",
	* Errmsg_egsh =	"file %s: elf_getshdr",
	* Errmsg_endt =	"file %s: elf_newdata",
	* Errmsg_rnas = "file %s: relocation against external symbol `%s'\n"
			"\tfrom a non-allocatable section `%s'\n"
			"\tcannot be processed at runtime: relocation ignored",
	* Errmsg_urty = "file %s: unknown relocation type %d "
			"against section %s",
	* Errmsg_ares = "file %s: relocation [%s] attempted against empty "
			"section %s (symbol `%s')",
	* Errmsg_ufte =	"file %s: unknown type, unable to process "
			"using elf(3E) libraries";

/*
 * Special symbol definitions.
 */
const char
	* Fini_sym =	"_fini",
	* Init_sym =	"_init",
	* Dyn_sym =	"DYNAMIC",
	* Dyn_usym =	"_DYNAMIC",
	* Edata_sym =	"edata",
	* Edata_usym =	"_edata",
	* End_sym =	"end",
	* End_usym =	"_end",
	* Etext_sym =	"etext",
	* Etext_usym =	"_etext",
	* Got_sym =	"GLOBAL_OFFSET_TABLE_",
	* Got_usym =	"_GLOBAL_OFFSET_TABLE_",
	* Plt_sym =	"PROCEDURE_LINKAGE_TABLE_",
	* Plt_usym =	"_PROCEDURE_LINKAGE_TABLE_";
