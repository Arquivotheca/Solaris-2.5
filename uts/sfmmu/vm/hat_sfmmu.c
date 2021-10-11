/*
 *
 * Copyright (c) 1989, 1990, 1991 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)hat_sfmmu.c 1.92     95/10/14 SMI"

/*
 * VM - Hardware Address Translation management for Spitfire MMU.
 *
 * This file implements the machine specific hardware translation
 * needed by the VM system.  The machine independent interface is
 * described in <vm/hat.h> while the machine dependent interface
 * and data structures are described in <vm/hat_sfmmu.h>.
 *
 * The hat layer manages the address translation hardware as a cache
 * driven by calls from the higher levels in the VM system.
 */

#include <sys/types.h>
#include <vm/hat.h>
#include <vm/hat_sfmmu.h>
#include <vm/page.h>
#include <sys/pte.h>
#include <sys/systm.h>
#include <sys/mman.h>
#include <sys/sysmacros.h>
#include <sys/machparam.h>
#include <sys/vtrace.h>
#include <sys/kmem.h>
#include <sys/mmu.h>
#include <sys/cmn_err.h>
#include <sys/cpu.h>
#include <sys/cpuvar.h>
#include <sys/debug.h>
#include <sys/archsystm.h>
#include <sys/machsystm.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/devpage.h>
#include <vm/seg_kp.h>
#include <vm/rm.h>
#include <sys/t_lock.h>
#include <sys/msgbuf.h>
#include <sys/obpdefs.h>
#include <sys/vm_machparam.h>
#include <sys/var.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/scb.h>
#include <sys/bitmap.h>
#include <sys/machlock.h>
#include <sys/membar.h>
#include <sys/atomic_prim.h>
#include <sys/cpu_module.h>
#include <sys/prom_debug.h>

/*
 * Private sfmmu data structures
 */
static struct ctx	*ctxhand;	/* hand used while stealing ctxs */
static struct ctx	*ctxfree;	/* head of free ctx list */

/*
 * sfmmu static variables for hmeblk resource management.
 */
static struct nucleus_memlist n_mlist[N_MLIST_NUM];
static struct kmem_cache *sfmmu8_cache;
static struct kmem_cache *sfmmu1_cache;

static struct hme_blk 	*hblk1_flist;	/* freelist for 1 hment hme_blks */
static struct hme_blk 	*hblk8_flist;	/* freelist for 8 hment hme_blks */
static struct hme_blk 	*nhblk1_flist;	/* freelist for nhme_blks (1 hmes) */
static struct hme_blk 	*nhblk8_flist;	/* freelist for nhme_blks (8 hmes) */
static int 		hblk1_avail;	/* number of free 1 hme hme_blks */
static int 		hblk8_avail;	/* number of free 8 hme hme_blks */
static int 		nhblk1_avail;	/* number of free 1 hme nhme_blks */
static int 		nhblk8_avail;	/* number of free 8 hme nhme_blks */
static kmutex_t 	hblk_lock;	/* mutex for hblk freelist */
static kmutex_t 	nhblk_lock;	/* mutex for nhblk freelist */
static kmutex_t 	ctx_lock;	/* mutex for ctx structures */
static kmutex_t 	ism_lock;	/* mutex for ism map blk cache */


/*
 * Private sfmmu subroutines
 */
static struct hme_blk *sfmmu_shadow_hcreate(struct sfmmu *, caddr_t, int);
static struct 	hme_blk *sfmmu_hblk_alloc(struct sfmmu *, caddr_t,
			struct hmehash_bucket *, int, hmeblk_tag);
static struct	hme_blk *sfmmu_nhblk_alloc(int, struct hmehash_bucket *);
static struct sf_hment *
		sfmmu_hblktohme(struct hme_blk *, caddr_t, int *);
static caddr_t	sfmmu_hblk_unload(struct as *, struct hme_blk *, caddr_t,
			caddr_t, int);
static caddr_t	sfmmu_hblk_sync(struct as *, struct hme_blk *, caddr_t,
			caddr_t, int);
static void	sfmmu_hblk_free(struct hmehash_bucket *, struct hme_blk *);
static void	sfmmu_hblk_grow(int);

static struct hme_blk *
		sfmmu_hmetohblk(struct sf_hment *);

void		sfmmu_tteload(struct hat *, tte_t *, caddr_t, struct page *,
			u_int);
u_int		sfmmu_user_vatopfn(caddr_t, struct sfmmu *);
void		sfmmu_memtte(tte_t *, u_int, u_int, int, int);
static void	sfmmu_resolve_consistency(struct page *, int);
static int	sfmmu_vac_conflict(struct as *, caddr_t, struct page *);

static u_int	sfmmu_vtop_prot(u_int, u_int *);
static struct ctx *
		sfmmu_get_ctx(struct sfmmu *);
static void	sfmmu_free_ctx(struct sfmmu *, struct ctx *);
static void	sfmmu_free_sfmmu(struct sfmmu *);
static caddr_t	sfmmu_hblk_unlock(struct hme_blk *, caddr_t, caddr_t);
static void	sfmmu_ttesync(struct as *, caddr_t, tte_t *, struct page *);
static void	sfmmu_page_cache(struct page *, int);
static void	sfmmu_tlbcache_demap(caddr_t, struct sfmmu *, int, int, int);
static void	sfmmu_cache_flush(int, int);
static void	sfmmu_tlb_demap(caddr_t, struct sfmmu *, int, int);
static void	sfmmu_tlbctx_demap(struct sfmmu *);
static void	sfmmu_ctx_demap(struct sfmmu *);
static void	sfmmu_xcall_sync(cpuset_t cpuset);
void		sfmmu_init_tsb(caddr_t, u_int);
static caddr_t	sfmmu_hblk_chgprot(struct as *, struct hme_blk *, caddr_t,
			caddr_t, u_int);
static u_int	sfmmu_ptov_prot(tte_t *);
int	sfmmu_add_nmlist(caddr_t, int);
static struct hme_blk *sfmmu_nmlist_alloc(int);
static void	sfmmu_cache_constructor(void *, size_t);
static void	sfmmu_cache_reclaim();
static void	ism_cache_constructor(void *, size_t);
static ism_map_blk_t *ism_map_blk_alloc(void);
static void	sfmmu_hblk_tofreelist(struct hme_blk *);
static void	sfmmu_reuse_ctx(struct ctx *, struct sfmmu *);

/*
 * Semi-private sfmmu data structures.  Some of them are initialize in
 * startup or in sfmmu_init. Some of them are private but accessed by
 * assembly code or mach_sfmmu.c
 */
struct hmehash_bucket *uhme_hash;	/* user hmeblk hash table */
struct hmehash_bucket *khme_hash;	/* kernel hmeblk hash table */
int 		uhmehash_num;		/* # of buckets in user hash table */
int 		khmehash_num;		/* # of buckets in kernel hash table */
caddr_t		tsb_base;		/* Base of global TSB */
caddr_t		tsb_end;		/* Adrs of last entry in TSB */
struct ctx	*ctxs, *ectxs;		/* used by <machine/mmu.c> */
u_int		nctxs = 0;		/* total number of contexts */

int		cache = 0;		/* describes system cache */
int	hblkalloc_inprog;		/* alloc nucleus hme_blk */

/*
 * ctx, hmeblk, mlistlock and other stats for sfmmu
 */
struct vmhatstat vmhatstat = {
	{ "vh_ctxfree",			KSTAT_DATA_ULONG },
	{ "vh_ctxsteal",		KSTAT_DATA_ULONG },
	{ "vh_tteload",			KSTAT_DATA_ULONG },
	{ "vh_hblk_hit",		KSTAT_DATA_ULONG },
	{ "vh_hblk_dalloc",		KSTAT_DATA_ULONG },
	{ "vh_hblk_nalloc",		KSTAT_DATA_ULONG },
	{ "vh_pgcolor_conflict",	KSTAT_DATA_ULONG },
	{ "vh_uncache_conflict",	KSTAT_DATA_ULONG },
	{ "vh_unload_conflict",		KSTAT_DATA_ULONG },
	{ "vh_mlist_enter",		KSTAT_DATA_ULONG },
	{ "vh_mlist_exit",		KSTAT_DATA_ULONG },
	{ "vh_pagesync",		KSTAT_DATA_ULONG },
	{ "vh_pagesync_invalid",	KSTAT_DATA_ULONG },
	{ "vh_itlb_misses",		KSTAT_DATA_ULONG },
	{ "vh_dtlb_misses",		KSTAT_DATA_ULONG },
	{ "vh_utsb_misses",		KSTAT_DATA_ULONG },
	{ "vh_ktsb_misses",		KSTAT_DATA_ULONG },
	{ "vh_tsb_hits",		KSTAT_DATA_ULONG },
	{ "vh_umod_faults",		KSTAT_DATA_ULONG },
	{ "vh_kmod_faults",		KSTAT_DATA_ULONG },
	{ "vh_slow_tsbmiss",		KSTAT_DATA_ULONG },
	{ "vh_pagefaults",		KSTAT_DATA_ULONG },
	{ "vh_uhash_searches",		KSTAT_DATA_ULONG },
	{ "vh_uhash_links",		KSTAT_DATA_ULONG },
	{ "vh_khash_searches",		KSTAT_DATA_ULONG },
	{ "vh_khash_links",		KSTAT_DATA_ULONG },
};

/*
 * kstat data
 */
kstat_named_t *vmhatstat_ptr = (kstat_named_t *)&vmhatstat;
ulong_t vmhatstat_ndata = sizeof (vmhatstat) / sizeof (kstat_named_t);

/*
 * Global data
 */
extern cpuset_t cpu_ready_set;
extern u_int vac_mask;
extern uint shm_alignment;
extern int do_virtual_coloring;
extern struct as kas;			/* kernel's address space */
extern int pf_is_memory(uint);
extern void trap(struct regs *, u_int, u_int, caddr_t);

struct sfmmu *ksfmmup;			/* kernel's hat id */
struct ctx *kctx;			/* kernel's context */

static void		sfmmu_init();
static void		sfmmu_alloc(struct hat *, struct as *);
static struct as 	*sfmmu_setup(struct as *, int);
static void		sfmmu_free(struct hat *, struct as *);
static void		sfmmu_swapin(struct hat *, struct as *);
static void		sfmmu_swapout(struct hat *, struct as *);
static int		sfmmu_dup(struct hat *, struct as *, struct as *);
static void		sfmmu_memload(struct hat *, struct as *, caddr_t,
				struct page *, u_int, int);
static void		sfmmu_devload(struct hat *, struct as *, caddr_t,
				devpage_t *, u_int, u_int, int);
static void		sfmmu_contig_memload(struct hat *, struct as *,
				caddr_t, struct page *, u_int, int, u_int);
static void		sfmmu_contig_devload(struct hat *, struct as *,
				caddr_t, devpage_t *, u_int, u_int, int, u_int);
static void		sfmmu_unlock(struct hat *, struct as *, caddr_t, u_int);
static faultcode_t	sfmmu_fault(struct hat *, caddr_t);
static int		sfmmu_probe(struct hat *, struct as *, caddr_t);
static int		sfmmu_share(struct as *, caddr_t, struct as *,
				caddr_t, u_int);
static void		sfmmu_unshare(struct as *, caddr_t, u_int);
void			sfmmu_chgprot(struct as *, caddr_t, u_int, u_int);
static void		sfmmu_unload(struct as *, caddr_t, u_int, int);
static void		sfmmu_sync(struct as *, caddr_t, u_int, u_int);
static void		sfmmu_pageunload(struct page *, struct sf_hment *);
static void		sfmmu_sys_pageunload(struct page *, struct hment *);

static void		sfmmu_pagesync(struct hat *, struct page *,
				struct sf_hment *, u_int);

static int		sfmmu_sys_pagesync(struct hat *, struct page *,
				struct hment *, u_int);
static void		sfmmu_pagecachectl(struct page *, u_int);
static void		sfmmu_sys_pagecachectl(struct page *, u_int);
static u_int		sfmmu_getkpfnum(caddr_t);
u_int			sfmmu_getpfnum(struct as *, caddr_t);
static int		sfmmu_map(struct hat *, struct as *, caddr_t, u_int,
				int);
static void		sfmmu_unmap(struct as *, caddr_t, u_int, int);
static void		sfmmu_lock_init(void);
void			sfmmu_reserve(struct as *, caddr_t, u_int, u_int);
void			sfmmu_reserve_check(struct as *, caddr_t, u_int);
u_int			sfmmu_getprot(struct hat *, struct as *, caddr_t);
int			sfmmu_get_ppvcolor(struct page *);
int			sfmmu_get_addrvcolor(caddr_t);

struct hatops sfmmu_hatops = {
	sfmmu_init,
	sfmmu_alloc,
	sfmmu_setup,
	sfmmu_free,
	sfmmu_dup,
	sfmmu_swapin,
	sfmmu_swapout,
	sfmmu_memload,
	sfmmu_devload,
	sfmmu_contig_memload,
	sfmmu_contig_devload,
	sfmmu_unlock,
	sfmmu_fault,
	sfmmu_chgprot,
	sfmmu_unload,
	sfmmu_sync,
	sfmmu_sys_pageunload,
	sfmmu_sys_pagesync,
	sfmmu_sys_pagecachectl,
	sfmmu_getkpfnum,
	sfmmu_getpfnum,
	sfmmu_map,
	sfmmu_probe,
	sfmmu_lock_init,
	sfmmu_share,
	sfmmu_unshare,
	sfmmu_unmap
};

/* sfmmu locking operations */
static void	sfmmu_page_enter(struct page *);
static void	sfmmu_page_exit(struct page *);
static void	sfmmu_hat_enter(struct as *);
static void	sfmmu_hat_exit(struct as *);
static void	sfvec_mlist_enter(struct page *);
static void	sfvec_mlist_exit(struct page *);
static int	sfmmu_mlist_held(struct page *);

struct hat_lockops sfmmu_lockops = {
	sfmmu_page_enter,
	sfmmu_page_exit,
	sfmmu_hat_enter,
	sfmmu_hat_exit,
	sfvec_mlist_enter,
	sfvec_mlist_exit,
	sfmmu_mlist_held
};

/* sfmmu internal locking operations - accessed directly */
static kmutex_t	*sfmmu_mlist_enter(struct page *);
static void	sfmmu_mlist_exit(kmutex_t *);

/* array of mutexes protecting a page's mapping list and p_nrm field */
#define	MLIST_SIZE		(0x40)
#define	MLIST_HASH(pp)		&mml_table[(((u_int)(pp))>>6) & (MLIST_SIZE-1)]
kmutex_t			mml_table[MLIST_SIZE];

/*
 * Flags for VAC consistency operations
 */
#define	NO_CONFLICT		0
#define	UNLOAD_CONFLICT		1
#define	UNCACHE_CONFLICT	2
#define	PGCOLOR_CONFLICT	4

/*
 * Move to some cache specific header file - XXX
*/
#define	VAC_ALIGNED(a1, a2) ((((u_int)(a1) ^ (u_int)(a2)) & vac_mask) == 0)

extern int ecache_linesize;

#ifdef DEBUG

struct ctx_trace stolen_ctxs[TRSIZE];
struct ctx_trace *ctx_trace_first = &stolen_ctxs[0];
struct ctx_trace *ctx_trace_last = &stolen_ctxs[TRSIZE-1];
struct ctx_trace *ctx_trace_ptr = &stolen_ctxs[0];
u_int	num_ctx_stolen = 0;

int	ism_debug = 0;

#endif /* DEBUG */



/*
 * Initialize the hardware address translation structures.
 * Called by hat_init() after the vm structures have been allocated
 * and mapped in.
 */
static void
sfmmu_init()
{
	register struct ctx	*ctx;
	register struct ctx	*cur_ctx = NULL;
	int 			i, j, one_pass_over;

	extern struct hatops	*sys_hatops;

	/* Initialize the hash locks */
	for (i = 0; i < khmehash_num; i++) {
		mutex_init(&khme_hash[i].hmehash_mutex, "khmehash_lock",
			MUTEX_DEFAULT, NULL);
	}
	for (i = 0; i < uhmehash_num; i++) {
		mutex_init(&uhme_hash[i].hmehash_mutex, "uhmehash_lock",
			MUTEX_DEFAULT, NULL);
	}
	khmehash_num--;		/* make sure counter starts from 0 */
	uhmehash_num--;		/* make sure counter starts from 0 */

	/*
	 * Initialize ctx structures
	 * We keep a free list of ctxs. That will be used to get/free ctxs.
	 * The first NUM_LOCKED_CTXS (0, .. NUM_LOCKED_CTXS-1)
	 * contexts are always not available. The rest of the contexts
	 * are put in a free list in the following fashion:
	 * Adjacent ctxs are not chained together - every (CTX_GAP)th one
	 * is chained next to each other. This results in a better hashing
	 * on ctxs at the begining. Later on the free list becomes random
	 * as processes exit randomly.
	 */
	kctx = &ctxs[KCONTEXT];
	ctx = &ctxs[NUM_LOCKED_CTXS];
	ctxhand = ctxfree = ctx;		/* head of free list */
	one_pass_over = 0;
	for (j = 0; j < CTX_GAP; j++) {
		for (i = NUM_LOCKED_CTXS + j; i < nctxs; i = i + CTX_GAP) {
			if (one_pass_over) {
				cur_ctx->c_free = &ctxs[i];
				cur_ctx->c_refcnt = 0;
				one_pass_over = 0;
			}
			cur_ctx = &ctxs[i];
			if ((i + CTX_GAP) < nctxs) {
				cur_ctx->c_free = &ctxs[i + CTX_GAP];
				cur_ctx->c_refcnt = 0;
			}
		}
		one_pass_over = 1;
	}
	cur_ctx->c_free = NULL;		/* tail of free list */

	sys_hatops = &sfmmu_hatops;

	sfmmu8_cache = kmem_cache_create("sfmmu8_cache", HME8BLK_SZ,
		HMEBLK_ALIGN, sfmmu_cache_constructor, NULL,
		sfmmu_cache_reclaim);

	sfmmu1_cache = kmem_cache_create("sfmmu1_cache", HME1BLK_SZ,
		HMEBLK_ALIGN, sfmmu_cache_constructor, NULL, NULL);
}

static void
sfmmu_cache_constructor(void *buf, size_t size)
{
	bzero(buf, size);
}

/*
 * The kmem allocator will callback into our reclaim routine when the system
 * is running low in memory.  We traverse the hash and free up all unused but
 * still cached hme_blks.  We also traverse the free list and free them up
 * as well.
 */
static void
sfmmu_cache_reclaim()
{
	int i;
	struct hmehash_bucket *hmebp;
	struct hme_blk *hmeblkp, *nx_hblk, *pr_hblk = NULL;
	extern struct hme_blk   *hblk1_flist;
	extern struct hme_blk   *hblk8_flist;
	extern int		hblk1_avail;
	extern int		hblk8_avail;

	for (i = 0; i <= UHMEHASH_SZ; i++) {
		hmebp = &uhme_hash[i];

		SFMMU_HASH_LOCK(hmebp);
		hmeblkp = hmebp->hmeblkp;
		while (hmeblkp) {
			nx_hblk = hmeblkp->hblk_next;
			if (!hmeblkp->hblk_vcnt && !hmeblkp->hblk_hmecnt) {
				sfmmu_hblk_hash_rm(hmebp, hmeblkp, pr_hblk);
				sfmmu_hblk_free(hmebp, hmeblkp);
			} else {
				pr_hblk = hmeblkp;
			}
			hmeblkp = nx_hblk;
		}
		SFMMU_HASH_UNLOCK(hmebp);
	}

	for (i = 0; i <= KHMEHASH_SZ; i++) {
		hmebp = &khme_hash[i];

		SFMMU_HASH_LOCK(hmebp);
		hmeblkp = hmebp->hmeblkp;
		while (hmeblkp) {
			nx_hblk = hmeblkp->hblk_next;
			if (!hmeblkp->hblk_vcnt && !hmeblkp->hblk_hmecnt) {
				sfmmu_hblk_hash_rm(hmebp, hmeblkp, pr_hblk);
				sfmmu_hblk_free(hmebp, hmeblkp);
			} else {
				pr_hblk = hmeblkp;
			}
			hmeblkp = nx_hblk;
		}
		SFMMU_HASH_UNLOCK(hmebp);
	}
	/*
	 * kmem free all free hme_blks
	 */
	while (hblk8_avail > HME8_TRHOLD) {
		HBLK_FLIST_LOCK();
		if (hblk8_avail <= HME8_TRHOLD) {
			/*
			 * No surplus to free.
			 */
			HBLK_FLIST_UNLOCK();
			break;
		}
		hmeblkp = hblk8_flist;
		ASSERT(hmeblkp);
		hblk8_flist = hmeblkp->hblk_next;
		hblk8_avail--;
		HBLK_FLIST_UNLOCK();
		/*
		 * We need to drop the free list lock because kmem
		 * can recall the hat and try to allocate am hme_blk
		 */
		kmem_cache_free(sfmmu8_cache, hmeblkp);
	}
	while (hblk1_avail > HME1_TRHOLD) {
		HBLK_FLIST_LOCK();
		if (hblk1_avail <= HME1_TRHOLD) {
			/*
			 * No surplus to free.
			 */
			HBLK_FLIST_UNLOCK();
			break;
		}
		hmeblkp = hblk1_flist;
		ASSERT(hmeblkp);
		hblk1_flist = hmeblkp->hblk_next;
		hblk1_avail--;
		HBLK_FLIST_UNLOCK();
		/*
		 * We need to drop the free list lock because kmem
		 * can recall the hat and try to allocate am hme_blk
		 */
		kmem_cache_free(sfmmu1_cache, hmeblkp);
	}
#ifdef DEBUG
	/*
	 * Debug code that verifies hblk lists are correct
	 */
	HBLK_FLIST_LOCK();
	for (i = 0, hmeblkp = hblk8_flist; hmeblkp;
			hmeblkp = hmeblkp->hblk_next, i++);

	if (i != hblk8_avail) {
		cmn_err(CE_PANIC,
			"sfmmu_reclaim: inconsistent hblk8_flist");
	}
	for (i = 0, hmeblkp = hblk1_flist; hmeblkp;
			hmeblkp = hmeblkp->hblk_next, i++);

	if (i != hblk1_avail) {
		cmn_err(CE_PANIC,
			"sfmmu_reclaim: inconsistent hblk1_flist");
	}
	HBLK_FLIST_UNLOCK();
#endif /* DEBUG */
}

static void
sfmmu_alloc(struct hat *hat, struct as *as)
{
	struct sfmmu *sfmmup;
	struct ctx *ctx;
	extern u_int get_color_start(struct as *);

	sfmmup = hattosfmmu(hat);

	if (as == &kas) {			/* XXX - 1 time only */
		ctx = kctx;
		ksfmmup = sfmmup;
		sfmmup->sfmmu_cnum = ctxtoctxnum(ctx);
		ctx->c_sfmmu = sfmmup;
		sfmmup->sfmmu_clrstart = 0;
	} else {

		/*
		 * Just set to invalid ctx. When it faults, it will
		 * get a valid ctx. This would avoid the situation
		 * where we get a ctx, but it gets stolen and then
		 * we fault when we try to run and so have to get
		 * another ctx.
		 */
		sfmmup->sfmmu_cnum = INVALID_CONTEXT;
		/* initialize original physical page coloring bin */
		sfmmup->sfmmu_clrstart = get_color_start(as);
	}
	sfmmup->sfmmu_lttecnt = 0;
	sfmmup->sfmmu_ismblk = NULL;
	sfmmup->sfmmu_free = 0;
	sfmmup->sfmmu_clrbin = sfmmup->sfmmu_clrstart;
	CPUSET_ADD(sfmmup->sfmmu_cpusran, CPU->cpu_id);
}

/*
 * Called to make the passed as the current one.  In sfmmu this translates
 * to setting the secondary context with the corresponding context.
 */
static struct as *
sfmmu_setup(struct as *as, int allocflag)
{
	struct ctx *ctx;
	struct sfmmu *sfmmup;
	u_int ctx_num;

#ifdef lint
	allocflag = allocflag;			/* allocflag is not used */
#endif /* lint */

	sfmmup = astosfmmu(as);
	/*
	 * Make sure that we have a valid ctx and it doesn't get stolen
	 * after this point.
	 */
	if (sfmmup != ksfmmup)
		sfmmu_disallow_ctx_steal(sfmmup);

	ctx = sfmmutoctx(sfmmup);
	CPUSET_ADD(sfmmup->sfmmu_cpusran, CPU->cpu_id);
	ctx_num = ctxtoctxnum(ctx);
	ASSERT(sfmmup == ctx->c_sfmmu);

	/* curiosity check - delete someday */
	if (as == &kas) {
		cmn_err(CE_PANIC, "sfmmu_setup called with kas");
	}

	ASSERT(ctx_num);
	sfmmu_setctx_sec(ctx_num);

	/*
	 * Allow ctx to be stolen.
	 */
	if (sfmmup != ksfmmup)
		sfmmu_allow_ctx_steal(sfmmup);

	return (NULL);
}

/*
 * Free all the translation resources for the specified address space.
 * Called from as_free when an address space is being destroyed.
 */
static void
sfmmu_free(struct hat *hat, struct as *as)
{
	struct sfmmu *sfmmup;

	ASSERT(as->a_hat == hat);

	sfmmup = hattosfmmu(hat);

	sfmmup->sfmmu_free = 1;

	/*
	 * If there are no active xlations, free up ctx and sfmmu.
	 *
	 * NOTE: If called thru as_free as the result of a failed
	 *	 fork of an ISM process and the spt segment has already
	 *	 been dup'ed then this sfmmu will have an ism_map_blk. We
	 *	 could clean it up here but instead will let the segment
	 *	 driver do the work by calling hat_unshare in unmap.
	 */
	if (!as->a_rss) {
		sfmmu_tlbctx_demap(sfmmup);
		sfmmu_xcall_sync(sfmmup->sfmmu_cpusran);
		sfmmu_free_ctx(sfmmup, sfmmutoctx(sfmmup));
		sfmmu_free_sfmmu(sfmmup);
	}
}

/*
 * Set up any translation structures, for the specified address space,
 * that are needed or preferred when the process is being swapped in.
 */
static void
sfmmu_swapin(struct hat *hat, struct as *as)
{
#ifdef lint
	hat = hat;
	as = as;
#endif
}

/*
 * Free all of the translation resources, for the specified address space,
 * that can be freed while the process is swapped out. Called from as_swapout.
 * Also, free up the ctx that this process was using.
 */
static void
sfmmu_swapout(struct hat *hat, struct as *as)
{
	struct hmehash_bucket *hmebp;
	struct sfmmu *sfmmup;
	struct hme_blk *hmeblkp;
	struct hme_blk *pr_hblk = NULL;
	struct hme_blk *nx_hblk;
	struct ctx *ctx;
	int i;

	/*
	 * There is no way to go from an as to all its translations in sfmmu.
	 * Here is one of the times when we take the big hit and traverse
	 * the hash looking for hme_blks to free up.  Not only do we free up
	 * this as hme_blks but all those that are free.  We are obviously
	 * swaping because we need memory so let's free up as much
	 * as we can.
	 */
	sfmmup = hattosfmmu(hat);
	ASSERT(sfmmup != KHATID);
	for (i = 0; i <= UHMEHASH_SZ; i++) {
		hmebp = &uhme_hash[i];

		SFMMU_HASH_LOCK(hmebp);
		hmeblkp = hmebp->hmeblkp;
		while (hmeblkp) {
			if ((hmeblkp->hblk_tag.htag_id == sfmmup) &&
			    !hmeblkp->hblk_shw_bit) {
				sfmmu_hblk_unload(as, hmeblkp,
					(caddr_t)get_hblk_base(hmeblkp),
					get_hblk_endaddr(hmeblkp), HAT_UNLOAD);
			}
			nx_hblk = hmeblkp->hblk_next;
			if (!hmeblkp->hblk_vcnt && !hmeblkp->hblk_hmecnt) {
				sfmmu_hblk_hash_rm(hmebp, hmeblkp, pr_hblk);
				sfmmu_hblk_free(hmebp, hmeblkp);
			} else {
				pr_hblk = hmeblkp;
			}
			hmeblkp = nx_hblk;
		}
		SFMMU_HASH_UNLOCK(hmebp);
	}

	/*
	 * Now free up the ctx so that others can reuse it.
	 */
	mutex_enter(&ctx_lock);
	ctx = sfmmutoctx(sfmmup);

	if (sfmmup->sfmmu_cnum != INVALID_CONTEXT &&
		rwlock_hword_enter(&ctx->c_refcnt, WRITER_LOCK) == 0) {
		sfmmu_reuse_ctx(ctx, sfmmup);
		/*
		 * Put ctx back to the free list.
		 */
		ctx->c_free = ctxfree;
		ctxfree = ctx;
		rwlock_hword_exit(&ctx->c_refcnt, WRITER_LOCK);
	}
	mutex_exit(&ctx_lock);

}

static int
sfmmu_dup(struct hat *hat, struct as *as, struct as *newas)
{
#ifdef lint
	hat = hat; as = as; newas = newas;
#endif
	return (0);
}

/*
 * Set up addr to map to page pp with protection prot.
 * As an optimization we also load the TSB with the
 * corresponding tte but it is no big deal if  the tte gets kicked out.
 */
static void
sfmmu_memload(struct hat *hat, struct as *as, caddr_t addr, struct page *pp,
	u_int prot, int flags)
{
	tte_t tte;

	ASSERT(as != NULL);
	ASSERT(se_assert(&pp->p_selock));
	ASSERT(!((uint)addr & MMU_PAGEOFFSET));

	if (!pp && (cache & CACHE_VAC)) {
		flags |= SFMMU_UNCACHEVTTE;
	}
	sfmmu_memtte(&tte, pp->p_pagenum, prot, flags, TTE8K);
	sfmmu_tteload(hat, &tte, addr, pp, flags);
}

/*
 * hat_devload can be called to map real memory (e.g.
 * /dev/kmem) and even though hat_devload will determine pf is
 * for memory, it will be unable to get a shared lock on the
 * page (because someone else has it exclusively) and will
 * pass dp = NULL.  If tteload doesn't get a non-NULL
 * page pointer it can't cache memory.
 */
static void
sfmmu_devload(struct hat *hat, struct as *as, caddr_t addr, devpage_t *dp,
	u_int pf, u_int prot, int flags)
{
	tte_t tte;
#ifdef lint
	as  = as;
#endif /* lint */

	if (flags & HAT_NOCONSIST) {
		flags |= SFMMU_UNCACHEVTTE;
	}
	if (!pf_is_memory(pf)) {
		flags |= SFMMU_UNCACHEPTTE;
		if (!(flags & (HAT_MERGING_OK | HAT_LOADCACHING_OK |
		    HAT_STORECACHING_OK))) {
			/*
			 * we set the side effect bit for all non memory
			 * mappings unless merging is ok
			 */
			flags |= SFMMU_SIDEFFECT;
		}
	}
	sfmmu_memtte(&tte, pf, prot, flags, TTE8K);
	sfmmu_tteload(hat, &tte, addr, dp, flags);
}

/*
 * Set up translations to a range of virtual addresses backed by
 * physically contiguous memory.
 */
static void
sfmmu_contig_memload(struct hat *hat, struct as *as, caddr_t addr,
	struct page *pp, u_int prot, int flags, u_int len)
{
	register caddr_t a;
	register struct page *tmp_pp = pp;

	ASSERT((len & MMU_PAGEOFFSET) == 0);

	/*
	 * It is the caller's responsibility to ensure that the page is
	 * locked down and its  identity does not change.
	 * XXX - use large pages, check va/pa alignments
	 */
	for (a = addr; a < addr + len; a += MMU_PAGESIZE, tmp_pp++) {
		/*
		 * We assume here that the page structs are contiguous
		 * and that they map physically contiguous memory
		 */
		sfmmu_memload(hat, as, a, tmp_pp, prot, flags);
	}
}

/*
 * Set up translations to a range of virtual addresses backed by
 * physically contiguous device memory.  'pf' is the base physical page
 * frame number of the physically contiguous memory and 'dp' is
 * always NULL.
 */
/* ARGSUSED */
static void
sfmmu_contig_devload(struct hat *hat, struct as *as, caddr_t addr,
	devpage_t *dp, u_int pf, u_int prot, int flags, u_int len)
{
	tte_t tte;

	ASSERT(dp == NULL);

	if (pf_is_memory(pf)) {
		cmn_err(CE_PANIC, "sfmmu_contig_devload: pf is memory\n");
	}

	/* device memory is always uncacheable */
	flags |= SFMMU_UNCACHEPTTE | SFMMU_UNCACHEVTTE | HAT_NOSYNCLOAD;
	if (!(flags & (HAT_MERGING_OK | HAT_LOADCACHING_OK |
		HAT_STORECACHING_OK))) {
			/*
			 * we set the side effect bit for all non memory
			 * mappings unless merging is ok
			 */
			flags |= SFMMU_SIDEFFECT;
	}

	/*
	 *  use large pages, check va/pa alignments
	 */
	while (len) {
		if (len >= MMU_PAGESIZE4M && !((int)addr & MMU_PAGEOFFSET4M) &&
		    !(mmu_ptob(pf) & MMU_PAGEOFFSET4M)) {
			sfmmu_memtte(&tte, pf, prot, flags, TTE4M);
			sfmmu_tteload(hat, &tte, addr, dp, flags);
			len -= MMU_PAGESIZE4M;
			addr += MMU_PAGESIZE4M;
			pf += MMU_PAGESIZE4M / MMU_PAGESIZE;
		}
		else
		if (len >= MMU_PAGESIZE512K &&
		    !((int)addr & MMU_PAGEOFFSET512K) &&
		    !(mmu_ptob(pf) & MMU_PAGEOFFSET512K)) {
			sfmmu_memtte(&tte, pf, prot, flags, TTE512K);
			sfmmu_tteload(hat, &tte, addr, dp, flags);
			len -= MMU_PAGESIZE512K;
			addr += MMU_PAGESIZE512K;
			pf += MMU_PAGESIZE512K / MMU_PAGESIZE;
		}
		else
		if (len >= MMU_PAGESIZE64K &&
		    !((int)addr & MMU_PAGEOFFSET64K) &&
		    !(mmu_ptob(pf) & MMU_PAGEOFFSET64K)) {
			sfmmu_memtte(&tte, pf, prot, flags, TTE64K);
			sfmmu_tteload(hat, &tte, addr, dp, flags);
			len -= MMU_PAGESIZE64K;
			addr += MMU_PAGESIZE64K;
			pf += MMU_PAGESIZE64K / MMU_PAGESIZE;
		} else {
			sfmmu_memtte(&tte, pf, prot, flags, TTE8K);
			sfmmu_tteload(hat, &tte, addr, dp, flags);
			len -= MMU_PAGESIZE;
			addr += MMU_PAGESIZE;
			pf++;
		}
	}
}

/*
 * Construct a tte for a page:
 *
 * tte_valid = 1
 * tte_size = size
 * tte_nfo = 0
 * tte_ie = 0
 * tte_hmenum = hmenum
 * tte_pahi = pp->p_pagenum >> TTE_PASHIFT;
 * tte_palo = pp->p_pagenum & TTE_PALOMASK;
 * tte_ref = 1 (optimization)
 * tte_wr_perm = PROT_WRITE & vprot;
 * tte_not_cons = 0
 * tte_nc_pref = 0
 * tte_no_sync = flags & HAT_NOSYNCLOAD
 * tte_lock = flags & SFMMU_LOCKTTE
 * tte_cp = !(flags & SFMMU_UNCACHEPTTE)
 * tte_cv = !(flags & SFMMU_UNCACHEVTTE)
 * tte_e = 0
 * tte_priv = !(PROT_USER & vprot)
 * tte_hwwr = if nosync is set and it is writable we set the mod bit (opt)
 * tte_glb = 0
 */
void
sfmmu_memtte(tte_t *ttep, u_int pfn, u_int vprot, int flags, int tte_sz)
{

	if ((vprot & PROT_ALL) != vprot) {
		cmn_err(CE_PANIC, "memtte -- bad prot %x\n", vprot);
	}
	ttep->tte_inthi =
		MAKE_TTE_INTHI(tte_sz, 0, pfn, flags);   /* hmenum = 0 */

	ttep->tte_intlo = MAKE_TTE_INTLO(pfn, vprot, flags);
	if (TTE_IS_NOSYNC(ttep)) {
		TTE_SET_REF(ttep);
		if (TTE_IS_WRITABLE(ttep)) {
			TTE_SET_MOD(ttep);
		}
	}
}

/*
 * This function will add a translation to the hme_blk and allocate the
 * hme_blk if one does not exist.
 * If a page structure is specified then it will add the
 * corresponding hment to the mapping list.
 * It will also update the hmenum field for the tte.
 */
void
sfmmu_tteload(struct hat *hat, tte_t *ttep, caddr_t vaddr, struct page *pp,
	u_int flags)
{
	struct hmehash_bucket *hmebp;
	struct sfmmu *sfmmup;
	hmeblk_tag hblktag;
	int hmeshift, hmenum, size, cnt;
	int remap = 0;
	struct hme_blk *hmeblkp, *nx_hblk, *pr_hblk = NULL;
	tte_t tteold;
	struct sf_hment *sfhme;
	u_int conflict;
	kmutex_t *pml;
	struct ctx *ctx;

	/*
	 * remove this panic when we decide to let user virtual address
	 * space be >= USERLIMIT.
	 */
	if (!TTE_IS_PRIVILEGED(ttep) && (u_int)vaddr >= USERLIMIT) {
			cmn_err(CE_PANIC, "user addr %x in kernel space\n",
				vaddr);
	}
	if (TTE_IS_GLOBAL(ttep)) {
		cmn_err(CE_PANIC, "sfmmu_tteload: creating global tte\n");
	}

#ifdef	DEBUG
	if (pf_is_memory(sfmmu_ttetopfn(ttep, vaddr)) &&
	    !TTE_IS_PCACHEABLE(ttep)) {
		cmn_err(CE_PANIC, "sfmmu_tteload: non cacheable memory tte\n");
	}
#endif /* DEBUG */

	tteold.tte_inthi = 0;
	tteold.tte_intlo = 0;
	SFMMU_STAT(vh_tteload);
	sfmmup = hattosfmmu(hat);


	size = ttep->tte_size;
	ASSERT(!((uint)vaddr & TTE_PAGE_OFFSET(size)));

	hblktag.htag_id = sfmmup;
	hmeshift = HME_HASH_SHIFT(size);
	hblktag.htag_bspage = HME_HASH_BSPAGE(vaddr, hmeshift);
	hblktag.htag_rehash = HME_HASH_REHASH(size);
	hmebp = HME_HASH_FUNCTION(sfmmup, vaddr, hmeshift);

	SFMMU_HASH_LOCK(hmebp);

	HME_HASH_SEARCH(hmebp, hblktag, hmeblkp, nx_hblk, pr_hblk);
	if (hmeblkp == NULL) {
		hmeblkp = sfmmu_hblk_alloc(sfmmup, vaddr, hmebp, size, hblktag);
	} else {
		SFMMU_STAT(vh_hblk_hit);
	}
	ASSERT(!hmeblkp->hblk_shw_bit);
	sfhme = sfmmu_hblktohme(hmeblkp, vaddr, &hmenum);

	/*
	 * Need to grab mlist lock here so that pageunload
	 * will not change tte behind us.
	 */
	if (pp) {
		pml = sfmmu_mlist_enter(pp);
	}

	MMU_COPYTTE(&sfhme->hme_tte, &tteold);
	/*
	 * Look for corresponding hment and if valid verify
	 * pfns are equal.
	 */
	remap = TTE_IS_VALID(&tteold);
	if (remap) {
		if (TTE_TO_PFN(vaddr, &tteold) != TTE_TO_PFN(vaddr, ttep)) {
			SFMMU_HASH_UNLOCK(hmebp);
			cmn_err(CE_PANIC, "sfmmu_tteload - tte remap,"
				"hmeblkp 0x%x\n", hmeblkp);
		}
	}
	/*
	 * Make sure hment is not on a mapping list.
	 */
	ASSERT(remap || (sfhme->hme_page == NULL));

	/* if it is not a remap then hme->next better be NULL */
	ASSERT((!remap) ? sfhme->hme_next == NULL : 1);

	if (flags & HAT_LOCK) {
		if (((int)hmeblkp->hblk_lckcnt + 1) >= MAX_HBLK_LCKCNT) {
			cmn_err(CE_PANIC,
				"too high lckcnt-hmeblk = 0x%x\n", hmeblkp);
		}
		atadd_hword(&hmeblkp->hblk_lckcnt, 1);
	}

	if (pp && !remap) {
		/*
		 * Handle VAC consistency
		 */
		if ((cache & CACHE_VAC) && !PP_ISNC(pp)) {
			conflict = sfmmu_vac_conflict(hat->hat_as, vaddr, pp);
			if (conflict) {
				sfmmu_resolve_consistency(pp, conflict);
			}
		}
	}
	if (pp && PP_ISNC(pp)) {
		/*
		 * If the physical page is marked to be unencacheable, like
		 * by a vac conflict, make sure the new mapping is also
		 * unencacheable.
		 */
		TTE_CLR_VCACHEABLE(ttep);
	}
	sfhme->hme_hat = hat - hats;
	ttep->tte_hmenum = hmenum;
	MMU_MODIFYTTE(&tteold, ttep, &sfhme->hme_tte);
	if (!TTE_IS_VALID(&tteold)) {
		cnt = TTEPAGES(size);
		atadd_hword(&hmeblkp->hblk_vcnt, 1);
		atadd_word((u_int *)&hat->hat_as->a_rss, cnt);
		if (size != TTE8K) {

			/*
			 * Make sure that we have a valid ctx and
			 * it doesn't get stolen after this point.
			 */
			if (sfmmup != ksfmmup)
				sfmmu_disallow_ctx_steal(sfmmup);

			ctx = sfmmutoctx(sfmmup);
			atadd_hword(&sfmmup->sfmmu_lttecnt, 1);
			ctx->c_flags |= LTTES_FLAG;
			/*
			 * Now we can allow our ctx to be stolen.
			 */
			if (sfmmup != ksfmmup)
				sfmmu_allow_ctx_steal(sfmmup);
		}
	}

	SFMMU_STACK_TRACE(hmeblkp, hmenum)

	if (remap && !((tteold.tte_intlo == ttep->tte_intlo) &&
	    (tteold.tte_inthi == ttep->tte_inthi))) {
		/*
		 * If remap and new tte differs from old tte we need
		 * to sync the mod bit and flush tlb/tsb.  We don't
		 * need to sync ref bit because we currently always set
		 * ref bit in tteload.
		 */
		ASSERT(TTE_IS_REF(ttep));
		if (TTE_IS_MOD(&tteold)) {
			sfmmu_ttesync(hat->hat_as, vaddr, &tteold, pp);
		}
		sfmmu_tlb_demap(vaddr, sfmmup, size, 0);
		sfmmu_xcall_sync(sfmmup->sfmmu_cpusran);
	}

	if ((size == TTE8K) && !(flags & SFMMU_NO_TSBLOAD)) {

		/*
		 * Make sure that we have a valid ctx and
		 * it doesn't get stolen after this point.
		 */
		if (sfmmup != ksfmmup)
			sfmmu_disallow_ctx_steal(sfmmup);

		sfmmu_load_tsb(vaddr, sfmmup->sfmmu_cnum, ttep);

		/*
		 * Now we can allow our ctx to be stolen.
		 */
		if (sfmmup != ksfmmup)
			sfmmu_allow_ctx_steal(sfmmup);
	}
	if (pp) {
		if (!remap) {
			PP_SET_VCOLOR(pp, addr_to_vcolor(vaddr));
			hme_add((struct hment *)sfhme, pp);
			atadd_hword(&hmeblkp->hblk_hmecnt, 1);
			ASSERT(hmeblkp->hblk_hmecnt > 0);
			ASSERT(hmeblkp->hblk_hmecnt <= NHMENTS);
		}
		sfmmu_mlist_exit(pml);
	}

	SFMMU_HASH_UNLOCK(hmebp);
}

/*
 * creates a large page shadow hmeblk for a tte.
 * The purpose of this routine is to allow us to do quick unloads because
 * the vm layer can easily pass a very large but sparsely populated range.
 */
static struct hme_blk *
sfmmu_shadow_hcreate(struct sfmmu *sfmmup, caddr_t vaddr, int ttesz)
{
	struct hmehash_bucket *hmebp;
	hmeblk_tag hblktag;
	int hmeshift, size, vshift;
	uint shw_mask, newshw_mask;
	struct hme_blk *hmeblkp;
	extern int cas();

	ASSERT(sfmmup != KHATID);
	ASSERT(ttesz < TTE4M);

	size = (ttesz == TTE8K)? TTE512K : ++ttesz;

	hblktag.htag_id = sfmmup;
	hmeshift = HME_HASH_SHIFT(size);
	hblktag.htag_bspage = HME_HASH_BSPAGE(vaddr, hmeshift);
	hblktag.htag_rehash = HME_HASH_REHASH(size);
	hmebp = HME_HASH_FUNCTION(sfmmup, vaddr, hmeshift);

	SFMMU_HASH_LOCK(hmebp);

	HME_HASH_FAST_SEARCH(hmebp, hblktag, hmeblkp);
	if (hmeblkp == NULL) {
		hmeblkp = sfmmu_hblk_alloc(sfmmup, vaddr, hmebp, size, hblktag);
		hmeblkp->hblk_shw_bit = 1;
	}
	ASSERT(hmeblkp);
	ASSERT(hmeblkp->hblk_shw_bit == 1);
	vshift = vaddr_to_vshift(hblktag, vaddr, size);
	ASSERT(vshift < 8);
	/*
	 * Atomically set shw mask bit
	 */
	do {
		shw_mask = hmeblkp->hblk_shw_mask;
		newshw_mask = shw_mask | (1 << vshift);
		newshw_mask = cas(&hmeblkp->hblk_shw_mask, shw_mask,
			newshw_mask);
	} while (newshw_mask != shw_mask);

	SFMMU_HASH_UNLOCK(hmebp);

	return (hmeblkp);
}

/*
 * Release one hardware address translation lock on the given address range.
 */
static void
sfmmu_unlock(struct hat *hat, struct as *as, caddr_t addr, u_int len)
{
	struct hmehash_bucket *hmebp;
	struct sfmmu *sfmmup;
	hmeblk_tag hblktag;
	int hmeshift, hashno = 1;
	struct hme_blk *hmeblkp, *nx_hblk, *pr_hblk = NULL;
	caddr_t endaddr;

	ASSERT(as->a_hat == hat);
	ASSERT((len & MMU_PAGEOFFSET) == 0);
	endaddr = addr + len;
	sfmmup = hattosfmmu(hat);
	hblktag.htag_id = sfmmup;

	/*
	 * Spitfire supports 4 page sizes.
	 * Most pages are expected to be of the smallest page size (8K) and
	 * these will not need to be rehashed. 64K pages also don't need to be
	 * rehashed because an hmeblk spans 64K of address space. 512K pages
	 * might need 1 rehash and and 4M pages might need 2 rehashes.
	 */
	while (addr < endaddr) {
		hmeshift = HME_HASH_SHIFT(hashno);
		hblktag.htag_bspage = HME_HASH_BSPAGE(addr, hmeshift);
		hblktag.htag_rehash = hashno;
		hmebp = HME_HASH_FUNCTION(sfmmup, addr, hmeshift);

		SFMMU_HASH_LOCK(hmebp);

		HME_HASH_SEARCH(hmebp, hblktag, hmeblkp, nx_hblk, pr_hblk);
		if (hmeblkp != NULL) {
			addr = sfmmu_hblk_unlock(hmeblkp, addr, endaddr);
			SFMMU_HASH_UNLOCK(hmebp);
			hashno = 1;
			continue;
		}
		SFMMU_HASH_UNLOCK(hmebp);

		if (!sfmmup->sfmmu_lttecnt || (hashno >= MAX_HASHCNT)) {
			/*
			 * We have traversed the whole list and rehashed
			 * if necessary without finding the address to unlock
			 * which should never happen.
			 */
			cmn_err(CE_PANIC,
				"sfmmu_unlock: addr not found."
				"addr = 0x%x as = 0x%x\n", addr, as);
			addr += MMU_PAGESIZE;
		} else {
			hashno++;
		}
	}
}

/*
 * Function to unlock a range of addresses in an hmeblk.  It returns the
 * next address that needs to be unlocked.
 * Should be called with the hash lock held.
 */
static caddr_t
sfmmu_hblk_unlock(struct hme_blk *hmeblkp, caddr_t addr, caddr_t endaddr)
{
	struct sf_hment *sfhme;
	tte_t tteold;
	int ttesz;

	ASSERT(in_hblk_range(hmeblkp, addr));

	endaddr = MIN(endaddr, get_hblk_endaddr(hmeblkp));
	ttesz = get_hblk_ttesz(hmeblkp);

	sfhme = sfmmu_hblktohme(hmeblkp, addr, NULL);
	while (addr < endaddr) {
		MMU_COPYTTE(&sfhme->hme_tte, &tteold);
		if (TTE_IS_VALID(&tteold)) {
			if (hmeblkp->hblk_lckcnt <= 0) {
				cmn_err(CE_PANIC, "negative tte lckcnt\n");
			}
			if (((uint)addr + TTEBYTES(ttesz)) > (uint)endaddr) {
				cmn_err(CE_PANIC, "can't unlock large tte\n");
			}
			atadd_hword(&hmeblkp->hblk_lckcnt, -1);
			ASSERT(hmeblkp->hblk_lckcnt >= 0);
		} else {
			cmn_err(CE_PANIC, "sfmmu_hblk_unlock: invalid tte");
		}
		addr += TTEBYTES(ttesz);
		sfhme++;
		SFMMU_STACK_TRACE(hmeblkp, tteold.tte_hmenum)
	}
	return (addr);
}

static faultcode_t
sfmmu_fault(struct hat *hat, caddr_t addr)
{
#ifdef lint
	hat = hat; addr = addr;
#endif
	return (FC_NOMAP);
}

static int
sfmmu_probe(struct hat *hat, struct as *as, caddr_t addr)
{
	struct sfmmu *sfmmup;


	ASSERT(as->a_hat == hat);
	ASSERT(as == &kas || AS_LOCK_HELD(as, &as->a_lock));

	sfmmup = hattosfmmu(hat);
	if (sfmmu_vatopfn(addr, sfmmup) != -1)
		return (1);
	else
		return (0);
}

static struct kmem_cache *ism_map_blk_cache;

static void
ism_cache_constructor(void *buf, size_t size) {
	bzero(buf, size);
}

/*
 * Allocate an ISM block mapping structure. This
 * routine will align the allocation to a E$ line
 * and make sure it does not cross a page boundary.
 */
static ism_map_blk_t *
ism_map_blk_alloc(void) {

	ism_map_blk_t *blkp, *blklist, *blkp1;

	/*
	 * Initialize cache if first time thru.
	 */
	if (ism_map_blk_cache == NULL) {
		mutex_enter(&ism_lock);
		if (ism_map_blk_cache == NULL) {
			ism_map_blk_cache =
				kmem_cache_create("ism_map_blk_cache",
					sizeof (ism_map_blk_t),
					ecache_linesize,
					ism_cache_constructor, NULL, NULL);
		}
		mutex_exit(&ism_lock);
#ifdef DEBUG
		if (ism_debug)
		printf("ism_map_blk_alloc: ism_map_blk_cache initialized\n");
#endif
	}
	blkp = kmem_cache_alloc(ism_map_blk_cache, KM_SLEEP);

	/*
	 * Make sure map blk doesn't cross a page boundary.
	 */
	if (!ISM_CROSS_PAGE(blkp, sizeof (ism_map_blk_t))) {
#ifdef DEBUG
		if (ism_debug)
		printf("ism_map_blk_alloc: map block allocated at 0x%x\n",
			blkp);
#endif
		return (blkp);
	}
	blklist = blkp;

	/*
	 * Our first attempt failed. Keep trying until
	 * we get one that fits while holding previous allocs.
	 * Then after finding a good one free the list.
	 */
	while (ISM_CROSS_PAGE(blkp, sizeof (ism_map_blk_t))) {
		blkp = kmem_cache_alloc(ism_map_blk_cache, KM_SLEEP);
		if (!ISM_CROSS_PAGE(blkp, sizeof (ism_map_blk_t)))
			break;

		blkp->next = blklist;
		blklist = blkp;
	}

	/*
	 * Free the duds before returning the good one.
	 */
	while (blklist) {
		blkp1 = blklist;
		blklist = blklist->next;
		kmem_cache_free(ism_map_blk_cache, blkp1);
	}
	return (blkp);
}

static void
ism_map_blk_free(ism_map_blk_t *blkp) {

	ASSERT(ism_map_blk_cache != NULL);
#ifdef DEBUG
		if (ism_debug)
		printf("ism_map_blk_free: map block freed at 0x%x\n",
			blkp);
#endif
	kmem_cache_free(ism_map_blk_cache, blkp);
}

static int
sfmmu_share(as, addr, sptas, sptaddr, size)
	struct as	*as, *sptas;
	caddr_t		addr, sptaddr;
	u_int		size;
{
	struct sfmmu 	*sfmmup, *ism_sfmmu;
	struct ctx	*ctx;
	ism_map_blk_t	*blkp, *prevp;
	ism_map_t 	*map;
	u_char		sh_size = ISM_SHIFT(size);
	u_char		sh_vbase = ISM_SHIFT(addr);
	int		i;

	ASSERT(sptas != NULL);
	ASSERT(sptaddr == 0x0);
	/*
	 * Check the alignment.
	 */
	if (! ISM_ALIGNED(addr) || ! ISM_ALIGNED(sptaddr))
		return (EINVAL);

	/*
	 * Check size alignment.
	 */
	if (! ISM_ALIGNED(size))
		return (EINVAL);


	mutex_enter(&as->a_hat->hat_mutex);
	/*
	 * Make sure that during the time ism-mappings are setup, this
	 * process doesn't allow it's context to be stolen.
	 */
	sfmmu_disallow_ctx_steal(astosfmmu(as));

	ism_sfmmu = astosfmmu(sptas);
	sfmmup    = astosfmmu(as);
	ctx	  = astoctx(as);

	/*
	 * Allocate an ism map blk if necessary.
	 * process doesn't allow it's context to be stolen.
	 */
	if (sfmmup->sfmmu_ismblk == NULL) {
		ASSERT(ctx->c_ismblkpa == (u_longlong_t)-1);
		sfmmup->sfmmu_ismblk = ism_map_blk_alloc();
		ctx->c_ismblkpa = va_to_pa((caddr_t)sfmmup->sfmmu_ismblk);
	}

	/*
	 * Make sure mapping does not already exist.
	 */
	blkp = sfmmup->sfmmu_ismblk;
	while (blkp) {
		map = blkp->maps;
		for (i = 0; i < ISM_MAP_SLOTS && map[i].ism_sfmmu; i++) {
			if ((map[i].ism_sfmmu == ism_sfmmu &&
				sh_vbase == map[i].vbase))
				cmn_err(CE_PANIC,
					"sfmmu_share: Already mapped!");
		}
		blkp = blkp->next;
	}

	/*
	 * Add mapping to first available mapping slot.
	 */
	blkp = prevp = sfmmup->sfmmu_ismblk;
	while (blkp) {
		map = blkp->maps;
		for (i = 0; i < ISM_MAP_SLOTS; i++)  {
			if (map[i].ism_sfmmu == NULL) {
				map[i].ism_sfmmu   = ism_sfmmu;
				map[i].vbase	   = sh_vbase;
				map[i].size	   = sh_size;
				goto out;
			}
		}
		prevp = blkp;
		blkp = blkp->next;
	}

	/*
	 * We did not find an empty slot so we must add a
	 * new map blk and allocate the first mapping.
	 */
	blkp = prevp->next = ism_map_blk_alloc();
	map = blkp->maps;
	map[0].ism_sfmmu   = ism_sfmmu;
	map[0].vbase	   = sh_vbase;
	map[0].size	   = sh_size;
#ifdef DEBUG
	if (ism_debug) {
		printf("sfmmu_share: ism_map_blk 0x%x added to ctx 0x%x\n",
			blkp, astoctx(as));
	}
#endif /* DEBUG */

out:
#ifdef DEBUG
	if (ism_debug) {
	printf("sfmmu_share: ctx:base:size 0x%x:0x%x:0x%x -> ctx 0x%x\n",
		astoctx(as), addr, size, astoctx(sptas));
	}
#endif /* DEBUG */

	/*
	 * Now the ctx can be stolen.
	 */
	sfmmu_allow_ctx_steal(astosfmmu(as));

	mutex_exit(&as->a_hat->hat_mutex);
	return (0);
}

int unshare_err; /* XXX */
static void
sfmmu_unshare(as, addr, size)
	struct as	*as;
	caddr_t		addr;
	u_int		size;
{
	struct sfmmu	*sfmmup;
	ism_map_t 	*map;
	ism_map_blk_t	*blkp, *prevp;
	struct ctx	*ctx;
	u_char		sh_size = ISM_SHIFT(size);
	u_char		sh_vbase = ISM_SHIFT(addr);
	int 		found;
	int 		i;

	ASSERT(ISM_ALIGNED(addr));
	ASSERT(ISM_ALIGNED(size));
	/*
	 * Check to see if as_free() has already completed.
	 */
	if (astosfmmu(as) == NULL) {
		return;
	}

	mutex_enter(&as->a_hat->hat_mutex);
	/*
	 * Make sure that during the time ism-mappings are setup, this
	 * process doesn't allow it's context to be stolen.
	 */
	sfmmu_disallow_ctx_steal(astosfmmu(as));

	/*
	 * Remove the mapping.
	 *
	 * We can't have any holes in the ism map.
	 * The tsb miss code while searching the ism map will
	 * stop on an empty mapping slot.  So we must move
	 * everyone past the hole up 1 if any.
	 */
	found = 0;
	sfmmup = astosfmmu(as);
	ctx = astoctx(as);
	blkp = sfmmup->sfmmu_ismblk;
	while (blkp) {
		map = blkp->maps;
		for (i = 0; i < ISM_MAP_SLOTS; i++) {
			if (!found && (sh_vbase == map[i].vbase &&
					sh_size == map[i].size)) {
				found = 1;
			}
			if (found) {
				if (map[i].ism_sfmmu &&
					i < (ISM_MAP_SLOTS - 1)) {
					map[i].ism_sfmmu =
					    map[i + 1].ism_sfmmu;
					map[i].vbase = map[i + 1].vbase;
					map[i].size = map[i + 1].size;
				} else {
					if (blkp->next) {
						ism_map_t *nmap;
						nmap = blkp->next->maps;

						map[i].ism_sfmmu =
						    nmap[0].ism_sfmmu;
						map[i].vbase = nmap[0].vbase;
						map[i].size =  nmap[0].size;
						break;
					} else {
						map[i].ism_sfmmu = NULL;
						map[i].vbase = 0;
						map[i].size = 0;
						break;
					}
				}
			}
		}
		blkp = blkp->next;
	}

	/*
	 * Free up empty ism map blk, if any.
	 */
	prevp = blkp = sfmmup->sfmmu_ismblk;
	while (blkp) {
		if (blkp->maps[0].ism_sfmmu == NULL) {
			if (blkp->next)
				cmn_err(CE_PANIC,
				    "sfmmu_unshare: blkp->next != NULL");

			ism_map_blk_free(blkp);
			if (blkp == sfmmup->sfmmu_ismblk) {
				/*
				 * single block case
				 */
				sfmmup->sfmmu_ismblk = NULL;
				ASSERT(ctx->c_ismblkpa != (u_longlong_t)-1);
				ctx->c_ismblkpa = (u_longlong_t)-1;
			} else {
				/*
				 * multi block case
				 */
				prevp->next = NULL;
			}
			break;
		}
		prevp = blkp;
		blkp = blkp->next;
	}

	if (found) {
		sfmmu_ctx_demap(sfmmup);
		sfmmu_xcall_sync(sfmmup->sfmmu_cpusran);
	}

	sfmmu_allow_ctx_steal(astosfmmu(as));
	mutex_exit(&as->a_hat->hat_mutex);

	/*
	 * XXX Should I panic?
	 */
	if (!found) {
		unshare_err++;
#ifdef DEBUG
		if (ism_debug) {
		printf("sfmmu_unshare: Could not find mapping to remove\n");
		printf("sfmmu_unshare: as      -> 0x%x\n", as);
		printf("sfmmu_unshare: addr    -> 0x%x\n", addr);
		printf("sfmmu_unshare: size    -> 0x%x\n", size);
		}
#endif /* DEBUG */
		return;
	}

#ifdef DEBUG
	if (ism_debug) {
	printf("sfmmu_unshare: ctx:base:size 0x%x:0x%x:0x%x\n",
		astoctx(as), addr, size);
	}
#endif
}

static void
sfmmu_unmap(struct as *as, caddr_t addr, u_int len, int flags)
{
	sfmmu_unload(as, addr, len, flags);
}

/*
 * Change the protections in the virtual address range
 * given to the specified virtual protection.  If vprot is ~PROT_WRITE,
 * then remove write permission, leaving the other
 * permissions unchanged.  If vprot is ~PROT_USER, remove user permissions.
 *
 * We made this a global (vs static) routine because wtodc uses it.
 * See comments in hardclk.c/wtodc routine.
 */
void
sfmmu_chgprot(struct as *as, caddr_t addr, u_int len, u_int vprot)
{
	struct hmehash_bucket *hmebp;
	struct sfmmu *sfmmup;
	hmeblk_tag hblktag;
	int hmeshift, hashno = 1;
	struct hme_blk *hmeblkp, *nx_hblk, *pr_hblk = NULL;
	caddr_t endaddr;

	ASSERT((len & MMU_PAGEOFFSET) == 0);
	ASSERT(((u_int)addr & MMU_PAGEOFFSET) == 0);

	if ((vprot != (uint)~PROT_WRITE) && (vprot & PROT_USER) &&
	    ((addr + len) > (caddr_t)USERLIMIT)) {
		cmn_err(CE_PANIC, "user addr %x vprot %x in kernel space\n",
			addr, vprot);
	}
	endaddr = addr + len;
	sfmmup = astosfmmu(as);
	hblktag.htag_id = sfmmup;

	while (addr < endaddr) {
		hmeshift = HME_HASH_SHIFT(hashno);
		hblktag.htag_bspage = HME_HASH_BSPAGE(addr, hmeshift);
		hblktag.htag_rehash = hashno;
		hmebp = HME_HASH_FUNCTION(sfmmup, addr, hmeshift);

		SFMMU_HASH_LOCK(hmebp);

		HME_HASH_SEARCH(hmebp, hblktag, hmeblkp, nx_hblk, pr_hblk);
		if (hmeblkp != NULL) {
			addr = sfmmu_hblk_chgprot(as, hmeblkp, addr, endaddr,
				vprot);
			SFMMU_HASH_UNLOCK(hmebp);
			hashno = 1;
			continue;
		}
		SFMMU_HASH_UNLOCK(hmebp);

		if (!sfmmup->sfmmu_lttecnt || (hashno >= MAX_HASHCNT)) {
			/*
			 * We have traversed the whole list and rehashed
			 * if necessary without finding the address to chgprot.
			 * This is ok so we increment the address by the
			 * smallest page size and continue.
			 */
			addr += MMU_PAGESIZE;
		} else {
			hashno++;
		}
	}
	sfmmu_xcall_sync(sfmmup->sfmmu_cpusran);
}

/*
 * This function chgprots a range of addresses in an hmeblk.  It returns the
 * next addres that needs to be chgprot.
 * It should be called with the hash lock held.
 * XXX It shold be possible to optimize chgprot by not flushing every time but
 * on the other hand:
 * 1. do one flush crosscall.
 * 2. only flush if we are increasing permissions (make sure this will work)
 */
static caddr_t
sfmmu_hblk_chgprot(struct as *as, struct hme_blk *hmeblkp, caddr_t addr,
	caddr_t endaddr, u_int vprot)
{
	struct sfmmu *sfmmup;
	u_int pprot;
	tte_t tte, ttemod;
	struct sf_hment *sfhmep;
	u_int tteflags;
	int ttesz;
	struct page *pp;
	kmutex_t *pml;

	ASSERT(in_hblk_range(hmeblkp, addr));

	sfmmup = hblktosfmmu(hmeblkp);
	endaddr = MIN(endaddr, get_hblk_endaddr(hmeblkp));
	ttesz = get_hblk_ttesz(hmeblkp);

	pprot = sfmmu_vtop_prot(vprot, &tteflags);
	sfhmep = sfmmu_hblktohme(hmeblkp, addr, 0);
	while (addr < endaddr) {
		MMU_COPYTTE(&sfhmep->hme_tte, &tte);
		if (TTE_IS_VALID(&tte)) {
			if (TTE_GET_LOFLAGS(&tte, tteflags) == pprot) {
				/*
				 * if the new protection is the same as old
				 * continue
				 */
				addr += TTEBYTES(ttesz);
				sfhmep++;
				continue;
			}
			ttemod = tte;
			TTE_SET_LOFLAGS(&ttemod, tteflags, pprot);
			if (MMU_MODIFYTTE_TRY(&tte, &ttemod,
			    &sfhmep->hme_tte)) {
				/* tte changed underneath us */
				continue;
			}
			if (tteflags & TTE_HWWR_INT) {
				/*
				 * need to sync if we are clearing modify bit.
				 */
				pp = sfhmep->hme_page;
				if (pp)
					pml = sfmmu_mlist_enter(pp);
				sfmmu_ttesync(as, addr, &tte, pp);
				if (pp)
					sfmmu_mlist_exit(pml);
			}

			sfmmu_tlb_demap(addr, sfmmup, ttesz, 0);
			SFMMU_STACK_TRACE(hmeblkp, tte.tte_hmenum)
		}
		addr += TTEBYTES(ttesz);
		sfhmep++;
	}
	return (addr);
}

/*
 * Unload all the mappings in the range [addr..addr+len). addr and len must
 * be MMU_PAGESIZE aligned.
 */
static void
sfmmu_unload(struct as *as, caddr_t addr, u_int len, int flags)
{
	struct hmehash_bucket *hmebp;
	struct sfmmu *sfmmup;
	hmeblk_tag hblktag;
	int hmeshift, hashno, iskernel;
	struct hme_blk *hmeblkp, *nx_hblk, *pr_hblk = NULL;
	caddr_t endaddr;

	ASSERT((len & MMU_PAGEOFFSET) == 0);

	endaddr = addr + len;
	sfmmup = astosfmmu(as);
	hblktag.htag_id = sfmmup;

	/*
	 * It is likely for the vm to call unload over a wide range of
	 * addresses that are actually very sparsely populated by
	 * translations.  In order to speed this up the sfmmu hat supports
	 * the concept of shadow hmeblks. Dummy large page hmeblks that
	 * correspond to actual small translations are allocated at tteload
	 * time and are referred to as shadow hmeblks.  Now, during unload
	 * time, we first check if we have a shadow hmeblk for that
	 * translation.  The absence of one means the corresponding address
	 * range is empty and can be skipped.
	 *
	 * The kernel is an exception to above statement and that is why
	 * we don't use shadow hmeblks and hash starting from the smallest
	 * page size.
	 */
	if (sfmmup == KHATID) {
		iskernel = 1;
		hashno = TTE64K;
	} else {
		iskernel = 0;
		hashno = TTE4M;
	}
	while (addr < endaddr) {
		hmeshift = HME_HASH_SHIFT(hashno);
		hblktag.htag_bspage = HME_HASH_BSPAGE(addr, hmeshift);
		hblktag.htag_rehash = hashno;
		hmebp = HME_HASH_FUNCTION(sfmmup, addr, hmeshift);

		SFMMU_HASH_LOCK(hmebp);

		HME_HASH_SEARCH(hmebp, hblktag, hmeblkp, nx_hblk, pr_hblk);
		if (hmeblkp == NULL) {
			/*
			 * didn't find an hmeblk. skip the appropiate
			 * address range.
			 */
			SFMMU_HASH_UNLOCK(hmebp);
			if (iskernel) {
				if (hashno < MAX_HASHCNT) {
					hashno++;
					continue;
				} else {
					hashno = TTE64K;
					addr = (caddr_t)roundup((u_int)addr
						+ 1, MMU_PAGESIZE64K);
					continue;
				}
			}
			addr = (caddr_t)roundup((u_int)addr + 1,
				(1 << hmeshift));
			if ((uint)addr & MMU_PAGEOFFSET512K) {
				ASSERT(hashno == TTE64K);
				continue;
			}
			if ((uint)addr & MMU_PAGEOFFSET4M) {
				hashno = TTE512K;
				continue;
			}
			hashno = TTE4M;
			continue;
		}
		ASSERT(hmeblkp);
		if (!hmeblkp->hblk_vcnt) {
			/*
			 * If the valid count is zero we can skip the range
			 * mapped by this hmeblk.
			 */
			addr = (caddr_t)roundup((u_int)addr + 1,
				get_hblk_span(hmeblkp));
			if (iskernel) {
				hashno = TTE64K;
			}
			SFMMU_HASH_UNLOCK(hmebp);
			continue;
		}
		if (hmeblkp->hblk_shw_bit) {
			/*
			 * If we encounter a shadow hmeblk we know there is
			 * smaller sized hmeblks mapping the same address space.
			 * Decrement the hash size and rehash.
			 */
			ASSERT(sfmmup != KHATID);
			hashno--;
			SFMMU_HASH_UNLOCK(hmebp);
			continue;
		}
		addr = sfmmu_hblk_unload(as, hmeblkp, addr, endaddr, flags);
		SFMMU_HASH_UNLOCK(hmebp);
		if (iskernel) {
			hashno = TTE64K;
			continue;
		}
		if ((uint)addr & MMU_PAGEOFFSET512K) {
			ASSERT(hashno == TTE64K);
			continue;
		}
		if ((uint)addr & MMU_PAGEOFFSET4M) {
			hashno = TTE512K;
			continue;
		}
		hashno = TTE4M;
	}
	sfmmu_xcall_sync(sfmmup->sfmmu_cpusran);

	if (sfmmup->sfmmu_free && !as->a_rss) {
		/* real as_free */
		sfmmu_tlbctx_demap(sfmmup);
		sfmmu_free_ctx(sfmmup, sfmmutoctx(sfmmup));
		sfmmu_free_sfmmu(sfmmup);
	}
}

/*
 * This function unloads a range of addresses for an hmeblk.
 * It returns the next addres to be unloaded.
 * It should be called with the hash lock held.
 */
static caddr_t
sfmmu_hblk_unload(struct as *as, struct hme_blk *hmeblkp, caddr_t addr,
	caddr_t endaddr, int flags)
{
	tte_t	tte, ttemod;
	struct	sf_hment *sfhmep;
	struct	sfmmu *sfmmup;
	int	ttesz, cnt;
	struct	page *pp;
	kmutex_t *pml;

	ASSERT(in_hblk_range(hmeblkp, addr));
	ASSERT(!hmeblkp->hblk_shw_bit);

	sfmmup = astosfmmu(as);
	endaddr = MIN(endaddr, get_hblk_endaddr(hmeblkp));

	ttesz = get_hblk_ttesz(hmeblkp);
	cnt = -TTEPAGES(ttesz);
	sfhmep = sfmmu_hblktohme(hmeblkp, addr, NULL);

	while (addr < endaddr) {
		MMU_COPYTTE(&sfhmep->hme_tte, &tte);
		if (TTE_IS_VALID(&tte)) {
			if (flags & HAT_UNLOCKUNLOAD) {
				atadd_hword(&hmeblkp->hblk_lckcnt, -1);
				ASSERT(hmeblkp->hblk_lckcnt >= 0);
			}

			pml = NULL;
			pp = sfhmep->hme_page;
			if (pp) {
				pml = sfmmu_mlist_enter(pp);
				if (sfhmep->hme_page == NULL) {
					/*
					 * If pageunload unloaded this
					 * hment before we grabbed the lock
					 * then undo the lock. Since pageunload
					 * will take care of all the unload
					 * functionality we can skip ahead to
					 * the next page.
					 */
					sfmmu_mlist_exit(pml);
					continue;
				}
				/*
				 * Remove the hment from the mapping list
				 */
				ASSERT(pp);
				ASSERT(pp == sfhmep->hme_page);
				ASSERT(hmeblkp->hblk_hmecnt > 0);
				ASSERT(hmeblkp->hblk_hmecnt <= NHMENTS);
				atadd_hword(&hmeblkp->hblk_hmecnt, -1);
				hme_sub((struct hment *)sfhmep, pp);
			}
			/*
			 * MODIFYTTE will loop until the cas succeeds.
			 * We need to loop on modify tte because it is
			 * possible for pagesync to come along and
			 * change the software bits beneath us, or
			 * pageunload do the unload for us.
			 */
			ttemod = tte;
			TTE_SET_INVALID(&ttemod);
			MMU_MODIFYTTE(&tte, &ttemod, &sfhmep->hme_tte);
			if (TTE_IS_VALID(&tte)) {
				if (!(flags & HAT_NOSYNCUNLOAD)) {
					sfmmu_ttesync(as, addr, &tte, pp);
				}
				atadd_hword(&hmeblkp->hblk_vcnt, -1);
				if (ttesz != TTE8K) {
					atadd_hword(&sfmmup->sfmmu_lttecnt, -1);
				}
				atadd_word((u_int *)&as->a_rss, cnt);
				/*
				 * Normally we would need to flush the page
				 * from the virtual cache at this point in
				 * order to prevent a potential cache alias
				 * inconsistency.
				 * The particular scenario we need to worry
				 * about is:
				 * Given:  va1 and va2 are two virtual address
				 * that alias and map the same physical
				 * address.
				 * 1.	mapping exists from va1 to pa and data
				 * has been read into the cache.
				 * 2.	unload va1.
				 * 3.	load va2 and modify data using va2.
				 * 4	unload va2.
				 * 5.	load va1 and reference data.  Unless we
				 * flush the data cache when we unload we will
				 * get stale data.
				 * Fortunately, page coloring eliminates the
				 * above scenario by remembering the color a
				 * physical page was last or is currently
				 * mapped to.  Now, we delay the flush until
				 * the loading of translations.  Only when the
				 * new translation is of a different color
				 * are we forced to flush.
				 */
				if (do_virtual_coloring) {
					sfmmu_tlb_demap(addr, sfmmup, ttesz,
						sfmmup->sfmmu_free);
				} else {
					int pfnum;
					pfnum = TTE_TO_PFN(addr, &tte);
					sfmmu_tlbcache_demap(addr, sfmmup,
					    ttesz, pfnum, sfmmup->sfmmu_free);
				}
				if (pp && PP_ISTNC(pp)) {
					/*
					 * if page was temporary
					 * unencached, try to recache
					 * it.
					 */
					sfmmu_page_cache(pp, HAT_CACHE);
				}
			}
			if (pp) {
				ASSERT(pml);
				sfmmu_mlist_exit(pml);
			}
		}
		SFMMU_STACK_TRACE(hmeblkp, tte.tte_hmenum)
		addr += TTEBYTES(ttesz);
		sfhmep++;
	}
	return (addr);
}

/*
 * sfmmu_sync can be called with clearflag having two states:
 * HAT_DONTZERO means just return the rm stats
 * HAT_ZERORM means zero rm bits in the tte and return the stats
 */
static void
sfmmu_sync(struct as *as, caddr_t addr, u_int len, u_int clearflag)
{
	struct hmehash_bucket *hmebp;
	struct sfmmu *sfmmup;
	hmeblk_tag hblktag;
	int hmeshift, hashno = 1;
	struct hme_blk *hmeblkp, *nx_hblk, *pr_hblk = NULL;
	caddr_t endaddr;

	ASSERT((len & MMU_PAGEOFFSET) == 0);
	ASSERT((clearflag == HAT_DONTZERO) || (clearflag == HAT_ZERORM));

	endaddr = addr + len;
	sfmmup = astosfmmu(as);
	hblktag.htag_id = sfmmup;
	/*
	 * Spitfire supports 4 page sizes.
	 * Most pages are expected to be of the smallest page
	 * size (8K) and these will not need to be rehashed. 64K
	 * pages also don't need to be rehashed because the an hmeblk
	 * spans 64K of address space. 512K pages might need 1 rehash and
	 * and 4M pages 2 rehashes.
	 */
	while (addr < endaddr) {
		hmeshift = HME_HASH_SHIFT(hashno);
		hblktag.htag_bspage = HME_HASH_BSPAGE(addr, hmeshift);
		hblktag.htag_rehash = hashno;
		hmebp = HME_HASH_FUNCTION(sfmmup, addr, hmeshift);

		SFMMU_HASH_LOCK(hmebp);

		HME_HASH_SEARCH(hmebp, hblktag, hmeblkp, nx_hblk, pr_hblk);
		if (hmeblkp != NULL) {
			addr = sfmmu_hblk_sync(as, hmeblkp, addr, endaddr,
			    clearflag);
			SFMMU_HASH_UNLOCK(hmebp);
			hashno = 1;
			continue;
		}
		SFMMU_HASH_UNLOCK(hmebp);

		if (!sfmmup->sfmmu_lttecnt || (hashno >= MAX_HASHCNT)) {
			/*
			 * We have traversed the whole list and rehashed
			 * if necessary without unloading so we assume it
			 * has already been unloaded
			 */
			addr += MMU_PAGESIZE;
		} else {
			hashno++;
		}
	}
	sfmmu_xcall_sync(sfmmup->sfmmu_cpusran);
}

static caddr_t
sfmmu_hblk_sync(struct as *as, struct hme_blk *hmeblkp, caddr_t addr,
	caddr_t endaddr, int clearflag)
{
	tte_t	tte, ttemod;
	struct sf_hment *sfhmep;
	int ttesz;
	struct page *pp;
	kmutex_t *pml;

	endaddr = MIN(endaddr, get_hblk_endaddr(hmeblkp));

	ttesz = get_hblk_ttesz(hmeblkp);
	sfhmep = sfmmu_hblktohme(hmeblkp, addr, 0);

	while (addr < endaddr) {
		MMU_COPYTTE(&sfhmep->hme_tte, &tte);
		if (TTE_IS_VALID(&tte)) {
			if (clearflag == HAT_ZERORM) {
				ttemod = tte;
				TTE_CLR_RM(&ttemod);
				if (MMU_MODIFYTTE_TRY(&tte, &ttemod,
				    &sfhmep->hme_tte)) {
					continue;
				}
				sfmmu_tlb_demap(addr, astosfmmu(as),
					tte.tte_size, 0);
			}
			pp = sfhmep->hme_page;
			if (pp) {
				pml = sfmmu_mlist_enter(pp);
			}
			sfmmu_ttesync(as, addr, &tte, pp);
			if (pp) {
				sfmmu_mlist_exit(pml);
			}
		}
		addr += TTEBYTES(ttesz);
		sfhmep++;
	}
	return (addr);
}

/*
 * This function will sync a tte to the page struct and it will
 * update the hat stats. Currently it allows us to pass a NULL pp
 * and we will simply update the stats.  We may want to change this
 * so we only keep stats for pages backed by pp's.
 */
static void
sfmmu_ttesync(struct as *as, caddr_t addr, tte_t *ttep, struct page *pp)
{
	u_int rm = 0;

	ASSERT(TTE_IS_VALID(ttep));

	if (TTE_IS_NOSYNC(ttep)) {
		return;
	}

	if (TTE_IS_REF(ttep))  {
		rm = HAT_STAT_REF;
	}
	if (TTE_IS_MOD(ttep))  {
		rm |= HAT_STAT_MOD;
	}
	if (rm && as->a_vbits) {
		hat_setstat(as, addr, rm);
	}
	/*
	 * XXX I want to use cas to update nrm bits but they
	 * currently belong in common/vm and not in hat where
	 * they should be.
	 * The nrm bits are protected by the same mutex as
	 * the one that protects the page's mapping list.
	 */
	if (pp) {
		ASSERT(sfmmu_mlist_held(pp));
		if ((rm == HAT_STAT_REF) && !PP_ISREF(pp)) {
			PP_SETREF(pp);
		} else if ((rm == HAT_STAT_MOD) && !PP_ISMOD(pp)) {
			PP_SETMOD(pp);
		} else if ((rm == (HAT_STAT_REF | HAT_STAT_MOD)) &&
		    (!PP_ISREF(pp) || !PP_ISMOD(pp))) {
			PP_SETREFMOD(pp);
		}
	}
}


/* ARGSUSED */
static void
sfmmu_sys_pageunload(register struct page *pp, register struct hment *hme)
{
	register struct hat *hat;
	struct sf_hment *sfhme;
	kmutex_t *pml;
	struct sfmmu *sfmmup;
	cpuset_t cpuset = 0;

	ASSERT(se_assert(&pp->p_selock));

	pml = sfmmu_mlist_enter(pp);
	while ((sfhme = (struct sf_hment *)pp->p_mapping) != NULL) {
		hat = &hats[sfhme->hme_hat];
		if (hat->hat_op == &sfmmu_hatops) {
			sfmmup = hattosfmmu(hat);
			cpuset |= sfmmup->sfmmu_cpusran;
			sfmmu_pageunload(pp, sfhme);
		} else {
			HATOP_PAGEUNLOAD(hat, pp, (struct hment *)sfhme);
		}
	}
	sfmmu_xcall_sync(cpuset);
	ASSERT(pp->p_mapping == NULL);
	sfmmu_mlist_exit(pml);
}


static void
sfmmu_pageunload(struct page *pp, struct sf_hment *sfhme)
{
	struct hme_blk *hmeblkp;
	struct sfmmu *sfmmup;
	tte_t tte, ttemod;
	caddr_t addr;
	struct as *as;
	int ttesz;

	ASSERT(pp != NULL);
	ASSERT(sfmmu_mlist_held(pp));

	hmeblkp = sfmmu_hmetohblk(sfhme);
	MMU_COPYTTE(&sfhme->hme_tte, &tte);
	if (TTE_IS_VALID(&tte)) {
		sfmmup = hblktosfmmu(hmeblkp);
		ttesz = get_hblk_ttesz(hmeblkp);
		if (ttesz != TTE8K) {
			cmn_err(CE_PANIC, "sfmmu_pageunload - large page");
		}

		as = sfhmetoas(sfhme);
		addr = tte_to_vaddr(hmeblkp, tte);
		ASSERT(hmeblkp->hblk_hmecnt > 0);
		ASSERT(hmeblkp->hblk_hmecnt <= NHMENTS);
		atadd_hword(&hmeblkp->hblk_hmecnt, -1);
		hme_sub((struct hment *)sfhme, pp);
		/*
		 * It is possible for a page_unload and sfmmu_unload to occur
		 * at the same time on the same tte.  Using modifytte and
		 * verifying the tte we unloaded was valid guarantees we only
		 * decrement the valid count once.
		 */
		ttemod = tte;
		TTE_SET_INVALID(&ttemod);
		MMU_MODIFYTTE(&tte, &ttemod, &sfhme->hme_tte);
		if (TTE_IS_VALID(&tte)) {
			SFMMU_STACK_TRACE(hmeblkp, tte.tte_hmenum)
			atadd_word((u_int *)&as->a_rss, -1);
			atadd_hword(&hmeblkp->hblk_vcnt, -1);
			/*
			 * since we don't support pageunload of a large page we
			 * don't need to atomically decrement lttecnt.
			 */
			sfmmu_ttesync(as, addr, &tte, pp);
		}
		/*
		 * We need to flush the page from the virtual cache in order
		 * to prevent a virtual cache alias inconsistency.  The
		 * particular scenario we need to worry about is:
		 * Given:  va1 and va2 are two virtual address that alias and
		 * will map the same physical address.
		 * 1.	mapping exists from va1 to pa and data has been read
		 *	into the cache.
		 * 2.	unload va1.
		 * 3.	load va2 and modify data using va2.
		 * 4	unload va2.
		 * 5.	load va1 and reference data.  Unless we flush the data
		 *	cache when we unload we will get stale data.
		 * This scenario is taken care of by using virtual page
		 * coloring.
		 */
		if (do_virtual_coloring) {
			sfmmu_tlb_demap(addr, sfmmup, ttesz, 0);
		} else {
			sfmmu_tlbcache_demap(addr, sfmmup, ttesz,
				pp->p_pagenum, 0);
		}
		if (PP_ISTNC(pp)) {
			/*
			 * if page was temporary unencached, try to recache it.
			 */
			sfmmu_page_cache(pp, HAT_CACHE);
		}
	}
}



/*ARGSUSED*/
static int
sfmmu_sys_pagesync(struct hat *hat, struct page *pp, struct hment *hme,
	u_int clearflag)
{
	struct sf_hment *sfhme, *tmphme = NULL;
	kmutex_t *pml;
	struct sfmmu *sfmmup;
	cpuset_t cpuset = 0;

	pml = sfmmu_mlist_enter(pp);
	for (sfhme = (struct sf_hment *)pp->p_mapping; sfhme;
	    sfhme = tmphme) {
		/*
		 * We need to save the next hment on the list since
		 * it is possible for pagesync to remove an invalid hment
		 * from the list.
		 */
		tmphme = (struct sf_hment *)sfhme->hme_next;
		hat = &hats[sfhme->hme_hat];
		if (hat->hat_op == &sfmmu_hatops) {
			sfmmup = hattosfmmu(hat);
			cpuset |= sfmmup->sfmmu_cpusran;
			sfmmu_pagesync(hat, pp, sfhme,
			    clearflag & ~HAT_STOPON_RM);
		} else {
			HATOP_PAGESYNC(hat, pp, (struct hment *)sfhme,
			    clearflag & ~HAT_STOPON_RM);
		}
		/*
		 * If clearflag is HAT_DONTZERO, break out as soon
		 * as the "ref" or "mod" is set.
		 */
		if ((clearflag & ~HAT_STOPON_RM) == HAT_DONTZERO &&
		    ((clearflag & HAT_STOPON_MOD) && PP_ISMOD(pp)) ||
		    ((clearflag & HAT_STOPON_REF) && PP_ISREF(pp)))
		break;
	}
	sfmmu_xcall_sync(cpuset);
	sfmmu_mlist_exit(pml);

	return (HAT_DONE);
}

/*
 * Get all the hardware dependent attributes for a page struct
 */
static void
sfmmu_pagesync(struct hat *hat, struct page *pp, struct sf_hment *sfhme,
	u_int clearflag)
{
	caddr_t addr;
	tte_t tte, ttemod;
	struct hme_blk *hmeblkp;

	ASSERT(pp != NULL);
	ASSERT(sfmmu_mlist_held(pp));
	ASSERT((clearflag == HAT_DONTZERO) || (clearflag == HAT_ZERORM));

	SFMMU_STAT(vh_pagesync);

sfmmu_pagesync_retry:

	MMU_COPYTTE(&sfhme->hme_tte, &tte);
	if (TTE_IS_VALID(&tte)) {
		hmeblkp = sfmmu_hmetohblk(sfhme);
		addr = tte_to_vaddr(hmeblkp, tte);
		if (clearflag == HAT_ZERORM) {
			ttemod = tte;
			TTE_CLR_RM(&ttemod);
			if (MMU_MODIFYTTE_TRY(&tte, &ttemod, &sfhme->hme_tte)) {
				goto sfmmu_pagesync_retry;
			}
			sfmmu_tlb_demap(addr, hattosfmmu(hat), tte.tte_size, 0);
		}
		sfmmu_ttesync(hat->hat_as, addr, &tte, pp);
	}
}

static void
sfmmu_sys_pagecachectl(struct page *pp, u_int flag)
{
	register struct hatsw *hsw;

	for (hsw = hattab; hsw->hsw_name && hsw->hsw_ops; hsw++) {
		if (hsw->hsw_ops == &sfmmu_hatops) {
			sfmmu_pagecachectl(pp, flag);
		} else {
			HATOP_PAGECACHECTL(hsw, pp, flag);
		}
	}
}

static void
sfmmu_pagecachectl(struct page *pp, u_int flag)
{
#ifdef lint
	pp = pp; flag = flag;
#endif /* lint */

	/*
	 * sfmmu_pagecachectl is not supported in sun4u because fusion
	 * requires a mapping to be cacheable to maintain mp consistency.
	 * It could be argued that this call should then make a mapping
	 * unencacheable in the virtual cache but cacheable in the physical
	 * cache but no known costumer of such a feature is known at this
	 * time. We should understand the costumer's requirements and
	 * justification before providing such service.  If such need ever
	 * arises we should call sfmmu_pagecache routine which is the private
	 * sfmmu routine providing this service to the hat.
	 */
	cmn_err(CE_PANIC, "sfmmu_pagecachectl is not supported in sun4u");
}

/*
 * Returns a page frame number for a given kernel virtual address.
 * Return -1 to indicate an invalid mapping (needed by kvtoppid)
 */
static u_int
sfmmu_getkpfnum(caddr_t addr)
{
	return (sfmmu_vatopfn(addr, KHATID));
}

/*
 * Returns a page frame number for a given user virtual address.
 */
u_int
sfmmu_getpfnum(struct as *as, caddr_t addr)
{
	return (sfmmu_vatopfn(addr, astosfmmu(as)));
}

/*
 * For compatability with AT&T and later optimizations
 */
static int
sfmmu_map(struct hat *hat, struct as *as, caddr_t addr, u_int len, int flags)
{
#ifdef  lint
	hat = hat;
	as = as;
	addr = addr;
	len = len;
	flags = flags;
#endif /* lint */

	return (0);
}

/*
 * Initialize locking for the hat layer, called early during boot.
 */
static void
sfmmu_lock_init()
{
	int i;
	char name_buf[100];

	hat_lockops = &sfmmu_lockops;
	mutex_init(&ctx_lock, "sfmmu_ctx_lock", MUTEX_DEFAULT, NULL);

	/* initialize locks for hme_blk freelists */
	mutex_init(&hblk_lock, "sfmmu_hblk_lock", MUTEX_DEFAULT, NULL);
	mutex_init(&nhblk_lock, "sfmmu_nhblk_lock", MUTEX_DEFAULT, NULL);

	/* initialize lock ism */
	mutex_init(&ism_lock, "sfmmu_ism_lock", MUTEX_DEFAULT, NULL);

	/*
	 * initialize the array of mutexes protecting a page's mapping
	 * list and p_nrm field.
	 */
	for (i = 0; i < MLIST_SIZE; i++) {
		(void) sprintf(name_buf, "mlist_%d", i);
		mutex_init(&mml_table[i], name_buf, MUTEX_DEFAULT, NULL);
	}
}

/*
 * sfmmu_reserve used to be called in segkmem_fault to update the pp
 * mapping list.  We now take care of this in hat_kern_setup so
 * this routine now simply verifies we have no holes.
 *
 * No rehash is required because this va range should be mapped with 8K pages.
 * XXX delete this before beta.
 */
void
sfmmu_reserve_check(struct as *as, caddr_t addr, u_int len)
{
	struct hmehash_bucket *hmebp;
	struct sfmmu *sfmmup;
	hmeblk_tag hblktag;
	int hmeshift, hashno = 1;
	struct hme_blk *hmeblkp, *nx_hblk, *pr_hblk = NULL;
	caddr_t endaddr;
	tte_t tte;
	struct sf_hment *sfhme;
	uint pfnum;
	struct page *pp;
	uint range = 0;		/* initialize to remove lint error */

	ASSERT(as == &kas);

	endaddr = addr + len;
	sfmmup = astosfmmu(as);
	hblktag.htag_id = sfmmup;

	for (; addr < endaddr; addr += range) {
		hmeshift = HME_HASH_SHIFT(hashno);
		hblktag.htag_bspage = HME_HASH_BSPAGE(addr, hmeshift);
		hblktag.htag_rehash = hashno;
		hmebp = HME_HASH_FUNCTION(sfmmup, addr, hmeshift);
		pp = (struct page *)NULL;
		pfnum = 0;
		range = MMU_PAGESIZE;

		SFMMU_HASH_LOCK(hmebp);

		HME_HASH_SEARCH(hmebp, hblktag, hmeblkp, nx_hblk, pr_hblk);
		if (hmeblkp != NULL) {
			sfhme = sfmmu_hblktohme(hmeblkp, addr, NULL);
			MMU_COPYTTE(&sfhme->hme_tte, &tte);
			if (TTE_IS_VALID(&tte)) {
				pfnum = TTE_TO_PFN(addr, &tte);
				ASSERT(pf_is_memory(pfnum));
				pp = page_numtopp_nolock(pfnum);
				range = TTEBYTES(get_hblk_ttesz(hmeblkp));
				if (pp &&
				    (pp->p_mapping == (struct hment *)sfhme) &&
				    pp->p_free) {
					SFMMU_HASH_UNLOCK(hmebp);
					continue;
				}
			}
		}
		SFMMU_HASH_UNLOCK(hmebp);
		printf("pp 0x%x, pfnum 0x%x \n", pp, pfnum);
		if (pp)
			printf("mapping 0x%x \n", pp->p_mapping);
		panic("sfmmu_reserve check");
	}
}

/*
 * XXX - sfmmu_getprot() should become a hatop interface routine. Till then
 * it needs to be global.
 *
 * Get virtual protections.
 */
u_int
sfmmu_getprot(struct hat *hat, struct as *as, caddr_t addr)
{
	struct hmehash_bucket *hmebp;
	struct sfmmu *sfmmup;
	hmeblk_tag hblktag;
	int hmeshift, hashno = 1;
	struct hme_blk *hmeblkp, *nx_hblk, *pr_hblk = NULL;
	struct sf_hment *sfhmep;
	u_int vprot;
	tte_t tte;

	ASSERT(!((u_int)addr & MMU_PAGEOFFSET));
	ASSERT(as->a_hat == hat);

	sfmmup = hattosfmmu(hat);
	hblktag.htag_id = sfmmup;

	do {
		hmeshift = HME_HASH_SHIFT(hashno);
		hblktag.htag_bspage = HME_HASH_BSPAGE(addr, hmeshift);
		hblktag.htag_rehash = hashno;
		hmebp = HME_HASH_FUNCTION(sfmmup, addr, hmeshift);

		SFMMU_HASH_LOCK(hmebp);

		HME_HASH_SEARCH(hmebp, hblktag, hmeblkp, nx_hblk, pr_hblk);
		if (hmeblkp != NULL) {
			sfhmep = sfmmu_hblktohme(hmeblkp, addr, 0);
			MMU_COPYTTE(&sfhmep->hme_tte, &tte);
			if (TTE_IS_VALID(&tte)) {
				SFMMU_HASH_UNLOCK(hmebp);
				vprot = sfmmu_ptov_prot(&tte);
				return (vprot);
			}
		}
		SFMMU_HASH_UNLOCK(hmebp);
		hashno++;
	} while (sfmmup->sfmmu_lttecnt && (hashno <= MAX_HASHCNT));
	/*
	 * We have traversed the entire hmeblk list and
	 * rehashed if necessary without finding the addr.
	 * This shouldn't happen since we only use it to find the prot of
	 * locked translations.
	 */
	cmn_err(CE_PANIC, "sfmmu_getprot: addr not found\n");
	return (0);
}

static u_int
sfmmu_ptov_prot(tte_t *ttep)
{
	u_int vprot;

	ASSERT(TTE_IS_VALID(ttep));

	vprot = PROT_READ | PROT_EXEC;

	if (TTE_IS_WRITABLE(ttep)) {
		vprot |= PROT_WRITE;
	}
	if (!TTE_IS_PRIVILEGED(ttep)) {
		vprot |= PROT_USER;
	}
	return (vprot);
}

/*
 * sfmmu_get_ppvcolor should become a vm_machdep or hatop interface.
 * same goes for sfmmu_get_addrvcolor().
 *
 * This function will return the virtual color for the specified page. The
 * virtual color corresponds to this page current mapping or its last mapping.
 * It is used by memory allocators to choose addresses with the correct
 * alignment so vac consistency is automatically maintained.  If the page
 * has no color it returns -1.
 */
int
sfmmu_get_ppvcolor(struct page *pp)
{
	int color;
	extern u_int shm_alignment;

	if (!(cache & CACHE_VAC) || PP_NEWPAGE(pp)) {
		return (-1);
	}
	color = PP_GET_VCOLOR(pp);
	ASSERT(color < mmu_btop(shm_alignment));
	return (color);
}

/*
 * This function will return the desired alignment for vac consistency
 * (vac color) given a virtual address.  If no vac is present it returns -1.
 */
int
sfmmu_get_addrvcolor(caddr_t vaddr)
{
	extern uint shm_alignment;
	extern int cache;

	if (cache & CACHE_VAC) {
		return (addr_to_vcolor(vaddr));
	} else {
		return (-1);
	}

}

/*
 * Check for conflicts.
 * A conflict exists if the new and existant mappings do not match in
 * their "shm_alignment fields. If conflicts exist, the existant mappings
 * are flushed unless one of them is locked. If one of them is locked, then
 * the mappings are flushed and converted to non-cacheable mappings.
 */
static int
sfmmu_vac_conflict(as, addr, pp)
	struct	as *as;
	caddr_t	addr;
	struct	page *pp;
{
	struct sf_hment *sfhmep, *tmphme = NULL;
	struct hme_blk *hmeblkp;
	struct hat *hat;
	int retval;
	tte_t tte;

	ASSERT(sfmmu_mlist_held(pp));
	ASSERT(!PP_ISNC(pp));		/* page better be cacheable */

	if (PP_NEWPAGE(pp) || (addr_to_vcolor(addr) == PP_GET_VCOLOR(pp))) {
		return (NO_CONFLICT);
	}

	retval = PGCOLOR_CONFLICT;
	for (sfhmep = (struct sf_hment *)pp->p_mapping; sfhmep;
	    sfhmep = tmphme) {
		tmphme = (struct sf_hment *)sfhmep->hme_next;
		hat = sfhmetohat(sfhmep);
		if (!hat_is_sfmmu(hat)) {
			retval = UNLOAD_CONFLICT;
			continue;
		}
		hmeblkp = sfmmu_hmetohblk(sfhmep);
		MMU_COPYTTE(&sfhmep->hme_tte, &tte);
		ASSERT(TTE_IS_VALID(&tte));
		/*
		 * If the requested mapping is inconsistent
		 * with another mapping and that mapping
		 * is in the same address space we have to
		 * make it non-cached.  The default thing
		 * to do is unload the inconsistent mapping
		 * but if they are in the same address space
		 * we run the risk of unmapping the pc or the
		 * stack which we will use as we return to the user,
		 * in which case we can then fault on the thing
		 * we just unloaded and get into an infinite loop.
		 * If we have a locked mapping on the mapping list
		 * we can't unload so it is also an uncache conflict.
		 */
		if ((hat->hat_as == as) || hmeblkp->hblk_lckcnt) {
			retval = UNCACHE_CONFLICT;
			break;
		} else {
			/*
			 * default is to unload
			 */
			retval = UNLOAD_CONFLICT;
		}
	}
	return (retval);
}

/*
 * XXX MP.  dcache flushes should be mpized
 */
static void
sfmmu_resolve_consistency(struct page *pp, int resolution)
{
	struct hat *hat;
	struct sf_hment *sfhme;

	ASSERT(sfmmu_mlist_held(pp));

	switch (resolution) {

	default:
		cmn_err(CE_PANIC, "sfmmu_resolve_consistency: bad resolution");
		break;

	case PGCOLOR_CONFLICT:
		SFMMU_STAT(vh_pgcolor_conflict);
		sfmmu_cache_flush(pp->p_pagenum, PP_GET_VCOLOR(pp));
		break;

	case UNCACHE_CONFLICT:
		SFMMU_STAT(vh_uncache_conflict);
		sfmmu_page_cache(pp, HAT_TMPNC);
		break;

	case UNLOAD_CONFLICT:
		SFMMU_STAT(vh_unload_conflict);
		while ((sfhme = (struct sf_hment *)pp->p_mapping) != NULL) {
			hat = &hats[sfhme->hme_hat];
			if (hat->hat_op == &sfmmu_hatops) {
				sfmmu_pageunload(pp, sfhme);
			} else {
				HATOP_PAGEUNLOAD(hat, pp,
					(struct hment *)sfhme);
			}
		}
		ASSERT(pp->p_mapping == NULL);
		/*
		 * unloads only does tlb flushes so we need to flush the
		 * cache here.
		 */
		sfmmu_cache_flush(pp->p_pagenum, PP_GET_VCOLOR(pp));
		break;
	}
	sfmmu_xcall_sync(cpu_ready_set);
}

/*
 * This function changes the virtual cacheability of all mappings to a
 * particular page.  When changing from uncache to cacheable the mappings will
 * only be changed if all of them have the same virtual color.
 */
static void
sfmmu_page_cache(struct page *pp, int flags)
{
	struct	sf_hment *sfhme, *tmphme = NULL;
	struct	hme_blk *hmeblkp;
	struct	sfmmu *sfmmup;
	tte_t	tte, ttemod;
	caddr_t	vaddr;
	int	color, clr_valid = 0;

	ASSERT(pp != NULL);
	ASSERT(sfmmu_mlist_held(pp));
	ASSERT(!(cache & CACHE_WRITEBACK));

	if ((flags == HAT_CACHE) && PP_ISPNC(pp))
		return;

	kpreempt_disable();

	/*
	 * We need to capture all cpus in order to change cacheability
	 * because we can't allow one cpu to access the same physical
	 * page using a cacheable and a non-cachebale mapping at the same
	 * time.
	 * A cache and tlb flush on all cpus who has referenced this page
	 * is necessary when going from cacheble to unencacheable.  A tlbflush
	 * is optional from going to unencacheable to cacheable and probably
	 * desireable because it allows the page to be cached sooner than
	 * waiting for the tlb entry to be invalidated randomly.
	 */
	xc_attention(cpu_ready_set);

redo:
	for (sfhme = (struct sf_hment *)pp->p_mapping; sfhme; sfhme = tmphme) {
		tmphme = (struct sf_hment *)sfhme->hme_next;
		if (!sfhme_is_sfmmu(sfhme)) {
			sfhme = (struct sf_hment *)sfhme->hme_next;
			continue;
		}
		hmeblkp = sfmmu_hmetohblk(sfhme);
		MMU_COPYTTE(&sfhme->hme_tte, &tte);
		ASSERT(TTE_IS_VALID(&tte));
		vaddr = tte_to_vaddr(hmeblkp, tte);

		if (flags == HAT_CACHE) {
			if (!clr_valid) {
				color = addr_to_vcolor(vaddr);
				clr_valid = 1;
			} else if (color != addr_to_vcolor(vaddr)) {
				/*
				 * if we detect two mappings that disagree
				 * on the virtual color we abort the caching
				 * and redo all mappings to be unencached.
				 */
				flags = HAT_TMPNC;
				goto redo;
			}
		}


		ttemod = tte;
		if (flags & (HAT_UNCACHE | HAT_TMPNC)) {
			TTE_CLR_VCACHEABLE(&ttemod);
		} else {	/* flags & HAT_CACHE */
			TTE_SET_VCACHEABLE(&ttemod);
		}
		while (MMU_MODIFYTTE_TRY(&tte, &ttemod, &sfhme->hme_tte)) {
			/*
			 * Since all cpus are captured modifytte should not
			 * fail.
			 */
			cmn_err(CE_PANIC,
				"sfmmu_page_cache: write to tte failed");
		}
		sfmmup = hblktosfmmu(hmeblkp);
		if (flags & (HAT_UNCACHE | HAT_TMPNC)) {
			/* flush tlb and caches */
			sfmmu_tlbcache_demap(vaddr, sfmmup,
				get_hblk_ttesz(hmeblkp), pp->p_pagenum, 0);
		} else {	/* flags & HAT_CACHE */
			/* flush tlb only */
			sfmmu_tlb_demap(vaddr, sfmmup,
				get_hblk_ttesz(hmeblkp), 0);
		}
		sfhme = (struct sf_hment *)sfhme->hme_next;
	}
	switch (flags) {

		default:
			cmn_err(CE_PANIC, "sfmmu_pagecache: unknown flags\n");
			break;

		case HAT_CACHE:
			PP_CLRTNC(pp);
			PP_CLRPNC(pp);
			PP_SET_VCOLOR(pp, color);
			break;

		case HAT_TMPNC:
			PP_SETTNC(pp);
			break;

		case HAT_UNCACHE:
			PP_SETPNC(pp);
			PP_CLRTNC(pp);
			break;
	}
	xc_dismissed(cpu_ready_set);
	kpreempt_enable();
}

/*
 * This routine converts virtual page protections to physical ones.  It will
 * update the tteflags field with the tte mask corresponding to the protections
 * affected and it returns the new protections.  It will also clear the modify
 * bit if we are taking away write permission.  This is necessary since the
 * modify bit is the hardware permission bit and we need to clear it in order
 * to detect write faults.
 * It accepts the following special protections:
 * ~PROT_WRITE = remove write permissions.
 * ~PROT_USER = remove user permissions.
 */
static u_int
sfmmu_vtop_prot(u_int vprot, u_int *tteflagsp)
{
	if (vprot == (uint)~PROT_WRITE) {
		*tteflagsp = TTE_WRPRM_INT | TTE_HWWR_INT;
		return (0);		/* will cause wrprm to be cleared */
	}
	if (vprot == (uint)~PROT_USER) {
		*tteflagsp = TTE_PRIV_INT;
		return (0);		/* will cause privprm to be cleared */
	}
	if ((vprot == 0) || (vprot == PROT_USER) ||
		((vprot & PROT_ALL) != vprot)) {
		cmn_err(CE_PANIC, "sfmmu_vtop_prot -- bad prot %x\n", vprot);
	}

	switch (vprot) {

	case (PROT_READ):
	case (PROT_EXEC):
	case (PROT_EXEC | PROT_READ):
		*tteflagsp = TTE_PRIV_INT | TTE_WRPRM_INT | TTE_HWWR_INT;
		return (TTE_PRIV_INT); 		/* set prv and clr wrt */
	case (PROT_WRITE):
	case (PROT_WRITE | PROT_READ):
	case (PROT_EXEC | PROT_WRITE):
	case (PROT_EXEC | PROT_WRITE | PROT_READ):
		*tteflagsp = TTE_PRIV_INT | TTE_WRPRM_INT;
		return (TTE_PRIV_INT | TTE_WRPRM_INT); 	/* set prv and wrt */
	case (PROT_USER | PROT_READ):
	case (PROT_USER | PROT_EXEC):
	case (PROT_USER | PROT_EXEC | PROT_READ):
		*tteflagsp = TTE_PRIV_INT | TTE_WRPRM_INT | TTE_HWWR_INT;
		return (0); 			/* clr prv and wrt */
	case (PROT_USER | PROT_WRITE):
	case (PROT_USER | PROT_WRITE | PROT_READ):
	case (PROT_USER | PROT_EXEC | PROT_WRITE):
	case (PROT_USER | PROT_EXEC | PROT_WRITE | PROT_READ):
		*tteflagsp = TTE_PRIV_INT | TTE_WRPRM_INT;
		return (TTE_WRPRM_INT); 	/* clr prv and set wrt */
	default:
		cmn_err(CE_PANIC, "sfmmu_vtop_prot -- bad prot %x\n", vprot);
	}
	return (0);
}


/*
 * This routine gets called when the system has run out of free contexts.
 * This will simply choose context passed to it to be stolen and reused.
*/
static void
sfmmu_reuse_ctx(struct ctx *ctx, struct sfmmu *sfmmup)
{
	struct sfmmu *stolen_sfmmup;
	cpuset_t cpuset;
	u_short	cnum = ctxtoctxnum(ctx);

	ASSERT(MUTEX_HELD(&ctx_lock));
	ASSERT(ctx->c_refcnt == HWORD_WLOCK);

	/*
	 * simply steal and reuse the ctx passed to us.
	 */
	stolen_sfmmup = ctx->c_sfmmu;
	ASSERT(stolen_sfmmup->sfmmu_cnum == cnum);

	TRACE_CTXS(ctx_trace_ptr, cnum, stolen_sfmmup, sfmmup, CTX_STEAL);
	SFMMU_STAT(vh_ctxsteal);

	/*
	 * Disable preemption. Capture other CPUS since TLB flush and
	 * TSB unload should be atomic. When we have a per-CPU TSB, we
	 * can do both atomically in one x-call and then we wouldn't need
	 * to do capture/release.
	 */
	kpreempt_disable();

	/*
	 * Update sfmmu and ctx structs. After this point all threads
	 * belonging to this hat/proc will fault and not use the ctx
	 * being stolen.
	 */
	stolen_sfmmup->sfmmu_cnum = INVALID_CONTEXT;
	ctx->c_sfmmu = NULL;
	membar_stld();

	cpuset = stolen_sfmmup->sfmmu_cpusran;
	CPUSET_DEL(cpuset, CPU->cpu_id);
	cpuset &= cpu_ready_set;
	xc_attention(cpuset);

	/*
	 * 1. flush TLB in all CPUs that ran the process whose ctx we are
	 *    stealing.
	 * 2. change context for all other CPUs to INVALID_CONTEXT,
	 *    if they are running in the context that we are going to steal.
	 */
	xt_some(cpuset, (u_int)sfmmu_ctx_steal_tl1, (u_int)cnum,
	    INVALID_CONTEXT, 0, 0);
	MMU_XCALL_SYNC_MP(cpuset);

	/*
	 * flush tlb of local processor
	 */
	MMU_TLBFLUSH_CTX(cnum);

	/*
	 * flush global TSB
	 */
	mmu_unload_tsbctx(cnum);

	/*
	 * Release all pinned CPUs and enable preemption.
	 * XXX: we can possibly unpin the CPUS before we flush
	 * the local tlb/global TSB. Avoided this optimization
	 * since capture/release will anyway go away with per-CPU
	 * TSB.
	 */
	xc_dismissed(cpuset);
	kpreempt_enable();

}

/*
 * Returns with context reader lock.
 */
static struct ctx *
sfmmu_get_ctx(struct sfmmu *sfmmup)
{
	struct ctx *ctx;
	u_short	cnum;
	struct ctx *lastctx = &ctxs[nctxs-1];
	struct ctx *firstctx = &ctxs[NUM_LOCKED_CTXS];
	u_int	found_stealable_ctx;
	u_int	retry_count = 0;

#define	NEXT_CTX(ctx)   (((ctx) >= lastctx) ? firstctx : ((ctx) + 1))

retry:
	mutex_enter(&ctx_lock);

	/*
	 * Check to see if this process has already got a ctx.
	 * In that case just set the sec-ctx, release ctx_lock and return.
	 */
	if (sfmmup->sfmmu_cnum >= NUM_LOCKED_CTXS) {
		ctx = sfmmutoctx(sfmmup);
		rwlock_hword_enter(&ctx->c_refcnt, READER_LOCK);
		mutex_exit(&ctx_lock);
		return (ctx);
	}

	found_stealable_ctx = 0;
	if ((ctx = ctxfree) != NULL) {
		/*
		 * Found a ctx in free list. Delete it from the list and
		 * use it.
		 */
		SFMMU_STAT(vh_ctxfree);
		ctxfree = ctx->c_free;
		ctx->c_flags = 0;

	} else {
		/*
		 * no free context available, steal approp ctx.
		 * The policy to choose the aprop context is very simple.
		 * Just sweep all the ctxs using ctxhand. This will steal
		 * the LRU ctx.
		 *
		 * We however only steal a context whose c_refcnt rlock can
		 * be grabbed. Keep searching till we find a stealable ctx.
		 */
		ctx = ctxhand;
		do {
			/*
			 * If you get the writers lock, you can steal this
			 * ctx.
			 */
			if (rwlock_hword_enter(&ctx->c_refcnt, WRITER_LOCK)
				== 0) {
				found_stealable_ctx = 1;
				break;
			}
			ctx = NEXT_CTX(ctx);
		} while (ctx != ctxhand);

		if (found_stealable_ctx) {
			/*
			 * Try and reuse the ctx.
			 */
			sfmmu_reuse_ctx(ctx, sfmmup);

		} else if (retry_count++ < GET_CTX_RETRY_CNT) {
			mutex_exit(&ctx_lock);
			goto retry;

		} else {
			cmn_err(CE_PANIC, "Can't find any stealable context\n");
		}
	}

	ctx->c_sfmmu = sfmmup;
	cnum = ctxtoctxnum(ctx);
	sfmmup->sfmmu_cnum = cnum;

	/*
	 * If this sfmmu has an ism-map, setup the ctx struct.
	 */
	if (sfmmup->sfmmu_ismblk) {
		ctx->c_ismblkpa = va_to_pa((caddr_t)sfmmup->sfmmu_ismblk);
	} else {
		ctx->c_ismblkpa = (u_longlong_t)-1;
	}

	/*
	 * Set up the c_flags field.
	 */
	if (sfmmup->sfmmu_lttecnt) {
		ctx->c_flags |= LTTES_FLAG;
	} else {
		ctx->c_flags = 0;
	}

	/*
	 * If ctx stolen, release the writers lock.
	 */
	if (found_stealable_ctx)
		rwlock_hword_exit(&ctx->c_refcnt, WRITER_LOCK);

	/*
	 * Set the reader lock.
	 */
	rwlock_hword_enter(&ctx->c_refcnt, READER_LOCK);

	ctxhand = NEXT_CTX(ctx);

	ASSERT(sfmmup == sfmmutoctx(sfmmup)->c_sfmmu);
	mutex_exit(&ctx_lock);

	return (ctx);

#undef	NEXT_CTX
}

/*
 * Free up a ctx
 */
static void
sfmmu_free_ctx(struct sfmmu *sfmmup, struct ctx *ctx)
{
	int ctxnum;

	mutex_enter(&ctx_lock);

	TRACE_CTXS(ctx_trace_ptr, sfmmup->sfmmu_cnum, sfmmup,
	    0, CTX_FREE);

	if (sfmmup->sfmmu_cnum == INVALID_CONTEXT) {
		sfmmup->sfmmu_cpusran = 0;
		sfmmup->sfmmu_cnum = 0;
		mutex_exit(&ctx_lock);
		return;
	}

	ASSERT(sfmmup == ctx->c_sfmmu);

	ctx->c_ismblkpa = (u_longlong_t)-1;
	ctx->c_sfmmu = NULL;
	ctx->c_refcnt = 0;
	ctx->c_flags = 0;
	sfmmup->sfmmu_cpusran = 0;
	sfmmup->sfmmu_cnum = 0;
	ctxnum = sfmmu_getctx_sec();
	if (ctxnum == ctxtoctxnum(ctx)) {
		sfmmu_setctx_sec(INVALID_CONTEXT);
	}

	/*
	 * Put the freed ctx back on the free list.
	 */
	ctx->c_free = ctxfree;
	ctxfree = ctx;

	mutex_exit(&ctx_lock);
}

/*
 * Free up a sfmmu
 * Since the sfmmu is currently embedded in the hat struct we simply zero
 * out our fields
 */
static void
sfmmu_free_sfmmu(struct sfmmu *sfmmup)
{
	ASSERT(sfmmup->sfmmu_lttecnt == 0);
	sfmmup->sfmmu_cnum = 0;
	sfmmup->sfmmu_free = 0;
}

/*
 * Locking primitves accessed by HATLOCK macros
 */

static void
sfmmu_page_enter(pp)
	struct page *pp;
{
	kmutex_t	*mml;

	ASSERT(pp != NULL);

	mml = MLIST_HASH(pp);
	mutex_enter(mml);
}

static void
sfmmu_page_exit(pp)
	struct page *pp;
{
	kmutex_t	*mml;

	ASSERT(pp != NULL);

	mml = MLIST_HASH(pp);
	mutex_exit(mml);
}

static void
sfmmu_hat_enter(as)
	struct as *as;
{
#ifdef lint
	as = as;
#endif /* lint */
	/* SFMMU doesn't use the per hat mutex */
}

static void
sfmmu_hat_exit(as)
	struct as *as;
{
#ifdef lint
	as = as;
#endif /* lint */
	/* SFMMU doesn't use the per hat mutex */
}

#ifdef DEBUG
/* XXX */
#define	MLIST_ENTER_STAT()
#define	MLIST_WAIT_STAT()
#define	MLIST_EXIT_STAT()
#define	MLIST_BROADCAST_STAT()

#else

#define	MLIST_ENTER_STAT()
#define	MLIST_WAIT_STAT()
#define	MLIST_EXIT_STAT()
#define	MLIST_BROADCAST_STAT()

#endif

/*
 * Sfmmu internal version of mlist enter/exit.
 */
static kmutex_t *
sfmmu_mlist_enter(pp)
	struct page *pp;
{
	kmutex_t	*mml;

	ASSERT(pp != NULL);

	mml = MLIST_HASH(pp);
	mutex_enter(mml);

	return (mml);
}

static void
sfmmu_mlist_exit(kmutex_t *mml)
{
	mutex_exit(mml);
}


/*
 * Sfmmu external version of mlist enter/exit - accessed via hat_lockops.
 */
static void
sfvec_mlist_enter(pp)
	struct page *pp;
{
	kmutex_t	*mml;

	ASSERT(pp != NULL);

	mml = MLIST_HASH(pp);
	mutex_enter(mml);
}

static void
sfvec_mlist_exit(pp)
	struct page *pp;
{
	kmutex_t	*mml;

	ASSERT(pp != NULL);

	mml = MLIST_HASH(pp);
	mutex_exit(mml);
}

static int
sfmmu_mlist_held(pp)
	struct page *pp;
{
	kmutex_t	*mml;

	ASSERT(pp != NULL);

	mml = MLIST_HASH(pp);
	return (MUTEX_HELD(mml));
}


/*
 * returns a free hmeblk with 8 hments or with 1 hment depending on size.
 * It will return NULL in case it had to dynamically allocate a hmeblkp,
 * in which case locks were dropped and the caller needs to handle it.
 *
 * RFE: it might be worth it to allocate out of the nucleus hmeblks until we
 * run out.  Only then would we start to allocate dynamically.  We would need
 * to implement a hmeblk stealer though.  This would reduce the number of
 * tlb misses while in the hat.  Of course, an  even better rfe would be
 * to modify kmem_alloc so it understands nucleus memory and tries to allocate
 * from it first.  This way the kernel tlb miss rate would drop.
 */
static struct hme_blk *
sfmmu_hblk_alloc(struct sfmmu *sfmmup, caddr_t vaddr,
	struct hmehash_bucket *hmebp, int size, hmeblk_tag hblktag)
{
	struct hme_blk *hmeblkp = NULL;
	struct hme_blk *newhblkp;
	struct hme_blk *shw_hblkp = NULL;
	int hmelock_held = 1;
	extern int hblk1_avail;
	extern int hblk8_avail;
	extern struct hme_blk *hblk1_flist;
	extern struct hme_blk *hblk8_flist;

	ASSERT(SFMMU_HASH_LOCK_ISHELD(hmebp));
	if (hblkalloc_inprog && (sfmmup == KHATID)) {
		hmeblkp = sfmmu_nhblk_alloc(size, hmebp);
		set_hblk_sz(hmeblkp, size);
	} else {

		SFMMU_STAT(vh_hblk_dalloc);

		if ((sfmmup != KHATID) && (size < TTE4M)) {
			SFMMU_HASH_UNLOCK(hmebp);
			hmelock_held = 0;
			shw_hblkp = sfmmu_shadow_hcreate(sfmmup, vaddr, size);
		}

		while (!hmeblkp) {
			HBLK_FLIST_LOCK();
			if ((size == TTE8K) && hblk8_avail) {
				hmeblkp = hblk8_flist;
				hblk8_flist = hmeblkp->hblk_next;
				hblk8_avail--;
				HBLK_FLIST_UNLOCK();
				break;
			} else if ((size != TTE8K) && hblk1_avail) {
				hmeblkp = hblk1_flist;
				hblk1_flist = hmeblkp->hblk_next;
				hblk1_avail--;
				HBLK_FLIST_UNLOCK();
				break;
			} else {
				/*
				 * couldn't find hmblk in free list
				 * so we need to kmemalloc.
				 */
				HBLK_FLIST_UNLOCK();
				if (hmelock_held) {
					SFMMU_HASH_UNLOCK(hmebp);
					hmelock_held = 0;
				}
				sfmmu_hblk_grow(size);
			}
		}
		set_hblk_sz(hmeblkp, size);
		if (!hmelock_held) {
			SFMMU_HASH_LOCK(hmebp);
			HME_HASH_FAST_SEARCH(hmebp, hblktag, newhblkp);
			if (newhblkp != NULL) {
				sfmmu_hblk_tofreelist(hmeblkp);
				return (newhblkp);
			}
		}
	}
	ASSERT(SFMMU_HASH_LOCK_ISHELD(hmebp));
	ASSERT(hmeblkp);
	hmeblkp->hblk_next = (struct hme_blk *)NULL;
	hmeblkp->hblk_nextpa = 0;
	hmeblkp->hblk_tag = hblktag;
	hmeblkp->hblk_shadow = shw_hblkp;
	ASSERT(get_hblk_ttesz(hmeblkp) == size);
	ASSERT(get_hblk_span(hmeblkp) == HMEBLK_SPAN(size));
	ASSERT(hmeblkp->hblk_hmecnt == 0);
	ASSERT(hmeblkp->hblk_vcnt == 0);
	ASSERT(hmeblkp->hblk_lckcnt == 0);
	sfmmu_hblk_hash_add(hmebp, hmeblkp);
	return (hmeblkp);
}

/*
 * returns a free nucleus hmeblk with 8 hmes or with 1 hme depending on size.
 */
static struct hme_blk *
sfmmu_nhblk_alloc(int size, struct hmehash_bucket *hmebp)
{
	struct hme_blk *hmeblkp;
	extern int nhblk1_avail;
	extern int nhblk8_avail;
	extern struct hme_blk *nhblk1_flist;
	extern struct hme_blk *nhblk8_flist;

	ASSERT(SFMMU_HASH_LOCK_ISHELD(hmebp));
	SFMMU_STAT(vh_hblk_nalloc);
	NHBLK_FLIST_LOCK();
	if (size == TTE8K) {
		if (nhblk8_avail) {
			hmeblkp = nhblk8_flist;
			nhblk8_flist = hmeblkp->hblk_next;
			nhblk8_avail--;
		} else {
			hmeblkp = sfmmu_nmlist_alloc(HME8BLK_SZ);
		}
	} else {
		if (nhblk1_avail) {
			hmeblkp = nhblk1_flist;
			nhblk1_flist = hmeblkp->hblk_next;
			nhblk1_avail--;
		} else {
			hmeblkp = sfmmu_nmlist_alloc(HME1BLK_SZ);
		}
	}

	NHBLK_FLIST_UNLOCK();
	ASSERT(hmeblkp);
	hmeblkp->hblk_nuc_bit = 1;
	return (hmeblkp);
}

/*
 * This function performs any cleanup required on the hme_blk
 * and returns it to the free list.
 */
static void
sfmmu_hblk_free(struct hmehash_bucket *hmebp, struct hme_blk *hmeblkp)
{
	int ttesz, shw_size, vshift;
	struct sfmmu *sfmmup;
	struct hme_blk *shw_hblkp;
	uint	shw_mask, newshw_mask, vaddr;
	extern int cas();

	ASSERT(hmeblkp);
	ASSERT(!hmeblkp->hblk_hmecnt);
	ASSERT(!hmeblkp->hblk_vcnt);
	ASSERT(!hmeblkp->hblk_lckcnt);
	ASSERT(SFMMU_HASH_LOCK_ISHELD(hmebp));

	ttesz = get_hblk_ttesz(hmeblkp);
	sfmmup = hblktosfmmu(hmeblkp);
	shw_hblkp = hmeblkp->hblk_shadow;
	if (shw_hblkp) {
		ASSERT(sfmmup != KHATID);
		ASSERT(ttesz < TTE4M);
		shw_size = get_hblk_ttesz(shw_hblkp);
		vaddr = get_hblk_base(hmeblkp);
		vshift = vaddr_to_vshift(shw_hblkp->hblk_tag, vaddr, shw_size);
		ASSERT(vshift < 8);
		/*
		 * Atomically clear shadow mask bit
		 */
		do {
			shw_mask = shw_hblkp->hblk_shw_mask;
			ASSERT(shw_mask & (1 << vshift));
			newshw_mask = shw_mask & ~(1 << vshift);
			newshw_mask = cas(&shw_hblkp->hblk_shw_mask,
				shw_mask, newshw_mask);
		} while (newshw_mask != shw_mask);
	}
	sfmmu_hblk_tofreelist(hmeblkp);
}

/*
 * This function puts an hmeblk back in the freelist
 */
static void
sfmmu_hblk_tofreelist(struct hme_blk *hmeblkp)
{
	int ttesz;

	extern struct hme_blk   *hblk1_flist;
	extern struct hme_blk   *hblk8_flist;
	extern int		hblk1_avail;
	extern int		hblk8_avail;
	extern struct hme_blk   *nhblk1_flist;
	extern struct hme_blk   *nhblk8_flist;
	extern int		nhblk1_avail;
	extern int		nhblk8_avail;

	ASSERT(hmeblkp);
	ASSERT(!hmeblkp->hblk_hmecnt);
	ASSERT(!hmeblkp->hblk_vcnt);

	ttesz = get_hblk_ttesz(hmeblkp);

#ifdef DEBUG
	/*
	 * Debug code that verifies hblk lists are correct
	 */
	{
		int i;
		struct hme_blk *hblktmp;

		HBLK_FLIST_LOCK();
		for (i = 0, hblktmp = hblk8_flist; hblktmp;
			hblktmp = hblktmp->hblk_next, i++);

		if (i != hblk8_avail) {
			cmn_err(CE_PANIC,
				"sfmmu_hblk_free: inconsistent hblk8_flist");
		}
		HBLK_FLIST_UNLOCK();
		NHBLK_FLIST_LOCK();
		for (i = 0, hblktmp = nhblk8_flist; hblktmp;
			hblktmp = hblktmp->hblk_next, i++);

		if (i != nhblk8_avail) {
			cmn_err(CE_PANIC,
				"sfmmu_hblk_free: inconsistent nhblk8_flist");
		}
		NHBLK_FLIST_UNLOCK();
	}
#endif /* DEBUG */

	if (hmeblkp->hblk_nuc_bit) {
		NHBLK_FLIST_LOCK();
		if (ttesz == TTE8K) {
			hmeblkp->hblk_next = nhblk8_flist;
			nhblk8_flist = hmeblkp;
			nhblk8_avail++;
		} else {
			hmeblkp->hblk_next = nhblk1_flist;
			nhblk1_flist = hmeblkp;
			nhblk1_avail++;
		}
		hmeblkp->hblk_nextpa = 0;
		hmeblkp->hblk_shw_bit = 0;
		NHBLK_FLIST_UNLOCK();
	} else {
		HBLK_FLIST_LOCK();
		if (ttesz == TTE8K) {
			ASSERT(hmeblkp->hblk_hme[1].hme_page == NULL);
			ASSERT(hmeblkp->hblk_hme[1].hme_next == NULL);
			ASSERT(!hmeblkp->hblk_hme[1].hme_tte.tte_inthi ||
				hmeblkp->hblk_hme[1].hme_tte.tte_hmenum == 1);
			hmeblkp->hblk_next = hblk8_flist;
			hblk8_flist = hmeblkp;
			hblk8_avail++;
		} else {
			hmeblkp->hblk_next = hblk1_flist;
			hblk1_flist = hmeblkp;
			hblk1_avail++;
		}
		hmeblkp->hblk_nextpa = 0;
		hmeblkp->hblk_shw_bit = 0;
		HBLK_FLIST_UNLOCK();
	}
}

static void
sfmmu_hblk_grow(int size)
{
	struct hme_blk *hmeblkp;
	int i;

	extern struct hme_blk *hblk1_flist;
	extern struct hme_blk *hblk8_flist;
	extern int hblk1_avail;
	extern int hblk8_avail;

	HBLK_FLIST_LOCK();
	if (size == TTE8K) {
		if (hblk8_avail > HME8_TRHOLD) {
			HBLK_FLIST_UNLOCK();
			return;
		}
		hblkalloc_inprog = 1;
		for (i = 0; i < HBLK_GROW_NUM; i++) {
			hmeblkp = kmem_cache_alloc(sfmmu8_cache, KM_SLEEP);
			/*
			 * make sure hmeblk doesn't cross page boundaries
			 * before using it
			 */
			ASSERT((((uint) hmeblkp & MMU_PAGEOFFSET) +
				HME8BLK_SZ) <= MMU_PAGESIZE);
			set_hblk_sz(hmeblkp, size);
			hmeblkp->hblk_next = hblk8_flist;
			hblk8_flist = hmeblkp;
			hblk8_avail++;
		}
	} else {
		if (hblk1_avail > HME1_TRHOLD) {
			HBLK_FLIST_UNLOCK();
			return;
		}
		hblkalloc_inprog = 1;
		for (i = 0; i < HBLK_GROW_NUM; i++) {
			hmeblkp = kmem_cache_alloc(sfmmu1_cache, KM_SLEEP);
			/*
			 * make sure hmeblk doesn't cross page boundaries
			 * before using it
			 */
			ASSERT((((uint) hmeblkp & MMU_PAGEOFFSET) +
				HME1BLK_SZ) <= MMU_PAGESIZE);
			hmeblkp->hblk_next = hblk1_flist;
			hblk1_flist = hmeblkp;
			hblk1_avail++;
		}
	}
	hblkalloc_inprog = 0;
	HBLK_FLIST_UNLOCK();
}

/*
 * HME_BLK HASH PRIMITIVES
 */

/*
 * This function returns the hment given the hme_blk and a vaddr.
 * It assumes addr has already been checked to belong to hme_blk's
 * range.  If hmenump is passed then we update it with the index.
 */
static struct sf_hment *
sfmmu_hblktohme(struct hme_blk *hmeblkp, caddr_t addr, int *hmenump)
{
	int index = 0;

	ASSERT(in_hblk_range(hmeblkp, addr));

	if (get_hblk_ttesz(hmeblkp) == TTE8K) {
		index = (((u_int)addr >> MMU_PAGESHIFT) & (NHMENTS-1));
	}

	if (hmenump) {
		*hmenump = index;
	}

	return (&hmeblkp->hblk_hme[index]);
}

static struct hme_blk *
sfmmu_hmetohblk(struct sf_hment *sfhme)
{
	struct hme_blk *hmeblkp;
	struct sf_hment *sfhme0;
	struct hme_blk *hblk_dummy = 0;

	sfhme0 = sfhme - sfhme->hme_tte.tte_hmenum;
	hmeblkp = (struct hme_blk *)((u_int)sfhme0 -
		(u_int)&hblk_dummy->hblk_hme[0]);

	ASSERT(hmeblkp->hblk_tag.htag_id == hattosfmmu(sfhmetohat(sfhme)));
	return (hmeblkp);
}

/*
 * XXX This will change for ISM 2
 * This code will only be called for a user process that has more than
 * ISM_MAP_SLOTS segments. Performance is not a priority in this case.
 *
 * This routine will look for a user vaddr and hatid in the hash
 * structure.  It returns a valid pfn or -1.
 */
u_int
sfmmu_user_vatopfn(caddr_t vaddr, struct sfmmu *sfmmup)
{
	struct hmehash_bucket *hmebp;
	hmeblk_tag hblktag;
	int hmeshift, hashno = 1;
	struct hme_blk *hmeblkp, *nx_hblk, *pr_hblk = NULL;
	struct sf_hment *sfhmep;
	u_int pfn;
	tte_t tte;

	u_char		sh_vaddr = 0;
	ism_map_t	*ism_map;
	ism_map_blk_t	*ismblkp;
	int		i;
	struct sfmmu *ism_sfmmu = NULL;

	ASSERT(sfmmup != KHATID);
	pfn = (u_int)-1;

	sh_vaddr = ISM_SHIFT(vaddr);
	ismblkp = sfmmup->sfmmu_ismblk;

	/*
	 * Set ism_sfmmu if vaddr falls in a ISM segment.
	 */
	while (ismblkp) {
		ism_map = ismblkp->maps;
		for (i = 0; ism_map[i].ism_sfmmu && i < ISM_MAP_SLOTS; i++) {
			if (sh_vaddr >= ism_map[i].vbase &&
				sh_vaddr < (u_char) (ism_map[i].vbase
						    + ism_map[i].size)) {
				ism_sfmmu = ism_map[i].ism_sfmmu;
				goto ism_hit;
			}
		}
		ismblkp = ismblkp->next;
	}
ism_hit:

	/*
	 * If lookup into ISM segment then vaddr is converted to offset
	 * into owning ISM segment. All owning ISM segments
	 * start at 0x0.
	 */
	if (ism_sfmmu) {
		sfmmup = ism_sfmmu;
		vaddr = (caddr_t)((uint)vaddr -
			((uint)ism_map[i].vbase << ISM_AL_SHIFT));
	}

	hblktag.htag_id = sfmmup;
	do {
		hmeshift = HME_HASH_SHIFT(hashno);
		hblktag.htag_bspage = HME_HASH_BSPAGE(vaddr, hmeshift);
		hblktag.htag_rehash = hashno;
		hmebp = HME_HASH_FUNCTION(sfmmup, vaddr, hmeshift);

		SFMMU_HASH_LOCK(hmebp);

		HME_HASH_SEARCH(hmebp, hblktag, hmeblkp, nx_hblk, pr_hblk);
		if (hmeblkp != NULL) {
			sfhmep = sfmmu_hblktohme(hmeblkp, vaddr, 0);
			MMU_COPYTTE(&sfhmep->hme_tte, &tte);
			if (TTE_IS_VALID(&tte)) {
				pfn = TTE_TO_PFN(vaddr, &tte);
			}
			SFMMU_HASH_UNLOCK(hmebp);
			return (pfn);
		}
		SFMMU_HASH_UNLOCK(hmebp);
		hashno++;
	} while (sfmmup->sfmmu_lttecnt && (hashno <= MAX_HASHCNT));
	return (pfn);
}

/*
 * Make sure that there is a valid ctx, if not get a ctx.
 * Also, get a readers lock on refcnt, so that the ctx cannot
 * be stolen underneath us.
 */
void
sfmmu_disallow_ctx_steal(struct sfmmu *sfmmup)
{
	struct	ctx *ctx;

	ASSERT(sfmmup != ksfmmup);
	/*
	 * If ctx has been stolen, get a ctx.
	 */
	if (sfmmup->sfmmu_cnum == INVALID_CONTEXT) {
		/*
		 * Our ctx was stolen. Get a ctx with rlock.
		 */
		ctx = sfmmu_get_ctx(sfmmup);
		return;
	} else {
		ctx = sfmmutoctx(sfmmup);
	}

	/*
	 * Try to get the reader lock.
	 */
	if (rwlock_hword_enter(&ctx->c_refcnt, READER_LOCK) == 0) {
		/*
		 * Successful in getting r-lock.
		 * Does ctx still point to sfmmu ?
		 * If NO, the ctx got stolen meanwhile.
		 * 	Release r-lock and try again.
		 * If YES, we are done - just exit
		 */
		if (ctx->c_sfmmu != sfmmup) {
			rwlock_hword_exit(&ctx->c_refcnt, READER_LOCK);
			/*
			 * Our ctx was stolen. Get a ctx with rlock.
			 */
			ctx = sfmmu_get_ctx(sfmmup);
		}
	} else {
		/*
		 * Our ctx was stolen. Get a ctx with rlock.
		 */
		ctx = sfmmu_get_ctx(sfmmup);
	}

	ASSERT(sfmmup->sfmmu_cnum >= NUM_LOCKED_CTXS);
	ASSERT(sfmmutoctx(sfmmup)->c_refcnt > 0);
}

/*
 * Decrement reference count for our ctx. If the reference count
 * becomes 0, our ctx can be stolen by someone.
 */
void
sfmmu_allow_ctx_steal(struct sfmmu *sfmmup)
{
	struct	ctx *ctx;

	ASSERT(sfmmup != ksfmmup);
	ctx = sfmmutoctx(sfmmup);

	ASSERT(ctx->c_refcnt > 0);
	ASSERT(sfmmup == ctx->c_sfmmu);
	ASSERT(sfmmup->sfmmu_cnum != INVALID_CONTEXT);
	rwlock_hword_exit(&ctx->c_refcnt, READER_LOCK);

}


/*
 * TLB Handling Routines
 * These routines get called from the trap vector table.
 * In some cases an optimized assembly handler has already been
 * executed.
 */

/*
 * We get here after we have missed in the TSB or taken a mod bit trap.
 * The TL1 assembly routine passes the contents of the tag access register.
 * Since we are only
 * supporting a 32 bit address space we manage this register as an uint.
 * This routine will try to find the hment for this address in the hment
 * hash and if found it will place the corresponding entry on the TSB and
 * If it fails then we will call trap which will call pagefault.
 * This routine is called via sys_trap and thus, executes at TL0
 */
void
sfmmu_tsb_miss(struct regs *rp, uint tagaccess, uint traptype)
{
	struct hmehash_bucket *hmebp;
	struct sfmmu *sfmmup;
	struct sfmmu *sfmmup_orig;
	hmeblk_tag hblktag;
	int hmeshift, hashno = 1;
	struct hme_blk *hmeblkp;

	caddr_t vaddr;
	u_short ctxnum;
	struct sf_hment *sfhmep;
	struct ctx *ctx;
	tte_t tte, ttemod;

	u_char		sh_vaddr;
	caddr_t		tmp_vaddr;
	ism_map_t	*ism_map;
	ism_map_blk_t	*ismblkp;
	int		i;
	struct sfmmu *ism_sfmmu = NULL;

	SFMMU_STAT(vh_slow_tsbmiss);
	tmp_vaddr = vaddr = (caddr_t)(tagaccess & TAGACC_VADDR_MASK);
	sh_vaddr = ISM_SHIFT(vaddr);
	ctxnum = tagaccess & TAGACC_CTX_MASK;

	/*
	 * Make sure we have a valid ctx and that our context doesn't get
	 * stolen after this point.
	 */
	if (ctxnum == KCONTEXT) {
		sfmmup_orig = ksfmmup;
	} else {
		sfmmup_orig = astosfmmu(curthread->t_procp->p_as);
		sfmmu_disallow_ctx_steal(sfmmup_orig);
		ctxnum = sfmmup_orig->sfmmu_cnum;
		sfmmu_setctx_sec(ctxnum);
	}
	ASSERT(sfmmup_orig == ksfmmup || ctxnum >= NUM_LOCKED_CTXS);

	ctx = ctxnumtoctx(ctxnum);
	sfmmup = ctx->c_sfmmu;
	ismblkp = sfmmup->sfmmu_ismblk;

	/*
	 * Set ism_sfmmu if vaddr falls in a ISM segment.
	 */
	while (ismblkp) {
		ism_map = ismblkp->maps;
		for (i = 0; ism_map[i].ism_sfmmu && i < ISM_MAP_SLOTS; i++) {
			if (sh_vaddr >= ism_map[i].vbase &&
				sh_vaddr < (u_char)(ism_map[i].vbase
							+ ism_map[i].size)) {
				ism_sfmmu = ism_map[i].ism_sfmmu;
				goto ism_hit;
			}
		}
		ismblkp = ismblkp->next;
	}
ism_hit:

	/*
	 * If ism tlb miss then vaddr is converted into offset
	 * into owning ISM segment. All owning ISM segments
	 * start at 0x0.
	 */
	if (ism_sfmmu) {
		sfmmup = ism_sfmmu;
		vaddr = (caddr_t)((uint) vaddr -
			((uint)ism_map[i].vbase << ISM_AL_SHIFT));
	}

	hblktag.htag_id = sfmmup;
	do {
		hmeshift = HME_HASH_SHIFT(hashno);
		hblktag.htag_bspage = HME_HASH_BSPAGE(vaddr, hmeshift);
		hblktag.htag_rehash = hashno;
		hmebp = HME_HASH_FUNCTION(sfmmup, vaddr, hmeshift);

		SFMMU_HASH_LOCK(hmebp);

		HME_HASH_FAST_SEARCH(hmebp, hblktag, hmeblkp);
		if (hmeblkp != NULL) {
			sfhmep = sfmmu_hblktohme(hmeblkp, vaddr, 0);
sfmmu_tsbmiss_retry:
			MMU_COPYTTE(&sfhmep->hme_tte, &tte);
			if (TTE_IS_VALID(&tte) &&
			    (!TTE_IS_NFO(&tte) ||
			    traptype != T_INSTR_MMU_MISS)) {
				ttemod = tte;
				if (traptype == T_DATA_PROT) {
					/*
					 * We don't need to flush our tlb
					 * because we did it in our trap
					 * handler.  We also don't need to
					 * unload our tsb because the new entry
					 * will replace it.
					 */
					if (TTE_IS_WRITABLE(&tte)) {
						TTE_SET_MOD(&ttemod);
					} else {
						SFMMU_HASH_UNLOCK(hmebp);
						break;
					}
				}
				TTE_SET_REF(&ttemod);
				if (ism_sfmmu)
					vaddr = tmp_vaddr;
				if (get_hblk_ttesz(hmeblkp) == TTE8K)
					sfmmu_load_tsb(vaddr, ctxnum, &ttemod);
				if (traptype == T_INSTR_MMU_MISS) {
					MMU_ITLB_LD(vaddr, ctxnum, &ttemod);
				} else {
					MMU_DTLB_LD(vaddr, ctxnum, &ttemod);
				}
				if (MMU_MODIFYTTE_TRY(&tte, &ttemod,
				    &sfhmep->hme_tte)) {
					/*
					 * pageunload could have unloaded
					 * the tte for us.  In this case
					 * we might have loaded a stale tte
					 * inside the tlb/tte.  Flush both
					 * just in case and retry.
					 */
					sfmmu_unload_tsb(vaddr, ctxnum,
						get_hblk_ttesz(hmeblkp));
					MMU_TLBFLUSH_PAGE(vaddr, ctxnum);
					goto sfmmu_tsbmiss_retry;
				}
				SFMMU_HASH_UNLOCK(hmebp);
				/*
				 * This assert can't be before loading the
				 * tsb/tlb or a recursive tlb miss is possible
				 * since the hats (and sfmmus) are kmemalloced.
				 */
				ASSERT(ism_sfmmu ||
					(ctxnum == sfmmup->sfmmu_cnum));

				/*
				 * Now we can allow context to be stolen.
				 */
				if (sfmmup_orig != ksfmmup)
					sfmmu_allow_ctx_steal(sfmmup_orig);
				return;
			} else {
				SFMMU_HASH_UNLOCK(hmebp);
				break;
			}
		}
		SFMMU_HASH_UNLOCK(hmebp);
		hashno++;
	} while (sfmmup->sfmmu_lttecnt && (hashno <= MAX_HASHCNT));
	ASSERT(ism_sfmmu || (ctxnum == sfmmup->sfmmu_cnum));

	/*
	 * Now we can allow our context to be stolen.
	 */
	if (sfmmup_orig != ksfmmup)
		sfmmu_allow_ctx_steal(sfmmup_orig);

	/* will call pagefault */
	trap(rp, traptype, 0, (caddr_t)tagaccess);
}

/*
 * Flushes caches and tlbs on all cpus for a particular virtual address
 * and ctx.  if noflush is set we do not flush the tlb.
 */
static void
sfmmu_tlbcache_demap(caddr_t addr, struct sfmmu *sfmmup, int size, int pfnum,
	int noflush)
{
	int ctxnum;
	cpuset_t cpuset;

	ctxnum = (int)sfmmutoctxnum(sfmmup);

	if (ctxnum == INVALID_CONTEXT) {
		/*
		 * if ctx was stolen then simply return
		 * whoever stole ctx is responsible for flush.
		 */
		return;
	}

	/*
	 * There is no need to protect against ctx being stolen.  If the ctx
	 * is stolen we will simply get an extra flush.
	 */
	sfmmu_unload_tsb(addr, ctxnum, size);
	kpreempt_disable();
	cpuset = sfmmup->sfmmu_cpusran;
	CPUSET_DEL(cpuset, CPU->cpu_id);
	cpuset &= cpu_ready_set;
	MMU_TLBCACHE_FLUSHPAGE_MP(addr, ctxnum, pfnum, cpuset);
	CPU_DCACHE_FLUSHPAGE(pfnum, addr_to_vcolor(addr));
	if (!noflush) {
		MMU_TLBFLUSH_PAGE(addr, ctxnum);
	}
	kpreempt_enable();
}

void
sfmmu_cache_flush(int pfnum, int vcolor)
{
	cpuset_t cpuset;
	extern cpuset_t cpu_ready_set;

	kpreempt_disable();
	cpuset = cpu_ready_set;
	CPUSET_DEL(cpuset, CPU->cpu_id);
	CPU_DCACHE_FLUSHPAGE_MP(pfnum, vcolor, cpuset);
	CPU_DCACHE_FLUSHPAGE(pfnum, vcolor);
	kpreempt_enable();
}

/*
 * Demaps the tsb and flushes all tlbs on all cpus for a particular virtual
 * address and ctx. if noflush is set we do not flush the tlb.
 */
static void
sfmmu_tlb_demap(caddr_t addr, struct sfmmu *sfmmup, int size, int noflush)
{
	int ctxnum;
	cpuset_t cpuset;

	ctxnum = (int)sfmmutoctxnum(sfmmup);
	if (ctxnum == INVALID_CONTEXT) {
		/*
		 * if ctx was stolen then simply return
		 * whoever stole ctx is responsible for flush.
		 */
		return;
	}

	/*
	 * There is no need to protect against ctx being stolen.  If the
	 * ctx is stolen we will simply get an extra flush.
	 */
	sfmmu_unload_tsb(addr, ctxnum, size);
	if (!noflush) {
		/*
		 * if process is exiting then delay flush.
		 */
		kpreempt_disable();
		cpuset = sfmmup->sfmmu_cpusran;
		CPUSET_DEL(cpuset, CPU->cpu_id);
		cpuset &= cpu_ready_set;
		MMU_TLBFLUSH_PAGE_MP(addr, ctxnum, cpuset);
		MMU_TLBFLUSH_PAGE(addr, ctxnum);
		kpreempt_enable();
	}
}

static
void
sfmmu_ctx_demap(struct sfmmu *sfmmup)
{
	int ctxnum;
	cpuset_t cpuset;

	ctxnum = (int)sfmmutoctxnum(sfmmup);
	if (ctxnum == INVALID_CONTEXT) {
		/*
		 * if ctx was stolen then simply return
		 * whoever stole ctx is responsible for flush.
		 */
		return;
	}
	/*
	 * There is no need to protect against ctx being stolen.  If the
	 * ctx is stolen we will simply get an extra flush.
	 */
	kpreempt_disable();

	/*
	 * flush global TSB
	 */
	mmu_unload_tsbctx(ctxnum);

	cpuset = sfmmup->sfmmu_cpusran;
	CPUSET_DEL(cpuset, CPU->cpu_id);
	cpuset &= cpu_ready_set;
	MMU_TLBFLUSH_CTX_MP(ctxnum, cpuset);
	MMU_TLBFLUSH_CTX(ctxnum);

	kpreempt_enable();
}

static
void
sfmmu_tlbctx_demap(struct sfmmu *sfmmup)
{
	int ctxnum;
	cpuset_t cpuset;

	ctxnum = (int)sfmmutoctxnum(sfmmup);
	if (ctxnum == INVALID_CONTEXT) {
		/*
		 * if ctx was stolen then simply return
		 * whoever stole ctx is responsible for flush.
		 */
		return;
	}
	/*
	 * There is no need to protect against ctx being stolen.  If the
	 * ctx is stolen we will simply get an extra flush.
	 */
	kpreempt_disable();
	cpuset = sfmmup->sfmmu_cpusran;
	CPUSET_DEL(cpuset, CPU->cpu_id);
	cpuset &= cpu_ready_set;
	MMU_TLBFLUSH_CTX_MP(ctxnum, cpuset);
	MMU_TLBFLUSH_CTX(ctxnum);
	kpreempt_enable();
}

/*
 *	This function allows the caller to guarantee that the previous
 *	xtrap done by the caller is complete. This is useful since the hardware
 *	(mondo dispatch mechanism) doesn't guarantee on return from a xtrap
 *	that the xtrap was executed. Since the hardware only can acknowledge
 *	1 mondo, return from here guarantees that the previous one was executed.
 */
static void
sfmmu_xcall_sync(cpuset_t cpuset)
{

	kpreempt_disable();
	CPUSET_DEL(cpuset, CPU->cpu_id);
	cpuset &= cpu_ready_set;
	MMU_XCALL_SYNC_MP(cpuset);
	kpreempt_enable();
}

void
sfmmu_init_tsb(caddr_t tsb_bs, u_int tsb_bytes)
{
	struct tsbe *tsbaddr;

	for (tsbaddr = (struct tsbe *)tsb_bs;
	    (uint)tsbaddr < (uint)(tsb_bs + tsb_bytes);
	    tsbaddr++) {
		tsbaddr->tte_tag.tag_invalid = 1;
	}
}

/*
 * this function adds a new segment to the array of nucleus memory
 * available to the hat.  It returns the approx. number of 8k hmeblks we could
 * create with this segment.
 */
int
sfmmu_add_nmlist(caddr_t addr, int size)
{
	int i;
	extern struct nucleus_memlist n_mlist[N_MLIST_NUM];

	ASSERT(addr && size);

	NHBLK_FLIST_LOCK();

	for (i = 0; i < N_MLIST_NUM; i++) {
		if (n_mlist[i].base == 0) {
			n_mlist[i].base = addr;
			n_mlist[i].size = size;
			NHBLK_FLIST_UNLOCK();
			return (size / HME8BLK_SZ);
		} else {
			if (((addr >= n_mlist[i].base) &&
			    (addr < n_mlist[i].base + n_mlist[i].size)) ||
			    ((addr + size >= n_mlist[i].base) &&
			    (addr + size < n_mlist[i].base +
			    n_mlist[i].size))) {
				cmn_err(CE_PANIC,
					"sfmmu_add_nmlist: bad segment "
					"bs=0x%x sz=0x%x\n",
					n_mlist[i].base, n_mlist[i].size);
				}
		}
	}
	NHBLK_FLIST_UNLOCK();
	cmn_err(CE_PANIC, "sfmmu_add_nmlist: too many segments\n");
	return (-1);
}

struct hme_blk *
sfmmu_nmlist_alloc(int bytes)
{
	int i;
	struct hme_blk *hmeblkp;
	extern struct nucleus_memlist n_mlist[N_MLIST_NUM];

	ASSERT(NHBLK_FLIST_ISHELD());

	bytes = roundup(bytes, sizeof (double));
	for (i = 0; i < N_MLIST_NUM; i++) {
		if (n_mlist[i].base && (n_mlist[i].size > bytes)) {
			hmeblkp = (struct hme_blk *)n_mlist[i].base;
			n_mlist[i].base += bytes;
			n_mlist[i].size -= bytes;
			return (hmeblkp);
		}
	}
	cmn_err(CE_PANIC, "sfmmu_nmlist_alloc: no more nhblks\n");
	return ((struct hme_blk *)-1);
}
