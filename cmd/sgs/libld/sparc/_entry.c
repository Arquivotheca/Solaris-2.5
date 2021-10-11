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
#pragma ident	"@(#)_entry.c	1.7	94/02/23 SMI"

/* LINTLIBRARY */

#include	<stdio.h>
#include	"_libld.h"

/*
 * The dynamic entrance descriptor list is maintained here to allow for machine
 * differences.
 */
const Ent_desc	dyn_ent_desc[] = {
	{{NULL, NULL}, NULL, SHT_PROGBITS,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC,
		(Sg_desc *)LD_TEXT, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_DYNSYM,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC,
		(Sg_desc *)LD_TEXT, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_STRTAB,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC,
		(Sg_desc *)LD_TEXT, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_HASH, 0, 0,
		(Sg_desc *)LD_TEXT, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_RELA,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC,
		(Sg_desc *)LD_TEXT, 0, FALSE},
	{{NULL, NULL}, ".SUNW_version", NULL,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC,
		(Sg_desc *)LD_TEXT, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_DYNAMIC, 0, 0,
		(Sg_desc *)LD_DATA, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_PROGBITS,
		SHF_ALLOC + SHF_WRITE + SHF_EXECINSTR,
		SHF_ALLOC + SHF_WRITE + SHF_EXECINSTR,
		(Sg_desc *)LD_DATA, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_PROGBITS,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC + SHF_WRITE,
		(Sg_desc *)LD_DATA, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_NOBITS,
		SHF_ALLOC + SHF_WRITE, SHF_ALLOC + SHF_WRITE,
		(Sg_desc *)LD_DATA, 0, FALSE},
	{{NULL, NULL}, NULL, SHT_NOTE, 0, 0,
		(Sg_desc *)LD_NOTE, 0, FALSE},
	{{NULL, NULL}, NULL, 0, 0, 0,
		(Sg_desc *)LD_EXTRA, 0, FALSE}
};
const int	dyn_ent_size = sizeof (dyn_ent_desc);
