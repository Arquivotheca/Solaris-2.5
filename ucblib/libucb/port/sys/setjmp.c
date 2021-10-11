/*	Copyright (c) 1988 AT&T */
/*	All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)setjmp.c	1.3	90/09/24 SMI"	/* SVr4.0 1.2	*/

#include <ucontext.h>
#include <setjmp.h>

greg_t _getsp();

#define _ABI_JBLEN	12	/* _JBLEN from base */

/*
 * The following structure MUST match the ABI size specifier _SIGJBLEN.
 * This is 19 (words). The ABI value for _JBLEN is 12 (words). A sigset_t
 * is 16 bytes and a stack_t is 12 bytes.
 */
typedef struct setjmp_struct_t {
	int		sjs_flags;	/* JBUF[ 0]	*/
	greg_t		sjs_sp;		/* JBUF[ 1]	*/
	greg_t		sjs_pc;		/* JBUF[ 2]	*/
	u_long		sjs_pad[_ABI_JBLEN-3];
	sigset_t	sjs_sigmask;
	stack_t		sjs_stack;
} setjmp_struct_t;

#define JB_SAVEMASK	0x1

int 
setjmp(env)
	jmp_buf env;
{
	register setjmp_struct_t *bp = (setjmp_struct_t *)env;
	register greg_t sp = _getsp();
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
	bp->sjs_sp    = *((greg_t *)sp+14);
	bp->sjs_pc    = *((greg_t *)sp+15) + 0x8;
	bp->sjs_stack = uc.uc_stack;

	/* save the mask */
	bp->sjs_flags |= JB_SAVEMASK;
	bp->sjs_sigmask = uc.uc_sigmask;

	return 0;
}


int 
_setjmp(env)
	jmp_buf env;
{
	register setjmp_struct_t *bp = (setjmp_struct_t *)env;
	register greg_t sp = _getsp();
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
	bp->sjs_sp    = *((greg_t *)sp+14);
	bp->sjs_pc    = *((greg_t *)sp+15) + 0x8;
	bp->sjs_stack = uc.uc_stack;

	return 0;
}


void
_longjmp(env, val)
	jmp_buf env;
	int val;
{
	siglongjmp(env, val);
}


void
longjmp(env, val)
	jmp_buf env;
	int val;
{
	siglongjmp(env, val);
}

