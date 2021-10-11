/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)door_support.c 1.6	95/04/18 SMI"

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/file.h>
#include <sys/door.h>
#include <sys/door_data.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/stack.h>
#include <sys/errno.h>
#include <sys/debug.h>

void	dr_set_pc(void *, u_int);
void	dr_set_sp(void *, u_int);
void	dr_set_data_size(void *, u_int);
void	dr_set_door_ptr(void *, u_int);
void	dr_set_door_size(void *, u_int);
void	dr_set_nservers(void *, u_int);

/*
 * All door server threads are dispatched here when they are signaled
 * 	They copy out the arguments passed by the caller and return
 *	to user land to execute the object invocation
 *
 * User level server expects arguments in this form:
 *
 * void object_server(void *cookie, caddr_t data_ptr, int data_size,
 *			int *door_ptr, int door_size);
 */
longlong_t
door_server_dispatch(door_data_t *caller_t, door_node_t *dp,
			caddr_t sp, int *error)
{
	klwp_id_t	lwp = ttolwp(curthread);
	door_desc_t	*didpp;
	caddr_t		data_ptr = NULL;
	int		asize;
	int		ndid;
	int		door_size;
	rval_t		rval;
	struct file	**fpp;

	ASSERT(caller_t->d_flag & DOOR_HOLD);
	/*
	 * Copy out parameters from temp buffer.
	 * Place the arguments on the stack and point to them.
	 */
	data_ptr = sp;			/* Base of user stack */
	data_ptr = (caddr_t)(data_ptr - SA(caller_t->d_bsize));
	dr_set_sp(lwp->lwp_regs, (u_int)(data_ptr-SA(MINFRAME)));
	/*
	 * First the arguments
	 */
	asize = caller_t->d_asize;
	ndid = caller_t->d_ndid;

	if (asize > 0) {
		if (copyout(caller_t->d_buf, data_ptr, asize)) {
			/* Stack isn't big enough to hold the data */
			door_fp_close(caller_t->d_fpp, ndid);
			*error = EFAULT;
			return (-1);
		}
		dr_set_data_size(lwp->lwp_regs, (u_int)asize);
	} else {
		dr_set_data_size(lwp->lwp_regs, 0);
	}

	/*
	 * stuff the passed doors into our proc, copyout the dids
	 */
	if (ndid > 0) {
		door_desc_t *start;

		door_size = ndid * sizeof (door_desc_t);
		start = didpp = (door_desc_t *)
			&caller_t->d_buf[caller_t->d_bsize - door_size],
		fpp = caller_t->d_fpp;

		while (ndid--) {
			didpp->d_descriptor = door_insert(*fpp);
			didpp->d_attributes = door_attributes(*fpp);
			if (didpp->d_descriptor == -1) {
				/*
				 * Cleanup up newly created fd's
				 * and close any remaining fps.
				 */
				door_fd_close(start, didpp - start, 0);
				door_fp_close(fpp, ndid + 1);
				*error = EMFILE;
				return (-1);
			}
			didpp++; fpp++;
		}
		if (copyout((caddr_t)start,
			&data_ptr[caller_t->d_bsize - door_size], door_size)) {
			door_fd_close(start, caller_t->d_ndid, 0);
			*error = EFAULT;
			return (-1);
		}
		dr_set_door_ptr(lwp->lwp_regs,
			(u_int)&data_ptr[caller_t->d_bsize - door_size]);
		dr_set_door_size(lwp->lwp_regs,
			(u_int)caller_t->d_ndid);
	} else {
		dr_set_door_ptr(lwp->lwp_regs, NULL);
		dr_set_door_size(lwp->lwp_regs, ndid);
	}


	dr_set_pc(lwp->lwp_regs, (u_int)dp->door_pc);

	/* Let the library know if this is the last available server */
	if (curproc->p_server_threads == NULL) {
		dr_set_nservers(lwp->lwp_regs, 0);
	} else {
		dr_set_nservers(lwp->lwp_regs, 1);
	}

	rval.r_val1 = (int)dp->door_data;
	if (asize > 0)
		rval.r_val2 = (int)data_ptr;
	else if (asize == (int)DOOR_UNREF_DATA)
		rval.r_val2 = (int)DOOR_UNREF_DATA;
	else
		rval.r_val2 = 0;

	*error = 0;
	return (rval.r_vals);
}
