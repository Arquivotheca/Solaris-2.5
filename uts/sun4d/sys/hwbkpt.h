/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_HWBKPT_H
#define	_SYS_HWBKPT_H

#pragma ident	"@(#)hwbkpt.h	1.13	93/04/13 SMI"

/*
 * Support for SuperSPARC breakpoints.  Naming conventions:
 *
 *	bkv:	breakpoint value
 *	bkm:	breakpoint mask
 *	bkc:	breakpoint control
 *	bks:	breakpoint status
 *	ctrv:	counter breakpoint value
 *	ctrc:	counter breakpoint control
 *	ctrs:	counter breakpoint status
 *	action:	breakpoint action register
 */

#ifdef	__cplusplus
extern "C" {
#endif

#ifndef _ASM

typedef unsigned long long u_ll;

/*
 * Prototypes for assembler functions in ml/subr_4d.s that access
 * the SuperSPARC breakpoint registers.
 */

u_ll get_bkv(void);
u_ll get_bkm(void);
u_ll get_bkc(void);
u_ll get_bks(void);

u_long get_ctrv(void);
u_long get_ctrc(void);
u_long get_ctrs(void);

u_long get_action(void);

void set_bkv(u_ll);
void set_bkm(u_ll);
void set_bkc(u_ll);
void set_bks(u_ll);

void set_ctrv(u_long);
void set_ctrc(u_long);
void set_ctrs(u_long);

void set_action(u_long);

#define	R regs		/* so ctags doesn't get confused about the regs tag */
typedef void (*bkpt_func_t)(
	struct R *,		/* pointer to saved regs */
	caddr_t vaddr,		/* virtual address */
	u_ll paddr,		/* phys addr (if phys bkpt or IKBP_XLATE) */
	u_ll pmatch,		/* phys addr to match (only for IKBP_XLATE) */
	int size,		/* size of access: 1, 2, 4, or 8 */
	int type		/* type of access: IKBP_READ/IKBP_WRITE */
);

int check_hwbkpt(struct regs *, caddr_t addr);

int ikbp_set_bkpt(int flags, bkpt_func_t func, ...);
void ikbp_clr_bkpt(void);
int ibkp_overlap(caddr_t v1, int s1, caddr_t v2, int s2);
int ibkp_overlap_phys(u_ll p1, int s1, u_ll p2, int s2);

#define	ALL_CTX		(-1)

#define	MASK_36		((u_ll) 0xFFFFFFFFF)		/* nine F's */
#define	MASK_32		((u_ll) ((u_int) 0xFFFFFFFF))	/* eight F's */

#endif	/* _ASM */

#define	MDIAG_ASI	0x38		/* diagnostic space */
#define	CTRV_ASI	0x49		/* counter value */
#define	CTRC_ASI	0x4a		/* counter control */
#define	CTRS_ASI	0x4b		/* counter status */
#define	ACTION_ASI	0x4c		/* breakpoint action */

/*
 * addresses used with MDIAG_ASI (SuperSPARC doc table 4-17)
 */

#define	MDIAG_BKV	(0 << 8)	/* breakpoint value */
#define	MDIAG_BKM	(1 << 8)	/* breakpoint mask */
#define	MDIAG_BKC	(2 << 8)	/* breakpoint control */
#define	MDIAG_BKS	(3 << 8)	/* breakpoint status */

/*
 * breakpoint control bits
 */

#define	BKC_CSPACE	(1 << 6)	/* code or data space */
#define	BKC_PAMD	(1 << 5)	/* physical or virtual address */
#define	BKC_CBFEN	(1 << 4)	/* code fault or interrupt */
#define	BKC_CBKEN	(1 << 3)	/* enable code breakpoints */
#define	BKC_DBFEN	(1 << 2)	/* data fault or interrupt */
#define	BKC_DBREN	(1 << 1)	/* enable data read breakpoint */
#define	BKC_DBWEN	(1 << 0)	/* enable data write breakpoint */

#define	BKC_MASK	((BKC_CSPACE << 1) - 1)

/*
 * breakpoint status bits
 */

#define	BKS_CBKIS	(1 << 3)	/* code interrupt generated */
#define	BKS_CBKFS	(1 << 2)	/* code fault */
#define	BKS_DBKIS	(1 << 1)	/* data interrupt */
#define	BKS_DBKFS	(1 << 0)	/* data fault */

#define	BKS_MASK	((BKS_CBKIS << 1) - 1)

/*
 * counter and action register stuff goes here ...
 */

/*
 * flags for ikbp_set_data_bkpt()
 */

#define	IKBP_READ	(1 << 0)	/* bkpt on reads */
#define	IKBP_WRITE	(1 << 1)	/* bkpt on writes */
#define	IKBP_CODE	(1 << 2)	/* code, as opposed to data, bkpt */
#define	IKBP_VIRT	(1 << 3)	/* virtual address bkpt */
#define	IKBP_XLATE	(1 << 4)	/* translate virt addr */

#define	IKBP_DATA	0		/* psuedo flag */
#define	IKBP_PHYS	0		/* psuedo flag */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_HWBKPT_H */
