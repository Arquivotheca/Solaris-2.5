/*
 *	Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident	"@(#)trace_init.c	1.17	95/03/21 SMI"

/*
 * Includes
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/debug.h>
#include <sys/tnf.h>
#include <sys/thread.h>

#include "tnf_buf.h"
#include "tnf_types.h"
#include "tnf_trace.h"

#ifndef NPROBE

/*
 * Globals
 * XXX All we need is the state
 */

static TNFW_B_CONTROL tnfw_b_control = {
	TNFW_B_NOBUFFER | TNFW_B_STOPPED,	/* tnf_state */
	NULL,					/* tnf_buffer */
	NULL,					/* init_callback */
	NULL,					/* fork_callback */
	0					/* tnf_pid */
};

TNFW_B_CONTROL *_tnfw_b_control = &tnfw_b_control;

size_t tnf_trace_file_size = TNF_TRACE_FILE_DEFAULT;

/*
 * tnf_trace_on
 */
void
tnf_trace_on(void)
{
	TNFW_B_UNSET_STOPPED(tnfw_b_control.tnf_state);
	tnf_tracing_active = 1;
	/* Enable system call tracing for all processes */
	set_all_proc_sys();
}

/*
 * tnf_trace_off
 */
void
tnf_trace_off(void)
{
	TNFW_B_SET_STOPPED(tnfw_b_control.tnf_state);
	tnf_tracing_active = 0;
	/* System call tracing is automatically disabled */
}

/*
 * tnf_trace_init
 * 	Not reentrant: only called from tnf_allocbuf(), which is
 *	single-threaded.
 */
void
tnf_trace_init(void)
{
	int stopped;
	tnf_ops_t *ops;

	ASSERT(tnf_buf != NULL);
	ASSERT(!tnf_tracing_active);

	stopped = tnfw_b_control.tnf_state & TNFW_B_STOPPED;

	if (tnfw_b_init_buffer(tnf_buf, tnf_trace_file_size / TNF_BLOCK_SIZE,
	    TNF_BLOCK_SIZE, B_TRUE) != TNFW_B_OK) {
		/*
		 * Tracing is broken
		 */
		tnfw_b_control.tnf_state = TNFW_B_BROKEN | stopped;
		return;
	}

	/*
	 * Mark allocator running (not stopped). Luckily,
	 * tnf_trace_alloc() first checks tnf_tracing_active, so no
	 * trace data will be written.
	 */
	tnfw_b_control.tnf_buffer = tnf_buf;
	tnfw_b_control.tnf_state = TNFW_B_RUNNING;

	/*
	 * 1195835: Write out some tags now.  The stopped bit needs
	 * to be clear while we do this.
	 */
	if ((ops = (tnf_ops_t *)curthread->t_tnf_tpdp) != NULL) {
		tnf_tag_data_t *tag;

		ops->busy = 1;

		tag = TAG_DATA(tnf_struct_type);
		(void) tag->tag_desc(ops, tag);
		tag = TAG_DATA(tnf_probe_type);
		(void) tag->tag_desc(ops, tag);
		tag = TAG_DATA(tnf_kernel_schedule);
		(void) tag->tag_desc(ops, tag);

		(void) tnfw_b_xcommit(&ops->wcb);
		ops->busy = 0;
	}

	/* Restore stopped bit */
	tnfw_b_control.tnf_state |= stopped;
}

#endif /* NPROBE */
