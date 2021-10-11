/*
 * Copyright (c) 1991, 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)sfmmu_asm.s	1.73	95/09/13 SMI"

/*
 * SFMMU primitives.  These primitives should only be used by sfmmu
 * routines.
 */

#if defined(lint)
#include <sys/types.h>
#else	/* lint */
#include "assym.s"
#endif	/* lint */

#include <sys/asm_linkage.h>
#include <sys/machtrap.h>
#include <sys/spitasi.h>
#include <sys/pte.h>
#include <sys/mmu.h>
#include <vm/hat_sfmmu.h>
#include <sys/machparam.h>
#include <sys/privregs.h>
#include <sys/scb.h>

#include <sys/spitregs.h>

#ifdef TRAPTRACE
#include <sys/traptrace.h>
#endif /* TRAPTRACE */

/*
 * Spitfire MMU Operators
 */
#if defined (lint)

/* ARGSUSED */
void
sfmmu_tlbflush_page(caddr_t vaddr, int ctxnum)
{
}

/* ARGSUSED */
void
sfmmu_tlbflush_ctx(int ctxnum)
{
}

/* ARGSUSED */
void
sfmmu_tlbflush_page_tl1(caddr_t vaddr, int ctxnum)
{
}

/* ARGSUSED */
void
sfmmu_tlbflush_ctx_tl1(int ctxnum)
{
}

/* ARGSUSED */
void
sfmmu_tlbcache_flushpage_tl1(caddr_t vaddr, int ctxnum, uint pfnum)
{
}

void
sfmmu_xcall_sync_tl1(void)
{
}

/* ARGSUSED */
void
sfmmu_itlb_ld(caddr_t vaddr, int ctxnum, tte_t *tte)
{}

/* ARGSUSED */
void
sfmmu_dtlb_ld(caddr_t vaddr, int ctxnum, tte_t *tte)
{}

/*
 * Use cas, if tte has changed underneath us then reread and try again.
 * In the case of a retry, it will update sttep with the new original.
 */
/* ARGSUSED */
int
sfmmu_modifytte(tte_t *sttep, tte_t *stmodttep, tte_t *dttep)
{ return(0); }

/*
 * Use cas, if tte has changed underneath us then return 1, else return 0
 */
/* ARGSUSED */
int
sfmmu_modifytte_try(tte_t *sttep, tte_t *stmodttep, tte_t *dttep)
{ return(0); }

/* ARGSUSED */
void
sfmmu_copytte(tte_t *sttep, tte_t *dttep)
{}

int
sfmmu_getctx_pri()
{ return(0); }

int
sfmmu_getctx_sec()
{ return(0); }

/* ARGSUSED */
void
sfmmu_setctx_pri(int ctx)
{}

/* ARGSUSED */
void
sfmmu_setctx_sec(int ctx)
{}

/* ARGSUSED */
u_int
sfmmu_ctx_steal_tl1(int sctx, int rctx)
{ return(0); }

/*
 * Supports only 32 bit virtual addresses
 */
uint
sfmmu_get_dsfar()
{ return(0); }

uint
sfmmu_get_isfsr()
{ return(0); }

uint
sfmmu_get_dsfsr()
{ return(0); }
#else	/* lint */

	ENTRY_NP(sfmmu_tlbflush_page)
	/*
	 * %o0 = vaddr
	 * %o1 = ctxnum
	 */
	sethi	%hi(FLUSH_ADDR), %o3
	brnz,a,pt  %o1, 1f			/* KCONTEXT? */
	  rdpr	%pstate, %o2
	/*
	 * For KCONTEXT demaps use primary. type = page implicitly
	 */
	stxa	%g0, [%o0]ASI_DTLB_DEMAP	/* dmmu flush for KCONTEXT */
	stxa	%g0, [%o0]ASI_ITLB_DEMAP	/* immu flush for KCONTEXT */
	retl
	  flush	%o3
1:
	/*
	 * User demap.  We need to set the secondary context properly.
	 * %o0 = vaddr
	 * %o1 = ctxnum
	 * %o3 = FLUSH_ADDR
	 * We disable interrupts to prevent the secondary ctx register changing
	 * underneath us.
	 */
#ifdef DEBUG
	andcc	%o2, PSTATE_IE, %g0		/* if interrupts already */
	bnz,a,pt %icc, 3f			/* disabled, panic	 */
	  nop
	sethi	%hi(hat_panic7), %o0
	or	%o0, %lo(hat_panic7), %o0
	call	panic
	  ld	[%o0], %o0
3:
#endif /* DEBUG */

	wrpr	%o2, PSTATE_IE, %pstate		/* disable interrupts */
	set	MMU_SCONTEXT, %o4
	ldxa	[%o4]ASI_DMMU, %o5		/* rd old ctxnum */
	or	DEMAP_SECOND | DEMAP_PAGE_TYPE, %o0, %o0
	cmp	%o5, %o1
	be,a,pt	%icc, 4f
	  nop
	stxa	%o1, [%o4]ASI_DMMU		/* wr new ctxum */
4:
	stxa	%g0, [%o0]ASI_DTLB_DEMAP
	stxa	%g0, [%o0]ASI_ITLB_DEMAP
	flush	%o3
	be,a,pt	%icc, 5f
	  nop
	stxa	%o5, [%o4]ASI_DMMU		/* restore old ctxnum */
	flush	%o3
5:
	retl
	  wrpr	%g0, %o2, %pstate		/* enable interrupts */
	SET_SIZE(sfmmu_tlbflush_page)

	ENTRY_NP(sfmmu_tlbflush_ctx)
	/*
	 * %o0 = ctxnum
	 * We disable interrupts to prevent the secondary ctx register changing
	 * underneath us.
	 */
	sethi	%hi(FLUSH_ADDR), %o3
	set	DEMAP_CTX_TYPE | DEMAP_SECOND, %g1
	rdpr	%pstate, %o2

#ifdef DEBUG
	andcc	%o2, PSTATE_IE, %g0		/* if interrupts already */
	bnz,a,pt %icc, 1f			/* disabled, panic	 */
	  nop
	sethi	%hi(hat_panic7), %o0
	or	%o0, %lo(hat_panic7), %o0
	call	panic
	  ld	[%o0], %o0
1:
#endif /* DEBUG */

	wrpr	%o2, PSTATE_IE, %pstate		/* disable interrupts */
	set	MMU_SCONTEXT, %o4
	ldxa	[%o4]ASI_DMMU, %o5		/* rd old ctxnum */
	cmp	%o5, %o0
	be,a,pt	%icc, 4f
	  nop
	stxa	%o0, [%o4]ASI_DMMU		/* wr new ctxum */
4:
	stxa	%g0, [%g1]ASI_DTLB_DEMAP
	stxa	%g0, [%g1]ASI_ITLB_DEMAP
	flush	%o3
	be,a,pt	%icc, 5f
	  nop
	stxa	%o5, [%o4]ASI_DMMU		/* restore old ctxnum */
	flush	%o3
5:
	retl
	  wrpr	%g0, %o2, %pstate		/* enable interrupts */
	SET_SIZE(sfmmu_tlbflush_ctx)

	ENTRY_NP(sfmmu_tlbflush_page_tl1)
	/*
	 * %g1 = vaddr
	 * %g2 = ctxnum
	 *
	 * NOTE: any changes to this code probably need to be reflected in
	 * sf_dtlbcache_flushpage_tl1.
	 *
	 *  We need to set the secondary context properly.
	 */
	set	MMU_SCONTEXT, %g4
	ldxa	[%g4]ASI_DMMU, %g5		/* rd old ctxnum */
	or	DEMAP_SECOND | DEMAP_PAGE_TYPE, %g1, %g1
	stxa	%g2, [%g4]ASI_DMMU		/* wr new ctxum */
	stxa	%g0, [%g1]ASI_DTLB_DEMAP
	stxa	%g0, [%g1]ASI_ITLB_DEMAP
	stxa	%g5, [%g4]ASI_DMMU		/* restore old ctxnum */
	retry
	membar #Sync
	SET_SIZE(sfmmu_tlbflush_page_tl1)

	ENTRY_NP(sfmmu_tlbflush_ctx_tl1)
	/*
	 * %g1 = ctxnum
	 */
	set	DEMAP_CTX_TYPE | DEMAP_SECOND, %g4
	set	MMU_SCONTEXT, %g3
	ldxa	[%g3]ASI_DMMU, %g5		/* rd old ctxnum */
	stxa	%g1, [%g3]ASI_DMMU		/* wr new ctxum */
	stxa	%g0, [%g4]ASI_DTLB_DEMAP
	stxa	%g0, [%g4]ASI_ITLB_DEMAP
	stxa	%g5, [%g3]ASI_DMMU		/* restore old ctxnum */
	retry
	membar #Sync
	SET_SIZE(sfmmu_tlbflush_ctx_tl1)

/*
 * sfmmu_dtlbcache_flushpage_tl1
 *	Flushes a page from the tlb and the dcache at tl1.
 *
 *	This routine is the sf_dcache_flushpage and
 *	sfmmu_tlbflush_page routines merged together and written to
 *	execute at tl>0.  It is the cross-trap client when we need
 *	to flush the cache and the tlb.  It assumes it has all global
 *	registers at its disposal.
 */
	ENTRY_NP(sfmmu_tlbcache_flushpage_tl1)
	/*
	 * %g1 = vaddr, %g2 = ctxnum, %g3 = pfnum
	 *
	 * Flush the tlb first.  The only reason to flush the tlb first is to
	 * free up regs.
	 * We need to set the secondary context properly.
	 */
	or	%g0, MMU_SCONTEXT, %g4
	sethi	%hi(FLUSH_ADDR), %g6		/* flush addr doesn't matter */
	ldxa	[%g4]ASI_DMMU, %g5		/* rd old ctxnum */
	or	DEMAP_SECOND | DEMAP_PAGE_TYPE, %g1, %g1
	stxa	%g2, [%g4]ASI_DMMU		/* wr new ctxum */
	stxa	%g0, [%g1]ASI_DTLB_DEMAP
	stxa	%g0, [%g1]ASI_ITLB_DEMAP
	stxa	%g5, [%g4]ASI_DMMU		/* restore old ctxnum */
	flush	%g6
	/*
	 * Flush the page from the dcache
	 * %g1 = vaddr, %g3 = pfnum.
	 */
	srlx	%g1, MMU_PAGESHIFT, %g2
	and	%g2, 0x1, %g2			/* g2 = virtual color */
	DCACHE_FLUSH(%g3, %g2, %g1, %g4, %g5)
	retry
	nop
	SET_SIZE(sfmmu_tlbcache_flushpage_tl1)



/*
 *	This dummy tl1 function is there to ensure that previously called
 *	xtrap handlers have exececuted. The hardware (mondo dispatch mechanism)
 *	is such that return from xtrap doesn't guarantee execution of xtrap
 *	handler. So, callers can call this xtrap-handler to ensure that
 *	the previous one is complete. This is because the hardware only
 *	can handle 1 mondo at a time - when this mondo is handled, we are
 *	sure that the mondo for the previous xtrap must have been handled.
*/
	ENTRY_NP(sfmmu_xcall_sync_tl1)
	retry
	SET_SIZE(sfmmu_xcall_sync_tl1)


	ENTRY_NP(sfmmu_itlb_ld)
	rdpr	%pstate, %o3
#ifdef DEBUG
	andcc	%o3, PSTATE_IE, %g0		/* if interrupts already */
	bnz,a,pt %icc, 1f			/* disabled, panic	 */
	  nop
	sethi	%hi(hat_panic7), %o0
	or	%o0, %lo(hat_panic7), %o0
	call	panic
	  ld	[%o0], %o0
1:
#endif /* DEBUG */
	wrpr	%o3, PSTATE_IE, %pstate		/* disable interrupts */

	srl	%o0, MMU_PAGESHIFT, %o0
	sll	%o0, MMU_PAGESHIFT, %o0		/* clear page offset */
	or	%o0, %o1, %o0
	ldx	[%o2], %g1
	set	MMU_TAG_ACCESS, %o5
	stxa	%o0,[%o5]ASI_IMMU
	stxa	%g1,[%g0]ASI_ITLB_IN
	sethi	%hi(FLUSH_ADDR), %o1		/* flush addr doesn't matter */
	flush	%o1				/* flush required for immu */
	retl
	  wrpr	%g0, %o3, %pstate		/* enable interrupts */
	SET_SIZE(sfmmu_itlb_ld)

	ENTRY_NP(sfmmu_dtlb_ld)
	rdpr	%pstate, %o3
#ifdef DEBUG
	andcc	%o3, PSTATE_IE, %g0		/* if interrupts already */
	bnz,a,pt %icc, 1f			/* disabled, panic	 */
	  nop
	sethi	%hi(hat_panic7), %o0
	or	%o0, %lo(hat_panic7), %o0
	call	panic
	  ld	[%o0], %o0
1:
#endif /* DEBUG */
	wrpr	%o3, PSTATE_IE, %pstate		/* disable interrupts */

	srl	%o0, MMU_PAGESHIFT, %o0
	sll	%o0, MMU_PAGESHIFT, %o0		/* clear page offset */
	or	%o0, %o1, %o0
	ldx	[%o2], %g1
	set	MMU_TAG_ACCESS, %o5
	stxa	%o0,[%o5]ASI_DMMU
	stxa	%g1,[%g0]ASI_DTLB_IN
	membar	#Sync
	retl
	  wrpr	%g0, %o3, %pstate		/* enable interrupts */
	SET_SIZE(sfmmu_dtlb_ld)

	ENTRY_NP(sfmmu_modifytte)
	ldx	[%o2], %g3			/* current */
	ldx	[%o0], %g1			/* original */
2:
	ldx	[%o1], %g2			/* modified */
	cmp	%g2, %g3			/* is modified = current? */
	be,a,pt	%xcc,1f				/* yes, don't write */
	  stx	%g3, [%o0]			/* update new original */
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	casx	[%o2], %g1, %g2
	cmp	%g1, %g2
	be,a,pt	%xcc, 1f			/* cas succeeded - return */
	  nop
	ldx	[%o2], %g3			/* new current */
	stx	%g3, [%o0]			/* save as new original */
	ba,pt	%xcc, 2b
	  mov	%g3, %g1
1:	retl
	nop
	SET_SIZE(sfmmu_modifytte)

	ENTRY_NP(sfmmu_modifytte_try)
	ldx	[%o1], %g2			/* modified */
	ldx	[%o2], %g3			/* current */
	ldx	[%o0], %g1			/* original */
	cmp	%g2, %g3			/* is modified = current? */
	be,pt	%xcc,1f				/* yes, don't write */
	  clr	%o0
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	casx	[%o2], %g1, %g2
	cmp	%g1, %g2
	movne	%xcc, 1, %o0
1:	retl
	nop
	SET_SIZE(sfmmu_modifytte_try)

	ENTRY_NP(sfmmu_copytte)
	ldx	[%o0], %g1
	retl
	stx	%g1, [%o1]
	SET_SIZE(sfmmu_copytte)

	ENTRY_NP(sfmmu_getctx_pri)
	set	MMU_PCONTEXT, %o0
#ifdef	ERRATA6_LDXA_WORKS
	retl
	ldxa	[%o0]ASI_DMMU, %o0
#else
	ldxa	[%o0]ASI_DMMU, %o0
	or	%o0, %g0, %g0
	retl
	  nop
#endif	/* ERRATA6_LDXA_WORKS */
	SET_SIZE(sfmmu_getctx_pri)

	ENTRY_NP(sfmmu_getctx_sec)
	set	MMU_SCONTEXT, %o0
#ifdef	ERRATA6_LDXA_WORKS
	retl
	ldxa	[%o0]ASI_DMMU, %o0
#else
	ldxa	[%o0]ASI_DMMU, %o0
	or	%o0, %g0, %g0
	retl
	  nop
#endif
	SET_SIZE(sfmmu_getctx_sec)

	ENTRY_NP(sfmmu_setctx_pri)
	set	MMU_PCONTEXT, %o1
	stxa	%o0, [%o1]ASI_DMMU
	sethi	%hi(FLUSH_ADDR), %o1		/* flush addr doesn't matter */
	retl
	  flush	%o1				/* flush required for immu */
	SET_SIZE(sfmmu_setctx_pri)

	ENTRY_NP(sfmmu_setctx_sec)
	set	MMU_SCONTEXT, %o1
	ldxa	[%o1]ASI_DMMU, %o2
	cmp	%o2, %o0
	be,pt	%icc, 1f
	  sethi	%hi(FLUSH_ADDR), %o3		/* flush addr doesn't matter */
	stxa	%o0, [%o1]ASI_DMMU
	flush	%o3				/* flush required for immu */
1:
	retl
	nop
	SET_SIZE(sfmmu_setctx_sec)

/*
 * 1. First flush all TLB entries whose ctx is ctx-being-stolen.
 * 2. If processor is running in the ctx-being-stolen, set the
 *    context to the resv context. That is 
 *    If processor in User-mode - pri/sec-ctx both set to ctx-being-stolen,
 *		change both pri/sec-ctx registers to resv ctx.
 *    If processor in Kernel-mode - pri-ctx is 0, sec-ctx is ctx-being-stolen,
 *		just change sec-ctx register to resv ctx. When it returns to
 *		kenel-mode, user_rtt will change pri-ctx.
 */
	ENTRY(sfmmu_ctx_steal_tl1)
	/*
	 * %g1 = ctx being stolen
	 * %g2 = new resv ctx
	 */
	set	DEMAP_CTX_TYPE | DEMAP_SECOND, %g4
	set	MMU_SCONTEXT, %g3
	ldxa	[%g3]ASI_DMMU, %g5		/* rd sec ctxnum */
	cmp	%g5, %g1
	be,a,pn %icc, 0f
	nop
	stxa	%g1, [%g3]ASI_DMMU		/* wr ctx being stolen */
0:
	stxa	%g0, [%g4]ASI_DTLB_DEMAP
	stxa	%g0, [%g4]ASI_ITLB_DEMAP	/* flush TLB */
	sethi	%hi(FLUSH_ADDR), %g4
	flush	%g4

	!
	! if (old sec-ctxnum == ctx being stolen) {
	!	write resv ctx to sec ctx-reg
	!	if (pri-ctx == ctx being stolen)
	!		write resv ctx to pri ctx-reg
	! } else
	!	restore old ctxnum
	!
	be,a,pn %icc, 1f
	nop
	stxa	%g5, [%g3]ASI_DMMU		/* restore old ctxnum */
	retry					/* and return */
	membar #Sync
1:
	stxa	%g2, [%g3]ASI_DMMU		/* wr resv ctxnum to sec-reg */
	set	MMU_PCONTEXT, %g3
	ldxa	[%g3]ASI_DMMU, %g4		/* rd pri ctxnum */
	cmp	%g1, %g4
	bne,a 	%icc, 2f
	nop
	stxa	%g2, [%g3]ASI_DMMU		/* wr resv ctxnum to pri-reg */
2:
	retry
	membar #Sync
	SET_SIZE(sfmmu_ctx_steal_tl1)


	ENTRY_NP(sfmmu_get_dsfar)
	set	MMU_SFAR, %o0
#ifdef	ERRATA6_LDXA_WORKS
	retl
	ldxa	[%o0]ASI_DMMU, %o0
#else
	ldxa	[%o0]ASI_DMMU, %o0
	or	%o0, %g0, %g0
	retl
	  nop
#endif /* ERRATA6_LDXA_WORKS */
	SET_SIZE(sfmmu_get_dsfar)

	ENTRY_NP(sfmmu_get_isfsr)
	set	MMU_SFSR, %o0
#ifdef	ERRATA6_LDXA_WORKS
	retl
	ldxa	[%o0]ASI_IMMU, %o0
#else
	ldxa	[%o0]ASI_IMMU, %o0
	or	%o0, %g0, %g0
	retl
	  nop
#endif /* ERRATA6_LDXA_WORKS */
	SET_SIZE(sfmmu_get_isfsr)

	ENTRY_NP(sfmmu_get_dsfsr)
	set	MMU_SFSR, %o0
#ifdef	ERRATA6_LDXA_WORKS
	retl
	ldxa	[%o0]ASI_DMMU, %o0
#else
	ldxa	[%o0]ASI_DMMU, %o0
	or	%o0, %g0, %g0
	retl
	  nop
#endif /* ERRATA6_LDXA_WORKS */
	SET_SIZE(sfmmu_get_dsfsr)

#endif /* lint */

/*
 * Other sfmmu primitives
 */


#if defined (lint)
/*
 * The itlb_rd_entry and dtlb_rd_entry functions return the tag portion of the
 * tte, the virtual address, and the ctxnum of the specified tlb entry.  They
 * should only be used in places where you have no choice but to look at the
 * tlb itself.
 */
/* ARGSUSED */
void
itlb_rd_entry(u_int entry, tte_t *tte, caddr_t *addr, int *ctxnum)
{}

/* ARGSUSED */
void
dtlb_rd_entry(u_int entry, tte_t *tte, caddr_t *addr, int *ctxnum)
{}
#else 	/* lint */
	ENTRY_NP(itlb_rd_entry)
	sllx	%o0, 3, %o0
	ldxa	[%o0]ASI_ITLB_ACCESS, %g1
	stx	%g1, [%o1]
	ldxa	[%o0]ASI_ITLB_TAGREAD, %g2
	set	TAGREAD_CTX_MASK, %o4
	and	%g2, %o4, %o5
	st	%o5, [%o3]
	andn	%g2, %o4, %o5
	retl
	  st	%o5, [%o2]
	SET_SIZE(itlb_rd_entry)

	ENTRY_NP(dtlb_rd_entry)
	sllx	%o0, 3, %o0
	ldxa	[%o0]ASI_DTLB_ACCESS, %g1
	stx	%g1, [%o1]
	ldxa	[%o0]ASI_DTLB_TAGREAD, %g2
	set	TAGREAD_CTX_MASK, %o4
	and	%g2, %o4, %o5
	st	%o5, [%o3]
	andn	%g2, %o4, %o5
	retl
	  st	%o5, [%o2]
	SET_SIZE(dtlb_rd_entry)
#endif /* lint */


#if defined (lint)
/* ARGSUSED */
void mmu_set_itsb(caddr_t tsbbase, uint split, uint size)
{
}

/* ARGSUSED */
void mmu_set_dtsb(caddr_t tsbbase, uint split, uint size)
{
}

/*
 * This routine will have to change if we change TSB.
 * It currently is written for one global TSB accessed in
 * virtual space.  Since we use only one cas to do the invalidate
 * it is not necessary to disable interrupts in this routine.
 * For servers, we probably want a per processor TSB accessed
 * in physical space.
 */
/* ARGSUSED */
void mmu_invalidate_tsbe(struct tsbe *tsbe, union tsb_tag *tagp)
{
}

/* ARGSUSED */
void mmu_update_tsbe(struct tsbe *tsbe, tte_t *tte, union tsb_tag *tagp)
{
}

/* ARGSUSED */
void mmu_unload_tsbctx(uint ctx)
{
}

#else /* lint */

	ENTRY_NP(mmu_set_itsb)
	srlx	%o0, TSBBASE_SHIFT, %o0
	sllx	%o0, TSBBASE_SHIFT, %o0		/* clear 12 lsb */
	sll	%o1, TSBSPLIT_SHIFT, %o1
	or	%o0, %o1, %g1
	or	%g1, %o2, %g1
	set	MMU_TSB, %o3
	stxa    %g1, [%o3]ASI_IMMU
	sethi	%hi(FLUSH_ADDR), %o1		/* flush addr doesn't matter */
	retl
	  flush	%o1				/* flush required by immu */
	SET_SIZE(mmu_set_itsb)

	ENTRY_NP(mmu_set_dtsb)
	srlx	%o0, TSBBASE_SHIFT, %o0
	sllx	%o0, TSBBASE_SHIFT, %o0		/* clear 12 lsb */
	sll	%o1, TSBSPLIT_SHIFT, %o1
	or	%o0, %o1, %g1
	or	%g1, %o2, %g1
	set	MMU_TSB, %o3
	stxa    %g1, [%o3]ASI_DMMU
	retl
	membar	#Sync
	SET_SIZE(mmu_set_dtsb)

	ENTRY_NP(mmu_invalidate_tsbe)
	add	%o0, TSBE_TAG + TSBTAG_INTHI, %o3
	add	%o0, TSBE_TAG, %o5
	sethi	%hi(TSBTAG_LOCKED), %o2
	ld	[%o3], %o4
	ldx	[%o1], %g1			/* read tsb tag */
1:
	cmp	%o4, %o2
	be,a,pn	%icc, 1b			/* if locked spin */
	  ld	[%o3], %o4
	ldx	[%o5], %g2
	cmp	%g2, %g1			/* tsb match? */
	bne,pt	%xcc, 2f			/* nope, exit */
	  sethi	%hi(TSBTAG_INVALID), %o1	/* exec irregardless of br */
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	cas	[%o3], %o4, %o1
	cmp	%o4, %o1
	bne,a,pn %icc, 1b			/* didn't lock/invalidate */
	  ld	[%o3], %o4			/*	so try again */
	/* tsbe lock acquired */
2:
	retl
	membar	#StoreStore
	SET_SIZE(mmu_invalidate_tsbe)

	ENTRY_NP(mmu_update_tsbe)
	add	%o0, TSBE_TAG + TSBTAG_INTHI, %o3
	ldx	[%o1], %g3			/* read tte */
	ldx	[%o2], %g4			/* read tag */
	ld	[%o3], %o4
	/*
	 * disable interrupts
	 */
	rdpr	%pstate, %o2
#ifdef DEBUG
	and	%o2, PSTATE_IE, %o1		/* if interrupts already */
	brnz,a,pt %o1, 3f			/* disabled, panic	 */
	  nop
	sethi	%hi(hat_panic8), %o0
	or	%o0, %lo(hat_panic8), %o0
	call	panic
	  ld	[%o0], %o0
3:
#endif /* DEBUG */
	wrpr	%o2, PSTATE_IE, %pstate		/* disable interrupts */
1:
	sethi	%hi(TSBTAG_LOCKED), %o5		/* in loop in case cas fails */
	cmp	%o4, %o5
	be,a,pn	%icc, 1b			/* if locked spin */
	  ld	[%o3], %o4
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	cas	[%o3], %o4, %o5
	cmp	%o4, %o5
	bne,a,pn %icc, 1b			/* didn't lock so try again */
	  ld	[%o3], %o4
	/* tsbe lock acquired */
	membar	#StoreStore
	stx	%g3, [%o0 + TSBE_TTE]		/* write tte */
	membar	#StoreStore
	stx	%g4, [%o0 + TSBE_TAG]		/* write tag and unlock */
	retl
	  wrpr	%g0, %o2, %pstate		/* enable interrupts */
	SET_SIZE(mmu_update_tsbe)

/*
 * Flush global TSB so that all entries with ctx = ctx-passed are flushed.
 */ 
	ENTRY(mmu_unload_tsbctx)
	!
	! %o0 = ctx to be flushed
	!
	sethi	%hi(tsb_base), %o1
	ld	[%o1 + %lo(tsb_base)], %o1	/* o1 = base of tsb */	
	set	TSB_BYTES, %o2
	add	%o1, %o2, %o2			/* o2 = end of tsb */

	mov	1, %o5
	sll	%o5, TSB_ENTRY_SHIFT, %o5	/* o5 = size of each entry */
	sethi	%hi(TSBTAG_INVALID), %o3	/* o3 = invalid entry */
	add	%o1, TSBE_TAG + TSBTAG_INTHI, %o1

	lduh	[%o1], %o4			/* read tag */
0:
	! preload next entry to make cache hot for next time around
	! this should speed up tsbflush
	ld	[%o1 + %o5], %g0		/* preload next entry */

	cmp	%o4, %o0			/* compare the tags */
	be,a,pn %icc, 1f			/* if ctx matches, invalidate */
	st	%o3, [%o1]			/* invalidate the entry */
1:
	add	%o1, %o5, %o1			/* the next entry */
	cmp	%o1, %o2			/* if not end of TSB go back */
	bl,a,pt	%icc, 0b
	lduh	[%o1], %o4			/* read tag */

	retl
	membar	#StoreStore
	SET_SIZE(mmu_unload_tsbctx)

#endif /* lint */

#if defined (lint)
/* ARGSUSED */
uint sfmmu_ttetopfn(tte_t *tte, caddr_t vaddr)
{ return(0); }

#else /* lint */
#define	TTETOPFN(tte, vaddr, label)					\
	srlx	tte, TTE_SZ_SHFT, %g2					;\
	sllx	tte, TTE_PA_LSHIFT, tte					;\
	andcc	%g2, TTE_SZ_BITS, %g2		/* g2 = ttesz */	;\
	sllx	%g2, 1, %g3						;\
	add	%g3, %g2, %g3			/* mulx 3 */		;\
	add	%g3, MMU_PAGESHIFT + TTE_PA_LSHIFT, %g4			;\
	srlx	tte, %g4, tte						;\
	sllx	tte, %g3, tte						;\
	bz,a,pt	%xcc, label/**/1					;\
	  nop								;\
	set	1, %g2							;\
	add	%g3, MMU_PAGESHIFT, %g4					;\
	sllx	%g2, %g4, %g2						;\
	sub	%g2, 1, %g2		/* g2=TTE_PAGE_OFFSET(ttesz) */	;\
	and	vaddr, %g2, %g3						;\
	srl	%g3, MMU_PAGESHIFT, %g3					;\
	or	tte, %g3, tte						;\
label/**/1:


	ENTRY_NP(sfmmu_ttetopfn)
	ldx	[%o0], %g1			/* read tte */
	TTETOPFN(%g1, %o1, sfmmu_ttetopfn_l1)
	/*
	 * g1 = pfn
	 */
	retl
	mov	%g1, %o0
	SET_SIZE(sfmmu_ttetopfn)

#endif /* !lint */


#if defined (lint)
/*
 * The sfmmu_hblk_hash_add is the assembly primitive for adding hmeblks to the
 * the hash list.
 */
/* ARGSUSED */
void
sfmmu_hblk_hash_add(struct hmehash_bucket *hmebp, struct hme_blk *hmeblkp)
{
}

/*
 * The sfmmu_hblk_hash_rm is the assembly primitive to remove hmeblks from the
 * hash list.
 */
/* ARGSUSED */
void
sfmmu_hblk_hash_rm(struct hmehash_bucket *hmebp, struct hme_blk *hmeblkp,
	struct hme_blk *prev_hblkp)
{
}
#else /* lint */

	.seg	".data"
hblk_add_panic1:
	.ascii	"sfmmu_hblk_hash_add: vatopa returned -1"
	.byte	0
hblk_add_panic2:
	.ascii	"sfmmu_hblk_hash_add: va hmeblkp is NULL but pa is not"
	.byte	0
	.align	4
	.seg	".text"

	ENTRY_NP(sfmmu_hblk_hash_add)
	save	%sp, -SA(MINFRAME), %sp
	/*
	 * %i0 = hmebp
	 * %i1 = hmeblkp
	 */
	set	MMU_PAGEOFFSET, %l2
	call	va_to_pfn
	  mov	%i1, %o0			/* hmeblkp */
	tst	%o0
	bpos,a,pt %icc, 2f
	 ld	[%i0 + HMEBUCK_HBLK], %l1	/* next hmeblk */
	sethi	%hi(hblk_add_panic1), %o0
	call	panic
	  or	%o0, %lo(hblk_add_panic1), %o0
2:
	mov	%o0, %i2
	ldx	[%i0 + HMEBUCK_NEXTPA], %g2	/* g2 = next hblkpa */
#ifdef	DEBUG
	cmp	%l1, %g0
	bne,a,pt %icc, 1f
	 nop
	brz,a,pt %g2, 1f
	 nop
	sethi	%hi(hblk_add_panic2), %o0
	call	panic
	  or	%o0, %lo(hblk_add_panic2), %o0
1:
#endif /* DEBUG */
	and	%i1, %l2, %l3
	sllx	%i2, MMU_PAGESHIFT, %g1
	or	%l3, %g1, %g1			/* g1 = hblk pa */
	st	%l1, [%i1 + HMEBLK_NEXT]	/* update hmeblk's next */
	stx	%g2, [%i1 + HMEBLK_NEXTPA]	/* update hmeblk's next pa */
	membar	#StoreStore
	st	%i1, [%i0 + HMEBUCK_HBLK]	/* update bucket hblk next */
	stx	%g1, [%i0 + HMEBUCK_NEXTPA]	/* add hmeblk to list */
	membar	#MemIssue
	ret
	restore
	SET_SIZE(sfmmu_hblk_hash_add)

	ENTRY_NP(sfmmu_hblk_hash_rm)
	/*
	 * %o0 = hmebp
	 * %o1 = hmeblkp
	 * %o2 = hmeblkp previous
	 */
	rdpr	%pstate, %o5
#ifdef DEBUG
	andcc	%o5, PSTATE_IE, %g0		/* if interrupts already */
	bnz,a,pt %icc, 3f			/* disabled, panic	 */
	  nop
	sethi	%hi(hat_panic8), %o0
	or	%o0, %lo(hat_panic8), %o0
	call	panic
	  ld	[%o0], %o0
3:
#endif /* DEBUG */
	wrpr	%o5, PSTATE_IE, %pstate		/* disable interrupts */

	ldx	[%o1 + HMEBLK_NEXTPA], %g1	/* read next hmeblk pa */
	ld	[%o1 + HMEBLK_NEXT], %o3	/* read next hmeblk va */
	ld	[%o0 + HMEBUCK_HBLK], %o4	/* first hmeblk in list */
	cmp	%o4, %o1
	bne,pt	%icc,1f
	  nop
	/* hmeblk is first on list */
	st	%o3, [%o0 + HMEBUCK_HBLK]
	membar	#StoreStore
	ba,pt	%xcc, 2f
	stx	%g1, [%o0 + HMEBUCK_NEXTPA]
1:
	/* hmeblk is not first on list */
	st	%o3, [%o2 + HMEBLK_NEXT]
	membar	#StoreStore
	stx	%g1, [%o2 + HMEBLK_NEXTPA]
2:
	membar	#MemIssue
	retl
	  wrpr	%g0, %o5, %pstate		/* enable interrupts */
	SET_SIZE(sfmmu_hblk_hash_rm)

#endif /* lint */

	/*
	 * XXX Should we have these stats on a per cpu basis, or
	 * does it matter if the cnt is slightly inaccurate?
	 */
#ifdef DEBUG
#define	HATSTAT_DEBUG(statname, tmp1, tmp2)				\
	sethi	%hi(vmhatstat), tmp1					;\
	add	tmp1, statname, tmp1					;\
	ld	[tmp1 + %lo(vmhatstat)], tmp2				;\
	inc	tmp2							;\
	st	tmp2, [tmp1 + %lo(vmhatstat)]
#else
#define	HATSTAT_DEBUG(statname, tmp1, tmp2)
#endif /* DEBUG */

#if defined (lint)
/*
 * The following routines are jumped to from the mmu trap handlers to do
 * the setting up to call systrap.  They are separate routines instead of 
 * being part of the handlers because the handlers would exceed 32
 * instructions and since this is part of the slow path the jump
 * cost is irrelevant.
 */
void
sfmmu_pagefault()
{
}

void
sfmmu_mmu_trap()
{
}

void
sfmmu_bad_trap()
{
}

void
sfmmu_window_trap()
{
}

#else /* lint */

#define	USE_ALTERNATE_GLOBALS						\
	rdpr	%pstate, %g5						;\
	wrpr	%g5, PSTATE_MG | PSTATE_AG, %pstate


	ENTRY_NP(sfmmu_pagefault)
	USE_ALTERNATE_GLOBALS
	mov	MMU_TAG_ACCESS, %g4
	rdpr	%tt, %g6
	ldxa	[%g4] ASI_DMMU, %g5
	ldxa	[%g4] ASI_IMMU, %g2
	cmp	%g6, FAST_IMMU_MISS_TT
	be,a,pn	%icc, 1f
	  mov	T_INSTR_MMU_MISS, %g3
	mov	%g5, %g2
	cmp	%g6, FAST_DMMU_MISS_TT
	move	%icc, T_DATA_MMU_MISS, %g3	/* arg2 = traptype */
	movne	%icc, T_DATA_PROT, %g3		/* arg2 = traptype */
	/*
	 * If ctx indicates large ttes then let sfmmu_tsb_miss 
	 * perform hash search before faulting.
	 */
	sll	%g5, TAGACC_CTX_SHIFT, %g4
	srl	%g4, TAGACC_CTX_SHIFT, %g4
	sll	%g4, CTX_SZ_SHIFT, %g4
	sethi	%hi(ctxs), %g6
	ld	[%g6 + %lo(ctxs)], %g1
	add	%g4, %g1, %g1
	lduh	[%g1 + C_FLAGS], %g7
	and	%g7, LTTES_FLAG, %g7
	brz,pt	%g7, 1f
	nop
	sethi	%hi(sfmmu_tsb_miss), %g1
	or	%g1, %lo(sfmmu_tsb_miss), %g1
	ba,pt	%xcc, 2f
	nop
1:
	HATSTAT_DEBUG(HATSTAT_PAGEFAULT, %g6, %g4)
	/*
	 * g2 = tag access reg
	 * g3 = type
	 */
	sethi	%hi(trap), %g1
	or	%g1, %lo(trap), %g1
	sllx	%g2, 32, %g5			/* arg4 =  tagaccess */
	or	%g3, %g5, %g2
	clr	%g3				/* arg3 (mmu_fsr)= null */
2:
	ba,pt	%xcc, sys_trap
	  mov	-1, %g4	
	SET_SIZE(sfmmu_pagefault)

	ENTRY_NP(sfmmu_mmu_trap)
	USE_ALTERNATE_GLOBALS
	mov	MMU_TAG_ACCESS, %g4
	rdpr	%tt, %g6
	ldxa	[%g4] ASI_DMMU, %g5
	ldxa	[%g4] ASI_IMMU, %g2
	cmp	%g6, FAST_IMMU_MISS_TT
	be,a,pn	%icc, 1f
	  mov	T_INSTR_MMU_MISS, %g3
	mov	%g5, %g2
	cmp	%g6, FAST_DMMU_MISS_TT
	move	%icc, T_DATA_MMU_MISS, %g3	/* arg2 = traptype */
	movne	%icc, T_DATA_PROT, %g3		/* arg2 = traptype */
1:
	/*
	 * g2 = tag access reg
	 * g3 = type
	 */
	sethi	%hi(sfmmu_tsb_miss), %g1
	or	%g1, %lo(sfmmu_tsb_miss), %g1
	ba,pt	%xcc, sys_trap
	  mov	-1, %g4	
	SET_SIZE(sfmmu_mmu_trap)

	ENTRY_NP(sfmmu_bad_trap)
#ifndef MPSAS
	ba	obp_bpt			/* for now */
	nop
#endif /* MPSAS */
	/* Force contexts to kernel */
	set	MMU_PCONTEXT, %o2
	stxa	%g0, [%o2]ASI_DMMU
	membar	#Sync
	set	MMU_SCONTEXT, %o2
	stxa	%g0, [%o2]ASI_DMMU
	membar	#Sync
	rdpr	%pstate, %o1
	andn	%o1, PSTATE_MG, %o1
	or	%o1, PSTATE_AG, %o1
	wrpr	%o1, %g0, %pstate	/* guarantee use alternate globlas */
	set	sfmmu_panic_stack_end, %sp
	add	%sp, -64, %sp
	stx	%g1, [%sp]
	stx	%g2, [%sp + 0x8]
	stx	%g3, [%sp + 0x10]
	stx	%g4, [%sp + 0x18]
	stx	%g5, [%sp + 0x20]
	stx	%g6, [%sp + 0x28]
	stx	%g7, [%sp + 0x30]
	rdpr	%pstate, %o1
	wrpr	%o1, PSTATE_MG | PSTATE_AG, %pstate /* use mmu globals */
	add	%sp, -64, %sp
	stx	%g1, [%sp]
	stx	%g2, [%sp + 0x8]
	stx	%g3, [%sp + 0x10]
	stx	%g4, [%sp + 0x18]
	stx	%g5, [%sp + 0x20]
	stx	%g6, [%sp + 0x28]
	stx	%g7, [%sp + 0x30]
	add	%sp, -32, %sp
	rdpr	%tstate, %o4
1:
	rdpr	%tl, %g3
	stx	%g3, [%sp]
	rdpr	%tpc, %g2
	stx	%g2, [%sp + 0x8]
	rdpr	%tstate, %g2
	stx	%g2, [%sp + 0x10]
	rdpr	%tt, %g2
	stx	%g2, [%sp + 0x18]
	cmp	%g3, 1
	dec	%g3
	add	%sp, -32, %sp
	bne,a,pt %icc, 1b
	  wrpr	%g3, %tl
	rdpr	%pstate, %o1
	wrpr	%o1, PSTATE_MG | PSTATE_AG, %pstate /* use alt globals */
	and	%o4, TSTATE_CWP, %g6
	set	sfmmu_panic, %g1
	mov	-1, %g4
	ba,pt	%xcc, priv_trap
	wrpr	%g0, %g6, %cwp
	SET_SIZE(sfmmu_bad_trap)

	ENTRY_NP(sfmmu_window_trap)
	/* user miss at tl>1. better be the window handler */
	rdpr	%tl, %g5
	sub	%g5, 1, %g3
	wrpr	%g3, %tl
	rdpr	%tt, %g2
	wrpr	%g5, %tl
	and	%g2, WTRAP_TTMASK, %g4
	cmp	%g4, WTRAP_TYPE	
	bne,a,pn %xcc, 1f
	 nop
	rdpr	%tpc, %g1
	andn	%g1, WTRAP_ALIGN, %g1	/* 128 byte aligned */
	add	%g1, WTRAP_FAULTOFF, %g1
	wrpr	%g0, %g1, %tnpc	
	/*
	 * some wbuf handlers will call systrap to resolve the fault
	 * we pass the trap type so they figure out the correct parameters.
	 * g5 = trap type, g6 = tag access reg
	 * only use g5, g6, g7 registers after we have switched to alternate
	 * globals.
	 */
	USE_ALTERNATE_GLOBALS
	mov	MMU_TAG_ACCESS, %g5
	ldxa	[%g5] ASI_DMMU, %g6
#ifndef	ERRATA6_LDXA_WORKS
	or	%g6, %g0, %g0
#endif	/* ERRATA6_LDXA_WORKS */
	rdpr	%tt, %g7
	cmp	%g7, FAST_IMMU_MISS_TT
	be,pn	%icc, sfmmu_bad_trap
	nop
	cmp	%g7, FAST_DMMU_MISS_TT
	move	%icc, T_DATA_MMU_MISS, %g5
	movne	%icc, T_DATA_PROT, %g5
	done
1:
	ba,a,pt	%xcc, sfmmu_bad_trap
	  nop
	SET_SIZE(sfmmu_window_trap)
#endif /* lint */

#if defined (lint)
/*
 * sfmmu_tsb_miss handlers
 *
 * These routines are responsible for resolving tlb misses once they have also
 * missed in the TSB.  They traverse the hmeblk hash list.  In the
 * case of user address space, it will attempt to grab the hash mutex
 * and in the case it doesn't succeed it will go to tl = 0 to resolve the
 * miss.   In the case of a kernel tlb miss we grab no locks.  This
 * eliminates the problem of recursive deadlocks (taking a tlb miss while
 * holding a hash bucket lock and needing the same lock to resolve it - but
 * it forces us to use a capture cpus when deleting kernel hmeblks).
 * It order to eliminate the possibility of a tlb miss we will traverse
 * the list using physical addresses.  It executes at  TL > 0.
 * NOTE: the following routines currently do not support large page sizes.
 *
 * Parameters:
 *		%g2 = MMU_TARGET register
 *		%g3 = ctx number
 */
void
sfmmu_ktsb_miss()
{
}

void
sfmmu_utsb_miss()
{
}

void
sfmmu_kprot_trap()
{
}

void
sfmmu_uprot_trap()
{
}
#else /* lint */

#if (C_SIZE != (1 << CTX_SZ_SHIFT))
ERROR - size of context struct does not match with CTX_SZ_SHIFT
#endif

/*
 * Copies ism mapping for this ctx in param "ism" if this is a ISM 
 * dtlb miss and branches to label "ismhit". If this is not an ISM 
 * process or an ISM dtlb miss it falls thru.
 *
 * In the rare event this is a ISM process and a ISM dtlb miss has
 * not been detected in the first ism map block, it will branch
 * to "exitlabel".
 *
 * NOTE: We will never have any holes in our ISM maps. sfmmu_share/unshare
 *       will make sure of that. This means we can terminate our search on
 *       the first zero mapping we find.
 *
 * Parameters:
 * ctxptr = 64 bit reg that points to current context structure (CLOBBERED)
 * vaddr  = 32 bit reg containing virtual address of tlb miss
 * ism    = 64 bit reg where ism mapping will be stored: 
 *		ism_sfmmu[63:32]
 *		unused   [31:16]
 *		vbase    [15:8]
 *		size     [7:0]
 * maptr  = 64 bit scratch reg
 * tmp1   = 64 bit scratch reg
 * tmp2   = 32 bit scratch reg
 * label: temporary labels
 * ismhit: label where to jump to if an ism dtlb miss
 * exitlabel: label where to jump if end of list is reached and there
 *	      is a next ismblk.
 */
#define ISM_CHECK(ctxptr, vaddr, ism, maptr, tmp1, tmp2			\
	label, ismhit, exitlabel)					\
	ldx	[ctxptr + C_ISMBLKPA], ctxptr /* ctxptr= phys &ismblk*/	;\
	brlz,pt  ctxptr, label/**/2 	   /* exit if -1 */		;\
	  add	ctxptr, ISMBLK_MAPS, maptr /* maptr = &ismblk.map[0] */	;\
	ldxa	[maptr] ASI_MEM, ism	   /* ism = ismblk.map[0]   */	;\
label/**/1:								;\
	brz,pt  ism, label/**/2			/* no mapping */	;\
	  srl	ism, ISM_VB_SHIFT, tmp2		/* tmp2 = vbase */	;\
	srl	vaddr, ISM_AL_SHIFT, tmp1 	/* tmp1 = 16MB va seg*/	;\
	sub	tmp1, tmp2, tmp2		/* tmp2 = va - vbase*/	;\
	and	ism, ISM_SZ_MASK, tmp1	 	/* tmp1 = size */	;\
	cmp	tmp2, tmp1		 	/* check va <= offset*/	;\
	blu,pt	%icc, ismhit			/* ism hit */		;\
	  add	maptr, ISM_MAP_SZ, maptr 	/* maptr += sizeof map*/;\
	add	ctxptr, (ISMBLK_MAPS + ISM_MAP_SLOTS * ISM_MAP_SZ), tmp1;\
	cmp	maptr, tmp1						;\
	bl,pt	%icc, label/**/1		/* keep looking  */	;\
	  ldxa	[maptr] ASI_MEM, ism		/* ism = map[maptr] */	;\
	add	ctxptr, ISMBLK_NEXT, tmp1				;\
	lda	[tmp1] ASI_MEM, tmp2		/* check blk->next */	;\
	brnz,pt	tmp2, exitlabel			/* continue search */	;\
	  nop								;\
label/**/2:	

/*
 * Same as above except can be called from tl=0.
 * Also takes hatid as param intead of address of context struct.
 */
#define ISM_CHECK_TL0(hatid, vaddr, ism, maptr, tmp1, tmp2		\
	label, ismhit, exitlabel)					\
	ld	[hatid + SFMMU_ISMBLK], hatid /* hatid= &ismblk*/	;\
	brz,pt  hatid, label/**/2 	      /* exit if null */	;\
	  add	hatid, ISMBLK_MAPS, maptr /* maptr = &ismblk.map[0] */	;\
	ldx	[maptr], ism	   	  /* ism = ismblk.map[0]   */	;\
label/**/1:								;\
	brz,pt  ism, label/**/2			/* no mapping */	;\
	  srl	ism, ISM_VB_SHIFT, tmp2		/* tmp2 = vbase */	;\
	srl	vaddr, ISM_AL_SHIFT, tmp1 	/* tmp1 = 16MB va seg*/	;\
	sub	tmp1, tmp2, tmp2		/* tmp2 = va - vbase*/	;\
	and	ism, ISM_SZ_MASK, tmp1	 	/* tmp1 = size */	;\
	cmp	tmp2, tmp1		 	/* check va <= offset*/	;\
	blu,pt	%icc, ismhit			/* ism hit */		;\
	  add	maptr, ISM_MAP_SZ, maptr 	/* maptr += sizeof map*/;\
	add	hatid, (ISMBLK_MAPS + ISM_MAP_SLOTS * ISM_MAP_SZ), tmp1;\
	cmp	maptr, tmp1						;\
	bl,pt	%icc, label/**/1		/* keep looking  */	;\
	  ldx	[maptr], ism			/* ism = map[maptr] */	;\
	add	hatid, ISMBLK_NEXT, tmp1				;\
	ld	[tmp1], tmp2			/* check blk->next */	;\
	brnz,pt	tmp2, exitlabel			/* continue search */	;\
	  nop								;\
label/**/2:	


/*
 * returns the hme hash bucket (hmebp) given the vaddr, and the hatid
 * It also returns the virtual pg for vaddr (ie. vaddr << hmeshift)
 * Parameters:
 * vaddr = reg containing virtual address
 * hatid = reg containing sfmmu pointer
 * hashsz = global variable containing number of buckets in hash
 * hashstart = global variable containing start of hash
 * hmeshift = constant/register to shift vaddr to obtain vapg
 * hmebp = register where bucket pointer will be stored
 * vapg = register where virtual page will be stored
 * tmp1, tmp2 = tmp registers
 */
#define	HMEHASH_FUNC_ASM(vaddr, hatid, hashsz, hashstart, hmeshift,	\
	hmebp, vapg, tmp1, tmp2)					\
	sethi	%hi(hashsz), hmebp					;\
	sethi	%hi(hashstart), tmp1					;\
	ld	[hmebp + %lo(hashsz)], hmebp				;\
	ld	[tmp1 + %lo(hashstart)], tmp1				;\
	srl	vaddr, hmeshift, vapg					;\
	xor	vapg, hatid, tmp2	/* hatid ^ (vaddr >> shift) */	;\
	and	tmp2, hmebp, hmebp	/* index into khme_hash */	;\
	mulx	hmebp, HMEBUCK_SIZE, hmebp				;\
	add	hmebp, tmp1, hmebp

#define	MAKE_HASHTAG(vapg, hatid, hmeshift, hashno, hblktag)		\
	sll	vapg, hmeshift, vapg					;\
	or	vapg, hashno, vapg					;\
	sllx	vapg, HTAG_SFMMUPSZ, hblktag				;\
	or	hatid, hblktag, hblktag

/*
 * Function to traverse hmeblk hash link list and find corresponding match
 * The search is done using physical pointers. It returns the physical address
 * and virtual address pointers to the hmeblk that matches with the tag
 * provided.
 * Parameters:
 * hmebp = register that pointes to hme hash bucket, also used as tmp reg
 * hmeblktag = register with hmeblk tag match
 * hmeblkpa = register where physical ptr will be stored
 * hmeblkva = register where virtual ptr will be stored
 * tmp1 = 32bit tmp reg
 * tmp2 = 64bit tmp reg
 * label: temporary label
 * exitlabel: label where to jump if end of list is reached and no match found
 */
#define	HMEHASH_SEARCH(hmebp, hmeblktag, hmeblkpa, hmeblkva, tmp1,	\
	label, exitlabel, searchstat, linkstat)				\
	ldx	[hmebp + HMEBUCK_NEXTPA], hmeblkpa			;\
	ld	[hmebp + HMEBUCK_HBLK], hmeblkva			;\
	HATSTAT_DEBUG(searchstat, hmebp, tmp1)				;\
label/**/1:								;\
	brz,a,pn hmeblkva, exitlabel	/* if hmeblk ptr = 0 branch */	;\
	  nop								;\
	HATSTAT_DEBUG(linkstat, hmebp, tmp1)				;\
	add	hmeblkpa, HMEBLK_TAG, tmp1				;\
	ldxa	[tmp1] ASI_MEM, hmebp		/* read hblk_tag */	;\
	cmp	hmeblktag, hmebp		/* compare tags */	;\
	be,a,pn	%xcc, label/**/2					;\
	  nop								;\
	add	hmeblkpa, HMEBLK_NEXT, tmp1				;\
	lda	[tmp1] ASI_MEM, hmeblkva	/* hmeblk ptr va */	;\
	add	hmeblkpa, HMEBLK_NEXTPA, tmp1				;\
	ba,pt	%xcc, label/**/1					;\
	  ldxa	[tmp1] ASI_MEM, hmeblkpa	/* hmeblk ptr pa */	;\
label/**/2:	


/*
 * HMEBLK_TO_HMENT is a macro that given an hmeblk and a vaddr returns
 * he offset for the corresponding hment.
 * Parameters:
 * vaddr = register with virtual address
 * hmeblkpa = physical pointer to hme_blk
 * hment = register where address of hment will be stored
 * hmentoff = register where hment offset will be stored
 * label1 = temporary label
 */
#define	HMEBLK_TO_HMENT(vaddr, hmeblkpa, hmentoff, tmp1, label1)	\
	add	hmeblkpa, HMEBLK_MISC, hmentoff				;\
	lda	[hmentoff] ASI_MEM, tmp1 				;\
	andcc	tmp1, HBLK_SZMASK, %g0	 /* tmp1 = get_hblk_sz(%g5) */	;\
	bnz,a,pn  %icc, label1		/* if sz != TTE8K branch */	;\
	  or	%g0, HMEBLK_HME1, hmentoff				;\
	srl	vaddr, MMU_PAGESHIFT, tmp1				;\
	and	tmp1, NHMENTS - 1, tmp1		/* tmp1 = index */	;\
	/* XXX use shift when SFHME_SIZE becomes power of 2 */		;\
	mulx	tmp1, SFHME_SIZE, tmp1 					;\
	add	tmp1, HMEBLK_HME1, hmentoff				;\
label1:									;\

/*
 * GET_TTE is a macro that returns a TTE given a tag and hatid.
 *
 * Parameters:
 * tag       = 32 bit reg containing vitrual address
 * hatid     = 64 bit reg containing sfmmu pointer (CLOBBERED)
 * tte       = 64 bit reg where tte will be stored.
 * hmeblkpa  = 64 bit reg where physical pointer to hme_blk will be stored)
 * hmeblkva  = 32 bit reg where virtual pointer to hme_blk will be stored)
 * hmentoff  = 64 bit reg where hment offset will be stored)
 * hashsz    = global variable containing number of buckets in hash
 * hashstart = global variable containing start of hash
 * hmeshift  = constant/register to shift vaddr to obtain vapg
 * hashno    = constant/register hash number
 * label     = temporary label
 * exitlabel = label where to jump to on error
 */                                                             
#define GET_TTE(tag, hatid, tte, hmeblkpa, hmeblkva, hmentoff,		\
		hashsz, hashstart, hmeshift, hashno, label, exitlabel,	\
		searchstat, linkstat)					\
									;\
	HMEHASH_FUNC_ASM(tag, hatid, hashsz, hashstart, hmeshift, 	\
			 tte, hmeblkpa, hmentoff, hmeblkva)		;\
									;\
	/*								;\
	 * tag   = vaddr						;\
	 * hatid = hatid						;\
	 * tte   = hmebp (hme bucket pointer)				;\
	 * hmeblkpa  = vpag  (virtual page)				;\
	 * hmentoff, hmeblkva = scratch					;\
	 */								;\
	MAKE_HASHTAG(hmeblkpa, hatid, hmeshift, hashno, hmentoff)	;\
									;\
	/*								;\
	 * tag   = vaddr						;\
	 * hatid = hatid						;\
	 * tte   = hmebp						;\
	 * hmeblkpa  = clobbered					;\
	 * hmentoff  = hblktag						;\
	 * hmeblkva  = scratch						;\
	 */								;\
	HMEHASH_SEARCH(tte, hmentoff, hmeblkpa, hmeblkva, hatid, 	\
		label/**/1, exitlabel, searchstat, linkstat)		;\
									;\
	/*								;\
	 * We have found the hmeblk containing the hment.		;\
	 * Now we calculate the corresponding tte.			;\
	 *								;\
	 * tag   = vaddr						;\
	 * hatid = clobbered						;\
	 * tte   = hmebp						;\
	 * hmeblkpa  = hmeblkpa						;\
	 * hmentoff  = hblktag						;\
	 * hmeblkva  = hmeblkva 					;\
	 */								;\
	HMEBLK_TO_HMENT(tag, hmeblkpa, hmentoff, hatid, label/**/2)	;\
									;\
	/*								;\
	 * tag   = vaddr						;\
	 * hatid = scratch						;\
	 * tte   = hmebp						;\
	 * hmeblkpa  = hmeblk pa					;\
	 * hmentoff  = hmentoff						;\
	 * hmeblkva  = hmeblk va					;\
	 */								;\
	add	hmentoff, SFHME_TTE, hmentoff				;\
	add     hmeblkpa, hmentoff, hmeblkpa				;\
	add     hmeblkva, hmentoff, hmeblkva				;\
	ldxa	[hmeblkpa] ASI_MEM, tte	/* MMU_READTTE through pa */

/*
 * TTE_MOD_REF is a macro that updates the reference bit if it is
 * not already set.
 *
 * Parameters:
 * tte      = reg containing tte
 * ttepa    = physical pointer to tte
 * tteva    = virtual ptr to tte
 * tmp1     = tmp reg
 * label    = temporary label
 */
#ifndef	ATOMICS_WORK
#define	TTE_MOD_REF(tte, hmeblkpa, hmeblkva, tmp1, label)		\
	/* check reference bit */					;\
	andcc	tte, TTE_REF_INT, %g0					;\
	bnz,a,pt %xcc, label/**/2	/* if ref bit set-skip ahead */	;\
	  nop								;\
	/* update reference bit */					;\
	sethi	%hi(dcache_line_mask), tmp1				;\
	ld	[tmp1 + %lo(dcache_line_mask)], tmp1			;\
	and	hmeblkva, tmp1, tmp1					;\
	stxa	%g0, [tmp1] ASI_DC_TAG /* flush line from dcache */	;\
	membar	#Sync							;\
label/**/1:								;\
	or	tte, TTE_REF_INT, tmp1					;\
	membar #Sync							;\
	casxa	[hmeblkpa] ASI_MEM, tte, tmp1 	/* update ref bit */	;\
	cmp	tte, tmp1						;\
	bne,a,pn %xcc, label/**/1					;\
	  ldxa	[hmeblkpa] ASI_MEM, tte	/* MMU_READTTE through pa */	;\
	or	tte, TTE_REF_INT, tte					;\
label/**/2:

#else /* ATOMICS_WORK */

#define	TTE_MOD_REF(tte, hmeblkpa, hmeblkva, tmp1, label)		\
	/* check reference bit */					;\
	andcc	tte, TTE_REF_INT, %g0					;\
	bnz,a,pt %xcc, label/**/2	/* if ref bit set-skip ahead */	;\
	  nop								;\
	/* update reference bit */					;\
	sethi	%hi(dcache_line_mask), tmp1				;\
	ld	[tmp1 + %lo(dcache_line_mask)], tmp1			;\
	and	hmeblkva, tmp1, tmp1					;\
	stxa	%g0, [tmp1] ASI_DC_TAG /* flush line from dcache */	;\
	membar	#Sync							;\
label/**/1:								;\
	or	tte, TTE_REF_INT, tmp1					;\
	/* membar #Sync */						;\
	casxa	[hmeblkpa] ASI_MEM, tte, tmp1 	/* update ref bit */	;\
	cmp	tte, tmp1						;\
	bne,a,pn %xcc, label/**/1					;\
	  ldxa	[hmeblkpa] ASI_MEM, tte	/* MMU_READTTE through pa */	;\
	or	tte, TTE_REF_INT, tte					;\
label/**/2:
#endif /* ATOMICS_WORK */
	

#define	TSB_UPDATE(tsb8k, tte, tagtarget, tmp1, tmp2, label)		\
	/* lock tsb entry  - assumes: TSBE_TAG + TSBTAG_INTHI = 0 */	;\
	ld	[tsb8k], tmp1 						;\
label:									;\
	sethi	%hi(TSBTAG_LOCKED), tmp2 /* needs to be in loop */	;\
	cmp	tmp1, tmp2 						;\
	be,a,pn	%icc, label/**/b	/* if locked spin */		;\
	  ld	[tsb8k], tmp1 						;\
	membar #Sync			/* workaround ATOMICS_WORK */	;\
	casa	[tsb8k] ASI_N, tmp1, tmp2 				;\
	cmp	tmp1, tmp2 						;\
	bne,a,pn %icc, label/**/b	/* didn't lock so try again */	;\
	  ld	[tsb8k], tmp1 						;\
	/* tsbe lock acquired */					;\
	membar #StoreStore						;\
	stx	tte, [tsb8k + TSBE_TTE]		/* write tte data */	;\
	membar #StoreStore						;\
	stx	tagtarget, [tsb8k + TSBE_TAG]  /* write tte tag and unlock */

/*
 * There is implicit knowledge of the TSB_SIZE in this macro.
 * I do not check if the xor with the ctx can result in a ptr
 * outside the tsb.  Given a tsb size of 512k the maximum
 * TSB_CTX_SHIFT can be is 2 or an incorrect hash will result.
 */
#define	GET_TSB_POINTER(tagacc, tsbp, tmp1, tmp2)			\
	srlx	tagacc, MMU_PAGESHIFT, tmp1				;\
	sethi	%hi(TSB_OFFSET_MASK), tsbp				;\
	or	tsbp, %lo(TSB_OFFSET_MASK), tsbp			;\
	and	tmp1, tsbp, tmp1					;\
	sll	tagacc, TAGACC_CTX_SHIFT, tmp2				;\
	srl	tmp2, TAGACC_CTX_SHIFT - TSB_CTX_SHIFT, tmp2		;\
	xor	tmp2, tmp1, tmp1					;\
	sethi	%hi(tsb_base), tsbp					;\
	ld	[tsbp + %lo(tsb_base)], tsbp				;\
	sllx	tmp1, TSB_ENTRY_SHIFT, tmp1				;\
	or	tsbp, tmp1, tsbp

/*
 * This function executes at both tl=0 and tl>0.
 * It executes using the mmu alternate globals.
 */
	ENTRY_NP(sfmmu_utsb_miss)
	/*
	 * USER TSB MISS
	 */
	HATSTAT_DEBUG(HATSTAT_UTSBMISS, %g6, %g4)
	mov	MMU_TAG_ACCESS, %g4
	rdpr	%tt, %g5
	ldxa	[%g4] ASI_DMMU, %g3
	ldxa	[%g4] ASI_IMMU, %g2
#ifndef	ERRATA6_LDXA_WORKS
	or	%g2, %g0, %g0
	or	%g3, %g0, %g0
#endif /* ERRATA6_LDXA_WORKS */
	cmp	%g5, FAST_IMMU_MISS_TT
	movne	%xcc, %g3, %g2			/* g2 = vaddr + ctx */
	sll	%g2, TAGACC_CTX_SHIFT, %g3
	srl	%g3, TAGACC_CTX_SHIFT, %g3	/* g3 = ctx */
	/* calculate hatid given ctxnum */
	sethi	%hi(ctxs), %g6
	ld	[%g6 + %lo(ctxs)], %g1
	sll	%g3, CTX_SZ_SHIFT, %g3
	add	%g3, %g1, %g1			/* g1 = ctx ptr */
        ld      [%g1 + C_SFMMU], %g7            /* g7 = hatid */

	brz,pn	%g7, utsbmiss_tl0		/* if zero jmp ahead */
	  nop

	be,pt	%icc, 1f			/* not ism if itlb miss */
	  nop

	ISM_CHECK(%g1, %g2, %g3, %g4, %g5, %g6, utsb_l1, utsb_ism, utsbmiss_tl0)

1:
	GET_TTE(%g2, %g7, %g3, %g4, %g5, %g6, UHMEHASH_SZ, uhme_hash,
		HBLK_RANGE_SHIFT, 1, utsb_l2, utsb_pagefault,
		HATSTAT_UHASH_SEARCH, HATSTAT_UHASH_LINKS)

	/*
	 * g3 = tte
	 * g4 = tte pa
	 * g5 = tte va
	 * g6 = clobbered
	 */
	brgez,a,pn %g3, utsb_pagefault	/* if tte invalid branch */
	  nop

        /* 
	 * If itlb miss check nfo bit.
	 * if set treat as invalid.
	 */
        rdpr    %tt, %g6
        cmp     %g6, FAST_IMMU_MISS_TT
        bne,a,pt %icc, 3f
         andcc  %g3, TTE_REF_INT, %g0
        sllx    %g3, TTE_NFO_SHIFT, %g6		/* if nfo bit is set treat */
        brlz,a,pn %g6, utsbmiss_tl0		/* it as invalid */
          nop
3:

	/*
	 * Set reference bit if not already set
	 */
	TTE_MOD_REF(%g3, %g4, %g5, %g6, utsb_l3) 

	/*
	 * Now, load into TSB/TLB
	 * g2 = tagacc
	 * g3 = tte
	 * g4 will equal tag target
	 */
	rdpr	%tt, %g6
	cmp	%g6, FAST_IMMU_MISS_TT
	be,a,pn	%icc, 8f
	  nop
	/* dmmu miss */
	srlx	%g3, TTE_SZ_SHFT, %g6
	cmp	%g6, TTESZ_VALID | TTE8K
	bne,a,pn %icc, 4f
	  nop
	GET_TSB_POINTER(%g2, %g1, %g5, %g6)
	ldxa	[%g0]ASI_DMMU, %g4
	TSB_UPDATE(%g1, %g3, %g4, %g5, %g6, 7)
4:
	stxa	%g3, [%g0] ASI_DTLB_IN
	retry
	membar	#Sync
8:
	/* immu miss */
	srlx	%g3, TTE_SZ_SHFT, %g6
	cmp	%g6, TTESZ_VALID | TTE8K
	bne,a,pn %icc, 4f
	  nop
	GET_TSB_POINTER(%g2, %g1, %g5, %g6)
	ldxa	[%g0]ASI_IMMU, %g4
	TSB_UPDATE(%g1, %g3, %g4, %g5, %g6, 7)
4:
	stxa	%g3, [%g0] ASI_ITLB_IN
	retry
	membar	#Sync
utsb_pagefault:
	/*
	 * we get here if we couldn't find a valid tte in the hash.
	 * if we are at tl>0 we go to window handling code, otherwise
	 * we call pagefault.
	 */
	rdpr	%tl, %g5
	cmp	%g5, 1
	bg,pn	%icc, sfmmu_window_trap
	  nop
	ba,pt	%icc, sfmmu_pagefault
	  nop

utsb_ism:
	/*
	 * This is an ISM dtlb miss. 
	 *
	 * g2 = vaddr + ctx
	 * g3 = ism mapping.
	 */

	srlx	%g3, ISM_HAT_SHIFT, %g1		/* g1 = ism hatid */
	brz,pn	%g1, sfmmu_bad_trap		/* if zero jmp ahead */
	  nop

	srl	%g3, ISM_VB_SHIFT, %g4		/* clr size field */
	sll	%g4, ISM_AL_SHIFT, %g4		/* g4 = ism vbase */
	set	TAGACC_CTX_MASK, %g7		/* mask off ctx number */
	andn	%g2, %g7, %g6			/* g6 = tlb miss vaddr */
	sub	%g6, %g4, %g4			/* g4 = offset in ISM seg */	

	/*
	 * ISM pages are always locked down.
	 * If we can't find the tte then pagefault
	 * and let the spt segment driver resovle it
	 *
	 * g1 = ISM hatid
	 * g2 = orig tag (vaddr + ctx)
	 * g3 = ism mapping
	 * g4 = ISM vaddr (offset in ISM seg + ctx)
	 */
	GET_TTE(%g4, %g1, %g3, %g5, %g6, %g7, UHMEHASH_SZ, uhme_hash,
		HBLK_RANGE_SHIFT, 1, utsb_l4, utsb_pagefault,
		HATSTAT_UHASH_SEARCH, HATSTAT_UHASH_LINKS)

	/*
	 * If tte is invalid then pagefault and let the 
	 * spt segment driver resolve it.
	 *
	 * g3 = tte
	 * g5 = tte pa
	 * g6 = tte va
	 * g7 = clobbered
	 */
	brgez,a,pn %g3, utsb_pagefault	/* if tte invalid branch */
	  nop

	/*
	 * Set reference bit if not already set
	 */
	TTE_MOD_REF(%g3, %g5, %g6, %g7, utsb_l5)

	/*
	 * load into the TSB
	 * g2 = vaddr
	 * g3 = tte
	 */
	srlx	%g3, TTE_SZ_SHFT, %g6
	cmp	%g6, TTESZ_VALID | TTE8K
	bne,pn %icc, 4f
	  nop
	GET_TSB_POINTER(%g2, %g1, %g4, %g5)
	ldxa	[%g0]ASI_DMMU, %g4
	TSB_UPDATE(%g1, %g3, %g4, %g5, %g6, 7)
4:

	stxa	%g3, [%g0] ASI_DTLB_IN
	retry
	membar	#Sync

utsbmiss_tl0:
	/*
	 * We get here when we need to service this tsb miss at tl=0.
	 * Causes: ctx was stolen, more than ISM_MAP_SLOTS ism segments, 
	 *         possible large page, itlb miss on nfo page.
	 *
	 * g2 = tag access
	 */
	rdpr	%tl, %g5
	cmp	%g5, 1
	ble,pt	%icc, sfmmu_mmu_trap
	  nop
	ba,pt	%icc, sfmmu_window_trap
	  nop
	SET_SIZE(sfmmu_utsb_miss)

/*
 * This routine can execute for both tl=0 and tl>0 traps.
 * When running for tl=0 traps it runs on the alternate globals,
 * otherwise it runs on the mmu globals.
 */

	ENTRY_NP(sfmmu_ktsb_miss)
	/*
	 * KERNEL TSB MISS
	 */
	HATSTAT_DEBUG(HATSTAT_KTSBMISS, %g6, %g4)
	mov	MMU_TAG_ACCESS, %g4
	rdpr	%tt, %g5
	ldxa	[%g4] ASI_DMMU, %g3
	ldxa	[%g4] ASI_IMMU, %g2
#ifndef	ERRATA6_LDXA_WORKS
	or	%g2, %g0, %g0
	or	%g3, %g0, %g0
#endif /* ERRATA6_LDXA_WORKS */
	cmp	%g5, FAST_IMMU_MISS_TT
	movne	%xcc, %g3, %g2			/* g2 = vaddr + ctx */
	sethi	%hi(ksfmmup), %g6
	ld	[%g6 + %lo(ksfmmup)], %g1		/* g1 = hatid */

	GET_TTE(%g2, %g1, %g3, %g4, %g5, %g6, KHMEHASH_SZ, khme_hash,
		HBLK_RANGE_SHIFT, 1, ktsb_l1, ktsb_done,
		HATSTAT_KHASH_SEARCH, HATSTAT_KHASH_LINKS)

	/*
	 * g2 = vaddr + ctx
	 * g3 = tte
	 * g4 = tte pa
	 * g5 = tte va
	 * g6 = clobbered
	 */
	brgez,a,pn %g3, ktsb_done	/* if tte invalid branch */
	  nop

#ifdef	DEBUG
	sllx	%g3, TTE_PA_LSHIFT, %g6
	srlx	%g6, 30 + TTE_PA_LSHIFT, %g6	/* if not memory continue */
	brnz,pn	%g6, 2f
	  nop
	andcc	%g3, TTE_CP_INT, %g0		/* if memory - check it is */
	bz,a,pn	%icc, sfmmu_bad_trap		/* ecache cacheable */
	  mov	1, %g6
2:
#endif	/* DEBUG */

	/* 
	 * If an itlb miss check nfo bit.
	 * If set, pagefault. XXX
	 */ 
	rdpr    %tt, %g6
	cmp     %g6, FAST_IMMU_MISS_TT
	bne,a,pt %icc, 3f
	  andcc  %g3, TTE_REF_INT, %g0
	sllx    %g3, TTE_NFO_SHIFT, %g6		/* if nfo bit is set treat */
	brlz,a,pn %g6, ktsb_done		/* it is invalid */
	  nop
3:
	/*
	 * Set reference bit if not already set
	 */
	TTE_MOD_REF(%g3, %g4, %g5, %g6, ktsb_l2) 

#ifdef	DEBUG
	ldxa	[%g4] ASI_MEM, %g5		/* MMU_READTTE through pa */
	sllx	%g5, TTE_PA_LSHIFT, %g6
	srlx	%g6, 30 + TTE_PA_LSHIFT, %g6	/* if not memory continue */
	brnz,pn	%g6, 6f
	  nop
	andcc	%g5, TTE_CP_INT, %g0		/* if memory - check it is */
	bz,a,pn	%icc, sfmmu_bad_trap		/* ecache cacheable */
	  mov	2, %g6
#endif	/* DEBUG */
6:
	/*
	 * Now, load into TSB/TLB
	 * g2 = tagacc
	 * g3 = tte
	 * g4 will equal tag target
	 */
	rdpr	%tt, %g6
	cmp	%g6, FAST_IMMU_MISS_TT
	be,pn	%icc, 8f
	  srlx	%g3, TTE_SZ_SHFT, %g6
	/* dmmu miss */
	cmp	%g6, TTESZ_VALID | TTE8K
	bne,a,pn %icc, 4f
	  nop
	GET_TSB_POINTER(%g2, %g1, %g5, %g6)
	ldxa	[%g0]ASI_DMMU, %g4
	TSB_UPDATE(%g1, %g3, %g4, %g5, %g6, 7)
4:
	stxa	%g3, [%g0] ASI_DTLB_IN
	retry
	membar	#Sync
8:
	/* immu miss */
	cmp	%g6, TTESZ_VALID | TTE8K
	bne,a,pn %icc, 4f
	  nop
	GET_TSB_POINTER(%g2, %g1, %g5, %g6)
	ldxa	[%g0]ASI_IMMU, %g4
	TSB_UPDATE(%g1, %g3, %g4, %g5, %g6, 7)
4:
	stxa	%g3, [%g0] ASI_ITLB_IN
	retry
	membar	#Sync

ktsb_done:
	/*
	 * we get here if we couldn't find valid hment in hash
	 * first check if we are tl > 1, in which case we panic.
	 * otherwise call pagefault
	 */
	rdpr	%tl, %g5
	cmp	%g5, 1
	ble,pt	%xcc, sfmmu_mmu_trap
	  nop
	ba,pt	%xcc, sfmmu_bad_trap
	  nop
	SET_SIZE(sfmmu_ktsb_miss)


	ENTRY_NP(sfmmu_kprot_trap)
	/*
	 * KERNEL Write Protect Traps
	 *
	 * %g2 = MMU_TAG_ACCESS
	 */
	HATSTAT_DEBUG(HATSTAT_KMODFAULTS, %g6, %g4)
	sethi	%hi(ksfmmup), %g6
	ld	[%g6 + %lo(ksfmmup)], %g1		/* g1 = hatid */

	GET_TTE(%g2, %g1, %g3, %g4, %g5, %g6, KHMEHASH_SZ, khme_hash,
		HBLK_RANGE_SHIFT, 1, kprot_l1, kprot_lpage,
		HATSTAT_KHASH_SEARCH, HATSTAT_KHASH_LINKS)

	/*
	 * g2 = vaddr + ctx
	 * g3 = tte
	 * g4 = tte pa
	 * g5 = tte va
	 * g6 = clobbered
	 */
	brgez,a,pn %g3, kprot_tl0	/* if tte invalid goto tl0 */
	  nop

#ifdef	DEBUG
	sllx	%g3, TTE_PA_LSHIFT, %g6
	srlx	%g6, 30 + TTE_PA_LSHIFT, %g6	/* if not memory continue */
	brnz,pn	%g6, 2f
	  nop
	andcc	%g3, TTE_CP_INT, %g0		/* if memory - check it is */
	bz,a,pn	%icc, sfmmu_bad_trap		/* ecache cacheable */
	  mov	1, %g6
2:
#endif	/* DEBUG */
	andcc	%g3, TTE_WRPRM_INT, %g0		/* check write permissions */
	bz,pn	%xcc, kprot_tl0			/* no, jump ahead */
	  andcc	%g3, TTE_HWWR_INT, %g0		/* check if modbit is set */
	bnz,pn	%xcc, 6f			/* yes, go load tte into tsb */
	  nop
	/* update mod bit  */
	sethi	%hi(dcache_line_mask), %g7
	ld	[%g7 + %lo(dcache_line_mask)], %g7
	and	%g7, %g5, %g5
	stxa	%g0, [%g5]ASI_DC_TAG		/* flush line from dcache */
	membar	#Sync
9:
	or	%g3, TTE_HWWR_INT | TTE_REF_INT, %g1
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	casxa	[%g4] ASI_MEM, %g3, %g1		/* update ref/mod bit */
	cmp	%g3, %g1
	bne,a,pn %xcc, 9b
	  ldxa	[%g4] ASI_MEM, %g3		/* MMU_READTTE through pa */
	or	%g3, TTE_HWWR_INT | TTE_REF_INT, %g3
#ifdef	DEBUG
	ldxa	[%g4] ASI_MEM, %g5		/* MMU_READTTE through pa */
	sllx	%g5, TTE_PA_LSHIFT, %g6
	srlx	%g6, 30 + TTE_PA_LSHIFT, %g6	/* if not memory continue */
	brnz,pn	%g6, 6f
	  nop
	andcc	%g5, TTE_CP_INT, %g0		/* if memory - check it is */
	bz,a,pn	%icc, sfmmu_bad_trap		/* ecache cacheable */
	  mov	2, %g6
#endif	/* DEBUG */
6:
	/*
	 * load into the TSB
	 * g2 = vaddr
	 * g3 = tte
	 */
	srlx	%g3, TTE_SZ_SHFT, %g6
	cmp	%g6, TTESZ_VALID | TTE8K
	bne,pn	%icc, 4f
	  nop
	GET_TSB_POINTER(%g2, %g1, %g4, %g5)
	ldxa	[%g0]ASI_DMMU, %g4
	TSB_UPDATE(%g1, %g3, %g4, %g5, %g6, 7)
	/*
	 * Now, load into TLB
	 */
4:
	stxa	%g3, [%g0] ASI_DTLB_IN
	retry
	membar	#Sync

kprot_lpage:
	/*
	 * we get here if we couldn't find valid hment in hash
	 * since we were able to find the tte in the tlb, the trap
	 * most likely ocurred on large page tte.
	 * first check if we are tl > 1, in which case we panic.
	 * otherwise call sfmmu_tsb_miss
	 */
	rdpr	%tl, %g5
	cmp	%g5, 1
	ble,pt	%icc, sfmmu_mmu_trap
	  nop
	ba,pt	%icc, 9f		/* shouldn't happen */
	  nop

kprot_tl0:
	/*
	 * We get here if we didn't have write permission on the tte.
	 * first check if we are tl > 1, in which case we panic.
	 * otherwise call pagefault
	 */
	rdpr	%tl, %g2
	cmp	%g2, 1
	ble,pt	%icc, sfmmu_pagefault
	  nop
9:
	ba,a,pt %xcc, sfmmu_bad_trap
	  nop
	SET_SIZE(sfmmu_kprot_trap)


	ENTRY_NP(sfmmu_uprot_trap)
	/*
	 * USER Write Protect Trap
	 */
	HATSTAT_DEBUG(HATSTAT_UMODFAULTS, %g6, %g4)
	mov	MMU_TAG_ACCESS, %g1
	ldxa	[%g1] ASI_DMMU, %g2		/* g2 = vaddr + ctx */
	sll	%g2, TAGACC_CTX_SHIFT, %g3
	srl	%g3, TAGACC_CTX_SHIFT, %g3	/* g3 = ctx */
	/* calculate hatid given ctxnum */
	sethi	%hi(ctxs), %g6
	ld	[%g6 + %lo(ctxs)], %g1
	sll	%g3, CTX_SZ_SHIFT, %g3
	add	%g3, %g1, %g1				/* g1 = ctx ptr */
	ld      [%g1 + C_SFMMU], %g7                   /* g7 = hatid */
	brz,pn	%g7, uprot_tl0			/* if zero jmp ahead */
	  nop

	/*
	 * If ism goto sfmmu_tsb_miss
	 */
	ISM_CHECK(%g1, %g2, %g3, %g4, %g5, %g6, uprot_l1, 
		  uprot_tl0, uprot_tl0)

	GET_TTE(%g2, %g7, %g3, %g4, %g5, %g6, UHMEHASH_SZ, uhme_hash,
		HBLK_RANGE_SHIFT, 1, uprot_l2, uprot_fault,
		HATSTAT_UHASH_SEARCH, HATSTAT_UHASH_LINKS)
	/*
	 * g3 = tte
	 * g4 = tte pa
	 * g5 = tte va
	 */
	brgez,a,pn %g3, uprot_fault		/* if tte invalid goto tl0 */
	  nop
	andcc	%g3, TTE_WRPRM_INT, %g0		/* check write permissions */
	bz,a,pn	%xcc, uprot_fault		/* no, jump ahead */
	  nop
	andcc	%g3, TTE_HWWR_INT, %g0		/* check if modbit is set */
	bnz,a,pn %xcc, 6f			/* yes, go load tte into tsb */
	  nop
	/* update mod bit  */
	sethi	%hi(dcache_line_mask), %g7
	ld	[%g7 + %lo(dcache_line_mask)], %g7
	and	%g7, %g5, %g5
	stxa	%g0, [%g5]ASI_DC_TAG		/* flush line from dcache */
	membar	#Sync
9:
	or	%g3, TTE_HWWR_INT | TTE_REF_INT, %g1
#ifndef	ATOMICS_WORK
	membar #Sync
#endif
	casxa	[%g4] ASI_MEM, %g3, %g1		/* update ref/mod bit */
	cmp	%g3, %g1
	bne,a,pn %xcc, 9b
	  ldxa	[%g4] ASI_MEM, %g3		/* MMU_READTTE through pa */
	or	%g3, TTE_HWWR_INT | TTE_REF_INT, %g3
6:

	/*
	 * load into the TSB
	 * g2 = vaddr
	 * g3 = tte
	 * the TSB_8K ptr still has a correct value so take advantage of it.
	 */
	srlx	%g3, TTE_SZ_SHFT, %g6
	cmp	%g6, TTESZ_VALID | TTE8K
	bne,pn	%icc, 4f
	  nop
	GET_TSB_POINTER(%g2, %g1, %g4, %g5)
	ldxa	[%g0]ASI_DMMU, %g4
	TSB_UPDATE(%g1, %g3, %g4, %g5, %g6, 7)
	/*
	 * Now, load into TLB
	 */
4:
	stxa	%g3, [%g0] ASI_DTLB_IN
	retry
	membar	#Sync

uprot_fault:
	/*
         * we get here if we couldn't find valid hment in hash
	 * or we didn't have write permission.
         * first check if we are tl > 1, in which case we call window_trap
         * otherwise call pagefault
         * g2 = tag access reg
         */

	rdpr	%tl, %g4
	cmp	%g4, 1
	bg,a,pn	%xcc, sfmmu_window_trap
	  nop
	ba,pt	%xcc, sfmmu_pagefault
	  nop

uprot_tl0:
	/*
	 * We get here in the case we need to service this protection
	 * in c code.  Causes:
	 * ctx was stolen
	 * write fault on ism segment.
	 *
	 * first check if we are tl > 1, in which case we panic.
	 * otherwise call sfmmu_tsb_miss
	 * g2 = tag access reg
	 */
	rdpr	%tl, %g4
	cmp	%g4, 1
	ble,pt	%icc, sfmmu_mmu_trap
	  nop
	ba,pt	%icc, sfmmu_window_trap
	  nop
	SET_SIZE(sfmmu_uprot_trap)

#endif /* lint */

#if defined (lint)
/*
 * This routine will look for a user or kernel vaddr in the hash
 * structure.  It returns a valid pfn or -1.  It doesn't
 * grab any locks.  It should only be used by other sfmmu routines.
 */
/* ARGSUSED */
u_int
sfmmu_vatopfn(caddr_t vaddr, struct sfmmu *sfmmup)
{
	return(0);
}

#else /* lint */

	ENTRY_NP(sfmmu_vatopfn)
	save	%sp, -SA(MINFRAME), %sp
	/*
	 * disable interrupts
	 */
	rdpr	%pstate, %i4
#ifdef DEBUG
	andcc	%i4, PSTATE_IE, %g0		/* if interrupts already */
	bnz,a,pt %icc, 1f			/* disabled, panic	 */
	  nop
	sethi	%hi(hat_panic7), %o0
	or	%o0, %lo(hat_panic7), %o0
	call	panic
	  ld	[%o0], %o0
1:
#endif /* DEBUG */
	wrpr	%i4, PSTATE_IE, %pstate		/* disable interrupts */

	/*
	 * i0 = vaddr
	 * i1 = sfmmup
	 */
	lduh	[%i1 + SFMMU_CNUM], %l1		/* l1 = ctxnum */
	brnz,pn %l1, vatopfn_user		/* if user ctx branch */
	  mov	%i1, %g3			/* g3 = hat id */

vatopfn_kernel:
	/*
	 * i0 = vaddr
	 * g3 = hatid
	 */
	mov	%g3, %l1			/* save hatid */
	mov	1, %l5				/* l5 = rehash # */
1:
	cmp	%l5, 1
	be,a,pt	%icc, 2f
	  mov	HBLK_RANGE_SHIFT, %l6
	cmp	%l5, 2
	move	%icc, MMU_PAGESHIFT512K, %l6
	movne	%icc, MMU_PAGESHIFT4M, %l6
2:

	/*
	 * i0 = vaddr
	 * g3 = hatid
	 * l5 = rehash #
	 * l6 = hemshift
	 */
	GET_TTE(%i0, %g3, %g1, %g2, %l2, %g4, KHMEHASH_SZ, khme_hash,
		%l6, %l5, vatopfn_l1, 4f, HATSTAT_KHASH_SEARCH,
		HATSTAT_KHASH_LINKS)

	brgez,a,pn %g1, 6f			/* if tte invalid goto tl0 */
	  sub	%g0, 1, %i0			/* output = -1 */
	TTETOPFN(%g1, %i0, vatopfn_l2)		/* uses g1, g2, g3, g4 */
	/*
	 * i0 = vaddr
	 * g1 = pfn
	 */
	ba	%icc, 6f
	  mov	%g1, %i0
4:
	/*
	 * we get here if we couldn't find valid hment in hash.
	 */
	cmp	%l5, MAX_HASHCNT
	mov	%l1, %g3			/* restore hatid */
	bne,pt	%icc, 1b
	  add	%l5, 1, %l5
	sub	%g0, 1, %i0			/* output = -1 */
6:
	wrpr	%g0, %i4, %pstate		/* enable interrupts */
	ret
	restore

vatopfn_user:
	/*
	 * First check for ISM. If not, just fall thru.
	 *
	 * i1, g3 = hatid
	 * i0 = vaddr
	 */
	ISM_CHECK_TL0(%g3, %i0, %g1, %g2, %g4, %l5, vatopfn_l3, 
		  vatopfn_ism, vatopfn_rare)
	mov	%i1, %g3			/* restore hatid */

vatopfn_user_common:

	/*
	 * i0 = vaddr
	 * i1, g3 = hatid
	 * g3 = ism mapping
	 */
	mov	%g3, %l1			/* save hatid */
	mov	1, %l5				/* l5 = rehash # */
1:
	cmp	%l5, 1
	be,a,pt	%icc, 2f
	  mov	HBLK_RANGE_SHIFT, %l6
	cmp	%l5, 2
	move	%icc, MMU_PAGESHIFT512K, %l6
	movne	%icc, MMU_PAGESHIFT4M, %l6
2:

	/*
	 * i0 = vaddr
	 * g3 = hatid
	 * l5 = rehash #
	 * l6 = hemshift
	 */
	GET_TTE(%i0, %g3, %g1, %g2, %l2, %g4, UHMEHASH_SZ, uhme_hash,
		%l6, %l5, vatopfn_l4, 4f, HATSTAT_UHASH_SEARCH,
		HATSTAT_UHASH_LINKS)

	brgez,a,pn %g1, 6f			/* if tte invalid goto tl0 */
	  sub	%g0, 1, %i0			/* output = -1 */
	TTETOPFN(%g1, %i0, vatopfn_l5)		/* uses g1, g2, g3, g4 */
	/*
	 * i0 = vaddr
	 * g1 = pfn
	 */
	ba	%icc, 6f
	  mov	%g1, %i0
4:
	/*
	 * we get here if we couldn't find valid hment in hash.
	 */
	cmp	%l5, MAX_HASHCNT
	mov	%l1, %g3			/* restore hatid */
	bne,pt	%icc, 1b
	  add	%l5, 1, %l5
	sub	%g0, 1, %i0			/* output = -1 */
6:
	wrpr	%g0, %i4, %pstate		/* enable interrupts */
	ret
	restore

	/*
	 * We branch here if we detect a lookup on an ISM
	 * address. Adjust hatid to point the correct ISM
	 * hatid and va to offset into ISM segment.
	 */
vatopfn_ism:

	/*
	 * i0 = vaddr
	 * i1 = hatid
	 * g1 = ism mapping
	 */
        srl     %g1, ISM_VB_SHIFT, %l4          /* clr ism hatid */
        sll     %l4, ISM_AL_SHIFT, %l4          /* l4 = ism vbase */
        sub     %i0, %l4, %i0                   /* i0 = offset in ISM seg */
	srlx	%g1, ISM_HAT_SHIFT, %g3		/* g3 = ism hatid */
	ba,pt	%xcc, vatopfn_user_common
	  nop

	/*
	 * In the rare case this is a user va and this process
	 * has more than ISM_MAP_SLOTS ISM segments we goto 
	 * C code and handle the lookup there.
	 */
vatopfn_rare:
	/*
	 * i0 = vaddr
	 * i1 = user hatid
	 */
	wrpr	%g0, %i4, %pstate		/* enable interrupts */
	mov	%i1, %o1
	call	sfmmu_user_vatopfn
	mov	%i0, %o0

	mov	%o0, %i0			/* ret value */
	ret
	restore

	SET_SIZE(sfmmu_vatopfn)
#endif /* lint */

#ifdef DEBUG
	
#if defined (lint)
/*
 * This routine is called by the mmu trap handlers to increment a
 * vmhatstat counter.  The offset into the counter is passed in %g1.
 * XXX This routine should only be used in a non-DEBUG kernel and is not
 * mpized.
 */
void sfmmu_count()
{
}
#else /* lint */

	ENTRY_NP(sfmmu_count)
	/*
	 * g4 = stat
	 */
	membar	#Sync
	sethi	%hi(FLUSH_ADDR), %g6		/* flush addr doesn't matter */
	flush	%g6				/* flush required by immu */
	sethi	%hi(vmhatstat), %g6
	add	%g6, %g4, %g4
	ld	[%g4 + %lo(vmhatstat)], %g3
	inc	%g3
	st	%g3, [%g4 + %lo(vmhatstat)]
	jmp	%g7 + 4
	membar	#Sync
	SET_SIZE(sfmmu_count)

#endif /* lint */

#endif /* DEBUG */

#ifndef lint

/*
 * save area for registers in case of a sfmmu_bad_trap.  Let's hope
 * at least nucleus data tte is still available.
 */
	.seg	".data"
	.align	8
	.global sfmmu_bad_regs
sfmmu_bad_regs:
	.skip	160

	.align 8
	.global sfmmu_panic_stack
sfmmu_panic_stack_end:
	.skip	2000
sfmmu_panic_stack:

	.align 4
	.seg	".text"

#endif /* lint */
