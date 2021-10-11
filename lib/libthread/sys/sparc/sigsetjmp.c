/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)sigsetjmp.c	1.1	95/08/29	SMI"

#ifdef __STDC__
#pragma weak sigsetjmp	= _sigsetjmp
#pragma weak siglongjmp	= _siglongjmp
#endif /* __STDC__ */

#include "libthread.h"
#include <ucontext.h>
#include <setjmp.h>

/*
 * The following structure MUST match the ABI size specifier _SIGJBLEN.
 * This is 19 (words). The ABI value for _JBLEN is 12 (words). A sigset_t
 * is 16 bytes and a stack_t is 12 bytes.
 */
typedef struct sigjmp_struct {
        int             sjs_flags;      /* JBUF[ 0]     */
        greg_t          sjs_sp;         /* JBUF[ 1]     */
        greg_t          sjs_pc;         /* JBUF[ 2]     */
        u_long          sjs_pad[_JBLEN-3];
        sigset_t        sjs_sigmask;
        stack_t         sjs_stack;
} sigjmp_struct_t;
 
#define JB_SAVEMASK     0x1
/*
 * _sigsetjmp() sets up the jmp buffer so that on a subsequent _siglongjmp()
 * to this buffer, control first goes to __thr_jmpret() below. The stack
 * has been carefully crafted so that on return from __thr_jmpret(), the
 * thread returns to the caller of _sigsetjmp(), at the place right after
 * the call. __thr_jmpret() is needed for correct switching of signal masks
 * in an MT process.
 */
static int
__thr_jmpret(int val, sigjmp_buf env)
{
	register sigjmp_struct_t *bp = (sigjmp_struct_t *)env;

	if (bp->sjs_flags & JB_SAVEMASK)
		thr_sigsetmask(SIG_SETMASK, &(bp->sjs_sigmask), NULL);
	return (val);
}


int
_sigsetjmp(sigjmp_buf env, int savemask)
{
        register sigjmp_struct_t *bp = (sigjmp_struct_t *)env;
        register greg_t sp = _getsp();
        ucontext_t uc;

        /*
         * Get the current machine context. For threads, do not really need the
	 * UC_SIGMASK, but get it anyway, to minimize difference between MT
	 * and single-threaded versions of this routine.
         */
        uc.uc_flags = UC_STACK | UC_SIGMASK;
        __getcontext(&uc);
        /*
         * Note that the pc and former sp (fp) from the stack are valid
         * because the call to __getcontext must flush the user windows
         * to the stack.
         */
        bp->sjs_flags	= 0;
        bp->sjs_sp	= *((greg_t *)sp+14);
        bp->sjs_pc    	= *((greg_t *)sp+15) + 0x8;
        bp->sjs_stack	= uc.uc_stack;
        if (savemask) {
                bp->sjs_flags |= JB_SAVEMASK;
		/*
		 * Save current thread's signal mask, not the LWP's mask
		 * for this thread.
		 */
                bp->sjs_sigmask = curthread->t_hold;
        }
        return (0);
}

void
_siglongjmp(sigjmp_buf env, int val)
{
        ucontext_t uc;
        register greg_t *reg = uc.uc_mcontext.gregs;
        register sigjmp_struct_t *bp = (sigjmp_struct_t *)env;
 
        /*
         * Get the current context to use as a starting point to construct
         * the sigsetjmp context. It might perhaps be more precise to
         * constuct an entire context from scratch, but this method is
         * closer to old (undocumented) semantics.
         */
        uc.uc_flags = UC_ALL;
        __getcontext(&uc);
        if (bp->sjs_flags & JB_SAVEMASK)
                /*
                 * If the jmp buf has a saved signal mask, the current mask
                 * cannot be changed until the longjmp to the new context
                 * occurs. Until then, to make the signal mask change atomic
                 * with respect to the switch, mask all signals on the current
                 * thread, which flushes all signals for this thread. Now,
                 * this thread should not receive any signals until the mask
                 * switch occurs in __thr_jmpret().
                 */
		thr_sigsetmask(SIG_SETMASK, &_totalmasked, NULL);
        /*
         * Use the information in the sigjmp_buf to modify the current
         * context to execute as though we called __thr_jmpret() from
	 * the caller of _sigsetjmp().
         */
        uc.uc_stack = bp->sjs_stack;
        reg[REG_PC] = (greg_t)&__thr_jmpret;
        reg[REG_nPC] = reg[REG_PC] + 0x4;
        reg[REG_SP] = bp->sjs_sp;

	/*
	 *  make %o7 on entry to __thr_jmpret(), point to the return pc as
	 * if the caller of _sigsetjmp() called __thr_jmpret(). So that on
	 * return from __thr_jmpret(), control goes to the caller of
	 * _sigsetjmp().
	 */
        reg[REG_O7] = bp->sjs_pc - 0x8;	

        reg[REG_O1] = (greg_t)(env);	/* second arg to __thr_jmpret() */
        if (val)
		reg[REG_O0] = (greg_t)val;
        else
                reg[REG_O0] = (greg_t)1;	/* 1st arg to __thr_jmpret() */
        setcontext(&uc);
}
