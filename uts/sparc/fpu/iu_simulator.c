/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ident	"@(#)iu_simulator.c	1.15	94/10/12 SMI"
		/* SunOS-4.1 1.8 88/11/30 */

/* Integer Unit simulator for Sparc FPU simulator. */

#include <sys/fpu/fpu_simulator.h>
#include <sys/fpu/globals.h>

#include <sys/privregs.h>

/*
 * fbcc also handles V9 fbpcc, and ignores the prediction bit.
 */
PRIVATE enum ftt_type
fbcc(pinst, pregs, pfpu)
	fp_inst_type    pinst;	/* FPU instruction to simulate. */
	struct regs	*pregs;	/* Pointer to PCB image of registers. */
	kfpu_t		*pfpu;	/* Pointer to FPU register block. */

{
#ifdef	__sparcv9
	fsr_type	fsr;
	int fbpcc = 0;
#else
	union {
		fsr_type	fsr;
		long		i;
	} klugefsr;
#endif
	union {
		fp_inst_type	fi;
		int		i;
	} kluge;
	enum fcc_type	fcc;
	enum icc_type {
		fbn, fbne, fblg, fbul, fbl, fbug, fbg, fbu,
		fba, fbe, fbue, fbge, fbuge, fble, fbule, fbo
	} icc;

	unsigned	annul, takeit;

#ifdef	__sparcv9
	if (((pinst.op3 >> 3) & 0xf) == 5)
		fbpcc = 1;
	fsr.ll = pfpu->fpu_fsr;
	if (fbpcc) {
		unsigned nfcc = (pinst.op3 >> 1) & 0x3;
		switch (nfcc) {
			case fcc0:
				fcc = fsr.FCC;
				break;
			case fcc1:
				fcc = fsr.FCC1;
				break;
			case fcc2:
				fcc = fsr.FCC2;
				break;
			case fcc3:
				fcc = fsr.FCC3;
				break;
			}
	} else {
		fcc = fsr.FCC;
	}
#else
	klugefsr.i = pfpu->fpu_fsr;
	fcc = klugefsr.fsr.fcc;
#endif
	icc = (enum icc_type) (pinst.rd & 0xf);
	annul = pinst.rd & 0x10;

	switch (icc) {
	case fbn:
		takeit = 0;
		break;
	case fbl:
		takeit = fcc == fcc_less;
		break;
	case fbg:
		takeit = fcc == fcc_greater;
		break;
	case fbu:
		takeit = fcc == fcc_unordered;
		break;
	case fbe:
		takeit = fcc == fcc_equal;
		break;
	case fblg:
		takeit = (fcc == fcc_less) || (fcc == fcc_greater);
		break;
	case fbul:
		takeit = (fcc == fcc_unordered) || (fcc == fcc_less);
		break;
	case fbug:
		takeit = (fcc == fcc_unordered) || (fcc == fcc_greater);
		break;
	case fbue:
		takeit = (fcc == fcc_unordered) || (fcc == fcc_equal);
		break;
	case fbge:
		takeit = (fcc == fcc_greater) || (fcc == fcc_equal);
		break;
	case fble:
		takeit = (fcc == fcc_less) || (fcc == fcc_equal);
		break;
	case fbne:
		takeit = fcc != fcc_equal;
		break;
	case fbuge:
		takeit = fcc != fcc_less;
		break;
	case fbule:
		takeit = fcc != fcc_greater;
		break;
	case fbo:
		takeit = fcc != fcc_unordered;
		break;
	case fba:
		takeit = 1;
		break;
	}
	if (takeit) {		/* Branch taken. */
		int		tpc;

		kluge.fi = pinst;
		tpc = pregs->r_pc;
		if (annul && (icc == fba)) {	/* fba,a is wierd */
#ifdef	__sparcv9
			if (fbpcc) {
				pregs->r_pc = tpc +
					(int) ((kluge.i << 13) >> 11);
			} else {
#endif
				pregs->r_pc = tpc +
					(int) ((kluge.i << 10) >> 8);
#ifdef	__sparcv9
			}
#endif
			pregs->r_npc = pregs->r_pc + 4;
		} else {
			pregs->r_pc = pregs->r_npc;
#ifdef	__sparcv9
			if (fbpcc) {
				pregs->r_npc = tpc +
					(int) ((kluge.i << 13) >> 11);
			} else {
#endif
				pregs->r_npc = tpc +
					(int) ((kluge.i << 10) >> 8);
#ifdef	__sparcv9
			}
#endif
		}
	} else {		/* Branch not taken. */
		if (annul) {	/* Annul next instruction. */
			pregs->r_pc = pregs->r_npc + 4;
			pregs->r_npc += 8;
		} else {	/* Execute next instruction. */
			pregs->r_pc = pregs->r_npc;
			pregs->r_npc += 4;
		}
	}
	return (ftt_none);
}

/* PUBLIC FUNCTIONS */

enum ftt_type
_fp_iu_simulator(pfpsd, pinst, pregs, pwindow, pfpu)
	fp_simd_type	*pfpsd;	/* FPU simulator data. */
	fp_inst_type	pinst;	/* FPU instruction to simulate. */
	struct regs	*pregs;	/* Pointer to PCB image of registers. */
	struct rwindow	*pwindow; /* Pointer to locals and ins. */
	kfpu_t		*pfpu;	/* Pointer to FPU register block. */
{
	extern enum ftt_type fldst();
#ifdef	__sparcv9
	extern enum ftt_type movcc();
#endif

	switch (pinst.hibits) {
	case 0:				/* fbcc and V9 fbpcc */
		return (fbcc(pinst, pregs, pfpu));
#ifdef	__sparcv9
	case 2:
		return (movcc(pfpsd, pinst, pregs, pwindow, pfpu));
#endif
	case 3:
		return (fldst(pfpsd, pinst, pregs, pwindow, pfpu));
	default:
		return (ftt_unimplemented);
	}
}
