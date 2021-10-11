/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ident  "@(#)spt_sfmmu.c 1.14     95/08/08 SMI"

#include <sys/param.h>
#include <sys/user.h>
#include <sys/mman.h>
#include <sys/kmem.h>
#include <vm/hat.h>
#include <vm/hat_sfmmu.h>
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
#include <sys/atomic_prim.h>

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

static int segspt_create(/* seg, argsp */);
static int segspt_unmap(/* seg, raddr, ssize */);
static void segspt_free(/* seg */);
static int segspt_lockop(/* seg, raddr, ssize, func */);
static int segspt_badops();
static void segspt_free_pages();
static int segspt_kluster();

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

int  	segspt_shmdup(/* seg, newseg */);
int  	segspt_shmunmap(/* seg, raddr, ssize */);
void  	segspt_shmfree(/* seg */);
faultcode_t segspt_shmfault();
int  	segspt_shmsetprot();
int  	segspt_shmcheckprot();
int  	segspt_shmbadops();
static int segspt_shmincore();
static int segspt_shmsync();
static int segspt_shmlockop();
int  	segspt_shmgetprot();
off_t 	segspt_shmgetoffset();
int   	segspt_shmgettype();
int   	segspt_shmgetvp();
static int segspt_shmadvice();
void   	segspt_shmdump();

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

/* ARGSUSED */
int
sptcreate(u_int size, struct as **pas, struct anon_map *amp)
{
	int err;
	struct as	*newas;
	struct sfmmu	*sfmmup;

	/*
	 * get a new as for this shared memory segment
	 */
	newas = *pas = as_alloc();

	sfmmup = astosfmmu(newas);
	ASSERT(sfmmup != ksfmmup);

	/*
	 * create a shared page table (spt) segment
	 */
	if (err = as_map(newas, SEGSPTADDR, size, segspt_create,
	    (caddr_t)amp)) {
		as_free(newas);
		return (err);
	}

	return (0);
}

void
sptdestroy(struct as *as, struct anon_map *amp)
{
	struct sfmmu	*sfmmup;

	sfmmup = astosfmmu(as);
	ASSERT(sfmmup != ksfmmup);

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
segspt_free(struct seg	*seg)
{
	struct spt_data *spt = (struct spt_data *)seg->s_data;

	TRACE_1(TR_FAC_VM, TR_ANON_SHM, "anon shm: seg %x", seg);

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	if (spt != NULL) {
		if (spt->realsize)
			(void) segspt_free_pages(seg, seg->s_base,
			    spt->realsize);

		if (spt->vp != NULL)
			kmem_free((caddr_t)spt->vp, sizeof (struct vnode));

		kmem_free((caddr_t)spt, sizeof (struct spt_data));
	}
}

/* ARGSUSED */
static int
segspt_shmsync(struct seg *seg, caddr_t addr, u_int len, int attr, u_int flags)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	return (0);
}

/*
 * segspt pages are always "in core" since the memory is locked down.
 */
/* ARGSUSED */
static int
segspt_shmincore(struct seg *seg, caddr_t addr,
		    u_int len, char *vec)
{
	u_int v = 0;

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	for (len = (len + PAGEOFFSET) & PAGEMASK; len; len -= PAGESIZE,
		    v += PAGESIZE)
		*vec++ = 1;

	return (v);
}

/*
 * called from as_ctl(, MC_LOCK,)
 *
 */
/* ARGSUSED */
static int
segspt_lockop(struct seg *seg, caddr_t raddr,
		u_int ssize, int func)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

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

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * seg.s_size may have been rounded up to the nearest 16MB
	 * boundary in shmat().
	 */
	share_size = get_hw_pagesize(num_hw_pagesizes() - 1);
	ssize = roundup(ssize, share_size);

	if (raddr == seg->s_base && ssize == seg->s_size) {
		hat_unshare(seg->s_as, raddr, ssize);
		seg_free(seg);
		return (0);
	} else
		cmn_err(CE_PANIC, "segspt_unmap - unexpected address \n");
	return (0);
}

int
segspt_badops()
{
	cmn_err(CE_PANIC, "segspt_badops is called");
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

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

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

		/*
		 * map the page
		 *
		 * Calling a generic hat function with sfmmu
		 * specific flag is ugly but necessary. We
		 * can get away with it since this segment driver
		 * is sfmmu specific. Without SFMMU_NO_TSBLOAD
		 * the tsb will be pre-loaded with bogus tte's.
		 *
		 * Another solution would be to change sfmmu_tteload()
		 * to not pre-load the tsb but that would defeat
		 * a otherwise worthy optimization.
		 */
		hat_memload(seg->s_as->a_hat, seg->s_as, v, pp,
				PROT_ALL, HAT_LOCK | SFMMU_NO_TSBLOAD);
		page_unlock(pp);
	}

out:
	spt->realsize = index * PAGESIZE;
	spt_used += btopr(spt->realsize);

	mutex_exit(&amp->serial_lock);
	return (err);
}

/* ARGSUSED */
void
segspt_free_pages(struct seg *seg, caddr_t addr, u_int len)
{
	struct as	*as;
	struct page 	*pp;
	struct spt_data *spt = (struct spt_data *)seg->s_data;
	struct anon_map *amp;
	u_int		npages, index;


	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

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
segspt_shmattach(struct seg *seg, caddr_t *argsp)
{

	struct sptshm_data *spt = (struct sptshm_data *)argsp;
	struct sptshm_data *ssd;
	struct anon_map *shm_amp = spt->amp;
	int error;

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	ssd = (struct sptshm_data *)kmem_zalloc((sizeof (struct sptshm_data)),
						KM_NOSLEEP);

	if (ssd == NULL)
		return (ENOMEM);

	ssd->sptas = spt->sptas;
	ssd->amp = shm_amp;
	seg->s_data = (void *)ssd;
	seg->s_ops = &segspt_shmops;

	atadd_word((u_int *)&shm_amp->refcnt, 1);

	kpreempt_disable();

	error = hat_share(seg->s_as, seg->s_base, spt->sptas, 0, seg->s_size);

	kpreempt_enable();

	return (error);
}

int
segspt_shmunmap(struct seg *seg, caddr_t raddr, u_int ssize)
{
	struct sptshm_data *ssd = (struct sptshm_data *)seg->s_data;

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	if (ssd->softlockcnt > 0)
		return (EAGAIN);

	hat_unshare(seg->s_as, raddr, ssize);
	seg_free(seg);
	return (0);
}

void
segspt_shmfree(struct seg *seg)
{
	struct sptshm_data *ssd = (struct sptshm_data *)seg->s_data;
	struct anon_map *shm_amp = ssd->amp;

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * Need to increment refcnt when attaching
	 * and decrement when detaching because of dup().
	 */
	atadd_word((u_int *)&shm_amp->refcnt, -1);

	kmem_free((caddr_t)seg->s_data, sizeof (struct sptshm_data));
}

/* ARGSUSED */
int
segspt_shmsetprot(struct seg *seg, caddr_t addr, u_int len, u_int prot)
{

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * Shared page table is more than shared mapping.
	 *  Individual process sharing page tables can't change prot
	 *  because there is only one set of page tables.
	 *  This will be allowed after private page table is
	 *  supported.
	 */
	return (0);
}

faultcode_t
segspt_shmfault(struct hat *hat, struct seg *seg, caddr_t addr,
		u_int len, enum fault_type type, enum seg_rw rw)
{
	struct seg		*sptseg;
	struct sptshm_data 	*ssd;
	struct spt_data 	*spt_ssd;
	struct as		*curspt;
	u_int			npages, anon_index, i;
	struct anon_map		*amp;
	struct page		*pp;

#ifdef lint
	rw = rw;
	hat = hat;
#endif

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	switch (type) {
	case F_SOFTUNLOCK:

		/*
		 * Check for softlock
		 */
		ssd = (struct sptshm_data *)seg->s_data;
		atadd_word((u_int *)&ssd->softlockcnt, -1);

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
		atadd_word((u_int *)&ssd->softlockcnt, 1);

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
	case F_PROT:

		/*
		 * We get here because we failed to find a valid TTE.
		 *
		 * Reasons:
		 *	1) There is a bug. (Panic)
		 *	2) The user process tried to access an address
		 *	   out of range of this segment.
		 *
		 * For reason 2 we have to check the realsize of the
		 * segment. Because of the way spt is implemented
		 * the realsize of the segment does not have to be
		 * equal to the segment size itself. The segment size
		 * is in multiples of 16MB. The realize is
		 * is rounded up to the nearest pagesize based on what
		 * the user requested. This is a bit of ungliness that
		 * is historical but not easily fixed without re-designing
		 * the higher levels of ISM.
		 */
		ssd = (struct sptshm_data *)seg->s_data;
		curspt = ssd->sptas;

		AS_LOCK_ENTER(curspt, &curspt->a_lock, RW_READER);
		sptseg = as_segat(curspt, SEGSPTADDR);
		spt_ssd = sptseg->s_data;
		if (addr >= seg->s_base &&
		    addr < (seg->s_base + spt_ssd->realsize)) {
			AS_LOCK_EXIT(curspt, &curspt->a_lock);
			if (type == F_INVAL) {
				cmn_err(CE_PANIC, "segspt_shmfault F_INVAL");
			} else {
				cmn_err(CE_PANIC, "segspt_shmfault F_PROT");
			}
		}
		AS_LOCK_EXIT(curspt, &curspt->a_lock);

		/*
		 * Invalid address.
		 */
		return (FC_NOMAP);

	default:
#ifdef DEBUG
		cmn_err(CE_WARN, "segspt_shmfault default type?");
#endif
		return (FC_NOMAP);
	}
}

int
segspt_shmbadops()
{
	cmn_err(CE_PANIC, "segspt_shmbadops is called \n");
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

	ASSERT(seg->s_as && AS_WRITE_HELD(seg->s_as, &seg->s_as->a_lock));

	nsd = (struct sptshm_data *)kmem_zalloc((sizeof (struct sptshm_data)),
						KM_NOSLEEP);

	if (nsd == NULL)
		return (ENOMEM);

	newseg->s_data = (void *) nsd;
	nsd->sptas = ssd->sptas;
	nsd->amp = amp;
	newseg->s_ops = &segspt_shmops;

	atadd_word((u_int *)&amp->refcnt, 1);

	if (hat_share(newseg->s_as, newseg->s_base, ssd->sptas,
			0, seg->s_size)) {
		cmn_err(CE_PANIC, "segspt_shmdup hat_share failed");
	}
	return (0);
}

/* ARGSUSED */
int
segspt_shmcheckprot(struct seg *seg, caddr_t addr,
		    u_int size, u_int prot)
{

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * ISM segment is always rw.
	 */
	return (((PROT_ALL & prot) != prot) ? EACCES : 0);
}

/* ARGSUSED */
static int
segspt_shmlockop(struct seg *seg, caddr_t addr, u_int len,
			int attr, int op, ulong *lockmap, size_t pos)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	/* ISM pages are always locked. */
	return (0);
}

/* ARGSUSED */
int
segspt_shmgetprot(struct seg *seg, caddr_t addr,
			u_int len, u_int *protv)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	/*
	 * ISM segment is always rw.
	 */
	*protv = PROT_ALL;
	return (0);
}

/* ARGSUSED */
off_t
segspt_shmgetoffset(struct seg *seg, caddr_t addr)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	/* Offset does not matter in ISM memory */

	return (0);
}

/* ARGSUSED */
int
segspt_shmgettype(struct seg *seg, caddr_t addr)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	/* The shared memory mapping is always MAP_SHARED */

	return (MAP_SHARED);
}

/* ARGSUSED */
int
segspt_shmgetvp(struct seg *seg, caddr_t addr, struct vnode **vpp)
{
	struct sptshm_data *ssd = (struct sptshm_data *)seg->s_data;
	struct as *as = ssd->sptas;
	struct spt_data *spt =
		(struct spt_data *)AS_SEGP(as, as->a_segs)->s_data;

	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	*vpp = spt->vp;
	return (0);
}

/* ARGSUSED */
static int
segspt_shmadvice(struct seg *seg, caddr_t addr, u_int len, int behav)
{
	ASSERT(seg->s_as && AS_READ_HELD(seg->s_as, &seg->s_as->a_lock));

	return (0);
}

/* ARGSUSED */
void
segspt_shmdump(struct seg *seg)
{
	/* no-op for ISM segment */
}

/* ARGSUSED */
int
segspt_kluster(struct seg *seg, caddr_t addr, int delta)
{
	return (0);
}

/*
 * Return architecture-specific aligned address
 * For sun4u, it is 16MB.
 * We add a fictitious 16MB page size for ISM XXX.
 */
#define	SFMMU_HW_PAGESIZES 5

int
num_hw_pagesizes()
{
	return (SFMMU_HW_PAGESIZES);
}

u_int
get_hw_pagesize(u_int n)
{
	u_int size;

	/*
	 * Basically we force it to return 16MB.
	 * XXX This should be changed in shmat().
	 */

	switch (n) {
	case 0:		size = MMU_PAGESIZE;
			break;

	case 1:		size = MMU_PAGESIZE64K;
			break;

	case 2:		size = MMU_PAGESIZE512K;
			break;

	case 3:		size = MMU_PAGESIZE4M;
			break;

	case 4:		size = MMU_PAGESIZE4M * 4; /* XXX */
			break;
	default:
			size = 0;
			break;
	}
	return (size);
}
