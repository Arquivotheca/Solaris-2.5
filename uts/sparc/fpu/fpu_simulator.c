/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ident	"@(#)fpu_simulator.c	1.29	95/03/24 SMI"
		/* SunOS-4.1 1.16 89/09/28 */

/* Main procedures for Sparc FPU simulator. */

#include <sys/fpu/fpu_simulator.h>
#include <sys/fpu/globals.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/siginfo.h>
#include <sys/thread.h>

#include <sys/privregs.h>

/* PUBLIC FUNCTIONS */

PRIVATE enum ftt_type
_fp_fpu_simulator(pfpsd, inst, pfsr)
	fp_simd_type	*pfpsd;	/* Pointer to fpu simulotor data */
	fp_inst_type	inst;	/* FPU instruction to simulate. */
	fsr_type	*pfsr;	/* Pointer to image of FSR to read and write. */
{
	unpacked	us1, us2, ud;	/* Unpacked operands and result. */
	unsigned	nrs1, nrs2, nrd;	/* Register number fields. */
	fsr_type	fsr;
	unsigned	usr;
	unsigned	andexcep;
	enum fcc_type	cc;
#ifdef	__sparcv9
	unsigned	nfcc;		/* fcc number field. */
	u_longlong_t	lusr;
	union ull {
		u_longlong_t	lusr;
		u_int		i[2];
	} kluge;
	extern enum ftt_type fmovcc();
	extern enum ftt_type fmovr();
#endif

	nrs1 = inst.rs1;
	nrs2 = inst.rs2;
	nrd = inst.rd;
	fsr = *pfsr;
	pfpsd->fp_current_exceptions = 0;	/* Init current exceptions. */
#ifndef	__sparcv9
	pfpsd->fp_fsrtem    = fsr.tem;		/* Obtain fsr's tem */
	pfpsd->fp_direction = fsr.rd;		/* Obtain rounding direction. */
	pfpsd->fp_precision = fsr.rp;		/* Obtain rounding precision. */
#else
	nfcc = nrd & 0x3;
	pfpsd->fp_fsrtem    = fsr.TEM;		/* Obtain fsr's tem */
	pfpsd->fp_direction = fsr.RD;		/* Obtain rounding direction. */
	pfpsd->fp_precision = fp_extended;	/* Same rounding prec. as V8. */
	if ((inst.op3 == 0x35) && ((inst.opcode & 0xf) == 0)) {
		return (fmovcc(pfpsd, inst, pfsr));	/* fmovcc */
	} else if ((inst.op3 == 0x35) && ((inst.opcode & 0x7) == 1)) {
		return (fmovr(pfpsd, inst));		/* fmovr */
	}
#endif
	switch (inst.opcode) {
	case fmovs:		/* also covers fmovd, fmovq */
		if (inst.prec < 2) {	/* fmovs */
			_fp_unpack_word(pfpsd, &usr, nrs2);
			_fp_pack_word(pfpsd, &usr, nrd);
#ifdef	__sparcv9
		} else {		/* fmovd */
			_fp_unpack_extword(pfpsd, &lusr, nrs2);
			_fp_pack_extword(pfpsd, &lusr, nrd);
			if (inst.prec > 2) {		/* fmovq */
				_fp_unpack_extword(pfpsd, &lusr, nrs2+2);
				_fp_pack_extword(pfpsd, &lusr, nrd+2);
			}
#endif
		}
		break;
	case fabss:		/* also covers fabsd, fabsq */
		if (inst.prec < 2) {	/* fabss */
			_fp_unpack_word(pfpsd, &usr, nrs2);
			usr &= 0x7fffffff;
			_fp_pack_word(pfpsd, &usr, nrd);
#ifdef	__sparcv9
		} else {		/* fabsd */
			_fp_unpack_extword(pfpsd, &kluge.lusr, nrs2);
			kluge.i[0] &= 0x7fffffff;
			kluge.i[1] &= 0xffffffff;
			_fp_pack_extword(pfpsd, &kluge.lusr, nrd);
			if (inst.prec > 2) {		/* fabsq */
				_fp_unpack_extword(pfpsd, &kluge.lusr, nrs2+2);
				kluge.i[0] &= 0xffffffff;
				kluge.i[1] &= 0xffffffff;
				_fp_pack_extword(pfpsd, &kluge.lusr, nrd+2);
			}
#endif
		}
		break;
	case fnegs:		/* also covers fnegd, fnegq */
		if (inst.prec < 2) {	/* fnegs */
			_fp_unpack_word(pfpsd, &usr, nrs2);
			usr ^= 0x80000000;
			_fp_pack_word(pfpsd, &usr, nrd);
#ifdef	__sparcv9
		} else {		/* fnegd, fnegq */
			_fp_unpack_extword(pfpsd, &kluge.lusr, nrs2);
			kluge.i[0] ^= 0x80000000;
			_fp_pack_extword(pfpsd, &kluge.lusr, nrd);
#endif
		}
		break;
	case fadd:
		_fp_unpack(pfpsd, &us1, nrs1, inst.prec);
		_fp_unpack(pfpsd, &us2, nrs2, inst.prec);
		_fp_add(pfpsd, &us1, &us2, &ud);
		_fp_pack(pfpsd, &ud, nrd, inst.prec);
		break;
	case fsub:
		_fp_unpack(pfpsd, &us1, nrs1, inst.prec);
		_fp_unpack(pfpsd, &us2, nrs2, inst.prec);
		_fp_sub(pfpsd, &us1, &us2, &ud);
		_fp_pack(pfpsd, &ud, nrd, inst.prec);
		break;
	case fmul:
		_fp_unpack(pfpsd, &us1, nrs1, inst.prec);
		_fp_unpack(pfpsd, &us2, nrs2, inst.prec);
		_fp_mul(pfpsd, &us1, &us2, &ud);
		_fp_pack(pfpsd, &ud, nrd, inst.prec);
		break;
	case fsmuld:
	case fdmulx:
		_fp_unpack(pfpsd, &us1, nrs1, inst.prec);
		_fp_unpack(pfpsd, &us2, nrs2, inst.prec);
		_fp_mul(pfpsd, &us1, &us2, &ud);
		_fp_pack(pfpsd, &ud, nrd, (enum fp_op_type) ((int)inst.prec+1));
		break;
	case fdiv:
		_fp_unpack(pfpsd, &us1, nrs1, inst.prec);
		_fp_unpack(pfpsd, &us2, nrs2, inst.prec);
		_fp_div(pfpsd, &us1, &us2, &ud);
		_fp_pack(pfpsd, &ud, nrd, inst.prec);
		break;
	case fcmp:
		_fp_unpack(pfpsd, &us1, nrs1, inst.prec);
		_fp_unpack(pfpsd, &us2, nrs2, inst.prec);
		cc = _fp_compare(pfpsd, &us1, &us2, 0);
		if (!(pfpsd->fp_current_exceptions & pfpsd->fp_fsrtem))
#ifndef	__sparcv9
			fsr.fcc = cc;
#else
			switch (nfcc) {
			case fcc0:
				fsr.FCC = cc;
				break;
			case fcc1:
				fsr.FCC1 = cc;
				break;
			case fcc2:
				fsr.FCC2 = cc;
				break;
			case fcc3:
				fsr.FCC3 = cc;
				break;
			}
#endif
		break;
	case fcmpe:
		_fp_unpack(pfpsd, &us1, nrs1, inst.prec);
		_fp_unpack(pfpsd, &us2, nrs2, inst.prec);
		cc = _fp_compare(pfpsd, &us1, &us2, 1);
		if (!(pfpsd->fp_current_exceptions & pfpsd->fp_fsrtem))
#ifndef	__sparcv9
			fsr.fcc = cc;
#else
			switch (nfcc) {
			case fcc0:
				fsr.FCC = cc;
				break;
			case fcc1:
				fsr.FCC1 = cc;
				break;
			case fcc2:
				fsr.FCC2 = cc;
				break;
			case fcc3:
				fsr.FCC3 = cc;
				break;
			}
#endif
		break;
	case fsqrt:
		_fp_unpack(pfpsd, &us1, nrs2, inst.prec);
		_fp_sqrt(pfpsd, &us1, &ud);
		_fp_pack(pfpsd, &ud, nrd, inst.prec);
		break;
	case ftoi:
		_fp_unpack(pfpsd, &us1, nrs2, inst.prec);
		pfpsd->fp_direction = fp_tozero;
		/* Force rounding toward zero. */
		_fp_pack(pfpsd, &us1, nrd, fp_op_integer);
		break;
#ifdef	__sparcv9
	case ftoll:
		_fp_unpack(pfpsd, &us1, nrs2, inst.prec);
		pfpsd->fp_direction = fp_tozero;
		/* Force rounding toward zero. */
		_fp_pack(pfpsd, &us1, nrd, fp_op_longlong);
		break;
	case flltos:
		_fp_unpack(pfpsd, &us1, nrs2, fp_op_longlong);
		_fp_pack(pfpsd, &us1, nrd, fp_op_single);
		break;
	case flltod:
		_fp_unpack(pfpsd, &us1, nrs2, fp_op_longlong);
		_fp_pack(pfpsd, &us1, nrd, fp_op_double);
		break;
	case flltox:
		_fp_unpack(pfpsd, &us1, nrs2, fp_op_longlong);
		_fp_pack(pfpsd, &us1, nrd, fp_op_extended);
		break;
#endif
	case fitos:
		_fp_unpack(pfpsd, &us1, nrs2, inst.prec);
		_fp_pack(pfpsd, &us1, nrd, fp_op_single);
		break;
	case fitod:
		_fp_unpack(pfpsd, &us1, nrs2, inst.prec);
		_fp_pack(pfpsd, &us1, nrd, fp_op_double);
		break;
	case fitox:
		_fp_unpack(pfpsd, &us1, nrs2, inst.prec);
		_fp_pack(pfpsd, &us1, nrd, fp_op_extended);
		break;
	default:
		return (ftt_unimplemented);
	}
#ifdef	__sparcv9
	fsr.CEXC = pfpsd->fp_current_exceptions;
#else
	fsr.cexc = pfpsd->fp_current_exceptions;
#endif
	if (pfpsd->fp_current_exceptions) {	/* Exception(s) occurred. */
#ifdef	__sparcv9
		andexcep = pfpsd->fp_current_exceptions & fsr.TEM;
#else
		andexcep = pfpsd->fp_current_exceptions & fsr.tem;
#endif
		if (andexcep != 0) {	/* Signal an IEEE SIGFPE here. */
			if (andexcep & (1 << fp_invalid))
				pfpsd->fp_trapcode = FPE_FLTINV;
			else if (andexcep & (1 << fp_overflow))
				pfpsd->fp_trapcode = FPE_FLTOVF;
			else if (andexcep & (1 << fp_underflow))
				pfpsd->fp_trapcode = FPE_FLTUND;
			else if (andexcep & (1 << fp_division))
				pfpsd->fp_trapcode = FPE_FLTDIV;
			else if (andexcep & (1 << fp_inexact))
				pfpsd->fp_trapcode = FPE_FLTRES;
			else
				pfpsd->fp_trapcode = 0;
			*pfsr = fsr;
			return (ftt_ieee);
		} else {	/* Just set accrued exception field. */
#ifdef	__sparcv9
			fsr.AEXC |= pfpsd->fp_current_exceptions;
#else
			fsr.aexc |= pfpsd->fp_current_exceptions;
#endif
		}
	}
	*pfsr = fsr;
	return (ftt_none);
}

/*
 * fpu_simulator simulates FPU instructions only;
 * reads and writes FPU data registers directly.
 */
enum ftt_type
fpu_simulator(pfpsd, pinst, pfsr, inst)
	fp_simd_type	*pfpsd;	/* Pointer to simulator data */
	fp_inst_type	*pinst;	/* Address of FPU instruction to simulate */
	fsr_type	*pfsr;	/* Pointer to image of FSR to read and write */
	int		 inst;	/* The FPU instruction to simulate */
{
	union {
		int		i;
		fp_inst_type	inst;
	} kluge;

	kluge.i = inst;
	pfpsd->fp_trapaddr = (char *) pinst;
	pfpsd->fp_current_read_freg = _fp_read_pfreg;
	pfpsd->fp_current_write_freg = _fp_write_pfreg;
#ifdef	__sparcv9
	pfpsd->fp_current_read_dreg = _fp_read_pdreg;
	pfpsd->fp_current_write_dreg = _fp_write_pdreg;
#endif
	return (_fp_fpu_simulator(pfpsd, kluge.inst, pfsr));
}

/*
 * fp_emulator simulates FPU and CPU-FPU instructions; reads and writes FPU
 * data registers from image in pfpu.
 */
enum ftt_type
fp_emulator(pfpsd, pinst, pregs, pwindow, pfpu)
	fp_simd_type	*pfpsd;	/* Pointer to simulator data */
	fp_inst_type	*pinst;	/* Pointer to FPU instruction to simulate. */
	struct regs	*pregs;	/* Pointer to PCB image of registers. */
	struct rwindow	*pwindow; /* Pointer to locals and ins. */
	kfpu_t		*pfpu;	/* Pointer to FPU register block. */
{
	klwp_id_t lwp = ttolwp(curthread);
	union {
		int		i;
		fp_inst_type	inst;
	} kluge;
	enum ftt_type	ftt;
#ifdef	__sparcv9
	struct v9_fpu *fp = lwptofpu(lwp);
	extern enum ftt_type _fp_ise_simulator();
	V9_FPU_FSR_TYPE	tfsr;
#else
	FPU_FSR_TYPE	tfsr;
#endif
	tfsr = pfpu->fpu_fsr;
	pfpsd->fp_current_pfregs = pfpu;
	pfpsd->fp_current_read_freg = _fp_read_vfreg;
	pfpsd->fp_current_write_freg = _fp_write_vfreg;
#ifdef	__sparcv9
	pfpsd->fp_current_read_dreg = _fp_read_vdreg;
	pfpsd->fp_current_write_dreg = _fp_write_vdreg;
#endif
	pfpsd->fp_trapaddr = (char *) pinst; /* bad inst addr in case we trap */
	ftt = _fp_read_inst((caddr_t) pinst, &(kluge.i), pfpsd);
	if (ftt != ftt_none)
		return (ftt);

	if ((kluge.inst.hibits == 2) &&
	    ((kluge.inst.op3 == 0x34) || (kluge.inst.op3 == 0x35))) {
		ftt = _fp_fpu_simulator(pfpsd, kluge.inst, (fsr_type *)&tfsr);
		/* Do not retry emulated instruction. */
		pregs->r_pc = pregs->r_npc;
		pregs->r_npc += 4;
		pfpu->fpu_fsr = tfsr;
		if (ftt != ftt_none) {
			/*
			 * Simulation generated an exception of some kind,
			 * simulate the fp queue for a signal.
			 */
			pfpu->fpu_q->FQu.fpq.fpq_addr = (u_long *)pinst;
			pfpu->fpu_q->FQu.fpq.fpq_instr = (u_long)kluge.i;
			pfpu->fpu_qcnt = 1;
		}

#ifdef	__sparcv9	/* implementation dependent instructions */
	} else if ((kluge.inst.hibits == 2) && (kluge.inst.op3 == 0x36)) {
		ftt = _fp_ise_simulator(pfpsd, kluge.inst, pregs, pwindow, fp);
#endif
	} else
		ftt = _fp_iu_simulator(pfpsd, kluge.inst, pregs, pwindow, pfpu);

	if (ftt != ftt_none)
		return (ftt);

	/*
	 * If we are single-stepping, don't emulate any more instructions.
	 */
	if (lwp->lwp_pcb.pcb_step != STEP_NONE)
		return (ftt);
again:
	/*
	 * now read next instruction and see if it can be emulated
	 */
	pinst = (fp_inst_type *)pregs->r_pc;
	pfpsd->fp_trapaddr = (char *) pinst; /* bad inst addr in case we trap */
	ftt = _fp_read_inst((caddr_t) pinst, &(kluge.i), pfpsd);
	if (ftt != ftt_none)
		return (ftt);
	if ((kluge.inst.hibits == 2) &&		/* fpops */
	    ((kluge.inst.op3 == 0x34) || (kluge.inst.op3 == 0x35))) {
		ftt = _fp_fpu_simulator(pfpsd, kluge.inst, (fsr_type *)&tfsr);
		/* Do not retry emulated instruction. */
		pfpu->fpu_fsr = tfsr;
		pregs->r_pc = pregs->r_npc;
		pregs->r_npc += 4;
		if (ftt != ftt_none) {
			/*
			 * Simulation generated an exception of some kind,
			 * simulate the fp queue for a signal.
			 */
			pfpu->fpu_q->FQu.fpq.fpq_addr = (u_long *)pinst;
			pfpu->fpu_q->FQu.fpq.fpq_instr = (u_long)kluge.i;
			pfpu->fpu_qcnt = 1;
		}
#ifdef	__sparcv9	/* implementation dependent instructions */
	} else if ((kluge.inst.hibits == 2) && (kluge.inst.op3 == 0x36)) {
		ftt = _fp_ise_simulator(pfpsd, kluge.inst, pregs, pwindow, fp);
#endif
	} else if (
#ifdef	__sparcv9
						/* movcc */
	    ((kluge.inst.hibits == 2) && ((kluge.inst.op3 & 0x3f) == 0x2c)) ||
						/* fbpcc */
	    ((kluge.inst.hibits == 0) && (((kluge.i>>22) & 0x7) == 5)) ||
#endif
						/* fldst */
	    ((kluge.inst.hibits == 3) && ((kluge.inst.op3 & 0x38) == 0x20)) ||
						/* fbcc */
	    ((kluge.inst.hibits == 0) && (((kluge.i>>22) & 0x7) == 6))) {
		ftt = _fp_iu_simulator(pfpsd, kluge.inst, pregs, pwindow, pfpu);
	} else
		return (ftt);

	if (ftt != ftt_none)
		return (ftt);
	else
		goto again;
}
