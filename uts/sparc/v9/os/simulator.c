/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 */

#ident	"@(#)simulator.c	1.17	95/10/15 SMI"

/* common code with bug fixes from original version in trap.c */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/inline.h>
#include <sys/debug.h>
#include <sys/privregs.h>
#include <sys/simulate.h>
#include <sys/proc.h>
#include <sys/cmn_err.h>
#include <sys/stack.h>
#include <sys/trap.h>
#include <sys/machtrap.h>
#include <sys/mman.h>
#include <sys/asi.h>
#include <vm/as.h>
#include <vm/page.h>

static int aligndebug = 0;

int getreg(u_longlong_t *, u_int *, u_int, u_longlong_t *, caddr_t *);
int putreg(u_longlong_t *, u_longlong_t *, u_int *, u_int, caddr_t *);

int xcopyin_little(caddr_t, caddr_t, size_t);
int xcopyout_little(caddr_t, caddr_t, size_t);

/*
 * For the sake of those who must be compatible with unaligned
 * architectures, users can link their programs to use a
 * corrective trap handler that will fix unaligned references
 * a special trap #6 (T_FIX_ALIGN) enables this 'feature'.
 * Returns 1 for success, 0 for failure.
 */

int
do_unaligned(rp, badaddr)
	register struct regs	*rp;
	caddr_t			*badaddr;
{
	register u_int	inst, op3, asi = 0;
	register u_int	rd, rs1, rs2;
	register u_longlong_t *rgs;
	register u_int	*rw;
	register int	sz, ltlend = 0;
	register int	floatflg;
	register int	fsrflg;
	register int	immflg;
	register int	lddstdflg;
	register int 	addr;
	u_longlong_t	val;
	extern void	_fp_read_pfreg();
	extern void	_fp_read_pdreg();
	extern void	_fp_write_pfreg();
	extern void	_fp_write_pdreg();
	extern int	fpu_exists;
	union ud {
		u_longlong_t	l[2];
		u_int		i[4];
		u_short		s[8];
		u_char		c[16];
	} data;

	inst = fuword((int *)rp->r_pc);	/* get the instruction */
	op3 = (inst >> 19) & 0x3f;
	rd = (inst >> 25) & 0x1f;
	rs1 = (inst >> 14) & 0x1f;
	rs2 = inst & 0x1f;
	floatflg = (inst >> 24) & 1;
	immflg = (inst >> 13) & 1;
	lddstdflg = fsrflg = 0;

	/* if not load or store do nothing */
	if (((inst >> 30) != 3) || ((op3 & 0xd) == 0xd) ||
	    ((op3 & 0xf) == 0xf))
		return (0);

	if (floatflg) {
		switch ((inst >> 19) & 3) {	/* map size bits to a number */
		case 0: sz = 4; break;		/* ldf{a}/stf{a} */
		case 1: fsrflg = 1;
			if (rd == 0)
				sz = 4;		/* ldfsr/stfsr */
			else
				sz = 8;		/* ldxfsr/stxfsr */
			break;
		case 2: sz = 16; break;		/* ldqf{a}/stqf{a} */
		case 3: sz = 8; break;		/* lddf{a}/sddf{a} */
		}
		/*
		 * Fix to access extra double register encoding plus
		 * compensate to access the correct fpu_dreg.
		 */
		if ((sz > 4) && (fsrflg == 0)) {
			if ((rd & 1) == 1)
				rd = (rd & 0x1e) | 0x20;
			rd = rd >> 1;
		}
	} else {
		register int sz_bits = (inst >> 19) & 0xf;
		switch (sz_bits) {		/* map size bits to a number */
		case 0:				/* lduw{a} */
		case 4:				/* stw{a} */
		case 8:				/* ldsw{a} */
			sz = 4; break;
		case 1:				/* ldub{a} */
		case 5:				/* stb{a} */
		case 9:				/* ldsb{a} */
			sz = 1; break;
		case 2:				/* lduh{a} */
		case 6:				/* sth{a} */
		case 0xa:			/* ldsh{a} */
			sz = 2; break;
		case 3:				/* ldd{a} */
		case 7:				/* std{a} */
			lddstdflg = 1;
			sz = 8; break;
		case 0xb:			/* ldx{a} */
		case 0xe:			/* stx{a} */
			sz = 8; break;
		}
	}


	/* only support primary and secondary asi's */
	if ((op3 >> 4) & 1) {
		if (immflg) {
			asi = (rp->r_tstate >> TSTATE_ASI_SHIFT) &
					TSTATE_ASI_MASK;
		} else {
			asi = (inst >> 5) & 0xff;
		}
		switch (asi) {
		case ASI_P:
		case ASI_S:
			break;
		case ASI_PL:
		case ASI_SL:
			ltlend = 1;
			break;
		default:
			return (0);
		}
	}
	if (aligndebug) {
		printf("unaligned access at 0x%x, instruction: 0x%x\n",
			rp->r_pc, inst);
		printf("type %s", (((inst >> 21) & 1) ? "st" : "ld"));
		if (((inst >> 21) & 1) == 0)
		    printf(" %s", (((inst >> 22) & 1) ? "signed" : "unsigned"));
		printf(" asi 0x%x size %d immflg %d\n", asi, sz, immflg);
		printf("rd = %d, op3 = 0x%x, rs1 = %d, rs2 = %d, imm13=0x%x\n",
			rd, op3, rs1, rs2, (inst & 0x1fff));
	}

	flush_user_windows_to_stack();		/* flush windows into memory */
	rgs = (u_longlong_t *)&rp->r_ps;	/* globals and outs */
	rw = (u_int *)rp->r_sp;			/* ins and locals */

	if (getreg(rgs, rw, rs1, &val, badaddr))
		return (SIMU_FAULT);
	addr = (u_int)val;			/* convert to 32 bit address */
	if (aligndebug)
		cmn_err(CE_CONT, "addr 1 = 0x%x\n", addr);

	/* check immediate bit and use immediate field or reg (rs2) */
	if (immflg) {
		register int imm;
		imm  = inst & 0x1fff;		/* mask out immediate field */
		imm <<= 19;			/* sign extend it */
		imm >>= 19;
		addr += imm;			/* compute address */
	} else {
		if (getreg(rgs, rw, rs2, &val, badaddr))
			return (SIMU_FAULT);
		addr += (u_int)val;
	}

	if (aligndebug)
		cmn_err(CE_CONT, "addr 2 = 0x%x\n", addr);
	if ((u_int)addr >= USERLIMIT) {
		*badaddr = (caddr_t)addr;
		goto badret;
	}

	/* a single bit differentiates ld and st */
	if ((inst >> 21) & 1) {			/* store */
		if (floatflg) {
			/* if fpu_exists read fpu reg */
			if (fpu_exists) {
				if (fsrflg) {
					_fp_read_pfsr(&data.l[0]);
				} else {
					if (sz == 4) {
						data.i[0] = 0;
						_fp_read_pfreg(
						    (unsigned *)&data.i[1], rd);
					}
					if (sz >= 8)
						_fp_read_pdreg(
							&data.l[0], rd);
					if (sz == 16)
						_fp_read_pdreg(
							&data.l[1], rd+1);
				}
			} else {
				klwp_id_t lwp = ttolwp(curthread);
				kfpu_t *fp = lwptofpu(lwp);
				if (fsrflg) {
					data.l[0] = fp->fpu_fsr;
				} else {
					if (sz == 4) {
						data.i[0] = 0;
						data.i[1] =
					    (unsigned)fp->fpu_fr.fpu_regs[rd];
					}
					if (sz >= 8)
						data.l[0] =
						    fp->fpu_fr.fpu_dregs[rd];
					if (sz == 16)
						data.l[1] =
						    fp->fpu_fr.fpu_dregs[rd+1];
				}
			}
		} else {
			if (lddstdflg) {
				if (getreg(rgs, rw, rd, &data.l[0], badaddr))
					return (SIMU_FAULT);
				if (getreg(rgs, rw, rd+1, &data.l[1], badaddr))
					return (SIMU_FAULT);
				data.i[0] = data.i[1];	/* combine the data */
				data.i[1] = data.i[3];
			} else {
				if (getreg(rgs, rw, rd, &data.l[0], badaddr))
					return (SIMU_FAULT);
			}
		}

		if (aligndebug) {
			if (sz == 16) {
				cmn_err(CE_CONT, "data %x %x %x %x\n",
				    data.i[0], data.i[1], data.i[2], data.c[3]);
			} else {
				cmn_err(CE_CONT,
				    "data %x %x %x %x %x %x %x %x\n",
				    data.c[0], data.c[1], data.c[2], data.c[3],
				    data.c[4], data.c[5], data.c[6], data.c[7]);
			}
		}

		if (ltlend) {
			if (sz == 1) {
				if (xcopyout_little((caddr_t)&data.c[7],
				    (caddr_t)addr, (u_int)sz) != 0)
					goto badret;
			} else if (sz == 2) {
				if (xcopyout_little((caddr_t)&data.s[3],
				    (caddr_t)addr, (u_int)sz) != 0)
					goto badret;
			} else if (sz == 4) {
				if (xcopyout_little((caddr_t)&data.i[1],
				    (caddr_t)addr, (u_int)sz) != 0)
					goto badret;
			} else {
				if (xcopyout_little((caddr_t)&data.i[0],
				    (caddr_t)addr, (u_int)sz) != 0)
					goto badret;
			}
		} else {
			if (sz == 1) {
				if (copyout((caddr_t)&data.c[7], (caddr_t)addr,
				    (u_int)sz) == -1)
				goto badret;
			} else if (sz == 2) {
				if (copyout((caddr_t)&data.s[3], (caddr_t)addr,
				    (u_int)sz) == -1)
				goto badret;
			} else if (sz == 4) {
				if (copyout((caddr_t)&data.i[1], (caddr_t)addr,
				    (u_int)sz) == -1)
				goto badret;
			} else {
				if (copyout((caddr_t)&data.i[0], (caddr_t)addr,
				    (u_int)sz) == -1)
				goto badret;
			}
		}
	} else {				/* load */
		if (sz == 1) {
			if (ltlend) {
				if (xcopyin_little((caddr_t)addr,
				    (caddr_t)&data.c[7], (u_int)sz) != 0)
					goto badret;
			} else {
				if (copyin((caddr_t)addr, (caddr_t)&data.c[7],
				    (u_int)sz) == -1)
					goto badret;
			}
			/* if signed and the sign bit is set extend it */
			if (((inst >> 22) & 1) && ((data.c[7] >> 7) & 1)) {
				data.i[0] = (u_int)-1;	/* extend sign bit */
				data.s[2] = (u_short)-1;
				data.c[6] = (u_char)-1;
			} else {
				data.i[0] = 0;	/* clear upper 32+24 bits */
				data.s[2] = 0;
				data.c[6] = 0;
			}
		} else if (sz == 2) {
			if (ltlend) {
				if (xcopyin_little((caddr_t)addr,
				    (caddr_t)&data.s[3], (u_int)sz) != 0)
					goto badret;
			} else {
				if (copyin((caddr_t)addr, (caddr_t)&data.s[3],
				    (u_int)sz) == -1)
					goto badret;
			}
			/* if signed and the sign bit is set extend it */
			if (((inst >> 22) & 1) && ((data.s[3] >> 15) & 1)) {
				data.i[0] = (u_int)-1;	/* extend sign bit */
				data.s[2] = (u_short)-1;
			} else {
				data.i[0] = 0;	/* clear upper 32+16 bits */
				data.s[2] = 0;
			}
		} else if (sz == 4) {
			if (ltlend) {
				if (xcopyin_little((caddr_t)addr,
				    (caddr_t)&data.i[1], (u_int)sz) != 0)
					goto badret;
			} else {
				if (copyin((caddr_t)addr, (caddr_t)&data.i[1],
				    (u_int)sz) == -1)
					goto badret;
			}
			/* if signed and the sign bit is set extend it */
			if (((inst >> 22) & 1) && ((data.i[1] >> 31) & 1)) {
				data.i[0] = (u_int)-1;	/* extend sign bit */
			} else {
				data.i[0] = 0;	/* clear upper 32 bits */
			}
		} else {
			if (ltlend) {
				if (xcopyin_little((caddr_t)addr,
				    (caddr_t)&data.i[0], (u_int)sz) != 0)
					goto badret;
			} else {
				if (copyin((caddr_t)addr, (caddr_t)&data.i[0],
				    (u_int)sz) == -1)
					goto badret;
			}
		}

		if (aligndebug) {
			if (sz == 16) {
				cmn_err(CE_CONT, "data %x %x %x %x\n",
				    data.i[0], data.i[1], data.i[2], data.c[3]);
			} else {
				cmn_err(CE_CONT,
				    "data %x %x %x %x %x %x %x %x\n",
				    data.c[0], data.c[1], data.c[2], data.c[3],
				    data.c[4], data.c[5], data.c[6], data.c[7]);
			}
		}

		if (floatflg) {		/* if fpu_exists write fpu reg */
			if (fpu_exists) {
				if (fsrflg) {
					_fp_write_pfsr(&data.l[0]);
				} else {
					if (sz == 4)
						_fp_write_pfreg(
						    (unsigned *)&data.i[1], rd);
					if (sz >= 8)
						_fp_write_pdreg(
							&data.l[0], rd);
					if (sz == 16)
						_fp_write_pdreg(
							&data.l[1], rd+1);
				}
			} else {
				klwp_id_t lwp = ttolwp(curthread);
				kfpu_t *fp = lwptofpu(lwp);
				if (fsrflg) {
					fp->fpu_fsr = data.l[0];
				} else {
					if (sz == 4)
						fp->fpu_fr.fpu_regs[rd] =
							(unsigned) data.i[1];
					if (sz >= 8)
						fp->fpu_fr.fpu_dregs[rd] =
							data.l[0];
					if (sz == 16)
						fp->fpu_fr.fpu_dregs[rd+1] =
							data.l[1];
				}
			}
		} else {
			if (lddstdflg) {		/* split the data */
				data.i[2] = 0;
				data.i[3] = data.i[1];
				data.i[1] = data.i[0];
				data.i[0] = 0;
				if (putreg(&data.l[0], rgs, rw, rd,
				    badaddr) == -1)
					goto badret;
				if (putreg(&data.l[1], rgs, rw, rd+1,
				    badaddr) == -1)
					goto badret;
			} else {
				if (putreg(&data.l[0], rgs, rw, rd,
				    badaddr) == -1)
					goto badret;
			}
		}
	}
	return (SIMU_SUCCESS);
badret:
	return (SIMU_FAULT);
}

/*
 * simulate unimplemented instructions (popc, ldqf{a}, stqf{a})
 */
int
simulate_unimp(rp, badaddr)
	struct regs	*rp;
	caddr_t		*badaddr;
{
	u_int	rs1, rd, optype, ignor, i;
	u_int	inst, op3;
	register proc_t *p = ttoproc(curthread);
	struct as *as;
	int	simulate_popc();
	caddr_t	naddr, ka;
	u_int	prot, pfnum;
	page_t *pp;

	if ((inst = fuword((int *)rp->r_pc)) == -1) {
		/*
		 * -1 is an illegal instruction or a error in fuword,
		 * give up now
		 */
		return (SIMU_ILLEGAL);
	}

	/* instruction fields */
	i = (inst >> 13) & 0x1;
	rd = (inst >> 25) & 0x1f;
	optype = (inst >> 30) & 0x3;
	op3 = (inst >> 19) & 0x3f;
	ignor = (inst >> 5) & 0xff;

	if (op3 == IOP_V8_POPC)
		return (simulate_popc(rp, badaddr, inst));
	if (op3 == IOP_V8_LDQF || op3 == IOP_V8_LDQFA ||
	    op3 == IOP_V8_STQFA)
		return (do_unaligned(rp, badaddr));

	/*
	 * The rest of the code handles v8 binaries with instructions
	 * that have dirty (non-zero) bits in reserved or 'ignored'
	 * fields; these will cause core dumps on v9 machines.
	 */
	switch (optype) {
	case OP_V8_BRANCH:
	case OP_V8_CALL:
		return (SIMU_ILLEGAL);	/* these don't have ignored fields */
		/*NOTREACHED*/
	case OP_V8_ARITH:
		switch (op3) {
		case IOP_V8_RETT:
			if (rd == 0 && !(i == 0 && ignor))
				return (SIMU_ILLEGAL);
			if (rd)
				inst &= ~(0x1f << 25);
			if (i == 0 && ignor)
				inst &= ~(0xff << 5);
			break;
		case IOP_V8_TCC:
		case IOP_V8_JMPL:
		case IOP_V8_RESTORE:
		case IOP_V8_SAVE:
			if ((op3 == IOP_V8_RETT && rd) ||
			    (i == 0 && ignor)) {
				inst &= ~(0xff << 5);
			} else {
				return (SIMU_ILLEGAL);
			}
			break;
		case IOP_V8_FCMP:
			if (rd == 0)
				return (SIMU_ILLEGAL);
			else
				inst &= ~(0x1f << 25);
			break;
		case IOP_V8_RDASR:
			rs1 = ((inst >> 14) & 0x1f);
			if (rs1 == 1 || (rs1 >= 7 && rs1 <= 14)) {
				/*
				 * The instruction specifies an invalid
				 * state register - better bail out than
				 * "fix" it when we're not sure what was
				 * intended.
				 */
				return (SIMU_ILLEGAL);
			} else {
				/*
				 * Note: this case includes the 'stbar'
				 * instruction (rs1 == 15 && i == 0).
				 */
				if ((ignor = (inst & 0x3fff)) != 0)
					inst &= ~(0x3fff);
			}
			break;
		case IOP_V8_SRA:
		case IOP_V8_SRL:
		case IOP_V8_SLL:
			if (ignor == 0)
				return (SIMU_ILLEGAL);
			else
				inst &= ~(0xff << 5);
			break;
		case IOP_V8_WRASR:
			if (i == 0 && ignor)
				inst &= ~(0xff << 5);
			else
				return (SIMU_ILLEGAL);
			break;
		default:
			return (SIMU_ILLEGAL);
		}
		break;
	case OP_V8_LDSTR:
		switch (op3) {
		case IOP_V8_STFSR:
		case IOP_V8_LDFSR:
			if (rd == 0 && !(i == 0 && ignor))
				return (SIMU_ILLEGAL);
			if (rd)
				inst &= ~(0x1f << 25);
			if (i == 0 && ignor)
				inst &= ~(0xff << 5);
			break;
		default:
			if (optype == OP_V8_LDSTR && !IS_LDST_ALT(op3) &&
			    i == 0 && ignor)
				inst &= ~(0xff << 5);
			else
				return (SIMU_ILLEGAL);
			break;
		}
		break;
	default:
		return (SIMU_ILLEGAL);
	}

	/*
	 * A "flush" instruction using the user PC's vaddr will not work
	 * here, at least on Spitfire. Instead we create a temporary kernel
	 * mapping to the user's text page, then modify and flush that.
	 */
	as = p->p_as;
	AS_LOCK_ENTER(as, &as->a_lock, RW_READER);
	pfnum = hat_getpfnum(as, (caddr_t)rp->r_pc);
	if ((pp = page_numtopp(pfnum, SE_SHARED)) == (page_t *)0) {
		AS_LOCK_EXIT(as, &as->a_lock);
		return (SIMU_ILLEGAL);
	}
	ka = (caddr_t)ppmapin(pp, PROT_READ|PROT_WRITE, (caddr_t)rp->r_pc);
	*(u_int *)(ka + (int)(rp->r_pc % PAGESIZE)) = inst;
	doflush(ka + (int)(rp->r_pc % PAGESIZE));
	ppmapout((caddr_t)((long)ka));
	page_unlock(pp);
	AS_LOCK_EXIT(as, &as->a_lock);

	return (SIMU_RETRY);
}

/*
 * simulate popc
 */
int
simulate_popc(rp, badaddr, inst)
	struct regs	*rp;
	caddr_t		*badaddr;
	u_int		inst;
{
	register u_int	rd, rs1, rs2;
	register u_int	immflg;
	register u_longlong_t	*rgs;
	register u_int	*rw;
	u_longlong_t	val, cnt = 0;

	rd = (inst >> 25) & 0x1f;
	rs1 = (inst >> 14) & 0x1f;
	rs2 = inst & 0x1f;
	immflg = (inst >> 13) & 1;

	flush_user_windows_to_stack();		/* flush windows into memory */
	rgs = (u_longlong_t *)&rp->r_ps;	/* globals and outs */
	rw = (u_int *)rp->r_sp;			/* ins and locals */

	if (getreg(rgs, rw, rs1, &val, badaddr))
		return (SIMU_FAULT);

	/* check immediate bit and use immediate field or reg (rs2) */
	if (immflg) {
		register int imm;
		imm  = inst & 0x1fff;		/* mask out immediate field */
		imm <<= 19;			/* sign extend it */
		imm >>= 19;
		if (imm != 0) {
			for (cnt = 0; imm != 0; imm &= imm-1)
				cnt++;
		}
	} else {
		if (getreg(rgs, rw, rs2, &val, badaddr))
			return (SIMU_FAULT);
		if (val != 0) {
			for (cnt = 0; val != 0; val &= val-1)
				cnt++;
		}
	}

	if (putreg((u_longlong_t *)&cnt, rgs, rw, rd, badaddr) == -1)
		return (SIMU_FAULT);

	return (SIMU_SUCCESS);
}

/*
 * Get the value of a register for instruction simulation
 * by using the regs or window structure pointers.
 * Return 0 for succes -1 failure.  If there is a failure,
 * save the faulting address using badaddr pointer.
 * We have 64 bit globals and outs, and 32 bit ins and locals.
 * Need to check lofault because -1 is a legit return value from fuword.
 */
int
getreg(rgs, rw, reg, val, badaddr)
	u_longlong_t *rgs;
	u_int	*rw, reg;
	u_longlong_t *val;
	caddr_t	*badaddr;
{
	union ull {
		u_longlong_t	ll;
		u_int		i[2];
	} kluge;

	if (reg == 0)
		*val = 0;
	else if (reg < 16)
		*val = rgs[reg];
	else {
		kluge.i[0] = 0;
		if ((kluge.i[1] = fuword((int *)&rw[reg-16])) == -1) {
			if (fubyte((caddr_t)&rw[reg-16]) == -1) {
				*badaddr = (caddr_t)&rw[reg-16];
				return (-1);
			}
		}
		*val = kluge.ll;
	}
	return (0);
}

/*
 * Set the value of a register after instruction simulation
 * by using the regs or window structure pointers.
 * Return 0 for succes -1 failure.
 * save the faulting address using badaddr pointer.
 * We have 64 bit globals and outs, and 32 bit ins and locals.
 */
int
putreg(data, rgs, rw, reg, badaddr)
	u_longlong_t *data, *rgs;
	u_int	*rw, reg;
	caddr_t	*badaddr;
{
	union ull {
		u_longlong_t	ll;
		u_int		i[2];
	} kluge;

	if (reg == 0)
		return (0);
	if (reg < 16) {
		rgs[reg] = *data;
	} else {
		kluge.ll = *data;
		if (suword((int *)&rw[reg-16], (int)kluge.i[1]) != 0) {
			*badaddr = (caddr_t)&rw[reg-16];
			return (-1);
		}
	}
	return (0);
}

/*
 * Calculate a memory reference address from instruction
 * operands, used to return the address of a fault, instead
 * of the instruction when an error occurs.  This is code that is
 * common with most of the routines that simulate instructions.
 */
int
calc_memaddr(rp, badaddr)
	register struct regs	*rp;
	caddr_t			*badaddr;
{
	register u_int	inst;
	register u_int	rd, rs1, rs2;
	register u_longlong_t *rgs;
	register u_int	*rw;
	register int	sz;
	register int	immflg;
	register int	floatflg;
	register int 	addr;
	u_longlong_t	val;

	inst = fuword((int *)rp->r_pc);	/* get the instruction */
	rd = (inst >> 25) & 0x1f;
	rs1 = (inst >> 14) & 0x1f;
	rs2 = inst & 0x1f;
	floatflg = (inst >> 24) & 1;
	immflg = (inst >> 13) & 1;

	if (floatflg) {
		switch ((inst >> 19) & 3) {	/* map size bits to a number */
		case 0: sz = 4; break;		/* ldf/stf */
		case 1: return (0);		/* ld[x]fsr/st[x]fsr */
		case 2: sz = 16; break;		/* ldqf/stqf */
		case 3: sz = 8; break;		/* lddf/sddf */
		}
		/*
		 * Fix to access extra double register encoding plus
		 * compensate to access the correct fpu_dreg.
		 */
		if (sz > 4) {
			if ((rd & 1) == 1)
				rd = (rd & 0x1e) | 0x20;
			rd = rd >> 1;
		}
	} else {
		switch ((inst >> 19) & 0xf) {	/* map size bits to a number */
		case 0:				/* lduw */
		case 4:				/* stw */
		case 8:				/* ldsw */
		case 0xf:			/* swap */
			sz = 4; break;
		case 1:				/* ldub */
		case 5:				/* stb */
		case 9:				/* ldsb */
		case 0xd:			/* ldstub */
			sz = 1; break;
		case 2:				/* lduh */
		case 6:				/* sth */
		case 0xa:			/* ldsh */
			sz = 2; break;
		case 3:				/* ldd */
		case 7:				/* std */
		case 0xb:			/* ldx */
		case 0xe:			/* stx */
			sz = 8; break;
		}
	}

	flush_user_windows_to_stack();		/* flush windows into memory */
	rgs = (u_longlong_t *)&rp->r_ps;	/* globals and outs */
	rw = (u_int *)rp->r_sp;			/* ins and locals */

	if (getreg(rgs, rw, rs1, &val, badaddr))
		return (SIMU_FAULT);
	addr = (int)val;

	/* check immediate bit and use immediate field or reg (rs2) */
	if (immflg) {
		register int imm;
		imm  = inst & 0x1fff;		/* mask out immediate field */
		imm <<= 19;			/* sign extend it */
		imm >>= 19;
		addr += imm;			/* compute address */
	} else {
		if (getreg(rgs, rw, rs2, &val, badaddr))
			return (SIMU_FAULT);
		addr += (int)val;
	}

	*badaddr = (caddr_t)addr;
	return (addr & (sz - 1) ? SIMU_UNALIGN : SIMU_SUCCESS);
}
