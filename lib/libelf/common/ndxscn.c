/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)ndxscn.c	1.4	92/07/17 SMI" 	/* SVr4.0 1.2	*/

/*LINTLIBRARY*/

#ifdef __STDC__
	#pragma weak	elf_ndxscn = _elf_ndxscn
#endif


#include "syn.h"
#include "libelf.h"
#include "decl.h"


size_t
elf_ndxscn(scn)
	register Elf_Scn	*scn;
{

	if (scn == 0)
		return SHN_UNDEF;
	return scn->s_index;
}
