/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ident	"@(#)v9instr.c	1.5	95/07/24 SMI"
		/* SunOS-4.1 1.8 88/11/30 */

/* Integer Unit simulator for Sparc FPU simulator. */

#include <sys/fpu/fpu_simulator.h>
#include <sys/fpu/globals.h>

#include <sys/privregs.h>
#include <sys/asi.h>

#define	FPU_REG_FIELD unsigned_reg	/* Coordinate with FPU_REGS_TYPE. */
#define	FPU_DREG_FIELD longlong_reg	/* Coordinate with FPU_DREGS_TYPE. */
#define	FPU_FSR_FIELD longlong_reg	/* Coordinate with V9_FPU_FSR_TYPE. */

/*
 * Simulator for loads and stores between floating-point unit and memory.
 * XXX - Could use _fp_read/write_extword routine if fu/sulonglong happens
 *	 and the v9 stuff would not need to be so hacky...
 */
enum ftt_type
fldst(pfpsd, pinst, pregs, pwindow, pfpu)
	fp_simd_type	*pfpsd;	/* FPU simulator data. */
	fp_inst_type	pinst;	/* FPU instruction to simulate. */
	struct regs	*pregs;	/* Pointer to PCB image of registers. */
	struct rwindow	*pwindow; /* Pointer to locals and ins. */
	kfpu_t		*pfpu;	/* Pointer to FPU register block. */
{
	unsigned	nrs1, nrs2, nrd;	/* Register number fields. */
	freg_type	f;
	int		ea, asi;
	union {
		freg_type	f;
		u_longlong_t	ea;
		int		i[2];
	} k;
	union {
		fp_inst_type	fi;
		int		i;
	} fkluge;
	union {
		u_longlong_t	ll;
		u_char		asi[4];
	} tstate;
	enum ftt_type   ftt;
	extern enum ftt_type ise_fldst();

	nrs1 = pinst.rs1;
	nrs2 = pinst.rs2;
	nrd = pinst.rd;
	if (pinst.ibit == 0) {	/* effective address = rs1 + rs2 */
		ftt = read_iureg(pfpsd, nrs1, pregs, pwindow, &k.ea);
		if (ftt != ftt_none)
			return (ftt);
		ea = k.i[1];
		ftt = read_iureg(pfpsd, nrs2, pregs, pwindow, &k.ea);
		if (ftt != ftt_none)
			return (ftt);
		ea += k.i[1];
		asi = (pinst.opcode << 2) | pinst.prec;
	} else {		/* effective address = rs1 + imm13 */
		fkluge.fi = pinst;
		ea = (fkluge.i << 19) >> 19;	/* Extract simm13 field. */
		ftt = read_iureg(pfpsd, nrs1, pregs, pwindow, &k.ea);
		if (ftt != ftt_none)
			return (ftt);
		ea += k.i[1];
		tstate.ll = pregs->r_tstate;
		asi = tstate.asi[1];
	}

	pfpsd->fp_trapaddr = (char *) ea; /* setup bad addr in case we trap */
	switch (pinst.op3 & 7) {
	case 0:		/* LDF and V9 LDFA */
		ftt = _fp_read_word((caddr_t) ea, &(f.int_reg), pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		pfpu->fpu_fr.fpu_regs[nrd] = f.FPU_REG_FIELD;
		break;
	case 1:		/* LDFSR and V9 LDXFSR */
		if ((int) nrd > 0) {
			ftt = _fp_read_word((caddr_t) ea, &k.i[0], pfpsd);
			if (ftt != ftt_none)
				return (ftt);
			ftt = _fp_read_word((caddr_t) (ea + 4), &k.i[1], pfpsd);
			if (ftt != ftt_none)
				return (ftt);
		} else {
			k.i[0] = 0;
			ftt = _fp_read_word((caddr_t) ea, &k.i[1], pfpsd);
			if (ftt != ftt_none)
				return (ftt);
		}
		pfpu->fpu_fsr = k.f.FPU_FSR_FIELD;
		break;
	case 2:		/* LDQF and LDQFA */
		if ((ea & 0x7) != 0)
			return (ftt_alignment);	/* Require double-alignment. */
		if ((nrd & 0x1) == 1) /* fix register encoding */
			nrd = (nrd & 0x1e) | 0x20;

		ftt = _fp_read_word((caddr_t) ea, &k.i[0], pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		ftt = _fp_read_word((caddr_t) (ea + 4), &k.i[1], pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		pfpu->fpu_fr.fpu_dregs[QUAD_E(nrd)] = k.f.FPU_DREG_FIELD;

		ftt = _fp_read_word((caddr_t) (ea + 8), &k.i[0], pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		ftt = _fp_read_word((caddr_t) (ea + 12), &k.i[1], pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		pfpu->fpu_fr.fpu_dregs[QUAD_F(nrd)] = k.f.FPU_DREG_FIELD;
		break;
	case 3:		/* LDDF and V9 LDDFA */
		if (asi > ASI_SNFL) {		/* highest defined V9 asi */
			return (ise_fldst(pfpsd, pinst, pregs,
				pwindow, pfpu, asi));
		}
		if ((ea & 0x3) != 0)
			return (ftt_alignment);	/* Require 32 bit-alignment. */
		if ((nrd & 0x1) == 1) /* fix register encoding */
			nrd = (nrd & 0x1e) | 0x20;
		ftt = _fp_read_word((caddr_t) ea, &k.i[0], pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		ftt = _fp_read_word((caddr_t) (ea + 4), &k.i[1], pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		pfpu->fpu_fr.fpu_dregs[DOUBLE(nrd)] = k.f.FPU_DREG_FIELD;
		break;
	case 4:		/* STF and V9 STFA */
		f.FPU_REG_FIELD = pfpu->fpu_fr.fpu_regs[nrd];
		ftt = _fp_write_word((caddr_t) ea, f.int_reg, pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		break;
	case 5:		/* STFSR and V9 STXFSR */
		k.f.FPU_FSR_FIELD = pfpu->fpu_fsr;
		k.i[1] &= ~0x301000;		/* Clear reserved bits. */
		k.i[1] |= 0x0E0000;		/* Set version number=7 . */
		if ((int) nrd > 0) {
			ftt = _fp_write_word((caddr_t) ea, k.i[0], pfpsd);
			if (ftt != ftt_none)
				return (ftt);
			ftt = _fp_write_word((caddr_t) (ea + 4), k.i[1], pfpsd);
			if (ftt != ftt_none)
				return (ftt);
		} else {
			ftt = _fp_write_word((caddr_t) ea, k.i[1], pfpsd);
			if (ftt != ftt_none)
				return (ftt);
		}
		break;
	case 6:		/* STQF and STQFA */
		if ((ea & 0x7) != 0)
			return (ftt_alignment);	/* Require double-alignment. */
		if ((nrd & 0x1) == 1) /* fix register encoding */
			nrd = (nrd & 0x1e) | 0x20;

		k.f.FPU_DREG_FIELD = pfpu->fpu_fr.fpu_dregs[QUAD_E(nrd)];
		ftt = _fp_write_word((caddr_t) ea, k.i[0], pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		ftt = _fp_write_word((caddr_t) (ea + 4), k.i[1], pfpsd);
		if (ftt != ftt_none)
			return (ftt);

		k.f.FPU_DREG_FIELD = pfpu->fpu_fr.fpu_dregs[QUAD_F(nrd)];
		ftt = _fp_write_word((caddr_t) (ea + 8), k.i[0], pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		ftt = _fp_write_word((caddr_t) (ea + 12), k.i[1], pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		break;
	case 7:		/* STDF and V9 STDFA */
		if (asi > ASI_SNFL) {		/* highest defined V9 asi */
			return (ise_fldst(pfpsd, pinst, pregs,
				pwindow, pfpu, asi));
		}
		if ((ea & 0x3) != 0)
			return (ftt_alignment);	/* Require 32 bit-alignment. */
		if ((nrd & 0x1) == 1) /* fix register encoding */
			nrd = (nrd & 0x1e) | 0x20;
		k.f.FPU_DREG_FIELD = pfpu->fpu_fr.fpu_dregs[DOUBLE(nrd)];
		ftt = _fp_write_word((caddr_t) ea, k.i[0], pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		ftt = _fp_write_word((caddr_t) (ea + 4), k.i[1], pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		break;
	default:
		/* addr of unimp inst */
		pfpsd->fp_trapaddr = (char *) pregs->r_pc;
		return (ftt_unimplemented);
	}

	pregs->r_pc = pregs->r_npc;	/* Do not retry emulated instruction. */
	pregs->r_npc += 4;
	return (ftt_none);
}

/*
 * Floating-point conditional moves between floating point unit registers.
 */
enum ftt_type
fmovcc_fcc(pfpsd, inst, pfsr, cc)
	fp_simd_type	*pfpsd;	/* Pointer to fpu simulator data */
	fp_inst_type	inst;	/* FPU instruction to simulate. */
	fsr_type	*pfsr;	/* Pointer to image of FSR to read and write. */
	enum cc_type	cc;
{
	unsigned 	moveit;
	fsr_type	fsr;
	enum fcc_type	fcc;
	enum icc_type {
		fmovn, fmovne, fmovlg, fmovul, fmovl, fmovug, fmovg, fmovu,
		fmova, fmove, fmovue, fmovge, fmovuge, fmovle, fmovule, fmovo
	} cond;

	fsr = *pfsr;
	switch (cc) {
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

	cond = (enum icc_type) (inst.rs1 & 0xf);
	switch (cond) {
	case fmovn:
		moveit = 0;
		break;
	case fmovl:
		moveit = fcc == fcc_less;
		break;
	case fmovg:
		moveit = fcc == fcc_greater;
		break;
	case fmovu:
		moveit = fcc == fcc_unordered;
		break;
	case fmove:
		moveit = fcc == fcc_equal;
		break;
	case fmovlg:
		moveit = (fcc == fcc_less) || (fcc == fcc_greater);
		break;
	case fmovul:
		moveit = (fcc == fcc_unordered) || (fcc == fcc_less);
		break;
	case fmovug:
		moveit = (fcc == fcc_unordered) || (fcc == fcc_greater);
		break;
	case fmovue:
		moveit = (fcc == fcc_unordered) || (fcc == fcc_equal);
		break;
	case fmovge:
		moveit = (fcc == fcc_greater) || (fcc == fcc_equal);
		break;
	case fmovle:
		moveit = (fcc == fcc_less) || (fcc == fcc_equal);
		break;
	case fmovne:
		moveit = fcc != fcc_equal;
		break;
	case fmovuge:
		moveit = fcc != fcc_less;
		break;
	case fmovule:
		moveit = fcc != fcc_greater;
		break;
	case fmovo:
		moveit = fcc != fcc_unordered;
		break;
	case fmova:
		moveit = 1;
		break;
	}
	if (moveit) {		/* Move fpu register. */
		unsigned usr, nrs2, nrd;
		u_longlong_t lusr;

		nrs2 = inst.rs2;
		nrd = inst.rd;
		if (inst.prec < 2) {	/* fmovs */
			_fp_unpack_word(pfpsd, &usr, nrs2);
			_fp_pack_word(pfpsd, &usr, nrd);
		} else {		/* fmovd */
			_fp_unpack_extword(pfpsd, &lusr, nrs2);
			_fp_pack_extword(pfpsd, &lusr, nrd);
			if (inst.prec > 2) {		/* fmovq */
				_fp_unpack_extword(pfpsd, &lusr, nrs2+2);
				_fp_pack_extword(pfpsd, &lusr, nrd+2);
			}
		}
	}
	return (ftt_none);
}

/*
 * Integer conditional moves between floating point unit registers.
 */
enum ftt_type
fmovcc_icc(pfpsd, inst, cc)
	fp_simd_type	*pfpsd;	/* Pointer to fpu simulator data */
	fp_inst_type	inst;	/* FPU instruction to simulate. */
	enum cc_type	cc;
{
	int 	moveit;
	enum icc_type {
		fmovn, fmove, fmovle, fmovl, fmovleu, fmovcs, fmovneg, fmovvs,
		fmova, fmovne, fmovg, fmovge, fmovgu, fmovcc, fmovpos, fmovvc
	} cond;

	register struct regs *pregs;
	union {
		u_longlong_t	ll;
		u_int		ccr[2];
	} tstate;
	union {
		unsigned	i;
		ccr_type	ccr;
	} k;

	pregs = lwptoregs(curthread->t_lwp);
	tstate.ll = pregs->r_tstate;
	switch (cc) {
	case icc:
		k.i = (tstate.ccr[0] & 0xf);
		break;
	case xcc:
		k.i = ((tstate.ccr[0] & 0xf0) >> 4);
		break;
	}

	cond = (enum icc_type) (inst.rs1 & 0xf);
	switch (cond) {
	case fmovn:
		moveit = 0;
		break;
	case fmove:
		moveit = (int) (k.ccr.z);
		break;
	case fmovle:
		moveit = (int) (k.ccr.z | (k.ccr.n ^ k.ccr.v));
		break;
	case fmovl:
		moveit = (int) (k.ccr.n ^ k.ccr.v);
		break;
	case fmovleu:
		moveit = (int) (k.ccr.c | k.ccr.z);
		break;
	case fmovcs:
		moveit = (int) (k.ccr.c);
		break;
	case fmovneg:
		moveit = (int) (k.ccr.n);
		break;
	case fmovvs:
		moveit = (int) (k.ccr.v);
		break;
	case fmova:
		moveit = 1;
		break;
	case fmovne:
		moveit = (int) (k.ccr.z == 0);
		break;
	case fmovg:
		moveit = (int) ((k.ccr.z | (k.ccr.n ^ k.ccr.v)) == 0);
		break;
	case fmovge:
		moveit = (int) ((k.ccr.n ^ k.ccr.v) == 0);
		break;
	case fmovgu:
		moveit = (int) ((k.ccr.c | k.ccr.z) == 0);
		break;
	case fmovcc:
		moveit = (int) (k.ccr.c == 0);
		break;
	case fmovpos:
		moveit = (int) (k.ccr.n == 0);
		break;
	case fmovvc:
		moveit = (int) (k.ccr.v == 0);
		break;
	}
	if (moveit) {		/* Move fpu register. */
		unsigned usr, nrs2, nrd;
		u_longlong_t lusr;

		nrs2 = inst.rs2;
		nrd = inst.rd;
		if (inst.prec < 2) {	/* fmovs */
			_fp_unpack_word(pfpsd, &usr, nrs2);
			_fp_pack_word(pfpsd, &usr, nrd);
		} else {		/* fmovd */
			_fp_unpack_extword(pfpsd, &lusr, nrs2);
			_fp_pack_extword(pfpsd, &lusr, nrd);
			if (inst.prec > 2) {		/* fmovq */
				_fp_unpack_extword(pfpsd, &lusr, nrs2+2);
				_fp_pack_extword(pfpsd, &lusr, nrd+2);
			}
		}
	}
	return (ftt_none);
}

/*
 * Simulator for moving fp register on condition (FMOVcc).
 */
enum ftt_type
fmovcc(pfpsd, inst, pfsr)
	fp_simd_type	*pfpsd;	/* Pointer to fpu simulator data */
	fp_inst_type	inst;	/* FPU instruction to simulate. */
	fsr_type	*pfsr;	/* Pointer to image of FSR to read and write. */
{
	enum cc_type	opf_cc;

	opf_cc = (enum cc_type) ((inst.ibit << 2) | (inst.opcode >> 4));
	if ((opf_cc == icc) || (opf_cc == xcc)) {
		return (fmovcc_icc(pfpsd, inst, opf_cc));
	} else {
		return (fmovcc_fcc(pfpsd, inst, pfsr, opf_cc));
	}
}

/*
 * Simulator for moving fp register on integer register condition (FMOVr).
 */
enum ftt_type
fmovr(pfpsd, inst)
	fp_simd_type	*pfpsd;	/* Pointer to fpu simulator data */
	fp_inst_type	inst;	/* FPU instruction to simulate. */
{
	struct regs	*pregs;
	struct rwindow	*pwindow;
	unsigned	nrs1;
	enum ftt_type	ftt;
	enum rcond_type {
		none, fmovre, fmovrlez, fmovrlz,
		nnone, fmovrne, fmovrgz, fmovrgez
	} rcond;
	longlong_t moveit, r;

	nrs1 = inst.rs1;
	if (nrs1 > 15)		/* rs1 must be a global register */
		return (ftt_unimplemented);
	if (inst.ibit)		/* ibit must be unused */
		return (ftt_unimplemented);
	pregs = lwptoregs(curthread->t_lwp);
	pwindow = (struct rwindow *)pregs->r_sp;
	ftt = read_iureg(pfpsd, nrs1, pregs, pwindow, (u_longlong_t *)&r);
	if (ftt != ftt_none)
		return (ftt);
	rcond = (enum rcond_type) (inst.opcode >> 3) & 7;
	switch (rcond) {
	case fmovre:
		moveit = r == 0;
		break;
	case fmovrlez:
		moveit = r <= 0;
		break;
	case fmovrlz:
		moveit = r < 0;
		break;
	case fmovrne:
		moveit = r != 0;
		break;
	case fmovrgz:
		moveit = r > 0;
		break;
	case fmovrgez:
		moveit = r >= 0;
		break;
	default:
		return (ftt_fault);
	}
	if (moveit) {		/* Move fpu register. */
		unsigned usr, nrs2, nrd;
		u_longlong_t lusr;

		nrs2 = inst.rs2;
		nrd = inst.rd;
		if (inst.prec < 2) {	/* fmovs */
			_fp_unpack_word(pfpsd, &usr, nrs2);
			_fp_pack_word(pfpsd, &usr, nrd);
		} else {		/* fmovd */
			_fp_unpack_extword(pfpsd, &lusr, nrs2);
			_fp_pack_extword(pfpsd, &lusr, nrd);
			if (inst.prec > 2) {		/* fmovq */
				_fp_unpack_extword(pfpsd, &lusr, nrs2+2);
				_fp_pack_extword(pfpsd, &lusr, nrd+2);
			}
		}
	}
	return (ftt_none);
}

/*
 * Move integer register on condition (MOVcc).
 */
enum ftt_type
movcc(pfpsd, pinst, pregs, pwindow,  pfpu)
	fp_simd_type	*pfpsd; /* Pointer to fpu simulator data */
	fp_inst_type    pinst;	/* FPU instruction to simulate. */
	struct regs	*pregs;	/* Pointer to PCB image of registers. */
	struct rwindow	*pwindow; /* Pointer to locals and ins. */
	struct v9_fpu	*pfpu;	/* Pointer to FPU register block. */

{
	fsr_type	fsr;
	enum cc_type	cc;
	enum fcc_type	fcc;
	enum icc_type {
		fmovn, fmovne, fmovlg, fmovul, fmovl, fmovug, fmovg, fmovu,
		fmova, fmove, fmovue, fmovge, fmovuge, fmovle, fmovule, fmovo
	} cond;
	unsigned moveit;
	enum ftt_type ftt = ftt_none;

	cc = (enum cc_type) (pinst.opcode >> 0x4) & 3;
	fsr.ll = pfpu->fpu_fsr;
	cond = (enum icc_type) (pinst.rs1 & 0xf);
	switch (cc) {
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

	switch (cond) {
	case fmovn:
		moveit = 0;
		break;
	case fmovl:
		moveit = fcc == fcc_less;
		break;
	case fmovg:
		moveit = fcc == fcc_greater;
		break;
	case fmovu:
		moveit = fcc == fcc_unordered;
		break;
	case fmove:
		moveit = fcc == fcc_equal;
		break;
	case fmovlg:
		moveit = (fcc == fcc_less) || (fcc == fcc_greater);
		break;
	case fmovul:
		moveit = (fcc == fcc_unordered) || (fcc == fcc_less);
		break;
	case fmovug:
		moveit = (fcc == fcc_unordered) || (fcc == fcc_greater);
		break;
	case fmovue:
		moveit = (fcc == fcc_unordered) || (fcc == fcc_equal);
		break;
	case fmovge:
		moveit = (fcc == fcc_greater) || (fcc == fcc_equal);
		break;
	case fmovle:
		moveit = (fcc == fcc_less) || (fcc == fcc_equal);
		break;
	case fmovne:
		moveit = fcc != fcc_equal;
		break;
	case fmovuge:
		moveit = fcc != fcc_less;
		break;
	case fmovule:
		moveit = fcc != fcc_greater;
		break;
	case fmovo:
		moveit = fcc != fcc_unordered;
		break;
	case fmova:
		moveit = 1;
		break;
	}
	if (moveit) {		/* Move fpu register. */
		unsigned nrd;
		u_longlong_t r;

		nrd = pinst.rd;
		if (pinst.ibit == 0) {	/* copy the value in r[rs2] */
			unsigned nrs2;

			nrs2 = pinst.rs2;
			ftt = read_iureg(pfpsd, nrs2, pregs, pwindow, &r);
			if (ftt != ftt_none)
				return (ftt);
			ftt = write_iureg(pfpsd, nrd, pregs, pwindow, &r);
		} else {		/* use sign_ext(simm11) */
			union {
				fp_inst_type	fi;
				int		i;
			} fk;

			fk.fi = pinst;		/* Extract simm11 field */
			r = (fk.i << 21) >> 21;
			ftt = write_iureg(pfpsd, nrd, pregs, pwindow, &r);
		}
	}
	pregs->r_pc = pregs->r_npc;	/* Do not retry emulated instruction. */
	pregs->r_npc += 4;
	return (ftt);
}
