/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)prusrio.c	1.26	95/09/05 SMI"	/* from SVr4.0 1.12 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/debug.h>
#include <sys/errno.h>
#include <sys/inline.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/sysmacros.h>
#include <sys/uio.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/poll.h>
#include <sys/cpuvar.h>

#include <sys/signal.h>
#include <sys/user.h>

#include <vm/as.h>
#include <vm/seg.h>

#include <fs/proc/prdata.h>

/*
 * Perform I/O to/from an address space
 */
int
prusrio(as, rw, uiop)
	struct as *as;
	enum uio_rw rw;
	struct uio *uiop;
{
	extern void sync_icache(caddr_t, u_int);
	caddr_t addr;
	caddr_t page;
	caddr_t vaddr;
	struct seg *seg;
	int error = 0;
	int err = 0;
	u_int prot;
	int protchanged;
	u_int len;
	enum seg_rw srw = (rw == UIO_WRITE) ? S_WRITE : S_READ;
	int total = uiop->uio_resid;

	AS_LOCK_ENTER(as, &as->a_lock, RW_READER);

	/*
	 * Fault in the necessary pages one at a time, map them into
	 * kernel space, and copy in or out.
	 */
	while (error == 0 && uiop->uio_resid != 0) {
		/*
		 * Locate segment containing address of interest.
		 */
		addr = (caddr_t) uiop->uio_offset;
		page = (caddr_t) (((u_int)addr) & PAGEMASK);
		if ((seg = as_segat(as, page)) == NULL) {
			err = error = EIO;
			break;
		}
		SEGOP_GETPROT(seg, page, 0, &prot);

		if (srw == S_READ && (prot & PROT_READ) == 0) {
			protchanged = 1;
			if (SEGOP_SETPROT(seg, page, PAGESIZE,
			    prot|PROT_READ)) {
				err = error = EIO;
				break;
			}
		} else if (srw == S_WRITE && (prot & PROT_WRITE) == 0) {
			protchanged = 1;
			if (SEGOP_SETPROT(seg, page, PAGESIZE,
			    prot|PROT_WRITE)) {
				err = error = EIO;
				break;
			}
		} else {
			protchanged = 0;
		}

		if (SEGOP_FAULT(as->a_hat, seg, page, PAGESIZE,
		    F_SOFTLOCK, (prot & PROT_WRITE)? S_WRITE : srw)) {
			err = error = EIO;
			break;
		}
		CPU_STAT_ADD_K(cpu_vminfo.softlock, 1);
		vaddr = prmapin(as, addr, srw == S_WRITE);

		/*
		 * Drop the address space lock before the uiomove().
		 * We don't want to hold a lock while taking a page fault.
		 */
		AS_LOCK_EXIT(as, &as->a_lock);
		len = min(uiop->uio_resid, (page + PAGESIZE) - addr);
		error = uiomove(vaddr, (long) len, rw, uiop);
		AS_LOCK_ENTER(as, &as->a_lock, RW_READER);

		/* Keeps i-$ consistent */
		if (srw == S_WRITE && (prot & PROT_EXEC) && !error)
			sync_icache(vaddr, len);
		prmapout(as, addr, vaddr, srw == S_WRITE);
		(void) SEGOP_FAULT(as->a_hat, seg, page, PAGESIZE,
		    F_SOFTUNLOCK, srw);

		if (protchanged)
			(void) SEGOP_SETPROT(seg, page, PAGESIZE, prot);
	}

	/*
	 * If the I/O was truncated because a page didn't exist,
	 * don't return an error.
	 */
	if (err && total != uiop->uio_resid)
		error = 0;

	AS_LOCK_EXIT(as, &as->a_lock);
	return (error);
}
