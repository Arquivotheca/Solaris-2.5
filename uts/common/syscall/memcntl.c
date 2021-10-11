/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/
/*	Copyright (c) 1994 Sun Microsystems, Inc. */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/


#ident	"@(#)memcntl.c	1.21	95/01/18 SMI" /* from SVR4.0 1.34 */

#include <sys/types.h>
#include <sys/bitmap.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/mman.h>
#include <sys/tuneable.h>
#include <sys/cmn_err.h>
#include <sys/cred.h>
#include <sys/vmsystm.h>
#include <sys/debug.h>

#include <vm/as.h>
#include <vm/seg.h>

#ifdef __STDC__
static void mem_unlock(struct as *, caddr_t, int, caddr_t, u_long *,
	size_t, size_t);
#else
static void mem_unlock();
#endif


/*
 * Memory control operations
 */

int
memcntl(caddr_t addr, size_t len, int cmd, caddr_t arg, int attr, int mask)
{
	register struct seg *seg;		/* working segment */
	register u_int	rlen = 0;		/* rounded as length */
	struct	 as  *as_pp = ttoproc(curthread)->p_as;
	register ulong	 *mlock_map; 		/* pointer to bitmap used */
						/* to represent the locked */
						/* pages. */
	caddr_t	raddr;				/* rounded address counter */
	size_t  mlock_size;			/* size of bitmap */
	size_t inx;
	size_t npages;
	int error = 0;
	faultcode_t fc;

	if (mask)
		return (set_errno(EINVAL));
	if ((cmd == MC_LOCKAS) || (cmd == MC_UNLOCKAS)) {
		if ((addr != 0) || (len != 0)) {
			return (set_errno(EINVAL));
		}
	} else {
		if (((int)addr & PAGEOFFSET) != 0)
			return (set_errno(EINVAL));
		if (valid_usr_range(addr, len) == 0)
			return (set_errno(ENOMEM));
	}

	if ((VALID_ATTR & attr) != attr)
		return (set_errno(EINVAL));

	if ((attr & SHARED) && (attr & PRIVATE))
		return (set_errno(EINVAL));

	if (((cmd == MC_LOCKAS) || (cmd == MC_LOCK) ||
	    (cmd == MC_UNLOCKAS) || (cmd == MC_UNLOCK)) &&
	    (!suser(CRED())))
		return (set_errno(EPERM));

	if (attr)
		attr |= PROT_USER;

	switch (cmd) {
	case MC_SYNC:
		if ((int)arg & ~(MS_ASYNC|MS_INVALIDATE))
			error = set_errno(EINVAL);
		else {
			error = as_ctl(as_pp, addr, len,
			    cmd, attr, arg, (ulong *)NULL, (size_t)NULL);
			if (error)
				(void) set_errno(error);
		}
		return (error);
	case MC_LOCKAS:
		if ((int)arg & ~(MCL_FUTURE|MCL_CURRENT) || (int)arg == 0)
			return (set_errno(EINVAL));

		AS_LOCK_ENTER(as_pp, &as_pp->a_lock, RW_READER);
		seg = AS_SEGP(as_pp, as_pp->a_segs);
		if (seg == NULL) {
			AS_LOCK_EXIT(as_pp, &as_pp->a_lock);
			return (0);
		}
		do {
			raddr = (caddr_t)((u_int)seg->s_base & PAGEMASK);
			rlen += (((u_int)(seg->s_base + seg->s_size) +
				PAGEOFFSET) & PAGEMASK) - (u_int)raddr;
		} while ((seg = AS_SEGP(as_pp, seg->s_next)) != NULL);
		AS_LOCK_EXIT(as_pp, &as_pp->a_lock);

		break;
	case MC_LOCK:
		/*
		 * Normalize addresses and lengths
		 */
		raddr = (caddr_t)((u_int)addr & PAGEMASK);
		rlen  = (((u_int)(addr + len) + PAGEOFFSET) & PAGEMASK) -
				(u_int)raddr;
		break;
	case MC_UNLOCKAS:
	case MC_UNLOCK:
		mlock_map = NULL;
		mlock_size = NULL;
		break;
	case MC_ADVISE:
		switch ((int)arg) {
		case MADV_WILLNEED:
			fc = as_faulta(as_pp, addr, len);
			if (fc) {
				if (FC_CODE(fc) == FC_OBJERR)
					error = set_errno(FC_ERRNO(fc));
				else
					error = set_errno(EINVAL);
				return (error);
			}
			break;

		case MADV_DONTNEED:
			/*
			 * For now, don't need is turned into an as_ctl(MC_SYNC)
			 * operation flagged for async invalidate.
			 */
			error = as_ctl(as_pp, addr, len, MC_SYNC, attr,
			    (caddr_t)(MS_ASYNC | MS_INVALIDATE), (ulong *)NULL,
			    (size_t)NULL);
			if (error)
				(void) set_errno(error);
			return (error);

		default:
			error = as_ctl(as_pp, addr, len, cmd, attr,
			    arg, (ulong *)NULL, (size_t)NULL);
			if (error)
				(void) set_errno(error);
			return (error);
		}
		break;
	default:
		return (set_errno(EINVAL));
	}

	if ((cmd == MC_LOCK) || (cmd == MC_LOCKAS)) {
		mlock_size = BT_BITOUL(btoc(rlen));
		mlock_map = (ulong *)kmem_zalloc((u_int)mlock_size *
		    sizeof (ulong), KM_SLEEP);
	}

	error = as_ctl(as_pp, addr, len, cmd, attr, arg, mlock_map, 0);
	if (cmd == MC_LOCK || cmd == MC_LOCKAS) {
		if (error) {
			if (cmd == MC_LOCKAS) {
				inx = 0;
				npages = 0;
				AS_LOCK_ENTER(as_pp, &as_pp->a_lock, RW_READER);
				for (seg = AS_SEGP(as_pp, as_pp->a_segs);
				    seg != NULL;
				    seg = AS_SEGP(as_pp, seg->s_next)) {
					raddr = (caddr_t)((u_int)seg->s_base
							& PAGEMASK);
					npages += seg_pages(seg);
					AS_LOCK_EXIT(as_pp, &as_pp->a_lock);
					mem_unlock(as_pp, raddr, attr, arg,
					    mlock_map, inx, npages);
					AS_LOCK_ENTER(as_pp, &as_pp->a_lock,
					    RW_READER);
					inx += seg_pages(seg);
				}
				AS_LOCK_EXIT(as_pp, &as_pp->a_lock);
			} else  /* MC_LOCK */
				mem_unlock(as_pp, raddr, attr, arg,
				    mlock_map, 0, btoc(rlen));
		}
		kmem_free((caddr_t)mlock_map, mlock_size * sizeof (ulong));
	}
	if (error)
		(void) set_errno(error);
	return (error);
}

static void
mem_unlock(struct as *as, caddr_t addr, int attr, caddr_t arg,
		ulong *bitmap, size_t position, size_t nbits)
{
	caddr_t	range_start;
	size_t	pos1, pos2;
	u_int	size;

	pos1 = position;

	while (bt_range(bitmap, &pos1, &pos2, nbits)) {
		size = ctob((pos2 - pos1) + 1);
		range_start = addr + ctob(pos1);
		(void) as_ctl(as, range_start, size, MC_UNLOCK,
			attr, arg, (ulong *)NULL, (size_t)NULL);
		pos1 = pos2 + 1;
	}
}
