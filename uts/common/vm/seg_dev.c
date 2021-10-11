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
 * 	(c) 1986, 1987, 1988, 1989, 1990, 1991  Sun Microsystems, Inc
 * 	(c) 1983, 1984, 1985, 1986, 1987, 1988, 1989  AT&T.
 *		All rights reserved.
 *
 */

#ident	"@(#)seg_dev.c	1.68	95/10/24 SMI"
/*	From:	SVr4.0	"kernel:vm/seg_dev.c	1.23"		*/

/*
 * VM - segment of a mapped device.
 *
 * This segment driver is used when mapping character special devices.
 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/sysmacros.h>
#include <sys/vmsystm.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/vnode.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/debug.h>

#include <vm/page.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/devpage.h>
#include <vm/seg_dev.h>
#include <vm/vpage.h>

#include <sys/sunddi.h>
#include <sys/fs/snode.h>

/*
 * Private seg op routines.
 */

static int	segdev_dup(struct seg *, struct seg *);
static int	segdev_unmap(struct seg *, caddr_t, u_int);
static void	segdev_free(struct seg *);
static faultcode_t segdev_fault(struct hat *, struct seg *, caddr_t, u_int,
		    enum fault_type, enum seg_rw);
static faultcode_t segdev_faulta(struct seg *, caddr_t);
static int	segdev_setprot(struct seg *, caddr_t, u_int, u_int);
static int	segdev_checkprot(struct seg *, caddr_t, u_int, u_int);
static void	segdev_badop(void);
static int	segdev_sync(struct seg *, caddr_t, u_int, int, u_int);
static int	segdev_incore(struct seg *, caddr_t, u_int, char *);
static int	segdev_lockop(struct seg *, caddr_t, u_int, int, int,
		    ulong *, size_t);
static int	segdev_getprot(struct seg *, caddr_t, u_int, u_int *);
static off_t	segdev_getoffset(struct seg *, caddr_t);
static int	segdev_gettype(struct seg *, caddr_t);
static int	segdev_getvp(struct seg *, caddr_t, struct vnode **);
static int	segdev_advise(struct seg *, caddr_t, u_int, int);
static void	segdev_dump(struct seg *);

/*
 * XXX	this struct is used by rootnex_map_fault to identify
 *	the segment it has been passed. So if you make it
 *	"static" you'll need to fix rootnex_map_fault.
 */
struct seg_ops segdev_ops = {
	segdev_dup,
	segdev_unmap,
	segdev_free,
	segdev_fault,
	segdev_faulta,
	segdev_setprot,
	segdev_checkprot,
	(int (*)())segdev_badop,	/* kluster */
	(u_int (*)(struct seg *))NULL,	/* swapout */
	segdev_sync,			/* sync */
	segdev_incore,
	segdev_lockop,			/* lockop */
	segdev_getprot,
	segdev_getoffset,
	segdev_gettype,
	segdev_getvp,
	segdev_advise,
	segdev_dump,
};

#define	vpgtob(n)	((n) * sizeof (struct vpage))	/* For brevity */

#define	VTOCVP(vp)	(VTOS(vp)->s_commonvp)	/* we "know" it's an snode */

/*
 * Private support routines
 */

static struct segdev_data *sdp_alloc(void);

static void segdev_softunlock(struct hat *, struct seg *, caddr_t,
    u_int, enum seg_rw, struct cred *);

static int segdev_faultpage(struct hat *, struct seg *, caddr_t,
    struct vpage *, enum fault_type, enum seg_rw, struct cred *);

/*
 * Create a device segment.
 */
int
segdev_create(struct seg *seg, void *argsp)
{
	register struct segdev_data *sdp;
	register struct segdev_crargs *a = (struct segdev_crargs *)argsp;
	register int error;

	/*
	 * Since the address space is "write" locked, we
	 * don't need the segment lock to protect "segdev" data.
	 */
	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * The following call to hat_map presumes that translation
	 * resources are set up by the system MMU. This may cause
	 * problems when the resources are allocated/managed by the
	 * device's MMU or an MMU other than the system MMU. For now
	 * hat_map is no-op and not implemented by the Sun MMU, SRMMU and
	 * X86 MMU drivers and should not pose any problems.
	 */
	error = hat_map(seg->s_as->a_hat, seg->s_as, seg->s_base,
	    seg->s_size, HAT_MAP);
	if (error != 0)
		return (error);

	/* TODO: try concatenation */

	if ((sdp = sdp_alloc()) == NULL)
		return (ENOMEM);

	sdp->mapfunc = a->mapfunc;
	sdp->offset = a->offset;
	sdp->prot = a->prot;
	sdp->maxprot = a->maxprot;
	sdp->hat_flags = a->hat_flags;
	sdp->pageprot = 0;
	sdp->vpage = NULL;

	/*
	 * Hold shadow vnode -- segdev only deals with
	 * character (VCHR) devices. We use the common
	 * vp to hang devpages on.
	 */
	sdp->vp = specfind(a->dev, VCHR);
	ASSERT(sdp->vp != NULL);

	seg->s_ops = &segdev_ops;
	seg->s_data = sdp;

	/*
	 * Inform the vnode of the new mapping.
	 */
	error = VOP_ADDMAP(VTOCVP(sdp->vp), (offset_t)sdp->offset,
	    seg->s_as, seg->s_base, seg->s_size,
	    sdp->prot, sdp->maxprot, MAP_SHARED, CRED());

	if (error != 0)
		hat_unmap(seg->s_as, seg->s_base, seg->s_size, HAT_UNMAP);

	return (error);
}

static struct segdev_data *
sdp_alloc(void)
{
	register struct segdev_data *sdp;

	sdp = (struct segdev_data *)
		kmem_zalloc(sizeof (struct segdev_data), KM_SLEEP);
	mutex_init(&sdp->lock, "sdp.lock", MUTEX_DEFAULT, DEFAULT_WT);
	return (sdp);
}

/*
 * Duplicate seg and return new segment in newseg.
 */
static int
segdev_dup(struct seg *seg, struct seg *newseg)
{
	register struct segdev_data *sdp = (struct segdev_data *)seg->s_data;
	register struct segdev_data *newsdp;

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	if ((newsdp = sdp_alloc()) == NULL)
		return (ENOMEM);

	newseg->s_ops = seg->s_ops;
	newseg->s_data = (void *)newsdp;

	mutex_enter(&sdp->lock);
	VN_HOLD(sdp->vp);
	newsdp->vp 	= sdp->vp;
	newsdp->mapfunc = sdp->mapfunc;
	newsdp->offset	= sdp->offset;
	newsdp->prot	= sdp->prot;
	newsdp->maxprot = sdp->maxprot;
	newsdp->pageprot = sdp->pageprot;
	newsdp->hat_flags = sdp->hat_flags;
	if (sdp->vpage != NULL) {
		register u_int nbytes = vpgtob(seg_pages(seg));
		/*
		 * Release the segment lock while allocating
		 * the vpage structure for the new segment
		 * since nobody can create it and our segment
		 * cannot be destroyed as the address space
		 * has a "read" lock.
		 */
		mutex_exit(&sdp->lock);
		newsdp->vpage = (struct vpage *) kmem_alloc(nbytes, KM_SLEEP);
		mutex_enter(&sdp->lock);
		bcopy((caddr_t)sdp->vpage, (caddr_t)newsdp->vpage, nbytes);
	} else
		newsdp->vpage = NULL;
	mutex_exit(&sdp->lock);

	/*
	 * Inform the common vnode of the new mapping.
	 */
	return (VOP_ADDMAP(VTOCVP(newsdp->vp),
	    (offset_t)newsdp->offset, newseg->s_as,
	    newseg->s_base, newseg->s_size, newsdp->prot,
	    newsdp->maxprot, MAP_SHARED, CRED()));
}

/*
 * Split a segment at addr for length len.
 */
/*ARGSUSED*/
static int
segdev_unmap(register struct seg *seg, register caddr_t addr, u_int len)
{
	register struct segdev_data *sdp = (struct segdev_data *)seg->s_data;
	register struct segdev_data *nsdp;
	register struct seg *nseg;
	register u_int	opages;		/* old segment size in pages */
	register u_int	npages;		/* new segment size in pages */
	register u_int	dpages;		/* pages being deleted (unmapped) */
	caddr_t nbase;
	u_int nsize;

	/*
	 * Since the address space is "write" locked, we
	 * don't need the segment lock to protect "segdev" data.
	 */
	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * Check for bad sizes
	 */
	if (addr < seg->s_base || addr + len > seg->s_base + seg->s_size ||
	    (len & PAGEOFFSET) || ((u_int)addr & PAGEOFFSET))
		cmn_err(CE_PANIC, "segdev_unmap");

	/*
	 * Unload any hardware translations in the range to be taken out.
	 */
	hat_unmap(seg->s_as, addr, len, HAT_UNMAP);

	/*
	 * Inform the vnode of the unmapping.
	 */
	ASSERT(sdp->vp != NULL);
	if (VOP_DELMAP(VTOCVP(sdp->vp),
	    (offset_t)sdp->offset + (addr - seg->s_base),
	    seg->s_as, addr, len, sdp->prot, sdp->maxprot,
	    MAP_SHARED, CRED()) != 0)
		cmn_err(CE_WARN, "segdev_unmap VOP_DELMAP failure");

	/*
	 * Check for entire segment
	 */
	if (addr == seg->s_base && len == seg->s_size) {
		seg_free(seg);
		return (0);
	}

	opages = seg_pages(seg);
	dpages = btop(len);
	npages = opages - dpages;

	/*
	 * Check for beginning of segment
	 */
	if (addr == seg->s_base) {
		if (sdp->vpage != NULL) {
			register u_int nbytes;
			register struct vpage *ovpage;

			ovpage = sdp->vpage;	/* keep pointer to vpage */

			nbytes = vpgtob(npages);
			sdp->vpage = (struct vpage *)
			    kmem_alloc(nbytes, KM_SLEEP);
			bcopy((caddr_t)&ovpage[dpages],
			    (caddr_t)sdp->vpage, nbytes);

			/* free up old vpage */
			kmem_free(ovpage, vpgtob(opages));
		}
		sdp->offset += len;

		seg->s_base += len;
		seg->s_size -= len;
		return (0);
	}

	/*
	 * Check for end of segment
	 */
	if (addr + len == seg->s_base + seg->s_size) {
		if (sdp->vpage != NULL) {
			register u_int nbytes;
			register struct vpage *ovpage;

			ovpage = sdp->vpage;	/* keep pointer to vpage */

			nbytes = vpgtob(npages);
			sdp->vpage = (struct vpage *)
			    kmem_alloc(nbytes, KM_SLEEP);
			bcopy((caddr_t)ovpage, (caddr_t)sdp->vpage, nbytes);

			/* free up old vpage */
			kmem_free(ovpage, vpgtob(opages));
		}
		seg->s_size -= len;
		return (0);
	}

	/*
	 * The section to go is in the middle of the segment,
	 * have to make it into two segments.  nseg is made for
	 * the high end while seg is cut down at the low end.
	 */
	nbase = addr + len;				/* new seg base */
	nsize = (seg->s_base + seg->s_size) - nbase;	/* new seg size */
	seg->s_size = addr - seg->s_base;		/* shrink old seg */
	nseg = seg_alloc(seg->s_as, nbase, nsize);
	if (nseg == NULL)
		cmn_err(CE_PANIC, "segdev_unmap seg_alloc");

	nseg->s_ops = seg->s_ops;
	nsdp = sdp_alloc();
	nseg->s_data = (void *)nsdp;
	nsdp->hat_flags = sdp->hat_flags;
	nsdp->pageprot = sdp->pageprot;
	nsdp->prot = sdp->prot;
	nsdp->maxprot = sdp->maxprot;
	nsdp->mapfunc = sdp->mapfunc;
	nsdp->offset = sdp->offset + nseg->s_base - seg->s_base;
	VN_HOLD(sdp->vp);	/* Hold vnode associated with the new seg */
	nsdp->vp = sdp->vp;

	if (sdp->vpage == NULL)
		nsdp->vpage = NULL;
	else {
		/* need to split vpage into two arrays */
		register u_int nbytes;
		register struct vpage *ovpage;

		ovpage = sdp->vpage;		/* keep pointer to vpage */

		npages = seg_pages(seg);	/* seg has shrunk */
		nbytes = vpgtob(npages);
		sdp->vpage = (struct vpage *)kmem_alloc(nbytes, KM_SLEEP);

		bcopy((caddr_t)ovpage, (caddr_t)sdp->vpage, nbytes);

		npages = seg_pages(nseg);
		nbytes = vpgtob(npages);
		nsdp->vpage = (struct vpage *)kmem_alloc(nbytes, KM_SLEEP);

		bcopy((caddr_t)&ovpage[opages - npages],
		    (caddr_t)nsdp->vpage, nbytes);

		/* free up old vpage */
		kmem_free(ovpage, vpgtob(opages));
	}

	return (0);
}

/*
 * Free a segment.
 */
static void
segdev_free(struct seg *seg)
{
	register struct segdev_data *sdp = (struct segdev_data *)seg->s_data;
	register u_int nbytes = vpgtob(seg_pages(seg));

	/*
	 * Since the address space is "write" locked, we
	 * don't need the segment lock to protect "segdev" data.
	 */
	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	VN_RELE(sdp->vp);
	if (sdp->vpage != NULL)
		kmem_free((caddr_t)sdp->vpage, nbytes);
	mutex_destroy(&sdp->lock);
	kmem_free((caddr_t)sdp, sizeof (*sdp));
}

/*
 * Do a F_SOFTUNLOCK call over the range requested.
 * The range must have already been F_SOFTLOCK'ed.
 * The segment lock should be held.
 */
static void
segdev_softunlock(
	struct hat *hat,		/* the hat */
	struct seg *seg,		/* seg_dev of interest */
	register caddr_t addr,		/* base address of range */
	u_int len,			/* number of bytes */
	enum seg_rw rw,			/* type of access at fault */
	struct cred *cr)		/* credentials */
{
	register struct segdev_data *sdp = (struct segdev_data *)seg->s_data;
	register u_int	offset;
	register struct devpage *dp;
	register caddr_t a;
	struct page *pl[2];

	offset = sdp->offset + (addr - seg->s_base);
	for (a = addr; a < addr + len; a += PAGESIZE) {
		(void) VOP_GETPAGE(VTOCVP(sdp->vp), (offset_t)offset, PAGESIZE,
		    (u_int *)NULL, pl, sizeof (pl) / sizeof (*pl),
		    seg, a, rw, cr);
		dp = (devpage_t *)pl[0];

		if (dp != NULL) {
			if (rw == S_WRITE)
				hat_setmod((struct page *)dp);
			if (rw != S_OTHER)
				hat_setref((struct page *)dp);
		}
		hat_unlock(hat, seg->s_as, a, PAGESIZE);

		offset += PAGESIZE;
	}
}

/*
 * Handle a single devpage.
 * Done in a separate routine so we can handle errors more easily.
 * This routine is called only from segdev_fault()
 * when looping over the range of addresses requested.  The
 * segment lock should be held.
 *
 * The basic algorithm here is:
 *		Find pfn from the driver's mmap function
 *		Load up the translation to the devpage
 *		return
 */
static int
segdev_faultpage(
	struct hat *hat,		/* the hat */
	struct seg *seg,		/* seg_dev of interest */
	caddr_t addr,			/* address in as */
	struct vpage *vpage,		/* pointer to vpage for seg, addr */
	enum fault_type type,		/* type of fault */
	enum seg_rw rw,			/* type of access at fault */
	struct cred *cr)		/* credentials */
{
	register struct segdev_data *sdp = (struct segdev_data *)seg->s_data;
	register struct devpage *dp;
	register u_int prot;
	register u_int pfnum;
	register u_int offset;
	register int err;
	u_int hat_flags;
	struct page *pl[2];
	dev_info_t *dip;
	int	pf_is_memory();

	/*
	 * Initialize protection value for this page.
	 * If we have per page protection values check it now.
	 */
	if (sdp->pageprot) {
		u_int protchk;

		switch (rw) {
		case S_READ:
			protchk = PROT_READ;
			break;
		case S_WRITE:
			protchk = PROT_WRITE;
			break;
		case S_EXEC:
			protchk = PROT_EXEC;
			break;
		case S_OTHER:
		default:
			protchk = PROT_READ | PROT_WRITE | PROT_EXEC;
			break;
		}

		prot = vpage->vp_prot;
		if ((prot & protchk) == 0)
			return (FC_PROT);	/* illegal access type */
	} else {
		prot = sdp->prot;
	}

	offset = sdp->offset + (addr - seg->s_base);

	pfnum = cdev_mmap(sdp->mapfunc, sdp->vp->v_rdev, offset, prot);
	if (pfnum == (u_int)-1)
		return (FC_MAKE_ERR(EFAULT));

	if (pf_is_memory(pfnum)) {
		hat_flags = ((type == F_SOFTLOCK) ? HAT_LOCK : HAT_LOAD);
		if (!(sdp->hat_flags & HAT_KMEM))
			hat_flags |= HAT_NOCONSIST;
		hat_devload(hat, seg->s_as, addr, NULL, pfnum, prot, hat_flags);
		return (0);
	}

	err = VOP_GETPAGE(VTOCVP(sdp->vp), (offset_t)offset, PAGESIZE,
	    (u_int *)NULL, pl, sizeof (pl) / sizeof (*pl), seg, addr, rw, cr);
	if (err)
		return (FC_MAKE_ERR(err));

	dp = (devpage_t *)pl[0];
	if (dp != NULL)
		hat_setref((struct page *)dp);

	dip = VTOS(VTOCVP(sdp->vp))->s_dip;
	ASSERT(dip);

	if (ddi_map_fault(dip, hat, seg, addr, dp, pfnum, prot,
	    (u_int)(type == F_SOFTLOCK)) != DDI_SUCCESS)
		return (FC_MAKE_ERR(EFAULT));

	return (0);
}

/*
 * This routine is called via a machine specific fault handling routine.
 * It is also called by software routines wishing to lock or unlock
 * a range of addresses.
 *
 * Here is the basic algorithm:
 *	If unlocking
 *		Call segdev_softunlock
 *		Return
 *	endif
 *	Checking and set up work
 *	Loop over all addresses requested
 *		Call segdev_faultpage to load up translations.
 *	endloop
 */
static faultcode_t
segdev_fault(
	struct hat *hat,		/* the hat */
	struct seg *seg,		/* the seg_dev of interest */
	register caddr_t addr,		/* the address of the fault */
	u_int len,			/* the length of the range */
	enum fault_type type,		/* type of fault */
	register enum seg_rw rw)	/* type of access at fault */
{
	register struct segdev_data *sdp = (struct segdev_data *)seg->s_data;
	register caddr_t a;
	register struct vpage *vpage;
	int err, page;
	struct cred *cr = CRED();

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	if (type == F_PROT) {
		/*
		 * Since the seg_dev driver does not implement copy-on-write,
		 * this means that a valid translation is already loaded,
		 * but we got an fault trying to access the device.
		 * Return an error here to prevent going in an endless
		 * loop reloading the same translation...
		 */
		return (FC_PROT);
	}

	/*
	 * First handle the easy stuff
	 */
	if (type == F_SOFTUNLOCK) {
		segdev_softunlock(hat, seg, addr, len, rw, cr);
		return (0);
	}

	/*
	 * If we have the same protections for the entire segment,
	 * insure that the access being attempted is legitimate.
	 */
	mutex_enter(&sdp->lock);
	if (sdp->pageprot == 0) {
		u_int protchk;

		switch (rw) {
		case S_READ:
			protchk = PROT_READ;
			break;
		case S_WRITE:
			protchk = PROT_WRITE;
			break;
		case S_EXEC:
			protchk = PROT_EXEC;
			break;
		case S_OTHER:
		default:
			protchk = PROT_READ | PROT_WRITE | PROT_EXEC;
			break;
		}

		if ((sdp->prot & protchk) == 0) {
			mutex_exit(&sdp->lock);
			return (FC_PROT);	/* illegal access type */
		}
	}

	page = seg_page(seg, addr);
	if (sdp->vpage == NULL)
		vpage = NULL;
	else
		vpage = &sdp->vpage[page];

	/* loop over the address range handling each fault */

	for (a = addr; a < addr + len; a += PAGESIZE) {
		err = segdev_faultpage(hat, seg, a, vpage, type, rw, cr);
		if (err) {
			if (type == F_SOFTLOCK && a > addr)
				segdev_softunlock(hat, seg, addr,
				    (u_int)(a - addr), S_OTHER, cr);
			mutex_exit(&sdp->lock);
			return (err); /* FC_MAKE_ERR done by segdev_faultpage */
		}
		if (vpage != NULL)
			vpage++;
	}
	mutex_exit(&sdp->lock);
	return (0);
}

/*
 * Asynchronous page fault.  We simply do nothing since this
 * entry point is not supposed to load up the translation.
 */
/*ARGSUSED*/
static faultcode_t
segdev_faulta(struct seg *seg, caddr_t addr)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	return (0);
}

static int
segdev_setprot(
	register struct seg *seg,
	register caddr_t addr,
	register u_int len,
	register u_int prot)
{
	register struct segdev_data *sdp = (struct segdev_data *)seg->s_data;
	register struct vpage *vp, *evp;

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	if ((sdp->maxprot & prot) != prot)
		return (EACCES);		/* violated maxprot */

	mutex_enter(&sdp->lock);
	if (addr == seg->s_base && len == seg->s_size && sdp->pageprot == 0) {
		if (sdp->prot == prot) {
			mutex_exit(&sdp->lock);
			return (0);			/* all done */
		}
		sdp->prot = (u_char)prot;
	} else {
		sdp->pageprot = 1;
		if (sdp->vpage == NULL) {
			/*
			 * First time through setting per page permissions,
			 * initialize all the vpage structures to prot
			 */
			sdp->vpage = (struct vpage *)
			    kmem_zalloc(vpgtob(seg_pages(seg)), KM_SLEEP);
			evp = &sdp->vpage[seg_pages(seg)];
			for (vp = sdp->vpage; vp < evp; vp++)
				vp->vp_prot = sdp->prot;
		}
		/*
		 * Now go change the needed vpages protections.
		 */
		evp = &sdp->vpage[seg_page(seg, addr + len)];
		for (vp = &sdp->vpage[seg_page(seg, addr)]; vp < evp; vp++)
			vp->vp_prot = prot;
	}
	mutex_exit(&sdp->lock);

	if ((prot & ~PROT_USER) == PROT_NONE)
		hat_unload(seg->s_as, addr, len, HAT_UNLOAD);
	else
		hat_chgprot(seg->s_as, addr, len, prot);
	return (0);
}

static int
segdev_checkprot(
	register struct seg *seg,
	register caddr_t addr,
	register u_int len,
	register u_int prot)
{
	struct segdev_data *sdp = (struct segdev_data *)seg->s_data;
	register struct vpage *vp, *evp;

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * If segment protection can be used, simply check against them
	 */
	mutex_enter(&sdp->lock);
	if (sdp->pageprot == 0) {
		register int err;

		err = ((sdp->prot & prot) != prot) ? EACCES : 0;
		mutex_exit(&sdp->lock);
		return (err);
	}

	/*
	 * Have to check down to the vpage level
	 */
	evp = &sdp->vpage[seg_page(seg, addr + len)];
	for (vp = &sdp->vpage[seg_page(seg, addr)]; vp < evp; vp++) {
		if ((vp->vp_prot & prot) != prot) {
			mutex_exit(&sdp->lock);
			return (EACCES);
		}
	}
	mutex_exit(&sdp->lock);
	return (0);
}

static int
segdev_getprot(
	register struct seg *seg,
	register caddr_t addr,
	register u_int len,
	register u_int *protv)
{
	struct segdev_data *sdp = (struct segdev_data *)seg->s_data;
	register u_int pgno;

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	pgno = seg_page(seg, addr + len) - seg_page(seg, addr) + 1;
	if (pgno != 0) {
		mutex_enter(&sdp->lock);
		if (sdp->pageprot == 0) {
			do
				protv[--pgno] = sdp->prot;
			while (pgno != 0);
		} else {
			register u_int pgoff = seg_page(seg, addr);

			do {
				pgno--;
				protv[pgno] = sdp->vpage[pgno + pgoff].vp_prot;
			} while (pgno != 0);
		}
		mutex_exit(&sdp->lock);
	}
	return (0);
}

/*ARGSUSED*/
static off_t
segdev_getoffset(register struct seg *seg, caddr_t addr)
{
	register struct segdev_data *sdp = (struct segdev_data *)seg->s_data;

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	return (sdp->offset);
}

/*ARGSUSED*/
static int
segdev_gettype(register struct seg *seg, caddr_t addr)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	return (MAP_SHARED);
}


/*ARGSUSED*/
static int
segdev_getvp(register struct seg *seg, caddr_t addr, struct vnode **vpp)
{
	register struct segdev_data *sdp = (struct segdev_data *)seg->s_data;

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * Note that this vp is the common_vp of the device, where the
	 * devpages are hung ..
	 */
	*vpp = VTOCVP(sdp->vp);

	return (0);
}

static void
segdev_badop(void)
{
	cmn_err(CE_PANIC, "segdev_badop");
	/*NOTREACHED*/
}

/*
 * segdev pages are not in the cache, and thus can't really be controlled.
 * Hence, syncs are simply always successful.
 */
/*ARGSUSED*/
static int
segdev_sync(struct seg *seg, caddr_t addr, u_int len, int attr, u_int flags)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	return (0);
}

/*
 * segdev pages are always "in core".
 */
/*ARGSUSED*/
static int
segdev_incore(struct seg *seg, caddr_t addr,
    register u_int len, register char *vec)
{
	register u_int v = 0;

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	for (len = (len + PAGEOFFSET) & PAGEMASK; len; len -= PAGESIZE,
	    v += PAGESIZE)
		*vec++ = 1;
	return (v);
}

/*
 * segdev pages are not in the cache, and thus can't really be controlled.
 * Hence, locks are simply always successful.
 */
/*ARGSUSED*/
static int
segdev_lockop(struct seg *seg, caddr_t addr,
    u_int len, int attr, int op, u_long *lockmap, size_t pos)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	return (0);
}

/*
 * segdev pages are not in the cache, and thus can't really be controlled.
 * Hence, advise is simply always successful.
 */
/*ARGSUSED*/
static int
segdev_advise(struct seg *seg, caddr_t addr, u_int len, int behav)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	return (0);
}

/*
 * segdev pages are not dumped, so we just return
 */
/*ARGSUSED*/
static void
segdev_dump(struct seg *seg)
{}

/*
 * ddi_segmap_setup:	Used by drivers who wish specify mapping attributes
 *			for a segment.	Called from a drivers segmap(9E)
 *			routine.
 */
/*ARGSUSED*/
int
ddi_segmap_setup(dev_t dev, off_t offset, struct as *as, caddr_t *addrp,
    off_t len, u_int prot, u_int maxprot, u_int flags, cred_t *cred,
    ddi_device_acc_attr_t *accattrp, u_int rnumber)
{
	struct segdev_crargs dev_a;
	int (*mapfunc)(dev_t dev, off_t off, int prot);
	u_int hat_flags;
	int	error, i;

	if ((mapfunc = devopsp[getmajor(dev)]->devo_cb_ops->cb_mmap) ==
	    nodev)
		return (ENODEV);

	/*
	 * Character devices that support the d_mmap
	 * interface can only be mmap'ed shared.
	 */
	if ((flags & MAP_TYPE) != MAP_SHARED)
		return (EINVAL);

	/*
	 * Check that this region is indeed mappable on this platform.
	 * Use the mapping function.
	 */
	if (ddi_device_mapping_check(dev, accattrp, rnumber, &hat_flags) == -1)
		return (ENXIO);

	/*
	 * Check to ensure that the entire range is
	 * legal and we are not trying to map in
	 * more than the device will let us.
	 */
	for (i = 0; i < len; i += PAGESIZE) {
		if (cdev_mmap(mapfunc, dev, offset + i, maxprot) == -1)
			return (ENXIO);
	}

	as_rangelock(as);
	if ((flags & MAP_FIXED) == 0) {
		/*
		 * Pick an address w/o worrying about
		 * any vac alignment contraints.
		 */
		map_addr(addrp, len, (off_t)offset, 0);
		if (*addrp == NULL) {
			as_rangeunlock(as);
			return (ENOMEM);
		}
	} else {
		/*
		 * User-specified address; blow away any previous mappings.
		 */
		(void) as_unmap(as, *addrp, len);
	}

	dev_a.mapfunc = mapfunc;
	dev_a.dev = dev;
	dev_a.offset = offset;
	dev_a.prot = (u_char)prot;
	dev_a.maxprot = (u_char)maxprot;
	dev_a.hat_flags = hat_flags;

	error = as_map(as, *addrp, len, segdev_create, (caddr_t)&dev_a);
	as_rangeunlock(as);
	return (error);

}
