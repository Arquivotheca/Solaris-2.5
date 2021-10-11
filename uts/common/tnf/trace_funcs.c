/*
 *	Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident	"@(#)trace_funcs.c 1.16     95/09/22 SMI"

/*
 * Includes
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/tnf.h>

#include "tnf_buf.h"
#include "tnf_types.h"
#include "tnf_trace.h"

/*
 * Defines
 */

#define	ENCODED_TAG(tag, tagarg) 		\
	(((tag) | ((tagarg) & 0x0000fffc)) | TNF_REF32_T_PAIR)

/*
 * CAUTION: halfword_accessible assumes that the pointer is to a reclaimable
 *		block - i.e. negative offsets have a 0 in high bit
 */
#define	HALFWORD_ACCESSIBLE(x) 			\
	((((x) & 0xffff8000) == 0) || (((x) & 0xffff8000) == 0x7fff8000))

#define	TIME_DELTA_FITS(x) 			\
	(((x) >> 32) == 0)

/*
 * CAUTION: Use the following macro only when doing a self relative pointer
 *		to a target in the same block
 */
#define	PTR_DIFF(item, ref)			\
	((tnf_ref32_t)((tnf_record_p)(item) - (tnf_record_p)(ref)))

/*
 * Typedefs
 */

typedef struct {
	tnf_probe_event_t		probe_event;
	tnf_time_delta_t		time_delta;
} probe_event_prototype_t;

/*
 * Declarations
 */

/*
 * tnf_trace_alloc
 * 	the probe allocation function
 */

void *
tnf_trace_alloc(tnf_ops_t *ops, tnf_probe_control_t *probe_p,
    tnf_probe_setup_t *set_p)
{
	tnf_uint32_t 		probe_index;
	tnf_record_p		sched_record_p;
	tnf_reference_t 	sched_offset, tag_disp;
	tnf_block_header_t	*block;
	tnf_uint32_t		shift;
	probe_event_prototype_t *buffer;
	hrtime_t 		curr_time, time_diff;
	tnf_schedule_t		*sched;
	tnf_ref32_t		*fwd_p;

	/*
	 * Check the "tracing active" flag both before and after
	 * setting the busy bit; this avoids a race in which we check
	 * the "tracing active" flag, then it gets turned off, and
	 * the buffer gets deallocated, before we've set the busy bit.
	 */
	if (!tnf_tracing_active)
		return (NULL);
	if (tnfw_b_get_lock(&ops->busy)) /* atomic op flushes WB */
		return (NULL);
	if (!tnf_tracing_active)
		goto null_ret;

	/*
	 * Write probe tag if needed
	 */
	probe_index = probe_p->index;
	if (probe_index == 0) {
		if ((probe_index = tnf_probe_tag(ops, probe_p)) == 0)
			goto null_ret;
	}

	/*
	 * Allocate memory + 2 words for forwarding pointers (may be needed)
	 */
	if ((buffer = tnfw_b_alloc(&ops->wcb,
	    probe_p->tnf_event_size + (2 * sizeof (tnf_ref32_t)),
	    ops->mode)) == NULL)
		goto null_ret;

	/* LINTED pointer cast may result in improper alignment */
	fwd_p = (tnf_ref32_t *)((char *)buffer + probe_p->tnf_event_size);

	/*
	 * Get high part of tag
	 */
	if (PROBE_IS_FILE_PTR(probe_index)) {
		/* common case - probe_index is a file ptr */
		tag_disp = probe_index & PROBE_INDEX_LOW_MASK;
		TNFW_B_GIVEBACK(&ops->wcb, fwd_p + 1);
	} else {
		/* use one of the extra alloced words for the forwarding ptr */
		/* REMIND: can make the next tnf_ref32 more efficient */
		*fwd_p = tnf_ref32(ops, (tnf_record_p)probe_index,
		    (tnf_reference_t)fwd_p);
		tag_disp = PTR_DIFF(fwd_p, buffer);
		tag_disp |= TNF_TAG16_T_REL;
		tag_disp = tag_disp << TNF_REF32_TAG16_SHIFT;
		fwd_p++;
	}

	/*
	 * Get timestamp
	 */
	curr_time = gethrtime();

	/*
	 * Write schedule record if needed
	 */

	sched = &ops->schedule;

	if ((sched_record_p = sched->record_p) == NULL)
		/* No record written yet */
		goto new_schedule;

	/* LINTED pointer cast */
	block = (tnf_block_header_t *)((tnf_uint32_t)buffer & TNF_BLOCK_MASK);
	/* LINTED pointer cast */
	shift = ((tnf_buf_file_header_t *)tnf_buf)->com.file_log_size;
	sched_offset = TNF_REF32_MAKE_RECLAIMABLE(
		((sched->record_gen - block->generation) << shift) +
		    (sched_record_p - (caddr_t)buffer));
	if (!HALFWORD_ACCESSIBLE(sched_offset))
		/* Record too far away to reference */
		goto new_schedule;

	time_diff = curr_time - sched->time_base;
	if (!TIME_DELTA_FITS(time_diff))
		/* Time delta can't fit in 32 bits */
		goto new_schedule;

	if (sched->cpuid != CPU->cpu_id)
		/* CPU information is invalid */
		goto new_schedule;

	/*
	 * Can reuse existing schedule record
	 * Since we did not allocate any more space, can giveback
	 */
	TNFW_B_GIVEBACK(&ops->wcb, fwd_p);

good_ret:
	/*
	 * Store return params and two common event members, return buffer
	 */
	set_p->tpd_p = ops;
	set_p->buffer_p = buffer;
	set_p->probe_p = probe_p;
	buffer->probe_event = ENCODED_TAG(tag_disp, sched_offset);
	buffer->time_delta = tnf_time_delta(ops, (unsigned long)time_diff,
	    &buffer->probe_time_delta);
	return (buffer);

new_schedule:
	/*
	 * Write a new schedule record for this thread
	 */
	sched->cpuid = CPU->cpu_id;
	sched->time_base = curr_time;
	time_diff = 0;
	if ((sched_record_p = tnf_kernel_schedule(ops, sched)) != NULL) {
		/* use one of the extra alloced words for the forwarding ptr */
		/* REMIND: can make the next tnf_ref32 more efficient */
		*fwd_p = tnf_ref32(ops, sched_record_p,
			(tnf_reference_t)fwd_p);
		sched_offset = PTR_DIFF(fwd_p, buffer);
	} else {
		/* Allocation failed (tracing may have been stopped) */
		sched_offset = 0;
		*fwd_p = TNF_NULL;
	}
	goto good_ret;

null_ret:
	/*
	 * Clear busy flag and return null
	 */
	ops->busy = 0;		/* XXX */
	return (NULL);
}

/*
 * tnf_trace_end
 *	the last (usually only) function in the list of probe functions
 */
void
tnf_trace_end(tnf_probe_setup_t *set_p)
{
	(set_p->probe_p->commit_func)(set_p);
	set_p->tpd_p->busy = 0; /* XXX */
}

/*
 * tnf_trace_commit
 *	a probe commit function that really commits trace data
 */
void
tnf_trace_commit(tnf_probe_setup_t *set_p)
{
	(void) tnfw_b_xcommit(&set_p->tpd_p->wcb);
}

/*
 * tnf_trace_rollback
 *	a probe commit function that unrolls trace data
 */
void
tnf_trace_rollback(tnf_probe_setup_t *set_p)
{
	set_p->tpd_p->schedule.record_p = NULL;
	(void) tnfw_b_xabort(&set_p->tpd_p->wcb);
}

/*
 * tnf_allocate
 *	exported interface for allocating trace memory
 */

void *
tnf_allocate(tnf_ops_t *ops, size_t size)
{
	return (tnfw_b_alloc(&ops->wcb, size, ops->mode));
}
