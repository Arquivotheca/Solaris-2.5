/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)getcontext.c	1.10	95/07/14 SMI"	/* from SVr4.0 1.83 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/vmparam.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/stack.h>
#include <sys/frame.h>
#include <sys/proc.h>
#include <sys/ucontext.h>
#include <sys/asm_linkage.h>
#include <sys/kmem.h>
#include <sys/errno.h>
#include <sys/archsystm.h>
#include <sys/fpu/fpusystm.h>
#include <sys/debug.h>

#include <sys/privregs.h>

extern void setgwins();
extern caddr_t xregs_getptr(struct ucontext *);
extern void xregs_clrptr(struct ucontext *);
extern int xregs_hasptr(struct ucontext *);
extern void xregs_setptr(struct ucontext *, caddr_t);

/*
 * Save user context.
 */
void
savecontext(
	register ucontext_t *ucp,
	k_sigset_t mask)
{
	register proc_t *p = ttoproc(curthread);
	register klwp_t *lwp = ttolwp(curthread);
	register fpregset_t *fp;
	extern void getfpregs(klwp_t *, fpregset_t *);
	extern void getgregs(klwp_t *, gregset_t);
	extern void xregs_getgregs(struct _klwp *, caddr_t);
	extern void xregs_getfpregs(struct _klwp *, caddr_t);

	flush_user_windows_to_stack();

	ucp->uc_flags = UC_ALL;
	ucp->uc_link = lwp->lwp_oldcontext;

	/*
	 * Save current stack state.
	 */
	if (lwp->lwp_sigaltstack.ss_flags == SS_ONSTACK)
		ucp->uc_stack = lwp->lwp_sigaltstack;
	else {
		ucp->uc_stack.ss_sp = (caddr_t)USRSTACK - p->p_stksize;
		ucp->uc_stack.ss_size = p->p_stksize;
		ucp->uc_stack.ss_flags = 0;
	}

	/*
	 * Save machine context.
	 */
	getgregs(lwp, ucp->uc_mcontext.gregs);
	xregs_getgregs(lwp, xregs_getptr(ucp));
	/*
	 * If we are using the floating point unit, save state
	 */
	fp = &ucp->uc_mcontext.fpregs;
	getfpregs(lwp, fp);
	xregs_getfpregs(lwp, xregs_getptr(ucp));
	if (!fp->fpu_en)
		ucp->uc_flags &= ~UC_FPU;
	ucp->uc_mcontext.gwins = (gwindows_t *)NULL;

	/*
	 * Save signal mask.
	 */
	sigktou(&mask, &ucp->uc_sigmask);
}


void
restorecontext(ucontext_t *ucp)
{
	register klwp_id_t lwp = ttolwp(curthread);
	void setfpregs(klwp_id_t, fpregset_t *);
	void setgregs(klwp_id_t, gregset_t);
	extern void xregs_setgregs(struct _klwp *, caddr_t);
	extern void xregs_setfpregs(struct _klwp *, caddr_t);
	extern void run_fpq(struct _klwp *, fpregset_t *);

	flush_user_windows_to_stack();
	if (lwp->lwp_pcb.pcb_xregstat != XREGNONE)
		xregrestore(lwp, 0);

	lwp->lwp_oldcontext = ucp->uc_link;

	if (ucp->uc_flags & UC_STACK) {
		if (ucp->uc_stack.ss_flags == SS_ONSTACK)
			lwp->lwp_sigaltstack = ucp->uc_stack;
		else
			lwp->lwp_sigaltstack.ss_flags &= ~SS_ONSTACK;
	}

	if (ucp->uc_flags & UC_CPU) {
		mcontext_t *mcp = &ucp->uc_mcontext;

		if (mcp->gwins != 0)
			setgwins(lwp, mcp->gwins);
		setgregs(lwp, mcp->gregs);
		xregs_setgregs(lwp, xregs_getptr(ucp));
	}

	if (ucp->uc_flags & UC_FPU) {
		fpregset_t *fp = &ucp->uc_mcontext.fpregs;

		setfpregs(lwp, fp);
		xregs_setfpregs(lwp, xregs_getptr(ucp));
		run_fpq(lwp, fp);
	}

	if (ucp->uc_flags & UC_SIGMASK) {
		sigutok(&ucp->uc_sigmask, &curthread->t_hold);
		sigdiffset(&curthread->t_hold, &cantmask);
		aston(curthread);	/* so thread will see new t_hold */
	}
}

struct setcontexta {
	int flag;
	caddr_t *ucp;
};

/* ARGSUSED */
setcontext(
	register struct setcontexta *uap,
	rval_t *rvp)
{
	ucontext_t uc;
	struct fq fpu_q[MAXFPQ];	/* to hold floating queue	*/
	fpregset_t *fpp;
	gwindows_t gwin;	/* to hold windows		*/
	caddr_t xregs;
	int xregs_size = 0;
	extern int xregs_getsize(void);

	/*
	 * In future releases, when the ucontext structure grows,
	 * getcontext should be modified to only return the fields
	 * specified in the uc_flags.
	 * That way, the structure can grow and still be binary
	 * compatible will all .o's which will only have old fields
	 * defined in uc_flags
	 */

	switch (uap->flag) {

	default:
		return (EINVAL);

	case GETCONTEXT:
		xregs_clrptr(&uc);
		savecontext(&uc, curthread->t_hold);
		/*
		 * When using floating point it should not be possible to get
		 * here with a fpu_qcnt other than zero since we go to great
		 * pains to handle all outstanding FP exceptions before any
		 * system call code gets executed. However we clear fpu_q and
		 * fpu_qcnt here before copyout anyway - this will prevent us
		 * interpreting the garbage we give back when FP is not enabled
		 * as valid queue data on later setcontext(2).
		 */
		uc.uc_mcontext.fpregs.fpu_qcnt = 0;
		uc.uc_mcontext.fpregs.fpu_q = (struct fq *)NULL;
		if (copyout((caddr_t)&uc, (caddr_t)uap->ucp,
		    sizeof (ucontext_t)))
			return (EFAULT);
		return (0);

	case SETCONTEXT:
		if (uap->ucp == NULL)
			exit(CLD_EXITED, 0);
		/*
		 * Don't copyin filler or floating state unless we need it.
		 * The ucontext_t struct and fields are specified in the ABI.
		 */
		if (copyin((caddr_t)uap->ucp, (caddr_t)&uc,
			sizeof (ucontext_t) - sizeof (uc.uc_filler) -
			sizeof (uc.uc_mcontext.fpregs) -
			sizeof (uc.uc_mcontext.xrs) -
			sizeof (uc.uc_mcontext.filler))) {
			return (EFAULT);
		}
		if (copyin((caddr_t)
		    &((struct ucontext *)(uap->ucp))->uc_mcontext.xrs,
		    (caddr_t)&uc.uc_mcontext.xrs,
		    sizeof (uc.uc_mcontext.xrs))) {
				return (EFAULT);
		}
		fpp = &uc.uc_mcontext.fpregs;
		if (uc.uc_flags & UC_FPU) {
			/*
			 * Need to copyin floating point state
			 */
			if (copyin((caddr_t)
			    &((struct ucontext *)(uap->ucp))->
							uc_mcontext.fpregs,
			    (caddr_t)&uc.uc_mcontext.fpregs,
			    sizeof (uc.uc_mcontext.fpregs)))
				return (EFAULT);
			if (fpp->fpu_q) {  /* if floating queue not empty */
				if (copyin((caddr_t)fpp->fpu_q, (caddr_t)fpu_q,
				    fpp->fpu_qcnt * fpp->fpu_q_entrysize))
					return (EFAULT);
				fpp->fpu_q = fpu_q;
			} else {
				fpp->fpu_qcnt = 0; /* avoid confusion later */
			}
		} else {
			fpp->fpu_qcnt = 0;
		}
		if (uc.uc_mcontext.gwins) {	/* if windows in context */
			register unsigned gwin_size;

			/*
			 * We do the same computation here to determine
			 * how many bytes of gwindows_t to copy in that
			 * is also done in sendsig() to decide how many
			 * bytes to copy out.  We just *know* that wbcnt
			 * is the first element of the structure.
			 */
			if (copyin((caddr_t)uc.uc_mcontext.gwins,
			    (caddr_t)&gwin.wbcnt, sizeof (gwin.wbcnt)))
				return (EFAULT);
			gwin_size = gwin.wbcnt * sizeof (struct rwindow) +
			    SPARC_MAXREGWINDOW * sizeof (int *) + sizeof (int);
			if (gwin_size > sizeof (gwindows_t) ||
			    copyin((caddr_t)uc.uc_mcontext.gwins,
			    (caddr_t)&gwin, gwin_size))
				return (EFAULT);
			uc.uc_mcontext.gwins = &gwin;
		}

		/*
		 * get extra register state if any exists
		 */
		if (xregs_hasptr(&uc) &&
		    ((xregs_size = xregs_getsize()) > 0)) {
			xregs = (caddr_t)kmem_zalloc(xregs_size, KM_SLEEP);
			if (copyin(xregs_getptr(&uc), xregs, xregs_size))
				return (EFAULT);
			xregs_setptr(&uc, xregs);
		} else {
			xregs_clrptr(&uc);
		}

		restorecontext(&uc);

		/*
		 * free extra register state area
		 */
		if (xregs_size)
			kmem_free(xregs, xregs_size);

		return (0);
	}
}
