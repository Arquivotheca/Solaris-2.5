/*
 *	Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident	"@(#)tnf_res.c	1.20	95/03/21 SMI"

/*
 * "Resident" part of TNF -- this has to be around even when the
 * driver is not loaded.
 */

#ifndef NPROBE
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/klwp.h>
#include <sys/proc.h>
#include <sys/kmem.h>
#include <sys/msacct.h>
#include <sys/tnf_com.h>
#include <sys/tnf_writer.h>
#include <sys/tnf_probe.h>
#include <sys/tnf.h>
#include <sys/debug.h>

#include "tnf_buf.h"
#include "tnf_types.h"
#include "tnf_trace.h"

/*
 * Defines
 */

#define	TNF_PC_COUNT	8

/*
 * Declarations
 */

/*
 * This makes the state of the TNFW_B_STOPPED bit externally visible
 * in the kernel.
 */
volatile int tnf_tracing_active = 0;

/*
 * The trace buffer pointer
 */
volatile caddr_t tnf_buf;

static u_int *pcstack(u_int *);

/*
 * Stub definitions for tag data pointers
 */

/* tnf_writer module */
tnf_tag_data_t *tnf_inline_tag_data = NULL;
tnf_tag_data_t *tnf_tagged_tag_data = NULL;
tnf_tag_data_t *tnf_scalar_tag_data = NULL;
tnf_tag_data_t *tnf_char_tag_data = NULL;
tnf_tag_data_t *tnf_int8_tag_data = NULL;
tnf_tag_data_t *tnf_uint8_tag_data = NULL;
tnf_tag_data_t *tnf_int16_tag_data = NULL;
tnf_tag_data_t *tnf_uint16_tag_data = NULL;
tnf_tag_data_t *tnf_int32_tag_data = NULL;
tnf_tag_data_t *tnf_uint32_tag_data = NULL;
tnf_tag_data_t *tnf_int64_tag_data = NULL;
tnf_tag_data_t *tnf_uint64_tag_data = NULL;
tnf_tag_data_t *tnf_float32_tag_data = NULL;
tnf_tag_data_t *tnf_float64_tag_data = NULL;
tnf_tag_data_t *tnf_array_tag_data = NULL;
tnf_tag_data_t *tnf_string_tag_data = NULL;
tnf_tag_data_t *tnf_type_array_tag_data = NULL;
tnf_tag_data_t *tnf_name_array_tag_data = NULL;
tnf_tag_data_t *tnf_derived_tag_data = NULL;
tnf_tag_data_t *tnf_align_tag_data = NULL;
tnf_tag_data_t *tnf_derived_base_tag_data = NULL;
tnf_tag_data_t *tnf_element_type_tag_data = NULL;
tnf_tag_data_t *tnf_header_size_tag_data = NULL;
tnf_tag_data_t *tnf_name_tag_data = NULL;
tnf_tag_data_t *tnf_opaque_tag_data = NULL;
tnf_tag_data_t *tnf_properties_tag_data = NULL;
tnf_tag_data_t *tnf_self_size_tag_data = NULL;
tnf_tag_data_t *tnf_size_tag_data = NULL;
tnf_tag_data_t *tnf_slot_names_tag_data = NULL;
tnf_tag_data_t *tnf_slot_types_tag_data = NULL;
tnf_tag_data_t *tnf_tag_tag_data = NULL;
tnf_tag_data_t *tnf_tag_arg_tag_data = NULL;
tnf_tag_data_t *tnf_type_size_tag_data = NULL;
tnf_tag_data_t *tnf_struct_tag_data = NULL;
tnf_tag_data_t *tnf_file_header_tag_data = NULL;
tnf_tag_data_t *tnf_block_header_tag_data = NULL;
tnf_tag_data_t *tnf_type_tag_data = NULL;
tnf_tag_data_t *tnf_array_type_tag_data = NULL;
tnf_tag_data_t *tnf_derived_type_tag_data = NULL;
tnf_tag_data_t *tnf_scalar_type_tag_data = NULL;
tnf_tag_data_t *tnf_struct_type_tag_data = NULL;

/* tnf_trace module */
tnf_tag_data_t *tnf_probe_event_tag_data = NULL;
tnf_tag_data_t *tnf_time_base_tag_data = NULL;
tnf_tag_data_t *tnf_time_delta_tag_data = NULL;
tnf_tag_data_t *tnf_pid_tag_data = NULL;
tnf_tag_data_t *tnf_lwpid_tag_data = NULL;
tnf_tag_data_t *tnf_kthread_id_tag_data = NULL;
tnf_tag_data_t *tnf_cpuid_tag_data = NULL;
tnf_tag_data_t *tnf_device_tag_data = NULL;
tnf_tag_data_t *tnf_symbol_tag_data = NULL;
tnf_tag_data_t *tnf_symbols_tag_data = NULL;
tnf_tag_data_t *tnf_sysnum_tag_data = NULL;
tnf_tag_data_t *tnf_microstate_tag_data = NULL;
tnf_tag_data_t *tnf_offset_tag_data = NULL;
tnf_tag_data_t *tnf_fault_type_tag_data = NULL;
tnf_tag_data_t *tnf_seg_access_tag_data = NULL;
tnf_tag_data_t *tnf_bioflags_tag_data = NULL;
tnf_tag_data_t *tnf_diskaddr_tag_data = NULL;
tnf_tag_data_t *tnf_kernel_schedule_tag_data = NULL;

tnf_tag_data_t *tnf_probe_type_tag_data = NULL;

/* Exported properties */
tnf_tag_data_t	***tnf_user_struct_properties = NULL;

/*
 * tnf_thread_create()
 * Called from thread_create() to initialize thread's tracing state.
 * XXX Do this when tracing is first turned on
 */

void
tnf_thread_create(kthread_t *t)
{
	/* If the allocation fails, this thread doesn't trace */
	t->t_tnf_tpdp = kmem_zalloc(sizeof (tnf_ops_t), KM_NOSLEEP);

	TNF_PROBE_3(thread_create, "thread", /* CSTYLED */,
		tnf_kthread_id,	tid,		t,
		tnf_pid,	pid,		ttoproc(t)->p_pid,
		tnf_symbol,	start_pc,	t->t_startpc);
}

/*
 * tnf_thread_exit()
 * Called from thread_exit() and lwp_exit() if thread has a tpdp.
 * From this point on, we're off the allthreads list
 */

void
tnf_thread_exit(void)
{
	tnf_ops_t *ops;
	tnf_block_header_t *block;
	extern void tnfw_b_clear_lock(u_char *);

	TNF_PROBE_0(thread_exit, "thread", /* CSTYLED */);

	ops = (tnf_ops_t *)curthread->t_tnf_tpdp;
	if (ops) {
		/* Mark ops as busy */
		ops->busy = 1;
		if (tnf_buf != NULL) {
			/* Release any A-locks held */
			block = ops->wcb.tnfw_w_pos.tnfw_w_block;
			ops->wcb.tnfw_w_pos.tnfw_w_block = NULL;
			if (block != NULL)
				tnfw_b_clear_lock(&block->A_lock);
			block = ops->wcb.tnfw_w_tag_pos.tnfw_w_block;
			ops->wcb.tnfw_w_tag_pos.tnfw_w_block = NULL;
			if (block != NULL)
				tnfw_b_clear_lock(&block->A_lock);
		}
		curthread->t_tnf_tpdp = NULL;
		/* Free ops */
		kmem_free(ops, sizeof (*ops));
	}
}

/*
 * Called from thread_free() if thread has tpdp.
 */

void
tnf_thread_free(kthread_t *t)
{
}

/*
 * tnf_thread_queue()
 * Probe wrapper called when tracing is enabled and a thread is
 * placed on some dispatch queue.
 */

void
tnf_thread_queue(kthread_t *t, cpu_t *cp, pri_t tpri)
{
	TNF_PROBE_4(thread_queue, "dispatcher", /* CSTYLED */,
		tnf_kthread_id,		tid,		t,
		tnf_cpuid,		cpuid,		cp->cpu_id,
		tnf_long,		priority,	tpri,
		tnf_ulong,		queue_length,
			/* cp->cpu_disp.disp_q[tpri].dq_sruncnt */
			cp->cpu_disp.disp_nrunnable);

	TNF_PROBE_2(thread_state, "thread", /* CSTYLED */,
		tnf_kthread_id,		tid,		t,
		tnf_microstate,		state,		LMS_WAIT_CPU);
}

/*
 * pcstack(): fill in, NULL-terminate and return pc stack.
 */

static u_int *
pcstack(u_int *pcs)
{
	u_int n;

	n = getpcstack(pcs, TNF_PC_COUNT);
	pcs[n] = 0;
	return (pcs);
}

/*
 * tnf_thread_switch()
 * Probe wrapper called when tracing enabled and curthread is about to
 * switch to the next thread.
 * XXX Simple sleepstate and runstate calculations
 */

#define	SLPSTATE(t)					\
	(((t)->t_state == TS_STOPPED) ? LMS_STOPPED :	\
	    ((t)->t_wchan0 ? LMS_USER_LOCK : LMS_SLEEP))

#define	RUNSTATE(next, lwp)				\
	((((lwp = ttolwp(next)) != NULL) &&		\
		lwp->lwp_state == LWP_USER) ?		\
		LMS_USER : LMS_SYSTEM)

void
tnf_thread_switch(kthread_t *next)
{
	kthread_t	*t;
	klwp_t		*lwp;
	tnf_ops_t	*tpdp, *ntpdp;
	int		borrow;
	u_int		pcs[TNF_PC_COUNT + 1];

	t = curthread;

	/*
	 * If we're a zombie, borrow idle thread's tpdp.  This lets
	 * the driver decide whether the buffer is busy by examining
	 * allthreads (idle threads are always on the list)
	 */
	if ((borrow = (t->t_state == TS_ZOMB))) {
		/* ASSERT(t != CPU->cpu_idle_thread); */
		ASSERT(t->t_tnf_tpdp == NULL);
		t->t_tnf_tpdp = CPU->cpu_idle_thread->t_tnf_tpdp;
		goto do_next;
	}

	/*
	 * If we're blocked, test the blockage probe
	 */
	if (t->t_state == TS_SLEEP && t->t_wchan)
		TNF_PROBE_2(thread_block, "synch", /* CSTYLED */,
		    tnf_opaque,		reason,		t->t_wchan,
		    tnf_symbols,	stack,		(void **)pcstack(pcs));

	/*
	 * Record outgoing thread's state
	 * Kernel thread ID is implicit in schedule record
	 */
	TNF_PROBE_1(thread_state, "thread", /* CSTYLED */,
	    tnf_microstate,	state,		SLPSTATE(t));

do_next:

	/*
	 * Record incoming thread's state
	 */
	TNF_PROBE_2(thread_state, "thread", /* CSTYLED */,
	    tnf_kthread_id,	tid,		next,
	    tnf_microstate,	state,		RUNSTATE(next, lwp));

	/*
	 * If we borrowed tpdp above, forget it now
	 */
	if (borrow)
		t->t_tnf_tpdp = NULL;

}

#endif	/* NPROBE */
