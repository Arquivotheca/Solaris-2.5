/*
 * Copyright (c) 1988, 1990 by Sun Microsystems, Inc.
 *	All rights reserved.
 */

#ifndef	_SYS_FPU_FPU_SIMULATOR_H
#define	_SYS_FPU_FPU_SIMULATOR_H

#pragma ident	"@(#)fpu_simulator.h	1.23	95/02/08 SMI"
/* SunOS-4.0 1.10	*/

/*
 * Sparc floating-point simulator PUBLIC include file.
 */

#include <sys/types.h>
#include <sys/ieeefp.h>
#include <vm/seg.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*	PUBLIC TYPES	*/

enum fcc_type {			/* relationships */
	fcc_equal	= 0,
	fcc_less	= 1,
	fcc_greater	= 2,
	fcc_unordered	= 3
};

enum cc_type {			/* icc/fcc number */
	fcc0	= 0,
	fcc1	= 1,
	fcc2	= 2,
	fcc3	= 3,
	icc	= 4,
	xcc	= 6
};

/* FSR types. */

enum ftt_type {			/* types of traps */
	ftt_none	= 0,
	ftt_ieee	= 1,
	ftt_unfinished	= 2,
	ftt_unimplemented = 3,
	ftt_sequence	= 4,
	ftt_alignment	= 5,	/* defined by software convention only */
	ftt_fault	= 6,	/* defined by software convention only */
	ftt_7		= 7
};

#ifndef	__sparcv9
typedef	struct {		/* Sparc FSR. */
	unsigned /* enum fp_direction_type */ rd : 2; /* rounding direction */
	unsigned /* enum fp_precision_type */ rp : 2; /* rounding precision */
	unsigned			tem  :	5; /* trap enable mask */
	unsigned			:	6;
	unsigned /* enum ftt_type */	ftt  :	3; /* FPU trap type */
	unsigned			qne  :	1; /* FPQ not empty */
	unsigned			pr   :	1; /* partial result */
	unsigned /* enum fcc_type */	fcc  :	2; /* FPU condition code */
	unsigned			aexc :	5; /* accumulated exception */
	unsigned			cexc :	5; /* current exception */
} fsr_type;
#else
typedef	struct {		/* Sparc V9 FSR. */
	ulong_t					: 26;
	ulong_t				   fcc3	: 2; /* fp condition code 3 */
	ulong_t				   fcc2	: 2; /* fp condition code 2 */
	ulong_t				   fcc1	: 2; /* fp condition code 1 */
	ulong_t /* enum fp_direction_type */ rd	: 2; /* rounding direction */
	ulong_t					: 2; /* was rounding p. */
	ulong_t				    tem	: 5; /* trap enable mask */
	ulong_t				    ns	: 1; /* non-standard */
	ulong_t					: 5;
	ulong_t /* enum ftt_type */	    ftt	: 3; /* FPU trap type */
	ulong_t				    qne : 1; /* FPQ not empty */
	ulong_t				    pr	: 1; /* partial result */
	ulong_t /* enum fcc_type */	    fcc	: 2; /* fp condition code 0 */
	ulong_t				   aexc	: 5; /* accumulated exception */
	ulong_t				   cexc : 5; /* current exception */
} fsr_types;

/*
 * XXX - The following union and defines can be removed when the
 * compiler can support bitfields in a long long, as per fsr_types above.
 */

typedef union {
	fsr_types	fsr;
	u_longlong_t	ll;
} fsr_type;

#define	FCC3	fsr.fcc3
#define	FCC2	fsr.fcc2
#define	FCC1	fsr.fcc1
#define	RD	fsr.rd
#define	TEM	fsr.tem
#define	NS	fsr.ns
#define	FTT	fsr.ftt
#define	QNE	fsr.qne
#define	PR	fsr.pr
#define	FCC	fsr.fcc
#define	AEXC	fsr.aexc
#define	CEXC	fsr.cexc
#endif

typedef			/* FPU register viewed as single components. */
	struct {
	unsigned sign :		1;
	unsigned exponent :	8;
	unsigned significand : 23;
} single_type;

typedef			/* FPU register viewed as double components. */
	struct {
	unsigned sign :		1;
	unsigned exponent :    11;
	unsigned significand : 20;
} double_type;

typedef			/* FPU register viewed as extended components. */
	struct {
	unsigned sign :		 1;
	unsigned exponent :	15;
	unsigned significand :	16;
} extended_type;

typedef			/* FPU register with multiple data views. */
	union {
	int		int_reg;
	long		long_reg;
	long long	longlong_reg;
	unsigned	unsigned_reg;
	float		float_reg;
	single_type	single_reg;
	double_type	double_reg;
	extended_type	extended_reg;
} freg_type;

enum fp_op_type {		/* Type specifiers in FPU instructions. */
	fp_op_integer	= 0,	/* Not in hardware, but convenient to define. */
	fp_op_single	= 1,
	fp_op_double	= 2,
	fp_op_extended	= 3,
	fp_op_longlong	= 4
};

enum fp_opcode {	/* FPU op codes, minus precision and leading 0. */
	fmovs		= 0x0,
	fnegs		= 0x1,
	fabss		= 0x2,
	fp_op_3 = 3, fp_op_4 = 4, fp_op_5 = 5, fp_op_6 = 6, fp_op_7 = 7,
	fp_op_8		= 0x8,
	fp_op_9		= 0x9,
	fsqrt		= 0xa,
	fp_op_b = 0xb, fp_op_c = 0xc, fp_op_d = 0xd,
	fp_op_e = 0xe, fp_op_f = 0xf,
	fadd		= 0x10,
	fsub		= 0x11,
	fmul		= 0x12,
	fdiv		= 0x13,
	fcmp		= 0x14,
	fcmpe		= 0x15,
	fp_op_16 = 0x16, fp_op_17 = 0x17,
	fp_op_18	= 0x18,
	fp_op_19	= 0x19,
	fsmuld		= 0x1a,
	fdmulx		= 0x1b,
	ftoll		= 0x20,
	flltos		= 0x21,
	flltod		= 0x22,
	flltox		= 0x23,
	fp_op_24 = 0x24, fp_op_25 = 0x25, fp_op_26 = 0x26, fp_op_27 = 0x27,
	fp_op_28 = 0x28, fp_op_29 = 0x29, fp_op_2a = 0x2a, fp_op_2b = 0x2b,
	fp_op_2c = 0x2c, fp_op_2d = 0x2d, fp_op_2e = 0x2e, fp_op_2f = 0x2f,
	fp_op_30	= 0x30,
	fitos		= 0x31,
	fitod		= 0x32,
	fitox		= 0x33,
	ftoi		= 0x34,
	fp_op_35 = 0x35, fp_op_36 = 0x36, fp_op_37 = 0x37,
	ft_op_38	= 0x38,
	fp_op_39 = 0x39, fp_op_3a = 0x3a, fp_op_3b = 0x3b,
	fp_op_3c	= 0x3c,
	fp_op_3d = 0x3d, fp_op_3e = 0x3e, fp_op_3f = 0x3f
};

typedef			/* FPU instruction. */
	struct {
	unsigned		hibits	: 2;	/* Top two bits. */
	unsigned		rd	: 5;	/* Destination. */
	unsigned		op3	: 6;	/* Main op code. */
	unsigned		rs1	: 5;	/* First operand. */
	unsigned		ibit	: 1;	/* I format bit. */
	unsigned /* enum fp_opcode */  opcode : 6; /* Floating-point op code. */
	unsigned /* enum fp_op_type */ prec   : 2; /* Precision. */
	unsigned		rs2	: 5;	/* Second operand. */
} fp_inst_type;

typedef			/* Integer condition code. */
	struct {
	unsigned			: 28;	/* the unused part */
	unsigned		n	: 1;	/* Negative bit. */
	unsigned		z	: 1;	/* Zero bit. */
	unsigned		v	: 1;	/* Overflow bit. */
	unsigned		c	: 1;	/* Carry bit. */
} ccr_type;

typedef			/* FPU data used by simulator. */
	struct {
	unsigned		fp_fsrtem;
	enum fp_direction_type	fp_direction;
	enum fp_precision_type	fp_precision;
	unsigned		fp_current_exceptions;
	kfpu_t			* fp_current_pfregs;
	void			(* fp_current_read_freg) ();
	void			(* fp_current_write_freg) ();
#ifdef	__sparcv9
	void			(* fp_current_read_dreg) ();
	void			(* fp_current_write_dreg) ();
#endif
	int			fp_trapcode;
	char			*fp_trapaddr;
	struct	regs		*fp_traprp;
	enum	seg_rw		fp_traprw;
} fp_simd_type;

/* PUBLIC FUNCTIONS */

#ifdef	__STDC__

extern enum ftt_type fpu_simulator(fp_simd_type *, fp_inst_type *, fsr_type *,
					int);

/*
 * fpu_simulator simulates FPU instructions only; reads and writes FPU data
 * registers directly.
 */

extern enum ftt_type fp_emulator(fp_simd_type *, fp_inst_type *, struct regs *,
				struct rwindow *, kfpu_t *);

/*
 * fp_emulator simulates FPU and CPU-FPU instructions; reads and writes FPU
 * data registers from image in pfpu.
 */

extern void fp_traps(fp_simd_type *, enum ftt_type, struct regs *);

/*
 * fp_traps handles passing execption conditions to the kernel.
 * It is called after fp_simulator or fp_emulator fail (return a non-zero ftt).
 */

#else	/* ! __STDC__ */

extern enum ftt_type fpu_simulator(/* pfpsd, pinst, pfsr, instr */);
/*	fp_simd_type	*pfpsd;	 Pointer to FPU simulator data */
/*	fp_inst_type	*pinst;	 Pointer to FPU instruction to simulate. */
/*	fsr_type	*pfsr;	 Pointer to image of FSR to read and write. */
/*	int		instr;	 Instruction to emulate. */

/*
 * fpu_simulator simulates FPU instructions only; reads and writes FPU data
 * registers directly.
 */

extern enum ftt_type fp_emulator(/* pfpsd, pinst, pregs, pwindow, pfpu */);
/*	fp_simd_type	*pfpsd;	   Pointer to FPU simulator data */
/*	fp_inst_type	*pinst;    Pointer to FPU instruction to simulate. */
/*	struct regs	*pregs;    Pointer to PCB image of registers. */
/*	struct rwindow	*pwindow;  Pointer to locals and ins. */
/*	struct fpu	*pfpu;	   Pointer to FPU register block. */

/*
 * fp_emulator simulates FPU and CPU-FPU instructions; reads and writes FPU
 * data registers from image in pfpu.
 */

extern void fp_traps(/* pfpsd, ftt, rp */);
/*	fp_simd_type	*pfpsd;	 Pointer to FPU simulator data */
/*	enum ftt_type	ftt;	 Type of trap. */
/*	struct regs	*rp;	 Pointer to PCB image of registers. */
/*
 * fp_traps handles passing execption conditions to the kernel.
 * It is called after fp_simulator or fp_emulator fail (return a non-zero ftt).
 */

#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FPU_FPU_SIMULATOR_H */
