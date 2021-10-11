/*	@(#)_Qglobals.h	1.1	*/

/*
 * Copyright (c) 1988, 1989 by Sun Microsystems, Inc.
 */

	/* PRIVATE CONSTANTS	*/

#define INTEGER_BIAS	   31
#define LONGLONG_BIAS	   63
#define	SINGLE_BIAS	  127
#define DOUBLE_BIAS	 1023
#define EXTENDED_BIAS	16383

	/* PRIVATE TYPES	*/

#ifdef DEBUG
#define PRIVATE
#else
#define PRIVATE static
#endif

typedef
	struct
	{
	int sign;
	enum fp_class_type fpclass;
	int	exponent;		/* Unbiased exponent. */
	unsigned significand[4];	/* Four significand word . */
	int	rounded;		/* rounded bit */
	int	sticky;			/* stick bit */
	}
	unpacked;

	/* PRIVATE GLOBAL VARIABLES */

extern void _fp_unpack ( /* pu, n, type , cexc */ );
/*	unpacked	*pu;	*/	/* unpacked result */
/*	unsigned	n;	*/	/* register where data starts */
/*	fp_op_type	type;	*/	/* type of datum */
/*	unsigned	*cexc;	*/	/* current exception */

extern void _fp_pack ( /* pu, n, type, cexc */);
/*	unpacked	*pu;	*/	/* unpacked result */
/*	unsigned	n;	*/	/* register where data starts */
/*	fp_op_type	type;	*/	/* type of datum */
/*	unsigned	*cexc;	*/	/* current exception */

extern void _fp_unpack_word ( /* pu, n, type */ );
/*	unsigned	*pu;	*/	/* unpacked result */
/*	unsigned	n;	*/	/* register where data starts */

extern void _fp_pack_word ( /* pu, n, type */);
/*	unsigned	*pu;	*/	/* unpacked result */
/*	unsigned	n;	*/	/* register where data starts */

extern void fpu_normalize (/* pu */);
/*	unpacked	*pu;	*/	/* unpacked operand and result */

extern void fpu_rightshift (/* pu, n */);
/*	unpacked *pu; unsigned n;	*/
/*	Right shift significand sticky by n bits. */

extern unsigned fpu_add3wc (/* z,x,y,c */);
/*	unsigned *z,x,y,c;	*/ 	/* *z = x+y+carry; return new carry */

extern unsigned fpu_sub3wc (/* z,x,y,c */);
/*	unsigned *z,x,y,c;	*/ 	/* *z = x-y-carry; return new carry */

extern unsigned fpu_neg2wc  (/* x,c */);
/*	unsigned *z,x,c;	*/ 	/* *z = 0-x-carry; return new carry */

extern int fpu_cmpli (/* x,y,n */);
/*	unsigned x[],y[],n;	*/ 	/* n-word compare  */

extern void fpu_set_exception(/* ex, cexc */);
/*	enum fp_exception_type ex; */	/* exception to be set in curexcep */
/*	unsigned	*cexc;	*/	/* current exception */

extern void fpu_error_nan(/* pu,cexc */);
/*	unpacked *pu;	*/	/* Set invalid exception and error nan in *pu */
/*	unsigned *cexc;	*/	/* current exception */

extern void unpacksingle (/* pu, x, cexc */);
/*	unpacked	*pu;	*/	/* packed result */
/*	single_type	x;	*/	/* packed single */
/*	unsigned	*cexc;	*/	/* current exception */

extern void unpackdouble (/* pu, x, y , cexc*/);
/*	unpacked	*pu;	*/	/* unpacked result */
/*	double_type	x;	*/	/* packed double */
/*	unsigned	y;	*/
/*	unsigned	*cexc;	*/	/* current exception */

extern enum fcc_type _fp_compare (/* px, py */);

extern void _fp_add(/* px, py, pz, cexc */);
extern void _fp_sub(/* px, py, pz, cexc */);
extern void _fp_mul(/* px, py, pz, cexc */);
extern void _fp_div(/* px, py, pz, cexc */);
extern void _fp_sqrt(/* px, pz, cexc */);
