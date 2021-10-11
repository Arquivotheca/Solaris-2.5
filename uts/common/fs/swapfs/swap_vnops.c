/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ident	"@(#)swap_vnops.c	1.27	93/12/15 SMI"
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/cred.h>
#include <sys/errno.h>
#include <sys/vnode.h>
#include <sys/cmn_err.h>
#include <sys/swap.h>
#include <sys/mman.h>
#include <sys/vmsystm.h>
#include <sys/vtrace.h>
#include <sys/debug.h>
#include <sys/sysmacros.h>
#include <sys/vm.h>

#include <sys/fs/swapnode.h>

#include <vm/seg.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <fs/fs_subr.h>

/*
 * Define the routines within this file.
 */
static int	swap_putpage();
static int	swap_getpage();
static int	swap_getapage();
static void	swap_inactive(struct vnode *, struct cred *);
static int 	swap_putapage(struct vnode *, page_t *, u_int *, u_int *, int,
		struct cred *);

struct vnodeops swap_vnodeops = {
	fs_nosys,
	fs_nosys,
	fs_nosys,	/* read */
	fs_nosys,	/* write */
	fs_nosys,	/* ioctl */
	fs_nosys,
	fs_nosys,	/* getattr */
	fs_nosys,	/* setattr */
	fs_nosys,	/* access */
	fs_nosys,	/* lookup */
	fs_nosys,	/* create */
	fs_nosys,	/* remove */
	fs_nosys,	/* link */
	fs_nosys,	/* rename */
	fs_nosys,	/* mkdir */
	fs_nosys,	/* rmdir */
	fs_nosys,	/* readdir */
	fs_nosys,	/* symlink */
	fs_nosys,	/* readlink */
	fs_nosys,	/* fsync */
	swap_inactive,	/* inactive */
	fs_nosys,	/* fid */
	fs_rwlock,	/* rwlock */
	fs_rwunlock,	/* rwunlock */
	fs_nosys, 	/* seek */
	fs_nosys,	/* cmp */
	fs_nosys,	/* frlock */
	fs_nosys,	/* space */
	fs_nosys,	/* realvp */
	swap_getpage,	/* getpages */
	swap_putpage,	/* putpages */
	fs_nosys_map,	/* map */
	fs_nosys_addmap,	/* addmap */
	fs_nosys,	/* delmap */
	fs_nosys_poll,	/* poll */
	fs_nosys,	/* dump */
	fs_nosys,	/* pathconf */
	fs_nosys,	/* pageio */
	fs_nosys,	/* dumpctl */
	fs_dispose,	/* dispose */
	fs_nosys,	/* setsecattr */
	fs_nosys	/* getsecattr */
};


/* ARGSUSED */
static void
swap_inactive(
	struct vnode *vp,
	struct cred *cr)
{
	SWAPFS_PRINT(SWAP_VOPS, "swap_inactive: vp %x\n", vp, 0, 0, 0, 0);
}

/*
 * Return all the pages from [off..off+len] in given file
 */
static int
swap_getpage(
	struct vnode *vp,
	offset_t off,
	u_int len,
	u_int *protp,
	page_t *pl[],
	u_int plsz,
	struct seg *seg,
	caddr_t addr,
	enum seg_rw rw,
	struct cred *cr)
{
	int err;

	SWAPFS_PRINT(SWAP_VOPS, "swap_getpage: vp %x, off %x %x, len %x\n",
		vp, ((off_t *)&off)[0], ((off_t *)&off)[1], len, 0);

	TRACE_3(TR_FAC_SWAPFS, TR_SWAPFS_GETPAGE,
		"swapfs getpage:vp %x off %x len %d", vp, off, len);
	if (len <= PAGESIZE)
		err = swap_getapage(vp, (u_int)off, len, protp, pl, plsz,
		    seg, addr, rw, cr);
	else
		err = pvn_getpages(swap_getapage, vp, (u_int)off, len,
		    protp, pl, plsz, seg, addr, rw, cr);

	return (err);
}

/*
 * Called from pvn_getpages or swap_getpage to get a particular page.
 */
/*ARGSUSED*/
static int
swap_getapage(
	struct vnode *vp,
	register u_int off,
	u_int len,
	u_int *protp,
	page_t *pl[],
	u_int plsz,
	struct seg *seg,
	caddr_t addr,
	enum seg_rw rw,
	struct cred *cr)
{
	struct page *pp;
	int flags;
	int err = 0;
	struct vnode *pvp;
	u_int poff;

	SWAPFS_PRINT(SWAP_VOPS, "swap_getapage: vp %x, off %x, len %x\n",
		vp, off, len, 0, 0);

	if (protp != NULL)
		*protp = PROT_ALL;
again:
	if (pp = page_lookup(vp, off, rw == S_CREATE ? SE_EXCL : SE_SHARED)) {
		if (pl) {
			pl[0] = pp;
			pl[1] = NULL;
		} else {
			page_unlock(pp);
		}
	} else {
		pp = page_create_va(vp, off, PAGESIZE, PG_WAIT | PG_EXCL,
			seg->s_as, addr);
		/*
		 * Someone raced in and created the page after we did the
		 * lookup but before we did the create, so go back and
		 * try to look it up again.
		 */
		if (pp == NULL)
			goto again;
		if (rw != S_CREATE) {
			err = swap_getphysname(vp, off, &pvp, &poff);
			if (!err && pvp) {
				flags = (pl == NULL ? B_ASYNC|B_READ : B_READ);
				err = VOP_PAGEIO(pvp, pp, poff, PAGESIZE,
				    flags, cr);
				if (flags & B_ASYNC)
					pp = NULL;
			}
		}
		if (err && pp)
			pvn_read_done(pp, B_ERROR);
		if (!err && pl)
			pvn_plist_init(pp, pl, plsz, off, PAGESIZE, rw);
	}
	TRACE_3(TR_FAC_SWAPFS, TR_SWAPFS_GETAPAGE,
		"swapfs getapage:pp %x vp %x off %x", pp, vp, off);
	return (err);

}

/* Async putpage klustering stuff */
int sw_pending_size;
extern int klustsize;
extern struct async_reqs *sw_getreq();
extern void sw_putreq(struct async_reqs *);
extern void sw_putbackreq(struct async_reqs *);
extern struct async_reqs *sw_getfree();
extern void sw_putfree(struct async_reqs *);

static int swap_putpagecnt, swap_pagespushed;
static int swap_otherfail, swap_otherpages;
static int swap_klustfail, swap_klustpages;
static int swap_getiofail, swap_getiopages;

/*
 * Flags are composed of {B_INVAL, B_DIRTY B_FREE, B_DONTNEED}.
 * If len == 0, do from off to EOF.
 */
static int swap_nopage = 0;	/* Don't do swap_putpage's if set */

/* ARGSUSED */
static int
swap_putpage(
	register struct vnode *vp,
	offset_t off,
	u_int len,
	int flags,
	struct cred *cr)
{
	register page_t *pp;
	u_int io_off, io_len = 0;
	int err = 0;
	struct async_reqs *arg;
	extern int pushes;

	if (swap_nopage)
		return (0);

	ASSERT(vp->v_count != 0);

	SWAPFS_PRINT(SWAP_VOPS,
		"swap_putpage: vp %x, off %x %x, len %x, flags %x\n",
		vp, ((off_t *)&off)[0], ((off_t *)&off)[1], len, flags);
	TRACE_3(TR_FAC_SWAPFS, TR_SWAPFS_PUTPAGE,
		"swapfs putpage:vp %x off %x len %d", vp, off, len);

	if (vp->v_flag & VNOMAP)
		return (ENOSYS);

	if (vp->v_pages == NULL)
		return (0);

	if (len == 0) {
		if (curproc == proc_pageout)
			cmn_err(CE_PANIC, "swapfs: pageout can't block");

		/* Search the entire vp list for pages >= off. */
		err = pvn_vplist_dirty(vp, (u_int)off, swap_putapage,
		    flags, cr);
	} else {
		register u_int eoff;

		/*
		 * Loop over all offsets in the range [off...off + len]
		 * looking for pages to deal with.
		 */
		eoff = (u_int)off + len;
		for (io_off = (u_int)off; io_off < eoff; io_off += io_len) {
			/*
			 * If we run out of the async req slot, put the page
			 * now instead of queuing.
			 */
			if (flags == (B_ASYNC | B_FREE) &&
			    sw_pending_size < klustsize &&
			    (arg = sw_getfree())) {
				/*
				 * If we are clustering, we should allow
				 * pageout to feed us more pages because # of
				 * pushes is limited by # of I/Os, and one
				 * cluster is considered to be one I/O.
				 */
				pushes--;

				arg->a_vp = vp;
				arg->a_off = io_off;
				arg->a_len = PAGESIZE;
				arg->a_flags = B_ASYNC | B_FREE;
				arg->a_cred = kcred;
				sw_putreq(arg);
				io_len = PAGESIZE;
				continue;
			}
			/*
			 * If we are not invalidating pages, use the
			 * routine page_lookup_nowait() to prevent
			 * reclaiming them from the free list.
			 */
			if ((flags & B_INVAL) ||
			    (flags & (B_ASYNC | B_FREE)) == B_FREE)
				pp = page_lookup(vp, io_off, SE_EXCL);
			else
				pp = page_lookup_nowait(vp, io_off,
					(flags & B_FREE) ? SE_EXCL : SE_SHARED);

			if (pp == NULL || pvn_getdirty(pp, flags) == 0)
				io_len = PAGESIZE;
			else {
				err = swap_putapage(vp, pp, &io_off, &io_len,
				    flags, cr);
				if (err != 0)
					break;
			}
		}
	}
	/* If invalidating, verify all pages on vnode list are gone. */
	if (err == 0 && off == 0 && len == 0 &&
	    (flags & B_INVAL) && vp->v_pages != NULL) {
		cmn_err(CE_WARN,
		    "swap_putpage: B_INVAL, pages not gone");
	}
	return (err);
}

/*
 * Write out a single page.
 * For swapfs this means choose a physical swap slot and write the page
 * out using VOP_PAGEIO.
 * In the (B_ASYNC | B_FREE) case we try to find a bunch of other dirty
 * swapfs pages, a bunch of contiguous swap slots and then write them
 * all out in one clustered i/o.
 */
/*ARGSUSED*/
static int
swap_putapage(
	struct vnode *vp,
	page_t *pp,
	u_int *offp,
	u_int *lenp,
	int flags,
	struct cred *cr)
{
	int err;
	struct vnode *pvp;
	u_int poff, off;
	u_int doff, dlen;
	u_int klsz = 0, klstart = 0;
	struct vnode *klvp = NULL;
	page_t *pplist;
	se_t se;
	struct async_reqs *arg;
	int swap_klustsize;

	/*
	 * This check is added for callers who access swap_putpage with len = 0.
	 * swap_putpage calls swap_putapage page-by-page via pvn_vplist_dirty.
	 * And it's necessary to do the same queuing if users have the same
	 * B_ASYNC|B_FREE flags on.
	 */
	if (flags == (B_ASYNC | B_FREE) &&
	    sw_pending_size < klustsize && (arg = sw_getfree())) {

		hat_setmod(pp);
		page_io_unlock(pp);
		page_unlock(pp);

		arg->a_vp = vp;
		arg->a_off = pp->p_offset;
		arg->a_len = PAGESIZE;
		arg->a_flags = B_ASYNC | B_FREE;
		arg->a_cred = kcred;
		sw_putreq(arg);

		return (0);
	}

	SWAPFS_PRINT(SWAP_PUTP,
		"swap_putapage: pp %x, vp %x, off %x, flags %x\n",
		pp, vp, pp->p_offset, flags, 0);

	ASSERT(se_assert(&pp->p_selock));

	off = pp->p_offset;

	doff = off;
	dlen = PAGESIZE;

	if (err = swap_newphysname(vp, off, &doff, &dlen, &pvp, &poff)) {
		err = (flags == (B_ASYNC | B_FREE) ? ENOMEM : 0);
		hat_setmod(pp);
		page_io_unlock(pp);
		page_unlock(pp);
		goto out;
	}

	klvp = pvp;
	klstart = poff;
	pplist = pp;
	/*
	 * If this is ASYNC | FREE and we've accumulated a bunch of such
	 * pending requests, kluster.
	 */
	if (flags == (B_ASYNC | B_FREE))
		swap_klustsize = klustsize;
	else
		swap_klustsize = PAGESIZE;
	se = (flags & B_FREE ? SE_EXCL : SE_SHARED);
	klsz = PAGESIZE;
	while (klsz < swap_klustsize) {
		if ((arg = sw_getreq()) == NULL) {
			swap_getiofail++;
			swap_getiopages += btop(klsz);
			break;
		}
		ASSERT(arg->a_vp->v_op == &swap_vnodeops);
		vp = arg->a_vp;
		off = (u_int) arg->a_off;

		if ((pp = page_lookup_nowait(vp, off, se)) == NULL) {
			swap_otherfail++;
			swap_otherpages += btop(klsz);
			sw_putfree(arg);
			break;
		}
		if (pvn_getdirty(pp, flags | B_DELWRI) == 0) {
			sw_putfree(arg);
			continue;
		}
		/* Get new physical backing store for the page */
		doff = off;
		dlen = PAGESIZE;
		if (err = swap_newphysname(vp, off,
		    &doff, &dlen, &pvp, &poff)) {
			swap_otherfail++;
			swap_otherpages += btop(klsz);
			hat_setmod(pp);
			page_io_unlock(pp);
			page_unlock(pp);
			sw_putbackreq(arg);
			break;
		}
		/* Try to cluster new physical name with previous ones */
		if (klvp == pvp && poff == klstart + klsz) {
			klsz += PAGESIZE;
			page_add(&pplist, pp);
			pplist = pplist->p_next;
			sw_putfree(arg);
		} else if (klvp == pvp && poff == klstart - PAGESIZE) {
			klsz += PAGESIZE;
			klstart -= PAGESIZE;
			page_add(&pplist, pp);
			sw_putfree(arg);
		} else {
			swap_klustfail++;
			swap_klustpages += btop(klsz);
			hat_setmod(pp);
			page_io_unlock(pp);
			page_unlock(pp);
			sw_putbackreq(arg);
			break;
		}
	}

	err = VOP_PAGEIO(klvp, pplist, klstart, klsz, B_WRITE | flags, cr);

	if ((flags & B_ASYNC) == 0)
		pvn_write_done(pp, ((err) ? B_ERROR : 0) | B_WRITE | flags);

	/* Statistics */
	if (!err) {
		swap_putpagecnt++;
		swap_pagespushed += btop(klsz);
	}
out:
	TRACE_5(TR_FAC_SWAPFS, TR_SWAPFS_PUTAPAGE,
		"swapfs putapage:vp %x off %x klvp %x, klstart %x, klsz %x",
		vp, *offp, klvp, klstart, klsz);
	if (err && err != ENOMEM)
		cmn_err(CE_WARN, "swapfs_putapage: err %d\n", err);
	if (lenp)
		*lenp = PAGESIZE;
	return (err);
}
