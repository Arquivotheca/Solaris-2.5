/*
 * Copyright (c) 1989 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)seg_kp.c	1.39	94/06/01	SMI"

/*
 * segkp is a segment driver that administers the allocation and deallocation
 * of pageable variable size chunks of kernel virtual address space. Each
 * allocated resource is page-aligned.
 *
 * The user may specify whether the resource should be initialized to 0,
 * include a redzone, or locked in memory.
 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/thread.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/mman.h>
#include <sys/vnode.h>
#include <sys/cmn_err.h>
#include <sys/swap.h>
#include <sys/tuneable.h>
#include <sys/kmem.h>
#include <sys/map.h>
#include <sys/cred.h>
#include <sys/dumphdr.h>
#include <sys/debug.h>
#include <sys/vtrace.h>
#include <sys/stack.h>

#include <vm/as.h>
#include <vm/seg.h>
#include <vm/seg_kp.h>
#include <vm/seg_kmem.h>
#include <vm/anon.h>
#include <vm/page.h>
#include <vm/hat.h>

/*
 * Private seg op routines
 */
static void	segkp_badop(void);
static void	segkp_dump(struct seg *);
static int	segkp_checkprot(struct seg *, caddr_t, u_int, u_int);
static int	segkp_kluster(struct seg *, caddr_t, int);
static void	segkp_freevm(struct seg *, caddr_t, int);
static void	segkp_insert(struct seg *, struct segkp_data *);
static void	segkp_delete(struct seg *, struct segkp_data *);
static caddr_t	segkp_getvm(struct seg *, u_int, int);
static caddr_t	segkp_get_internal(struct seg *, u_int, u_int,
			struct segkp_data **);
static void	segkp_release_internal(struct seg *,
			struct segkp_data *, u_int);
static int	segkp_unlock(struct hat *, struct seg *, caddr_t, u_int,
			struct segkp_data *, u_int);
static int	segkp_load(struct hat *, struct seg *, caddr_t, u_int,
			struct segkp_data *, int);
static struct	segkp_data *segkp_find(struct seg *, caddr_t);

/*
 * Lock used to protect the hash table(s) and caches.
 */
static kmutex_t	segkp_lock;

/*
 * The segkp caches
 */
static struct segkp_cache segkp_cache[SEGKP_MAX_CACHE];

#define	SEGKP_BADOP(t)	(t(*)())segkp_badop

static struct	seg_ops segkp_ops = {
	SEGKP_BADOP(int),		/* dup */
	SEGKP_BADOP(int),		/* unmap */
	SEGKP_BADOP(void),		/* free */
	segkp_fault,
	SEGKP_BADOP(faultcode_t),	/* faulta */
	SEGKP_BADOP(int),		/* setprot */
	segkp_checkprot,
	segkp_kluster,
	SEGKP_BADOP(u_int),		/* swapout */
	SEGKP_BADOP(int),		/* sync */
	SEGKP_BADOP(int),		/* incore */
	SEGKP_BADOP(int),		/* lockop */
	SEGKP_BADOP(int),		/* getprot */
	SEGKP_BADOP(off_t),		/* getoffset */
	SEGKP_BADOP(int),		/* gettype */
	SEGKP_BADOP(int),		/* getvp */
	SEGKP_BADOP(int),		/* advise */
	segkp_dump,			/* dump */
};


static void
segkp_badop()
{
	cmn_err(CE_PANIC, "segkp_badop");
	/* NOTREACHED */
}

/*
 * Allocate the segment specific private data struct and fill it in
 * with the per kp segment mutex, anon ptr. array and hash table.
 */
int
segkp_create(seg)
	struct seg *seg;
{
	struct segkp_segdata *kpsd;
	static int segkp_init = 0;
	int	mapsize;
	int	np;

	ASSERT(seg != NULL && seg->s_as == &kas);
	ASSERT(RW_WRITE_HELD(&seg->s_as->a_lock));

	if (seg->s_size & PAGEOFFSET)
		cmn_err(CE_PANIC, "Bad segkp size");

	if (!segkp_init) {
		mutex_init(&segkp_lock, "segkp lock", MUTEX_DEFAULT, NULL);
		segkp_init = 1;
	}
	kpsd = (struct segkp_segdata *)
		kmem_zalloc(sizeof (struct segkp_segdata), KM_SLEEP);

	np = btop(seg->s_size);
	kpsd->kpsd_anon = (struct anon **)
		    kmem_zalloc(sizeof (struct anon *) * np, KM_SLEEP);
	/*
	 * Allocate the resource map for segkp and initialize it
	 */
	mapsize = max((seg->s_size >> SEGKP_MAP_SHIFT), PAGESIZE);
	kpsd->kpsd_kpmap = (struct map *) kmem_zalloc(mapsize, KM_SLEEP);
	mapinit(kpsd->kpsd_kpmap, seg->s_size, (ulong_t)seg->s_base,
		"kpmap", mapsize / sizeof (struct map));

	kpsd->kpsd_hash = (struct segkp_data **)
			kmem_zalloc(SEGKP_HASHSZ * sizeof (struct segkp *),
				    KM_SLEEP);
	seg->s_data = (void *)kpsd;
	seg->s_ops = &segkp_ops;
	return (0);
}


/*
 * Find a free 'freelist' and initialize it with the appropriate attributes
 */
void *
segkp_cache_init(seg, maxsize, len, flags)
	struct seg * seg;
	int maxsize;
	u_int len;
	u_int flags;
{
	int i;

	if ((flags & KPD_NO_ANON) && !(flags & KPD_LOCKED))
		return ((void *)-1);

	mutex_enter(&segkp_lock);
	for (i = 0; i < SEGKP_MAX_CACHE; i++) {
		if (segkp_cache[i].kpf_inuse)
			continue;
		segkp_cache[i].kpf_inuse = 1;
		segkp_cache[i].kpf_max = maxsize;
		segkp_cache[i].kpf_flags = flags;
		segkp_cache[i].kpf_seg = seg;
		segkp_cache[i].kpf_len = len;
		mutex_exit(&segkp_lock);
		return ((void *)i);
	}
	mutex_exit(&segkp_lock);
	return ((void *)-1);
}

/*
 * Free all the cache resources.
 */
void
segkp_cache_free()
{
	struct segkp_data *kpd;
	struct seg *seg;
	int i;

	mutex_enter(&segkp_lock);
	for (i = 0; i < SEGKP_MAX_CACHE; i++) {
		if (!segkp_cache[i].kpf_inuse)
			continue;
		/*
		 * Disconnect the freelist and process each element
		 */
		kpd = segkp_cache[i].kpf_list;
		seg = segkp_cache[i].kpf_seg;
		segkp_cache[i].kpf_list = NULL;
		segkp_cache[i].kpf_count = 0;
		mutex_exit(&segkp_lock);

		while (kpd != NULL) {
			struct segkp_data *next;

			next = kpd->kp_next;
			segkp_release_internal(seg, kpd, kpd->kp_len);
			kpd = next;
		}
		mutex_enter(&segkp_lock);
	}
	mutex_exit(&segkp_lock);
}

/*
 * There are 2 entries into segkp_get_internal. The first includes a cookie
 * used to access a pool of cached segkp resources. The second does not
 * use the cache.
 */
caddr_t
segkp_get(seg, len, flags)
	struct seg *seg;
	u_int len;
	u_int flags;
{
	struct segkp_data *kpd = NULL;

	if (segkp_get_internal(seg, len, flags, &kpd) != NULL) {
		kpd->kp_cookie = -1;
		return (stom(kpd->kp_base, flags));
	}
	return (NULL);
}

/*
 * Return a 'cached' segkp address
 */
caddr_t
segkp_cache_get(cookie)
	void *cookie;
{
	struct segkp_cache *freelist = NULL;
	struct segkp_data *kpd = NULL;
	int index = (int)cookie;
	struct seg *seg;
	u_int len;
	u_int flags;

	if (index < 0 || index >= SEGKP_MAX_CACHE)
		return (NULL);
	freelist = &segkp_cache[index];

	mutex_enter(&segkp_lock);
	seg = freelist->kpf_seg;
	flags = freelist->kpf_flags;
	if (freelist->kpf_list != NULL) {
		kpd = freelist->kpf_list;
		freelist->kpf_list = kpd->kp_next;
		freelist->kpf_count--;
		mutex_exit(&segkp_lock);
		kpd->kp_next = NULL;
		segkp_insert(seg, kpd);
		return (stom(kpd->kp_base, flags));
	}
	len = freelist->kpf_len;
	mutex_exit(&segkp_lock);
	if (segkp_get_internal(seg, len, flags, &kpd) != NULL) {
		kpd->kp_cookie = index;
		return (stom(kpd->kp_base, flags));
	}
	return (NULL);
}

/*
 * This does the real work of segkp allocation.
 * Return to client base addr. len must be page-aligned. A null value is
 * returned if there are no more vm resources (e.g. pages, swap). The len
 * and base recorded in the private data structure include the redzone
 * and the redzone length (if applicable). If the user requests a redzone
 * either the first or last page is left unmapped depending whether stacks
 * grow to low or high memory.
 *
 * The client may also specify a no-wait flag. If that is set then the
 * request will choose a non-blocking path when requesting resources.
 * The default is make the client wait.
 */
static caddr_t
segkp_get_internal(seg, len, flags, tkpd)
	struct seg *seg;
	u_int len;
	u_int flags;
	struct segkp_data **tkpd;
{
	struct segkp_segdata	*kpsd = (struct segkp_segdata *)seg->s_data;
	struct segkp_data	*kpd;
	caddr_t vbase = NULL;	/* always first virtual, may not be mapped */
	int np = 0;		/* number of pages in the resource */
	struct anon **anonp;	/* ptr to first anon ptr for resource */
	int i;
	caddr_t va;
	u_int pages = 0;
	int will_wait = 1;	/* assume client will want to wait for res. */

	if (len & PAGEOFFSET)
		cmn_err(CE_PANIC, "segkp_get: len is not page-aligned");

	/* Only allow KPD_NO_ANON if we are going to lock it down */
	if ((flags & (KPD_LOCKED|KPD_NO_ANON)) == KPD_NO_ANON)
		return (NULL);

	if (flags & KPD_NOWAIT)
		will_wait = 0;

	if ((kpd = (struct segkp_data *)kmem_zalloc(sizeof (struct segkp_data),
			    will_wait ? KM_SLEEP : KM_NOSLEEP)) == 0)
		return (NULL);

	/*
	 * Fix up the len to reflect the REDZONE if applicable
	 */
	if (flags & KPD_HASREDZONE) {
		np = btop(len) + 1;
		len = len + PAGESIZE;
	} else
		np = btop(len);

	if ((vbase = segkp_getvm(seg, len, will_wait)) == 0) {
		kmem_free(kpd, sizeof (struct segkp_data));
		return (NULL);
	}

	/* If locking, reserve physical memory */
	if (flags & KPD_LOCKED) {
		pages = btop(SEGKP_MAPLEN(len, flags));
		mutex_enter(&freemem_lock);
		if (availrmem - pages < tune.t_minarmem) {
			mutex_exit(&freemem_lock);
			segkp_freevm(seg, vbase, len);
			kmem_free(kpd, sizeof (struct segkp_data));
			return (NULL);
		}
		availrmem -= pages;
		mutex_exit(&freemem_lock);
	}

	/*
	 * Reserve sufficient swap space for this vm resource.  We'll
	 * actually allocate it in the loop below, but reserving it
	 * here allows us to back out more gracefully than if we
	 * had an allocation failure in the body of the loop.
	 *
	 * Note that we don't need swap space for the red zone page.
	 */
	if (!(flags & KPD_NO_ANON)) {
		if (anon_resv(SEGKP_MAPLEN(len, flags)) == 0) {
			if (flags & KPD_LOCKED) {
				mutex_enter(&freemem_lock);
				availrmem += pages;
				mutex_exit(&freemem_lock);
			}
			segkp_freevm(seg, vbase, len);
			kmem_free(kpd, sizeof (struct segkp_data));
			return (NULL);
		}
		kpd->kp_anon = anonp =
		    &kpsd->kpsd_anon[(vbase - seg->s_base) / PAGESIZE];
		TRACE_5(TR_FAC_VM, TR_ANON_SEGKP, "anon segkp:%u %u %u %u %u",
		kpd, curproc->p_pid, vbase, SEGKP_MAPLEN(len, flags), 1);
	} else {
		kpd->kp_anon = anonp = NULL;
	}

	/*
	 * Allocate page and anon resources for the virtual address range
	 * except the redzone
	 */
	for (i = 0, va = vbase; i < np; i++, va += PAGESIZE) {
		page_t			*pl[2];
		struct vnode		*vp;
		u_int			off;
		int			err;
		page_t			*pp = NULL;

		/*
		 * If this page is the red zone page, we don't need swap
		 * space for it.  Note that we skip over the code that
		 * establishes MMU mappings, so that the page remains
		 * invalid.
		 */
		if ((flags & KPD_HASREDZONE) && KPD_REDZONE(kpd) == i)
			continue;

		if (anonp != NULL) {
			ASSERT(anonp[i] == NULL);
			/*
			 * Determine the "vp" and "off" of the anon slot.
			 */
			anonp[i] = anon_alloc(NULL, 0);
			swap_xlate(anonp[i], &vp, &off);

			/*
			 * Create a page with the specified identity.  The
			 * page is returned with the "shared" lock held.
			 */
			err = VOP_GETPAGE(vp, (offset_t)off, PAGESIZE,
			    (u_int *)NULL, pl, PAGESIZE, seg, va, S_CREATE,
			    kcred);
			if (err) {
				/*
				 * XXX - This should not fail.
				 */
				cmn_err(CE_PANIC, "segkp_get: no pages");
			}
			pp = pl[0];
		} else {
			ASSERT(!page_exists(&kvp, (u_int)va));

			if ((pp = page_create_va(&kvp, (u_int)va, PAGESIZE,
			    will_wait ? PG_WAIT : PG_EXCL, seg->s_as, va))
			    == NULL) {
				/*
				 * Legitimize resource; then destroy it.
				 * Easier than trying to unwind here.
				 */
				kpd->kp_flags = flags;
				kpd->kp_base = vbase;
				kpd->kp_len = len;
				segkp_release_internal(seg, kpd, va - vbase);
				return (NULL);
			} else
				page_io_unlock(pp);
		}

		/*
		 * Mark the page for long term keep by setting
		 * `p_lckcnt' field in the page structure.
		 */
		if ((flags & KPD_LOCKED) && !page_pp_lock(pp, 0, 1))
			cmn_err(CE_PANIC, "segkp_get: page lock failed");

		/*
		 * Load and lock an MMU translation for the page.
		 */
		hat_memload(seg->s_as->a_hat, seg->s_as, va, pp,
			(PROT_READ|PROT_WRITE),
			flags & KPD_LOCKED ? HAT_LOCK : HAT_LOAD);

		/*
		 * Now, release lock on the page.
		 */
		page_unlock(pp);
	}

	if (flags & KPD_ZERO)
		bzero(stom(vbase, flags), SEGKP_MAPLEN(len, flags));

	kpd->kp_flags = flags;
	kpd->kp_base = vbase;
	kpd->kp_len = len;
	segkp_insert(seg, kpd);
	*tkpd = kpd;
	return (stom(kpd->kp_base, flags));
}

/*
 * Release the resource to cache if the pool(designate by the cookie)
 * has less than the maximum allowable. If inserted in cache,
 * segkp_delete insures element is taken off of active list.
 */
void
segkp_release(seg, vaddr)
	struct seg *seg;
	caddr_t vaddr;
{
	struct segkp_cache *freelist;
	struct segkp_data *kpd = NULL;

	if ((kpd = segkp_find(seg, vaddr)) == NULL)
		cmn_err(CE_PANIC, "segkp_release: null kpd");

	if (kpd->kp_cookie != -1) {
		freelist = &segkp_cache[kpd->kp_cookie];
		mutex_enter(&segkp_lock);
		if (freelist->kpf_count < freelist->kpf_max) {
			segkp_delete(seg, kpd);
			kpd->kp_next = freelist->kpf_list;
			freelist->kpf_list = kpd;
			freelist->kpf_count++;
			mutex_exit(&segkp_lock);
			return;
		} else {
			mutex_exit(&segkp_lock);
			kpd->kp_cookie = -1;
		}
	}
	segkp_release_internal(seg, kpd, kpd->kp_len);
}

/*
 * Free the entire resource. segkp_unlock gets called with the start of the
 * mapped portion of the resource. The length is the size of the mapped
 * portion
 */
static void
segkp_release_internal(seg, kpd, len)
	struct seg *seg;
	struct segkp_data	*kpd;
	u_int len;
{
	caddr_t			va;
	int			i;
	int			redzone;
	int			np;
	page_t			*pp;
	struct vnode 		*vp;
	u_int			off;

	ASSERT(kpd != NULL);
	np = btop(len);

	/* Remove from active hash list */
	if (kpd->kp_cookie == -1) {
		mutex_enter(&segkp_lock);
		segkp_delete(seg, kpd);
		mutex_exit(&segkp_lock);
	}

	/*
	 * Precompute redzone page index.
	 */
	redzone = -1;
	if (kpd->kp_flags & KPD_HASREDZONE)
		redzone = KPD_REDZONE(kpd);

	/*
	 * Free up those anon resouces that are quiescent.
	 */
	for (va = kpd->kp_base, i = 0; i < np; i++, va += PAGESIZE) {
		if (i == redzone)
			continue;
		if (kpd->kp_flags & KPD_LOCKED)
			hat_unlock(seg->s_as->a_hat, seg->s_as, va, PAGESIZE);
		/*
		 * Unload the translations.
		 */
		hat_unload(seg->s_as, va, PAGESIZE, HAT_UNLOAD);

		if (kpd->kp_anon) {
			/*
			 * Free up anon resouces and destroy the
			 * associated pages.
			 *
			 * Release the lock if there is one. Have to get the
			 * page to do this, unfortunately.
			 */
			if (kpd->kp_flags & KPD_LOCKED) {
				swap_xlate(kpd->kp_anon[i], &vp, &off);
				pp = page_lookup(vp, off, SE_SHARED);
				if (pp == NULL) {
					cmn_err(CE_PANIC,
					    "segkp_release: no page to unlock");
				}
				page_pp_unlock(pp, 0, 1);
				page_unlock(pp);
			}
			anon_free(&kpd->kp_anon[i], PAGESIZE);
			anon_unresv(PAGESIZE);
			TRACE_5(TR_FAC_VM,
			    TR_ANON_SEGKP, "anon segkp:%u %u %u %u %u",
			    kpd, curproc->p_pid, va, PAGESIZE, 0);
		} else {
			pp = page_lookup(&kvp, (u_int)va, SE_EXCL);
			if (kpd->kp_flags & KPD_LOCKED) {
				if (pp == NULL) {
					cmn_err(CE_PANIC,
					    "segkp_release: no page to unlock");
				}
				page_pp_unlock(pp, 0, 1);
			}
			if (pp != NULL)
				page_destroy(pp, 0);
		}
	}

	/* If locked, release physical memory reservation */
	if (kpd->kp_flags & KPD_LOCKED) {
		mutex_enter(&freemem_lock);
		availrmem += btop(SEGKP_MAPLEN(kpd->kp_len, kpd->kp_flags));
		mutex_exit(&freemem_lock);
	}

	segkp_freevm(seg, kpd->kp_base, kpd->kp_len);
	kmem_free(kpd, sizeof (struct segkp_data));
}

/*
 * Handle a fault on an address corresponding to one of the
 * resources in the segkp segment.
 */
faultcode_t
segkp_fault(hat, seg, vaddr, len, type, rw)
	struct hat	*hat;
	struct seg	*seg;
	caddr_t		vaddr;
	u_int		len;
	enum fault_type	type;
	enum seg_rw rw;
{
	struct segkp_data	*kpd = NULL;
	int			err;

	ASSERT(seg->s_as == &kas && RW_READ_HELD(&seg->s_as->a_lock));

	/*
	 * Sanity checks.
	 */
	if (type == F_PROT)
		cmn_err(CE_PANIC, "segkp_fault: unexpected F_PROT fault");

	if ((kpd = segkp_find(seg, vaddr)) == NULL)
		return (FC_NOMAP);

	mutex_enter(&kpd->kp_lock);

	if (type == F_SOFTLOCK) {
		ASSERT(!(kpd->kp_flags & KPD_LOCKED));
		/*
		 * The F_SOFTLOCK case has more stringent
		 * range requirements: the given range must exactly coincide
		 * with the resource's mapped portion. Note reference to
		 * redzone is handled since vaddr would not equal base
		 */
		if (vaddr != stom(kpd->kp_base, kpd->kp_flags) ||
		    len != SEGKP_MAPLEN(kpd->kp_len, kpd->kp_flags)) {
			mutex_exit(&kpd->kp_lock);
			return (FC_MAKE_ERR(EFAULT));
		}

		if ((err = segkp_load(hat, seg, vaddr, len, kpd, KPD_LOCKED))) {
			mutex_exit(&kpd->kp_lock);
			return (FC_MAKE_ERR(err));
		}
		kpd->kp_flags |= KPD_LOCKED;
		mutex_exit(&kpd->kp_lock);
		return (0);
	}

	if (type == F_INVAL) {
		ASSERT(!(kpd->kp_flags & KPD_NO_ANON));
		/*
		 * Check if we touched the redzone. Somewhat optimistic
		 * here if we are touching the redzone of our own stack
		 * since we wouldn't have a stack to get this far...
		 */
		if ((kpd->kp_flags & KPD_HASREDZONE) &&
			btop(vaddr - kpd->kp_base) == KPD_REDZONE(kpd))
			cmn_err(CE_PANIC, "segkp_fault: accessing redzone");

		err = segkp_load(hat, seg, vaddr, len, kpd, kpd->kp_flags);
		mutex_exit(&kpd->kp_lock);
		return (err ? FC_MAKE_ERR(err) : 0);
	}

	if (type == F_SOFTUNLOCK) {
		u_int	flags;

		/*
		 * Make sure the addr is LOCKED and it has anon backing
		 * before unlocking
		 */
		if ((kpd->kp_flags & (KPD_LOCKED|KPD_NO_ANON)) == KPD_NO_ANON)
			cmn_err(CE_PANIC, "segkp_fault: bad unlock");

		if (vaddr != stom(kpd->kp_base, kpd->kp_flags) ||
		    len != SEGKP_MAPLEN(kpd->kp_len, kpd->kp_flags))
			cmn_err(CE_PANIC, "segkp_fault: bad range");

		if (rw == S_WRITE)
			flags = kpd->kp_flags | KPD_WRITEDIRTY;
		else
			flags = kpd->kp_flags;
		err = segkp_unlock(hat, seg, vaddr, len, kpd, flags);
		kpd->kp_flags &= ~KPD_LOCKED;
		mutex_exit(&kpd->kp_lock);
		return (err ? FC_MAKE_ERR(err) : 0);
	}
	mutex_exit(&kpd->kp_lock);
	cmn_err(CE_PANIC, "segkp_fault: bogus fault type: %d\n", type);
	/*NOTREACHED*/
}

/*
 * Check that the given protections suffice over the range specified by
 * vaddr and len.  For this segment type, the only issue is whether or
 * not the range lies completely within the mapped part of an allocated
 * resource.
 */
/* ARGSUSED */
static int
segkp_checkprot(seg, vaddr, len, prot)
	struct seg	*seg;
	caddr_t		vaddr;
	u_int		len;
	u_int		prot;
{
	struct segkp_data *kpd = NULL;
	caddr_t mbase;
	int mlen;

	if ((kpd = segkp_find(seg, vaddr)) == NULL)
		return (EACCES);

	mutex_enter(&kpd->kp_lock);
	mbase = stom(kpd->kp_base, kpd->kp_flags);
	mlen = SEGKP_MAPLEN(kpd->kp_len, kpd->kp_flags);
	if (len > mlen || vaddr < mbase ||
	    ((vaddr + len) > (mbase + mlen))) {
		mutex_exit(&kpd->kp_lock);
		return (EACCES);
	}
	mutex_exit(&kpd->kp_lock);
	return (0);
}


/*
 * Check to see if it makes sense to do kluster/read ahead to
 * addr + delta relative to the mapping at addr.  We assume here
 * that delta is a signed PAGESIZE'd multiple (which can be negative).
 *
 * For seg_u we always "approve" of this action from our standpoint.
 */
/* ARGSUSED */
static int
segkp_kluster(seg, addr, delta)
	struct seg	*seg;
	caddr_t		addr;
	int		delta;
{
	return (0);
}

/*
 * Load and possibly lock intra-slot resources in the range given by
 * vaddr and len.
 */
static
segkp_load(hat, seg, vaddr, len, kpd, flags)
	struct hat *hat;
	struct seg *seg;
	caddr_t vaddr;
	u_int len;
	struct segkp_data *kpd;
	int flags;
{
	caddr_t va;
	caddr_t vlim;
	register u_int i;
	u_int lock;

	ASSERT(MUTEX_HELD(&kpd->kp_lock));

	len = roundup(len, PAGESIZE);

	/* If locking, reserve physical memory */
	if (flags & KPD_LOCKED) {
		mutex_enter(&freemem_lock);
		if (availrmem - btop(len) < tune.t_minarmem) {
			mutex_exit(&freemem_lock);
			return (ENOMEM);
		}
		availrmem -= btop(len);
		mutex_exit(&freemem_lock);
	}

	/*
	 * Loop through the pages in the given range.
	 */
	va = (caddr_t)((u_int)vaddr & PAGEMASK);
	vaddr = va;
	vlim = va + len;
	lock = flags & KPD_LOCKED;
	i = (va - kpd->kp_base) / PAGESIZE;
	for (; va < vlim; va += PAGESIZE, i++) {
		page_t		*pl[2];	/* second element NULL terminator */
		struct vnode    *vp;
		u_int		off;
		register int    err;

		/*
		 * Summon the page.  If it's not resident, arrange
		 * for synchronous i/o to pull it in.
		 */
		swap_xlate(kpd->kp_anon[i], &vp, &off);

		/*
		 * The returned page list will have exactly one entry,
		 * which is returned to us already kept.
		 */
		err = VOP_GETPAGE(vp, (offset_t)off, PAGESIZE, (u_int *)NULL,
		    pl, PAGESIZE, seg, va, S_READ, kcred);

		if (err) {
			/*
			 * Back out of what we've done so far.
			 */
			(void) segkp_unlock(hat, seg, vaddr,
			    (u_int)(va - vaddr), kpd, flags);
			return (err);
		}

		/*
		 * If we're locking down resources, we need to increment
		 * the page's long term keep count.  In any event, we
		 * need to release the "shared" lock on the page.
		 *
		 * XXX: When page_pp_lock returns a success/failure
		 *	indication, we'll probably want to panic if
		 *	it fails.
		 */
		if (lock) {
			if (!page_pp_lock(pl[0], 0, 1))
				cmn_err(CE_PANIC,
				    "segkp_load: attempt to lock page failed");
		}

		/*
		 * Load an MMU translation for the page.
		 */
		hat_memload(hat, seg->s_as, va, pl[0], (PROT_READ|PROT_WRITE),
		    lock ? HAT_LOCK : HAT_LOAD);

		/*
		 * Now, release "shared" lock on the page.
		 */
		page_unlock(pl[0]);
	}
	return (0);
}

/*
 * At the very least unload the mmu-translations and unlock the range if locked
 * Can be called with the following flag value KPD_WRITEDIRTY which specifies
 * any dirty pages should be written to disk.
 */
static int
segkp_unlock(hat, seg, vaddr, len, kpd, flags)
	struct hat *hat;
	struct seg *seg;
	caddr_t vaddr;
	u_int len;
	struct segkp_data *kpd;
	u_int flags;
{
	register caddr_t va;
	caddr_t vlim;
	register u_int i;
	register struct page *pp;
	struct vnode *vp;
	u_int off;

	ASSERT(MUTEX_HELD(&kpd->kp_lock));

	/*
	 * Loop through the pages in the given range. It is assumed
	 * segkp_unlock is called with page aligned base
	 */
	va = vaddr;
	vlim = va + len;
	i = (u_int)(va - kpd->kp_base) / PAGESIZE;
	for (; va < vlim; va += PAGESIZE, i++) {

		if (flags & KPD_LOCKED)
			hat_unlock(hat, seg->s_as, va, PAGESIZE);
		/*
		 * Unload it.
		 */
		hat_unload(seg->s_as, va, PAGESIZE, HAT_UNLOAD);
		/*
		 * Find the page associated with this part of the
		 * slot, tracking it down through its associated swap
		 * space.
		 */
		swap_xlate(kpd->kp_anon[i], &vp, &off);

		if ((pp = page_lookup(vp, off, SE_SHARED)) == NULL) {
			if (flags & KPD_LOCKED)
				cmn_err(CE_PANIC,
				    "segkp_softunlock: missing page");
		}

		/*
		 * Nothing to do if the slot is not locked and the
		 * page doesn't exist.
		 */
		if (!(flags & KPD_LOCKED) && pp == NULL)
			continue;

		/*
		 * Release our long-term claim on the page, if
		 * the "segkp" slot was locked.
		 */
		if (flags & KPD_LOCKED)
			page_pp_unlock(pp, 0, 1);

		/*
		 * If the page doesn't have any translations, is
		 * dirty and not being shared, then push it out
		 * asynchronously and avoid waiting for the
		 * pageout daemon to do it for us.
		 *
		 * XXX - Do we really need to get the "exclusive"
		 * lock via an upgrade?
		 */
		if ((flags & KPD_WRITEDIRTY) && pp->p_mapping == NULL &&
		    PP_ISMOD(pp) && page_tryupgrade(pp)) {
			/*
			 * Hold the vnode before releasing the page lock to
			 * prevent it from being freed and re-used by some
			 * other thread.
			 */
			VN_HOLD(vp);
			page_unlock(pp);

			/*
			 * Want most powerful credentials we can get so
			 * use kcred.
			 */
			(void) VOP_PUTPAGE(vp, (offset_t)off, PAGESIZE,
			    B_ASYNC | B_FREE, kcred);
			VN_RELE(vp);
		} else {
			page_unlock(pp);
		}
	}

	/* If unlocking, release physical memory */
	if (flags & KPD_LOCKED) {
		mutex_enter(&freemem_lock);
		availrmem += btopr(len);
		mutex_exit(&freemem_lock);
	}
	return (0);
}

/*
 * Assign a range of virtual addresses represent a chunk of size bytes.
 */
static caddr_t
segkp_getvm(seg, size, wait)
	struct seg *seg;
	u_int size;
	int wait;
{
	struct segkp_segdata *kpsd = (struct segkp_segdata *)seg->s_data;

	if (wait)
		return ((caddr_t)rmalloc_wait(kpsd->kpsd_kpmap, (size_t)size));
	else
		return ((caddr_t)rmalloc(kpsd->kpsd_kpmap, (size_t)size));
}


/*
 * Free a range of virtual addresses represent a chunk of len bytes. The whole
 * range will be released
 */
static void
segkp_freevm(seg, base, len)
	struct seg *seg;
	caddr_t base;
	int len;
{
	struct segkp_segdata *kpsd = (struct segkp_segdata *)seg->s_data;

	rmfree(kpsd->kpsd_kpmap, len, (u_long)base);
}


/*
 * Insert the kpd in the hash table.
 */
static void
segkp_insert(seg, kpd)
	struct seg *seg;
	struct segkp_data *kpd;
{
	struct segkp_segdata *kpsd = (struct segkp_segdata *)seg->s_data;
	int index;

	/*
	 * Insert the kpd based on the address that will be returned
	 * via segkp_release.
	 */
	index = SEGKP_HASH(stom(kpd->kp_base, kpd->kp_flags));
	mutex_enter(&segkp_lock);
	kpd->kp_next = kpsd->kpsd_hash[index];
	kpsd->kpsd_hash[index] = kpd;
	mutex_exit(&segkp_lock);
}

/*
 * Remove kpd from the hash table.
 */
static void
segkp_delete(seg, kpd)
	struct seg *seg;
	struct segkp_data *kpd;
{
	struct segkp_segdata *kpsd = (struct segkp_segdata *)seg->s_data;
	struct segkp_data **kpp;
	int index;

	ASSERT(MUTEX_HELD(&segkp_lock));

	index = SEGKP_HASH(stom(kpd->kp_base, kpd->kp_flags));
	for (kpp = &kpsd->kpsd_hash[index]; *kpp != NULL;
					kpp = &((*kpp)->kp_next)) {
		if (*kpp == kpd) {
			*kpp = kpd->kp_next;
			return;
		}
	}
	cmn_err(CE_PANIC, "segkp_delete: unable to find element to delete");
}

/*
 * Find the kpd associated with a vaddr.
 *
 * Most of the callers of segkp_find will pass the vaddr that
 * hashes to the desired index, but there are cases where
 * this is not true in which case we have to (potentially) scan
 * the whole table looking for it. This should be very rare
 * (e.g. a segkp_fault(F_INVAL) on a an address somwhere in the
 * middle of the segkp_data region).
 */
static struct segkp_data *
segkp_find(seg, vaddr)
	struct seg *seg;
	caddr_t vaddr;
{
	struct segkp_segdata *kpsd = (struct segkp_segdata *)seg->s_data;
	struct segkp_data *kpd;
	int	i;
	int	stop;

	i = stop = SEGKP_HASH(vaddr);
	mutex_enter(&segkp_lock);
	do {
		for (kpd = kpsd->kpsd_hash[i]; kpd != NULL;
						kpd = kpd->kp_next) {
			if (vaddr >= kpd->kp_base &&
			    vaddr <= kpd->kp_base + kpd->kp_len) {
				mutex_exit(&segkp_lock);
				return (kpd);
			}
		}
		if (--i < 0)
			i = SEGKP_HASHSZ - 1;	/* Wrap */
	} while (i != stop);
	mutex_exit(&segkp_lock);
	return (NULL);		/* Not found */
}

/*
 * returns size of swappable area.
 */
int
swapsize(v)
	caddr_t v;
{
	struct segkp_data *kpd;

	if ((kpd = segkp_find(segkp, v)) != NULL)
		return (SEGKP_MAPLEN(kpd->kp_len, kpd->kp_flags));
	else
		return (NULL);
}

/*
 * Dump out all the active segkp pages
 */
static void
segkp_dump(seg)
	struct seg *seg;
{
	int i;
	struct segkp_data *kpd;
	struct segkp_segdata *kpsd = (struct segkp_segdata *)seg->s_data;

	for (i = 0; i < SEGKP_HASHSZ; i++) {
		for (kpd = kpsd->kpsd_hash[i]; kpd != NULL;
						kpd = kpd->kp_next) {
			u_int pfn;
			caddr_t addr;
			caddr_t eaddr;

			addr = kpd->kp_base;
			eaddr = addr + kpd->kp_len;
			while (addr < eaddr) {
				pfn = hat_getkpfnum(addr);
				if (pfn != (u_int) -1)
					dump_addpage(pfn);
				addr += PAGESIZE;
			}
		}
	}
}
