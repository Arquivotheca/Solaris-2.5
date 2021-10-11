/*
 * Copyright (c) 1990-1994, by Sun Microsystems, Inc.
 */

#ident	"@(#)iommu_asm.s	1.4	95/03/22 SMI"

#include <sys/spitasi.h>
#include <sys/asm_linkage.h>
#include <sys/iommu.h>
#ifndef lint
#include <assym.s>
#endif

/*
 * Function: iommu_tteload
 * Args:	%o0: u_ll_t *iotte_ndx
 *		%o1: u_int pfn
 *		%o2-%o3: u_ll_t tte_flag
 */
#ifdef lint
/*ARGSUSED*/
void
iommu_tteload(u_ll_t *iotte_ndx, u_int pfn, u_ll_t tte_flag)
{
	return;
}
#else
	ENTRY(iommu_tteload)
	srl	%o1, 0, %o1			/* Clear upper 32 bits */
	or	%o1, %g0, %g1			/* Set the pfn */
	sllx	%g1, IOTTE_PFN_SHIFT, %g1	/* shift the pfn */
	sethi	%hi(IOTTE_VALID), %g2		/* Set */
	sllx	%g2, 32, %g2			/* the IOTTE valid bit */
	or	%g1, %g2, %g1			/* Set the valid bit */
	sllx	%o2, 32, %g2			/* Shift to upper 32 bits */
	srl	%o3, 0, %o3			/* Clear upper 32 bits */
	or	%o3, %g2, %g2			/* Put everything in %g2 */
	or	%g2, %g1, %g1			/* Put tte flags in tte */
	retl
	stxa	%g1, [%o0]ASI_MEM		/* Store the hardware tte */
	SET_SIZE(iommu_tteload)
#endif


/*
 * Function: iommu_tteunload
 * Args:	%o0: u_ll_t *iotte_ndx
 */
#ifdef lint
/*ARGSUSED*/
void
iommu_tteunload(u_ll_t *iotte_ndx)
{
	return;
}
#else
	ENTRY(iommu_tteunload)
	srl	%o0, 0, %o0			/* Clear upper 32 bits */
	retl
	stxa	%g0, [%o0]ASI_MEM		/* Store 0 to invalidate */
	SET_SIZE(iommu_tteunload)
#endif


/*
 * Function: iommu_tlb_flush
 * Args: 	%o0: struct sbus_soft_state *softsp
 *		%o1: caddr_t flush_addr
 */
#ifdef lint
/*ARGSUSED*/
void
iommu_tlb_flush(struct sbus_soft_state *softsp, u_long flush_addr)
{
	return;
}
#else
	ENTRY(iommu_tlb_flush)
	srl	%o1, 0, %o1			/* Clear upper 32 bits */
	or	%o1, %g0, %g1			/* Put flush data in global */
	ld	[%o0 + IOMMU_FLUSH_REG], %o1	/* Get the flush register */
	stx	%g1, [%o1]			/* Bang the flush reg */
	ld	[%o0 + SBUS_CTRL_REG], %o1	/* Get the sbus ctrl reg */
	retl
	ldx	[%o1], %g0			/* Flush write buffers */
	SET_SIZE(iommu_tlb_flush)
#endif
