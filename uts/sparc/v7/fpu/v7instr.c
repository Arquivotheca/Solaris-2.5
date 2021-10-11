/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ident	"@(#)v7instr.c	1.2	94/10/14 SMI"
		/* SunOS-4.1 1.8 88/11/30 */

/* Integer Unit simulator for Sparc FPU simulator. */

#include <sys/fpu/fpu_simulator.h>
#include <sys/fpu/globals.h>

#include <sys/privregs.h>

#define	FPU_REG_FIELD unsigned_reg	/* Coordinate with FPU_REGS_TYPE. */
#define	FPU_FSR_FIELD unsigned_reg	/* Coordinate with FPU_FSR_TYPE. */

/*
 * Simulator for loads and stores between floating-point unit and memory.
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
	int		ea;
	int		i;
	union {
		fp_inst_type	fi;
		int		i;
	} kluge;
	enum ftt_type   ftt;

	nrs1 = pinst.rs1;
	nrs2 = pinst.rs2;
	nrd = pinst.rd;
	if (pinst.ibit == 0) {	/* effective address = rs1 + rs2 */
		ftt = read_iureg(pfpsd, nrs1, pregs, pwindow, &ea);
		if (ftt != ftt_none)
			return (ftt);
		ftt = read_iureg(pfpsd, nrs2, pregs, pwindow, &i);
		if (ftt != ftt_none)
			return (ftt);
		ea += i;
	} else {		/* effective address = rs1 + imm13 */
		kluge.fi = pinst;
		ea = (kluge.i << 19) >> 19;	/* Extract simm13 field. */
		ftt = read_iureg(pfpsd, nrs1, pregs, pwindow, &i);
		if (ftt != ftt_none)
			return (ftt);
		ea += i;
	}

	pfpsd->fp_trapaddr = (char *) ea; /* setup bad addr in case we trap */
	switch (pinst.op3 & 7) {
	case 0:		/* LDF */
		ftt = _fp_read_word((caddr_t) ea, &(f.int_reg), pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		pfpu->fpu_fr.fpu_regs[nrd] = f.FPU_REG_FIELD;
		break;
	case 1:		/* LDFSR */
		ftt = _fp_read_word((caddr_t) ea, &(f.int_reg), pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		pfpu->fpu_fsr = f.FPU_FSR_FIELD;
		break;
	case 3:		/* LDDF */
		if ((ea & 0x7) != 0)
			return (ftt_alignment);	/* Require double-alignment. */
		ftt = _fp_read_word((caddr_t) ea, &(f.int_reg), pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		pfpu->fpu_fr.fpu_regs[DOUBLE_E(nrd)] = f.FPU_REG_FIELD;
		ftt = _fp_read_word((caddr_t) (ea + 4), &(f.int_reg), pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		pfpu->fpu_fr.fpu_regs[DOUBLE_F(nrd)] = f.FPU_REG_FIELD;
		break;
	case 4:		/* STF */
		f.FPU_REG_FIELD = pfpu->fpu_fr.fpu_regs[nrd];
		ftt = _fp_write_word((caddr_t) ea, f.int_reg, pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		break;
	case 5:		/* STFSR */
		f.FPU_FSR_FIELD = pfpu->fpu_fsr;
		f.FPU_FSR_FIELD &= ~0x301000;	/* Clear reserved bits. */
		f.FPU_FSR_FIELD |= 0x0E0000;	/* Set version number=7 . */
		ftt = _fp_write_word((caddr_t) ea, f.int_reg, pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		break;
	case 7:		/* STDF */
		if ((ea & 0x7) != 0)
			return (ftt_alignment);	/* Require double-alignment. */
		f.FPU_REG_FIELD = pfpu->fpu_fr.fpu_regs[DOUBLE_E(nrd)];
		ftt = _fp_write_word((caddr_t) ea, f.int_reg, pfpsd);
		if (ftt != ftt_none)
			return (ftt);
		f.FPU_REG_FIELD = pfpu->fpu_fr.fpu_regs[DOUBLE_F(nrd)];
		ftt = _fp_write_word((caddr_t) (ea + 4), f.int_reg, pfpsd);
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
