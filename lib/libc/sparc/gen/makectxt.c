/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)makectxt.c	1.9	95/06/30 SMI"	/* SVr4.0 1.4	*/

#ifdef __STDC__
	#pragma weak makecontext = _makecontext
#endif
#include "synonyms.h"
#include <stdarg.h>
#include <ucontext.h>
#include <sys/stack.h>
#include <sys/frame.h>

void
#ifdef __STDC__
makecontext(ucontext_t *ucp, void (*func)(), int argc, ...)
#else
makecontext(ucp, func, argc, va_alist)
ucontext_t *ucp;
void (*func)();
int argc;
va_dcl
#endif
{
	register greg_t *reg;
	int *tsp;
	char *sp;
	int argno;
	va_list ap;

	reg = ucp->uc_mcontext.gregs;
	reg[REG_PC] = (greg_t)func;
	reg[REG_nPC] = reg[REG_PC] + 0x4;

	sp = ucp->uc_stack.ss_sp;
	*(int *) sp = (int) ucp->uc_link;		/* save uc_link */

	/*
	 * reserve enough space for argc, reg save area, uc_link,
	 * and "hidden" arg;  rounding to stack alignment
	 */
	sp -= SA((argc + 16 + 1 + 1) * sizeof (int));

#ifdef __STDC__
	va_start(ap, ...);
#else
	va_start(ap, va_alist);
#endif
	/*
	 * Copy all args to the alt stack,
	 * also copy the first 6 args to .gregs
	 */
	argno = 0;
	tsp = ((struct frame *) sp)->fr_argd;

	while (argno < argc) {
		if (argno < 6)
			*tsp++ =  reg[REG_O0 + argno] = va_arg(ap, int);
		else
			*tsp++ =  va_arg(ap, int);
		argno++;
	}

	ucp->uc_stack.ss_sp = sp;
	reg[REG_O6] = (int) sp;			/* sp (when done)	*/
	reg[REG_O7] = (int) setcontext - 8;	/* return pc		*/
}
