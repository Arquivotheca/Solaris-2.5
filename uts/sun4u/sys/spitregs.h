/*
 * Copyright (c) 1986 by Sun Microsystems, Inc.
 */

#ifndef _SYS_SPITREGS_H
#define	_SYS_SPITREGS_H

#pragma ident	"@(#)spitregs.h	1.7	95/06/05 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This file is cpu dependent.
 */

#ifdef _KERNEL

#include <sys/asi.h>
#include <sys/machparam.h>

#define	BSTORE_FPREGS(FP) \
	wr	%g0, ASI_BLK_P, %asi; \
	stda	%d0, [FP] %asi; \
	stda	%d16, [FP + 64] %asi; \
	stda	%d32, [FP + 128] %asi; \
	stda	%d48, [FP + 192] %asi;

#define	BSTORE_V8_FPREGS(FP) \
	wr	%g0, ASI_BLK_P, %asi; \
	stda	%d0, [FP] %asi; \
	stda	%d16, [FP + 64] %asi;

#define	BSTORE_V8P_FPREGS(FP) \
	wr	%g0, ASI_BLK_P, %asi; \
	stda	%d32, [FP + 128] %asi; \
	stda	%d48, [FP + 192] %asi;

#define	BLOAD_FPREGS(FP) \
	wr	%g0, ASI_BLK_P, %asi; \
	ldda	[FP] %asi, %d0; \
	ldda	[FP + 64] %asi, %d16; \
	ldda	[FP + 128] %asi, %d32; \
	ldda	[FP + 192] %asi, %d48;

#define	BLOAD_V8_FPREGS(FP) \
	wr	%g0, ASI_BLK_P, %asi; \
	ldda	[FP] %asi, %d0; \
	ldda	[FP + 64] %asi, %d16;

#define	BLOAD_V8P_FPREGS(FP) \
	wr	%g0, ASI_BLK_P, %asi; \
	ldda	[FP + 128] %asi, %d32; \
	ldda	[FP + 192] %asi, %d48;

#define	BZERO_FPREGS(FP) \
	wr	%g0, ASI_BLK_P, %asi; \
	ldda	[FP] %asi, %d0; \
	ldda	[FP] %asi, %d16; \
	ldda	[FP] %asi, %d32; \
	ldda	[FP] %asi, %d48;

#define	GSR_SIZE 8	/* Graphics Status Register size 64 bits */

/*
 * LSU Control Register
 *
 * +------+----+----+----+----+----+----+-----+------+----+----+----+---+
 * | Resv | PM | VM | PR | PW | VR | VW | Rsv |  FM  | DM | IM | DC | IC|
 * +------+----+----+----+----+----+----+-----+------+----+----+----+---+
 *  63  41   33   25   24   23	 22   21   20  19   4	3    2	  1   0
 *
 */

#define	LSU_IC		0x00000001	/* icache enable */
#define	LSU_DC		0x00000002	/* dcache enable */
#define	LSU_IM		0x00000004	/* immu enable */
#define	LSU_DM		0x00000008	/* dmmu enable */
#define	LSU_FM		0x000FFFF0	/* parity mask */
#define	LSU_VW		0x00200000	/* virtual watchpoint write enable */
#define	LSU_VR		0x00400000	/* virtual watchpoint read enable */
#define	LSU_PW		0x00800000	/* phys watchpoint write enable */
#define	LSU_PR		0x01000000	/* phys watchpoint read enable */

/*
 * Defines for the different types of dcache_flush
 * it is stored in dflush_type
 */
#define	FLUSHALL_TYPE	0x0		/* blasts all cache lines */
#define	FLUSHMATCH_TYPE	0x1		/* flush entire cache but check each */
					/* each line for a match */
#define	FLUSHPAGE_TYPE	0x2		/* flush only one page and check */
					/* each line for a match */

/*
 * D-Cache Tag Data Register
 *
 * +----------+--------+----------+
 * | Reserved | DC_Tag | DC_Valid |
 * +----------+--------+----------+
 *  63	    30 29    2	1	 0
 *
 */
#define	ICACHE_FLUSHSZ	0x8
#define	DC_PTAG_SHIFT	34
#define	DC_LINE_SHIFT	30
#define	DC_VBIT_SHIFT	2
#define	DC_VBIT_MASK	0x3
#define	IC_LINE_SHIFT	3
#define	IC_LINE		512
#define	INDEX_BIT_SHIFT	13

#ifndef _ASM

extern	int dflush_type;

#endif	/* !_ASM */

#ifdef _ASM

/*
 * XXX Having to cstyle this makes it a lot worse
 */
#define	DCACHE_FLUSH(arg1, arg2, tmp1, tmp2, tmp3)			\
	ldxa	[%g0]ASI_LSU, tmp1; \
	btst	LSU_DC, tmp1;		/* is dcache enabled? */	\
	/* CSTYLED */							\
	bz,pn	%icc, 1f; \
	sethi	%hi(dcache_linesize), tmp1; \
	ld	[tmp1 + %lo(dcache_linesize)], tmp1; \
	sethi	%hi(dflush_type), tmp2; \
	ld	[tmp2 + %lo(dflush_type)], tmp2; \
	cmp	tmp2, FLUSHPAGE_TYPE; \
	/* CSTYLED */							\
	be,pt	%icc, 2f; \
	sllx	arg1, DC_VBIT_SHIFT, arg1;	/* tag to compare */	\
	sethi	%hi(dcache_size), tmp3; \
	ld	[tmp3 + %lo(dcache_size)], tmp3; \
	cmp	tmp2, FLUSHMATCH_TYPE; \
	/* CSTYLED */							\
	be,pt	%icc, 3f; \
	nop; \
	/*								\
	 * flushtype = FLUSHALL_TYPE, flush the whole thing		\
	 * tmp3 = cache size						\
	 * tmp1 = cache line size					\
	 */								\
	sub	tmp3, tmp1, tmp2; \
4:									\
	stxa	%g0, [tmp2]ASI_DC_TAG; \
	membar	#Sync; \
	cmp	%g0, tmp2; \
	/* CSTYLED */							\
	bne,pt	%icc, 4b; \
	sub	tmp2, tmp1, tmp2; \
	/* CSTYLED */							\
	ba,pt	%icc, 1f; \
	nop; \
	/*								\
	 * flushtype = FLUSHPAGE_TYPE					\
	 * arg1 = tag to compare against				\
	 * arg2 = virtual color						\
	 * tmp1 = cache line size					\
	 * tmp2 = tag from cache					\
	 * tmp3 = counter						\
	 */								\
2:									\
	set	MMU_PAGESIZE, tmp3; \
	sllx	arg2, MMU_PAGESHIFT, arg2; /* color to dcache page */	\
	sub	tmp3, tmp1, tmp3; \
4:									\
	ldxa	[arg2 + tmp3]ASI_DC_TAG, tmp2;	/* read tag */		\
	btst	DC_VBIT_MASK, tmp2; \
	/* CSTYLED */							\
	bz,pn	%icc, 5f;	  /* branch if no valid sub-blocks */	\
	andn	tmp2, DC_VBIT_MASK, tmp2;	/* clear out v bits */	\
	cmp	tmp2, arg1;\
	/* CSTYLED */							\
	bne,pn	%icc, 5f;			/* br if tag miss */	\
	nop; \
	stxa	%g0, [arg2 + tmp3]ASI_DC_TAG; \
	membar	#Sync; \
5:									\
	cmp	%g0, tmp3; \
	/* CSTYLED */							\
	bnz,pt	%icc, 4b;		/* branch if not done */	\
	sub	tmp3, tmp1, tmp3; \
	/* CSTYLED */							\
	ba,pt	%icc, 1f; \
	nop; \
	/*								\
	 * flushtype = FLUSHMATCH_TYPE					\
	 * arg1 = tag to compare against				\
	 * tmp1 = cache line size					\
	 * tmp3 = cache size						\
	 * arg2 = counter						\
	 * tmp2 = cache tag						\
	 */								\
3:									\
	sub	tmp3, tmp1, arg2; \
4:									\
	ldxa	[arg2]ASI_DC_TAG, tmp2;		/* read tag */		\
	btst	DC_VBIT_MASK, tmp2; \
	/* CSTYLED */							\
	bz,pn	%icc, 5f;		/* br if no valid sub-blocks */	\
	andn	tmp2, DC_VBIT_MASK, tmp2;	/* clear out v bits */	\
	cmp	tmp2, arg1; \
	/* CSTYLED */							\
	bne,pn	%icc, 5f;		/* branch if tag miss */	\
	nop; \
	stxa	%g0, [arg2]ASI_DC_TAG; \
	membar	#Sync; \
5:									\
	cmp	%g0, arg2; \
	/* CSTYLED */							\
	brnz,pt %icc, 4b;		/* branch if not done */	\
	sub	arg2, tmp1, arg2; \
1:

#endif /* _ASM */

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SPITREGS_H */
