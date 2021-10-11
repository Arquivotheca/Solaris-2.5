/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_IOMMU_H
#define	_SYS_IOMMU_H

#pragma ident	"@(#)iommu.h	1.12	95/06/14 SMI"

#if defined(_KERNEL) && !defined(_ASM)
#include <sys/sunddi.h>
#include <sys/sysiosbus.h>
#endif /* defined(_KERNEL) && !defined(_ASM) */

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _ASM
/* constants for DVMA */
#define	IOMMU_DVMA_RANGE	0x4000000	/* 64M DVMA range */
#define	IOMMU_DVMA_BASE		(0 - IOMMU_DVMA_RANGE)
#define	IOMMU_DVMA_DVFN		iommu_btop(IOMMU_DVMA_BASE)

#define	SBUSMAP_SIZE		(iommu_btop(IOMMU_DVMA_RANGE))
#define	SBUSMAP_MAXRESERVE	(SBUSMAP_SIZE >> 1)

#define	IOMMU_PAGESIZE		0x2000		/* 8k page */
#define	IOMMU_PAGEMASK		0x1fff
#define	IOMMU_PAGEOFFSET	(IOMMU_PAGESIZE - 1)
#define	IOMMU_N_TTES		(IOMMU_DVMA_RANGE/IOMMU_PAGESIZE)
#define	IOMMU_TSB_TBL_SIZE	(IOMMU_N_TTES << 3)	/* 8B for each entry */
#define	IOMMU_PAGESHIFT		13

#define	OFF_IOMMU_CTRL_REG	0x2400
#define	IOMMU_CTRL_REG_SIZE	(NATURAL_REG_SIZE)
#define	OFF_TSB_BASE_ADDR	0x2408
#define	TSB_BASE_ADDR_SIZE	(NATURAL_REG_SIZE)
#define	OFF_IOMMU_FLUSH_REG	0x2410
#define	IOMMU_FLUSH_REG		(NATURAL_REG_SIZE)

#define	TSB_SIZE		3		/* 64M of DVMA */
#define	TSB_SIZE_SHIFT		16

#define	IOMMU_ENABLE		1		/* Turns on the IOMMU */

/* define IOPTEs */
#define	IOTTE_PFN_MSK	0x1ffffffe000ull
#define	IOTTE_CACHE	0x10ull
#define	IOTTE_WRITE	0x2ull
#define	IOTTE_STREAM	0x1000000000000000ull
#define	IOTTE_INTRA	0x800000000000000ull
#define	IOTTE_64K_PAGE	0x2000000000000000ull
#endif	/* _ASM */
#define	IOTTE_VALID	0x80000000
#define	IOTTE_PFN_SHIFT 13

/*
 * IOMMU pages to bytes, and back (with and without rounding)
 */
#define	iommu_ptob(x)	((x) << IOMMU_PAGESHIFT)
#define	iommu_btop(x)	(((unsigned)(x)) >> IOMMU_PAGESHIFT)
#define	iommu_btopr(x)	((((unsigned)(x) + IOMMU_PAGEOFFSET) \
				>>  IOMMU_PAGESHIFT))

#if defined(_KERNEL) && !defined(_ASM)

extern int iommu_init(struct sbus_soft_state *, int);
extern int iommu_resume_init(struct sbus_soft_state *);
extern int iommu_dma_mctl(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
	enum ddi_dma_ctlops, off_t *, u_int *, caddr_t *, u_int);
extern int iommu_dma_map(dev_info_t *, dev_info_t *, struct ddi_dma_req *,
	ddi_dma_handle_t *);
extern int iommu_dma_allochdl(dev_info_t *, dev_info_t *, ddi_dma_attr_t *,
	int (*waitfp)(caddr_t), caddr_t arg, ddi_dma_handle_t *);
extern int iommu_dma_freehdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t);
extern int iommu_dma_bindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
	struct ddi_dma_req *, ddi_dma_cookie_t *, u_int *);
extern int iommu_dma_unbindhdl(dev_info_t *, dev_info_t *, ddi_dma_handle_t);
extern int iommu_dma_flush(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
	off_t, u_int, u_int);
extern int iommu_dma_win(dev_info_t *, dev_info_t *, ddi_dma_handle_t,
	uint_t, off_t *, uint_t *, ddi_dma_cookie_t *, uint_t *);

extern void iommu_tteload(u_ll_t *, u_int, u_ll_t);
extern void iommu_tteunload(u_ll_t *);
extern void iommu_tlb_flush(struct sbus_soft_state *, u_long);

void
iommu_dvma_kaddr_load(ddi_dma_handle_t h, caddr_t a, u_int len, u_int index,
    ddi_dma_cookie_t *cp);

void
iommu_dvma_unload(ddi_dma_handle_t h, u_int objindex, u_int view);

void
iommu_dvma_sync(ddi_dma_handle_t h, u_int objindex, u_int view);

#endif /* _KERNEL && !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IOMMU_H */
