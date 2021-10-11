/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef _SYS_PTE_H
#define	_SYS_PTE_H

#pragma ident	"@(#)pte.h	1.37	95/07/04 SMI"

/*
 * Copyright (c) 1988, Sun Microsystems, Inc. All Rights Reserved.
 * Sun considers its source code as an unpublished, proprietary
 * trade secret, and it is available only under strict license
 * provisions. This copyright notice is placed here only to protect
 * Sun in the event the source is deemed a published work. Disassembly,
 * decompilation, or other means of reducing the object code to human
 * readable form is prohibited by the license agreement under which
 * this code is provided to the user or company in possession of this
 * copy
 *
 * RESTRICTED RIGHTS LEGEND: Use, duplication, or disclosure by the
 * Government is subject to restrictions as set forth in subparagraph
 * (c) (1) (ii) of the Rights in Technical Data and Computeer Software
 * clause at DFARS 52.227-7013 and in similar clauses in the FAR and
 * NASA FAR Supplement
 */

#ifndef _ASM
#include <sys/types.h>
#endif /* _ASM */

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _ASM
/*
 * The tte struct is a 64 bit data type.  Since we currently plan to
 * use a V8 compiler all manipulations in C will be done using the bit fields
 * or as 2 integers.  In assembly code we will deal with it as a double (using
 * ldx and stx).  The structure is defined to force a double alignment.
 */
typedef union {
	struct tte {
		unsigned	v:1;		/* 1=valid mapping */
		unsigned	sz:2;		/* 0=8k 1=64k 2=512k 3=4m */
		unsigned	nfo:1;		/* 1=no-fault access only */

		unsigned	ie:1;		/* 1=invert endianness */
		unsigned	hmenum:3;	/* sw - # of hment in hme_blk */

		unsigned	lckcnt:6;	/* sw - tte lock ref cnt */
		unsigned	diag:9;		/* */
		unsigned	pahi:9;		/* pa[40:32] */
		unsigned	palo:19;	/* pa[31:13] */
		unsigned	soft1:1;	/* sw bits - unused */

		unsigned	soft2:1;	/* sw bits - unused */
		unsigned	ref:1;		/* sw - reference */
		unsigned	wr_perm:1;	/* sw - write permission */
		unsigned	no_sync:1;	/* sw - ghost unload */

		unsigned	soft3:1;	/* sw bits - unused */
		unsigned	l:1;		/* 1=lock in tlb */
		unsigned	cp:1;		/* 1=cache in ecache, icache */
		unsigned	cv:1;		/* 1=cache in dcache */

		unsigned	e:1;		/* 1=side effect */
		unsigned	p:1;		/* 1=privilege required */
		unsigned	w:1;		/* 1=writes allowed */
		unsigned	g:1;		/* 1=any context matches */
	} tte_bit;
	struct {
		int		inthi;
		uint		intlo;
	} tte_int;
	double			dummy;		/* To force alignment only */
} tte_t;

#define	tte_val 	tte_bit.v		/* use < 0 check in asm */
#define	tte_size	tte_bit.sz
#define	tte_nfo		tte_bit.nfo
#define	tte_ie		tte_bit.ie		/* XXX? */
#define	tte_hmenum	tte_bit.hmenum
#define	tte_lckcnt	tte_bit.lckcnt
#define	tte_pahi	tte_bit.pahi
#define	tte_palo	tte_bit.palo
#define	tte_ref		tte_bit.ref
#define	tte_wr_perm	tte_bit.wr_perm
#define	tte_no_sync	tte_bit.no_sync
#define	tte_lock	tte_bit.l
#define	tte_cp		tte_bit.cp
#define	tte_cv		tte_bit.cv
#define	tte_se		tte_bit.e
#define	tte_priv	tte_bit.p
#define	tte_hwwr	tte_bit.w
#define	tte_glb		tte_bit.g

#define	tte_inthi	tte_int.inthi
#define	tte_intlo	tte_int.intlo

#endif /* !_ASM */

/* Defines for sz field in tte */
#define	TTE8K			0x0
#define	TTE64K			0x1
#define	TTE512K			0x2
#define	TTE4M			0x3
#define	TTESZ_VALID		0x4

#define	TTE_SZ_SHFT_INT		29
#define	TTE_SZ_SHFT		32+29
#define	TTE_SZ_BITS		0x3

/*
 * the tte lock cnt now lives in the hme blk and is 16 bits long. See
 * comments in hme_blk declaration.
 */
#define	MAX_TTE_LCKCNT		(0x10000 - 1)

#define	TTE_BSZS_SHIFT(sz)	((sz) * 3)
#define	TTEBYTES(sz)		(MMU_PAGESIZE << TTE_BSZS_SHIFT(sz))
#define	TTEPAGES(sz)		(1 << TTE_BSZS_SHIFT(sz))
#define	TTE_PAGE_SHIFT(sz)	(MMU_PAGESHIFT + TTE_BSZS_SHIFT(sz))
#define	TTE_PAGE_OFFSET(sz)	(TTEBYTES(sz) - 1)
#define	TTE_PAGEMASK(sz)	(~TTE_PAGE_OFFSET(sz))
#define	TTE_PFNMASK(sz)		(~(TTE_PAGE_OFFSET(sz) >> MMU_PAGESHIFT))

#define	TTE_PA_LSHIFT		23	/* used to do sllx on tte to get pa */

#ifndef _ASM

#define	TTE_PASHIFT		19	/* used to manage pahi and palo */
#define	TTE_PALOMASK		((1 << TTE_PASHIFT) -1)
/* PFN is defined as bits [40-13] of the physical address */
#define	TTE_TO_TTEPFN(ttep)						\
	((((ttep)->tte_pahi << TTE_PASHIFT) | (ttep)->tte_palo) &	\
	TTE_PFNMASK((ttep)->tte_size))
/*
 * This define adds the vaddr page offset to obtain a correct pfn
 */
#define	TTE_TO_PFN(vaddr, ttep)						\
	(sfmmu_ttetopfn(ttep, vaddr))

#define	PFN_TO_TTE(entry, pfn) {		\
	entry.tte_pahi = pfn >> TTE_PASHIFT;	\
	entry.tte_palo = pfn & TTE_PALOMASK;	\
	}

#endif /* !_ASM */
/* Defines for tte using inthi */
#define	TTE_VALID_INT			0x80000000
#define	TTE_NFO_INT			0x10000000
#define	TTE_NFO_SHIFT			0x3	/* makes for an easy check */
#define	TTE_IE_INT			0x08000000

/* Defines for tte using intlo */
#define	TTE_REF_INT			0x00000400
#define	TTE_WRPRM_INT			0x00000200
#define	TTE_NOSYNC_INT			0x00000100
#define	TTE_LCK_INT			0x00000040
#define	TTE_CP_INT			0x00000020
#define	TTE_CV_INT			0x00000010
#define	TTE_SIDEFF_INT			0x00000008
#define	TTE_PRIV_INT			0x00000004
#define	TTE_HWWR_INT			0x00000002
#define	TTE_GLB_INT			0x00000001

#define	TTE_PROT_INT			(TTE_WRPRM_INT | TTE_PRIV_INT)

#ifndef ASM

/* Defines to help build ttes using inthi */
#define	TTE_SZ_INT(sz)			((sz) << TTE_SZ_SHFT_INT)
#define	TTE_HMENUM_INT(hmenum)		((hmenum) << 24)
/* PFN is defined as bits [40-13] of the physical address */
#define	TTE_PFN_INTHI(pfn)		((pfn) >> TTE_PASHIFT)
#define	TTE_IE_CHECK(flags)	\
	(((flags) & HAT_STRUCTURE_LE) ? TTE_IE_INT : 0)

/* Defines to help build ttes using intlo */
#define	TTE_PFN_INTLO(pfn)		(((pfn) & TTE_PALOMASK) << 13)
#define	TTE_WRPRM_CHECK(vprot)	 \
	(((vprot) & PROT_WRITE) ? TTE_WRPRM_INT : 0)
#define	TTE_NOSYNC_CHECK(flags)	 \
	(((flags) & HAT_NOSYNCLOAD) ? TTE_NOSYNC_INT : 0)
#define	TTE_CP_CHECK(flags)	\
	(((flags) & SFMMU_UNCACHEPTTE) ? 0: TTE_CP_INT)
#define	TTE_CV_CHECK(flags)	\
	(((flags) & SFMMU_UNCACHEVTTE) ? 0: TTE_CV_INT)
#define	TTE_SE_CHECK(flags)	\
	(((flags) & SFMMU_SIDEFFECT) ? TTE_SIDEFF_INT : 0)
#define	TTE_PRIV_CHECK(vprot)	\
	(((vprot) & PROT_USER) ? 0 : TTE_PRIV_INT)
#define	TTE_NFO_CHECK(flags)	\
	(((flags) & HAT_NOFAULT) ? TTE_NFO_INT : 0)

#define	MAKE_TTE_INTHI(sz, hmenum, pfn, flags)				\
	(TTE_VALID_INT | TTE_NFO_CHECK(flags) | TTE_IE_CHECK(flags) |	\
	TTE_SZ_INT(sz) | TTE_HMENUM_INT(hmenum) | TTE_PFN_INTHI(pfn))

#define	MAKE_TTE_INTLO(pfn, vprot, flags)				\
	(TTE_PFN_INTLO(pfn) | TTE_REF_INT | TTE_WRPRM_CHECK(vprot) |	\
	TTE_NOSYNC_CHECK(flags) | TTE_CP_CHECK(flags) |			\
	TTE_CV_CHECK(flags) | TTE_SE_CHECK(flags) | TTE_PRIV_CHECK(vprot))

/*
 * Defines to check/set TTE bits.
 */
#define	TTE_IS_VALID(ttep)	((ttep)->tte_inthi < 0)
#define	TTE_SET_INVALID(ttep)	((ttep)->tte_val = 0)
#define	TTE_IS_8K(ttep)		((ttep)->tte_size == TTE8K)
#define	TTE_IS_WRITABLE(ttep)	((ttep)->tte_wr_perm)
#define	TTE_IS_PRIVILEGED(ttep)	((ttep)->tte_priv)
#define	TTE_IS_NOSYNC(ttep)	((ttep)->tte_no_sync)
#define	TTE_IS_LOCKED(ttep)	((ttep)->tte_lock)
#define	TTE_IS_GLOBAL(ttep)	((ttep)->tte_glb)
#define	TTE_IS_SIDEFFECT(ttep)	((ttep)->tte_se)
#define	TTE_IS_NFO(ttep)	((ttep)->tte_nfo)

#define	TTE_IS_REF(ttep)	((ttep)->tte_ref)
#define	TTE_IS_MOD(ttep)	((ttep)->tte_hwwr)
#define	TTE_SET_REF(ttep)	((ttep)->tte_ref = 1)
#define	TTE_CLR_REF(ttep)	((ttep)->tte_ref = 0)
#define	TTE_SET_MOD(ttep)	((ttep)->tte_hwwr = 1)
#define	TTE_CLR_MOD(ttep)	((ttep)->tte_hwwr = 0)
#define	TTE_SET_RM(ttep)						\
	(((ttep)->tte_intlo) = (ttep)->tte_intlo | TTE_HWWR_INT | TTE_REF_INT)
#define	TTE_CLR_RM(ttep)						\
	(((ttep)->tte_intlo) = (ttep)->tte_intlo &			\
	~(TTE_HWWR_INT | TTE_REF_INT))

#define	TTE_SET_WRT(ttep)	((ttep)->tte_wr_perm = 1)
#define	TTE_CLR_WRT(ttep)	((ttep)->tte_wr_perm = 0)
#define	TTE_SET_PRIV(ttep)	((ttep)->tte_priv = 1)
#define	TTE_CLR_PRIV(ttep)	((ttep)->tte_priv = 0)

#define	TTE_IS_VCACHEABLE(ttep)		((ttep)->tte_cv)
#define	TTE_SET_VCACHEABLE(ttep)	((ttep)->tte_cv = 1)
#define	TTE_CLR_VCACHEABLE(ttep)	((ttep)->tte_cv = 0)
#define	TTE_IS_PCACHEABLE(ttep)		((ttep)->tte_cp)
#define	TTE_SET_PCACHEABLE(ttep)	((ttep)->tte_cp = 1)
#define	TTE_CLR_PCACHEABLE(ttep)	((ttep)->tte_cp = 0)


/*
 * This define provides a generic method to set and clear multiple tte flags.
 * A bitmask of all flags to be affected is passed in "flags" and a bitmask
 * of the new values is passed in "newflags".
 */
#define	TTE_SET_LOFLAGS(ttep, flags, newflags)				\
	((ttep)->tte_intlo = ((ttep)->tte_intlo & ~(flags)) | (newflags))

#define	TTE_GET_LOFLAGS(ttep, flags)	((ttep)->tte_intlo & flags)

#endif /* !_ASM */

#if !defined(_ASM) && defined(_KERNEL)
extern  char Syslimit[];
#endif /* !defined(_ASM) && defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_PTE_H */
