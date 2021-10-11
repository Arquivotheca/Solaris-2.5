/*
 * Copyright (c) 1990-1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MMU_H
#define	_SYS_MMU_H

#pragma ident	"@(#)mmu.h	1.19	94/11/23 SMI"

#include <sys/param.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Sun-4c memory management unit.
 * All sun-4c implementations use 32 bits of address.
 * A particular implementation may implement a smaller MMU.
 * If so, the missing addresses are in the "middle" of the
 * 32 bit address space. All accesses in this range behave
 * as if there was an invalid page map entry correspronding
 * to the address.
 *
 * There are two types of MMUs a 2 level MMU and a 3 level MMU.
 * Three level MMUs do not have holes.
 */

/*
 * Hardware context and segment information
 * Mnemonic decoding:
 *	PMENT - Page Map ENTry
 *	PMGRP - Group of PMENTs (aka "segment")
 *	SMENT - Segment Map ENTry - 3 level MMU only
 *	SMGRP - Group of SMENTs (aka "region") - 3 level MMU only
 */
/* fixed SUN4C constants */
#define	NPMENTPERPMGRP		64
#define	NPMENTPERPMGRPSHIFT	6	/* log2(NPMENTPERPMGRP) */
#define	NPMGRPPERCTX		4096
#define	PMGRPSIZE	(NPMENTPERPMGRP * PAGESIZE)
#define	PMGRPOFFSET	(PMGRPSIZE - 1)
#define	PMGRPSHIFT	(PAGESHIFT + NPMENTPERPMGRPSHIFT)
#define	PMGRPMASK	(~PMGRPOFFSET)

#define	NSMENTPERSMGRP		64
#define	NSMENTPERSMGRPSHIFT	6	/* log2(NSMENTPERSMGRP) */
#define	SMGRPSIZE	(NSMENTPERSMGRP * PMGRPSIZE)
#define	SMGRPOFFSET	(SMGRPSIZE - 1)
#define	SMGRPSHIFT	(PMGRPSHIFT + NSMENTPERSMGRPSHIFT)
#define	SMGRPMASK	(~SMGRPOFFSET)

#define	NSMGRPPERCTX		256

/* variable (per implementation) */

/* SUN4C_60 -- not in boot PROM */
#define	NCTXS_60		8
#define	NPMGRPS_60		128
#define	VAC_SIZE_60		65536
#define	VAC_LINESIZE_60		16


/*
 * max mips at Mhz rates
 */
#define	CPU_MAXMIPS_20MHZ	20
#define	CPU_MAXMIPS_25MHZ	25

/*
 * Useful defines for hat constants,
 * Every implementation seems to have its own set
 * they are set at boot time by setcputype()
 */
#define	NCTXS		nctxs
#define	NPMGRPS		npmgrps
#define	NSMGRPS		nsmgrps

/*
 * Variables set at boot time to reflect machine capabilities.
 */
#ifndef _ASM
extern u_int nctxs;		/* number of implemented contexts */
extern u_int npmgrps;		/* number of pmgrps in page map */
extern u_int nsmgrps;		/* number of smgrps in segment map (3 level) */
extern caddr_t hole_start;	/* addr of start of MMU "hole" */
extern caddr_t hole_end;	/* addr of end of MMU "hole" */
extern u_int shm_alignment;	/* VAC address consistency modulus */

#define	PMGRP_INVALID (NPMGRPS - 1)
#define	SMGRP_INVALID	(NSMGRPS - 1)

/*
 * Macro to determine whether an address is within the range of the MMU.
 */
#define	good_addr(a) \
	((caddr_t)(a) < hole_start || (caddr_t)(a) >= hole_end)
#endif /* !_ASM */

/*
 * Address space identifiers.
 */
#define	ASI_CTL		0x2	/* control space */
#define	ASI_SM		0x3	/* segment map */
#define	ASI_PM		0x4	/* page map */
#define	ASI_FCS_HW	0x5	/* flush cache segment (HW assisted) */
#define	ASI_FCP_HW	0x6	/* flush cache page (HW assisted) */
#define	ASI_FCC_HW	0x7	/* flush cache context (HW assisted) */
#define	ASI_UP		0x8	/* user program */
#define	ASI_UI		0x8	/* user program; for compatibility  */
#define	ASI_SP		0x9	/* supervisor program */
#define	ASI_UD		0xA	/* user data */
#define	ASI_SD		0xB	/* supervisor data */
#define	ASI_FCS		0xC	/* flush cache segment */
#define	ASI_FCP		0xD	/* flush cache page */
#define	ASI_FCC		0xE	/* flush cache context */
#define	ASI_FCU_HW	0xF	/* flush cache unconditional (HW assisted) */

/*
 * ASI_CTL addresses
 */

#define	CONTEXT_REG		0x30000000
#define	SYSTEM_ENABLE		0x40000000
#define	SYNC_ERROR_REG		0x60000000
#define	SYNC_VA_REG		0x60000004
#define	ASYNC_ERROR_REG		0x60000008
#define	ASYNC_VA_REG		0x6000000C
#define	ASYNC_DATA1_REG		0x60000010	/* not avail. on 4/60 */
#define	ASYNC_DATA2_REG		0x60000014	/* not avail. on 4/60 */
#define	CACHE_TAGS		0x80000000
#define	CACHE_DATA		0x90000000
#define	UART_BYPASS		0xF0000000

/*
 * Various I/O space related constants
 */
#define	SBUS_BASE	0xf8000000	/* address of Sbus slots in OBIO */
#define	SBUS_SIZE	0x02000000	/* size of each slot */
#define	SBUS_SLOTS	4		/* number of slots */

/*
 * The usable DVMA space size.	 XXXXXXXXXXX - this is not real!!!
 */
#define	DVMASIZE	((1024*1024)-PMGRPSIZE)
#define	DVMABASE	(0-(1024*1024))

/*
 * Context for kernel. On a Sun-4c the kernel is in every address space,
 * but KCONTEXT is magic in that there is never any user context there.
 */
#define	KCONTEXT	0

/*
 * PPMAPBASE is the base virtual address of the range which
 * the kernel used to quickly map pages for operations such
 * as ppcopy, pagecopy, pagezero, and pagesum.
 */
#define	PPMAPBASE	(SYSBASE - NCARGS - (4 * PMGRPSIZE))

/*
 * SEGTEMP & SEGTEMP2 are virtual segments reserved for temporary operations.
 * We use the segments immediately before the start of debugger area.
 */
#define	SEGTEMP		((caddr_t)(SYSBASE - NCARGS - (2 * PMGRPSIZE)))
#define	SEGTEMP2	((caddr_t)(SYSBASE - NCARGS - PMGRPSIZE))

/*
 * REGTEMP is only during intialization, we use the
 * REGION immediately before KERNELBASE, it is invalidated
 * after use
 */
#define	REGTEMP		((caddr_t)((KERNELBASE-SMGRPSIZE)&SMGRPMASK))

#if defined(_KERNEL) && !defined(_ASM)
#include <vm/hat.h>
#include <vm/hat_sunm.h>
#include <sys/pte.h>

struct pmgrp;		/* forward declaration, keeps lint happy */

/*
 * Low level mmu-specific functions
 */
u_int	map_getctx(void);
void	map_setctx(u_int);
u_int	map_getsgmap(caddr_t);
void	map_setsgmap(caddr_t, u_int);
u_int	map_getpgmap(caddr_t);
void	map_setpgmap(caddr_t, u_int);

struct	ctx *mmu_getctx(void);
void	mmu_setctx(struct ctx *);
void	mmu_setpmg(caddr_t, struct pmgrp *);
void	mmu_settpmg(caddr_t, struct pmgrp *);
struct	pmgrp *mmu_getpmg(caddr_t);
void	mmu_pmginval(caddr_t);
void	mmu_setpte(caddr_t, struct pte);
void	mmu_getpte(caddr_t, struct pte *);
void	mmu_getkpte(caddr_t, struct pte *);

/* #ifdef MMU_3LEVEL */
void	map_setrgnmap(caddr_t, u_int);
u_int	map_getrgnmap(caddr_t);

struct	smgrp *mmu_getsmg(caddr_t base);
void	mmu_setsmg(caddr_t base, struct smgrp *);
void	mmu_settsmg(caddr_t base, struct smgrp *);
void	mmu_smginval(caddr_t base);
/* #endif */

#ifdef VAC

/*
 * cache related constants set at boot time
 */
extern int vac_size;			/* size of cache in bytes */
extern int vac_linesize;		/* cache linesize */
extern int vac_nlines;			/* number of lines in cache */
extern int vac_pglines;			/* number of cache lines in a page */
extern int vac_hwflush;			/* vac has hw flush capability */

/*
 * Cache specific routines - ifdef'ed out if there is no chance
 * of running on a machine with a virtual address cache.
 */
void	vac_control(int);
void	vac_init(void);
void	vac_tagsinit(void);
void	vac_flushall(void);
void	vac_ctxflush(void);
void	vac_usrflush(void);
void	vac_rgnflush(caddr_t base);
void	vac_segflush(caddr_t base);
void	vac_pageflush(caddr_t base);
void	vac_flush(caddr_t base, u_int nbytes);

#else /* VAC */

#define	vac_control()
#define	vac_init()
#define	vac_tagsinit()
#define	vac_flushall()
#define	vac_usrflush()
#define	vac_ctxflush()
#define	vac_rgnflush(base)
#define	vac_segflush(base)
#define	vac_pageflush(base)
#define	vac_flush(base, len)

#endif /* VAC */

#endif /* defined(_KERNEL) && !defined(_ASM) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MMU_H */
