/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)next.c	1.4	92/07/17 SMI" 	/* SVr4.0 1.5	*/

/*LINTLIBRARY*/

#ifdef __STDC__
	#pragma weak	elf_next = _elf_next
#endif


#include "syn.h"
#include "libelf.h"
#include "decl.h"


Elf_Cmd
elf_next(elf)
	Elf	*elf;
{
	Elf	*parent;

	if (elf == 0
	|| (parent = elf->ed_parent) == 0
	|| elf->ed_siboff >= parent->ed_fsz)
		return ELF_C_NULL;
	parent->ed_nextoff = elf->ed_siboff;
	return ELF_C_READ;
}
