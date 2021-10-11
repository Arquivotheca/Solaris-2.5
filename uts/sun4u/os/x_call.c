
/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)x_call.c	1.35	95/08/10 SMI"

#include <sys/cpuvar.h>
#include <sys/intreg.h>
#include <sys/x_call.h>
#include <sys/cmn_err.h>
#include <sys/membar.h>
#include <sys/disp.h>
#include <sys/debug.h>
#include <sys/privregs.h>
#include <sys/xc_impl.h>

/*
 * xc_init()
 *	initialize x-call related locks
 */
void
xc_init(void)
{
#ifdef DEBUG
	int pix;
#endif DEBUG
	extern int add_softintr();
	extern kmutex_t cpu_idle_lock;

	mutex_init(&xc_sys_mutex, "xc sys lock", MUTEX_SPIN, (void *)XCALL_PIL);

#ifdef DEBUG
	for (pix = 0; pix < NCPU; pix++) {
		if (cpunodes[pix].nodeid) {
			XC_STAT_INIT(pix);
		};
	}
#endif DEBUG

	xc_serv_inum = add_softintr(XCALL_PIL, xc_serv, 0, 0);
	xc_loop_inum = add_softintr(XCALL_PIL, xc_loop, 0, 0);

	mutex_init(&cpu_idle_lock, "cpu idle/resume lock",
		MUTEX_SPIN, (void *)XCALL_PIL);
}

/*
 * The following routines basically provide callers with two kinds of
 * inter-processor interrupt services:
 *	1. cross calls (x-calls) - requests are handled at target cpu's TL=0
 *	2. cross traps (c-traps) - requests are handled at target cpu's TL>0
 *
 * Although these routines protect the services from migrating to other cpus
 * "after" they are called, it is the caller's choice or responsibility to
 * prevent the cpu migration "before" calling them.
 *
 * X-call routines:
 *
 *	xc_one()  - send a request to one processor
 *	xc_some() - send a request to some processors
 *	xc_all()  - send a request to all processors
 *
 *	Their common parameters:
 *		func - a TL=0 handler address
 *		arg1 and arg2  - optional
 *
 *	The services provided by x-call routines allow callers
 *	to send a request to target cpus to execute a TL=0
 *	handler.
 *	The interface of the registers of the TL=0 handler:
 *		%o0: arg1
 *		%o1: arg2
 *
 * X-trap routines:
 *
 *	xt_one()  - send a request to one processor
 *	xt_some() - send a request to some processors
 *	xt_all()  - send a request to all processors
 *
 *	Their common parameters:
 *		func - a TL>0 handler address or an interrupt number
 *		arg1, arg2, arg3, and arg4 -
 *		       optional when "func" is an address;
 *		       0        when "func" is an interrupt number
 *
 *	If the request of "func" is a kernel address, then
 *	the target cpu will execute the request of "func" with
 *	args at "TL>0" level.
 *	The interface of the registers of the TL>0 handler:
 *		%g1: arg1
 *		%g2: arg2
 *		%g3: arg3
 *		%g4: arg4
 *
 *
 *	If the request of "func" is not a kernel address, then it has
 *	to be an assigned interrupt number through add_softintr().
 *	An interrupt number is an index to the interrupt vector table,
 *	which entry contains an interrupt handler address with its
 *	corresponding interrupt level and argument.
 *	The target cpu will arrange the request to be serviced according
 *	to its pre-registered information.
 *	args are assumed to be zeros in this case.
 *
 * In addition, callers are allowed to capture and release cpus by
 * calling the routines: xc_attention() and xc_dismissed().
 */

/*
 * xt_one()
 *	send a "x-trap" to a cpu
 */
void
xt_one(int cix, u_int func, u_int arg1, u_int arg2, u_int arg3, u_int arg4)
{
	int lcx;
	int opl;

	/*
	 * send to nobody; just return
	 */
	if (!CPU_IN_SET(cpu_ready_set, cix))
		return;

	XC_SPL_ENTER(lcx, opl);			/* lcx set by the macro */

	if (cix == lcx) {
		/*
		 * same cpu - use software fast trap
		 */
		send_self_xcall(CPU, arg1, arg2, arg3, arg4, func);
		XC_STAT_INC(x_dstat[lcx][XT_ONE_SELF]);
		XC_TRACE(XT_ONE_SELF, 0, x_dstat[lcx][XT_ONE_SELF]);
	} else {		/* other cpu - send a mondo to the target cpu */
		/*
		 * other cpu - send a mondo to the target cpu
		 */
		setup_mondo(func, arg1, arg2, arg3, arg4);
		send_mondo(cix);
		XC_STAT_INC(x_dstat[lcx][XT_ONE_OTHER]);
		XC_TRACE(XT_ONE_OTHER, 0, x_dstat[lcx][XT_ONE_OTHER]);
	}
	XC_SPL_EXIT(lcx, opl);
}

/*
 * xt_some()
 *	send a "x-trap" to some cpus
 */
void
xt_some(cpuset_t cpuset, u_int func, u_int arg1, u_int arg2,
    u_int arg3, u_int arg4)
{
	int lcx;
	int opl;
	cpuset_t xc_cpuset;

	/*
	 * send to nobody; just return
	 */
	if (cpuset == 0)
		return;

	XC_SPL_ENTER(lcx, opl);		/* lcx set by the macro */

	/*
	 * only send to the CPU_READY ones
	 */
	xc_cpuset = cpu_ready_set & cpuset;

	/*
	 * don't send mondo to self
	 */
	if (CPU_IN_SET(xc_cpuset, lcx)) {
		/*
		 * same cpu - use software fast trap
		 */
		send_self_xcall(CPU, arg1, arg2, arg3, arg4, func);
		XC_STAT_INC(x_dstat[lcx][XT_SOME_SELF]);
		XC_TRACE(XT_SOME_SELF, xc_cpuset,
		    x_dstat[lcx][XC_SOME_SELF]);
		CPUSET_DEL(xc_cpuset, lcx);
		if (xc_cpuset == 0) {
			XC_SPL_EXIT(lcx, opl);
			return;
		}
	}
	setup_mondo(func, arg1, arg2, arg3, arg4);
	SEND_MONDO_ONLY(xc_cpuset);
	XC_STAT_INC(x_dstat[lcx][XT_SOME_OTHER]);
	XC_TRACE(XT_SOME_OTHER, xc_cpuset, x_dstat[lcx][XC_SOME_OTHER]);

	XC_SPL_EXIT(lcx, opl);
}

/*
 * xt_all()
 *	send a "x-trap" to all cpus
 */
void
xt_all(u_int func, u_int arg1, u_int arg2, u_int arg3, u_int arg4)
{
	int lcx;
	int opl;
	cpuset_t xc_cpuset;

	XC_SPL_ENTER(lcx, opl);		/* lcx set by the macro */

	/*
	 * same cpu - use software fast trap
	 */
	send_self_xcall(CPU, arg1, arg2, arg3, arg4, func);

	/*
	 * don't send mondo to self
	 */
	xc_cpuset = cpu_ready_set;
	CPUSET_DEL(xc_cpuset, lcx);

	if (xc_cpuset == 0) {
		XC_STAT_INC(x_dstat[lcx][XT_ALL_SELF]);
		XC_TRACE(XT_ALL_SELF, 0, x_dstat[lcx][XT_ALL_SELF]);
		XC_SPL_EXIT(lcx, opl);
		return;
	}

	setup_mondo(func, arg1, arg2, arg3, arg4);
	SEND_MONDO_ONLY(xc_cpuset);

	XC_STAT_INC(x_dstat[lcx][XT_ALL_OTHER]);
	XC_TRACE(XT_ALL_OTHER, xc_cpuset, x_dstat[lcx][XT_ALL_OTHER]);
	XC_SPL_EXIT(lcx, opl);
}

/*
 * xc_one()
 *	send a "x-call" to a cpu
 */
void
xc_one(int cix, u_int (*func)(), u_int arg1, u_int arg2)
{
	int lcx;
	int opl;
#ifdef DEBUG
	int loop_cnt = 0;
#endif DEBUG

	/*
	 * send to nobody; just return
	 */
	if (!CPU_IN_SET(cpu_ready_set, cix))
		return;

	ASSERT((u_int)func > KERNELBASE);
	ASSERT(((u_int)func % PC_ALIGN) == 0);

	XC_SPL_ENTER(lcx, opl);		/* lcx set by the macro */
	if (cix == lcx) {	/* same cpu just do it */
		(*func)(arg1, arg2);
		XC_STAT_INC(x_dstat[lcx][XC_ONE_SELF]);
		XC_TRACE(XC_ONE_SELF, 0, x_dstat[lcx][XC_ONE_SELF]);
		XC_SPL_EXIT(lcx, opl);
		return;
	}

	if (xc_holder == lcx) {		/* got the xc_sys_mutex already */
		ASSERT(MUTEX_HELD(&xc_sys_mutex));
		ASSERT(CPU_IN_SET(xc_mbox[lcx].xc_cpuset, lcx));
		ASSERT(CPU_IN_SET(xc_mbox[cix].xc_cpuset, cix));
		ASSERT(xc_mbox[cix].xc_state == XC_WAIT);
		/*
		 * target processor's xc_loop should be waiting
		 * for the work to do; just set up the xc_mbox
		 */
		XC_SETUP(cix, func, arg1, arg2);
		membar_stld();
		while (xc_mbox[cix].xc_state != XC_WAIT) {
#ifdef DEBUG
			loop_cnt++;
			if (loop_cnt > XC_TIMEOUT) {
				panic("xc_one failed, xc_state != XC_WAIT");
			}
			DELAY(1);
#endif DEBUG
			continue;
		}
		XC_STAT_INC(x_dstat[lcx][XC_ONE_OTHER_H]);
		XC_TRACE(XC_ONE_OTHER_H, 0, x_dstat[lcx][XC_ONE_OTHER_H]);
		XC_SPL_EXIT(lcx, opl);
		return;
	}

	/*
	 * Avoid dead lock if someone has sent us a xc_loop request while
	 * we are trying to grab xc_sys_mutex.
	 */
	XC_SPL_EXIT(lcx, opl);

	/*
	 * At this point, since we don't own xc_sys_mutex,
	 * our pil shouldn't run at or above the XCALL_PIL.
	 */
	ASSERT(getpil() < XCALL_PIL);

	/*
	 * Since xc_holder is not owned by us, it could be that
	 * no one owns it, or we are not informed to enter into
	 * xc_loop(). In either case, we need to grab the
	 * xc_sys_mutex before we write to the xc_mbox, and
	 * we shouldn't release it until the request is finished.
	 */

	mutex_enter(&xc_sys_mutex);
	xc_spl_enter[lcx] = 1;

	/*
	 * Since we own xc_sys_mutex now, we are safe to
	 * write to the xc_mobx.
	 */
	ASSERT(xc_mbox[cix].xc_state == XC_IDLE);
	XC_SETUP(cix, func, arg1, arg2);
	setup_mondo((u_int)xc_serv_inum, 0, 0, 0, 0);
	send_mondo(cix);		/* does membar_sync */


	/* xc_serv does membar_stld */
	while (xc_mbox[cix].xc_state != XC_IDLE) {
#ifdef DEBUG
		loop_cnt++;
		if (loop_cnt > XC_TIMEOUT) {
			panic("xc_one failed, xc_state != XC_WAIT");
		}
		DELAY(1);
#endif DEBUG
		continue;
	}
	xc_spl_enter[lcx] = 0;
	XC_STAT_INC(x_dstat[lcx][XC_ONE_OTHER]);
	XC_TRACE(XC_ONE_OTHER, 0, x_dstat[lcx][XC_ONE_OTHER]);
	mutex_exit(&xc_sys_mutex);

}
/*
 * xc_some()
 *	send a "x-call" to some cpus; sending to self is excluded
 */
void
xc_some(cpuset_t cpuset, u_int (*func)(), u_int arg1, u_int arg2)
{
	int lcx;
	int opl;
	cpuset_t xc_cpuset;

	/*
	 * send to nobody; just return
	 */
	if (cpuset == 0)
		return;

	ASSERT((u_int)func > KERNELBASE);
	ASSERT(((u_int)func % PC_ALIGN) == 0);

	XC_SPL_ENTER(lcx, opl);			/* lcx set by the macro */

	/*
	 * only send to the CPU_READY ones
	 */
	xc_cpuset = cpu_ready_set & cpuset;

	if (CPU_IN_SET(xc_cpuset, lcx)) {
		/*
		 * same cpu just do it
		 */
		(*func)(arg1, arg2);
		CPUSET_DEL(xc_cpuset, lcx);
		if (xc_cpuset == 0) {
			XC_STAT_INC(x_dstat[lcx][XC_SOME_SELF]);
			XC_TRACE(XC_SOME_SELF, cpuset,
			    x_dstat[lcx][XC_SOME_SELF]);
			XC_SPL_EXIT(lcx, opl);
			return;
		}
	}

	if (xc_holder == lcx) {		/* got the xc_sys_mutex already */
		ASSERT(MUTEX_HELD(&xc_sys_mutex));
		ASSERT((xc_mbox[lcx].xc_cpuset & cpuset) == cpuset);
		SEND_MBOX_ONLY(xc_cpuset, func, arg1, arg2, lcx, XC_WAIT);
		WAIT_MBOX_DONE(xc_cpuset, lcx, XC_WAIT);
		XC_STAT_INC(x_dstat[lcx][XC_SOME_OTHER_H]);
		XC_TRACE(XC_SOME_OTHER_H, cpuset,
		    x_dstat[lcx][XC_SOME_OTHER_H]);
		XC_SPL_EXIT(lcx, opl);
		return;
	}

	/*
	 * Avoid dead lock if someone has sent us a xc_loop request while
	 * we are trying to grab xc_sys_mutex.
	 */
	XC_SPL_EXIT(lcx, opl);

	/*
	 * At this point, since we don't own xc_sys_mutex,
	 * our pil shouldn't run at or above the XCALL_PIL.
	 */
	ASSERT(getpil() < XCALL_PIL);

	/*
	 * grab xc_sys_mutex before writing to the xc_mbox
	 */
	mutex_enter(&xc_sys_mutex);
	xc_spl_enter[lcx] = 1;

	setup_mondo(xc_serv_inum, 0, 0, 0, 0);
	SEND_MBOX_MONDO(xc_cpuset, func, arg1, arg2, XC_IDLE);
	WAIT_MBOX_DONE(xc_cpuset, lcx, XC_IDLE);

	xc_spl_enter[lcx] = 0;
	XC_STAT_INC(x_dstat[lcx][XC_SOME_OTHER]);
	XC_TRACE(XC_SOME_OTHER, cpuset, x_dstat[lcx][XC_SOME_OTHER]);
	mutex_exit(&xc_sys_mutex);
}
/*
 * xc_all()
 *	send a "x-call" to all cpus
 */
void
xc_all(u_int (*func)(), u_int arg1, u_int arg2)
{
	int lcx;
	int opl;
	cpuset_t xc_cpuset;

	ASSERT((u_int)func > KERNELBASE);
	ASSERT(((u_int)func % PC_ALIGN) == 0);

	XC_SPL_ENTER(lcx, opl);			/* lcx set by the macro */

	/*
	 * same cpu just do it
	 */
	(*func)(arg1, arg2);
	xc_cpuset = cpu_ready_set;
	CPUSET_DEL(xc_cpuset, lcx);

	if (xc_cpuset == 0) {
		XC_STAT_INC(x_dstat[lcx][XC_ALL_SELF]);
		XC_TRACE(XC_ALL_SELF, 0, x_dstat[lcx][XC_ALL_SELF]);
		XC_SPL_EXIT(lcx, opl);
		return;
	}

	if (xc_holder == lcx) {		/* got the xc_sys_mutex already */
		ASSERT(MUTEX_HELD(&xc_sys_mutex));
		ASSERT((xc_mbox[lcx].xc_cpuset & xc_cpuset) == xc_cpuset);
		SEND_MBOX_ONLY(xc_cpuset, func, arg1, arg2, lcx, XC_WAIT);
		WAIT_MBOX_DONE(xc_cpuset, lcx, XC_WAIT);
		XC_STAT_INC(x_dstat[lcx][XC_ALL_OTHER_H]);
		XC_TRACE(XC_ALL_OTHER_H, xc_cpuset,
		    x_dstat[lcx][XC_ALL_OTHER_H]);
		XC_SPL_EXIT(lcx, opl);
		return;
	}

	/*
	 * Avoid dead lock if someone has sent us a xc_loop request while
	 * we are trying to grab xc_sys_mutex.
	 */
	XC_SPL_EXIT(lcx, opl);

	/*
	 * At this point, since we don't own xc_sys_mutex,
	 * our pil shouldn't run at or above the XCALL_PIL.
	 */
	ASSERT(getpil() < XCALL_PIL);

	/*
	 * grab xc_sys_mutex before writing to the xc_mbox
	 */
	mutex_enter(&xc_sys_mutex);
	xc_spl_enter[lcx] = 1;

	setup_mondo(xc_serv_inum, 0, 0, 0, 0);
	SEND_MBOX_MONDO(xc_cpuset, func, arg1, arg2, XC_IDLE);
	WAIT_MBOX_DONE(xc_cpuset, lcx, XC_IDLE);

	xc_spl_enter[lcx] = 0;
	XC_STAT_INC(x_dstat[lcx][XC_ALL_OTHER]);
	XC_TRACE(XC_ALL_OTHER, xc_cpuset, x_dstat[lcx][XC_ALL_OTHER]);
	mutex_exit(&xc_sys_mutex);
}
/*
 * xc_attention()
 *	paired with xc_dismissed()
 *	xt_attention() holds the xc_sys_mutex and xc_dismissed() releases it
 *	called when an initiator wants to capture some/all cpus for a critical
 *	session
 */
void
xc_attention(cpuset_t cpuset)
{
	int pix, lcx;
	cpuset_t xc_cpuset, tmpset;
	cpuset_t recv_cpuset = 0;
#ifdef DEBUG
	int loop_cnt = 0;
#endif DEBUG

	/*
	 * don't migrate the cpu until xc_dismissed() is finished
	 */
	ASSERT(getpil() < XCALL_PIL);
	mutex_enter(&xc_sys_mutex);
	lcx = (int)(CPU->cpu_id);
	ASSERT(x_dstat[lcx][XC_ATTENTION] ==
	    x_dstat[lcx][XC_DISMISSED]);
	ASSERT(xc_holder == -1);
	xc_mbox[lcx].xc_cpuset = cpuset;
	xc_holder = lcx; /* no membar; only current cpu needs the right lcx */

	XC_STAT_INC(x_dstat[lcx][XC_ATTENTION]);
	XC_TRACE(XC_ATTENTION, cpuset, x_dstat[lcx][XC_ATTENTION]);

	/*
	 * only send to the CPU_READY ones
	 */
	xc_cpuset = cpu_ready_set & cpuset;

	/*
	 * don't send mondo to self
	 */
	CPUSET_DEL(xc_cpuset, lcx);

	if (xc_cpuset == 0)
		return;

	xc_spl_enter[lcx] = 1;
	/*
	 * inform the target processors to enter into xc_loop()
	 */
	tmpset = xc_cpuset;
	setup_mondo(xc_loop_inum, 0, 0, 0, 0);
	for (pix = 0; pix < NCPU; pix++) {
		if (CPU_IN_SET(tmpset, pix)) {
			ASSERT(xc_mbox[pix].xc_state == XC_IDLE);
			xc_mbox[pix].xc_state = XC_ENTER;
			send_mondo(pix);	/* does membar_sync */
			CPUSET_DEL(tmpset, pix);
			CPU_STAT_ADDQ(CPU, cpu_sysinfo.xcalls, 1);
			if (tmpset == 0) {
				break;
			}
		}
	}
	xc_spl_enter[lcx] = 0;

	/*
	 * make sure target processors have entered into xc_loop()
	 */
	while (recv_cpuset != xc_cpuset) {
		tmpset = xc_cpuset;
		for (pix = 0; pix < NCPU; pix++) {
			if (CPU_IN_SET(tmpset, pix)) {
				/*
				 * membar_stld() is done in xc_loop
				 */
				if (xc_mbox[pix].xc_state == XC_WAIT) {
					CPUSET_ADD(recv_cpuset, pix);
				}
				CPUSET_DEL(tmpset, pix);
				if (tmpset == 0) {
					break;
				}
			}
		}
#ifdef DEBUG
		loop_cnt++;
		if (loop_cnt > XC_TIMEOUT) {
			panic("xc_attention failed");
		}
		DELAY(1);
#endif DEBUG
	}


	/*
	 * keep locking xc_sys_mutex until xc_dismissed() is finished
	 */
}

/*
 * xc_dismissed()
 *	paired with xc_attention()
 *	called after the critical session is finished
 */
void
xc_dismissed(cpuset_t cpuset)
{
	int pix;
	int lcx = (int)(CPU->cpu_id);
	cpuset_t xc_cpuset, tmpset;
	cpuset_t recv_cpuset = 0;
#ifdef DEBUG
	int loop_cnt = 0;
#endif DEBUG

	ASSERT(lcx == xc_holder);
	ASSERT(xc_mbox[lcx].xc_cpuset == cpuset);
	ASSERT(getpil() >= XCALL_PIL);
	xc_mbox[lcx].xc_cpuset = 0;
	membar_stld();

	XC_STAT_INC(x_dstat[lcx][XC_DISMISSED]);
	ASSERT(x_dstat[lcx][XC_DISMISSED] ==
	    x_dstat[lcx][XC_ATTENTION]);
	XC_TRACE(XC_DISMISSED, cpuset, x_dstat[lcx][XC_DISMISSED]);

	/*
	 * only send to the CPU_READY ones
	 */
	xc_cpuset = cpu_ready_set & cpuset;

	/*
	 * exclude itself
	 */
	CPUSET_DEL(xc_cpuset, lcx);

	if (xc_cpuset == 0) {
		xc_holder = -1;
		mutex_exit(&xc_sys_mutex);
		return;
	}

	/*
	 * inform other processors to get out of xc_loop()
	 */
	tmpset = xc_cpuset;
	for (pix = 0; pix < NCPU; pix++) {
		if (CPU_IN_SET(tmpset, pix)) {
			xc_mbox[pix].xc_state = XC_EXIT;
			membar_stld();
			CPUSET_DEL(tmpset, pix);
			if (tmpset == 0) {
				break;
			}
		}
	}

	/*
	 * make sure target processors have exited from xc_loop()
	 */
	while (recv_cpuset != xc_cpuset) {
		tmpset = xc_cpuset;
		for (pix = 0; pix < NCPU; pix++) {
			if (CPU_IN_SET(tmpset, pix)) {
				/*
				 * membar_stld() is done in xc_loop
				 */
				if (xc_mbox[pix].xc_state == XC_IDLE) {
					CPUSET_ADD(recv_cpuset, pix);
				}
				CPUSET_DEL(tmpset, pix);
				if (tmpset == 0) {
					break;
				}
			}
		}
#if DEBUG
		loop_cnt++;
		if (loop_cnt > XC_TIMEOUT) {
			panic("xc_dismissed failed");
		}
		DELAY(1);
#endif DEBUG
	}
	xc_holder = -1;
	mutex_exit(&xc_sys_mutex);
}

/*
 * xc_serv()
 *	"x-call" handler at TL=0; serves only one x-call request
 *	runs at XCALL_PIL level
 */
int
xc_serv()
{
	int lcx = (int)(CPU->cpu_id);
	struct xc_mbox *xmp;
	u_int (*func)();
	u_int arg1, arg2;

	ASSERT(getpil() == XCALL_PIL);
	flush_windows();
	xmp = &xc_mbox[lcx];
	ASSERT(lcx != xc_holder);
	ASSERT(xmp->xc_state == XC_DOIT);
	func = xmp->xc_func;
	if (func != NULL) {
		arg1 = xmp->xc_arg1;
		arg2 = xmp->xc_arg2;
		(*func)(arg1, arg2);
	}
	XC_STAT_INC(x_rstat[lcx][XC_SERV]);
	XC_TRACE(XC_SERV, 0, x_rstat[lcx][XC_SERV]);
	xmp->xc_state = XC_IDLE;
	membar_stld();
	return (1);
}

/*
 * xc_loop()
 *	"x-call" handler at TL=0; capture the cpu for a critial session,
 *	or serve multiple x-call requests
 *	runs at XCALL_PIL level
 */
/*ARGSUSED0*/
int
xc_loop()
{
	int lcx = (int)(CPU->cpu_id);
	struct xc_mbox *xmp;
	u_int (*func)();
	u_int arg1, arg2;
#ifdef DEBUG
	int loop_cnt = 0;
#endif DEBUG

	ASSERT(getpil() == XCALL_PIL);
	flush_windows();
	/*
	 * Some one must have owned the xc_sys_mutex;
	 * no further interrupt (at XCALL_PIL or below) can
	 * be taken by this processor until xc_loop exits.
	 *
	 * The owner of xc_sys_mutex (or xc_holder) can expect
	 * its xc/xt requests are handled as follows:
	 * 	xc requests use xc_mbox's handshaking for their services
	 * 	xt requests at TL>0 will be handled immediately
	 * 	xt requests at TL=0:
	 *		if their handlers'pils are <= XCALL_PIL, then
	 *			they will be handled after xc_loop exits
	 *			(so, they probably should not be used)
	 *		else they will be handled immediately
	 *
	 * For those who are not informed to enter xc_loop, if they
	 * send xc/xt requests to this processor at this moment,
	 * the requests will be handled as follows:
	 *	xc requests will be handled after they grab xc_sys_mutex
	 *	xt requests at TL>0 will be handled immediately
	 * 	xt requests at TL=0:
	 *		if their handlers'pils are <= XCALL_PIL, then
	 *			they will be handled after xc_loop exits
	 *		else they will be handled immediately
	 */
	xmp = &xc_mbox[lcx];
	ASSERT(lcx != xc_holder);
	ASSERT(xmp->xc_state == XC_ENTER);
	xmp->xc_state = XC_WAIT;
	membar_stld();
	XC_STAT_INC(x_rstat[lcx][XC_LOOP]);
	XC_TRACE(XC_LOOP, 0, x_rstat[lcx][XC_LOOP]);
	while (xmp->xc_state != XC_EXIT) {
		if (xmp->xc_state == XC_DOIT) {
			func = xmp->xc_func;
			if (func != NULL) {
				arg1 = xmp->xc_arg1;
				arg2 = xmp->xc_arg2;
				(*func)(arg1, arg2);
			} else {
				/* EMPTY */
			}
			xmp->xc_state = XC_WAIT;
			membar_stld();
		}
#ifdef DEBUG
		loop_cnt++;
		if (loop_cnt > XC_TIMEOUT) {
			panic("xc_loop failed");
		}
		DELAY(1);
#endif DEBUG
	}
	ASSERT(xmp->xc_state == XC_EXIT);
	ASSERT(xc_holder != -1);
	xmp->xc_state = XC_IDLE;
	membar_stld();
	return (1);
}
