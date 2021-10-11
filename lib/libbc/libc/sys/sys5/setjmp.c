/*	Copyright (c) 1988 AT&T */
/*	All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)setjmp.c	1.2	92/07/14 SMI"	/* SVr4.0 1.2	*/


#include <sys/setjmp.h>
#include "../common/ucontext.h"

int _getsp();

int 
setjmp(env)
	jmp_buf env;
{
	register o_setjmp_struct_t *bp = (o_setjmp_struct_t *)env;
	register int sp = _getsp();
	ucontext_t uc;

	/*
	 * Get the current machine context.
	 */
	uc.uc_flags = UC_STACK | UC_SIGMASK;
	__getcontext(&uc);

	/*
	 * Note that the pc and former sp (fp) from the stack are valid
	 * because the call to __getcontext must flush the user windows
	 * to the stack.
	 */
	bp->sjs_flags = 0;
	bp->sjs_sp    = *((int *)sp+14);
	bp->sjs_pc    = *((int *)sp+15) + 0x8;
	bp->sjs_stack = uc.uc_stack;

	return 0;
}


void
longjmp(env, val)
	jmp_buf env;
	int val;
{
	o_setjmp_struct_t *bp = (o_setjmp_struct_t *)env;
	setjmp_struct_t sjmp, *sp;

	sp = &sjmp;
	sp->sjs_flags = bp->sjs_flags;
	sp->sjs_sp = bp->sjs_sp;
	sp->sjs_pc = bp->sjs_pc;
	sp->sjs_stack = bp->sjs_stack;

	_siglongjmp(sjmp, val);
}

