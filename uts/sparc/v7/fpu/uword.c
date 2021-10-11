/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident	"@(#)uword.c	1.20	94/11/21 SMI"	/* SunOS-4.1 1.8 88/11/30 */

/* Read/write user memory procedures for Sparc FPU simulator. */

#include <sys/param.h>
#include <sys/fpu/fpu_simulator.h>
#include <sys/fpu/globals.h>
#include <sys/systm.h>
#include <vm/seg.h>

/* read the user instruction */
enum ftt_type
_fp_read_inst(address, pvalue, pfpsd)
	caddr_t		address;
	int		*pvalue;
	fp_simd_type	*pfpsd;
{
	int		w;
	int		b;

	if (((int) address & 0x3) != 0)
		return (ftt_alignment);	/* Must be word-aligned. */
	w = fuiword((int *)address);
	if (w == -1) {
		b = fuibyte(address);
		if (b == -1) {
			pfpsd->fp_trapaddr = address;
			pfpsd->fp_traprw = S_READ;
			return (ftt_fault);
		}
	}
	*pvalue = w;
	return (ftt_none);
}

#include <sys/privregs.h>

enum ftt_type
_fp_read_word(address, pvalue, pfpsd)
	caddr_t		address;
	int		*pvalue;
	fp_simd_type	*pfpsd;
{
	int		w;
	int		b;

	if (((int) address & 0x3) != 0)
		return (ftt_alignment);	/* Must be word-aligned. */
	w = fuword((int *)address);
	if (w == -1) {
		b = fubyte(address);
		if (b == -1) {
			pfpsd->fp_trapaddr = address;
			pfpsd->fp_traprw = S_READ;
			return (ftt_fault);
		}
	}
	*pvalue = w;
	return (ftt_none);
}

enum ftt_type
_fp_write_word(address, value, pfpsd)
	caddr_t		address;
	unsigned	value;
	fp_simd_type	*pfpsd;
{
	int		w;

	if (((int) address & 0x3) != 0)
		return (ftt_alignment);	/* Must be word-aligned. */
	w = suword((int *)address, value);
	if (w == -1) {
		pfpsd->fp_trapaddr = address;
		pfpsd->fp_traprw = S_WRITE;
		return (ftt_fault);
	} else {
		return (ftt_none);
	}
}

enum ftt_type
read_iureg(pfpsd, n, pregs, pwindow, pvalue)
	fp_simd_type	*pfpsd;
	unsigned	n;
	struct regs	*pregs;		/* Pointer to PCB image of registers. */
	struct rwindow	*pwindow;	/* Pointer to locals and ins. */
	int		*pvalue;
{				/* Reads integer unit's register n. */
	register int	*pint;

	if (n <= 15) {
		if (n == 0) {
			*pvalue = 0;
			return (ftt_none);	/* Read global register 0. */
		}
		if (n <= 7) {	/* globals */
			pint = &(pregs->r_g1) + (n - 1);
			*pvalue = *pint;
			return (ftt_none);
		} else {	/* outs */
			pint = &(pregs->r_o0) + (n - 8);
			*pvalue = *pint;
			return (ftt_none);
		}
	} else {		/* locals and ins */
		if (n <= 23)
			pint = &pwindow->rw_local[n - 16];
		else
			pint = &pwindow->rw_in[n - 24];
		if ((int) pint > KERNELBASE) {
			*pvalue =  *pint;
			return (ftt_none);
		}
		return (_fp_read_word((char *) pint, pvalue, pfpsd));
	}
}
