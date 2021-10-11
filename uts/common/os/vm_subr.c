/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986, 1987, 1988, 1989, 1990  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)vm_subr.c	1.75	94/11/17 SMI"
/*	From:	SVr4.0	"kernel:os/vm_subr.c	1.24"		*/
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>
#include <sys/inline.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/vmsystm.h>
#include <sys/cpuvar.h>
#include <sys/mman.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/vm.h>
#include <sys/tnf_probe.h>

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/page.h>
#include <vm/seg_vn.h>
#include <vm/seg_kmem.h>

/*
 * Raw I/O. The arguments are
 *	The strategy routine for the device
 *	A buffer, which will always be a special buffer
 *	  header owned exclusively by the device for this purpose
 *	The device number
 *	Read/write flag
 * Essentially all the work is computing physical addresses and
 * validating them.
 * If the user has the proper access privileges, the process is
 * marked 'delayed unlock' and the pages involved in the I/O are
 * faulted and locked. After the completion of the I/O, the above pages
 * are unlocked.
 */

extern int maxphys;

void
minphys(struct buf *bp)
{
	if (bp->b_bcount > maxphys)
		bp->b_bcount = maxphys;
}

/*
 * We declare (*strat) to be an integer function
 * since it is invariably a driver's strategy
 * entry point which is type as in int function.
 * Following the principle of least pain, we
 * deal with this here.
 */
physio(strat, bp, dev, rw, mincnt, uio)
	int (*strat)();
	register struct buf *bp;
	dev_t dev;
	int rw;
	void (*mincnt)();
	struct uio *uio;
{
	register struct iovec *iov;
	register int c;
	faultcode_t fault_err;
	struct proc *procp;
	struct as *asp;
	char *a;
	int	error = 0;
	int	unsafe;			/* nz if called from MT-unsafe driver */
	struct buf tbuf;

	unsafe = UNSAFE_DRIVER_LOCK_HELD();

	/* Kernel probe */
	TNF_PROBE_4(physio_start, "io rawio", /* CSTYLED */,
		tnf_device,	device,		dev,
		tnf_offset,	offset,		uio->uio_loffset,
		tnf_size,	size,		uio->uio_resid,
		tnf_bioflags,	rw,		rw);

	if (rw == B_READ) {
		CPU_STAT_ADD_K(cpu_sysinfo.phread, 1);
	} else {
		CPU_STAT_ADD_K(cpu_sysinfo.phwrite, 1);
	}

	if (bp == NULL) {
		bp = &tbuf;
		struct_zero((caddr_t)bp, sizeof (struct buf));
		sema_init(&bp->b_sem, 0, "bp owner", SEMA_DEFAULT, DEFAULT_WT);
		sema_init(&bp->b_io, 0, "bp io", SEMA_DEFAULT, DEFAULT_WT);
	}

	if (uio->uio_segflg == UIO_USERSPACE) {
		procp = ttoproc(curthread);
		asp = procp->p_as;
	} else {
		procp = NULL;
		asp = &kas;
	}
	ASSERT(SEMA_HELD(&bp->b_sem));

	while (uio->uio_iovcnt > 0) {
		iov = uio->uio_iov;

		bp->b_oerror = 0;		/* old error field */
		bp->b_error = 0;
		bp->b_proc = procp;

		while (iov->iov_len > 0) {
			if (uio->uio_resid == 0)
				break;
			if (uio->uio_loffset < 0 ||
			    uio->uio_loffset > MAXOFFSET_T) {
				error = EINVAL;
				break;
			}
			bp->b_flags = B_KERNBUF | B_BUSY | B_PHYS | rw;
			bp->b_edev = dev;
			bp->b_dev = cmpdev(dev);
			bp->b_lblkno = btodt(uio->uio_loffset);
			/*
			 * Don't count on b_addr remaining untouched by the
			 * code below (it may be reset because someone does
			 * a bp_mapin on the buffer) -- reset from the iov
			 * each time through, updating the iov's base address
			 * instead.
			 */
			a = bp->b_un.b_addr = iov->iov_base;
			bp->b_bcount = MIN(iov->iov_len, uio->uio_resid);
			(*mincnt)(bp);
			c = bp->b_bcount;

			if (unsafe)
				mutex_exit(&unsafe_driver);
			fault_err = as_fault(asp->a_hat, asp, a, (uint)c,
				F_SOFTLOCK, rw == B_READ? S_WRITE : S_READ);
			if (unsafe)
				mutex_enter(&unsafe_driver);
			if (fault_err != 0) {
				/*
				 * Even though the range of addresses were
				 * valid and had the correct permissions,
				 * we couldn't lock down all the pages for
				 * the access we needed. (e.g. we needed to
				 * allocate filesystem blocks for
				 * rw == B_READ but the file system was full).
				 */
				switch (FC_CODE(fault_err)) {
				case FC_OBJERR:
					error = FC_ERRNO(fault_err);
					break;

				case FC_PROT:
					error = EACCES;
					break;

				default:
					error = EFAULT;
					break;
				}
				bp->b_flags |= B_ERROR;
				bp->b_error = error;
				bp->b_flags &= ~(B_BUSY|B_WANTED|B_PHYS);
				break;
			}
			(void) (*strat)(bp);
			error = biowait(bp);
			if (unsafe)
				mutex_exit(&unsafe_driver);
			if (as_fault(asp->a_hat, asp, a, (uint)c, F_SOFTUNLOCK,
			    rw == B_READ ? S_WRITE : S_READ) != 0)
				cmn_err(CE_PANIC, "physio unlock");
			if (unsafe)
				mutex_enter(&unsafe_driver);

			c -= bp->b_resid;
			iov->iov_base += c;
			iov->iov_len -= c;
			uio->uio_resid -= c;
			uio->uio_loffset += c;
			/* bp->b_resid - temp kludge for tape drives */
			if (bp->b_resid || error)
				break;
		}
		bp->b_flags &= ~(B_BUSY|B_WANTED|B_PHYS);
		/* bp->b_resid - temp kludge for tape drives */
		if (bp->b_resid || error)
			break;
		uio->uio_iov++;
		uio->uio_iovcnt--;
	}

	/* Kernel probe */
	TNF_PROBE_1(physio_end, "io rawio", /* CSTYLED */,
		tnf_device,	device,		dev);

	return (error);
}

/*
 * Returns 0 on success, or an error on failure.
 *
 * This function is no longer a part of the DDI/DKI.
 * However, for compatibility, its interface should not
 * be changed and it should not be removed from the kernel.
 */
int
useracc(addr, count, access)
	register caddr_t addr;
	register u_int count;
	register int access;
{
	u_int prot;

	prot = PROT_USER | ((access == B_READ) ? PROT_READ : PROT_WRITE);
	return (as_checkprot(ttoproc(curthread)->p_as,
	    (caddr_t)addr, count, prot));
}

/*
 * Acquire the "exclusive" lock on pages [start, last - 1]
 * and add pages [first, last - 1] to the page free list.
 * Assumes that memory is allocated with no overlapping
 * allocation ranges, and that increasing physical addresses
 * imply increasing page struct addresses.
 */
void
memialloc(start, first, last)
	register uint start, first, last;
{
	register page_t *pp;

	if (start > first || first >= last)
		cmn_err(CE_PANIC, "memialloc");

	for (; start <= (last - 1); start++) {
		/*
		 * Acquire the "exclusive" lock on the page.
		 */
		pp = page_numtopp(start, SE_EXCL);
		if (pp == (page_t *)NULL)
			cmn_err(CE_PANIC, "memialloc");

		if (start >= first)
			page_free(pp, 1);
	}
}
