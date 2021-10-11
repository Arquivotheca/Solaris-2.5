/*
 * Copyright (c) 1994, Sun Microsystems, Inc.
 */

#ident  "@(#)xregs.c 1.7     95/07/24 SMI"
#include <sys/t_lock.h>
#include <sys/klwp.h>
#include <sys/ucontext.h>
#include <sys/procfs.h>
#include <sys/privregs.h>
#include <sys/cpuvar.h>
#include <sys/cmn_err.h>
#include <sys/systm.h>

extern int fpdispr;

/*
 * Association of extra register state with a struct ucontext is
 * done by placing an xrs_t within the uc_mcontext filler area.
 *
 * The following routines provide an interface for this association.
 */

/*
 * clear the struct ucontext extra register state pointer
 */
void
xregs_clrptr(uc)
	ucontext_t *uc;
{
	uc->uc_mcontext.xrs.xrs_id = 0;
	uc->uc_mcontext.xrs.xrs_ptr = (caddr_t)NULL;
}

/*
 * indicate whether or not an extra register state
 * pointer is associated with a struct ucontext
 */
int
xregs_hasptr(uc)
	ucontext_t *uc;
{
	return (uc->uc_mcontext.xrs.xrs_id == XRS_ID);
}

/*
 * get the struct ucontext extra register state pointer field
 */
caddr_t
xregs_getptr(uc)
	ucontext_t *uc;
{
	if (uc->uc_mcontext.xrs.xrs_id == XRS_ID)
		return (uc->uc_mcontext.xrs.xrs_ptr);
	return ((caddr_t)NULL);
}

/*
 * set the struct ucontext extra register state pointer field
 */
void
xregs_setptr(uc, xrp)
	ucontext_t *uc;
	caddr_t xrp;
{
	uc->uc_mcontext.xrs.xrs_id = XRS_ID;
	uc->uc_mcontext.xrs.xrs_ptr = xrp;
}

/*
 * Extra register state manipulation routines.
 * NOTE:  'lwp' might not correspond to 'curthread' in any of the
 * functions below since they are called from code in /proc to get
 * or set the extra registers of another lwp.
 */

int xregs_exists = 1;

#define	GET_UPPER_32(all)		(u_long)((u_longlong_t)(all) >> 32)
#define	SET_ALL_64(upper, lower)	\
		(((u_longlong_t)(upper) << 32) | (u_long)(lower))


/*
 * fill in the extra register state area specified with the
 * specified lwp's non-floating-point extra register state
 * information
 */
void
xregs_getgregs(klwp_id_t lwp, caddr_t xrp)
{
	register prxregset_t *xregs = (prxregset_t *)xrp;
	struct regs *rp = lwptoregs(lwp);
	extern void xregs_getgfiller(klwp_id_t, caddr_t);

	if (xregs == (prxregset_t *)NULL)
		return;

	xregs->pr_type = XR_TYPE_V8P;

	xregs->pr_un.pr_v8p.pr_xg[XR_G0] = 0;
	xregs->pr_un.pr_v8p.pr_xg[XR_G1] = GET_UPPER_32(rp->r_g1);
	xregs->pr_un.pr_v8p.pr_xg[XR_G2] = GET_UPPER_32(rp->r_g2);
	xregs->pr_un.pr_v8p.pr_xg[XR_G3] = GET_UPPER_32(rp->r_g3);
	xregs->pr_un.pr_v8p.pr_xg[XR_G4] = GET_UPPER_32(rp->r_g4);
	xregs->pr_un.pr_v8p.pr_xg[XR_G5] = GET_UPPER_32(rp->r_g5);
	xregs->pr_un.pr_v8p.pr_xg[XR_G6] = GET_UPPER_32(rp->r_g6);
	xregs->pr_un.pr_v8p.pr_xg[XR_G7] = GET_UPPER_32(rp->r_g7);

	xregs->pr_un.pr_v8p.pr_xo[XR_O0] = GET_UPPER_32(rp->r_o0);
	xregs->pr_un.pr_v8p.pr_xo[XR_O1] = GET_UPPER_32(rp->r_o1);
	xregs->pr_un.pr_v8p.pr_xo[XR_O2] = GET_UPPER_32(rp->r_o2);
	xregs->pr_un.pr_v8p.pr_xo[XR_O3] = GET_UPPER_32(rp->r_o3);
	xregs->pr_un.pr_v8p.pr_xo[XR_O4] = GET_UPPER_32(rp->r_o4);
	xregs->pr_un.pr_v8p.pr_xo[XR_O5] = GET_UPPER_32(rp->r_o5);
	xregs->pr_un.pr_v8p.pr_xo[XR_O6] = GET_UPPER_32(rp->r_o6);
	xregs->pr_un.pr_v8p.pr_xo[XR_O7] = GET_UPPER_32(rp->r_o7);

	xregs->pr_un.pr_v8p.pr_tstate = rp->r_tstate;

	xregs_getgfiller(lwp, xrp);
}

/*
 * fill in the extra register state area specified with the
 * specified lwp's floating-point extra register state information
 */
void
xregs_getfpregs(klwp_id_t lwp, caddr_t xrp)
{
	register prxregset_t *xregs = (prxregset_t *)xrp;
	kfpu_t *fp = lwptofpu(lwp);
	unsigned fprs = (FPRS_FEF|FPRS_DU|FPRS_DL);
	extern int fpu_exists;
	extern unsigned _fp_read_fprs(void);
	extern void _fp_write_fprs(unsigned);
	extern void fp_v8p_fksave(kfpu_t *);
	extern void xregs_getfpfiller(klwp_id_t, caddr_t);

	if (xregs == (prxregset_t *)NULL)
		return;

	kpreempt_disable();

	xregs->pr_type = XR_TYPE_V8P;

	if (ttolwp(curthread) == lwp)
		fp->fpu_fprs = _fp_read_fprs();
	if ((fp->fpu_en != 0) ||
	    ((fp->fpu_fprs & FPRS_FEF) == FPRS_FEF)) {
		/*
		 * If we have an fpu and the current thread owns the fp
		 * context, flush fp registers into the pcb.
		 * Don't check cpu_fpowner, for memcpy's sake.
		 */
		if (fpu_exists && (ttolwp(curthread) == lwp)) {
			if ((fp->fpu_fprs & FPRS_FEF) != FPRS_FEF) {
				_fp_write_fprs(fprs);
				fp->fpu_fprs = fprs;
				if (fpdispr)
				printf("xregs_getfpregs with fp disabled!\n");
			}
			fp_v8p_fksave(fp);
		}
		kcopy((caddr_t)&fp->fpu_fr.fpu_dregs[16],
			(caddr_t)&xregs->pr_un.pr_v8p.pr_xfr,
			sizeof (xregs->pr_un.pr_v8p.pr_xfr));
		xregs->pr_un.pr_v8p.pr_xfsr = GET_UPPER_32(fp->fpu_fsr);
		xregs->pr_un.pr_v8p.pr_fprs = fp->fpu_fprs;

		xregs_getfpfiller(lwp, xrp);
	}

	kpreempt_enable();
}

/*
 * fill in the extra register state area specified with
 * the specified lwp's extra register state information
 */
void
xregs_get(klwp_id_t lwp, caddr_t xrp)
{
	if (xrp) {
		xregs_getgregs(lwp, xrp);
		xregs_getfpregs(lwp, xrp);
	}
}

/*
 * set the specified lwp's non-floating-point extra
 * register state based on the specified input
 */
void
xregs_setgregs(klwp_id_t lwp, caddr_t xrp)
{
	register prxregset_t *xregs = (prxregset_t *)xrp;
	struct regs *rp = lwptoregs(lwp);
	int current = (lwp == curthread->t_lwp);
	extern void xregs_setgfiller(klwp_id_t, caddr_t);
	extern int save_syscall_args();

	if (xregs == (prxregset_t *)NULL)
		return;

#ifdef DEBUG
	if (xregs->pr_type != XR_TYPE_V8P) {
		cmn_err(CE_WARN,
			"xregs_setgregs: pr_type is %d and should be %d\n",
			xregs->pr_type, XR_TYPE_V8P);
	}
#endif DEBUG

	if (current) {
		/*
		 * copy the args from the regs first
		 */
		(void) save_syscall_args();
	}

	rp->r_g1 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xg[XR_G1], rp->r_g1);
	rp->r_g2 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xg[XR_G2], rp->r_g2);
	rp->r_g3 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xg[XR_G3], rp->r_g3);
	rp->r_g4 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xg[XR_G4], rp->r_g4);
	rp->r_g5 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xg[XR_G5], rp->r_g5);
	rp->r_g6 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xg[XR_G6], rp->r_g6);
	rp->r_g7 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xg[XR_G7], rp->r_g7);

	rp->r_o0 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xo[XR_O0], rp->r_o0);
	rp->r_o1 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xo[XR_O1], rp->r_o1);
	rp->r_o2 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xo[XR_O2], rp->r_o2);
	rp->r_o3 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xo[XR_O3], rp->r_o3);
	rp->r_o4 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xo[XR_O4], rp->r_o4);
	rp->r_o5 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xo[XR_O5], rp->r_o5);
	rp->r_o6 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xo[XR_O6], rp->r_o6);
	rp->r_o7 = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xo[XR_O7], rp->r_o7);

	rp->r_tstate &= ~((u_longlong_t)CCR_XCC << TSTATE_CCR_SHIFT);
	rp->r_tstate |= xregs->pr_un.pr_v8p.pr_tstate &
			((u_longlong_t)CCR_XCC << TSTATE_CCR_SHIFT);
	rp->r_tstate &= ~((u_longlong_t)TSTATE_ASI_MASK << TSTATE_ASI_SHIFT);
	rp->r_tstate |= xregs->pr_un.pr_v8p.pr_tstate &
		((u_longlong_t)TSTATE_ASI_MASK << TSTATE_ASI_SHIFT);

	xregs_setgfiller(lwp, xrp);

	if (current) {
		/*
		 * This was called from a system call, but we
		 * do not want to return via the shared window;
		 * restoring the CPU context changes everything.
		 */
		lwp->lwp_eosys = JUSTRETURN;
		curthread->t_post_sys = 1;
	}
}

/*
 * set the specified lwp's floating-point extra
 * register state based on the specified input
 */
void
xregs_setfpregs(klwp_id_t lwp, caddr_t xrp)
{
	register prxregset_t *xregs = (prxregset_t *)xrp;
	kfpu_t *fp = lwptofpu(lwp);
	unsigned fprs = (FPRS_FEF|FPRS_DU|FPRS_DL);
	extern int fpu_exists;
	extern unsigned _fp_read_fprs(void);
	extern void _fp_write_fprs(unsigned);
	extern void fp_v8p_load(kfpu_t *);
	extern void xregs_setfpfiller(klwp_id_t, caddr_t);

	if (xregs == (prxregset_t *)NULL)
		return;

#ifdef DEBUG
	if (xregs->pr_type != XR_TYPE_V8P) {
		cmn_err(CE_WARN,
			"xregs_setfpregs: pr_type is %d and should be %d\n",
			xregs->pr_type, XR_TYPE_V8P);
	}
#endif DEBUG
	if ((fp->fpu_en) || (xregs->pr_un.pr_v8p.pr_fprs & FPRS_FEF)) {
		kcopy((caddr_t)&xregs->pr_un.pr_v8p.pr_xfr,
			(caddr_t)&fp->fpu_fr.fpu_dregs[16],
			sizeof (xregs->pr_un.pr_v8p.pr_xfr));
		fp->fpu_fprs = xregs->pr_un.pr_v8p.pr_fprs;
		fp->fpu_fsr = SET_ALL_64(xregs->pr_un.pr_v8p.pr_xfsr,
								fp->fpu_fsr);

		xregs_setfpfiller(lwp, xrp);

		kpreempt_disable();

		/*
		 * If not the current lwp then resume() will handle it
		 * Don't check cpu_fpowner, for memcpy's sake.
		 */
		if (lwp != ttolwp(curthread)) {
			/* force resume to reload fp regs */
			if (CPU->cpu_fpowner == lwp)
				CPU->cpu_fpowner = NULL;
			kpreempt_enable();
			return;
		}

		if (fpu_exists) {
			if ((_fp_read_fprs() & FPRS_FEF) != FPRS_FEF) {
				_fp_write_fprs(fprs);
				fp->fpu_fprs = (V9_FPU_FPRS_TYPE)fprs;
				if (fpdispr)
				printf("xregs_setfpregs with fp disabled!\n");
			}
			fp_v8p_load(fp);
		}

		kpreempt_enable();
	}
}

/*
 * set the specified lwp's extra register
 * state based on the specified input
 */
void
xregs_set(klwp_id_t lwp, caddr_t xrp)
{
	if (xrp) {
		xregs_setgregs(lwp, xrp);
		xregs_setfpregs(lwp, xrp);
	}
}

/*
 * return the size of the extra register state
 */
int
xregs_getsize(void)
{
	if (xregs_exists)
		return (sizeof (prxregset_t));
	else
		return (0);
}
