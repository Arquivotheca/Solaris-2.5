/*
*
*ident  "@(#)xtd_to.c 1.26     95/05/25 SMI"
*
* Copyright(c) 1993, 1994 by Sun Microsystems, Inc.
*
*/

/*
*  Description:
* This module contains functions for interacting with
* the threads within the program.
*/

#ifdef __STDC__
#pragma weak td_thr_getgregs = __td_thr_getgregs /* i386 work around */
#pragma weak td_thr_setgregs = __td_thr_setgregs /* i386 work around */
#endif				/* __STDC__ */

#include <thread_db.h>
#include "thread_db2.h"

#include <signal.h>
#include "td.h"
#include "xtd_to.h"
#include "td.extdcl.h"
#include "xtd_arch.h"

/* These are used to keep lines <80 characters. */
#define	XTDT_M1 "Writing rwin to stack: td_thr_setregs"
#define	XTDT_M2 "Writing process: __td_write_thr_struct"
#define	XTDT_M3 "Reading rwin from stack: td_thr_getregs"

/*
* Description:
*   Get the general registers for a given thread.  For a
* thread that is currently executing on an LWP, (td_thr_state_e)
* TD_THR_ACTIVE, all registers in regset will be read for the
* thread.  For a thread not executing on an LWP, only the
* following registers will be read.
*
*   %i0-%i7,
*   %l0-%l7,
*   %g7, %pc, %sp(%o6).
*
* %pc and %sp will be the program counter and stack pointer
* at the point where the thread will resume execution
* when it becomes active, (td_thr_state_e) TD_THR_ACTIVE.
*
* Input:
*   *th_p - thread handle
*
* Output:
*   *regset - Values of general purpose registers(see
* 		sys/procfs.h)
*   td_thr_getgregs - return value
*
* Side effect:
*   none
*   Imported functions called: ps_pstop, ps_pcontinue, ps_pdread, ps_lgetregs.
*/
td_err_e
__td_thr_getgregs(const td_thrhandle_t *th_p, prgregset_t regset)
{
	td_err_e	return_val = TD_OK;
	td_terr_e	td_return;
	uthread_t	thr_struct;
	struct rwindow	reg_window;
	paddr_t		thr_sp;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdread(0, 0, 0, 0);
		(void) ps_lgetregs(0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}
	if (regset == NULL) {
		return (TD_ERR);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
		return (return_val);
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}


	/*
	 * More than 1 byte is being read.  Stop the process.
	 */

	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {

		/*
		 * Extract the thread struct address
		 * from the thread handle and read
		 * the thread struct.
		 */

		td_return = __td_read_thr_struct(th_p->th_ta_p, th_p->th_unique,
			&thr_struct);


		if (td_return == TD_TOK) {
			if (ISVALIDLWP(&thr_struct)) {
				if (ps_lgetregs(th_p->th_ta_p->ph_p,
					thr_struct.t_lwpid, regset) == PS_ERR) {
					return_val = TD_DBERR;
				}
			} else {
				/*
				 * Set all regset to zero so that values not
				 * set below are zero.  This is a friendly
				 * value.
				 */
				memset(regset, 0, sizeof (prgregset_t));
				return_val = TD_PARTIALREG;

				/*
				 * Get G7 from thread handle,
				 * SP from thread struct
				 * and FP from register window.
				 * Register window is
				 * saved on the stack.
				 */
				regset[R_G7] = th_p->th_unique;
				regset[R_O6] = thr_struct.t_sp;
				regset[R_PC] = thr_struct.t_pc;
				thr_sp = thr_struct.t_sp;
				if (thr_sp != NULL) {
					if (ps_pdread(
					    th_p->th_ta_p->ph_p, thr_sp,
					    (char *) &reg_window,
					    sizeof (reg_window)) != PS_OK) {
						return_val = TD_DBERR;
						__td_report_po_err(return_val,
						    XTDT_M3);
					} else {
						(void) memcpy(&regset[R_L0],
						    &reg_window,
						    sizeof (reg_window));
					}
				}
			}
		} else {
			return_val = TD_ERR;
		}

		/*
		 * Continue process.
		 */
		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}

/*
* Description:
*   Map fpregset_t (sys/regset.h) to prfpregset_t
* (sys/procfs.h).
*
* Input:
*   *sink - destination for fp register information.
*
* Output:
*   *source - source for fp register information.
*   __map_fp_reg2pr - return value of function.
*
* Side effects:
*   none
*/

td_ierr_e
__map_fp_reg2pr(prfpregset_t * sink, fpregset_t * source)
{
	td_ierr_e	return_val = TD_IOK;

	/*
	 * This is the only place in library that needs to know about
	 * internals of prfpregset_t and fpregset_t so access directly (no
	 * access macros).
	 */

	if ((sink != NULL) && (source != NULL)) {

		/*
		 * Clear out the sink first.
		 */
		memset(sink, 0, sizeof (*sink));

		(void) memcpy(sink->pr_fr.pr_regs, source->fpu_fr.fpu_regs,
			sizeof (sink->pr_fr.pr_regs));

		sink->pr_fsr = source->fpu_fsr;
		sink->pr_qcnt = source->fpu_qcnt;
		sink->pr_q_entrysize = source->fpu_q_entrysize;
		sink->pr_en = source->fpu_en;

		/*
		 * If the source addresses is NULL, skip the copy. The FP
		 * registers are not always used.
		 */
		if (source->fpu_q) {
			(void) memcpy(sink->pr_q, source->fpu_q,
				sizeof (sink->pr_q));
		}
	} else {
		return_val = TD_INULL;
		__td_report_in_err(return_val,
			"__map_fp_reg2pr input parameter error");
	}

	return (return_val);
}

/*
* Description:
*   Map prfpregset_t (sys/procfs.h) to fpregset_t
* (sys/regset.h).
*
* Input:
*   *sink - destination for fp register information.
*
* Output:
*   *source - source for fp register information.
*   __map_fp_pr2reg - return value of function.
*
* Side effects:
*   none
*/

td_ierr_e
__map_fp_pr2reg(fpregset_t * sink, prfpregset_t * source)
{
	td_ierr_e	return_val = TD_IOK;
	int		q_size;

	/*
	 * This is the only place in library that needs to know about
	 * internals of prfpregset_t and fpregset_t so access directly (no
	 * access macros).
	 */

	if ((sink != NULL) && (source != NULL)) {

		/*
		 * Clear out the sink first.
		 */
		memset(sink, 0, sizeof (*sink));

		(void) memcpy(&sink->fpu_fr.fpu_regs, &source->pr_fr.pr_regs,
			sizeof (sink->fpu_fr.fpu_regs));
		sink->fpu_fsr = source->pr_fsr;
		sink->fpu_qcnt = source->pr_qcnt;
		sink->fpu_q_entrysize = source->pr_q_entrysize;
		sink->fpu_en = source->pr_en;

		/*
		 * Check to see if there is queue information.
		 * If there is, malloc space for it and copy.
		 */
		if (sink->fpu_qcnt) {
			q_size = sink->fpu_qcnt * sink->fpu_q_entrysize;
			sink->fpu_q = (struct fq *) malloc(q_size);
			if (sink->fpu_q) {
				(void) memcpy(sink->fpu_q,
					source->pr_q, q_size);
				return_val = TD_IOK;
			} else {
				__td_report_in_err(TD_IMALLOC,
					"malloc failure in __map_fp_pr2reg");
				return_val = TD_IMALLOC;
			}
		}
	} else {
		return_val = TD_INULL;
		__td_report_in_err(return_val,
			"__map_fp_reg2pr input parameter error");
	}

	return (return_val);
}


/*
* Description:
*   Set the general registers for a given
* thread.  For a thread that is currently executing on
* an LWP, (td_thr_state_e) TD_THR_ACTIVE, all registers
* in regset will be written for the thread.  For a thread
* not executing on an LWP, only the following registers
* will be written
*
*   %i0-%i7,
*   %l0-%l7,
*   %pc, %sp(%o6).
*
* %pc and %sp will be the program counter and stack pointer
* at the point where the thread will resume execution
* when it becomes active, (td_thr_state_e) TD_THR_ACTIVE.
*
* Input:
*   *th_p -  thread handle
*   *regset - Values of general purpose registers(see
* 	sys/procfs.h)
*
* Output:
*   td_thr_setgregs - return value
*
* Side effect:
*   The general purpose registers for the thread are changed.
*   Imported functions called: ps_pstop, ps_pcontinue, ps_pdread, ps_lsetregs
*
*/

td_err_e
__td_thr_setgregs(const td_thrhandle_t *th_p, const prgregset_t regset)
{
	td_err_e	return_val = TD_OK;
	td_terr_e	td_return;
	uthread_t	thr_struct;
	paddr_t		thr_sp;

#ifdef TEST_PS_CALLS
	if (td_noop) {
		(void) ps_pstop(0);
		(void) ps_pcontinue(0);
		(void) ps_pdread(0, 0, 0, 0);
		(void) ps_pdwrite(0, 0, 0, 0);
		(void) ps_lsetregs(0, 0, 0);
		return (TD_OK);
	}
#endif

	/*
	 * Sanity checks.
	 */
	if (th_p == NULL) {
		return_val = TD_BADTH;
		return (return_val);
	}
	if ((th_p->th_ta_p == NULL) || (th_p->th_unique == NULL)) {
		return_val = TD_BADTA;
		return (return_val);
	}
	if (regset == NULL) {
		return (TD_ERR);
	}

	/*
	 * Get a reader lock.
	 */
	if (rw_rdlock(&(th_p->th_ta_p->rwlock))) {
		return_val = TD_ERR;
		return (return_val);
	}

	if (th_p->th_ta_p->ph_p == NULL) {
		rw_unlock(&(th_p->th_ta_p->rwlock));
		return_val = TD_BADTA;
		return (return_val);
	}


	/*
	 * More than 1 byte is geing read.  Stop the process.
	 */

	if (ps_pstop(th_p->th_ta_p->ph_p) == PS_OK) {

		/*
		 * Extract the thread struct address
		 * from the thread handle and read
		 * the thread struct.
		 */

		td_return = __td_read_thr_struct(th_p->th_ta_p,
			th_p->th_unique, &thr_struct);


		if (td_return == TD_TOK) {
			if (ISONPROC(&thr_struct) || ISBOUND(&thr_struct) ||
					ISPARKED(&thr_struct)) {

				/*
				 * Thread has an associated lwp.
				 * Write regsiters
				 * back to lwp.
				 */
				if (ps_lsetregs(th_p->th_ta_p->ph_p,
					thr_struct.t_lwpid, regset) == PS_ERR) {
					return_val = TD_DBERR;
				}
			} else {
				thr_sp = thr_struct.t_sp;

				if (thr_sp) {

					/*
					 * Write back the local and in register
					 * values to the stack.
					 */
					if (ps_pdwrite(th_p->th_ta_p->ph_p,
					    thr_sp, (char *) &regset[R_L0],
					    sizeof (struct rwindow)) != PS_OK) {
						return_val = TD_DBERR;
						__td_report_po_err(return_val,
						    XTDT_M1);
					}

					/*
					 * Thread does not have associated lwp.
					 * Modify thread %i and %o registers.
					 */
#ifdef PHASE2
					Don 't change the values of
					the struct
					thread pointer and stack
					point in the thread
					handle nor thread struct.
					We may do something
					later if required.
					th_p->th_unique = regset[R_G7];
					thr_struct.t_sp = regset[R_O6];

					/*
					 * Write back the global
					 * register values into
					 * the thread struct
					 */
					td_return = __td_write_thr_struct(
						th_p->th_ta_p, th_p->th_unique,
						&thr_struct);
#endif
				}	/* Good stack pointer  */
				else {
					return_val = TD_ERR;
				}
			}   /*   Thread not on lwp  */
		}   /*   Read thread data ok  */
		else {
			return_val = TD_ERR;
		}

		/*
		 * Continue process.
		 */
		if (ps_pcontinue(th_p->th_ta_p->ph_p) != PS_OK) {
			return_val = TD_DBERR;
		}
	}
	/* ps_pstop succeeded   */
	else {
		return_val = TD_DBERR;
	}

	rw_unlock(&(th_p->th_ta_p->rwlock));
	return (return_val);
}
