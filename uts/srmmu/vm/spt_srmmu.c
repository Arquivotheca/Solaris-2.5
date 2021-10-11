/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#pragma	ident  "@(#)spt_srmmu.c 1.20     95/06/13 SMI"

#include <sys/param.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <sys/kmem.h>
#include <vm/hat.h>
#include <vm/hat_srmmu.h>
#include <vm/seg.h>
#include <vm/as.h>
#include <vm/anon.h>
#include <vm/page.h>
#include <sys/buf.h>
#include <vm/spt.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/swap.h>
#include <sys/debug.h>
#include <sys/vtrace.h>

#define	SEGSPTADDR	(caddr_t)0x0
#define	CHECK_MAXMEM(x) ((spt_used + (x)) * 100 < maxmem * spt_max)

/*
 * max percent among maxmem for spt
 */
static int	spt_max = 90;

/*
 * # pages used for spt
 */
static u_int	spt_used;

u_int existing_anon_pages;
u_int existing_anon_error;

extern int		maxmem;

int segspt_create(struct seg *seg, caddr_t argsp);
static int segspt_unmap(struct seg *seg, caddr_t raddr, u_int ssize);
static int segspt_unmap(struct seg *seg, caddr_t raddr, u_int ssize);
void segspt_free(struct seg *seg);
static int segspt_lockop(struct seg *seg, caddr_t addr, u_int len, int attr,
    int op, ulong *lockmap, size_t pos);
int 	segspt_badops();
void segspt_free_pages(struct seg *seg, caddr_t addr, u_int len);
static int segspt_kluster(struct seg *seg, caddr_t addr, int delta);

struct seg_ops segspt_ops = {
	segspt_badops,
	segspt_unmap,
	segspt_free,
	segspt_badops,
	segspt_badops,
	segspt_badops,
	segspt_badops,
	segspt_kluster,
	(u_int (*)()) NULL,	/* swapout */
	segspt_badops,
	segspt_badops,
	segspt_lockop,
	segspt_badops,
	(off_t (*)()) segspt_badops,
	segspt_badops,
	segspt_badops,
	segspt_badops,		/* advise */
	(void (*)()) segspt_badops,
};

int segspt_shmdup(struct seg *seg, struct seg *newseg);
int segspt_shmunmap(struct seg *seg, caddr_t raddr, u_int ssize);
void segspt_shmfree(struct seg *seg);
static faultcode_t segspt_shmfault(struct hat *hat, struct seg *seg,
    caddr_t addr, u_int len, enum fault_type type, enum seg_rw rw);
static int segspt_shmsetprot(register struct seg *seg, register caddr_t addr,
    register u_int len, register u_int prot);
static int segspt_shmcheckprot(struct seg *seg, caddr_t addr, u_int size,
    u_int prot);
int  	segspt_shmbadops();
static int segspt_shmincore(struct seg *seg, caddr_t addr, u_int len,
    register char *vec);
static int segspt_shmsync(struct seg *seg, register caddr_t addr, u_int len,
    int attr, u_int flags);
static int segspt_shmlockop(struct seg *seg, caddr_t addr, u_int len, int attr,
    int op, ulong *lockmap, size_t pos);
static int segspt_shmgetprot(struct seg *seg, caddr_t addr, u_int len,
    u_int *protv);
static off_t segspt_shmgetoffset(struct seg *seg, caddr_t addr);
static int segspt_shmgettype(struct seg *seg, caddr_t addr);
static int segspt_shmgetvp(struct seg *seg, caddr_t addr, struct vnode **vpp);
static int segspt_shmadvice(struct seg *seg, caddr_t addr, u_int len,
    int behav);
static void segspt_shmdump(struct seg *seg);

struct seg_ops segspt_shmops = {
	segspt_shmdup,
	segspt_shmunmap,
	segspt_shmfree,
	segspt_shmfault,
	segspt_shmbadops,
	segspt_shmsetprot,
	segspt_shmcheckprot,
	segspt_shmbadops,
	(u_int (*)()) NULL,
	segspt_shmsync,
	segspt_shmincore,
	segspt_shmlockop,
	segspt_shmgetprot,
	segspt_shmgetoffset,
	segspt_shmgettype,
	segspt_shmgetvp,
	segspt_shmadvice,	/* advise */
	segspt_shmdump,
};

int  hat_share();
void hat_unshare();

#ifdef VAC
#include <sys/cpu.h>

#define	MAX_SPT_SHMSEG	20

static	kmutex_t	spt_as_mtx;

struct  spt_seglist {
	struct  seg		*spt_seg;
	struct  spt_seglist	*spt_next;
};

struct  spt_aslist {
	struct  as		*spt_as;
	struct  spt_seglist	*spt_list;
	struct  spt_aslist	*spt_next;
	int			spt_count;
};

struct  spt_aslist *spt_ashead = NULL;
int	max_spt_shmseg = MAX_SPT_SHMSEG;

void	spt_mtxinit();
void	spt_vacsync(char *, struct as *);
static	void	spt_addsptas(struct as *);
static	void	spt_delsptas(struct as *);
static	void	spt_addsptseg(struct seg *, struct as *);
static	void	spt_delsptseg(struct seg *, struct as *);
#endif /* VAC */

int
sptcreate(size, pas, amp)
	u_int size;
	struct as **pas;
	struct anon_map *amp;
{
	int err;
	struct as	*newas;

	/*
	 * get a new as for this shared memory segment
	 */
	newas = as_alloc();
	*pas = newas;

	/*
	 * create a shared page table (spt) segment
	 */
	if (err = as_map(newas, SEGSPTADDR, size, segspt_create,
				(caddr_t)amp)) {
		as_free(newas);
		return (err);
	}

#ifdef VAC
	if (cache & CACHE_VAC)
		spt_addsptas(newas);
#endif /* VAC */
	return (0);
}

void
sptdestroy(as, amp)
	struct as	*as;
	struct anon_map *amp;
{
#ifdef VAC
	if (cache & CACHE_VAC)
		spt_delsptas(as);
#endif /* VAC */
	as_unmap(as, SEGSPTADDR, amp->size);
	as_free(as);

	TRACE_2(TR_FAC_VM, TR_ANON_SHM, "anon shm: as %x, amp %x", as, amp);
}

/*
 * called from seg_free().
 * free (i.e., unlock, unmap, return to free list)
 *  all the pages in the given seg.
 */
void
segspt_free(struct seg *seg)
{
	struct spt_data *spt = (struct spt_data *)seg->s_data;

	TRACE_1(TR_FAC_VM, TR_ANON_SHM, "anon shm: seg %x", seg);

	if (spt != NULL) {
		if (spt->realsize)
			(void) segspt_free_pages(seg, seg->s_base,
			    spt->realsize);

		if (spt->vp != NULL)
			kmem_free((caddr_t)spt->vp, sizeof (struct vnode));

		kmem_free((caddr_t)spt, sizeof (struct spt_data));
	}
}

/*ARGSUSED*/
static int
segspt_shmsync(struct seg *seg, register caddr_t addr, u_int len,
    int attr, u_int flags)
{

	return (0);

}

static int
segspt_shmincore(struct seg *seg, caddr_t addr, u_int len,
    register char *vec)
{
	caddr_t eo_seg;	/* end of segment */

#ifdef lint
	seg = seg;
#endif

	eo_seg = addr + len;
	while (addr < eo_seg) {
		/* page exist, and it's locked. */
		*vec++ = (char)0x9;
		addr += PAGESIZE;
	}

	return (len);
}

/*
 * called from as_ctl(, MC_LOCK,)
 *
 */
/*ARGSUSED*/
static int
segspt_lockop(struct seg *seg, caddr_t addr, u_int len, int attr,
    int op, ulong *lockmap, size_t pos)
{
	/*
	 * for spt, as->a_paglck is never set
	 * so this routine should not be called.
	 */
	return (0);
}

static int
segspt_unmap(struct seg *seg, caddr_t raddr, u_int ssize)
{
	u_int share_size;

	u_int get_hw_pagesize();
	int num_hw_pagesizes();

	TRACE_3(TR_FAC_VM, TR_ANON_SHM, "anon shm: seg %x, addr %x, size %x",
				seg, raddr, ssize);

	/*
	 * seg.s_size may have been rounded up to the nearest L1 pt
	 * boundary in shmat().
	 */
	share_size = get_hw_pagesize(num_hw_pagesizes() - 1);
	ssize = roundup(ssize, share_size);

	if (raddr == seg->s_base && ssize == seg->s_size) {
		seg_free(seg);
		return (0);
	} else {
		panic("segspt_unmap - unexpected address");
		/*NOTREACHED*/
	}
}

int
segspt_badops()
{
	panic("segspt_badops is called");
	/*NOTREACHED*/
	return (0);
}

int
segspt_create(struct seg *seg, caddr_t argsp)
{

	int		err = 0;
	u_int		len  = seg->s_size;
	caddr_t		addr = seg->s_base;
	struct page 	*pp;
	page_t 		*anon_pl[1 + 1];
	caddr_t		v;
	struct vnode	*vp;
	struct anon_map *amp = (struct anon_map *)argsp;
	struct anon	*ap;
	u_int		npages, index, prot;
	struct cred 	*cred;
	struct spt_data *spt;

	if (!CHECK_MAXMEM(btopr(len)))
		return (ENOMEM);

	spt = kmem_zalloc(sizeof (struct spt_data), KM_NOSLEEP);
	if (spt == NULL)
		return (ENOMEM);

	vp = kmem_zalloc(sizeof (struct vnode), KM_NOSLEEP);
	if (vp == NULL) {
		kmem_free(spt, sizeof (struct spt_data));
		return (ENOMEM);
	} else {
		seg->s_ops = &segspt_ops;
		spt->vp = vp;
		spt->amp = amp;
		seg->s_data = (caddr_t)spt;
	}

	/*
	 * check each of anon slots in amp, and
	 * if it is allocated, map the page;
	 * if it is not, get a page for the slot.
	 */
	npages = btopr(amp->size);
	cred = CRED();

	mutex_enter(&amp->serial_lock);
	for (index = 0, v = addr; index < npages; index++, v += PAGESIZE) {
		struct anon **app;

		app = &amp->anon[index];

		if (*app == NULL) {
			if ((pp = anon_zero(seg, v, &ap, cred)) == NULL) {
				mutex_exit(&amp->serial_lock);
				cmn_err(CE_PANIC,
					"segspt_create anon_zero");
				}

			mutex_enter(&amp->lock);
			ASSERT(*app == NULL);
			*app = ap;
			mutex_exit(&amp->lock);
		} else {
			/*
			 * if anon slot already exists
			 *   (means page has been created)
			 * so 1) look up the page
			 *    2) if the page is still in memory, get it.
			 *    3) if not, create a page and
			 *	  page in from physical swap device.
			 * These are done in anon_getpage().
			 */
			existing_anon_pages++;
			err = anon_getpage(app, &prot, anon_pl, PAGESIZE,
					seg, v, S_READ, cred);
			if (err) {
				existing_anon_error++;
				goto out;
			}
			pp = anon_pl[0];
		}

		/*
		 * lock each page long-term.
		 * so pageout and fsflush won't touch it.
		 */
		if (page_pp_lock(pp, 0, 0) != 1) {

			/*
			 * we failed to lock this page. This means
			 * there is not enough memory left to be
			 * locked down. Well, give up ISM.
			 * undo what we have done. But, we don't
			 * have to give up pages that we just
			 * created, we just need to unlock thems so
			 * they behave like normal pages.
			 */
			page_unlock(pp);

			/*
			 * A better return code should be ENOMEM, but
			 * Oracle seems to take ENOMEM very hard. It
			 * behaves very much different in startup
			 * stage.
			 */
			err = EINVAL;
			goto out;
		}

		/* map the page */
		hat_memload(seg->s_as->a_hat, seg->s_as, v, pp,
				PROT_ALL, PTELD_LOCK | HAT_LOADSHARED);
		page_unlock(pp);
	}

out:
	spt->realsize = index * PAGESIZE;
	spt_used += btopr(spt->realsize);

	mutex_exit(&amp->serial_lock);
	return (err);
}

void
segspt_free_pages(struct seg *seg, caddr_t addr, u_int len)
{
	struct as	*as;
	struct page 	*pp;
	struct spt_data *spt = (struct spt_data *)seg->s_data;
	struct anon_map *amp;
	u_int		npages, index;

	as = seg->s_as;
	amp = spt->amp;

	TRACE_3(TR_FAC_VM, TR_ANON_SHM, "anon shm: seg %x, addr %x, spt %x",
				seg, addr, spt);

	npages = btopr(len);
	for (index = 0; index < npages; addr += PAGESIZE, index++) {
		struct anon **app;
		struct vnode *vp;
		u_int off;

		app = &amp->anon[index];
		if (*app == NULL) {
			break;
		}

		hat_unlock(as->a_hat, as, addr, PAGESIZE);
		hat_unload(seg->s_as, addr, PAGESIZE, HAT_UNLOAD);

		mutex_enter(&amp->lock);
		swap_xlate(*app, &vp, &off);
		mutex_exit(&amp->lock);

		pp = page_lookup(vp, off, SE_EXCL);
		if (pp == NULL) {
			cmn_err(CE_PANIC, "segspt_free_pages");
		}

		page_pp_unlock(pp, 0, 0);
		page_unlock(pp);
	}

	spt_used -= index;

	seg->s_size = NULL;		/* mark that pages have been released */
}

int
segspt_shmattach(seg, argsp)
	struct seg	*seg;
	caddr_t 	*argsp;
{

	struct sptshm_data *spt = (struct sptshm_data *)argsp;
	struct sptshm_data *ssd;
	struct anon_map *shm_amp = spt->amp;
	int error;

	ssd = (struct sptshm_data *)kmem_zalloc((sizeof (struct sptshm_data)),
						KM_NOSLEEP);

	if (ssd == NULL)
		return (ENOMEM);

	ssd->sptas = spt->sptas;
	ssd->amp = shm_amp;
	seg->s_data = (void *)ssd;
	seg->s_ops = &segspt_shmops;
	mutex_enter(&shm_amp->lock);
	shm_amp->refcnt++;
	mutex_exit(&shm_amp->lock);

	kpreempt_disable();

	error = hat_share(seg->s_as, seg->s_base, spt->sptas, 0, seg->s_size);

#ifdef VAC
	if (!error && (cache & CACHE_VAC))
		spt_addsptseg(seg, spt->sptas);
#endif /* VAC */
	kpreempt_enable();

	return (error);
}

int
segspt_shmunmap(struct seg *seg, caddr_t raddr, u_int ssize)
{
	struct sptshm_data *ssd = (struct sptshm_data *)seg->s_data;

	if (ssd->softlockcnt > 0)
		return (EAGAIN);
#ifdef VAC
	if (cache & CACHE_VAC)
		spt_delsptseg(seg, ssd->sptas);
#endif /* VAC */

	hat_unshare(seg->s_as, raddr, ssize);
	seg_free(seg);
	return (0);
}

void
segspt_shmfree(struct seg *seg)
{
	struct sptshm_data *ssd = (struct sptshm_data *)seg->s_data;
	struct anon_map *shm_amp = ssd->amp;

	/*
	 * Need to increment refcnt when attaching
	 * and decrement when detaching because of dup().
	 */
	mutex_enter(&shm_amp->lock);
	shm_amp->refcnt--;
	mutex_exit(&shm_amp->lock);

	kmem_free((caddr_t)seg->s_data, sizeof (struct sptshm_data));
}

/*ARGSUSED*/
static int
segspt_shmsetprot(register struct seg *seg, register caddr_t addr,
    register u_int len, register u_int prot)
{
	/*
	 * Shared page table is more than shared mapping.
	 *  Individual process sharing page tables can't change prot
	 *  because there is only one set of page tables.
	 *  This will be allowed after private page table is
	 *  supported.
	 */
	return (0);
}

u_int spt_fault;

static faultcode_t
segspt_shmfault(struct hat *hat, struct seg *seg, caddr_t addr, u_int len,
    enum fault_type type, enum seg_rw rw)

{
	struct seg		*sptseg;
	struct sptshm_data 	*ssd;
	struct as		*as, *curspt;
	u_int			npages, anon_index, i;
	struct anon_map		*amp;
	struct page		*pp;

#ifdef lint
	rw = rw;
	hat = hat;
#endif

	switch (type) {
	case F_SOFTUNLOCK:
		/*
		 * Check for softlock
		 */
		ssd = (struct sptshm_data *)seg->s_data;
		mutex_enter(&ssd->lock);
		ssd->softlockcnt--;
		mutex_exit(&ssd->lock);
		/*
		 * unlock the pages locked below in SOFTLOCK
		 */
		amp = ssd->amp;
		npages = btopr(len);
		anon_index = seg_page(seg, addr);
		for (i = 0; i < npages; i++, anon_index++) {
			struct anon **app;
			struct vnode *vp;
			u_int off;

			app = &amp->anon[anon_index];
			if (*app == NULL) {
				break;
			}

			mutex_enter(&amp->lock);
			swap_xlate(*app, &vp, &off);
			mutex_exit(&amp->lock);

			pp = page_lookup(vp, off, SE_SHARED);
			if (pp == NULL) {
				cmn_err(CE_PANIC, "segspt_shmfault SOFTUNLOCK");
			}
			/*
			 * Once for page_lookup, once for SOFTLOCK
			 */
			page_unlock(pp);
			page_unlock(pp);
		}
		if (ssd->softlockcnt == 0) {
			/*
			 * All SOFTLOCKS are gone. Wakeup any waiting
			 * unmappers so they can try again to unmap.
			 * As an optimization check for waiters first
			 * without the mutex held, so we're not always
			 * grabbing it on softunlocks.
			 */
			if (seg->s_as->a_unmapwait) {
				mutex_enter(&seg->s_as->a_contents);
				if (seg->s_as->a_unmapwait) {
					seg->s_as->a_unmapwait = 0;
					cv_broadcast(&seg->s_as->a_cv);
				}
				mutex_exit(&seg->s_as->a_contents);
			}
		}
		return (0);
	case F_SOFTLOCK:
		/*
		 * Because we know that every shared memory is
		 * already locked and called in the same context.
		 */
		ssd = (struct sptshm_data *)seg->s_data;
		mutex_enter(&ssd->lock);
		ssd->softlockcnt++;
		mutex_exit(&ssd->lock);
		/*
		 * prmapin calls ppmapin which expects selock to be held
		 */
		amp = ssd->amp;
		npages = btopr(len);
		anon_index = seg_page(seg, addr);
		for (i = 0; i < npages; i++, anon_index++) {
			struct anon **app;
			struct vnode *vp;
			u_int off;

			app = &amp->anon[anon_index];
			if (*app == NULL) {
				break;
			}

			mutex_enter(&amp->lock);
			swap_xlate(*app, &vp, &off);
			mutex_exit(&amp->lock);

			/*
			 * called for it's side effect of locking the page
			 */
			pp = page_lookup(vp, off, SE_SHARED);
			if (pp == NULL) {
				cmn_err(CE_PANIC, "segspt_shmfault SOFTLOCK");
			}
		}
		return (0);

	case F_INVAL:
		spt_fault++;

		as = seg->s_as;

		ssd = (struct sptshm_data *)seg->s_data;
		curspt = ssd->sptas;

		AS_LOCK_ENTER(curspt, &curspt->a_lock, RW_READER);
		sptseg = as_segat(curspt, SEGSPTADDR);
		AS_LOCK_EXIT(curspt, &curspt->a_lock);

		if (hat_share(as, seg->s_base, curspt, sptseg->s_base,
		    sptseg->s_size) != 0) {
			cmn_err(CE_PANIC, "ISM fault with bad addr.");
		}

#ifdef VAC
		if (cache & CACHE_VAC)
			spt_addsptseg(seg, curspt);
#endif /* VAC */

		return (0);

	case F_PROT:

	default:
		return (FC_NOMAP);
	}
}
int
segspt_shmbadops()
{
	panic("segspt_shmbadops is called");
	/*NOTREACHED*/
	return (0);
}

/*
 * duplicate the shared page tables
 */

int
segspt_shmdup(struct seg *seg, struct seg *newseg)
{
	struct sptshm_data *ssd = (struct sptshm_data *)seg->s_data;
	struct anon_map *amp = ssd->amp;
	struct sptshm_data *nsd;

	nsd = (struct sptshm_data *)kmem_zalloc((sizeof (struct sptshm_data)),
						KM_NOSLEEP);

	if (nsd == NULL)
		return (ENOMEM);

	newseg->s_data = (void *) nsd;
	nsd->sptas = ssd->sptas;
	nsd->amp = amp;
	newseg->s_ops = &segspt_shmops;

	mutex_enter(&amp->lock);
	amp->refcnt++;
	mutex_exit(&amp->lock);

	/*
	 * this will depend on hat_dup() to
	 * duplicate level-1 page table.
	 * If hat_dup() does not duplicate,
	 * we need to do it here.
	 */

	hat_share(newseg->s_as, newseg->s_base, ssd->sptas,
			0, seg->s_size);

#ifdef VAC
	if (cache & CACHE_VAC)
		spt_addsptseg(newseg, ssd->sptas);
#endif /* VAC */

	return (0);
}

/*ARGSUSED*/
static int
segspt_shmcheckprot(struct seg *seg, caddr_t addr, u_int size, u_int prot)
{
	/*
	 * ISM segment is always rw.
	 */
	return (((PROT_ALL & prot) != prot) ? EACCES : 0);
}

/*ARGSUSED*/
static int
segspt_shmlockop(struct seg *seg, caddr_t addr, u_int len, int attr,
    int op, ulong *lockmap, size_t pos)
{
	/* ISM pages are always locked. */
	return (0);
}

/*ARGSUSED*/
static int
segspt_shmgetprot(struct seg *seg, caddr_t addr, u_int len, u_int *protv)
{
	/*
	 * ISM segment is always rw.
	 */
	*protv = PROT_ALL;
	return (0);
}

/*ARGSUSED*/
static off_t
segspt_shmgetoffset(struct seg *seg, caddr_t addr)
{
	/* Offset does not matter in ISM segment */

	return (0);
}

/*ARGSUSED*/
static int
segspt_shmgettype(struct seg *seg, caddr_t addr)
{
	/* The shared memory mapping is always MAP_SHARED */

	return (MAP_SHARED);
}

static int
segspt_shmgetvp(struct seg *seg, caddr_t addr, struct vnode **vpp)
{
	struct sptshm_data *ssd = (struct sptshm_data *)seg->s_data;
	struct as *as = ssd->sptas;
	struct spt_data *spt =
		(struct spt_data *)AS_SEGP(as, as->a_segs)->s_data;

#ifdef lint
	addr = addr;
#endif

	*vpp = spt->vp;
	return (0);
}

/*ARGSUSED*/
static int
segspt_shmadvice(struct seg *seg, caddr_t addr, u_int len, int behav)
{
	return (0);
}

/*ARGSUSED*/
static void
segspt_shmdump(struct seg *seg)
{
	/* no-op for ISM segment */
}

/*ARGSUSED*/
static int
segspt_kluster(struct seg *seg, caddr_t addr, int delta)
{
	return (0);
}

/*
 * Return architecture-specific aligned address
 * For sun4m/4d, it is 16MB.
 */
#define	SRMMU_HW_PAGESIZE 3

int
num_hw_pagesizes()
{
	return (SRMMU_HW_PAGESIZE);
}

u_int
get_hw_pagesize(n)
	u_int n;
{
	int size;

	switch (n) {
	case 0:		size = PAGESIZE;
			break;
	case 1:		size = L3PTSIZE;
			break;
	case 2: 	size = L2PTSIZE;
			break;
	default:
			size = 0;
			break;
	}

	return (size);
}

#ifdef VAC
/*
 * initialize a mutex for the list of spt address spaces
 */
void
spt_mtxinit()
{
	mutex_init(&spt_as_mtx, "spt_as_mtx", MUTEX_DEFAULT, NULL);
}

/*
 * Add entry for the sptas onto list of shm address spaces
 */
static void
spt_addsptas(struct as *as)
{
	register struct  spt_aslist *spt;

	spt = (struct spt_aslist *)
		kmem_zalloc(sizeof (struct spt_aslist), KM_SLEEP);
	spt->spt_as = as;
	spt->spt_list = NULL;
	spt->spt_count = 0;

	mutex_enter(&spt_as_mtx);
	spt->spt_next = spt_ashead;
	spt_ashead = spt;
	mutex_exit(&spt_as_mtx);
}

/*
 * Delete the requested sptas from the list of shm address spaces
 */
static void
spt_delsptas(struct as *as)
{
	register struct  spt_aslist *spt, *last;
	register struct  spt_aslist **pspt;

	mutex_enter(&spt_as_mtx);

	if (spt_ashead == NULL) {
		mutex_exit(&spt_as_mtx);
		cmn_err(CE_PANIC, "spt_delsptas: NULL spt_ashead\n");
	}

	/*
	 * Search list for this sptas
	 */
	for (last = spt = spt_ashead; spt; spt = spt->spt_next) {
		if (spt->spt_as == as) {
			if (spt == spt_ashead)
				spt_ashead = spt->spt_next;
			else
				last->spt_next = spt->spt_next;
			mutex_exit(&spt_as_mtx);
			kmem_free(spt, sizeof (struct spt_aslist));
			return;
		}
		last = spt;
	}

	mutex_exit(&spt_as_mtx);
	cmn_err(CE_PANIC, "spt_delsptas: sptas not found\n");
}

/*
 * Add entry for this segment onto list of shm address spaces
 */
static void
spt_addsptseg(struct seg *seg, struct as *sptas)
{
	register struct  spt_aslist	*spt;
	register struct  spt_seglist	*sp, *tsp;

	/*
	 * Allocate segment info
	 */
	sp = (struct spt_seglist *)
		kmem_zalloc(sizeof (struct spt_seglist), KM_SLEEP);

	/*
	 * find which sptas this segment is attached to
	 */
	mutex_enter(&spt_as_mtx);
	for (spt = spt_ashead; spt; spt = spt->spt_next) {
		if (spt->spt_as == sptas)
			break;
	}

	if (spt == NULL) {
		mutex_exit(&spt_as_mtx);
		kmem_free(sp, sizeof (struct spt_seglist));
		cmn_err(CE_PANIC, "spt_addsptseg: sptas not found\n");
	}

	/*
	 * Should not add duplicate entries
	 */
	for (tsp = spt->spt_list; tsp; tsp = tsp->spt_next) {
		if (tsp->spt_seg == seg) {
			/*
			 * duplicate entry from segspt_shmdup()
			 */
			mutex_exit(&spt_as_mtx);
			kmem_free(sp, sizeof (struct spt_seglist));
			return;
		}
	}

	/*
	 * Link the segment info
	 */
	sp->spt_seg = seg;
	sp->spt_next = spt->spt_list;
	spt->spt_list = sp;
	spt->spt_count++;
	mutex_exit(&spt_as_mtx);
}

/*
 * Delete entry for this segment from list of shm address spaces
 */
static void
spt_delsptseg(struct seg *seg, struct as *sptas)
{
	register struct  spt_aslist	*spt;
	register struct  spt_seglist	*sp, *last;


	/*
	 * find which sptas this segment is attached to
	 */
	mutex_enter(&spt_as_mtx);
	for (spt = spt_ashead; spt; spt = spt->spt_next) {
		if (spt->spt_as == sptas)
			break;
	}

	if (spt == NULL) {
		mutex_exit(&spt_as_mtx);
		cmn_err(CE_PANIC, "spt_delsptseg: sptas not found\n");
	}

	/*
	 * Remove seg info from list and deallocate
	 */
	for (sp = last = spt->spt_list; sp; sp = sp->spt_next) {
		if (sp->spt_seg == seg) {
			spt->spt_count--;
			if (sp == spt->spt_list)
				spt->spt_list = sp->spt_next;
			else
				last->spt_next = sp->spt_next;
			mutex_exit(&spt_as_mtx);
			kmem_free(sp, sizeof (struct spt_seglist));
			return;
		}
		last = sp;
	}
	mutex_exit(&spt_as_mtx);
	cmn_err(CE_PANIC, "spt_delsptseg: spt seg not found\n");
}

void
spt_vacsync(char *vaddr, struct as *sptas)
{
	register struct  spt_aslist	*spt;
	register struct  spt_seglist	*sp;
	register struct  as		*as;
	register struct  srmmu		*srmmu;
	char	*addr;
	short	ctxn;

	mutex_enter(&spt_as_mtx);

	/*
	 * find index into list of segments - from sptas
	 */
	for (spt = spt_ashead; spt; spt = spt->spt_next) {
		if (spt->spt_as == sptas)
			break;
	}

	if (spt == NULL)  {
		mutex_exit(&spt_as_mtx);
		cmn_err(CE_PANIC, "spt_vacsync: sptas not found\n");
	}

	/*
	 * If the number of attached processes is equal to or greater than
	 * max_spt_shmseg, then flush the entire cache
	 */
	if (spt->spt_count >= max_spt_shmseg)  {
		mutex_exit(&spt_as_mtx);
		XCALL_PROLOG;
		vac_allflush(FL_TLB_CACHE);
		XCALL_EPILOG;
		return;
	}

	/*
	 * now use segment info to flush vaddr in all contexts
	 */
	for (sp = spt->spt_list; sp; sp = sp->spt_next) {
		as = sp->spt_seg->s_as;
		hat_enter(as);
		srmmu = (struct srmmu *)as->a_hat->hat_data[HAT_DATA_SRMMU];
		ctxn = srmmu->s_ctx;
		if (ctxn != -1) {
			addr = sp->spt_seg->s_base + (int)vaddr;
			XCALL_PROLOG;
			srmmu_vacflush(3, addr, ctxn, FL_TLB_CACHE);
			XCALL_EPILOG;
		}
		hat_exit(as);
	}
	mutex_exit(&spt_as_mtx);
}
#endif /* VAC */
