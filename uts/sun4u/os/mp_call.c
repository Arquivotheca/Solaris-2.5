/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)mp_call.c	1.5	94/06/09 SMI"

/*
 * Facilities for cross-processor subroutine calls using "mailbox" interrupts.
 *
 */

#ifdef MP				/* Around entire file */

#include <sys/types.h>
#include <sys/thread.h>

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
poke_cpu(int cpun)
{
	extern kthread_id_t panic_thread;
	extern u_int poke_cpu_inum;
	extern void xt_one();

	if (panic_thread)
		return;

	xt_one(cpun, poke_cpu_inum, 0, 0, 0, 0);
}
#endif	MP
