#ident	"@(#)mp_call.c	1.12	94/03/22 SMI"

/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

/*
 * Facilities for cross-processor subroutine calls using "mailbox" interrupts.
 *
 */

#ifdef MP				/* Around entire file */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/thread.h>
#include <sys/cpuvar.h>
#include <sys/x_call.h>
#include <sys/debug.h>
#include <sys/machsystm.h>

/*
 * Interrupt another CPU.
 * 	This is useful to make the other CPU go through a trap so that
 *	it recognizes an address space trap (AST) for preempting a thread.
 *
 *	It is possible to be preempted here and be resumed on the CPU
 *	being poked, so it isn't an error to poke the current CPU.
 *	We could check this and still get preempted after the check, so
 *	we don't bother.
 */
void
poke_cpu(cpun)
	int	cpun;
{
	extern kthread_id_t panic_thread;

	if (panic_thread)
		return;

	/*
	 * We don't need to receive an ACK from the CPU being poked,
	 * so just send out a directed level 14 interrupt.
	 */
	send_dirint(cpun, 14);
}

/*
 * Call routine on another CPU.
 */
/* VARARGS3 */
call_cpu(cpun, func, arg)
	int	cpun;
	int	(*func)();
	int	arg;
{
	/*
	 * XXX - need to deal with the race condition where
	 * the return value isn't valid after xc_sync returns
	 */
	xc_sync(arg, 0, 0, X_CALL_HIPRI, CPUSET(cpun), func);
	return (cpu[cpun]->cpu_m.xc_retval[X_CALL_HIPRI]);
}

/*
 * Call a function on remote CPUs, but call that function at a low interrupt
 * level that can do mutex operations (block) in the interrupt handler.
 * This uses the softint mechanism, and relies on softcall not using mutexes.
 *
 * For now at least, it's illegal to call this against the same CPU, since
 * it must wait for the soft-call and cannot use the spin lock for this.
 */
int
call_cpu_soft(cpun, func, arg)
	int	cpun;
	int	(*func)();
	int	arg;
{
	ASSERT(cpun != (int)CPU->cpu_id);

	/*
	 * We don't need to wait for the CPU to respond, so
	 * use xc-call instead of xc_sync.
	 */
#ifdef XXX
	xc_sync(CPUSET(cpun), softcall, func, arg, X_CALL_LOPRI);
#endif XXX
	xc_call(arg, 0, 0, X_CALL_LOPRI, CPUSET(cpun), func);
	return (cpu[cpun]->cpu_m.xc_retval[X_CALL_LOPRI]);
}

/*
 * Call a function on all active CPUs (present company included).
 */
void
call_cpus(func, arg)
	int	(*func)();
	int	arg;
{
	/*
	 * We don't need to wait for the CPU to respond, so
	 * use xc-call instead of xc_sync.
	 */
	xc_call(arg, 0, 0, X_CALL_LOPRI, CPUSET_ALL, func);
}

/*
 * Stop all other CPUs except the one passed in, using cross-call.
 *	Does the best it can, but stopping is really up to the other CPU.
 *	Restarting is done manually, if at all.
 */
void
stop_cpus(msg)
	char	*msg;		/* message string */
{

	xc_sync((int)msg, 0, 0, X_CALL_HIPRI,
	    CPUSET_ALL_BUT(CPU->cpu_id), (int (*)())mp_halt);
}

void
call_mon_enter()
{
	extern void prom_enter_mon();

	xc_sync(0, 0, 0, X_CALL_HIPRI, CPUSET_ALL,
	    (int (*)())prom_enter_mon);
}

void
call_prom_exit()
{
	extern void prom_exit_to_mon();

	xc_sync(0, 0, 0, X_CALL_HIPRI, CPUSET_ALL,
	    (int (*)())prom_exit_to_mon);
}

#endif	MP
