/*
 * Copyright (c) 1990, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_MMU_H
#define	_SYS_MMU_H

#pragma ident	"@(#)mmu.h	1.30	95/08/17 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/module.h>

/*
 * Definitions for the SOFT MMU
 */

#define	FAST_IMMU_MISS_TT	0x64
#define	FAST_DMMU_MISS_TT	0x68
#define	FAST_PROT_TT		0x6c

/*
 * Constants defining alternate spaces
 * and register layouts within them,
 * and a few other interesting assembly constants.
 */

#define	MMU_NUM_TLB_ENTRIES	64	/* XXX use prom property */

/*
 * vaddr offsets of various registers
 */
#define	MMU_TTARGET		0x00 /* TSB tag target */
#define	MMU_PCONTEXT		0x08 /* primary context number */
#define	MMU_SCONTEXT		0x10 /* secondary context number */
#define	MMU_SFSR		0x18 /* sync fault status reg */
#define	MMU_SFAR		0x20 /* sync fault addr reg */
#define	MMU_TSB			0x28 /* tsb base and config */
#define	MMU_TAG_ACCESS		0x30 /* tlb tag access */
#define	MMU_VAW			0x38 /* virtual watchpoint */
#define	MMU_PAW			0x40 /* physical watchpoint */


/*
 * Synchronous Fault Stauts Register Layout
 *
 * IMMU and DMMU maintain their own SFSR Register
 * ______________________________________________________________________
 * |   Reserved   |  ASI | Reserved | FT | E | Cntx | PRIV | W | OW | FV|
 * |--------------|------|----------|----|---|------|------|---|----|---|
 *  63		24 23  16 15	12 11	7  6  5	   4	3    2	  1   0
 *
 */
#define	SFSR_FV		0x00000001	/* fault valid */
#define	SFSR_OW		0x00000002	/* overwrite */
#define	SFSR_W		0x00000004	/* data write */
#define	SFSR_PR		0x00000008	/* privilege mode */
#define	SFSR_CTX	0x00000030	/* context id */
#define	SFSR_E		0x00000040	/* side-effect */
#define	SFSR_FT		0x00000F80	/* fault type mask */
#define	SFSR_ASI	0x00FF0000	/* ASI */

/*
 * Definition of FT (Fault Type) bit field of sfsr.
 */
#define	FT_NONE		0x00
#define	FT_PRIV		0x01		/* privilege violation */
#define	FT_SPEC_LD	0x02		/* speculative ld to e page */
#define	FT_ATOMIC_NC	0x04		/* atomic to nc page */
#define	FT_ILL_ALT	0x08		/* illegal lda/sta */
#define	FT_NFO		0x10		/* normal access to nfo page */
#define	FT_RANGE_PC	0x20		/* pc out of range */
#define	FT_RANGE_REG	0x40		/* jump to reg out of range */
#define	SFSR_FT_SHIFT	7	/* amt. to shift right to get flt type */
#define	X_FAULT_TYPE(x)	(((x) & SFSR_FT) >> SFSR_FT_SHIFT)

/*
 * Defines for CT (ConText id) bit field of sfsr.
 */
#define	CT_PRIMARY	0x0	/* primary */
#define	CT_SECONDARY	0x1	/* secondary */
#define	CT_NUCLEUS	0x2	/* nucleus */
#define	SFSR_CT_SHIFT	4

#define	SFSR_ASI_SHIFT	16

/*
 * MMU TAG TARGET register Layout
 *
 * +-----+---------+------+-------------------------+
 * | 000 | context |  --  | virtual address [63:22] |
 * +-----+---------+------+-------------------------+
 *  63 61 60	 48 47	42 41			   0
 */
#define	TTARGET_CTX_SHIFT	48

/*
 * MMU TAG ACCESS register Layout
 *
 * +-------------------------+------------------+
 * | virtual address [63:13] |  context [12:0]  |
 * +-------------------------+------------------+
 *  63			  13	12		0
 */
#define	TAGACC_CTX_MASK		0x1FFF
#define	TAGACC_SHIFT		13
#define	TAGACC_VADDR_MASK	(~TAGACC_CTX_MASK)
#define	TAGACC_CTX_SHIFT	19

/*
 * MMU DEMAP Register Layout
 *
 * +-------------------------+------+------+---------+-----+
 * | virtual address [63:13] | rsvd | type | context |  0  |
 * +-------------------------+------+------+---------+-----+
 *  63			   13 12   7   6    5	    4 3   0
 */
#define	DEMAP_PRIMARY		(CT_PRIMARY << SFSR_CT_SHIFT)
#define	DEMAP_SECOND		(CT_SECONDARY << SFSR_CT_SHIFT)
#define	DEMAP_NUCLEUS		(CT_NUCLEUS << SFSR_CT_SHIFT)
#define	DEMAP_PAGE_TYPE		0x00		/* Type bit 6 */
#define	DEMAP_CTX_TYPE		0x40		/* Type bit 6 */

/*
 * TLB DATA ACCESS Address Layout
 *
 * +-------------+---------------+---+
 * +   Not used	 |   tlb entry	 | 0 |
 * +-------------+---------------+---+
 *  63		9 8		3 2  0
 */
#define	DTACC_SHIFT	0x3
#define	DTACC_INC	0x8

/*
 * TSB Register Layout
 *
 * +----------------------+-------+-----+-------+
 * +  tsb_base va [63:13] | split |  -  |  size |
 * +----------------------+-------+-----+-------+
 *  63			13   12	    11 3 2	0
 */
#define	TSBBASE_SHIFT		13
#define	TSBSPLIT_SHIFT		12
#define	TSB_SPLIT_CONFIG	1
#define	TSB_COMMON_CONFIG	0

/*
 * MMU TAG READ register Layout
 *
 * +-------------------------+------------------+
 * | virtual address [63:13] |  context [12:0]  |
 * +-------------------------+------------------+
 *  63			  13	12		0
 */
#define	TAGREAD_CTX_MASK	0x1FFF
#define	TAGREAD_SHIFT		13
#define	TAGREAD_VADDR_MASK	(~TAGREAD_CTX_MASK)

/*
 * The kernel always runs in KCONTEXT, and no user mappings
 * are ever valid in it (so any user access pagefaults).
 */
#define	KCONTEXT	0

/*
 * FLUSH_ADDR is used in the flush instruction to guarantee stores to mmu
 * registers complete.  It is selected so it won't miss in the tlb.
 */
#define	FLUSH_ADDR	(KERNELBASE + MMU_PAGESIZE4M)


#ifndef _ASM

/*
 * Defines for MMU Module Routines
 */

extern struct module_ops *moduleops;

/*
 * This routine will demap the tlb entries for the specified virtual page
 * number and ctx.  It expects the vaddr to be page aligned.
 */
#define	MMU_TLBFLUSH_PAGE(vaddr, ctxnum)				\
	ASSERT(!((uint)vaddr & MMU_PAGEOFFSET));			\
	ASSERT(moduleops->mmuops.tlbflush_page);			\
	(*moduleops->mmuops.tlbflush_page) (vaddr, ctxnum)

/*
 * This routine will demap the tlb entries corresponding to the
 * specified context.
 */
#define	MMU_TLBFLUSH_CTX(ctxnum)					\
	ASSERT(ctxnum != KCONTEXT);					\
	ASSERT(moduleops->mmuops.tlbflush_ctx);				\
	(*moduleops->mmuops.tlbflush_ctx) (ctxnum)

#define	MMU_TLBFLUSH_PAGE_MP(vaddr, ctxnum, cpuset)			\
	xt_some(cpuset, (uint)moduleops->mmuops.tlbflush_page_tl1,	\
		(u_int)vaddr,  ctxnum, 0, 0)

#define	MMU_TLBFLUSH_CTX_MP(ctxnum, cpuset)				\
	xt_some(cpuset, (uint)moduleops->mmuops.tlbflush_ctx_tl1,	\
		ctxnum, 0, 0, 0)

#define	MMU_TLBCACHE_FLUSHPAGE_MP(vaddr, ctxnum, pfnum, cpuset)		\
	xt_some(cpuset, (uint)moduleops->mmuops.tlbcacheflush_page_tl1, \
		(uint)vaddr, ctxnum, pfnum, 0)

#define	MMU_XCALL_SYNC_MP(cpuset)					\
	xt_some(cpuset, (uint)sfmmu_xcall_sync_tl1, 0, 0, 0, 0)

/*
 * This routines loads a translation into the itlb.
 * vaddr = virtual address of translation.
 * ctxnum = context number of translation.
 * ttep  = pointer to tte of translation.
 */
#define	MMU_ITLB_LD(vaddr, ctxnum, ttep)				\
	ASSERT(moduleops->mmuops.itlb_ld);				\
	(*moduleops->mmuops.itlb_ld) (vaddr, ctxnum, ttep)

/*
 * This routines loads a translation into the dtlb.
 * vaddr = virtual address of translation.
 * ctxnum = context number of translation.
 * ttep  = pointer to tte of translation.
 */
#define	MMU_DTLB_LD(vaddr, ctxnum, ttep)				\
	ASSERT(moduleops->mmuops.dtlb_ld);				\
	(*moduleops->mmuops.dtlb_ld) (vaddr, ctxnum, ttep)

/*
 * This routine modifies a tte atomically using cas. It will reread
 * the TTE if the cas failed and retry until it succeeds.  It will return
 * the last tte contents in *sttep before the cas succeeded.
 * sttep points to the source tte unmodified.
 * modttep points to the source tte modified.
 * dttep point to the destination tte.
 */
#define	MMU_MODIFYTTE(sttep, modttep, dttep)				\
	((*moduleops->mmuops.modifytte) (sttep, modttep, dttep))

/*
 * This routine attempts to modify a tte atomically using cas.
 * It returns 0 on success or 1 if the tte has changed.
 * sttep points to the source tte unmodified.
 * modttep points to the source tte modified.
 * dttep point to the destination tte.
 */
#define	MMU_MODIFYTTE_TRY(sttep, modttep, dttep)			\
	((*moduleops->mmuops.modifytte_try) (sttep, modttep, dttep))

/*
 * This routine copies a tte.  First parameter is the source address and
 * the second parameter is the destination address.
 */
#define	MMU_COPYTTE(sttep, dttep)					\
	ASSERT(moduleops->mmuops.copytte);				\
	(*moduleops->mmuops.copytte) (sttep, dttep)

#endif /* !_ASM */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_MMU_H */
