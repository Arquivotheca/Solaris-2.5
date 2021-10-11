/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ifndef _SYS_IOMMU_H
#define	_SYS_IOMMU_H

#pragma ident	"@(#)iommu.h	1.26	95/03/07 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/* constants for DVMA */
#define	IOMMU_DVMA_RANGE	(0x4000000)	/* 64M for sun4d */
#define	IOMMU_DVMA_BASE		(0 - IOMMU_DVMA_RANGE)
#define	IOMMU_IOPBMEM_BASE	(0xFFF00000)	/* top 1M */

#define	IOMMU_PAGE_SIZE		(4096)		/* 4k page */
#define	IOMMU_PAGE_OFFSET	(4096 - 1)

#ifndef _ASM

#include <sys/avintr.h>

/* define IOPTEs */
#define	IOPTE_PFN_MSK	0xffffff00
#define	IOPTE_PFN_SHIFT	0x8

#define	IOPTE_CACHE	0x80
#define	IOPTE_STREAM	0x40
#define	IOPTE_INTRA	0x08
#define	IOPTE_WRITE	0x04

#define	IOPTE_FLAG_MSK	0xcc

#define	IOPTE_VALID	0x02
#define	IOPTE_PARITY	0x01

typedef struct {
	u_int	iopte;
} iommu_pte_t;

/* maybe we should have a sbus.h? */
#define	MX_SBUS		(14)
#define	MX_PRI		(8)
#define	MX_SBUS_SLOTS	(4)		/* 4 slots per Sbus */

/*
 * some bit masks describe slave access burst capability
 * of a slot in DDI way.
 */
#define	NO_SBUS_BURST	(7)		/* 7 realy means no burst */
#define	SBUS_BURST_MASK	(0x7f)		/* all possible bits */
#define	SBUS_BURST_64B	(0x7f)		/* 64 Bytes burst */
#define	HWBC_BURST_MASK	(0x70)		/* hwbcopy supports only BA16 and up */

struct sbus_private {
	int sbi_id;			/* sun4d UnitNum */
	caddr_t va_sbi;			/* vaddr for SBI regs */
	iommu_pte_t *va_xpt;		/* vaddr for IOPTEs */
	struct map *map;		/* DVMA map for this IOMMU */
	u_int dma_reserve;		/* # of pages for dvma_reserve */

	int dvma_call_list_id;		/* DVMA callback list */

	kmutex_t dma_pool_lock;
	caddr_t dmaimplbase;		/* protected by dma_pool_lock */

	int burst_size[MX_SBUS_SLOTS];	/* burst size in slot config reg */
	struct autovec *vec;		/* ISR list */
};

extern char DVMA[];

/* prototype for IOMMU related routines */
extern void iommu_init(iommu_pte_t *va_xpt);
extern int iommu_pteload(iommu_pte_t *piopte, u_int mempfn, u_int flag);
extern int iommu_unload(iommu_pte_t *va_xpt, caddr_t dvma_addr, int npf);
extern int iommu_pteunload(iommu_pte_t *piopte);
extern iommu_pte_t *iommu_ptefind(iommu_pte_t *va_xpt, caddr_t dvma_addr);

#endif !_ASM

/* some macros for iommu ... */
#define	iommu_btop(x)	(mmu_btop(x))	/* all have 4k pages */
#define	iommu_btopr(x)	(mmu_btopr(x))	/* all have 4k pages */
#define	iommu_ptob(x)	(mmu_ptob(x))	/* all have 4k pages */

/* define page frame number for IOPBMEM and DVMA_BASE, in IOMMU's view */
#define	IOMMU_DVMA_DVFN		(iommu_btop(IOMMU_DVMA_BASE))
#define	SBUSMAP_BASE		(IOMMU_DVMA_DVFN)
/* SBUS map size: the whole 64M minus 4 pages for dump now */
#define	SBUSMAP_SIZE 		(iommu_btop(IOMMU_DVMA_RANGE) - 4)
#define	SBUSMAP_FRAG 		(SBUSMAP_SIZE >> 3)
#define	SBUSMAP_MAXRESERVE	(SBUSMAP_SIZE >> 1)

/*
 * NOTE: These mapping are not visible from host SRMMU. These mappings
 *	 are used on IOMMU only.
 */
#define	SBUSDVMA_BASE		(IOMMU_IOPBMEM_BASE)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IOMMU_H */
