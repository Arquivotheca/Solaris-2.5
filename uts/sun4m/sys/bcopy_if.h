/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef _SYS_BCOPY_IF_H
#define	_SYS_BCOPY_IF_H

#pragma ident	"@(#)bcopy_if.h	1.4	93/04/13 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/* #define	OPTIMIZER_BUG */

/* ---------------------------------------------------------------------- */
/*
 * This #define	is used at compile time to determine what is the default
 * *bcopy* set of routines.  This doesn't affect HW *Block* copy presence,
 * however.  To use the hwbcopy set of routines *by default*, then #define
 * USE_HW_BCOPY.  Else, to have them compiled in as hw_bcopy, etc... and thus
 * not used by default, comment out the "#define USE_HW_BCOPY" line.
 * Also see sun4m/ml/copy.s for related #defines.
 */

/* #define	USE_HW_BCOPY */

#ifndef _ASM
#ifdef USE_HW_BCOPY

#define	hw_bcopy	bcopy
#define	hw_bzero	bzero
#define	hw_kcopy	kcopy
#define	hw_copyin	copyin
#define	hw_copyout	copyout
#define	hw_xcopyin	xcopyin
#define	hw_xcopyout	xcopyout

#define	SWBCOPY		bcopy_asm
#define	SWBZERO		bzero_asm
#define	SWKCOPY		kcopy_asm
#define	SWCOPYIN	copyin_asm
#define	SWCOPYOUT	copyout_asm
#define	SWXCOPYIN	xcopyin_asm
#define	SWXCOPYOUT	xcopyout_asm

#else	/* USE_HW_BCOPY */

#define	SWBCOPY		bcopy
#define	SWBZERO		bzero
#define	SWKCOPY		kcopy
#define	SWCOPYIN	copyin
#define	SWCOPYOUT	copyout
#define	SWXCOPYIN	xcopyin
#define	SWXCOPYOUT	xcopyout

#endif	/* USE_HW_BCOPY */

extern int defhwbcopy, hwbcopy;
extern int mxcc;

#include <sys/vm_machparam.h>

extern void hwbc_init(void);
extern void hwbc_scan(u_int blks, pa_t src);
extern void hwbc_fill(u_int blks, pa_t dest, pa_t pattern);
extern void hwbc_copy(u_int blks, pa_t src, pa_t dest);
extern void hwpage_scan(u_int pfn);
extern void hwpage_zero(u_int pfn);
extern void hwpage_fill(u_int pfn, pa_t pattern);
extern void hwpage_copy(u_int spfn, u_int dpfn);
extern void hwblk_zero(u_int blks, pa_t dest);

extern void hw_bcopy(caddr_t, caddr_t, size_t);
extern void hw_bzero(caddr_t, size_t);
extern int  hw_kcopy(caddr_t, caddr_t, size_t);
extern int  hw_copyin(caddr_t, caddr_t, size_t);
extern int  hw_copyout(caddr_t, caddr_t, size_t);
extern int  hw_xcopyin(caddr_t, caddr_t, size_t);
extern int  hw_xcopyout(caddr_t, caddr_t, size_t);

#endif	/* !_ASM */


#define	BLOCK_SIZE		32		/* Must be a power of 2 */
#define	BLOCK_MASK		(BLOCK_SIZE-1)
#define	BLOCK_SIZE_SHIFT	5	/* log2(BLOCK_SIZE) */
#define	BLOCKS_PER_PAGE		(PAGESIZE >> BLOCK_SIZE_SHIFT)
#define	PFN_ENLARGE(pfn) 	((pa_t) pfn << PAGESHIFT)
#define	ROUNDUP1(v, size)	((v) + ((size)-1) & ~((size-1)))
#define	ROUNDUP2(a, size)	((caddr_t) \
				(((u_int) (a) + (size)) & ~((size)-1)))


/* ---------------------------------------------------------------------- */

#ifdef BCSTAT

typedef struct histosize {	/* 20 words */
	u_int bucket[4];
	u_int sbucket[16];
} histo_t;


typedef struct callerl {	/* 2 words */
	caddr_t caller;
	u_int count;
} callerl_t;


#define	CALLERLSIZE	64

typedef struct {

	/* 16 words: */
	u_int totalbcopy;
	u_int totalkcopy;
	u_int totalcopyin;
	u_int totalcopyout;
	u_int totalbzero;

	u_int bctoosmall;
	u_int kctoosmall;
	u_int citoosmall;
	u_int cotoosmall;
	u_int bztoosmall;

	u_int bcnotaligned;
	u_int kcnotaligned;
	u_int cinotaligned;
	u_int conotaligned;

	u_int filler[2];

/* bcstat+0x40 */
	histo_t bcsize;		/* 20 words */
	histo_t kcsize;		/* ditto... */
	histo_t cisize;
	histo_t cosize;

	histo_t bzsize;

/* bcstat+0x1d0 */
	histo_t bcusehw;
	histo_t bzusehw;

	histo_t src_mem;
	histo_t src_io;
	histo_t dest_mem;
	histo_t dest_io;

	callerl_t toosmall[CALLERLSIZE];	/* 128 words */
	callerl_t usehw[CALLERLSIZE];		/* 128 words */
	callerl_t notaligned[CALLERLSIZE];	/* 128 words */

	kmutex_t lock;			/* lock for this data structure */
} bcopy_stat_t;

/*
 * Masks for bcstaton:
 */
#define	BCTOTALS		0x0001
#define	BCTYPESTATS		0x0002
#define	BCSIZESTATS		0x0004
#define	BCCALLERSTATS		0x0008
#define	BCSMALLCALLER		0x0010

void insertcaller(callerl_t *,  caddr_t);
void bc_statsize(histo_t *, int);
void bc_stattype(caddr_t, caddr_t, int, int);
extern caddr_t caller();
extern int bcstaton;
extern bcopy_stat_t bcstat;

#define	BC_STAT(categ)		if (bcstaton & BCTOTALS) { \
					mutex_enter(&bcstat.lock); \
					bcstat.categ++; \
					mutex_exit(&bcstat.lock); \
				}

#define	BC_STAT_SIZE(categ, bytes) \
				if (bcstaton & BCSIZESTATS) \
					bc_statsize(&bcstat.categ, bytes);

#define	BC_STAT_CALLER(clist)	if (bcstaton & BCCALLERSTATS) { \
					mutex_enter(&bcstat.lock); \
					insertcaller(bcstat.clist, caller()); \
					mutex_exit(&bcstat.lock); \
				}

#define	BC_STAT_SMALLCALLER(size)	if (bcstaton & BCSMALLCALLER && \
					    size <= bcmin) { \
					mutex_enter(&bcstat.lock); \
					insertcaller(bcstat.toosmall, \
						caller()); \
					mutex_exit(&bcstat.lock); \
				}

#define	BC_STAT_TYPE	if (bcstaton & BCTYPESTATS) \
				bc_stattype(from, to, count, 0);

#define	BZ_STAT_TYPE	if (bcstaton & BCTYPESTATS) \
				bc_stattype(0, addr, count, 1);

#else
#define	BC_STAT(x)
#define	BC_STAT_SIZE(x, y)
#define	BC_STAT_CALLER(x)
#define	BC_STAT_SMALLCALLER(x)
#define	BC_STAT_TYPE
#define	BZ_STAT_TYPE
#endif	/* BCSTAT */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BCOPY_IF_H */
