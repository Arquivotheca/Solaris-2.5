/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1989 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_REGSET_H
#define	_SYS_REGSET_H

#pragma ident	"@(#)regset.h	1.13	94/11/21 SMI"	/* SVr4.0 1.1	*/

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Location of the users' stored registers relative to R0.
 * Usage is as an index into a gregset_t array or as u.u_ar0[XX].
 */
#define	REG_PSR	(0)
#define	REG_PC	(1)
#define	REG_nPC	(2)
#define	REG_Y	(3)
#define	REG_G1	(4)
#define	REG_G2	(5)
#define	REG_G3	(6)
#define	REG_G4	(7)
#define	REG_G5	(8)
#define	REG_G6	(9)
#define	REG_G7	(10)
#define	REG_O0	(11)
#define	REG_O1	(12)
#define	REG_O2	(13)
#define	REG_O3	(14)
#define	REG_O4	(15)
#define	REG_O5	(16)
#define	REG_O6	(17)
#define	REG_O7	(18)

/* the following defines are for portability */
#define	REG_PS	REG_PSR
#define	REG_SP	REG_O6
#define	REG_R0	REG_O0
#define	REG_R1	REG_O1

#ifndef	_ASM

typedef int	greg_t;

/*
 * A gregset_t is defined as an array type for compatibility with the reference
 * source. This is important due to differences in the way the C language
 * treats arrays and structures as parameters.
 *
 * Note that NGREG is really (sizeof (struct regs) / sizeof (greg_t)),
 * but that the ABI defines it absolutely to be 19.
 */
#define	NGREG	19

typedef greg_t	gregset_t[NGREG];

/*
 * The following structures define how a register window can appear on the
 * stack. This structure is available (when required) through the `gwins'
 * field of an mcontext (nested within ucontext). SPARC_MAXWINDOW is the
 * maximum number of outstanding regiters window defined in the SPARC
 * architecture (*not* implementation).
 */
#define	SPARC_MAXREGWINDOW	31		/* max windows in SPARC arch. */

struct	rwindow {
	greg_t	rw_local[8];		/* locals */
	greg_t	rw_in[8];		/* ins */
};

#define	rw_fp	rw_in[6]		/* frame pointer */
#define	rw_rtn	rw_in[7]		/* return address */

struct gwindows {
	int		wbcnt;
	int		*spbuf[SPARC_MAXREGWINDOW];
	struct rwindow	wbuf[SPARC_MAXREGWINDOW];
};

typedef struct gwindows	gwindows_t;

/*
 * Floating point definitions.
 */

#define	MAXFPQ	16	/* max # of fpu queue entries currently supported */

/*
 * struct fq defines the minimal format of a floating point instruction queue
 * entry. The size of entries in the floating point queue are implementation
 * dependent. The union FQu is guarenteed to be the first field in any ABI
 * conformant system implementation. Any additional fields provided by an
 * implementation should not be used applications designed to be ABI conformant.
 */

struct fpq {
	unsigned long *fpq_addr;	/* address */
	unsigned long fpq_instr;	/* instruction */
};

struct fq {
	union {				/* FPU inst/addr queue */
		double whole;
		struct fpq fpq;
	} FQu;
};

/*
 * struct fpu is the floating point processor state. struct fpu is the sum
 * total of all possible floating point state which includes the state of
 * external floating point hardware, fpa registers, etc..., if it exists.
 *
 * A floating point instuction queue may or may not be associated with
 * the floating point processor state. If a queue does exist, the field
 * fpu_q will point to an array of fpu_qcnt entries where each entry is
 * fpu_q_entrysize long. fpu_q_entry has a lower bound of sizeof (union FQu)
 * and no upper bound. If no floating point queue entries are associated
 * with the processor state, fpu_qcnt will be zeo and fpu_q will be NULL.
 */

#define	FPU_REGS_TYPE		unsigned
#define	FPU_DREGS_TYPE		unsigned long long
#define	V7_FPU_FSR_TYPE		unsigned
#define	V9_FPU_FSR_TYPE		unsigned long long
#define	V9_FPU_FPRS_TYPE	unsigned

#ifdef	__sparcv9
#define	FPU_FSR_TYPE		V9_FPU_FSR_TYPE
#else
#define	FPU_FSR_TYPE		V7_FPU_FSR_TYPE
#endif

struct fpu {
	union {					/* FPU floating point regs */
		FPU_REGS_TYPE	fpu_regs[32];	/* 32 singles */
		double		fpu_dregs[16];	/* 16 doubles */
	} fpu_fr;
	struct fq	*fpu_q;			/* ptr to array of FQ entries */
	V7_FPU_FSR_TYPE	fpu_fsr;		/* FPU status register */
	unsigned char	fpu_qcnt;		/* # of entries in saved FQ */
	unsigned char	fpu_q_entrysize;	/* # of bytes per FQ entry */
	unsigned char	fpu_en;			/* flag signifying fpu in use */
};

typedef struct fpu	fpregset_t;

#ifdef __sparcv9
/*
 * struct v9_fpu contains the extra V9 floating point processor state.
 * This is a separate definition because struct fpu is defined by the
 * V8 ABI. Note that the fprs could be 64 bits, but that seems excessive
 * considering its current V9 definition of 3 bits.
 */

struct v9_fpu {
	union {				/* V9 FPU floating point regs */
		FPU_REGS_TYPE	fpu_regs[32];	/* 32 singles */
		FPU_DREGS_TYPE	fpu_dregs[32];	/* 32 doubles */
		long double	fpu_qregs[16];	/* 16 quads */
	} fpu_fr;
	V9_FPU_FSR_TYPE	fpu_fsr;		/* FPU status register */
	V9_FPU_FPRS_TYPE fpu_fprs;		/* fprs register */
	struct fq	*fpu_q;
	unsigned char	fpu_qcnt;
	unsigned char	fpu_q_entrysize;
	unsigned char	fpu_en;			/* flag signifying fpu in use */
};

typedef struct v9_fpu	v9_fpregset_t;

#endif

#ifdef	_KERNEL
/*
 * The abi uses struct fpu, so we use this to describe the kernel's
 * view of the fpu, which changes from v7 to v9.
 */
#ifdef	__sparcv9
typedef	struct v9_fpu	kfpu_t;
#else
typedef	struct fpu	kfpu_t;
#endif
#endif

/*
 * The following structure is for associating extra register state with
 * the ucontext structure and is kept within the uc_mcontext filler area.
 *
 * If (xrs_id == XRS_ID) then the xrs_ptr field is a valid pointer to
 * extra register state. The exact format of the extra register state
 * pointed to by xrs_ptr is platform-dependent.
 *
 * Note: a platform may or may not manage extra register state.
 */
typedef struct {
	unsigned int		xrs_id;		/* indicates xrs_ptr validity */
	caddr_t			xrs_ptr;	/* ptr to extra reg state */
} xrs_t;

#define	XRS_ID			0x78727300	/* the string "xrs" */


/*
 * Structure mcontext defines the complete hardware machine state. If
 * the field `gwins' is non NULL, it points to a save area for register
 * window frames. If `gwins' is NULL, the register windows were saved
 * on the user's stack.
 *
 * The filler of 21 longs is historical. The value was selected to provide
 * binary compatibility with statically linked ICL binaries. It is in the
 * ABI (do not change). It actually appears in the ABI as a single filler of
 * 44 is in the field uc_filler of struct ucontext. It is split here so that
 * ucontext.h can (hopefully) remain architecture independent.
 *
 * Note that 2 longs of the filler are used to hold extra register state info.
 */
typedef struct {
	gregset_t	gregs;	/* general register set */
	gwindows_t	*gwins;	/* POSSIBLE pointer to register windows */
	fpregset_t	fpregs;	/* floating point register set */
	xrs_t		xrs;	/* POSSIBLE extra register state association */
	long		filler[19];
} mcontext_t;

#endif	/* _ASM */

#ifndef	_KERNEL
#include <v7/sys/privregs.h>
#endif


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_REGSET_H */
