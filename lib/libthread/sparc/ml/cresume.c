/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)cresume.c	1.12	95/06/30	SMI"


#include "libthread.h"
#include <sys/psw.h>
/*
 * resume the execution of the specified thread on the current
 * LWP.
 *
 * signals are disabled by the caller and are enabled on return
 * from resume via _resume_ret() which happens in threadjmp() or
 * swtchsig().
 */
_resume(t, tempstk, dontsave)
	uthread_t *t;
	caddr_t tempstk;
	int dontsave;
{
	register psw_t psr = 0;
	register u_int reguse = 0;
	uthread_t *ct = curthread;
	uthread_t *tt = t;
	uthread_t *zombie = ct;

	if (curthread->t_state != TS_ZOMB) {
		if (!dontsave) {
			_save_thread(ct);
			/*
			 * if the current thread has fpu enabled, save the FSR.
			 */
			psr = _getpsr();
			if (psr & PSR_EF) {
				ct->t_fpu_en = 1;
				_savefsr(ct);
			} else {
				ct->t_fpu_en = 0;
			}
		}
		/*
		 * switch to a temporary stack, flush register windows
		 * to stack, and release the current thread's cpu lock,
		 * curthread->t_lock.
		 */
		tt = (uthread_t *)_switch_stack(tempstk, ct, tt);
		ct = curthread;
		zombie = NULL;
	}
	/*
	 * must first acquire the per-thread mutex of the resumed thread
	 * before going any further.
	 */
	_lwp_mutex_lock(&(tt->t_lock));
	/*
	 * if the resumed thread has fpu enabled, restore the FSR.
	 * else, mark the fp registers are not in use (see bug 1176340)
	 */
	if (tt->t_fpu_en)
		_restorefsr(tt);
	else {
		psr = _getpsr();
		if ((psr & PSR_EF))
			_setpsr((psr & (~PSR_EF)));
	}
	tt->t_fpu_en = 0;
	_threadjmp(tt, zombie);
	/* never returns */
}
