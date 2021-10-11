/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ident	"@(#)spitinstr.c	1.2	95/03/23 SMI"
		/* SunOS-4.1 1.8 88/11/30 */

/* Integer Unit simulator for Sparc FPU simulator. */

#include <sys/fpu/fpu_simulator.h>
#include <sys/fpu/globals.h>

#include <sys/privregs.h>
#include <sys/spitasi.h>

#define	FPU_REG_FIELD unsigned_reg	/* Coordinate with FPU_REGS_TYPE. */
#define	FPU_DREG_FIELD longlong_reg	/* Coordinate with FPU_DREGS_TYPE. */
#define	FPU_FSR_FIELD longlong_reg	/* Coordinate with V9_FPU_FSR_TYPE. */

/*
 * Simulator for Spitfire-specific instructions with op3 == 0x36
 * XXX - A big pain in the rear for this code is that some of these
 * instructions are integer instructions and some are floating
 * point instructions. The convention for integer instructions is that
 * each function which implements the integer instruction emulation also
 * increments the pc. Naturally it's different for floating point intrs.
 * Groan.
 */
enum ftt_type
_fp_ise_simulator(pfpsd, pinst, pregs, pwindow, fp)
	fp_simd_type	*pfpsd;	/* FPU simulator data. */
	fp_inst_type	pinst;	/* FPU instruction to simulate. */
	struct regs	*pregs;	/* Pointer to PCB image of registers. */
	struct rwindow	*pwindow; /* Pointer to locals and ins. */
	struct v9_fpu	*fp;	/* Need to fp to access gsr reg */
{
	unsigned	nrs1, nrd;	/* Register number fields. */
	u_longlong_t	lusr;
	enum ftt_type	ise_edge8();
	enum ftt_type	ise_alignaddr();
	enum ftt_type	ise_fcmpne32();
	enum ftt_type	ise_faligndata();

	nrs1 = pinst.rs1;
	nrd = pinst.rd;

	switch (pinst.opcode) {
	case (0x0):		/* edge8 */
		if ((pinst.ibit != 0) || (pinst.prec != 0))
			return (ftt_unimplemented);
		return (ise_edge8(pfpsd, pinst, pregs, pwindow));
	case (0x6):		/* alignaddr */
		if ((pinst.ibit != 0) || (pinst.prec != 0))
			return (ftt_unimplemented);
		return (ise_alignaddr(pfpsd, pinst, pregs, pwindow, fp));
	case (0x9):		/* fcmpne32 */
		if ((pinst.ibit != 0) || (pinst.prec != 0))
			return (ftt_unimplemented);
		return (ise_fcmpne32(pfpsd, pinst, pregs, pwindow));
	case (0x12):		/* faligndata */
		if ((pinst.ibit != 0) || (pinst.prec != 0))
			return (ftt_unimplemented);
		return (ise_faligndata(pfpsd, pinst, fp));
	case (0x1d):		/* fsrc1 */
		if ((pinst.ibit != 0) || (pinst.prec != 0))
			return (ftt_unimplemented);
		_fp_unpack_extword(pfpsd, &lusr, nrs1);
		_fp_pack_extword(pfpsd, &lusr, nrd);
		return (ftt_none);
	default:
		return (ftt_unimplemented);
	}
}

/*
 * Simulator for edge8 instruction.
 */
enum ftt_type
ise_edge8(pfpsd, pinst, pregs, pwindow)
	fp_simd_type	*pfpsd;	/* FPU simulator data. */
	fp_inst_type	pinst;	/* FPU instruction to simulate. */
	struct regs	*pregs;	/* Pointer to PCB image of registers. */
	struct rwindow	*pwindow; /* Pointer to locals and ins. */
{
	unsigned	nrs1, nrs2, nrd;	/* Register number fields. */
	enum ftt_type   ftt = ftt_none;
	u_int l3lsb, r3lsb, ccr;

	u_int left_edge_mask[8] = {
		0xff, 0x7f, 0x3f, 0x1f, 0x0f, 0x07, 0x03, 0x01
	};
	u_int right_edge_mask[8] = {
		0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe, 0xff
	};
	union {
		freg_type	f;
		u_longlong_t	ea;
		int		i[2];
	} krs1, krs2, krd;
	union {
		u_longlong_t	ll;
		u_int		ccr[2];
	} tstate;

	unsigned _fp_subcc_ccr();

	nrs1 = pinst.rs1;
	nrs2 = pinst.rs2;
	nrd = pinst.rd;

	ftt = read_iureg(pfpsd, nrs1, pregs, pwindow, &krs1.ea);
	if (ftt != ftt_none)
		return (ftt);
	l3lsb = krs1.i[1] & 0x7;
	krs1.i[1] = (krs1.i[1] >> 3) << 3;
	ftt = read_iureg(pfpsd, nrs2, pregs, pwindow, &krs2.ea);
	if (ftt != ftt_none)
		return (ftt);
	r3lsb = krs2.i[1] & 0x7;
	krs2.i[1] = (krs2.i[1] >> 3) << 3;

	if (krs1.i[1] == krs2.i[1])
		krd.i[1] = right_edge_mask[r3lsb] & left_edge_mask[l3lsb];
	else
		krd.i[1] = left_edge_mask[l3lsb];
	ftt = write_iureg(pfpsd, nrd, pregs, pwindow, &krd.ea);

	ccr = _fp_subcc_ccr(krs1.i[1], krs2.i[1]);
	pregs = lwptoregs(curthread->t_lwp);	/* update user's ccr */
	tstate.ll = pregs->r_tstate;
	tstate.ccr[0] = (u_int) ccr & 0xff;

	pregs->r_pc = pregs->r_npc;	/* Do not retry emulated instruction. */
	pregs->r_npc += 4;
	return (ftt);
}

/*
 * Simulator for alignaddress instruction.
 */
enum ftt_type
ise_alignaddr(pfpsd, pinst, pregs, pwindow, fp)
	fp_simd_type	*pfpsd;	/* FPU simulator data. */
	fp_inst_type	pinst;	/* FPU instruction to simulate. */
	struct regs	*pregs;	/* Pointer to PCB image of registers. */
	struct rwindow	*pwindow; /* Pointer to locals and ins. */
	struct v9_fpu	*fp;	/* Need to fp to access gsr reg */
{
	unsigned	nrs1, nrs2, nrd;	/* Register number fields. */
	union {
		freg_type	f;
		u_longlong_t	ea;
		int		i[2];
	} k;
	enum ftt_type ftt;
	u_longlong_t addr, g, r;
	extern void get_gsr(struct v9_fpu *, u_longlong_t *);
	extern void set_gsr(u_longlong_t *, struct v9_fpu *);

	nrs1 = pinst.rs1;
	nrs2 = pinst.rs2;
	nrd = pinst.rd;

	ftt = read_iureg(pfpsd, nrs1, pregs, pwindow, &k.ea);
	if (ftt != ftt_none)
		return (ftt);
	addr = k.ea;
	ftt = read_iureg(pfpsd, nrs2, pregs, pwindow, &k.ea);
	if (ftt != ftt_none)
		return (ftt);
	addr += k.ea;
	r = (addr >> 3) << 3;	/* zero least 3 significant bits */
	ftt = write_iureg(pfpsd, nrd, pregs, pwindow, &r);

	r = addr & 0x7; 	/* store lower 3 bits in gsr save area */
	get_gsr(fp, &g);
	g = (g & 0x7) | r;
	set_gsr(&g, fp);

	pregs->r_pc = pregs->r_npc;	/* Do not retry emulated instruction. */
	pregs->r_npc += 4;
	return (ftt);
}

/*
 * Simulator for fcmpne32 instruction.
 */
enum ftt_type
ise_fcmpne32(pfpsd, pinst, pregs, pwindow)
	fp_simd_type	*pfpsd;	/* FPU simulator data. */
	fp_inst_type	pinst;	/* FPU instruction to simulate. */
	struct regs	*pregs;	/* Pointer to PCB image of registers. */
	struct rwindow	*pwindow; /* Pointer to locals and ins. */
{
	unsigned	nrs1, nrs2, nrd;	/* Register number fields. */
	union {
		u_longlong_t	ll;
		int		i[2];
	} krs1, krs2;
	u_longlong_t krd;
	enum ftt_type ftt;

	nrs1 = pinst.rs1;
	nrs2 = pinst.rs2;
	nrd = pinst.rd;

	_fp_unpack_extword(pfpsd, &krs1.ll, nrs1);
	_fp_unpack_extword(pfpsd, &krs2.ll, nrs2);
	krd = krs1.i[1] ^ krs2.i[1];
	ftt = write_iureg(pfpsd, nrd, pregs, pwindow, &krd);

	pregs->r_pc = pregs->r_npc;	/* Do not retry emulated instruction. */
	pregs->r_npc += 4;
	return (ftt);
}

/*
 * Simulator for faligndata instruction.
 */
enum ftt_type
ise_faligndata(pfpsd, pinst, fp)
	fp_simd_type	*pfpsd;	/* FPU simulator data. */
	fp_inst_type	pinst;	/* FPU instruction to simulate. */
	struct v9_fpu	*fp;	/* Need to fp to access gsr reg */
{
	unsigned	nrs1, nrs2, nrd;	/* Register number fields. */
	int		i, ri;
	union {
		freg_type	f[2];
		char		c[16];
	} krs12;
	union {
		freg_type	f;
		char		c[8];
	} krd;
	u_longlong_t lusr, r;
	extern void get_gsr(struct v9_fpu *, u_longlong_t *);

	nrs1 = pinst.rs1;
	nrs2 = pinst.rs2;
	nrd = pinst.rd;

	_fp_unpack_extword(pfpsd, &lusr, nrs1);
	krs12.f[0].FPU_DREG_FIELD = lusr;
	_fp_unpack_extword(pfpsd, &lusr, nrs2);
	krs12.f[1].FPU_DREG_FIELD = lusr;

	get_gsr(fp, &r);
	ri = r & 0x7;

	for (i = 0; i <  8; i++, ri++)
		krd.c[i] = krs12.c[ri];

	lusr = krd.f.FPU_DREG_FIELD;
	_fp_pack_extword(pfpsd, &lusr, nrd);
	return (ftt_none);
}

/*
 * Simulator for Spitfire-specific loads and stores between
 * floating-point unit and memory.
 */
enum ftt_type
ise_fldst(pfpsd, pinst, pregs, pwindow, pfpu, asi)
	fp_simd_type	*pfpsd;	/* FPU simulator data. */
	fp_inst_type	pinst;	/* FPU instruction to simulate. */
	struct regs	*pregs;	/* Pointer to PCB image of registers. */
	struct rwindow	*pwindow; /* Pointer to locals and ins. */
	kfpu_t		*pfpu;	/* Pointer to FPU register block. */
	unsigned	asi;	/* asi to emulate! */
{
	enum ftt_type ise_blk_fldst();

	switch (asi) {
		case ASI_BLK_P:
			return (ise_blk_fldst(pfpsd, pinst, pregs,
				pwindow, pfpu));
		default:
			return (ftt_unimplemented);
	}
}

/*
 * Simulator for block loads and stores between floating-point unit and memory.
 */
enum ftt_type
ise_blk_fldst(pfpsd, pinst, pregs, pwindow, pfpu)
	fp_simd_type	*pfpsd;	/* FPU simulator data. */
	fp_inst_type	pinst;	/* FPU instruction to simulate. */
	struct regs	*pregs;	/* Pointer to PCB image of registers. */
	struct rwindow	*pwindow; /* Pointer to locals and ins. */
	kfpu_t		*pfpu;	/* Pointer to FPU register block. */
{
	unsigned	nrs1, nrs2, nrd;	/* Register number fields. */
	int		ea, i;
	union {
		freg_type	f;
		u_longlong_t	ea;
		int		i[2];
	} k;
	union {
		fp_inst_type	fi;
		int		i;
	} fkluge;
	enum ftt_type   ftt;

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
	} else {		/* effective address = rs1 + imm13 */
		fkluge.fi = pinst;
		ea = (fkluge.i << 19) >> 19;	/* Extract simm13 field. */
		ftt = read_iureg(pfpsd, nrs1, pregs, pwindow, &k.ea);
		if (ftt != ftt_none)
			return (ftt);
		ea += k.i[1];
	}

	pfpsd->fp_trapaddr = (char *) ea; /* setup bad addr in case we trap */
	switch (pinst.op3 & 7) {
	case 3:		/* LDDF and V9 LDDFA */
		if ((ea & 0x7) != 0)
			return (ftt_alignment);	/* Require 64 bit-alignment. */
		if ((nrd & 0x1) == 1) 		/* fix register encoding */
			nrd = (nrd & 0x1e) | 0x20;
		if (nrd > 48)			/* check upper end */
			return (ftt_fault);
						/* loop 8 times */
		for (i = 0; i < 8; i++, ea += 8, nrd += 2) {
			ftt = _fp_read_word((caddr_t) ea, &k.i[0], pfpsd);
			if (ftt != ftt_none)
				return (ftt);
			ftt = _fp_read_word((caddr_t) (ea+4), &k.i[1], pfpsd);
			if (ftt != ftt_none)
					return (ftt);
			pfpu->fpu_fr.fpu_dregs[DOUBLE(nrd)] =
				k.f.FPU_DREG_FIELD;
		}
		break;
	case 7:		/* STDF and V9 STDFA */
		if ((ea & 0x7) != 0)
			return (ftt_alignment);	/* Require 64 bit-alignment. */
		if ((nrd & 0x1) == 1) 		/* fix register encoding */
			nrd = (nrd & 0x1e) | 0x20;
		if (nrd > 48)			/* check upper end */
			return (ftt_fault);
						/* loop 8 times */
		for (i = 0; i < 8; i++, ea += 8, nrd += 2) {
			k.f.FPU_DREG_FIELD =
				pfpu->fpu_fr.fpu_dregs[DOUBLE(nrd)];
			ftt = _fp_write_word((caddr_t) ea, k.i[0], pfpsd);
			if (ftt != ftt_none)
				return (ftt);
			ftt = _fp_write_word((caddr_t) (ea + 4), k.i[1], pfpsd);
			if (ftt != ftt_none)
				return (ftt);
		}
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
