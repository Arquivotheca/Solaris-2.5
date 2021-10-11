/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 *	All rights reserved.
 */

#ifndef _SYS_FPU_GLOBALS_H
#define	_SYS_FPU_GLOBALS_H

#pragma ident	"@(#)globals.h	1.26	94/11/21 SMI"	/* SunOS-4.0 1.11 */

/*
 * Sparc floating-point simulator PRIVATE include file.
 */

#include <sys/types.h>
#include <vm/seg.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*	PRIVATE CONSTANTS	*/

#define	INTEGER_BIAS	   31
#define	LONGLONG_BIAS	   63
#define	SINGLE_BIAS	  127
#define	DOUBLE_BIAS	 1023
#define	EXTENDED_BIAS	16383

/* PRIVATE TYPES	*/

#ifdef DEBUG
#define	PRIVATE
#else
#define	PRIVATE static
#endif

#define	DOUBLE_E(n) (n & 0xfffe)	/* More significant word of double. */
#define	DOUBLE_F(n) (1+DOUBLE_E(n))	/* Less significant word of double. */
#define	EXTENDED_E(n) (n & 0xfffc) /* Sign/exponent/significand of extended. */
#define	EXTENDED_F(n) (1+EXTENDED_E(n)) /* 2nd word of extended significand. */
#define	EXTENDED_G(n) (2+EXTENDED_E(n)) /* 3rd word of extended significand. */
#define	EXTENDED_H(n) (3+EXTENDED_E(n)) /* 4th word of extended significand. */
#define	DOUBLE(n) ((n & 0xfffe) >> 1)	/* Shift n to access double regs. */
#define	QUAD_E(n) ((n & 0xfffc) >> 1)	/* More significant half of quad. */
#define	QUAD_F(n) (1+QUAD_E(n))		/* Less significant half of quad. */


#if defined(_KERNEL)

typedef struct {
	int sign;
	enum fp_class_type fpclass;
	int	exponent;		/* Unbiased exponent	*/
	unsigned significand[4];	/* Four significand word . */
	int	rounded;		/* rounded bit */
	int	sticky;			/* stick bit */
} unpacked;


/* PRIVATE FUNCTIONS */

/* pfreg routines use "physical" FPU registers. */

void _fp_read_pfreg(FPU_REGS_TYPE *, unsigned);
void _fp_write_pfreg(FPU_REGS_TYPE *, unsigned);
void _fp_write_pfsr(FPU_FSR_TYPE *);
#ifdef	__sparcv9
void _fp_read_pdreg(FPU_DREGS_TYPE *, unsigned);
void _fp_write_pdreg(FPU_DREGS_TYPE *, unsigned);
#endif

/* vfreg routines use "virtual" FPU registers at *_fp_current_pfregs. */

void _fp_read_vfreg(FPU_REGS_TYPE *, unsigned, fp_simd_type *);
void _fp_write_vfreg(FPU_REGS_TYPE *, unsigned, fp_simd_type *);
#ifdef	__sparcv9
void _fp_read_vdreg(FPU_DREGS_TYPE *, unsigned, fp_simd_type *);
void _fp_write_vdreg(FPU_DREGS_TYPE *, unsigned, fp_simd_type *);
#endif

enum ftt_type _fp_iu_simulator(fp_simd_type *, fp_inst_type,
			struct regs *, struct rwindow *, kfpu_t *);

void _fp_unpack(fp_simd_type *, unpacked *, unsigned, enum fp_op_type);
void _fp_pack(fp_simd_type *, unpacked *, unsigned, enum fp_op_type);
void _fp_unpack_word(fp_simd_type *, unsigned *, unsigned);
void _fp_pack_word(fp_simd_type *, unsigned *, unsigned);
#ifdef	__sparcv9
void _fp_unpack_extword(fp_simd_type *, u_longlong_t *, unsigned);
void _fp_pack_extword(fp_simd_type *, u_longlong_t *, unsigned);
#endif
void fpu_normalize(unpacked *);
void fpu_rightshift(unpacked *, int);
void fpu_set_exception(fp_simd_type *, enum fp_exception_type);
void fpu_error_nan(fp_simd_type *, unpacked *);
void unpacksingle(fp_simd_type *, unpacked *, single_type);
void unpackdouble(fp_simd_type *, unpacked *, double_type, unsigned);
unsigned fpu_add3wc(unsigned *, unsigned, unsigned, unsigned);
unsigned fpu_sub3wc(unsigned *, unsigned, unsigned, unsigned);
unsigned fpu_neg2wc(unsigned *, unsigned, unsigned);
int fpu_cmpli(unsigned *, unsigned *, int);

/* extern void _fp_product(unsigned, unsigned, unsigned *); */

enum fcc_type _fp_compare(fp_simd_type *, unpacked *, unpacked *, int);

void _fp_add(fp_simd_type *, unpacked *, unpacked *, unpacked *);
void _fp_sub(fp_simd_type *, unpacked *, unpacked *, unpacked *);
void _fp_mul(fp_simd_type *, unpacked *, unpacked *, unpacked *);
void _fp_div(fp_simd_type *, unpacked *, unpacked *, unpacked *);
void _fp_sqrt(fp_simd_type *, unpacked *, unpacked *);

enum ftt_type	_fp_write_word(caddr_t, unsigned, fp_simd_type *);
enum ftt_type	_fp_read_word(caddr_t, int *, fp_simd_type *);
enum ftt_type	_fp_read_inst(caddr_t, int *, fp_simd_type *);
#ifdef	__sparcv9
enum ftt_type	read_iureg(fp_simd_type *, unsigned, struct regs *,
					struct rwindow *, u_longlong_t *);
enum ftt_type	write_iureg(fp_simd_type *, unsigned, struct regs *,
					struct rwindow *, u_longlong_t *);
#else
enum ftt_type	read_iureg(fp_simd_type *, unsigned, struct regs *,
					struct rwindow *, int *);
#endif

#endif  /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FPU_GLOBALS_H */
