/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1989 by Sun Microsystems, Inc.
 */

#ident	"@(#)grow.c	1.35	95/03/02 SMI"	/* from SVr4.0 1.35 */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/lock.h"
#include "sys/var.h"
#include "sys/proc.h"
#include "sys/tuneable.h"
#include "sys/debug.h"
#include "sys/cmn_err.h"
#include "sys/cred.h"
#include "sys/vnode.h"
#include "sys/vm.h"
#include "sys/file.h"
#include "sys/mman.h"
#include "sys/vmparam.h"

#include "vm/hat.h"
#include "vm/as.h"
#include "vm/seg.h"
#include "vm/seg_dev.h"
#include "vm/seg_vn.h"


int
brk(caddr_t nva)
{
	size_t size;
	register caddr_t ova;			/* current break address */
	register int	change;			/* change in pages */
	register int	error;
	register struct proc *p = ttoproc(curthread);
	register struct as *as = p->p_as;

	/*
	 * Serialize brk operations on an address space.
	 */
	as_rangelock(as);

	size = nva - p->p_brkbase;
	nva = (caddr_t)roundup((u_int)(p->p_brkbase + size), PAGESIZE);
	ova = (caddr_t)roundup((u_int)(p->p_brkbase + p->p_brksize), PAGESIZE);

	if ((int)size < 0 || (size > p->p_brksize &&
	    size > u.u_rlimit[RLIMIT_DATA].rlim_cur)) {
		as_rangeunlock(as);
		return (set_errno(ENOMEM));
	}

	change = nva - ova;

	if (change > 0) {
		/*
		 * Add new zfod mapping to extend UNIX data segment
		 */
		error = as_map(as, ova, (u_int)change,
				segvn_create, zfod_argsp);
		if (error) {
			as_rangeunlock(as);
			return (set_errno(error));
		}

	} else if (change < 0) {
		/*
		 * Release mapping to shrink UNIX data segment.
		 * NOTE: change is negative.
		 */
		(void) as_unmap(as, ova + change, (u_int)(-change));
	}

	p->p_brksize = size;
	as_rangeunlock(as);
	return (0);
}

/*
 * Grow the stack to include the SP.  Return 1 if successful, 0 otherwise.
 * This routine is machine dependent.
 */
int
grow(int *sp)
{
	register int	si;
	register int	ssize;
	register struct as *as;
	register struct proc *p = ttoproc(curthread);
	int error;

	if (sp == 0)
		return (0);

#ifdef i386
	/* fix for wrong behavior of handling faults on the user text area */
	if ((u_int)sp >= (u_int)USRSTACK)
		return (0);
#endif

	as = p->p_as;
	ssize = btoc(p->p_stksize);
	si = btoc((u_int)USRSTACK - (u_int)sp) - ssize + btoc(SINCR);

	if (si == 0)
		return (1);
	if (si < 0 && -si > ssize)
		si = -ssize;

	if (si > 0) {
		if (ctob(ssize + si) > u.u_rlimit[RLIMIT_STACK].rlim_cur) {
			return (0);
		}

		as_rangelock(as);
		if ((error = as_map(as,
		    (caddr_t) ((caddr_t)USRSTACK - ctob(ssize + si)),
		    (u_int)ctob(si), segvn_create, zfod_argsp)) != 0) {
			as_rangeunlock(as);
			if (error == EAGAIN) {
				cmn_err(CE_WARN,
				    "Sorry, no swap space to grow stack "
				    "for pid %d (%s)\n",
				    (int)p->p_pid, u.u_comm);
			}
			return (0);
		}
		p->p_stksize += ctob(si);
		as_rangeunlock(as);

		/*
		 * Ensure that translations are setup so as to avoid faults
		 * when this virtual address range is accessed by the user
		 * thread.
		 */
		(void) as_fault(as->a_hat, as,
		    (caddr_t)((caddr_t)USRSTACK - ctob(ssize + si)),
		    (u_int)ctob(si), F_INVAL, S_WRITE);
	} else {
		/*
		 * Release mapping to shrink UNIX stack segment
		 */
		(void) as_unmap(as, (caddr_t)((caddr_t)USRSTACK - ctob(ssize)),
			(u_int)ctob(-si));
		p->p_stksize -= ctob(-si);
	}

	return (1);

}

int
getpagesize()
{
	return (PAGESIZE);
}

int
smmap(caddr_t addr, size_t len, int prot, int flags, int fd, off_t pos)
{
	register struct vnode *vp;
	struct as *as = ttoproc(curthread)->p_as;
	struct file *fp;
	u_int uprot, maxprot;
	u_int type;
	register int error;
#ifdef sparc
	int old_mmap;
#endif

	type = flags & MAP_TYPE;

	if ((flags & ~(MAP_SHARED | MAP_PRIVATE | MAP_FIXED | _MAP_NEW |
	    MAP_NORESERVE)) != 0) { /* not implemented, but don't fail here */
		/* | MAP_RENAME */	/* not yet implemented, let user know */
		return (set_errno(EINVAL));
	}

	if (type != MAP_PRIVATE && type != MAP_SHARED)
		return (set_errno(EINVAL));

	/*
	 * Check for bad lengths and file position.
	 * We let the VOP_MAP routine check for negative lengths
	 * since on some vnode types this might be appropriate.
	 */
	if (len == 0 || (pos & PAGEOFFSET) != 0)
		return (set_errno(EINVAL));

	if ((fp = GETF(fd)) == NULL)
		return (set_errno(EBADF));

	vp = fp->f_vnode;

	maxprot = PROT_ALL;		/* start out allowing all accesses */
	uprot = prot | PROT_USER;

	if (type == MAP_SHARED && (fp->f_flag & FWRITE) == 0) {
		/* no write access allowed */
		maxprot &= ~PROT_WRITE;
	}

	/*
	 * XXX - Do we also adjust maxprot based on protections
	 * of the vnode?  E.g. if no execute permission is given
	 * on the vnode for the current user, maxprot probably
	 * should disallow PROT_EXEC also?  This is different
	 * from the write access as this would be a per vnode
	 * test as opposed to a per fd test for writability.
	 */

	/*
	 * Verify that the specified protections are not greater than
	 * the maximum allowable protections.  Also test to make sure
	 * that the file descriptor does allows for read access since
	 * "write only" mappings are hard to do since normally we do
	 * the read from the file before the page can be written.
	 */
	if (((maxprot & uprot) != uprot) || (fp->f_flag & FREAD) == 0) {
		RELEASEF(fd);
		return (set_errno(EACCES));
	}

	/*
	 * See if this is an "old mmap call".  If so, remember this
	 * fact and convert the flags value given to mmap to indicate
	 * the specified address in the system call must be used.
	 * _MAP_NEW is turned set by all new uses of mmap.
	 */

#ifdef sparc
	old_mmap = (flags & _MAP_NEW) == 0;
	if (old_mmap)
		flags |= MAP_FIXED;
#endif
	flags &= ~_MAP_NEW;

	/*
	 * If the user specified an address, do some simple checks here
	 */
	if ((flags & MAP_FIXED) != 0) {
		/*
		 * Use the user address.  First verify that
		 * the address to be used is page aligned.
		 * Then make some simple bounds checks.
		 */
		if (((int)addr & PAGEOFFSET) != 0) {
			RELEASEF(fd);
			return (set_errno(EINVAL));
		}
		if (valid_usr_range(addr, len) == 0) {
			RELEASEF(fd);
			return (set_errno(ENOMEM));
		}
	}

	/*
	 * Ok, now let the vnode map routine do its thing to set things up.
	 */
	if (error = VOP_MAP(vp, (offset_t)pos, as, &addr, len,
	    uprot, maxprot, flags, fp->f_cred)) {
		RELEASEF(fd);
		return (set_errno(error));
	}

	RELEASEF(fd);
	return ((int)addr);	/* return starting address */
}

int
munmap(caddr_t addr, size_t len)
{
	register struct proc *p = ttoproc(curthread);

	if (((int)addr & PAGEOFFSET) != 0 || ((int)len <= 0))
		return (set_errno(EINVAL));

	if (valid_usr_range(addr, len) == 0)
		return (set_errno(EINVAL));

	if (as_unmap(p->p_as, addr, len) != 0)
		return (set_errno(EINVAL));

	return (0);
}

int
mprotect(caddr_t addr, size_t len, int prot)
{
	register u_int uprot = prot | PROT_USER;
	register int error;

	if (((int)addr & PAGEOFFSET) != 0)
		return (set_errno(EINVAL));

	if (len == 0)
		return (set_errno(EINVAL));

	if (valid_usr_range(addr, len) == 0)
		return (set_errno(ENOMEM));

	error = as_setprot(ttoproc(curthread)->p_as, addr, len, uprot);
	if (error)
		return (set_errno(error));
	return (0);
}

#define	MC_CACHE	128			/* internal result buffer */
#define	MC_QUANTUM	(MC_CACHE * PAGESIZE)	/* addresses covered in loop */

int
mincore(caddr_t addr, size_t len, char *vecp)
{
	register caddr_t ea;			/* end address of loop */
	register struct as *as;			/* address space */
	u_int rl;				/* inner result length */
	char vec[MC_CACHE];			/* local vector cache */
	register int error;

	/*
	 * Validate form of address parameters.
	 */
	if (((int)addr & PAGEOFFSET) != 0 || ((int)len <= 0))
		return (set_errno(EINVAL));

	if (valid_usr_range(addr, len) == 0)
		return (set_errno(ENOMEM));

	/*
	 * Loop over subranges of interval [addr : addr + len), recovering
	 * results internally and then copying them out to caller.  Subrange
	 * is based on the size of MC_CACHE, defined above.
	 */
	as = ttoproc(curthread)->p_as;
	for (ea = addr + len; addr < ea; addr += MC_QUANTUM) {
		error = as_incore(as, addr,
		    (u_int)MIN(MC_QUANTUM, ea - addr), vec, &rl);
		if (rl != 0) {
			rl = (rl + PAGESIZE - 1) / PAGESIZE;
			if (copyout(vec, vecp, rl) != 0)
				return (set_errno(EFAULT));
			vecp += rl;
		}
		if (error != 0)
			return (set_errno(ENOMEM));
	}
	return (0);
}
