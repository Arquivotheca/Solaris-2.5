/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)getbase.c	1.4	92/07/17 SMI" 	/* SVr4.0 1.6	*/

/*LINTLIBRARY*/

#ifdef __STDC__
	#pragma weak	elf_getbase = _elf_getbase
#endif


#include "syn.h"
#include "libelf.h"
#include "decl.h"


off_t
elf_getbase(elf)
	Elf	*elf;
{
	if (elf == 0)
		return -1;
	return elf->ed_baseoff;
}
