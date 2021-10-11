/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */
#ident	"@(#)mmu.c	1.15	93/11/11 SMI"

/*
 * VM - Sun-4c low-level routines.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/vm.h>
#include <sys/mman.h>
#include <sys/map.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

#include <vm/as.h>

#include <vm/page.h>
#include <vm/seg.h>

#include <sys/cpu.h>
#include <sys/pte.h>
#include <sys/mmu.h>
#include <sys/stack.h>
#include <vm/seg_kmem.h>

#include <vm/hat_sunm.h>

extern	struct ctx *ctxs;
extern	struct pmgrp *pmgrps;
extern	struct hwpmg *hwpmgs;

extern	u_int map_getpgmap();
extern	u_int map_getsgmap();
extern	u_int map_getctx();

struct	pte mmu_pteinvalid = { 0 };

static	void mmu_setkpmg(/* base, pmgnum */);

struct ctx *
mmu_getctx()
{
	return (&ctxs[map_getctx()]);
}

void
mmu_setctx(ctx)
	struct ctx *ctx;
{
	/*
	 * We must make sure that there are no user windows
	 * in the register file when we switch contexts.
	 * Otherwise the flushed windows will go to the
	 * wrong place.
	 */
	flush_user_windows();
	map_setctx(ctx->c_num);
}

void
mmu_setpmg(base, pmg)
	caddr_t base;
	struct pmgrp *pmg;
{
	if (pmg->pmg_num == PMGNUM_SW)
		cmn_err(CE_PANIC, "pmg_num == PMGNUM_SW");

	if (base >= (caddr_t)KERNELBASE)
		mmu_setkpmg(base, pmg->pmg_num);
	else
		map_setsgmap(base, pmg->pmg_num);
}

void
mmu_settpmg(base, pmg)
	caddr_t base;
	struct pmgrp *pmg;
{
	map_setsgmap(base, pmg->pmg_num);
}

struct pmgrp *
mmu_getpmg(base)
	caddr_t base;
{
	struct pmgrp	*retval;

	retval = hwpmgs[map_getsgmap(base)].hwp_pmgrp;

	if (retval == NULL)
		panic("mmu_getpmg");

	return (retval);
}

void
mmu_setpte(base, pte)
	caddr_t base;
	struct pte pte;
{
	ASSERT(mmu_getpmg(base) != pmgrp_invalid);

	/*
	 * Make sure user windows are flushed before
	 * possibly taking a mapping away.
	 */
	if (!pte_valid(&pte))
		flush_user_windows();
	map_setpgmap(base, *(u_int *)&pte);
}

void
mmu_getpte(base, ppte)
	caddr_t base;
	struct pte *ppte;
{
	*(u_int *)ppte = map_getpgmap(base);
}

void
mmu_getkpte(base, ppte)
	caddr_t base;
	struct pte *ppte;
{

	*(u_int *)ppte = map_getpgmap(base);
}

void
mmu_pmginval(base)
	caddr_t base;
{

	/*
	 * Make sure user windows are flushed before
	 * possibly taking a mapping away.
	 */
	flush_user_windows();
	if (base >= (caddr_t)KERNELBASE)
		mmu_setkpmg(base, PMGRP_INVALID);
	else
		map_setsgmap(base, PMGRP_INVALID);
}

/*
 * Copy pmgnum to all contexts.
 * Keeps kernel up-to-date in all contexts.
 */
static void
mmu_setkpmg(base, pmgnum)
	caddr_t base;
	u_int pmgnum;
{
	register u_int c;
	register u_int my_c;
	register u_int ommuctx;

	/* ASSERT(MUTEX_HELD(&hat_mutex)); startup code doesn't obey locking */

	if (base < (caddr_t)KERNELBASE)
		panic("mmu_setkpmg");

	flush_user_windows();		/* flush before changing ctx */

	my_c = map_getctx();
	ommuctx = curthread->t_mmuctx;
	for (c = 0; c < NCTXS; c++) {
		curthread->t_mmuctx = 0x100 | c;
		map_setctx(c);
		map_setsgmap(base, pmgnum);
	}
	curthread->t_mmuctx = ommuctx;
	map_setctx(my_c);
}

#ifdef VAC
/*
 * Flush the entire VAC.  Flush all user contexts and flush all valid
 * kernel segments.
 */
void
vac_flushall()
{
	register u_int c;
	register u_int my_c;
	register caddr_t v;
	register u_int ommuctx;

	/*
	 * Do nothing if no VAC active.
	 */
	if (vac) {
		if (vac & VAC_WRITETHRU) {
			/*
			 * This is a write-through VAC (sun4c and 4/330),
			 * just a vac_tagsinit will do.
			 */
			vac_tagsinit();
			return;
		}
		/*
		 * Save starting context, step over all contexts,
		 * flushing them.
		 */
		flush_user_windows();		/* flush before changing ctx */

		mutex_enter(&sunm_mutex);
		ommuctx = curthread->t_mmuctx;
		my_c = map_getctx();

#ifdef MMU_3LEVEL
		if (mmu_3level)
			vac_usrflush();
		else
#endif MMU_3LEVEL
		{
			for (c = 0; c < NCTXS; c++) {
				curthread->t_mmuctx = 0x100 | c;
				map_setctx(c);
				vac_ctxflush();
			}
		}

		/*
		 * For all valid kernel pmgrps, flush them.
		 */
#ifdef MMU_3LEVEL
		if (mmu_3level) {
			for (v = (caddr_t)KERNELBASE; v != NULL; v += SMGRPSIZE)
				if (mmu_getsmg(v) != smgrp_invalid)
					vac_rgnflush(v);
		} else
#endif MMU_3LEVEL
		{
			for (v = (caddr_t)KERNELBASE; v != NULL; v += PMGRPSIZE)
				if (mmu_getpmg(v) != pmgrp_invalid)
					vac_segflush(v);
		}

		/*
		 * Restore starting context.
		 */
		curthread->t_mmuctx = ommuctx;
		map_setctx(my_c);
		mutex_exit(&sunm_mutex);
	}
}
#endif VAC
