/*	Copyright (c) 1993 SMI	*/
/*	 All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#pragma	ident	"@(#)genassym.c	1.31	95/08/30	SMI"

#include "libthread.h"
#include <signal.h> /* for SIG_SETMASK */
#include <sys/psw.h>

main()
{
	register struct thread *t = (struct thread *)0;
	register struct tls *tls = (struct tls *)0;
	register mutex_t *mp = (mutex_t *)0;

	printf("#define\tTHREAD_SIZE\t0x%x\n", sizeof (struct thread));
	printf("#define\tT_PC\t0x%x\n", &t->t_pc);
	printf("#define\tT_SP\t0x%x\n", &t->t_sp);
	printf("#define\tT_FSR\t0x%x\n", &t->t_fsr);
	printf("#define\tT_FPU_EN\t0x%x\n", &t->t_fpu_en);
	printf("#define\tT_LOCK\t0x%x\n", &t->t_lock);
	printf("#define\tT_STOP\t0x%x\n", &t->t_stop);
	printf("#define\tT_PSIG\t0x%x\n", &t->t_psig);
	printf("#define\tT_HOLD\t0x%x\n", &t->t_hold);
	printf("#define\tT_SSIG\t0x%x\n", &t->t_ssig);
	printf("#define\tT_NOSIG\t0x%x\n", &t->t_nosig);
	printf("#define\tT_PENDING\t0x%x\n", &t->t_pending);
	printf("#define\tT_LINK\t0x%x\n", &t->t_link);
	printf("#define\tT_PREV\t0x%x\n", &t->t_prev);
	printf("#define\tT_NEXT\t0x%x\n", &t->t_next);
	printf("#define\tT_TLS\t0x%x\n", &t->t_tls);
	printf("#define\tT_STATE\t0x%x\n", &t->t_state);
	printf("#define\tT_FLAG\t0x%x\n", &t->t_flag);
	printf("#define\tT_STKSIZE\t0x%x\n", &t->t_stksize);
	printf("#define\tT_TID\t0x%x\n", &t->t_tid);
	printf("#define\tT_LWPID\t0x%x\n", &t->t_lwpid);
	printf("#define\tT_IDLE\t0x%x\n", &t->t_idle);
	printf("#define\tT_FORW\t0x%x\n", &t->t_forw);
	printf("#define\tT_BACKW\t0x%x\n", &t->t_backw);
	printf("#define\tMUTEX_LOCK\t0x%x\n", &mp->mutex_lockw);
        printf("#define\tMUTEX_WAITERS\t0x%x\n", &mp->mutex_waiters);
	printf("#define\tMUTEX_WANTED\t0x%x\n", 0);
	printf("#define\tT_USROPTS\t0x%x\n", &t->t_usropts);
	/* PROBE_SUPPORT begin */
	printf("#define\tT_TPDP\t0x%x\n", &t->t_tpdp);
	/* PROBE_SUPPORT end */

	/* cancellation support begin */
	printf("#define\tT_CANSTATE\t0x%x\n", &t->t_can_state);
	printf("#define\tT_CANTYPE\t0x%x\n", &t->t_can_type);
	printf("#define\tT_CANPENDING\t0x%x\n", &t->t_can_pending);
	printf("#define\tT_CANCELABLE\t0x%x\n", &t->t_cancelable);

	printf("#define\tTC_PENDING\t0x%x\n", TC_PENDING);
	printf("#define\tTC_DISABLE\t0x%x\n", TC_DISABLE);
	printf("#define\tTC_ENABLE\t0x%x\n", TC_ENABLE);
	printf("#define\tTC_ASYNCHRONOUS\t0x%x\n", TC_ASYNCHRONOUS);
	printf("#define\tTC_DEFERRED\t0x%x\n", TC_DEFERRED);
	printf("#define\tPTHREAD_CANCELED\t0x%x\n", PTHREAD_CANCELED);
	/* cancellation support end */

        printf("#define\tTS_ZOMB\t0x%x\n", TS_ZOMB);
        printf("#define\tT_IDLETHREAD\t0x%x\n", T_IDLETHREAD);
        printf("#define\tTS_ONPROC\t0x%x\n", TS_ONPROC);
        printf("#define\tSIG_SETMASK\t0x%x\n", SIG_SETMASK);
        printf("#define\tPSR_EF\t0x%x\n", PSR_EF);
        printf("#define\tPAGESIZE\t0x%x\n", PAGESIZE);

#ifdef TRACE_INTERNAL
        printf("#define\tTR_T_SWTCH\t0x%x\n", TR_T_SWTCH);
#endif
	exit(0);
}
