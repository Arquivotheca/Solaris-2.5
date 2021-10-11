/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)foreign.c	1.5	92/07/14 SMI"	/* SVr4.0 6.	*/
						/* SVr4.0 1.5	*/
/*LINTLIBRARY*/

#include	<stdio.h>
#if	COFF_FILE_CONVERSION
#include	"filehdr.h"
#endif
#include	"syn.h"
#include	"elf.h"
#include	"libelf.h"
#include	"decl.h"
#include	"foreign.h"


/* Foreign file conversion
 *	Allow other file formats to be converted to elf.
 *	Change this table to support new or drop old conversions.
 *
 *	A foreign function actually returns Elf_Kind or -1 on error.
 */


#if COFF_FILE_CONVERSION
int	_elf_coff	_((Elf *));
#endif


int	(*const _elf_foreign[]) _((Elf *)) =
{
#if COFF_FILE_CONVERSION
	_elf_coff,
#endif
	0,
};

#if COFF_FILE_CONVERSION
_elf_coff(elfp)
	Elf *elfp;
{
	struct filehdr *base;

	base = (struct filehdr *) elfp->e_ident;
	if (ISCOFF(base->f_magic))
		return (ELF_K_COFF);
	else
		return (ELF_K_NONE);
}
#endif
