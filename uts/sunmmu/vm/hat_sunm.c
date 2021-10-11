/*
 * Copyright (c) 1989-1993 by Sun Microsystems, Inc.
 */

#pragma ident   "@(#)hat_sunm.c 1.71     95/07/04 SMI"

/*
 * VM - Hardware Address Translation management.
 *
 * This file implements the machine specific hardware translation
 * needed by the VM system.  The machine independent interface is
 * described in <vm/hat.h> while the machine dependent interface
 * and data structures are described in <xxxmmu/hat_xxx.h>.
 *
 * The actual loading of the hardware registers is done at the mmu
 * layer which is different for each Sun architecture type.
 *
 * The hat layer manages the address translation hardware as a cache
 * driven by calls from the higher levels in the VM system.  Nearly
 * all the details of how the hardware is managed should be invisible
 * above this layer except for miscellaneous machine specific functions
 * (e.g. mapin/mapout) that work in conjunction with this code.  Other
 * than a small number of machine specific places, the hat data
 * structures seen by the higher levels in the VM system are opaque
 * and are only operated on by the hat routines.  Each address space
 * contains a struct hat and a page contains an opaque pointer which
 * is used by the hat code to hold a list of active translations to
 * that page.
 *
 * There is one lock, sunm_mutex, that must be held when manipulating
 * any of the mmu hardware or the hat layer data structures.  The
 * mmu_* routines and map_* routines, being utility functions of the
 * hat layer, must have obey this protocol also.  This code is
 * fully preemptable.  This depends on code in resume and a field in
 * the thread structure, t_mmuctx, which when set binds a mmu context
 * and a thread.  When a thread is resumed and t_mmuctx is set, the
 * thread must use the bound context instead of the address space context
 * normally used for the thread.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/t_lock.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/mman.h>
#include <sys/debug.h>
#include <sys/vtrace.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/disp.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>

#include <sys/pte.h>
#include <sys/cpu.h>
#include <sys/mmu.h>
#include <sys/vmsystm.h>

#include <sys/openprom.h>		/* now, don't puke */
#include <sys/promif.h>

#ifdef	IOC
#include <sys/iocache.h>
#endif

#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg.h>
#include <vm/page.h>
#include <vm/vpage.h>
#include <vm/devpage.h>
#include <vm/rm.h>

#include <vm/hat_sunm.h>
#include <vm/spt.h>

#define	ASSERTPMGMAPPED(pmg, msg) /* assertpmgmapped(pmg, msg) */

/*
 * Public data structures accessed by this hat.
 */
extern struct	hatops	*sys_hatops;
extern struct	as	kas;
extern struct	hatsw	hattab[];

/*
 * HAT specific public data structures.
 */
struct ctx	*kctx;
struct pmgrp	*pmgrp_invalid;
struct smgrp	*smgrp_invalid;

/*
 * Semi-private vm_hat data structures.
 * Other machine specific routines need to access these.
 */
kmutex_t	sunm_mutex;
int		npmgrpssw;
int		npmghash;
int		npmghash_offset;
u_short		ctx_time;

u_int		nsmgrps;

struct ctx	*ctxs,		*ctxsNCTXS;
struct pmgrp	*pmgrps,	*pmgrpsNPMGRPS;
struct hwpmg	*hwpmgs,	*hwpmgsNHWPMGS;
struct pmgrp	**pmghash,	**pmghashNPMGHASH;
struct smgrp	*smgrps,	*smgrpsNSMGRPS;
struct sment	*sments,	*smentsNSMENTS;

extern u_int	vac_mask;		/* vac alignment consistency mask */
extern void	vac_flushallctx();
extern void	kvm_dup();
extern void	hme_add(), hme_sub();

/*
 * context, smeg, and pmeg statistics
 */
struct vmhatstat vmhatstat = {
	{ "vh_ctxfree",			KSTAT_DATA_ULONG },
	{ "vh_ctxstealclean",		KSTAT_DATA_ULONG },
	{ "vh_ctxstealflush",		KSTAT_DATA_ULONG },
	{ "vh_ctxmappings",		KSTAT_DATA_ULONG },

	{ "vh_pmgallocfree",		KSTAT_DATA_ULONG },
	{ "vh_pmgallocsteal",		KSTAT_DATA_ULONG },

	{ "vh_pmgmap",			KSTAT_DATA_ULONG },
	{ "vh_pmgldfree",		KSTAT_DATA_ULONG },
	{ "vh_pmgldnoctx",		KSTAT_DATA_ULONG },
	{ "vh_pmgldcleanctx",		KSTAT_DATA_ULONG },
	{ "vh_pmgldflush",		KSTAT_DATA_ULONG },
	{ "vh_pmgldnomap",		KSTAT_DATA_ULONG },

	{ "vh_faultmap",		KSTAT_DATA_ULONG },
	{ "vh_faultload",		KSTAT_DATA_ULONG },
	{ "vh_faultinhw",		KSTAT_DATA_ULONG },
	{ "vh_faultnopmg",		KSTAT_DATA_ULONG },
	{ "vh_faultctx",		KSTAT_DATA_ULONG },

	{ "vh_smgfree",			KSTAT_DATA_ULONG },
	{ "vh_smgnoctx",		KSTAT_DATA_ULONG },
	{ "vh_smgcleanctx",		KSTAT_DATA_ULONG },
	{ "vh_smgflush",		KSTAT_DATA_ULONG },

	{ "vh_pmgallochas",		KSTAT_DATA_ULONG },

	{ "vh_pmsalloc",		KSTAT_DATA_ULONG },
	{ "vh_pmsfree",			KSTAT_DATA_ULONG },
	{ "vh_pmsallocfail",		KSTAT_DATA_ULONG },
};

/*
 * kstat data
 */
kstat_named_t *vmhatstat_ptr = (kstat_named_t *)&vmhatstat;
ulong_t vmhatstat_ndata = sizeof (vmhatstat) / sizeof (kstat_named_t);

struct cache_stats cache_stats;

/*
 * sunm_pmgfind() look aside buffer hit/miss statistics.
 */
static struct pmgfindstat {
	u_int   pf_hit;
	u_int   pf_miss;
	u_int   pf_notfound;
	u_int   pf_mmuhit;
} pmgfindstat;


static void		sunm_init(void);
static void		sunm_alloc(struct hat *, struct as *);

struct as		*sunm_setup(struct as *, int);
static void		sunm_free(struct hat *, struct as *);
static void		sunm_swapin(struct hat *, struct as *);
static void		sunm_swapout(struct hat *, struct as *);
static int		sunm_dup(struct hat *, struct as *, struct as *);

static void		sunm_memload(struct hat *, struct as *, caddr_t,
				struct page *, u_int, int);
static void		sunm_devload(struct hat *, struct as *, caddr_t,
				devpage_t *, u_int, u_int, int);
static void		sunm_contig_memload(struct hat *, struct as *, caddr_t,
				struct page *, u_int, int, u_int);
static void		sunm_contig_devload(struct hat *, struct as *, caddr_t,
				devpage_t *, u_int, u_int, int, u_int);
static void		sunm_unlock(struct hat *, struct as *, caddr_t, u_int);
static 	faultcode_t	sunm_fault(struct hat *, caddr_t);
static int		sunm_probe(struct hat *, struct as *, caddr_t);
static int		sunm_share(struct as *, caddr_t, struct as *,
				caddr_t, u_int);
static void		sunm_unshare(struct as *, caddr_t, u_int);

static void		sunm_chgprot(struct as *, caddr_t, u_int, u_int);
void			sunm_unload(struct as *, caddr_t, u_int, int);
static void		sunm_sync(struct as *, caddr_t, u_int, u_int);

static void		sunm_sys_pageunload(struct page *, struct hment *);
static void		sunm_pageunload(struct page *, struct hment *);

static void		sunm_pagesync(struct hat *, struct page *,
				struct hment *, u_int);

static int		sunm_sys_pagesync(struct hat *, struct page *,
				struct hment *, u_int);

static void		sunm_pagecachectl(struct page *, u_int);
static void		sunm_sys_pagecachectl(struct page *, u_int);

static u_int		sunm_getkpfnum(caddr_t);
static u_int		sunm_getpfnum(struct as *, caddr_t);

static int		sunm_map(struct hat *, struct as *, caddr_t, u_int,
				int);
static void		sunm_unmap(struct as *, caddr_t, u_int, int);
void			sunm_vacsync(u_int);
static void		sunm_lock_init(void);

static void		sunm_smgreserve(struct as *, caddr_t);
static void		sunm_smginit(void);

struct hatops sunm_hatops = {
	sunm_init,
	sunm_alloc,
	sunm_setup,
	sunm_free,
	sunm_dup,
	sunm_swapin,
	sunm_swapout,
	sunm_memload,
	sunm_devload,
	sunm_contig_memload,
	sunm_contig_devload,
	sunm_unlock,
	sunm_fault,
	sunm_chgprot,
	sunm_unload,
	sunm_sync,
	sunm_sys_pageunload,
	sunm_sys_pagesync,
	sunm_sys_pagecachectl,
	sunm_getkpfnum,
	sunm_getpfnum,
	sunm_map,
	sunm_probe,
	sunm_lock_init,
	sunm_share,
	sunm_unshare,
	sunm_unmap	/* alias for sunm_unmap */
};

/* sun mmu locking operations */
static void	sunm_page_enter(struct page *);
static void	sunm_page_exit(struct page *);

static void	sunm_hat_enter(struct as *);
static void	sunm_hat_exit(struct as *);

static void	sunm_mlist_enter(struct page *);
static void	sunm_mlist_exit(struct page *);
static int	sunm_mlist_held(struct page *);
static void	sunm_cachectl_enter(struct page *);
static void	sunm_cachectl_exit(struct page *);

static struct hat_lockops sunm_lockops = {
	sunm_page_enter,
	sunm_page_exit,
	sunm_hat_enter,
	sunm_hat_exit,
	sunm_mlist_enter,
	sunm_mlist_exit,
	sunm_mlist_held,
	sunm_cachectl_enter,
	sunm_cachectl_exit,
};

enum ptesflag { PTESFLAG_SKIP, PTESFLAG_UNLOAD };

/*
 * Pmgrps are allocated dynamically; after stealing PMG_STEAL_LIMIT pmgs
 * without an intervening pmgfree, more pmgrps are allocated, if below a
 * preset maximum limit.  The maximum limit npmgrpssw is set at boot time
 * based upon the amount of memory in the system.
 */
#define	PMG_STEAL_LIMIT 8
int pmg_steal_limit = PMG_STEAL_LIMIT;
static	struct pmgseg	*pmgseglist = NULL;
static	struct pmgseg	*last_pmgseg;
static	struct pmgrp	*last_pmg;

static	int pmgsegsz;
static	int pmgsegsz_pgs;
static	int pmgsteals = 0;
static	int pmgrps_allocd;
static	int pmgrps_free;
static	int pmgrps_lowater;
static	int pmgrps_countdown = 0;

static	int getting_pmgs = 0;
static	int freeing_pmgs = 0;
static	int need_allocpmgs = 0;
static	int need_freepmgs = 0;

static	struct ctx *ctxhand;

static	struct pmgrp *pmgrphand;
static	struct pmgrp *pmgrpfree;
static	struct pmgrp *pmgrpmin;

static	struct hwpmg *hwpmghand;
static	struct hwpmg *hwpmgfree;
static	struct hwpmg *hwpmgmin;

static	struct smgrp *smgrphand;
static	struct smgrp *smgrpfree;
static	struct smgrp *smgrpmin;

static	void sunm_xfree(struct sunm *);
static	void sunm_pteunload(struct pmgrp *, struct hment *, caddr_t, int);
static	void sunm_ptesync(page_t *, struct pmgrp *, struct hment *,
							caddr_t, int);
static	void sunm_pmgfree(struct pmgrp *);
static	void sunm_pmglink(struct pmgrp *, struct as *, caddr_t);
static	void sunm_pmgload(struct pmgrp *);
static	void sunm_pmgunload(struct pmgrp *, enum ptesflag);
static	struct pmgrp *sunm_pmgalloc(struct as *, caddr_t, int);
static	struct pmgrp *sunm_pmgfind(caddr_t, struct as *);
static	void sunm_getctx(struct as *);
static	void sunm_pmgmap(struct pmgrp *);
static	void sunm_pmgmapinit(struct pmgrp *);
static	void sunm_clrcleanbit(void);
static	void sunm_unmap_aspmgs(struct as *);
static	void sunm_initdata(void);
static	void sunm_allocpmgchunk(void);
static	void sunm_freepmgchunk(void);

static  struct smgrp *sunm_getsmg(caddr_t);
static  void sunm_smgfree(struct smgrp *);
static  void sunm_smglink(struct smgrp *, struct as *, caddr_t);
static  void sunm_smgalloc(struct as *, caddr_t, struct pmgrp *);
static	void sunm_smgcheck_keepcntall();
static	void sunm_smgcheck_keepcnt(struct smgrp *);
static	void sunm_hwpmgmv(struct hwpmg *, struct hwpmg *);
static	void sunm_panic(char *);

#define	sunm_pmgtosmg(pmg) \
	(&smgrps[((pmg)->pmg_sme - sments) >> NSMENTPERSMGRPSHIFT])

static	int hatunmaplimit = 30;		/* % limit used sunm_unmap_aspmgs() */

static	struct pmgrp	*hmetopmg(struct hment *);
static	struct pmgseg	*pmgtopmgseg(struct pmgrp *);
static	int 		hash_asaddr(struct as *, caddr_t);

extern	int		sunm_kill_procs(page_t *, caddr_t);

/* local inline functions */
#define	sunm_pmgbase(a) ((caddr_t)((u_int)a & PMGRPMASK))
#define	sunm_pmgisloaded(pmg) (pmg->pmg_num != PMGNUM_SW)
#define	sunm_addrtopte(pmg, a) \
	((pmg)->pmg_pte + (((u_int)(a) & PMGRPOFFSET)  >> PAGESHIFT))
#define	sunm_hmetopte(pmg, hme) (&(pmg)->pmg_pte[hme->hme_impl])

extern char DVMA[];		/* addresses in kas above DVMA are for "IO" */

#define	VAC_ALIGNED(a1, a2) ((((u_int)(a1) ^ (u_int)(a2)) & vac_mask) == 0)

/*
 *
 * The next set of routines implements the machine
 * independent hat interface described in <vm/hat.h>
 *
 */


/*
 * initialize locking for the hat layer, called early during boot.
 */
static void
sunm_lock_init(void)
{
	hat_lockops = &sunm_lockops;
	mutex_init(&sunm_mutex, "sunm_mutex", MUTEX_DEFAULT, NULL);
}

/*
 * Initialize the hardware address translation structures.
 * Called by startup.
 *
 * Initialize the SW page tables in the range [0..npmgrps] as loaded
 * and mapped. This is required for sunm_pmgreserve to work. After startup
 * reserves kernel pmgs, it calls sunm_pmginit. sunm_pmginit will create
 * a list of free SW page tables and a list of free HW pmgs by skipping
 * reserved pmgs.
 */
void
sunm_init(void)
{
	register struct ctx	*ctx;
	register struct pmgrp	*pmg;
	register int i;

	sunm_initdata();

	i = 0;
	for (ctx = ctxs; ctx < ctxsNCTXS; ctx++)
		ctx->c_num = i++;
	ctxhand = ctxs;

	if (mmu_3level) {
		register struct smgrp *smg;
		register struct sment *sme;

		i = 0;
		sme = sments;
		for (smg = smgrps; smg < smgrpsNSMGRPS; smg++) {
			smg->smg_num = i++;
			smg->smg_sme = sme;
			sme += NSMENTPERSMGRP;
		}
		smgrps[SMGRP_INVALID].smg_keepcnt++;
		smgrp_invalid = &smgrps[SMGRP_INVALID];
		smgrphand = smgrps;
	}

	/*
	 * Note that &pmgrps[npmgrps] cannot be changed to pmgrpsNPMGRPS.
	 * npmgrps is the number the hardware provides and the array
	 * includes both hardware and software with the hardware occupying
	 * the lower region.  [sunm_pmgreserve() assumes this.]
	 */
	i = 0;
	for (pmg = pmgrps; pmg < &pmgrps[npmgrps]; i++, pmg++) {
		pmg->pmg_num = i;
		pmg->pmg_mapped = 1;
		hwpmgs[i].hwp_pmgrp = pmg;
		sunm_pmgmapinit(pmg);
	}

	/*
	 * The remaining SW pmgrp are not loaded in HW.
	 */
	for (; pmg < pmgrpsNPMGRPS; pmg++) {
		pmg->pmg_num = PMGNUM_SW;
		pmg->pmg_mapped = 0;
		sunm_pmgmapinit(pmg);
	}
	pmgrps[PMGRP_INVALID].pmg_keepcnt++;
	pmgrp_invalid = &pmgrps[PMGRP_INVALID];

	/*
	 * There can be only one system mmu and this hat contols it.
	 * Initialize the system's global hat ops to be this hat's ops.
	 */
	sys_hatops = &sunm_hatops;

	/*
	 * Grab a context for the kernel and lock it forever.
	 */
	kctx = &ctxs[KCONTEXT];
	kctx->c_lock = 1;
	kctx->c_as = &kas;
}

/*ARGSUSED1*/
static void
sunm_alloc(struct hat *hat, struct as *as)
{
	hat->hat_op = &sunm_hatops;
}

/*
 * Free all the hat resources held by an address space.
 * Called from as_free when an address space is being
 * destroyed and when it is to be "swapped out".
 *
 * XXX - should we do anything about locked translations here?
 */
/*ARGSUSED1*/
static void
sunm_free(register struct hat *hat, register struct as *as)
{
	register struct ctx *ctx;
	register struct sunm *sunm;

	mutex_enter(&sunm_mutex);
	sunm = (struct sunm *)&hat->hat_data;
	if ((ctx = sunm->sunm_ctx) != NULL) {

		if (ctx->c_lock)
			sunm_panic("sunm_free - ctx is locked");

		/*
		 * Clean context now. This will prevent expensive segment
		 * and page flushing when freeing individual pmgs.
		 */
		curthread->t_mmuctx = ctx->c_num;
		mmu_setctx(ctx);
		if (!ctx->c_clean) {
			vac_ctxflush();
			ctx->c_clean = 1;
		}
		sunm_xfree(sunm);

		sunm->sunm_ctx = NULL;
		ctx->c_as = NULL;

	} else {
		sunm_xfree(sunm);
	}

	/*
	 * Switch to kernel context.
	 */
	curthread->t_mmuctx = 0;
	mmu_setctx(kctx);
	mutex_exit(&sunm_mutex);
}

/*ARGSUSED*/
static int
sunm_dup(struct hat *hat, struct as *as, struct as *newas)
{
	return (0);
}

/*
 * Set up any translation structures, for the specified address space,
 * that are needed or preferred when the process is being swapped in.
 */
/*ARGSUSED*/
static void
sunm_swapin(struct hat *hat, struct as *as)
{}

/*
 * Free all of the translation resources, for the specified address space,
 * that can be freed while the process is swapped out. Called from as_swapout.
 */
/*ARGSUSED1*/
static void
sunm_swapout(struct hat *hat, struct as *as)
{
	register struct ctx *ctx, *savctx;
	register struct sunm *sunm;
	register struct pmgrp *pmg, *nextpmg = NULL;
	u_int		ommuctx;

	mutex_enter(&sunm_mutex);

	sunm = (struct sunm *)&hat->hat_data;
	if ((ctx = sunm->sunm_ctx) != NULL) {
		if (ctx->c_lock)
			sunm_panic("swapout of locked ctx");

		/*
		 * Clean context now. This will prevent expensive segment
		 * and page flushing when freeing individual pmgs.
		 */
		if (!ctx->c_clean) {
			ommuctx = curthread->t_mmuctx;
			savctx = mmu_getctx();

			curthread->t_mmuctx = ctx->c_num;
			mmu_setctx(ctx);
			vac_ctxflush();
			ctx->c_clean = 1;

			curthread->t_mmuctx = ommuctx;
			mmu_setctx(savctx);
		}
	}

	/*
	 * Free unlocked pmgrps and smgrps.
	 */
	for (pmg = sunm->sunm_pmgrps; pmg != NULL; pmg = nextpmg) {
		nextpmg = pmg->pmg_next;
		if (pmg->pmg_keepcnt == 0) {
			sunm_pmgfree(pmg);
			pmgsteals = 0;
		}
	}

	if (mmu_3level) {
		register struct smgrp *smg, *nextsmg = NULL;

		for (smg = sunm->sunm_smgrps; smg != NULL; smg = nextsmg) {
			nextsmg = smg->smg_next;
			if (smg->smg_keepcnt == 0)
				sunm_smgfree(smg);
		}
	}

	/*
	 * If we freed all of the pmgrps, ie, nothing was locked,
	 * clean up and free the ctx.  We do this because in rare
	 * cases /proc will temporarily lock a mapping in an address
	 * space while it is doing i/o to it.
	 */
	if (ctx != NULL &&
	    sunm->sunm_pmgrps == NULL &&
	    sunm->sunm_smgrps == NULL) {
		sunm->sunm_ctx = NULL;
		ctx->c_as = NULL;
	}
	mutex_exit(&sunm_mutex);
}

/*
 * Set up addr to map to page pp with protection prot.
 */
/*ARGSUSED*/
void
sunm_memload(
	struct hat	*hat,
	struct as	*as,
	caddr_t		addr,
	struct page	*pp,
	u_int		prot,
	int		flags)
{
	struct pte pte;

	ASSERT(curthread->t_mmuctx == 0);

	/*
	 * Pmgrps are dynamically allocated and freed at this point
	 * because it is safe to drop the hat layer mutex and make a
	 * call to kmem_alloc or kmem_free since we have yet to use
	 * any hat layer state.  Kmem_alloc and kmem_free may re-enter
	 * that hat layer to map or unmap the memory that is being
	 * allocated for freed.
	 */
	if (need_allocpmgs)
		sunm_allocpmgchunk();
	else if (need_freepmgs)
		sunm_freepmgchunk();

	sunm_mempte(pp, prot, &pte);
	sunm_pteload(as, addr, pp, pte, flags);
}

/*
 * Cons up a struct pte using the device's pf bits and protection
 * prot to load into the hardware for address addr.
 */
/*ARGSUSED*/
void
sunm_devload(
	struct hat	*hat,
	struct as	*as,
	caddr_t		addr,
	devpage_t	*dp,
	u_int		pf,
	u_int		prot,
	int		flags)
{
	union {
		struct pte u_pte;
		int u_pf;
	} apte;

	apte.u_pf = pf & PG_PFNUM;
	apte.u_pte.pg_v = 1;
	apte.u_pte.pg_prot = sunm_vtop_prot(prot);

	sunm_pteload(as, addr, (struct page *)dp, apte.u_pte, flags);
}

/*
 * Set up translations to a range of virtual addresses backed by
 * physically contiguous memory. Sun MMU implementation will just
 * load len/MMU_PAGESIZE 4k bytes size translations.
 */
static void
sunm_contig_memload(
	struct hat	*hat,
	struct as	*as,
	caddr_t		addr,
	struct page	*pp,
	u_int		prot,
	int		flags,
	u_int		len)
{
	register caddr_t a;
	register struct page *tmp_pp = pp;

	/* ASSERT(MUTEX_HELD(&hat_mutex)); */

	/*
	 * Setting up translations to memory backed by page structs.
	 * It is the caller's responsibility to ensure that the page is
	 * locked down and its  identity does not change.
	 * XXX - Later on we must check for virtual and physical
	 * address alignment and load a level1, level2 or a level3
	 * PTE directly.
	 */
	for (a = addr; a < addr + len; a += MMU_PAGESIZE, tmp_pp++) {
		/*
		 * We assume here that the page structs are contiguous
		 * and that they map physically contiguous memory
		 */
		sunm_memload(hat, as, a, tmp_pp, prot, flags);
	}
}

/*
 * Set up translations to a range of virtual addresses backed by
 * physically contiguous device memory.  'pf' is the base physical page
 * framenumber of the physically contiguous memory and 'dp' is non-null for
 * device memory backed by devpages.
 * The Sun MMU implementation will just call sunm_devload() to load up the
 * len/MMU_PAGESIZE 4k sized translations.
 */
static void
sunm_contig_devload(
	struct hat	*hat,
	struct as	*as,
	caddr_t		addr,
	devpage_t	*dp,
	u_int		pf,
	u_int		prot,
	int		flags,
	u_int		len)
{
	register caddr_t a;
	register struct devpage *tmp_dp = NULL;
	struct vnode *vp = NULL;
	off_t off;

	/* ASSERT(MUTEX_HELD(&hat_mutex)); */

	/*
	 * Main memory not backed by page structs or device memory
	 */
	if (dp) {
		/*
		 * We are setting up translations to
		 * device memory backed by devpage structs.
		 */
		if ((vp = dp->p_vnode) == NULL)
			sunm_panic("sunm_contig_devload: null vp\n");

		off = dp->p_offset;

		/*
		 * Since the devpage structs are not allocated in a
		 * contiguous chunk but instead kmem_alloced one at a
		 * time during VOP_ADDMAP we derive the appropiate
		 * dp based on a <vnode, offset> devpage look up. Even
		 * when loading up a level1 or level2 PTE we need the
		 * dp's to keep track of the hments. More on level1
		 * and level2 PTEs later...
		 */
		for (a = addr; a < addr + len; a += MMU_PAGESIZE,
		    off += MMU_PAGESIZE, pf++) {
			tmp_dp = (struct devpage *)
				devpage_lookup(vp, off, SE_SHARED);
			if (tmp_dp == NULL) {
			    mutex_exit(&sunm_mutex);
			    cmn_err(CE_PANIC,
				"sunm_contig_devload: null devpage\n"
				"vnode = 0x%x off = 0x%x", vp, off);
			}
		}
		sunm_devload(hat, as, a, tmp_dp, pf, prot, flags);
		page_unlock((struct page *)tmp_dp);
	} else {
		for (a = addr; a < addr + len; a += MMU_PAGESIZE, pf++) {
			/*
			 * For device memory not backed by devpage structs or
			 * the case of main memory not backed by page structs.
			 */
			sunm_devload(hat, as, a,
				(struct devpage *)NULL, pf, prot, flags);
		}
	}
}

/*
 * Release hardware address translation locks on the range of address.
 * For the Sun MMU, this means decrementing the counter on the pmgrp.
 */
/*ARGSUSED*/
void
sunm_unlock(struct hat *hat, struct as *as, caddr_t addr, u_int len)
{
	register struct pmgrp	*pmg;
	struct as		*oas;
	u_int			ommuctx;
	register caddr_t a;

	mutex_enter(&sunm_mutex);
	ommuctx = curthread->t_mmuctx;
	oas = sunm_setup(as, HAT_ALLOC);

	for (a = addr; a < addr + len; a += MMU_PAGESIZE) {
		pmg = sunm_pmgfind(a, as);
		if (pmg == NULL || pmg->pmg_keepcnt < 1) {
			mutex_exit(&sunm_mutex);
			printf("sunm_unlock pmg %x keep %x\n",
				pmg, (pmg) ? pmg->pmg_keepcnt : NULL);
			cmn_err(CE_PANIC, "sunm_unlock");
		}

#ifdef	VAC
		if (vac && pmg->pmg_keepcnt == 1) {
		    register struct hment *hme = pmg->pmg_hme;
		    register int cnt;
		    struct page *pp;

			/*
			 * Now check to see if we now can cache any non-cached
			 * pages.  For now, we use the simple minded algorithm
			 * and just unload any locked of the locked translations
			 * if the corresponding page is currently marked as non-
			 * cacheable. This situation doesn't happen all the
			 * much, so the efficiency doesn't have to be all
			 * that great.
			 */
		    for (cnt = 0; cnt < NPMENTPERPMGRP; cnt++, hme++) {
			if (hme->hme_valid &&
			    (pp = hme->hme_page) != NULL && PP_ISTNC(pp)) {
				sunm_pteunload(pmg, hme, (caddr_t)NULL,
				    HAT_RMSYNC);
				if (PP_ISTNC(pp)) {
					/*
					 * We lost - unloading the mmu
					 * translation wasn't enough to
					 * make the page cacheable again.
					 */
					cache_stats.cs_unloadnofix++;
				} else {
					/*
					 * We won - unloading the mmu
					 * translation made the page
					 * cacheable again.
					 */
					cache_stats.cs_unloadfix++;
				}
			}
		    }
		}
#endif	VAC
		pmg->pmg_keepcnt--;
		if (mmu_3level)
			sunm_pmgtosmg(pmg)->smg_keepcnt--;

		curthread->t_mmuctx = ommuctx;
	}

	if (oas != NULL)
		(void) sunm_setup(oas, HAT_DONTALLOC);
	mutex_exit(&sunm_mutex);
}

/*
 * Resolve a page fault by loading a cached translation.
 *
 * sunm_fault() is called from the fault handler in locore.s via hat_fault
 * We rely on hat_fault never calling sunm_fault with addr >= KERNELBASE.
 *
 * Note that sunm_fault returning 0 means that sunm_fault "helped to
 * improve the translations" rather than that it resolved the fault. This means
 * that a process may fault again. When sunm_fault is called during
 * the second fault, it will return FC_NOMAP and the higher VM layers will
 * be invoked. sunm_fault doesn't have enough information to fully test
 * if the second fault will happen. We could improve by reading the
 * pte from the MMU to see if the pte is valid, but this would add
 * overhead to the case when the pte is valid. The situations leading to
 * the second fault is unlikely and therefore we don't need to optimize.
 */
/*ARGSUSED*/
static faultcode_t
sunm_fault(struct hat *hat, caddr_t addr)
{
	struct pmgrp	*pmg;
	struct as	*as;

	if (addr >= (caddr_t)KERNELBASE) {
		return (FC_NOMAP);
	}

	/*
	 * Test if address falls into MMU hole
	 */
	if (!good_addr(addr)) {
		vmhatstat.vh_faultnopmg.value.ul++;
		return (FC_NOMAP);
	}

	/*
	 * The typical case is that we have the right context, the right
	 * pmgrp, but faulted on pte. The fault must be resolved by
	 * the higher VM layers.
	 */
	if ((pmg = mmu_getpmg(addr)) != pmgrp_invalid && pmg->pmg_mapped) {
		vmhatstat.vh_faultinhw.value.ul++;
		return (FC_NOMAP);
	}

	mutex_enter(&sunm_mutex);

	if ((as = curproc->p_as) != mmu_getctx()->c_as) {
		struct pte	pte;

		(void) sunm_setup(as, HAT_ALLOC);
		mmu_getpte(addr, &pte);
		curthread->t_mmuctx = 0;

		if (pte.pg_v) {
			/*
			 * Setting up the context resolved the fault.
			 */
			vmhatstat.vh_faultctx.value.ul++;
			mutex_exit(&sunm_mutex);
			return ((faultcode_t)0);
		}
	}

	if ((pmg = sunm_pmgfind(addr, as)) != NULL) {

		if (pmg->pmg_mapped) {
			/*
			 * Pmg is mapped and loaded. The hat layer cannot
			 * resolve the fault.
			 */
			ASSERT(pmg->pmg_num != PMGNUM_SW);
			vmhatstat.vh_faultinhw.value.ul++;
			mutex_exit(&sunm_mutex);
			return (FC_NOMAP);
		} else if (sunm_pmgisloaded(pmg)) {

			/*
			 * pmg is loaded but not mapped.
			 */
			/*
			 * The code below is inline sunm_pmgmap.
			 */
			if (mmu_3level)
				sunm_smgalloc(pmg->pmg_as, pmg->pmg_base, pmg);
			mmu_setpmg(pmg->pmg_base, pmg);
			pmg->pmg_mapped = 1;
			/* End of inline sunm_pmgmap. */

			vmhatstat.vh_faultmap.value.ul++;
			mutex_exit(&sunm_mutex);
			return ((faultcode_t)0);
		} else {
			/*
			 * Pmg is not loaded.
			 */
			sunm_pmgload(pmg);
			vmhatstat.vh_faultload.value.ul++;
			mutex_exit(&sunm_mutex);
			return ((faultcode_t)0);
		}
	}

	/*
	 * HAT couldn't resolve the fault because there is no SW pmg.
	 */
	vmhatstat.vh_faultnopmg.value.ul++;
	mutex_exit(&sunm_mutex);
	return (FC_NOMAP);
}

/*
 * Change the protections in the virtual address range
 * given to the specified virtual protection.  If
 * vprot == ~PROT_WRITE, then all the write permission
 * is taken away for the current translations, else if
 * vprot == ~PROT_USER, then all the user permissions
 * are taken away for the current translations, otherwise
 * vprot gives the new virtual protections to load up.
 */
static void
sunm_chgprot(
	struct as	*as,
	caddr_t		addr,
	u_int		len,
	u_int		vprot)		/* virtual page protections */
{
	register caddr_t	a, ea;
	register struct pmgrp	*pmg = NULL;
	register u_int		pprot;	/* physical page protections */
	register int		newprot;
	struct as		*oas;
	struct pte		pte;
	struct pte		*ppte;

	ASSERT(curthread->t_mmuctx == 0);

	if ((vprot != (u_int)~PROT_WRITE) && (vprot != (u_int)~PROT_USER))
		pprot = sunm_vtop_prot(vprot);

	mutex_enter(&sunm_mutex);

	/*
	 * We must get a context for the AS because we will be
	 * synchronizing SW PTE by reads from MMU.
	 */
	oas = sunm_setup(as, HAT_ALLOC);

	for (a = addr, ea = addr + len; a < ea; a += MMU_PAGESIZE) {
		if (pmg == NULL ||
		    ((u_int)a & (PMGRPSIZE - 1)) < MMU_PAGESIZE) {
			pmg = sunm_pmgfind(a, as);
			if (pmg == NULL) {
				/*
				 * Bump up `a' to avoid checking all
				 * the ptes in the invalid pmgrp.
				 */
				a = (caddr_t)((u_int)a & ~(PMGRPSIZE - 1)) +
				    PMGRPSIZE - MMU_PAGESIZE;
				continue;
			}

			/*
			 * Make sure that loaded pmg is also mapped.
			 */
			if (sunm_pmgisloaded(pmg))
				sunm_pmgmap(pmg);
		}

		ppte = sunm_addrtopte(pmg, a);

		if (!pte_valid(ppte))
			continue;

		/*
		 * Synchronize PTE from MMU.
		 */
		if (sunm_pmgisloaded(pmg)) {
			mmu_getpte(a, ppte);
		}

		pte = *ppte;

		if (vprot == (u_int)~PROT_WRITE) {
			switch (pte.pg_prot) {
			case KW: pprot = KR; newprot = 1; break;
			case UW: pprot = UR; newprot = 1; break;
			default: newprot = 0; break;
			}
		} else if (vprot == (u_int)~PROT_USER) {
			switch (pte.pg_prot) {
			case UW: pprot = KW; newprot = 1; break;
			case UR: pprot = KR; newprot = 1; break;
			default: newprot = 0; break;
			}
		} else if (pte.pg_prot != pprot) {
			newprot = 1;
		} else {
			newprot = 0;
		}
		if (newprot) {
			pte.pg_prot = pprot;

			*ppte = pte;		/* Set SW PTE */

			/*
			 * Synchronize HW PTE if this pmg is loaded.
			 * We assume that sunm_setup has been done.
			 */
			if (sunm_pmgisloaded(pmg)) {
#ifdef	VAC
				if (vac && !pte.pg_nc && pte.pg_r)
					vac_pageflush(a);
#endif	VAC
				mmu_setpte(a, pte);
			}
		}
	}

	curthread->t_mmuctx = 0;
	if (oas != NULL)
		(void) sunm_setup(oas, HAT_DONTALLOC);
	mutex_exit(&sunm_mutex);
}

/*
 * Unload all the mappings in the range [addr..addr+len).
 * This is essentially an optimization for unloading chunks of
 * mappings that would otherwise get done sunm_pageunload.
 */
void
sunm_unload(struct as *as, caddr_t addr, u_int len, int flags)
{
	register caddr_t	a;
	register struct hment	*hme;
	register struct pmgrp	*pmg = NULL;
	register struct sunm	*sunm;
	struct as		*oas;
	caddr_t			ea;
	u_int			ommuctx;

	ASSERT(flags == HAT_UNLOAD);

	mutex_enter(&sunm_mutex);
	sunm = (struct sunm *)&as->a_hat->hat_data;
	if (sunm->sunm_pmgrps == NULL) {
		/*
		 * If there are no allocated pmgrps for this
		 * address space, we don't have to do anything.
		 */
		mutex_exit(&sunm_mutex);
		return;
	}

	ommuctx = curthread->t_mmuctx;
	oas = sunm_setup(as, HAT_ALLOC);

	for (a = addr, ea = addr + len; a < ea; a += MMU_PAGESIZE) {
		if (pmg == NULL ||
		    ((u_int)a & (PMGRPSIZE - 1)) < MMU_PAGESIZE) {
			pmg = sunm_pmgfind(a, as);
			if (pmg == NULL) {
				/*
				 * Bump up `a' to avoid checking all
				 * the hmes in the invalid pmgrp.
				 */
				a = (caddr_t)((u_int)a & ~(PMGRPSIZE - 1)) +
				    PMGRPSIZE - MMU_PAGESIZE;
				continue;
			}
			hme = &pmg->pmg_hme[mmu_btop(a - pmg->pmg_base)];
		} else {
			hme++;
		}
		/*
		 * Throw out the mapping.
		 */
		if (hme->hme_nosync) {
			/*
			 * decrement keepcnt on pmg and smg to indicate
			 * 'unlock' on the address.
			 */
			if (mmu_3level)
				sunm_pmgtosmg(pmg)->smg_keepcnt--;
			pmg->pmg_keepcnt--;
			sunm_pteunload(pmg, hme, a, HAT_NOSYNC | HAT_VADDR);
		} else
			sunm_pteunload(pmg, hme, a, HAT_RMSYNC | HAT_VADDR);
	}

	curthread->t_mmuctx = ommuctx;
	if (oas != NULL)
		(void) sunm_setup(oas, HAT_DONTALLOC);
	mutex_exit(&sunm_mutex);
}

/*
 * Synchronize all the mappings in the range [addr..addr+len].
 * This is an optimization for doing ref and mod bit synchronization
 * on large chunks of mappings that would otherwise get done by repeated
 * calls sunm_pagesync.  There is a difference, for this operation,
 * only mappings in the argument address space have to be "synced".
 */
static void
sunm_sync(struct as *as, caddr_t addr, u_int len, u_int flag)
{
	register caddr_t	a;
	register struct hment	*hme;
	register struct pmgrp	*pmg = NULL;
	register struct sunm	*sunm;
	struct as		*oas;
	caddr_t			ea;
	u_int			ommuctx;


	mutex_enter(&sunm_mutex);
	sunm = (struct sunm *)&as->a_hat->hat_data;
	if (sunm->sunm_pmgrps == NULL) {
		/*
		 * If there are no allocated pmgrps for this
		 * address space, we don't have to do anything.
		 */
		mutex_exit(&sunm_mutex);
		return;
	}

	ommuctx = curthread->t_mmuctx;
	oas = sunm_setup(as, HAT_ALLOC);

	/*
	 * If this is a large range, flush the context
	 * which avoids flushing each page that is mapped by it.
	 */
	if (len >= PMGRPSIZE) {
		struct ctx *ctx = (struct ctx *)as->a_hat->hat_data[0];
		if (!ctx->c_clean) {
			vac_ctxflush();
			ctx->c_clean = 1;
		}
	}

	for (a = addr, ea = addr + len; a < ea; a += MMU_PAGESIZE) {
		if (pmg == NULL ||
		    ((u_int)a & (PMGRPSIZE - 1)) < MMU_PAGESIZE) {
			pmg = sunm_pmgfind(a, as);
			if (pmg == NULL) {
				/*
				 * Bump up `a' to avoid checking all
				 * the hmes in the invalid pmgrp.
				 */
				a = (caddr_t)((u_int)a & ~(PMGRPSIZE - 1)) +
				    PMGRPSIZE - MMU_PAGESIZE;
				continue;
			}
			hme = &pmg->pmg_hme[mmu_btop(a - pmg->pmg_base)];
		} else {
			hme++;
		}
		if ((hme->hme_valid) && (hme->hme_page)) {
			sunm_ptesync(hme->hme_page, pmg, hme, a,
				HAT_VADDR | (flag ? HAT_RMSYNC : HAT_RMSTAT));
		}
	}

	curthread->t_mmuctx = ommuctx;
	if (oas != NULL)
		(void) sunm_setup(oas, HAT_DONTALLOC);
	mutex_exit(&sunm_mutex);
}


static void
sunm_sys_pageunload(register struct page *pp, register struct hment *hme)
{
	register struct hat *hat;

	ASSERT(se_assert(&pp->p_selock));

	hat_mlist_enter(pp);
	while ((hme = pp->p_mapping) != NULL) {
		hat = &hats[hme->hme_hat];
		if (hat->hat_op == &sunm_hatops)
			sunm_pageunload(pp, hme);
		else
			HATOP_PAGEUNLOAD(hat, pp, hme);
	}
	ASSERT(pp->p_mapping == NULL);
	hat_mlist_exit(pp);
}

/*
 * Unload a the hardware translation (hme) that maps page `pp'.
 */
static void
sunm_pageunload(register struct page *pp, register struct hment *hme)
{
	register struct pmgrp *pmg;

	/*
	 * the lock of the mapping list acquired the sunm_mutex in hat.c.
	 */
	ASSERT(MUTEX_HELD(&sunm_mutex));
	ASSERT(se_assert(&pp->p_selock) || panicstr);

	pmg = hmetopmg(hme);
	sunm_pteunload(pmg, hme, (caddr_t)NULL, HAT_RMSYNC);
}

/*
 * Get all the hardware dependent attributes for a page struct
 */
/*
 * Get all the hardware dependent attributes for a page struct
 */
/*ARGSUSED*/
static int
sunm_sys_pagesync(
	struct hat *hat,
	struct page *pp,
	struct hment *hme,
	u_int clearflag)
{
	hat_mlist_enter(pp);
	for (hme = pp->p_mapping; hme; hme = hme->hme_next) {
		hat = &hats[hme->hme_hat];
		if (hat->hat_op == &sunm_hatops)
			sunm_pagesync(hat, pp, hme,
			    clearflag & ~HAT_STOPON_RM);
		else
			HATOP_PAGESYNC(hat, pp, hme,
			    clearflag & ~HAT_STOPON_RM);
		/*
		 * If clearflag is HAT_DONTZERO, break out as soon
		 * as the "ref" or "mod" is set.
		 */
		if ((clearflag & ~HAT_STOPON_RM) == HAT_DONTZERO &&
		    ((clearflag & HAT_STOPON_MOD) && PP_ISMOD(pp)) ||
		    ((clearflag & HAT_STOPON_REF) && PP_ISREF(pp)))
			break;
	}
	hat_mlist_exit(pp);

	return (HAT_DONE);
}

/*ARGSUSED*/
static void
sunm_pagesync(
	struct hat *hat,
	struct page *pp,
	struct hment *hme,
	u_int clearflag)
{
	register struct pmgrp *pmg;

	/*
	 * the lock of the mapping list acquired the sunm_mutex in hat.c.
	 */
	ASSERT(MUTEX_HELD(&sunm_mutex));
	pmg = hmetopmg(hme);
	sunm_ptesync(pp, pmg, hme, (caddr_t)NULL,
		clearflag ? HAT_RMSYNC : HAT_RMSTAT);
}

/*
 * Make a mapping non-cached.
 * hat should be locked, only called when cache conflicts are found.
 */
static void
sunm_sys_pagecachectl(page_t *pp, u_int flag)
{
	struct hatsw *hsw;

	for (hsw = hattab; hsw->hsw_name && hsw->hsw_ops; hsw++) {
		if (hsw->hsw_ops == &sunm_hatops) {
			sunm_pagecachectl(pp, flag);
		} else {
			HATOP_PAGECACHECTL(hsw, pp, flag);
		}
	}
}

/*ARGSUSED1*/
static void
sunm_pagecachectl(page_t *pp, u_int flag)
{
	register struct hment *hme;
	struct hat *hat;
	struct pmgrp *pmg;

	ASSERT(se_assert(&pp->p_selock) || panicstr);
	ASSERT(MUTEX_HELD(&sunm_mutex));

	for (hme = pp->p_mapping; hme; hme = hme->hme_next) {
		pmg = hmetopmg(hme);
		hat = &hats[hme->hme_hat];
		if (hat->hat_op == &sunm_hatops)
			sunm_ptesync(pp, pmg, hme, (caddr_t)NULL, HAT_NCSYNC);
	}
}


/*
 * Returns the page frame number for a given kernel virtual address.
 * Since zero can be a valid page frame number, we return -1, 0xffffffff,
 * if the mapping is invalid
 *
 * XXX	This 'bad' return value must be numerically equal to 'NOPAGE', but
 *	we don't want to pollute hat_sunm.c by #including <sys/ddi.h> .. )
 */
static u_int
sunm_getkpfnum(caddr_t addr)
{
	struct pte pte;

	if ((u_int)addr <= KERNELBASE)
		return ((u_int)-1);

	mmu_getkpte(addr, &pte);
	if (pte.pg_v)
		return (MAKE_PFNUM(&pte));
	else
		return ((u_int)-1);
#ifdef notdef
	/* desired */
	if (pte.pg_v)
		return (pte.pg_pfnum);
	else
		return ((u_int)-1);
#endif notdef
}

/*
 * Returns the page frame number for a given kernel virtual address.
 * Since zero can be a valid page frame number, we return -1, 0xffffffff,
 * if the mapping is invalid.
 */
static u_int
sunm_getpfnum(struct as *as, caddr_t addr)
{
	struct pte pte;
	struct as *oas;

	mutex_enter(&sunm_mutex);
	ASSERT(curthread->t_mmuctx == 0);
	oas = sunm_setup(as, HAT_ALLOC);
	mmu_getpte(addr, &pte);
	curthread->t_mmuctx = 0;
	if (oas != NULL)
		(void) sunm_setup(oas, HAT_DONTALLOC);
	mutex_exit(&sunm_mutex);

	if (pte.pg_v)
		return (MAKE_PFNUM(&pte));
	else
		return ((u_int)-1);
}


/*
 * End of MMU independent interface routines.
 *
 * The next few routines implement some machine dependent functions
 * need for the Sun MMU.  Note that each hat implementation can define
 * whatever additional interfaces that make sense for that machine.
 *
 * Start MMU specific interface routines.
 */

struct as *
sunm_setup(struct as *as, int allocflag)
{
	register struct ctx *ctx;
	register struct as *oas;
	extern u_short ctx_time;

	ASSERT(MUTEX_HELD(&sunm_mutex));
	ASSERT(as->a_hat != NULL);
	ASSERT(as->a_hat->hat_op != NULL);
	ASSERT(as->a_hat->hat_op == sys_hatops);

	ctx = (struct ctx *)as->a_hat->hat_data[0];
	if (allocflag) {
		oas = mmu_getctx()->c_as;
		if (oas != as) {
			if (ctx != kctx) {
				if (ctx == NULL) {
					sunm_getctx(as);
				} else {
					ctx->c_time = ctx_time++;
					ctx->c_clean = 0;
					curthread->t_mmuctx = ctx->c_num;
					mmu_setctx(ctx);
				}
			}
		} else  {
			curthread->t_mmuctx = 0x100|ctx->c_num;
			oas = NULL;
		}
		return (oas);
	}
	if (ctx != NULL) {
		ctx->c_time = ctx_time++;
		ctx->c_clean = 0;
		mmu_setctx(ctx);
	} else {
		mmu_setctx(kctx);
	}
	return (NULL);
}

/*
 * Old proms seem to lie about the memory they use, or at least
 * they are inconsistent in what they tell the kernel during boot
 * up, so we conservatively work around them by removing any pages that
 * have mappings in the prom's part of the kernel address space.
 *
 * It's not that easy. That's what we do:
 * (1) If the address is not in the Prom range then we return 0 and the
 *     caller should panic.
 * (2) If the page is already used we invalidate the Prom mapping
 *     and return 1. The caller has to continue without setting [wiping
 *     out] the current hme on the page.
 * (3) If the page is on the free list we take the page out of traffic,
 *     assign it the Prom vnode and return 2. The caller has to setup the
 *     hme on the page. This will make some code happy that checks for
 *     hmes on the p_mapping field.
 */
static int
chk_prompg(struct page *pp, u_int addr)
{
	extern struct vnode prom_pages_vp;

	if (addr >= (u_int)SUNMON_START && addr < (u_int)SUNMON_END) {
		/*
		 * Lock the page and leave it locked forever.
		 * Even if the prom gives us this page twice
		 * we will only do this logic once.
		 */
		if (page_trylock(pp, SE_EXCL)) {
			if (pp->p_free) {
				page_reclaim(pp, NULL);
				mutex_exit(&sunm_mutex);
				pp->p_free = 0;
				cmn_err(CE_CONT,
					"?stealing page %x pfnum %x for prom\n",
					pp, pp->p_pagenum);
				(void) page_hashin(pp, &prom_pages_vp, addr,
				    NULL);
				page_downgrade(pp);
				mutex_enter(&sunm_mutex);
				return (2);
			} else {
				/*
				 * This is bad, blow away proms mapping.
				 */
				mmu_setpte((caddr_t)addr, mmu_pteinvalid);
				page_unlock(pp);
				return (1);
			}
		} else {
			mmu_setpte((caddr_t)addr, mmu_pteinvalid);
			return (1);
		}
	}
	return (0);
}

/*
 * This routine is called for kernel initialization
 * to cause a pmgrp to be reserved w/o any unloading, it
 * links the pmgrp into the address space (if not already there),
 * and returns with the pmgrp in question leaving its keepcnt incremented.
 * It must load the software pmg state and setup any valid mappings
 * on the p_mapping list, sunm_vacsync of kmem_alloc'd pages depends
 * on this.
 */
void
sunm_pmgreserve(register struct as *as, caddr_t addr, int len)
{
	register struct pmgrp	*pmg;
	u_int			pmgnum;
	struct hment 		*hme;
	struct pte		*pte;
	int			offset, pmg_off, pmg_cnt;
	struct pte		tmp_ptes[NPMENTPERPMGRP];

	mutex_enter(&sunm_mutex);

	pmgnum = map_getsgmap(addr);
	if (pmgnum == PMGRP_INVALID)
		sunm_panic("sunm_pmgreserve: invalid pmg");

	pmg = hwpmgs[pmgnum].hwp_pmgrp;
	ASSERT(pmg != NULL);
	ASSERT(pmg->pmg_num == pmgnum);
	pmg->pmg_keepcnt++;

	/* XXX - the context pointer is the first word of private data */
	if (as->a_hat->hat_data[0] == NULL ||
	    (pmg->pmg_as != NULL && pmg->pmg_as != as))
		sunm_panic("sunm_pmgreserve");

	/*
	 * Synchronize the associated SW ptes and pmes with the hardware. We
	 * need to do this because the hat layer expects this invariant to
	 * hold. In particular, if we ever try to unload this pmeg, it won't
	 * be properly cleared out for re-use, unless the invariant holds.
	 * The ptes are loaded into a local array and only the ones in the
	 * requested range are initialized.
	 * in the range we are requested to reserve.
	 */
	pmg_off = ((u_int)addr & PMGRPOFFSET) >> MMU_PAGESHIFT;
	pmg_cnt = len >> MMU_PAGESHIFT;
	ASSERT(pmg_cnt <= NPMENTPERPMGRP);

	addr = (caddr_t)((u_int)addr & PMGRPMASK);
	sunm_pmgloadswptes(addr, &tmp_ptes[0]);

	for (offset = pmg_off, hme = &pmg->pmg_hme[pmg_off],
	    pte = &tmp_ptes[pmg_off];
	    offset < pmg_off + pmg_cnt;
	    offset++, hme++, pte++) {
		if (pte->pg_v) {
			struct page *pp;
			u_int base;

			base = ((u_int)addr & ~(PMGRPSIZE - 1));
			pmg->pmg_pte[offset] = tmp_ptes[offset];
			pp = page_numtopp_nolock(pte->pg_pfnum);
			if (pp && (pte->pg_type == OBMEM)) {
			    if (((pp->p_mapping != NULL) &&
				(pp->p_mapping != hme)) || pp->p_free) {
				    switch (chk_prompg(pp,
					base + offset * MMU_PAGESIZE)) {
				    case 0:

prom_printf("pp %x mapping %x pfnum %x pmg %x base %x addr %x\n",
pp, pp->p_mapping, pte->pg_pfnum, pmg, base, base + offset * MMU_PAGESIZE);

					sunm_panic(
					    "sunm_pmgreserve mappings");
					/*NOTREACHED*/

				    case 1:
					/*
					 * This page isn't being used by us.
					 */
					pmg->pmg_pte[offset] = mmu_pteinvalid;
					hme->hme_next = NULL;
					hme->hme_prev = NULL;
					hme->hme_valid = 0;
					continue;

				    case 2:
					break;
				    }
			    }
			    if (pp->p_mapping == NULL) {
				hme_add(hme, pp);
			    }
			    ASSERT(pp->p_share == 1);
			}
		} else {
			pmg->pmg_pte[offset] = mmu_pteinvalid;
		}

		ASSERT(hme->hme_next == NULL);
		ASSERT(hme->hme_prev == NULL);

		hme->hme_valid = pte->pg_v;
	}
	if (pmg != pmgrp_invalid && pmg->pmg_as == NULL) {
		sunm_pmglink(pmg, as, addr);
	}
	if (mmu_3level)
		sunm_smgreserve(as, addr);

	mutex_exit(&sunm_mutex);
}

/*
 * Initialize all the unlocked pmgs to have invalid ptes
 * and add them to the free list.
 * This routine is called during startup after all the
 * kernel pmgs have been reserved.  This routine will
 * also set the pmgrpmin variable for use in sunm_pmgalloc.
 */
void
sunm_pmginit(void)
{
	register struct pmgrp	*pmg;
	register struct	hwpmg	*hwpmg;
	register caddr_t	addr;
	register int		i;

	/*
	 * Make HW free list. Skip locked and kept pmgrps.
	 * Here we assume that [0..npmgrps] were loaded in MMU by sunm_init().
	 */
	for (pmg = pmgrps, hwpmg = hwpmgs;
	    hwpmg < hwpmgsNHWPMGS; pmg++, hwpmg++) {

		if (pmg->pmg_keepcnt != 0) {
			pmg->pmg_mapped = 1;
			continue;
		}

		if (pmgrpmin == NULL) {
			pmgrpmin = pmgrphand = pmg;
			hwpmgmin = hwpmghand = hwpmg;
		}

		mmu_settpmg(SEGTEMP, pmg);

		for (addr = SEGTEMP; addr < SEGTEMP + PMGRPSIZE;
		    addr += PAGESIZE)
			mmu_setpte(addr, mmu_pteinvalid);

		pmg->pmg_num = PMGNUM_SW;

		hwpmg->hwp_next = hwpmgfree;
		hwpmg->hwp_pmgrp = NULL;
		hwpmgfree = hwpmg;

	}

	mmu_pmginval(SEGTEMP);

	/*
	 * Make SW pmg free list.
	 */
	for (pmg = pmgrps; pmg < pmgrpsNPMGRPS; pmg++) {

		if (pmg->pmg_keepcnt != 0)
			continue;

		for (i = 0; i < NPMENTPERPMGRP; i++) {
			pmg->pmg_pte[i] = mmu_pteinvalid;
		}
		pmg->pmg_num = PMGNUM_SW;
		pmg->pmg_mapped = 0;

		pmg->pmg_next = pmgrpfree;
		pmgrpfree = pmg;
	}

	/*
	 * Additional SW pmgrps are allocated, up to pmgrpssw,
	 * when the freelist becomes empty.
	 */

	if (mmu_3level)
		sunm_smginit();

	/*
	 * Check keepcnt on smegs and pmegs to detect double mapped pmegs.
	 */
	sunm_smgcheck_keepcntall();
}

/*
 * Set addr in address space as to use pte to (possibly) map to page pp.
 * This is the common routine used for sunm_memload and sunm_devload
 * in addition to the machine dependent mapin implementation.
 */
void
sunm_pteload(
	struct as	*as,
	caddr_t		addr,
	struct page	*pp,
	struct pte	pte,
	int		flags)
{
	register struct hment	*phme;
	register struct pmgrp	*ppmg;
	struct as		*oas;
	struct pte		opte;
	struct hatsw		*hsw;
	u_int			ommuctx;

	mutex_enter(&sunm_mutex);
	if (pp != NULL && pp->p_free && !panicstr)
		sunm_panic("sunm_pteload free page");

	ommuctx = curthread->t_mmuctx;
	oas = sunm_setup(as, HAT_ALLOC);

	/*
	 * sunm_pmgalloc() will return NULL if and only if we don't have
	 * any free HW pmegs available on the free list and this is an
	 * "advisory" load (HAT_ADVLOAD).
	 */
	if ((ppmg = sunm_pmgalloc(as, addr, flags)) == NULL) {
		curthread->t_mmuctx = ommuctx;
		if (oas != NULL)
			(void) sunm_setup(oas, HAT_DONTALLOC);
		mutex_exit(&sunm_mutex);
		return;
	}

	sunm_pmgload(ppmg);

	if (flags & PTELD_LOCK) {
		ppmg->pmg_keepcnt++;
		if (mmu_3level)
			sunm_pmgtosmg(ppmg)->smg_keepcnt++;
	}

	phme = &ppmg->pmg_hme[mmu_btop(addr - ppmg->pmg_base)];

	/*
	 * We must be sure that setting the pte and adding to the list
	 * of mappings is atomic.
	 */
#ifdef	VAC
	/*
	 * If there's no page structure associated with this mapping,
	 * or the vac is turned off, or the page is non-cacheable,
	 * then force the mapping to be non-cached.
	 */
	if (pp == NULL || !vac || PP_ISNC(pp))
		pte.pg_nc = 1;
#endif	/* VAC */

	if (phme->hme_valid) {
		/*
		 * Reloading a translation - be sure to preserve the
		 * existing ref and mod bits for this translation.
		 *
		 * XXX - should cache all the attributes of a loaded
		 * translation in the hme structure so that we can
		 * avoid reloading all together unless something
		 * is actually going to change.
		 */

		/*
		 * Synch SW PTE by reading MMU.
		 */
		opte = *sunm_addrtopte(ppmg, addr);

		mmu_getpte(addr, &opte);
		pte.pg_r = opte.pg_r;
		pte.pg_m = opte.pg_m;

#ifdef VAC
		/*
		 * Must flush vac now to clear the cache of any
		 * writeable lines because this operation could
		 * be changing them to read-only, bug 1103648.
		 */
		if ((pp != NULL) && vac && pte.pg_r &&
		    *(int *)&pte != *(int *)&opte)
			vac_pageflush(addr);
#endif /* VAC */

	} else {
		phme->hme_valid = 1;
	}

#ifdef	IOC
	if (ioc) {
		struct pte *p = &pte;

		if (flags & PTELD_IOCACHE) {
			ioc_pteset(p);
		}
	}
#endif	/* IOC */

	*sunm_addrtopte(ppmg, addr) = pte; /* Set SW PTE */

	mmu_setpte(addr, pte);		/* load the SW pte in HW */

	phme->hme_nosync = (flags & PTELD_NOSYNC) != 0;

	/*
	 * Check to see if this hme needs to be added
	 * to the list of hme's mapping this page.
	 */
	if (pp != phme->hme_page) {
		if (phme->hme_page != NULL) {
		    mutex_exit(&sunm_mutex);
		    cmn_err(CE_PANIC, "sunm_pteload (pp %x, phme %x, ppmg %x)",
				pp, phme, ppmg);
		}

		ASSERT(pp != NULL && phme->hme_page == NULL);

#ifdef	VAC
		/*
		 * If (vac) active, then check for conflicts.
		 * A conflict exists if the new and extant mappings
		 * do not match in their "shm_alignment" fields
		 * XXX and one of them is writable XXX.  If conflicts
		 * exist, the extant mappings are flushed UNLESS
		 * one of them is locked.  If one of them is locked,
		 * then the mappings are flushed and converted to
		 * non-cacheable mappings [must be deconverted in
		 * sunm_pteunload].
		 * XXX	need to store protections in hme
		 * 	to employ writable optimization.
		 */
		if (vac && pp->p_mapping && !PP_ISNC(pp)) {
			struct pmgrp	*pmg;		/* temporary pmg */
			struct hment	*hme;		/* temporary hme */
			struct hment    *next;
			struct hat	*hat;
			int unload = 0;
			int uncache = 0;

#ifdef notdef
			struct pmgrp 	*pmgpc = NULL,	 /* user's text pmg */
					*pmgsp = NULL;	 /* user's stack pmg */
			/*
			 * mappings to the user's current stack and
			 * text locations must be locked in memory,
			 * or we run the risk of getting into an
			 * infinite paging loop if the program tries
			 * to read the physical pages containing either
			 * via a mapping that is not cache aliased.
			 */
			if (!servicing_interrupt() && ttolwp(curthread)) {
			    if (ttolwp(curthread)->lwp_regs != NULL &&
				ttolwp(curthread)->lwp_regs->r_sp != 0) {
				    pmgsp = mmu_getpmg((caddr_t)
					ttolwp(curthread)->lwp_regs->r_sp);
				    if (pmgsp != (struct pmgrp *)0)
					pmgsp->pmg_keepcnt ++;
			    }
			    if (ttolwp(curthread)->lwp_regs != NULL &&
				ttolwp(curthread)->lwp_regs->r_pc != 0) {
				    pmgpc = mmu_getpmg((caddr_t)
					ttolwp(curthread)->lwp_regs->r_pc);
				    if (pmgpc != (struct pmgrp *)0)
					pmgpc->pmg_keepcnt ++;
			    }
			}
#endif notdef

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
			 */

			hme = (struct hment *)pp->p_mapping;
			pmg = (struct pmgrp *)hmetopmg(hme);

			/*
			 * check for a mapping that is not consistent
			 */
			if (hme && hme->hme_noconsist) {

				if (hme->hme_ncpref)
				    uncache = 1;
				else
				    unload = 1;

				if (uncache) {
				    pte.pg_nc = 1;
				    PP_SETTNC(pp);
				    for (hsw = hattab;
					(hsw->hsw_name && hsw->hsw_ops);
					hsw++) {
					    if (hsw->hsw_ops->h_pagecachectl)
						HATOP_PAGECACHECTL(hsw, pp,
							HAT_UNCACHE);
				    }
				} else if (unload) {
					while ((hme = pp->p_mapping) != NULL) {
						hat = &hats[hme->hme_hat];
						if (hat->hat_op == &sunm_hatops)
							sunm_pageunload(pp,
							    hme);
						else
							HATOP_PAGEUNLOAD(hat,
							    pp, hme);
					}
				}
			} else if (!VAC_ALIGNED(addr, pmg->pmg_base +
			    mmu_ptob(hme->hme_impl))) {

			/*
			 * Now, check vac consistency modulus;
			 * If the first entry is unaligned,
			 * it is vac inconsistent.  Unload any unlocked
			 * mappings and uncache any locked ones.
			 */

			    /* LINTED 'next' may be used before set */
			    for (hme = pp->p_mapping; hme; hme = next) {
				    next = hme->hme_next;
				    pmg = hmetopmg(hme);
				    hat = &hats[hme->hme_hat];
				    if (pmg->pmg_keepcnt > 0 ||	/* locked */
					pmg->pmg_as == ppmg->pmg_as) {
					    pte.pg_nc = 1;
					    PP_SETTNC(pp);
					    if (hat->hat_op == &sunm_hatops)
						sunm_ptesync(pp, pmg, hme,
						    (caddr_t)NULL, HAT_NCSYNC);
					    else for (hsw = hattab;
						(hsw->hsw_name && hsw->hsw_ops);
						hsw++) {
						    HATOP_PAGECACHECTL(hsw,
							pp, HAT_UNCACHE);
					    }
				    } else {			/* unlocked */
					if (hat->hat_op == &sunm_hatops)
						sunm_pageunload(pp, hme);
					else
						HATOP_PAGEUNLOAD(hat, pp, hme);
				    }
			    }
			}

#ifdef notdef
			/*
			 * Release locked pmgrps.
			 */
			if (pmgsp != (struct pmgrp *)0)
				pmgsp->pmg_keepcnt--;
			if (pmgpc != (struct pmgrp *)0)
				pmgpc->pmg_keepcnt--;
#endif notdef

		}
#endif	/* VAC */

		/*
		 * now add the mapping to the page's mapping list
		 */
		phme->hme_page = pp;
		phme->hme_next = NULL;
		phme->hme_hat = as->a_hat - hats;
		/* phme->hme_prot = ???; */
		hme_add(phme, pp);
		as->a_rss += 1;

	} else {
		if (pp != NULL &&
		    (phme->hme_nosync != ((flags & PTELD_NOSYNC) != 0)))
			sunm_panic("pteload - remap flags");
	}

	/*
	 * keep some statistics on the cache-ability of the translation
	 */
#ifdef	VAC
	if (pte.pg_type == OBMEM) {
		if (as == &kas) {
			if (addr >= DVMA) {
				if (!pte.pg_nc)
					cache_stats.cs_ioc++;
				else
					cache_stats.cs_ionc++;
			} else {
				if (!pte.pg_nc)
					cache_stats.cs_kc++;
				else
					cache_stats.cs_knc++;
			}
		} else {
			if (!pte.pg_nc)
				cache_stats.cs_uc++;
			else
				cache_stats.cs_unc++;
		}
	} else {
		cache_stats.cs_other++;
	}
#else
	if (pte.pg_type == OBMEM) {
		if (as == &kas) {
			if (addr >= DVMA) {
				cache_stats.cs_ionc++;
			} else {
				cache_stats.cs_knc++;
			}
		} else {
			cache_stats.cs_unc++;
		}
	} else {
		cache_stats.cs_other++;
	}
#endif	VAC

	curthread->t_mmuctx = ommuctx;
	if (oas != NULL)
		(void) sunm_setup(oas, HAT_DONTALLOC);
	mutex_exit(&sunm_mutex);
}

void
sunm_mempte(struct page *pp, u_int vprot, register struct pte *ppte)
{
	ASSERT(se_assert(&pp->p_selock) || panicstr);

	*ppte = mmu_pteinvalid;
	ppte->pg_prot = sunm_vtop_prot(vprot);
	ppte->pg_nc = PP_ISNC(pp) ? 1 : 0;
	ppte->pg_v = 1;
	ppte->pg_type = OBMEM;
	ppte->pg_pfnum = page_pptonum(pp);
}

/*
 * Allocate a ctx for use by the specified address space.
 * If there are any pmgrps associated with the sunm, load
 * them up after we get the ctx.
 */
static void
sunm_getctx(struct as *as)
{
	register struct ctx	*ctx, *ttctx;
	register struct pmgrp	*pmg;
	register struct sunm	*osunm, *nsunm;
	register u_short	tt = 0;
	register struct as	*oas;
	register struct smgrp	*smg;

	ASSERT(MUTEX_HELD(&sunm_mutex));

	osunm = (struct sunm *)&as->a_hat->hat_data;

	/* find a free ctx or an old one */
	ttctx = NULL;
	for (ctx = ctxhand + 1; ctx != ctxhand; ctx++) {

		if (ctx == ctxsNCTXS)	/* wrap around the ctx table */
			ctx = ctxs;

		if (ctx->c_lock != 0)	/* can't touch */
			continue;
		/*
		 * Take ctx with no address space.
		 */
		if (ctx->c_as == NULL) {	/* no as - use it */
			ttctx = ctx;
			break;
		}
		if (ttctx == NULL || ctx->c_time <= tt) {
			ttctx = ctx;		/* new "best" ctx */
			tt = ctx->c_time;
		}

	}
	if (ttctx == NULL)
		sunm_panic("sunm_getctx - no ctxs");

	ctxhand = ctx = ttctx;

	/*
	 * Update vmhatstat statistics.
	 */
	if (ctx->c_as) {
		if (ctx->c_clean)
			vmhatstat.vh_ctxstealclean.value.ul++;
		else
			vmhatstat.vh_ctxstealflush.value.ul++;
	} else
		vmhatstat.vh_ctxfree.value.ul++;

	/* remove context from old address space, if any */
	oas = ctx->c_as;
	if (oas) {
		osunm = (struct sunm *)&oas->a_hat->hat_data;
		osunm->sunm_ctx = NULL;
	}

	/* give context to new address space */
	ctx->c_as = as;
	ctx->c_time = ctx_time++;
	nsunm = (struct sunm *)&as->a_hat->hat_data;
	nsunm->sunm_ctx = ctx;

	curthread->t_mmuctx = 0x100 + ctx->c_num;
	mmu_setctx(ctx);

	/*
	 * If we stole a context from another address space,
	 * flush all contexts and free up the mapping resources of
	 * the old address space.
	 */
	if (oas) {
		vac_flushallctx(); /* stealing a context; flush all of them */

		if (mmu_3level) {
			/* invalidate smgrps already loaded for this ctx */
			for (smg = osunm->sunm_smgrps; smg != NULL;
			    smg = smg->smg_next)
				mmu_smginval(smg->smg_base);
		} else {
			/* invalidate pmgrps already loaded for this ctx */
			for (pmg = osunm->sunm_pmgrps; pmg != NULL;
			    pmg = pmg->pmg_next) {
				if (pmg->pmg_mapped) {
					ASSERT(pmg->pmg_num != PMGNUM_SW);
					mmu_pmginval(pmg->pmg_base);
					pmg->pmg_mapped = 0;
				}
			}
		}
	}

	/*
	 * if vac_flushallctx runs, it will mark all contexts as clean
	 * fix the new one to be dirty
	 */
	ctx->c_clean = 0;

	/*
	 * load up any mappings already owned by the new address space
	 */
	if (mmu_3level) {
		for (smg = nsunm->sunm_smgrps; smg != NULL;
		    smg = smg->smg_next)
			mmu_setsmg(smg->smg_base, smg);
	} else {
		for (pmg = nsunm->sunm_pmgrps; pmg != NULL;
		    pmg = pmg->pmg_next) {

			if (pmg->pmg_num != PMGNUM_SW) {
				ASSERT(!pmg->pmg_mapped);
				mmu_setpmg(pmg->pmg_base, pmg);
				pmg->pmg_mapped = 1;
				vmhatstat.vh_ctxmappmgs.value.ul++;
			}
		}
	}
}

/*
 * Used to lock down hat resources for an address range. In this implementation,
 * this means locking down the necessary pmegs. This currently works only
 * for kernel addresses.
 */
void
sunm_reserve(struct as *as, caddr_t addr, u_int len)
{
	register caddr_t	a;
	caddr_t			ea;
	struct pmgrp		*pmg;

	if (as != &kas)
		cmn_err(CE_PANIC, "sunm_reserve");

	mutex_enter(&sunm_mutex);
	for (a = addr, ea = addr + len; a < ea; a += MMU_PAGESIZE) {

		pmg = sunm_pmgalloc(as, a, 0);
		if (pmg == NULL)
			sunm_panic("sunm_reserve: pmg == NULL");
		sunm_pmgload(pmg);
		pmg->pmg_keepcnt++;
		if (mmu_3level)
			sunm_pmgtosmg(pmg)->smg_keepcnt++;
	}
	mutex_exit(&sunm_mutex);
}

u_int
sunm_vtop_prot(u_int vprot)
{

	switch (vprot) {
	case 0:
	case PROT_USER:
		/*
		 * Since 0 might be a valid protection,
		 * the caller should not set valid bit
		 * if vprot == 0 to be sure.
		 */
		return (0);
	case PROT_READ:
	case PROT_EXEC:
	case PROT_READ | PROT_EXEC:
		return (KR);
	case PROT_WRITE:
	case PROT_WRITE | PROT_EXEC:
	case PROT_READ | PROT_WRITE:
	case PROT_READ | PROT_WRITE | PROT_EXEC:
		return (KW);
	case PROT_EXEC | PROT_USER:
	case PROT_READ | PROT_USER:
	case PROT_READ | PROT_EXEC | PROT_USER:
		return (UR);
	case PROT_WRITE | PROT_USER:
	case PROT_WRITE | PROT_EXEC | PROT_USER:
	case PROT_READ | PROT_WRITE | PROT_USER:
	case PROT_READ | PROT_WRITE | PROT_EXEC | PROT_USER:
		return (UW);
	default:
		cmn_err(CE_PANIC, "sunm_vtop_prot");
		/* NOTREACHED */
	}
}

#if defined(VAC)
/*
 * Flush all possible cache lines mapping the given physical page. This
 * is used for software cache consistency with I/O, to clean the cache
 * of all data subject to I/O.
 */
void
sunm_vacsync(u_int pfnum)
{
	register struct page	*pp;
	register struct pmgrp	*pmg;
	register struct hment	*hme;
	caddr_t			va;
	struct pte		tpte;
	struct as		*oas;
	struct hat 		*hat;
	struct sunm		*sunm;
	u_int			ommuctx;

	if (!vac)
		return;

	pp = page_numtopp_nolock(pfnum);

	mutex_enter(&sunm_mutex);

	/*
	 * If the cache is off, the page isn't memory, or the page is
	 * non-cacheable, then none of the page could be in the cache
	 * in the first place, with the exception that a page frame
	 * for kernel .data or .bss objects could be in the cache,
	 * but will have no page structure.
	 */
	if (pp == (struct page *)NULL) {
		extern u_int kpfn_dataseg, kpfn_endbss;
		if (pfnum >= kpfn_dataseg || pfnum <= kpfn_endbss) {
			extern char *e_text;

			/*
			 * In sun4c, the page frame number for the start
			 * of the kernel data segment and the page frame
			 * number for end are latched up in kvm_init().
			 * If a page frame number ends up here, then some-
			 * body is doing i/o to an object in kernel .data
			 * or .bss.
			 *
			 * This is a temporary solution, and it does have
			 * some holes in it. It assumes that the page frame
			 * numbers between kernel .data and end are contiguous.
			 *
			 * As a side note, we could go to the effort of
			 * of reading the kernel pte for the calculated
			 * address to check with the passed page frame
			 * number, but it isn't really worth the effort.
			 */

			va = (caddr_t)(roundup((u_int)e_text, DATA_ALIGN) +
			    ((pfnum - kpfn_dataseg) << MMU_PAGESHIFT));

			vac_pageflush(va);
		}
		mutex_exit(&sunm_mutex);
		return;
	} else if (PP_ISNC(pp)) {
		mutex_exit(&sunm_mutex);
		return;
	}

	/*
	 * Walk the list of translations for this page, flushing each
	 * one.
	 */
	for (hme = pp->p_mapping; hme; hme = hme->hme_next) {
		hat = &hats[hme->hme_hat];
		if (hat->hat_op->h_init == sunm_init) {
			pmg = hmetopmg(hme);
			sunm = (struct sunm *)&pmg->pmg_as->a_hat->hat_data;

			/*
			 * check for cases where we can avoid the flush
			 * if the hat that holds the mapping
			 * has no context, or no pmegs,
			 * or the ctx is clean
			 * or the pmg is not mapped, skip it.
			 */
			if (sunm->sunm_ctx == NULL ||
			    sunm->sunm_pmgrps == NULL ||
			    sunm->sunm_ctx->c_clean ||
			    !pmg->pmg_mapped)
				continue;
			va = pmg->pmg_base + mmu_ptob(hme->hme_impl);

			ommuctx = curthread->t_mmuctx;
			oas = sunm_setup(pmg->pmg_as, HAT_ALLOC);

			mmu_getpte(va, &tpte);
			if (tpte.pg_r)
				vac_pageflush(va);

			curthread->t_mmuctx = ommuctx;
			if (oas != NULL)
				(void) sunm_setup(oas, HAT_DONTALLOC);
		}

	}

	mutex_exit(&sunm_mutex);
}
#endif	defined(VAC)

/*ARGSUSED*/
static int
sunm_map(struct hat *hat, struct as *as, caddr_t addr, u_int len, int flags)
{
	return (0);
}

static void
sunm_unmap(struct as *as, caddr_t addr, u_int len, int flags)

{
	/*
	 * Do not do anything special for address range unmap notifications.
	 * Turn off the HAT_UNMAP flag and call the unload routine to do the
	 * work.
	 */

	flags = (flags & ~HAT_UNMAP) | HAT_UNLOAD;
	sunm_unload(as, addr, len, flags);
}

/*ARGSUSED*/
static int
sunm_probe(struct hat *hat, struct as *as, caddr_t addr)
{
	struct pte	pte;

	mmu_getpte(addr, &pte);
	return (pte.pg_v);
}


/*
 * End machine specific interface routines.
 *
 * The remainder of the routines are private to this module and are used
 * by the routines above to implement a service to the outside caller.
 *
 * Start private routines.
 */

/*
 * sunm_uncache(pp)
 * a interface for folks that want pages permanently non-cached
 */
void
sunm_uncache(page_t *pp)
{
	struct hat *hat;
	struct hment *hme;
	struct pmgrp *pmg;

	mutex_enter(&sunm_mutex);
	PP_CLRTNC(pp);
	PP_SETPNC(pp);
	for (hme = pp->p_mapping; hme; hme = hme->hme_next) {
		hat = &hats[hme->hme_hat];
		if (hat->hat_op->h_init == sunm_init) {
			pmg = hmetopmg(hme);
			sunm_ptesync(pp, pmg, hme, (caddr_t)NULL, HAT_NCSYNC);
		}
	}
	mutex_exit(&sunm_mutex);
}

/*
 * Unload a hme.  We call sunm_ptesync() to unload the translation
 * then remove the hme from the list of hme's mapping the page.
 * Should always be called with the pmgrp for the hme being held.
 */
static void
sunm_pteunload(
	struct pmgrp		*ppmg,
	register struct hment	*phme,
	caddr_t			vaddr,
	int			flags)
{
	struct page		*pp = phme->hme_page;
#ifdef	VAC
	struct hment		*hme, *nhme;
	struct pmgrp		*pmg, *npmg;
	caddr_t			pa, na;
	int			ccf;	/* cache conflict found flag */
#endif	VAC

	ASSERT(MUTEX_HELD(&sunm_mutex));

	if (pp != NULL) {
		/*
		 * Remove it from the list of mappings for the page.
		 */
		hme_sub(phme, pp);
		phme->hme_page = NULL;
		ppmg->pmg_as->a_rss -= 1;
#ifdef	VAC
		/*
		 * if the page is marked temporarily non-cacheable,
		 * see if we are removing a mapping that allows the
		 * remaining mappings to become consistent
		 */
		hme = (struct hment *)pp->p_mapping;
		if (vac && PP_ISTNC(pp) && hme && !hme->hme_noconsist) {
			ccf = 0;

			/*
			 * We removed a mapping above, if all of the
			 * remaining mappings on the the list are
			 * "consistent", recache them.
			 */
			pmg = hmetopmg(hme);
			pa = pmg->pmg_base + mmu_ptob(hme->hme_impl);
			do {
				if (hme->hme_noconsist) {
					/* XXX - should be first on list */
					ccf = 1;
					break;
				}
				if ((nhme = hme->hme_next) != NULL) {
					npmg = hmetopmg(nhme);
					na = npmg->pmg_base +
						mmu_ptob(nhme->hme_impl);
					if (!VAC_ALIGNED(pa, na)) {
						ccf = 1;
						break;
					}
				}
			} while ((hme = hme->hme_next) != NULL);

			if (!ccf) {
				/*
				 * No more cache conflict.
				 * Use sunm_ptesync to resync.
				 */
				PP_CLRTNC(pp);
				hme = pp->p_mapping;
				do {
					sunm_ptesync(pp, hmetopmg(hme),
						hme, (caddr_t)NULL, HAT_NCSYNC);
				} while ((hme = hme->hme_next) != NULL);
			}
		}
#endif	VAC
	}
	/*
	 * Invalidate the translation.
	 */
	if (phme->hme_valid) {
		flags |= HAT_INVSYNC;
		sunm_ptesync(pp, ppmg, phme, vaddr, flags);
		phme->hme_nosync = phme->hme_valid = 0;
	}
}

/*
 * Synchronize the hardware and software of a pte.  Used for updating the
 * hardware nocache bit, the software R & M bits, and invalidating ptes.
 */
static void
sunm_ptesync(
	struct page		*pp,
	register struct pmgrp	*pmg,
	register struct hment	*hme,
	caddr_t			vaddr,
	int			flags)
{
	register struct ctx	*ctxsav = NULL;
	register struct ctx	*nctx = NULL;
	register caddr_t	mapaddr;
	int			pmg_off;
	struct pte		pte;
	struct pte		*ppte;	/* pointer to SW pte */
	struct as		*as;
	struct ctx		*ctx;
	int			usetemp;
	int 			dommu;
	int			doflush;
	int			didsetpte = 0;
	int			didflush = 0;
	u_int			ommuctx;

	ASSERT(MUTEX_HELD(&sunm_mutex));

	if (hme->hme_valid == 0) {
		mutex_exit(&sunm_mutex);
		cmn_err(CE_PANIC, "hat_ptesync - invalid hme: %x", hme);
	}

	ppte = (struct pte *)sunm_hmetopte(pmg, hme);
	as = pmg->pmg_as;
	ctx = (struct ctx *)as->a_hat->hat_data[0]; /* XXX */

	ommuctx = curthread->t_mmuctx;
	ctxsav = mmu_getctx();
	curthread->t_mmuctx = ctxsav->c_num;

	/*
	 * The HAT_VADDR flag means that the vaddr argument contains a valid
	 * page address.
	 *
	 * It's used as optimization when sunm_ptesync is called
	 * from sunm_unload(). We know that the pmg context is set up.
	 *
	 */
	if (flags & HAT_VADDR) {
		if (sunm_pmgisloaded(pmg)) {
			/*
			 * pmg is loaded in HW - make sure that it is mapped.
			 */
			sunm_pmgmap(pmg);

			mapaddr = vaddr;
			dommu = 1;
			usetemp = 0;
			/*
			 * We must flush VAC if the context is not clean.
			 */
			doflush = !ctx->c_clean;

		} else {
			/*
			 * pmg is not loaded in MMU
			 */
			dommu = doflush = usetemp = 0;
			mapaddr = (caddr_t)0;
		}
		goto skip;
	}

	pmg_off = mmu_ptob(hme->hme_impl);
	vaddr = pmg->pmg_base + pmg_off;

	if (!sunm_pmgisloaded(pmg)) {
		/*
		 * SW page table is not loaded.
		 */
		dommu = doflush = usetemp = 0;

		/*
		 * XXX - Set mapaddr to 0 so that we get a kernel text fault
		 * and panic if we try to use it in the code below.
		 */
		mapaddr = (caddr_t)0;
	} else if (ctx == NULL) {
		/*
		 * pmg is loaded but its as does not have a context.
		 * Set things up so that the pmgrp is mapped into a temporary
		 * segment.  No need to do any VAC flushing since this
		 * was done when we took the ctx away.  Set
		 * up the mapaddr within the temporary segment.
		 */
		nctx = ctxsav;		/* no need to switch context */
		mmu_settpmg(SEGTEMP, pmg);
		mapaddr = SEGTEMP + pmg_off;
		dommu = usetemp = 1;
		doflush = 0;
	} else {
		/*
		 * pmg is loaded and pmgs as has a ctx.
		 * Make sure we are in running in the as context.
		 * Use the virtual address as the mapping address.
		 */
		if ((nctx = ctx) != ctxsav) {
			curthread->t_mmuctx = 0x100 | nctx->c_num;
			mmu_setctx(nctx);
		}

		/*
		 * Make sure that pmg is mapped.
		 */
		sunm_pmgmap(pmg);
		mapaddr = vaddr;
		dommu = 1;
		usetemp = 0;

		/*
		 * We must flush VAC if the context is not clean.
		 */
		doflush = !ctx->c_clean;
	}
skip:
	if (!vac)
		doflush = 0;		/* no VAC on this system */

	/*
	 * At this point, the flags are set:
	 *
	 * doflush - if the page must be flushed
	 * dommu   - if pte must be get/set from HW MMU
	 * usetemp - if SEGTEMP is used to access the HW MMU map
	 *
	 * Note that a loaded pmg is also mapped if we have a context.
	 */

	if (pp != NULL) {
		if (flags & HAT_RMSTAT) {
			if (dommu) {
				mmu_getpte(mapaddr, ppte);
			}
			pte = *ppte;
			if (!hme->hme_nosync)
				pp->p_nrm |= ((pte.pg_r ? P_REF : 0) |
					(pte.pg_m ? P_MOD : 0));
			goto out;
		} else if (flags & HAT_RMSYNC) {
			if (dommu) {
				mmu_getpte(mapaddr, ppte);
			}
			pte = *ppte;
#ifdef TRACE
			if (pte.pg_m) {
				TRACE_3(TR_FAC_VM, TR_SAMPLE_MOD,
					"sample mod:vp %x off %x as %x",
					pp->p_vnode, pp->p_offset, as);
			} else if (pte.pg_r) {
				TRACE_3(TR_FAC_VM, TR_SAMPLE_REF,
					"sample ref:vp %x off %x as %x",
					pp->p_vnode, pp->p_offset, as);
			}
#endif /* TRACE */
			/*
			 * Call to record reference and modified bits
			 */
			if (as->a_vbits) {
			    int pmg_off = mmu_ptob(hme->hme_impl);
			    (void) hat_setstat(as, pmg->pmg_base + pmg_off,
				((u_int) pte.pg_r << 1 | (u_int) pte.pg_m));
			}
			if (!hme->hme_nosync)
				pp->p_nrm |= ((pte.pg_r ? P_REF : 0) |
					(pte.pg_m ? P_MOD : 0));
#ifdef	VAC
			/*
			 * When you zero the modified bit in the MMU
			 * and leave it set in the cache you may not
			 * get it set in the mmu when the line is
			 * re-written.  Writeback caches perform the
			 * setting of the modified bit for a page in
			 * the MMU on the first write miss that happens
			 * to that page. Subsequent writes don't bother
			 * to set the modified bit because the first
			 * write did it.  Therefore if you are zeroing
			 * the modified bit you must flush the cache
			 * so that subsequent writes, see the modified
			 * bit unset in the cache and write it back to
			 * the MMU.
			 */
			if (doflush && pte.pg_r) {
				vac_pageflush(mapaddr);
				didflush = 1;
			}
#endif	VAC
			pte.pg_r = pte.pg_m = 0;
		}
#ifdef	VAC
		else if (flags & HAT_NCSYNC) {

			if (dommu) {
				mmu_getpte(mapaddr, ppte);
			}
			pte = *ppte;
			/*
			 * N.B.  The following test assumes that there
			 * are no user addresses at the same virtual
			 * addresses as DVMA and segkp in VAC machines.
			 */
			if (mapaddr >= DVMA) {
				/*
				 * To avoid lots of problems, we don't
				 * try to convert anything from cached
				 * to non-cached (or vice-versa) when
				 * it is being loaded for DVMA use.
				 * Also, we refuse to mess with user
				 * areas since it is impossible to
				 * reliably flush when converting
				 * from cached to non-cached and we
				 * don't want to take any performance
				 * hits from using a non-cached stack.
				 */
				didsetpte = 1;
				cache_stats.cs_skip++;

				goto skip_ncsync;
			}

			/*
			 * To avoid lots of problems, we don't try to convert
			 * anything from cached to non-cached (or vice-versa)
			 * when it is being loaded for DVMA use.
			 */
			if (mapaddr < DVMA) {
				if (doflush && !pte.pg_nc && PP_ISNC(pp)) {
					int pri, iskas;

					/*
					 * Need to convert from a cached
					 * translation to a non-cached
					 * translation.  There are lots
					 * of potential races here in the
					 * kernel's address space.  If
					 * some clean line ends up in the
					 * cache after it is flushed here
					 * and is then written to, the
					 * Sirius cache system will end
					 * up giving a memory timeout error.
					 *
					 * For now, we assume that between
					 * time that we flush the virtual
					 * address and reset the MMU that
					 * nothing will be getting into
					 * the cache from things like
					 * ethernet (this is questionable).
					 * We also assume that will never
					 * be converting anything from
					 * cached to non-cached in the
					 * kernel for the current stack,
					 * (i.e., the stack can be accessed
					 * safely w/o it being changed from
					 * cached to non-cached), the interrupt
					 * stack, or anything that might be
					 * touched at interrupts above splhigh
					 * (UARTS, level7 profiling).
					 * The stack being considered safe
					 * will need to be watched if/when
					 * we go away from using a fixed
					 * virtual address for the user area
					 * that is not managed by the hat layer.
					 */
					if (!pmg->pmg_mapped) {
						sunm_panic(
						"sunm_ptesync: pmg not mapped");
					}

					pte.pg_nc = 1;

					iskas = (as == &kas);

					pri = splhigh();

					if (pte.pg_r)
						vac_pageflush(mapaddr);

					/* Change both SW and HW pte */
					*ppte = pte;
					mmu_setpte(mapaddr, pte);
					didsetpte = 1;

					if (iskas) {
						/*
						 * Flush the virtual address
						 * again just in case some IO
						 * got in behind our back
						 * above.  Doing this for
						 * iskas only assumes there
						 * is no UDVMA to worry about.
						 */
						struct pte	tpte;

						mmu_getpte(mapaddr, &tpte);
						if (tpte.pg_r)
							vac_pageflush(mapaddr);

						cache_stats.cs_kchange++;
					} else {
						cache_stats.cs_uchange++;
					}
					(void) splx(pri);
				} else {
					pte.pg_nc = PP_ISNC(pp) ? 1 : 0;
				}
			} else {
				cache_stats.cs_iowantchg++;
			}
		skip_ncsync:
			;
		}
#endif	VAC
	}

	if (flags & HAT_INVSYNC) {
#ifdef	VAC
		if (doflush && !didflush) {
			struct pte	tpte;

			mmu_getpte(mapaddr, &tpte);
			if (tpte.pg_r)
				vac_pageflush(mapaddr);
		}
#endif	VAC
		pte = mmu_pteinvalid;
	}
	if (!didsetpte) {
		*ppte = pte;
		if (dommu) {
			mmu_setpte(mapaddr, pte);
		}
	}
out:
	/*
	 * Optimized return when sunm_ptesync was called from sunm_unload.
	 */
	if (!(flags & HAT_VADDR)) {
		if (usetemp)
			mmu_settpmg(SEGTEMP, pmgrp_invalid);
		curthread->t_mmuctx = ommuctx;
		if (nctx != ctxsav) {
			mmu_setctx(ctxsav);
		}
	}
	curthread->t_mmuctx = ommuctx;
}

/*
 * Allocate a SW page table for a given address.
 */
static struct pmgrp *
sunm_pmgalloc(struct as *as, caddr_t addr, int flags)
{
	struct pmgrp	*pmg;
	struct pmgseg	*pms;

	ASSERT(MUTEX_HELD(&sunm_mutex));

	if ((pmg = sunm_pmgfind(addr, as)) != NULL) {
		vmhatstat.vh_pmgallochas.value.ul++;
		return (pmg);
	}

	/*
	 * Don't allocate a software pmeg if this is an "advisory" load
	 * and there aren't any free HW pmegs.
	 */
	if ((flags & HAT_ADVLOAD) && hwpmgfree == NULL)
		return ((struct pmgrp *)NULL);

	/*
	 * No pmgrp allocated to this address space contains the address;
	 * allocate a new pmg for this address space.  First, try
	 * the free list.
	 */

	/*
	 * Update vmhatstat statistics.
	 */
	if (pmgrpfree == NULL)
		vmhatstat.vh_pmgallocsteal.value.ul++;
	else
		vmhatstat.vh_pmgallocfree.value.ul++;

top:
	if ((pmg = pmgrpfree) == NULL) {
	    int try;

	    pmg = pmgrphand;
	    pms = pmgtopmgseg(pmgrphand);

	    for (try = 1; /* empty */; try++) {
		do {
		    pmg++;
		    if (pmg >= (pms->pms_base + pms->pms_size)) {
			if (pms->pms_next)
				pms = pms->pms_next;
			else
				pms = pmgseglist;
			pmg = pms->pms_base;
		    }

		    if (pmg->pmg_keepcnt == 0) {
			/*
			 * On the first try, only take a pmg
			 * from an address space with no ctx.
			 */
			if (try == 1 &&
			    pmg->pmg_as->a_hat->hat_data[0] != NULL)
				continue;
			/*
			 * Found a candidate, free
			 * it up and try again.
			 */
			sunm_pmgfree(pmg);
			pmgrphand = pmg;
			goto top;
		    }
		} while (pmg != pmgrphand);

		/*
		 * Give up after 2 tries.
		 */
		if (try >= 2)
			sunm_panic("sunm_pmgalloc out of hat");
	    }
	}

	pmgrpfree = pmg->pmg_next;	/* take it off the free list */
	ASSERT(pmg->pmg_keepcnt == 0);

	sunm_pmglink(pmg, as, addr);

	pmgrps_free--;

	/*
	 * If there are no pmgs on the pmgrpfree list and there have
	 * been pmg_steal_limit pmgs stolen without one being freed,
	 * set a flag to allocate more pmgrps.
	 */
	if ((pmgrpfree == NULL) &&
	    (pmgrps_allocd < npmgrpssw) &&
	    (pmgsteals++ > pmg_steal_limit)) {

		pmgsteals = 0;
		/*
		 * If there aren't enough pages for another
		 * pmgseg don't bother trying to allocate.
		 */
		if (freemem > pmgsegsz_pgs)
			need_allocpmgs = 1;
	}

	return (pmg);
}

/*
 * Load SW pmg in HW
 */
static void
sunm_pmgload(struct pmgrp *swpmg)
{
	register struct as	*as;
	register struct pmgrp	*pmg = (struct pmgrp *)NULL;
	register struct hwpmg	*hwpmg;
	struct as		*oas;
	int			pass;
	u_int			ommuctx;

	ASSERT(MUTEX_HELD(&sunm_mutex));

	if (swpmg->pmg_mapped) {
		ASSERT(swpmg->pmg_num != PMGNUM_SW);
		return;
	}

	as = swpmg->pmg_as;

	/*
	 * Loaded, but not mapped.
	 */
	if (swpmg->pmg_num != PMGNUM_SW) {
		ASSERT(as == &kas || mmu_getctx()->c_as == as);
		sunm_pmgmap(swpmg);
		return;
	}

	ommuctx = curthread->t_mmuctx;
	oas = sunm_setup(as, HAT_ALLOC);

	if ((hwpmg = hwpmgfree) != NULL) {
		vmhatstat.vh_pmgldfree.value.ul++;
		goto found_free_hwpmg;
	}

	/*
	 * The strategy for stealing a HW hmeg:
	 * We make 3 passes over the array of HW pmegs (starting at hwpmghand)
	 * and will take the pmeg if:
	 *
	 * Pass 1. pmeg does not require VAC flush
	 *    (we clean all pmegs between Pass 1 and Pass 2).
	 * Pass 2. Take any unlocked pmeg. In pass 2, we will take
	 *    even a dirty pmeg because it may be an unused kernel pmeg.
	 */
	hwpmg = hwpmghand;
	for (pass = 1; pass < 3; pass++) {
		struct ctx	*ctx;

		do {
			hwpmg++;
			if (hwpmg == hwpmgsNHWPMGS) {
				/*
				 * Wrap around and skip kernel pmgs.
				 */
				hwpmg = hwpmgmin;
			}

			pmg = hwpmg->hwp_pmgrp;
			ASSERT(pmg != NULL);
			ASSERT(pmg->pmg_num != PMGNUM_SW);

			/*
			 * Skip locked and kept pmgs
			 */
			if (pmg->pmg_keepcnt != 0)
				continue;

			if (pass == 1 &&
	    (ctx = (struct ctx *)pmg->pmg_as->a_hat->hat_data[0]) != NULL &&
			    !ctx->c_clean && pmg->pmg_mapped)
				continue;

			if (pass == 1) {
				if (ctx == NULL)
					vmhatstat.vh_pmgldnoctx.value.ul++;
				else if (ctx->c_clean)
					vmhatstat.vh_pmgldcleanctx.value.ul++;
				else
					vmhatstat.vh_pmgldnomap.value.ul++;
			}

			/*
			 * Keep the pmg until sunm_pmgswapptes().
			 */
			sunm_pmgunload(pmg, PTESFLAG_SKIP);
			ASSERT(hwpmg == hwpmgfree);
			hwpmghand = hwpmg;
			goto found_free_hwpmg;

		} while (hwpmg != hwpmghand);

		if (pass == 1) {

			/*
			 * We haven't found a pmg that does not require
			 * VAC flush. Clear all pmgs now by flushing
			 * all contexts.
			 */
			vac_flushallctx();
			sunm_unmap_aspmgs(as);
			vmhatstat.vh_pmgldflush.value.ul++;

			/*
			 * Randomize the hwpghand to keep from
			 * cycling through the hwpmgs sequentially.
			 * This can happen when a large program
			 * completely fills (overflows) the mapping
			 * provided by the MMU, if we steal LRU,
			 * the one we steal now is quite likely the
			 * one we may need next.
			 */
			hwpmghand += npmgrps/2;
			if (hwpmghand >= hwpmgsNHWPMGS) {
				hwpmghand = hwpmgmin +
					(hwpmghand - hwpmgsNHWPMGS);
			}

			/*
			 * sunm_unmap_aspmgs() may have freed one
			 * or more pmgs, if so, we are done.
			 */
			if (hwpmgfree) {
				hwpmg = hwpmgfree;
				pmg = NULL;
				goto found_free_hwpmg;
			}
		}
	}
	sunm_panic("sunm_pmgload: failed after two  passes");

found_free_hwpmg:
	sunm_clrcleanbit();
	swpmg->pmg_num = (hwpmg - hwpmgs);
	hwpmg->hwp_pmgrp = swpmg;
	hwpmgfree = hwpmg->hwp_next;	/* take it off the free list */

	ASSERT(swpmg->pmg_as == &kas || mmu_getctx()->c_as == swpmg->pmg_as);

	if (mmu_3level)
		sunm_smgalloc(as, swpmg->pmg_base, swpmg);

	mmu_setpmg(swpmg->pmg_base, swpmg);
	swpmg->pmg_mapped = 1;

	/*
	 * Load all valid SW PTE's in MMU.
	 *
	 */
	if (pmg == (struct pmgrp *)NULL)
		sunm_pmgloadptes(swpmg->pmg_base, swpmg->pmg_pte);
	else
		sunm_pmgswapptes(swpmg->pmg_base, swpmg->pmg_pte, pmg->pmg_pte);

	/* incr. number of HW pmegs for this as */
	as->a_hat->hat_data[3]++;	/* XXX - sunm_pmgldcnt++ */

	curthread->t_mmuctx = ommuctx;
	if (oas != NULL)
		(void) sunm_setup(oas, HAT_DONTALLOC);
}

/*
 * Map a pmgrp in segment map.
 */
static void
sunm_pmgmap(struct pmgrp *pmg)
{
	ASSERT(MUTEX_HELD(&sunm_mutex));

	if (pmg->pmg_num != PMGNUM_SW && !pmg->pmg_mapped)  {
		if (mmu_3level)
			sunm_smgalloc(pmg->pmg_as, pmg->pmg_base, pmg);
		mmu_setpmg(pmg->pmg_base, pmg);
		pmg->pmg_mapped = 1;
	}
}

/*
 * Free the specified SW pmgrp.  This is done by calling sunm_pteunload
 * on all the hme's to process all the referenced and modified bits
 * and to invalidate the hme.  If the hat containing this pmg currently
 * has a ctx, then invalidate that mapping.  Finally we unlink the
 * the pmgrp from the hat pmgrp list and put it on the free list.
 * pmg should be kept (once) when this routine is called.
 */
static void
sunm_pmgfree(register struct pmgrp *pmg)
{
	register struct hment	*hme = pmg->pmg_hme;
	register struct as	*as;
	register int		cnt;
	register struct sunm	*sunm;
	register int 		hashind;

	ASSERT(MUTEX_HELD(&sunm_mutex));
	ASSERT(pmg->pmg_keepcnt == 0);
	ASSERT(pmg->pmg_as != NULL);

	if (sunm_pmgisloaded(pmg))
		sunm_pmgunload(pmg, PTESFLAG_UNLOAD);

	if ((as = pmg->pmg_as) != NULL) {
		for (cnt = 0; cnt < NPMENTPERPMGRP; cnt++, hme++) {
			if (hme->hme_valid)
				sunm_pteunload(pmg, hme, (caddr_t)NULL,
				    HAT_RMSYNC);
		}
		sunm = (struct sunm *)&as->a_hat->hat_data;
		if (sunm->sunm_pmgrps == pmg) {
			sunm->sunm_pmgrps = pmg->pmg_next;
			if (pmg->pmg_next)
				pmg->pmg_next->pmg_prev = NULL;
		} else {
			pmg->pmg_prev->pmg_next = pmg->pmg_next;
			if (pmg->pmg_next)
				pmg->pmg_next->pmg_prev = pmg->pmg_prev;
		}

		hashind = hash_asaddr(pmg->pmg_as, pmg->pmg_base);
		if (pmghash[hashind] == pmg)
			pmghash[hashind] = NULL;

		pmg->pmg_as = NULL;
		pmg->pmg_next = pmg->pmg_prev = NULL;
	}
	ASSERT(pmg->pmg_keepcnt == 0);
	pmg->pmg_next = pmgrpfree;
	pmgrpfree = pmg;

	pmgrps_free++;
	if (!freeing_pmgs && (pmgrps_allocd > pmgrps_lowater) &&
	    (pmgrps_free > (pmgsegsz * 2)) &&
	    (pmgrps_countdown-- < 0)) {
		need_freepmgs = 1;
	}
}

static void
sunm_pmgunload(struct pmgrp *pmg, enum ptesflag ptesflag)
{
	caddr_t		a;
	struct pte	*ppte = pmg->pmg_pte;
	struct hwpmg	*hwpmg;
	struct ctx	*ctx, *ctxsav;
	struct sunm	*sunm;
	u_int		ommuctx;

	ASSERT(MUTEX_HELD(&sunm_mutex));
	ASSERT(pmg->pmg_num != PMGNUM_SW);

	sunm = (struct sunm *)&pmg->pmg_as->a_hat->hat_data;

	ASSERT(sunm->sunm_ctx == NULL || pmg->pmg_as == sunm->sunm_ctx->c_as);

#ifdef	notdef				/* 3 level mmu */
	ASSERT(sunm->sunm_ctx != NULL || !pmg->pmg_mapped);
#endif	notdef

	/*
	 * Unmap pmg from segment map.
	 */
	if (pmg->pmg_mapped) {

		if (mmu_3level) {
			ASSERT(pmg->pmg_sme != NULL);
		}

		if ((ctx = sunm->sunm_ctx) != NULL) {
			ctxsav = mmu_getctx();
			if (ctxsav != ctx) {
				ommuctx = curthread->t_mmuctx;
				curthread->t_mmuctx = 0x100 | ctx->c_num;
				mmu_setctx(ctx);
			}

			if (!ctx->c_clean)
				vac_segflush(pmg->pmg_base);

			mmu_pmginval(pmg->pmg_base);

			if (ctxsav != ctx) {
				curthread->t_mmuctx = ommuctx;
				mmu_setctx(ctxsav);
			}
		} else {
			if (mmu_3level) {
				/*
				 * We have to use REGTEMP to map the smg.
				 * Note that REGTEMP may be used only in kctx.
				 */

				struct smgrp	*smg;

				ctxsav = mmu_getctx();
				if (ctxsav != kctx) {
					ommuctx = curthread->t_mmuctx;
					curthread->t_mmuctx = 0x100 | KCONTEXT;
					mmu_setctx(kctx);
				}

				smg = &smgrps[(pmg->pmg_sme - sments)
				    >> NSMENTPERSMGRPSHIFT];
				mmu_setsmg(REGTEMP, smg);
				mmu_pmginval(REGTEMP +
				    ((u_int)pmg->pmg_base & SMGRPOFFSET));
				mmu_smginval(REGTEMP);

				if (ctxsav != kctx) {
					curthread->t_mmuctx = ommuctx;
					mmu_setctx(ctxsav);
				}
			} else
				sunm_panic("sunm_pmgunload");
		}
		pmg->pmg_mapped = 0;

		if (mmu_3level) {
			pmg->pmg_sme->sme_valid = 0;
			pmg->pmg_sme->sme_pmg = (struct pmgrp *)NULL;
			pmg->pmg_sme = (struct sment *)NULL;
		}
	}

	ASSERT(!pmg->pmg_mapped);

	if (ptesflag == PTESFLAG_UNLOAD) {
		/*
		 * Unload all valid SW PTE's in MMU.
		 *
		 */
		map_setsgmap(SEGTEMP, pmg->pmg_num);
		a = SEGTEMP;
		sunm_pmgunloadptes(a, ppte);
		map_setsgmap(SEGTEMP, PMGRP_INVALID);
	}


	sunm->sunm_pmgldcnt--;

	/*
	 * Put hwpmg in HW pmg free list
	 */
	hwpmg = &hwpmgs[pmg->pmg_num];
	hwpmg->hwp_next = hwpmgfree;
	hwpmgfree = hwpmg;
	hwpmg->hwp_pmgrp = NULL;

	pmg->pmg_num = PMGNUM_SW;

}

/*
 * Add the specified pmgrp to the list of pmgrp's allocated to
 * the specified address space.  We hang pmgrps off the address
 * space and not the ctx so that we can keep them around even if
 * we don't have a hardware context.
 */
static void
sunm_pmglink(register struct pmgrp *pmg, struct as *as, caddr_t addr)
{
	struct sunm	*sunm = (struct sunm *)&as->a_hat->hat_data;

	ASSERT(MUTEX_HELD(&sunm_mutex));

	pmg->pmg_as = as;
	pmg->pmg_next = sunm->sunm_pmgrps;
	if (pmg->pmg_next)
		pmg->pmg_next->pmg_prev = pmg;
	pmg->pmg_prev = NULL;
	sunm->sunm_pmgrps = pmg;
	pmg->pmg_base = (caddr_t)((u_int)addr & ~(PMGRPSIZE - 1));
}

static void
sunm_xfree(register struct sunm *sunm)
{
	register struct pmgrp *pmg;

	ASSERT(MUTEX_HELD(&sunm_mutex));

	/*
	 * Free pmgrp's.
	 */
	while ((pmg = sunm->sunm_pmgrps) != NULL) {
		sunm_pmgfree(pmg);
		pmgsteals = 0;
	}

	/*
	 * If three level mmu, free smgrp's.
	 */
	if (mmu_3level) {
		register struct smgrp *smg;

		while ((smg = sunm->sunm_smgrps) != NULL) {
			sunm_smgfree(smg);	/* should unkeep the smg */
		}
	}

	ASSERT(sunm->sunm_pmgldcnt == 0);
}

/*
 * Find a SW page table.
 *
 * The returned page table is 'kept'.
 *
 * We optimize the search by using a look-aside buffer of mappings
 * from <as, addr> to a pointer to the pmg structure. For each hashed
 * <as, addr> value, we store a pointer to the most recently found pmgrp.
 * 1009 is a prime that was found to be optimal.
 *
 * XXX	Perhaps this could be inlined as a macro?
 */
static int
hash_asaddr(register struct as *as, register caddr_t addr)
{
	return ((((u_int)as >> 2) * 1009 + ((u_int)addr >> PMGRPSHIFT)) &
		npmghash_offset);
}

static struct pmgrp *
sunm_pmgfind(caddr_t addr, struct as *as)
{
	struct pmgrp	*pmg;
	caddr_t		pmgaddr = (caddr_t)sunm_pmgbase(addr);
	int		hashind;
	struct	sunm	*sunm;

	ASSERT(MUTEX_HELD(&sunm_mutex));

	/*
	 * We optimize for the typical case by reading the MMU segment
	 * map.
	 */
	if ((pmg = mmu_getpmg(addr)) != pmgrp_invalid && pmg->pmg_as == as) {
		pmgfindstat.pf_mmuhit++;
		goto out;
	}

	hashind = hash_asaddr(as, pmgaddr);

	/*
	 * Check if pmg is in the lookaside buffer.
	 */
	if ((pmg = pmghash[hashind]) != NULL &&
	    pmg->pmg_as == as && pmg->pmg_base == pmgaddr) {
		pmgfindstat.pf_hit++;
		goto out;
	}

	/*
	 * Exhaustive search of pmgs within the address space.
	 */
	sunm = (struct sunm *)&as->a_hat->hat_data;
	for (pmg = sunm->sunm_pmgrps; pmg != NULL &&
	    pmg->pmg_base != pmgaddr; pmg = pmg->pmg_next)
		;

	if (pmg != NULL) {
		pmgfindstat.pf_miss++;

		/*
		 * Write the <as, addr> -> pmg entry to look aside buffer
		 */
		pmghash[hashind] = pmg;
	} else {
		pmgfindstat.pf_notfound++;
	}

out:
	return (pmg);
}

/*
 * Clear running process's context clean bit.
 */
static void
sunm_clrcleanbit(void)
{
	struct	proc	*p;
	struct	as	*a;
	struct	ctx	*c;
	struct	sunm	*sunm;
	struct  hat	*hat;

	ASSERT(MUTEX_HELD(&sunm_mutex));
	if ((p = curproc) != NULL &&
	    (a = p->p_as) != NULL &&
	    (hat = a->a_hat) != NULL &&
	    (sunm = (struct sunm *)&hat->hat_data) &&
	    (c = sunm->sunm_ctx) != NULL) {
		c->c_clean = 0;
	}
}


/*
 * Code to handle allocation of smegs is cloned from the pmeg versions
 */

static int getsmg_check = 1;

/*
 * This routine will return the smgrp structure for the given address
 * in the current ctx.  But unlike mmu_getsmg, this routine will protect
 * against the smgrp being lost by spl'ing and will return a kept smgrp
 * pointer.  The keepcnt should be decremented by the caller when it is
 * done looking at the smgrp contents.
 */
static struct smgrp *
sunm_getsmg(caddr_t addr)
{
	struct smgrp *smg;

	smg = mmu_getsmg(addr);
	if (getsmg_check && smg != smgrp_invalid && smg->smg_base != 0 &&
	    smg->smg_base != (caddr_t)((u_int)addr & ~(SMGRPSIZE - 1))) {
		printf("sunm_getsmg: addr=%x, smg=%x, smg base=%x\n",
		    addr, smg, smg->smg_base);
		(void) debug_enter("sunm_getsmg");
	}
	return (smg);
}

/*
 * Free the specified smgrp.  This is done by calling sunm_pmgfree
 * on all the sme's to invalidate the smgrp.  If the hat containing
 * this smg currently has a ctx, then invalidate that mapping.
 * Finally we unlink the the smgrp from the hat smgrp list and
 * put it on the free list.
 * smg should be kept (once) when this routine is called.
 */
static void
sunm_smgfree(register struct smgrp *smg)
{
	register struct sment *sme;
	register struct pmgrp *pmg;
	register struct sunm *sunm;
	register int cnt;
	struct ctx *ctx, *ctxsav, *curctx;
	u_int		ommuctx;

	ASSERT(MUTEX_HELD(&sunm_mutex));
	ASSERT(smg->smg_keepcnt == 0);

	sunm = (struct sunm *)&smg->smg_as->a_hat->hat_data;

	ommuctx = curthread->t_mmuctx;

	if (smg->smg_as != NULL) {

		/* XXX - we should simplify switching between ctx's */
		ctxsav = curctx = mmu_getctx();
		if ((ctx = sunm->sunm_ctx) != NULL &&
		    !ctx->c_clean) {
			curthread->t_mmuctx = 0x100 | ctx->c_num;
			mmu_setctx(curctx = ctx);
			vac_rgnflush(smg->smg_base);
		}

		if (curctx != kctx) {
			curthread->t_mmuctx = 0x100;
			mmu_setctx(curctx = kctx);
		}

		/*
		 * XXX - we may optimize by not using kctx if smg has ctx.
		 */
		ASSERT(mmu_getsmg(REGTEMP) == smgrp_invalid);
		mmu_setsmg(REGTEMP, smg);
		sme = smg->smg_sme;
		for (cnt = 0; cnt < NSMENTPERSMGRP; cnt++) {
			if (sme->sme_valid) {
				pmg = sme->sme_pmg;
				ASSERT(((u_int)pmg->pmg_base & SMGRPOFFSET) <
				    SMGRPSIZE);
				mmu_pmginval(REGTEMP +
				    ((u_int)pmg->pmg_base & SMGRPOFFSET));
				pmg->pmg_mapped = 0;
				sme->sme_valid = 0;
				pmg->pmg_sme = (struct sment *)NULL;
				sme->sme_pmg = (struct pmgrp *)NULL;
			}
			sme++;
		}
		mmu_smginval(REGTEMP);
		if ((ctx = sunm->sunm_ctx) != NULL) {
			if (ctx != curctx) {
				curthread->t_mmuctx = 0x100 | ctx->c_num;
				mmu_setctx(curctx = ctx);
			}
			mmu_smginval(smg->smg_base);
		}
		if (ctxsav != curctx) {
			curthread->t_mmuctx = ommuctx;
			mmu_setctx(ctxsav);
		}

		if (sunm->sunm_smgrps == smg) {
			sunm->sunm_smgrps = smg->smg_next;
			if (smg->smg_next)
				smg->smg_next->smg_prev = NULL;
		} else {
			smg->smg_prev->smg_next = smg->smg_next;
			if (smg->smg_next)
				smg->smg_next->smg_prev = smg->smg_prev;
		}
		smg->smg_as = NULL;
		smg->smg_next = smg->smg_prev = NULL;
	}
	smg->smg_next = smgrpfree;
	smgrpfree = smg;

	curthread->t_mmuctx = ommuctx;
}

/*
 * Add the specified smgrp to the list of smgrp's allocated to
 * the specified address space.  We hang smgrps off the address
 * space and not the ctx so that we can keep them around even if
 * we don't have a hardware context.
 */
static void
sunm_smglink(register struct smgrp *smg, struct as *as, caddr_t addr)
{
	struct sunm *sunm = (struct sunm *)&as->a_hat->hat_data;

	ASSERT(MUTEX_HELD(&sunm_mutex));
	smg->smg_as = as;
	smg->smg_next = sunm->sunm_smgrps;
	if (smg->smg_next)
		smg->smg_next->smg_prev = smg;
	smg->smg_prev = NULL;
	sunm->sunm_smgrps = smg;
	smg->smg_base = (caddr_t)((u_int)addr & ~(SMGRPSIZE - 1));
}

static void
sunm_smgreserve(struct as *as, caddr_t addr)
{
	register struct smgrp	*smg;
	register struct pmgrp	*pmg;
	struct sment		*sme;
	struct sunm		*sunm;

	if (!mmu_3level)
		return;

	sunm = (struct sunm *)&as->a_hat->hat_data;

	ASSERT(MUTEX_HELD(&sunm_mutex));
	smg = sunm_getsmg(addr);
	smg->smg_keepcnt++;

	/* XXX - for debug, delete later */
	if (smg == smgrp_invalid)
		printf("sunm_smgreserve: addr 0x%x invalid smg\n", addr);

	if (sunm->sunm_ctx == NULL ||
	    (smg->smg_as != NULL && smg->smg_as != as))
		sunm_panic("sunm_smgreserve");

	if (smg != smgrp_invalid && smg->smg_as == NULL)
		sunm_smglink(smg, as, addr);

	/*
	 * Set up sme structure.
	 */
	pmg = mmu_getpmg(addr);
	sme = &(smg->smg_sme[(mmu_btop(addr-smg->smg_base)/NPMENTPERPMGRP)]);

	ASSERT(!sme->sme_valid || sme == pmg->pmg_sme);

	if (!sme->sme_valid) {
		pmg = mmu_getpmg(addr);
		ASSERT(pmg->pmg_sme == NULL);
		sme->sme_pmg  = pmg;
		sme->sme_valid = 1;
		pmg->pmg_sme = sme;
	}
}

/*
 * Initialize all the unlocked smgs to have invalid sme's
 * and add them to the free list.
 * This routine is called during startup after all the
 * kernel smgs have been reserved.  This routine will
 * also set the smgrpmin variable for use in sunm_smgalloc.
 *
 * REGTEMP is only used here so we temporarily steal
 * the region before KERNELBASE and mark it invalid
 * when we are finished.
 */
static void
sunm_smginit(void)
{
	register struct smgrp *smg;
	register caddr_t addr;

	if (!mmu_3level)
		return;

	for (smg = smgrps; smg < smgrpsNSMGRPS; smg++) {
		if (smg->smg_keepcnt != 0)
			continue;

		if (smgrpmin == NULL)
			smgrpmin = smg;

		mmu_settsmg((caddr_t)REGTEMP, smg);

		for (addr = (caddr_t)REGTEMP;
		    addr < (caddr_t)(REGTEMP + SMGRPSIZE);
		    addr += PMGRPSIZE) {
			mmu_pmginval(addr);
		}

		smg->smg_next = smgrpfree;
		smgrpfree = smg;
	}
	smgrphand = smgrpmin;

	mmu_smginval((caddr_t)REGTEMP);
}

/*
 * Allocate a smgrp to map the specified address.
 * Returns w/ the keepcnt incremented for the particular smgrp used.
 * First look for something in the free list and then steal one
 * that is currently being used.
 */
static void
sunm_smgalloc(struct as *as, caddr_t addr, struct pmgrp *pmg)
{
	register struct smgrp	*smg;
	register struct sment	*sme;
	register struct sunm	*sunm;
	struct ctx		*ctx;

	if (!mmu_3level)
		return;

	sunm = (struct sunm *)&as->a_hat->hat_data;

	ASSERT(MUTEX_HELD(&sunm_mutex));
	if ((smg = mmu_getsmg(addr)) != smgrp_invalid) {
		if (getsmg_check && smg->smg_base !=
		    (caddr_t)((u_int)addr & ~(SMGRPSIZE - 1))) {
			mutex_exit(&sunm_mutex);
			printf("sunm_smgalloc: addr=%x, smg=%x, smg base=%x\n",
			    addr, smg, smg->smg_base);
			cmn_err(CE_PANIC, "sunm_smgalloc");
		}
		sme = &(smg->smg_sme
			[(mmu_btop(addr-smg->smg_base)/NPMENTPERPMGRP)]);
		sme->sme_pmg  = pmg;
		sme->sme_valid = 1;
		pmg->pmg_sme = sme;
		return;
	}

	/*
	 * No smgrp allocated to this address space contains the hme,
	 * allocate a new smg for this address space.  First, try
	 * the free list.
	 */
	if (smgrpfree != NULL)
		vmhatstat.vh_smgfree.value.ul++;

top:
	if ((smg = smgrpfree) == NULL) {
		int try;

		/*
		 * No smg's free, have to take one from someone.
		 * Take from address spaces with no ctx first.
		 * XXX - could do it with just one pass.
		 */
		smg = smgrphand;
		try = 1;
		for (;;) {
			do {
				smg++;
				if (smg == smgrpsNSMGRPS) {
					if (smgrpmin) {
						/* skip some kernel smgrps */
						smg = smgrpmin;
					} else {
						smg = smgrps;
					}
				}
				if (smg->smg_keepcnt == 0) {
					/*
					 * On the first try, only take a smg
					 * from an address space with no ctx.
					 */
					if (try < 3 &&
					    ((ctx = sunm->sunm_ctx) != NULL) &&
					    !ctx->c_clean)
						continue;

					/*
					 * Found a candidate, free
					 * it up and try again.
					 */
					if (try == 1) {
						if (ctx == NULL)
					vmhatstat.vh_smgnoctx.value.ul++;
						else
					vmhatstat.vh_smgcleanctx.value.ul++;
					}

					sunm_smgfree(smg);
					smgrphand = smg;
					goto top;
				}
			} while (smg != smgrphand);

			if (try == 1) {
				/*
				 * We were not able to find a segment that
				 * would not require flushing.
				 *
				 * Flush all user VAC lines and try again.
				 */
				vac_flushallctx();
				vmhatstat.vh_smgflush.value.ul++;
			}

			/*
			 * Give up after 2 tries.
			 */
			if (try >= 3) {
				rm_outofhat();
			}
			try++;
		}
	}
	sunm_clrcleanbit();
	smgrpfree = smg->smg_next;	/* take it off the free list */

	sunm_smglink(smg, as, addr);

	sme = &(smg->smg_sme[(mmu_btop(addr-smg->smg_base)/NPMENTPERPMGRP)]);
	sme->sme_pmg  = pmg;
	sme->sme_valid = 1;
	pmg->pmg_sme = sme;

	mmu_setsmg(smg->smg_base, smg);
}

static void
sunm_smgcheck_keepcnt(struct smgrp *smg)
{
	/*
	 * smgrp_invalid has smg_keepcnt == 1, but it has no pmgs.
	 */
	if (smg == smgrp_invalid)
		return;
	{
		register struct sment *sme;
		register struct pmgrp *pmg;
		int		tcnt;
		int		keepcnt = 0;

		sme = smg->smg_sme;
		for (tcnt = 0; tcnt < NSMENTPERSMGRP; tcnt++) {
			if (sme->sme_valid) {
				pmg = sme->sme_pmg;
				keepcnt += pmg->pmg_keepcnt;
			}
			sme++;
		}

		if (keepcnt != smg->smg_keepcnt) {
			mutex_exit(&sunm_mutex);
			printf(
	"check_smgkeepcnt: keepcnt %d smg_keepcnt %d base 0x%x smg# %d\n",
			    keepcnt, smg->smg_keepcnt,
			    smg->smg_base, smg->smg_num);
			cmn_err(CE_PANIC, "possibly double mapped pmgrp");
		}

	}
}

static void
sunm_smgcheck_keepcntall()
{
	register struct smgrp *smg;

	for (smg = smgrps; smg < smgrpsNSMGRPS; smg++)
		sunm_smgcheck_keepcnt(smg);
}

/*
 * Unmap all HW pmegs (except for locked pmegs) held by an address space.
 */
static void
sunm_unmap_aspmgs(struct as *as)
{
	struct pmgrp	*pmg;
	struct sunm	*sunm;

	ASSERT(MUTEX_HELD(&sunm_mutex));
	/*
	 * Don't do anything if this is kernel address space.
	 */
	if (as == &kas)
		return;

	sunm = (struct sunm *)&as->a_hat->hat_data;
	/*
	 * We don't unmap pmegs if the address space holds only a
	 * "small number" of HW pmegs. We expect that future pmeg allocations
	 * will steal HW pmegs from other address spaces with a clean
	 * or no context.
	 *
	 * If the address space holds less than 'hatunmaplimit' percent
	 * of the total number of HW pmegs we don't unmap. hatunmaplimit
	 * is set to 30% and may be patched.
	 */

	if (sunm->sunm_pmgldcnt * 100 < npmgrpssw * hatunmaplimit)
		return;

	ASSERT(mmu_getctx()->c_as == as);

	for (pmg = sunm->sunm_pmgrps; pmg != NULL; pmg = pmg->pmg_next) {
		if (pmg->pmg_keepcnt == 0)
			continue;
		if (pmg->pmg_mapped)
			continue;
		sunm_pmgunload(pmg, PTESFLAG_UNLOAD);
	}
}

#ifdef	notdef
/* XXX - temporary (and quite dirty) assertion function */
static
assertpmgmapped(pmg, msg)
	struct pmgrp	*pmg;
	char		*msg;
{
	caddr_t		a = pmg->pmg_base;
	struct sunm	*sunm;

	if (pmg == NULL) {
		printf("%s addr 0x%x\n", msg, pmg->pmg_base);
		ASSERT(pmg != NULL);
	}

	sunm = (struct sunm *)&pmg->pmg_as->a_hat->hat_data;
	if (sunm->sunm_ctx == NULL) {
		if (pmg->pmg_mapped) {
			printf("%s addr 0x%x\n", msg, pmg->pmg_base);
			ASSERT(!pmg->pmg_mapped);
		}
		a = SEGTEMP;
	} else {
		if (!pmg->pmg_mapped) {
			printf("%s addr 0x%x\n", msg, pmg->pmg_base);
			ASSERT(pmg->pmg_mapped);
		}
	}

	if (pmg->pmg_num == PMGNUM_SW) {
		printf("%s addr 0x%x\n", msg, pmg->pmg_base);
		ASSERT(pmg->pmg_num != PMGNUM_SW);
	}

	if (map_getsgmap(a) == PMGRP_INVALID) {
		printf("%s addr 0x%x\n", msg, pmg->pmg_base);
		/* Force traceback */
		printf("pmg_num %d, pmg_keepcnt %d, ctx 0x%x\n",
		    pmg->pmg_num, pmg->pmg_keepcnt,
		    (struct ctx *)pmg->pmg_as->a_hat->hat_data[0]);
		ASSERT(map_getsgmap(a) != PMGRP_INVALID);
	}
}
#endif	notdef

/*
 * hmetopmg(hme)
 * return the pmg that contains the given hme
 */
static struct pmgrp *
hmetopmg(register struct hment *hme)
{
	register struct pmgrp	*pmg;
	register struct pmgseg	*pms;
	register int		pmgnum;

	ASSERT(MUTEX_HELD(&sunm_mutex));
	ASSERT(hme != NULL);

	pmg = last_pmg;
	if ((caddr_t)hme > (caddr_t)pmg &&
	    (caddr_t)hme < (caddr_t)pmg + sizeof (struct pmgrp))
		return (pmg);

	/*
	 * search the pmgseg list for the one containg the given hme
	 */
	for (pms = pmgseglist; pms; pms = pms->pms_next) {

		if ((struct pmgrp *)(hme) < pms->pms_base)
			continue;
		else if ((caddr_t)hme < (caddr_t)pms->pms_base +
		    pms->pms_size * sizeof (struct pmgrp)) {
			pmgnum = (struct pmgrp *)(hme) - pms->pms_base;
			pmg = pms->pms_base + pmgnum;
			last_pmg = pmg;
			return (pmg);
		}
	}
	sunm_panic("hmetopmg, hme not in pmg on pmgseglist\n");
	/* NOTREACHED */
}

/*
 * pmgtopmgseg(pmg)
 * return the pmgseg that contains the given pmg
 */
static struct pmgseg *
pmgtopmgseg(register struct pmgrp *pmg)
{
	register struct pmgseg *pms;

	ASSERT(MUTEX_HELD(&sunm_mutex));
	ASSERT(pmg != NULL);

	pms = last_pmgseg;
	if ((pmg >= pms->pms_base) &&
	    (pmg < pms->pms_base + pms->pms_size))
		return (pms);
	/*
	 * search the pmgseg list for the one containg the given pmg
	 */
	for (pms = pmgseglist; pms; pms = pms->pms_next) {
		if ((pmg >= pms->pms_base) &&
		    (pmg < pms->pms_base + pms->pms_size)) {
			last_pmgseg = pms;
			return (pms);
		}
	}
	sunm_panic("pmgtopmgseg, pmg not in pmgseglist\n");
	/* NOTREACHED */
}

/*
 * Allocate initial dynamic data structures for that hat layer,
 * executed only once during boot.
 */
static void
sunm_initdata(void)
{
	struct pmgseg	*pmgseg;
	struct pmgrp	*pmgs;
	int 		nalloc;
	extern int	highbit(ulong);

	/*
	 * The number of SW pmgrps is by default set to map physical
	 * memory 8 times, npmgrpssw is the maximum that the system will
	 * allocate so we start off small and grow as needed.
	 * Any configured size gets rounded up to a multiple of
	 * one half of the size provied by mmu hardware, pmgsegsz.
	 */
	pmgsegsz = npmgrps/2;
	if (npmgrpssw == 0)
		npmgrpssw = 8 * ptob(physmem) / PMGRPSIZE;
	npmgrpssw = roundup(npmgrpssw, pmgsegsz);

	/*
	 * When make an attempt to allocate more memory for pmgrps
	 * we don't bother if the system doesn't at least have
	 * pmgsegsz_pgs free.
	 */
	pmgsegsz_pgs = ((pmgsegsz * sizeof (struct pmgrp)) >> MMU_PAGESHIFT) +
		minfree;
	/*
	 * Number of cached entries for hat_pmgfind.
	 * Round up to the nearest power of two.
	 */
	npmghash = 1 << highbit(npmgrpssw - 1);
	npmghash_offset = npmghash - 1;

	if ((pmghash = (struct pmgrp **)kmem_zalloc(sizeof (struct pmgrp *) *
	    npmghash, KM_NOSLEEP)) == NULL)
		cmn_err(CE_PANIC, "Cannot allocate memory for pmghash");

	/*
	 * Allocate a portion of the possible pmegs needed for this system.
	 * Initially, enough is allocated to cover the hardware mmu twice,
	 * when the pmgrpfree list becomes empty we allocate more.
	 */
	nalloc = npmgrps * 2;
	if ((pmgs = (struct pmgrp *)kmem_zalloc(sizeof (struct pmgrp) *
	    npmgrps * 2, KM_NOSLEEP)) == NULL)
		cmn_err(CE_PANIC, "Cannot make initial allocation of pmgrps");


	if ((pmgseg = (struct pmgseg *)kmem_zalloc(sizeof (struct pmgseg),
	    KM_NOSLEEP)) == NULL)
		cmn_err(CE_PANIC, "Cannot allocate memory for pmegseg");

	pmgrps = pmgs;
	pmgrpsNPMGRPS = pmgs + nalloc;
	pmgrps_allocd = nalloc;
	pmgrps_free = nalloc;
	pmgrps_lowater = nalloc;

	pmgseg->pms_base = pmgs;
	pmgseg->pms_next = NULL;
	pmgseg->pms_size = nalloc;
	pmgseglist = pmgseg;
	last_pmgseg = pmgseg;
}

/*
 * Allocate memory for pgmrps,
 * pmgrps are the major memory consumer of this hat implementation so
 * they are allocated as needed until a maximum, npmgrpsw, is reached.
 * The parmater n is a number of chunks, each one half of the size of the
 * mmu's hardware pmeg array.  The system starts by making the pmeg cache
 * twice the size of the hardware array.  The pmeg cache is grown when
 * the pmeg freelist is exhausted until the maximum is reached.
 */
static void
sunm_allocpmgchunk(void)
{
	struct pmgrp	*pmg;
	struct pmgseg	*pmgseg;
	int 		i;

	if ((npmgrpssw == pmgrps_allocd) || getting_pmgs)
		return;

	getting_pmgs = 1;

	/*
	 * Drop the "sunm_mutex" since we may block and/or be re-entered
	 * while allocating memory.
	 */
	if ((pmgseg = (struct pmgseg *)kmem_zalloc(sizeof (struct pmgseg),
	    KM_NOSLEEP)) == NULL) {
		vmhatstat.vh_pmsallocfail.value.ul++;
		getting_pmgs = 0;
		return;
	}

	if ((pmgseg->pms_base = (struct pmgrp *)kmem_zalloc(
	    sizeof (struct pmgrp) * pmgsegsz, KM_NOSLEEP)) == NULL) {
		getting_pmgs = 0;
		kmem_free(pmgseg, sizeof (struct pmgseg));
		vmhatstat.vh_pmsallocfail.value.ul++;
		return;
	}

	/*
	 * Now, re-acquire the "sunm_mutex".
	 */
	mutex_enter(&sunm_mutex);
	pmgseg->pms_size = pmgsegsz;
	pmgseg->pms_next = pmgseglist;
	pmgseglist = pmgseg;

	/*
	 * Initialize the new pmegs and thread them onto the free list.
	 */
	for (i = 0, pmg = (struct pmgrp *)pmgseg->pms_base;
	    i < pmgsegsz; i++, pmg++) {

		sunm_pmgmapinit(pmg);
		pmg->pmg_num = PMGNUM_SW;
		pmg->pmg_mapped = 0;

		pmg->pmg_next = pmgrpfree;
		pmgrpfree = pmg;
	}
	pmgrps_free += pmgsegsz;
	pmgrps_allocd += pmgsegsz;
	getting_pmgs = 0;
	vmhatstat.vh_pmsalloc.value.ul++;
	mutex_exit(&sunm_mutex);
}

static void
sunm_pmgmapinit(struct pmgrp *pmg)
{
	register int j;

	for (j = 0; j < NPMENTPERPMGRP; j++) {
		pmg->pmg_pte[j] = mmu_pteinvalid;
		pmg->pmg_hme[j].hme_impl = j;
	}
}

/*
 * Check the pmgseg list and free a pmgseg if we find one
 * that has no pmgs locked.  Unload any pmgs that are active
 * and let them fault back into another pmgseg.  The pmgseg
 * holding the kernel is skipped, it is recognized by not being
 * pmgsegsz in size.
 */
static void
sunm_freepmgchunk(void)
{
	register struct pmgseg	*pms;
	register struct pmgrp	*pmg;
	register int		i;
	register int		canfreepmgseg;

	ASSERT(MUTEX_HELD(&sunm_mutex));

	freeing_pmgs = 1;
	for (pms = pmgseglist; pms; pms = pms->pms_next) {

		if (pms->pms_size != pmgsegsz)
			continue;

		canfreepmgseg = 1;
		for (i = 0; i < pmgsegsz; i++) {
			pmg = &pms->pms_base[i];
			if (pmg->pmg_keepcnt) {
				canfreepmgseg = 0;
				break;
			}
		}

		if (canfreepmgseg) {
			struct pmgrp **ppmg;
			struct pmgseg **ppms, *cpms;

			/*
			 * If the pmgs are used, free them.  Let the current
			 * user fault and find another.  NULL the hash
			 * pointers to any allocated pmgs.
			 */
			for (i = 0; i < pmgsegsz; i++) {
				int hashind;

				pmg = &pms->pms_base[i];
				if (pmg->pmg_as != NULL) {
					hashind = hash_asaddr(pmg->pmg_as,
								pmg->pmg_base);
					sunm_pmgfree(pmg);
					if (pmg == pmghash[hashind])
						pmghash[hashind] = NULL;
				}
			}

			/*
			 * Remove the pmgrps from the freelist,
			 * all of the ones we need are now there.
			 */
			pmg = pmgrpfree;
			ppmg = &pmgrpfree;
			do {
				if ((pmg >= pms->pms_base) &&
				    (pmg < (pms->pms_base + pms->pms_size))) {
					*ppmg = pmg->pmg_next;
				} else
					ppmg = &pmg->pmg_next;
			} while ((pmg = pmg->pmg_next) != NULL);

			/*
			 * Remove the pmgseg from the pmgseg list and
			 * fix all the global counters.
			 */
			cpms = pmgseglist;
			ppms = &pmgseglist;
			do {
				if (cpms == pms) {
					*ppms = cpms->pms_next;
					break;
				} else
					ppms = &cpms->pms_next;
			} while ((cpms = cpms->pms_next) != NULL);
			pmgrps_allocd -= pmgsegsz;
			pmgrps_free -= pmgsegsz;
			vmhatstat.vh_pmsfree.value.ul++;
			last_pmg = pmgrphand = pmgrpmin;
			last_pmgseg = pmgseglist;

			/*
			 * Drop the "sunm_mutex" before freeing the pmeg
			 * and re-acquire it before returning.
			 */
			mutex_exit(&sunm_mutex);
			kmem_free(pms->pms_base,
				pms->pms_size * sizeof (struct pmgrp));
			kmem_free(pms, sizeof (struct pmgseg));
			mutex_enter(&sunm_mutex);
			freeing_pmgs = 0;
			return;
		}
	}

	/*
	 * if this attempt at freeing up memory failed,
	 * it is becuase all the pmgsegs had pmgs locked
	 * so we don't try again until we see more pmgrps
	 * being freed.
	 *
	 * XXX - bias pmgs allocated for kas to come out of
	 * the initial pmgseg so that the rest have a higher
	 * chance of being freed.
	 */
	pmgrps_countdown = 2 * pmgsegsz;
	freeing_pmgs = 0;
}

/*
 * Compress the pmegs that the kernel uses to the lowest part of
 * the intial pmgseg of the system, optimizes searches for steals
 * of hwpmgs becuase the kernel pmgs, which are locked, can be skipped.
 */
void
sunm_hwpmgshuffle(void)
{
	register int i, j, lasthwpmgnum;

	mutex_enter(&sunm_mutex);
	lasthwpmgnum = npmgrps - 1;	/* invalid pmg */
	for (i = 0; i < lasthwpmgnum; i++) {
		if (hwpmgs[i].hwp_pmgrp == NULL) {
			for (j = i + 1; j < lasthwpmgnum; j++) {
				if (hwpmgs[j].hwp_pmgrp != NULL) {
					sunm_hwpmgmv(&hwpmgs[j], &hwpmgs[i]);
					break;
				}
			}
			if (j == lasthwpmgnum)
				break;
		}
	}
	hwpmgmin = hwpmghand = &hwpmgs[i];
	hwpmghand = &hwpmgs[i];
	kvm_dup();
	mutex_exit(&sunm_mutex);
}

static void
sunm_hwpmgmv(struct hwpmg *fromhwpmg, struct hwpmg *tohwpmg)
{
	struct hwpmg *hwpmg;
	struct hwpmg **phwpmg;
	struct pmgrp *swpmg;
	int oldhwpmgnum, newhwpmgnum;
	register int i;
	caddr_t addr;

	/* pull "to" hwpmg off of free list */
	hwpmg = hwpmgfree;
	phwpmg = &hwpmgfree;
	do {
		if (hwpmg == tohwpmg)
			*phwpmg = hwpmg->hwp_next;
		else
			phwpmg = &hwpmg->hwp_next;
	} while ((hwpmg = hwpmg->hwp_next) != NULL);

	swpmg = fromhwpmg->hwp_pmgrp;
	oldhwpmgnum = swpmg->pmg_num;
	newhwpmgnum = tohwpmg - hwpmgs;

	/*
	 * some folks don't use the hat layer for all memory
	 * mapping updates so the ptes in those pmgs can get
	 * out of date, resync with the current mapping state.
	 */
	sunm_pmgloadswptes(swpmg->pmg_base, &swpmg->pmg_pte[0]);

	/* load the ptes into the new "to" HW pmg */
	map_setsgmap(SEGTEMP, newhwpmgnum);
	sunm_pmgloadptes(SEGTEMP, swpmg->pmg_pte);
	map_setsgmap(SEGTEMP, PMGRP_INVALID);

	/* remap to use new "to" HW pmg */
	tohwpmg->hwp_pmgrp = swpmg;
	swpmg->pmg_num = newhwpmgnum;

	map_setsgmap(swpmg->pmg_base, newhwpmgnum);

	/* invalidate the ptes in "from" HW pmg */
	map_setsgmap(SEGTEMP, oldhwpmgnum);
	for (i = 0, addr = SEGTEMP; i < NPMENTPERPMGRP; i++, addr += PAGESIZE)
		mmu_setpte(addr, mmu_pteinvalid);
	map_setsgmap(SEGTEMP, PMGRP_INVALID);

	/* put old hwpmg on freelist */
	fromhwpmg->hwp_next = hwpmgfree;
	fromhwpmg->hwp_pmgrp = NULL;
	hwpmgfree = fromhwpmg;
}

/*
 * Common code to drop the sunm_mutex when panicing.
 */
static void
sunm_panic(char *str)
{
	mutex_exit(&sunm_mutex);
	cmn_err(CE_PANIC, str);
}


/*
 * Locking primitves accessed by HATLOCK macros
 */

/*ARGSUSED*/
static void
sunm_page_enter(struct page *pp)
{
	mutex_enter(&sunm_mutex);
}

/*ARGSUSED*/
static void
sunm_page_exit(struct page *pp)
{
	mutex_exit(&sunm_mutex);
}

/*ARGSUSED*/
static void
sunm_hat_enter(struct as *as)
{
	mutex_enter(&sunm_mutex);
}

/*ARGSUSED*/
static void
sunm_hat_exit(struct as *as)
{
	mutex_exit(&sunm_mutex);
}

/*ARGSUSED*/
static void
sunm_mlist_enter(struct page *pp)
{
	mutex_enter(&sunm_mutex);
}

/* ARGSUSED */
static void
sunm_mlist_exit(pp)
	struct page *pp;
{
	mutex_exit(&sunm_mutex);
}

/* ARGSUSED */
static int
sunm_mlist_held(pp)
	struct page *pp;
{
	return (MUTEX_HELD(&sunm_mutex));
}

static void
sunm_cachectl_enter(pp)
	struct page *pp;
{

#ifdef  lint
	pp = pp;
#endif
	mutex_enter(&sunm_mutex);
}

static void
sunm_cachectl_exit(pp)
	struct page *pp;
{

#ifdef  lint
	pp = pp;
#endif
	mutex_exit(&sunm_mutex);
}

/*
 * Added for ISM
 */

/*ARGSUSED*/
int
sptcreate(u_int size, struct as **pas, struct anon_map *amp)
{
	return (EINVAL);
}

/*ARGSUSED*/
static int
sunm_share(struct as *as, caddr_t addr, struct as *sptas, caddr_t sptaddr,
    u_int size)
{
	return (0);
}

/*ARGSUSED*/
void
sunm_unshare(struct as *as, caddr_t addr, u_int size)
{}

/*ARGSUSED*/
int
segspt_shmattach(struct seg *seg, caddr_t *argsp)
{
	return (0);
}

#define	SUN4C_HW_PAGESIZE 1
int
num_hw_pagesizes()
{
	return (SUN4C_HW_PAGESIZE);
}
/*ARGSUSED*/
u_int
get_hw_pagesize(n)
	u_int n;
{
	if (n == 0)
		return (PAGESIZE);
	else
		return (0);
}

/*ARGSUSED*/
void
sptdestroy(struct as *as, struct anon_map *amp)
{}
