/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)swapctxt.c	1.9	92/08/12 SMI"	/* SVr4.0 1.3	*/

#ifdef __STDC__
	#pragma weak swapcontext = _swapcontext
#endif
#include "synonyms.h"
#include <ucontext.h>
#include <stdlib.h>

greg_t _getsp();

int
swapcontext(oucp, nucp)
ucontext_t *oucp, *nucp;
{
	register greg_t *reg;
	register greg_t sp;

	if (__getcontext(oucp))
		return (-1);

	/*
	 * Note that %o1 and %g1 are modified by the system call
	 * routine. ABI calling conventions specify that the caller
	 * can not depend upon %o0 thru %o5 nor g1, so no effort is
	 * made to maintain these registers. %o0 is forced to reflect
	 * an affermative return code.
	 */
	reg = oucp->uc_mcontext.gregs;
	sp = _getsp();
	reg[REG_PC] = *((greg_t *)sp+15) + 0x8;
	reg[REG_nPC] = reg[REG_PC] + 0x4;
	reg[REG_O0] = 0;
	reg[REG_SP] = *((greg_t *)sp+14);
	reg[REG_O7] = *((greg_t *)sp+15);

	return (setcontext(nucp));
}
