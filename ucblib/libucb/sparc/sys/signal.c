/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)signal.c	1.13	95/08/18 SMI"	/* SVr4.0 1.7	*/

/*******************************************************************

		PROPRIETARY NOTICE (Combined)

This source code is unpublished proprietary information
constituting, or derived under license from AT&T's UNIX(r) System V.
In addition, portions of such source code were derived from Berkeley
4.3 BSD under license from the Regents of the University of
California.

		Copyright Notice

Notice of copyright on this source code product does not indicate
publication.

	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
		  All rights reserved.
********************************************************************/

/*
 * 4.3BSD signal compatibility functions
 *
 * the implementation interprets signal masks equal to -1 as "all of the
 * signals in the signal set", thereby allowing signals with numbers
 * above 32 to be blocked when referenced in code such as:
 *
 *	for (i = 0; i < NSIG; i++)
 *		mask |= sigmask(i)
 */

#include <sys/types.h>
#include <sys/ucontext.h>
#include <signal.h>
#include <errno.h>

#undef	BUS_OBJERR	/* namespace conflict */
#include <sys/siginfo.h>

#pragma weak sigvechandler = _sigvechandler
#pragma weak sigsetmask = _sigsetmask
#pragma weak sigblock = _sigblock
#pragma weak sigpause = usigpause
#pragma weak sigvec = _sigvec
#pragma weak sigstack = _sigstack
#pragma weak signal = usignal
#pragma weak siginterrupt = _siginterrupt

#define	set2mask(setp) ((setp)->__sigbits[0])
#define	mask2set(mask, setp) \
	((mask) == -1 ? _sigfillset(setp) : \
	    (_sigemptyset(setp), (((setp)->__sigbits[0]) = (int)(mask))))

extern void (*_siguhandler[])();

/*
 * sigvechandler is the real signal handler installed for all
 * signals handled in the 4.3BSD compatibility interface - it translates
 * SVR4 signal hander arguments into 4.3BSD signal handler arguments
 * and then calls the real handler
 */

_sigvechandler(sig, sip, ucp)
	int sig;
	siginfo_t *sip;
	ucontext_t *ucp;
{
	static void ucbsigvechandler();

	ucbsigvechandler(sig, sip, ucp);
}

static void
ucbsigvechandler(sig, sip, ucp)
	int sig;
	siginfo_t *sip;
	ucontext_t *ucp;
{
	struct sigcontext sc;
	int code;
	char *addr;
	register int i, j;
	int gwinswitch = 0;

	sc.sc_onstack = ((ucp->uc_stack.ss_flags & SS_ONSTACK) != 0);
	sc.sc_mask = set2mask(&ucp->uc_sigmask);

#ifdef sparc
	if (sig == SIGFPE && sip != NULL && SI_FROMKERNEL(sip) &&
	    (sip->si_code == FPE_INTDIV || sip->si_code == FPE_INTOVF)) {
		/*
		 * Hack to emulate the 4.x kernel behavior of incrementing
		 * the PC on integer divide by zero and integer overflow
		 * on sparc machines.  (5.x does not increment the PC.)
		 */
		ucp->uc_mcontext.gregs[REG_PC] =
			ucp->uc_mcontext.gregs[REG_nPC];
		ucp->uc_mcontext.gregs[REG_nPC] += 4;
	}
	sc.sc_sp = (int)(ucp->uc_mcontext.gregs[REG_SP]);
	sc.sc_pc = (int)(ucp->uc_mcontext.gregs[REG_PC]);
	sc.sc_npc = (int)(ucp->uc_mcontext.gregs[REG_nPC]);
	sc.sc_psr = (int)(ucp->uc_mcontext.gregs[REG_PSR]);
	sc.sc_g1 = (int)(ucp->uc_mcontext.gregs[REG_G1]);
	sc.sc_o0 = (int)(ucp->uc_mcontext.gregs[REG_O0]);

	/*
	 * XXX - What a kludge!
	 * Store a pointer to the original ucontext_t in the sigcontext
	 * so that it's available to the sigcleanup call that needs to
	 * return from the signal handler.  Otherwise, vital information
	 * (e.g., the "out" registers) that's only saved in the
	 * ucontext_t isn't available to sigcleanup.
	 */
	sc.sc_wbcnt = sizeof (*ucp);
	sc.sc_spbuf[0] = (char *)sig;
	sc.sc_spbuf[1] = (char *)ucp;
#ifdef NEVER
	/*
	 * XXX - Sorry, we can never pass the saved register windows
	 * on in the sigcontext because we use that space to save the
	 * ucontext_t.
	 */
	if (ucp->uc_mcontext.gwins != (gwindows_t *)0) {
		gwinswitch = 1;
		sc.sc_wbcnt = ucp->uc_mcontext.gwins->wbcnt;
		/* XXX - should use bcopy to move this in bulk */
		for (i = 0; i < ucp->uc_mcontext.gwins; i++) {
			sc.sc_spbuf[i] = ucp->uc_mcontext.gwins->spbuf[i];
			for (j = 0; j < 8; j++)
				sc.sc_wbuf[i][j] =
				    ucp->uc_mcontext.gwins->wbuf[i].rw_local[j];
			for (j = 0; j < 8; j++)
				sc.sc_wbuf[i][j+8] =
				    ucp->uc_mcontext.gwins->wbuf[i].rw_in[j];
		}
	}
#endif
#endif

	/*
	 * Translate signal codes from new to old.
	 * /usr/include/sys/siginfo.h contains new codes.
	 * /usr/ucbinclude/sys/signal.h contains old codes.
	 */
	code = 0;
	addr = SIG_NOADDR;
	if (sip != NULL && SI_FROMKERNEL(sip)) {
		addr = sip->si_addr;

		switch (sig) {
		case SIGILL:
			switch (sip->si_code) {
			case ILL_PRVOPC:
				code = ILL_PRIVINSTR_FAULT;
				break;
			case ILL_BADSTK:
				code = ILL_STACK;
				break;
			case ILL_ILLTRP:
				code = ILL_TRAP_FAULT(sip->si_trapno);
				break;
			default:
				code = ILL_ILLINSTR_FAULT;
				break;
			}
			break;

		case SIGEMT:
			code = EMT_TAG;
			break;

		case SIGFPE:
			switch (sip->si_code) {
			case FPE_INTDIV:
				code = FPE_INTDIV_TRAP;
				break;
			case FPE_INTOVF:
				code = FPE_INTOVF_TRAP;
				break;
			case FPE_FLTDIV:
				code = FPE_FLTDIV_TRAP;
				break;
			case FPE_FLTOVF:
				code = FPE_FLTOVF_TRAP;
				break;
			case FPE_FLTUND:
				code = FPE_FLTUND_TRAP;
				break;
			case FPE_FLTRES:
				code = FPE_FLTINEX_TRAP;
				break;
			default:
				code = FPE_FLTOPERR_TRAP;
				break;
			}
			break;

		case SIGBUS:
			switch (sip->si_code) {
			case BUS_ADRALN:
				code = BUS_ALIGN;
				break;
			case BUS_ADRERR:
				code = BUS_HWERR;
				break;
			default:	/* BUS_OBJERR */
				code = FC_MAKE_ERR(sip->si_errno);
				break;
			}
			break;

		case SIGSEGV:
			switch (sip->si_code) {
			case SEGV_MAPERR:
				code = SEGV_NOMAP;
				break;
			case SEGV_ACCERR:
				code = SEGV_PROT;
				break;
			default:
				code = FC_MAKE_ERR(sip->si_errno);
				break;
			}
			break;

		default:
			addr = SIG_NOADDR;
			break;
		}
	}

	(*_siguhandler[sig])(sig, code, &sc, addr);

	if (sc.sc_onstack)
		ucp->uc_stack.ss_flags |= SS_ONSTACK;
	else
		ucp->uc_stack.ss_flags &= ~SS_ONSTACK;
	mask2set(sc.sc_mask, &ucp->uc_sigmask);

#ifdef sparc
	ucp->uc_mcontext.gregs[REG_SP] = (int)sc.sc_sp;
	ucp->uc_mcontext.gregs[REG_PC] = (int)sc.sc_pc;
	ucp->uc_mcontext.gregs[REG_nPC] = (int)sc.sc_npc;
	ucp->uc_mcontext.gregs[REG_PSR] = (int)sc.sc_psr;
	ucp->uc_mcontext.gregs[REG_G1] = (int)sc.sc_g1;
	ucp->uc_mcontext.gregs[REG_O0] = (int)sc.sc_o0;
#ifdef NEVER
	if (gwinswitch == 1) {
		ucp->uc_mcontext.gwins->wbcnt = sc.sc_wbcnt;
		/* XXX - should use bcopy to move this in bulk */
		for (i = 0; i < sc.sc_wbcnt; i++) {
			ucp->uc_mcontext.gwins->spbuf[i] = sc.sc_spbuf[i];
			for (j = 0; j < 8; j++)
				ucp->uc_mcontext.gwins->wbuf[i].rw_local[j] =
				    sc.sc_wbuf[i][j];
			for (j = 0; j < 8; j++)
				ucp->uc_mcontext.gwins->wbuf[i].rw_in[j] =
				    sc.sc_wbuf[i][j+8];
		}
	}
#endif

	if (sig == SIGFPE) {
		if (ucp->uc_mcontext.fpregs.fpu_qcnt > 0) {
			ucp->uc_mcontext.fpregs.fpu_qcnt--;
			ucp->uc_mcontext.fpregs.fpu_q++;
		}
	}
#endif

	setcontext(ucp);
}

#ifdef sparc
/*
 * Emulate the special sigcleanup trap.
 * This is only used by statically linked 4.x applications
 * and thus is only called by the static BCP support.
 * It lives here because of its close relationship with
 * the ucbsigvechandler code above.
 *
 * It's used by 4.x applications to:
 *	1. return from a signal handler (in __sigtramp)
 *	2. [sig]longjmp
 *	3. context switch, in the old 4.x liblwp
 */
void
__sigcleanup(scp)
	struct sigcontext *scp;
{
	ucontext_t uc, *ucp;
	int sig;

	/*
	 * If there's a pointer to a ucontext_t hiding in the sigcontext,
	 * we *must* use that to return, since it contains important data
	 * such as the original "out" registers when the signal occurred.
	 */
	if (scp->sc_wbcnt == sizeof (*ucp)) {
		sig = (int)scp->sc_spbuf[0];
		ucp = (ucontext_t *)scp->sc_spbuf[1];
	} else {
		/*
		 * Otherwise, use a local ucontext_t and
		 * initialize it with getcontext.
		 */
		sig = 0;
		ucp = &uc;
		getcontext(ucp);
	}

	if (scp->sc_onstack) {
		ucp->uc_stack.ss_flags |= SS_ONSTACK;
	} else
		ucp->uc_stack.ss_flags &= ~SS_ONSTACK;
	mask2set(scp->sc_mask, &ucp->uc_sigmask);

	ucp->uc_mcontext.gregs[REG_SP] = (int)scp->sc_sp;
	ucp->uc_mcontext.gregs[REG_PC] = (int)scp->sc_pc;
	ucp->uc_mcontext.gregs[REG_nPC] = (int)scp->sc_npc;
	ucp->uc_mcontext.gregs[REG_PSR] = (int)scp->sc_psr;
	ucp->uc_mcontext.gregs[REG_G1] = (int)scp->sc_g1;
	ucp->uc_mcontext.gregs[REG_O0] = (int)scp->sc_o0;

	if (sig == SIGFPE) {
		if (ucp->uc_mcontext.fpregs.fpu_qcnt > 0) {
			ucp->uc_mcontext.fpregs.fpu_qcnt--;
			ucp->uc_mcontext.fpregs.fpu_q++;
		}
	}
	setcontext(ucp);
	/* NOTREACHED */
}
#endif

_sigsetmask(mask)
	int mask;
{
	return (ucbsigsetmask(mask));
}

ucbsigsetmask(mask)
	int mask;
{
	sigset_t oset;
	sigset_t nset;

	(void) _sigprocmask(0, (sigset_t *)0, &nset);
	mask2set(mask, &nset);
	(void) _sigprocmask(SIG_SETMASK, &nset, &oset);
	return (set2mask(&oset));
}

_sigblock(mask)
	int mask;
{
	return (ucbsigblock(mask));
}

ucbsigblock(mask)
	int mask;
{
	sigset_t oset;
	sigset_t nset;

	(void) _sigprocmask(0, (sigset_t *)0, &nset);
	mask2set(mask, &nset);
	(void) _sigprocmask(SIG_BLOCK, &nset, &oset);
	return (set2mask(&oset));
}

usigpause(mask)
	int mask;
{
	return (ucbsigpause(mask));
}

ucbsigpause(mask)
	int mask;
{
	sigset_t set, oset;
	int ret;

	(void) _sigprocmask(0, (sigset_t *)0, &set);
	oset = set;
	mask2set(mask, &set);
	ret = sigsuspend(&set);
	(void) _sigprocmask(SIG_SETMASK, &oset, (sigset_t *)0);
	return (ret);
}

_sigvec(sig, nvec, ovec)
	int sig;
	struct sigvec *nvec;
	struct sigvec *ovec;
{
	return (ucbsigvec(sig, nvec, ovec));
}

ucbsigvec(sig, nvec, ovec)
	int sig;
	struct sigvec *nvec;
	struct sigvec *ovec;
{
	struct sigaction nact;
	struct sigaction oact;
	struct sigaction *nactp;
	void (*ohandler)(), (*nhandler)();

	if (sig <= 0 || sig >= NSIG) {
		errno = EINVAL;
		return (-1);
	}

	if ((int)ovec == -1 || (int)nvec == -1) {
		errno = EFAULT;
		return (-1);
	}

	ohandler = _siguhandler[sig];

	if (nvec) {
		__sigaction(sig, (struct sigaction *)0, &nact);
		nhandler = nvec->sv_handler;
		_siguhandler[sig] = nhandler;
		if (nhandler != SIG_DFL && nhandler != SIG_IGN)
			nact.sa_handler = (void (*)())ucbsigvechandler;
		else nact.sa_handler = nhandler;
		mask2set(nvec->sv_mask, &nact.sa_mask);
		if (sig == SIGKILL || sig == SIGSTOP)
			nact.sa_handler = SIG_DFL;
		nact.sa_flags = SA_SIGINFO;
		if (!(nvec->sv_flags & SV_INTERRUPT))
			nact.sa_flags |= SA_RESTART;
		if (nvec->sv_flags & SV_RESETHAND)
			nact.sa_flags |= SA_RESETHAND;
		if (nvec->sv_flags & SV_ONSTACK)
			nact.sa_flags |= SA_ONSTACK;
		nactp = &nact;
	} else
		nactp = (struct sigaction *)0;

	if (__sigaction(sig, nactp, &oact) < 0) {
		_siguhandler[sig] = ohandler;
		return (-1);
	}

	if (ovec) {
		if (oact.sa_handler == SIG_DFL || oact.sa_handler == SIG_IGN)
			ovec->sv_handler = oact.sa_handler;
		else
			ovec->sv_handler = ohandler;
		ovec->sv_mask = set2mask(&oact.sa_mask);
		ovec->sv_flags = 0;
		if (oact.sa_flags & SA_ONSTACK)
			ovec->sv_flags |= SV_ONSTACK;
		if (oact.sa_flags & SA_RESETHAND)
			ovec->sv_flags |= SV_RESETHAND;
		if (!(oact.sa_flags & SA_RESTART))
			ovec->sv_flags |= SV_INTERRUPT;
	}

	return (0);
}


_sigstack(nss, oss)
	struct sigstack *nss;
	struct sigstack *oss;
{
	struct sigaltstack nalt;
	struct sigaltstack oalt;
	struct sigaltstack *naltp;

	if (nss) {
		/*
		 * XXX: assumes stack growth is down (like sparc)
		 */
		nalt.ss_sp = nss->ss_sp - SIGSTKSZ;
		nalt.ss_size = SIGSTKSZ;
		nalt.ss_flags = 0;
		naltp = &nalt;
	}  else
		naltp = (struct sigaltstack *)0;

	if (sigaltstack(naltp, &oalt) < 0)
		return (-1);

	if (oss) {
		/*
		 * XXX: assumes stack growth is down (like sparc)
		 */
		oss->ss_sp = oalt.ss_sp + oalt.ss_size;
		oss->ss_onstack = ((oalt.ss_flags & SS_ONSTACK) != 0);
	}

	return (0);
}

void (*
ucbsignal(s, a))()
	int s;
	void (*a)();
{
	struct sigvec osv;
	struct sigvec nsv;
	static int mask[NSIG];
	static int flags[NSIG];

	nsv.sv_handler = a;
	nsv.sv_mask = mask[s];
	nsv.sv_flags = flags[s];
	if (ucbsigvec(s, &nsv, &osv) < 0)
		return (SIG_ERR);
	if (nsv.sv_mask != osv.sv_mask || nsv.sv_flags != osv.sv_flags) {
		mask[s] = nsv.sv_mask = osv.sv_mask;
		flags[s] = nsv.sv_flags =
			osv.sv_flags & ~(SV_RESETHAND|SV_INTERRUPT);
		if (ucbsigvec(s, &nsv, (struct sigvec *)0) < 0)
			return (SIG_ERR);
	}
	return (osv.sv_handler);
}

void (*
usignal(s, a))()
	int s;
	void (*a)();
{
	return (ucbsignal(s, a));
}

/*
 * Set signal state to prevent restart of system calls
 * after an instance of the indicated signal.
 */
_siginterrupt(sig, flag)
	int sig, flag;
{
	return (ucbsiginterrupt(sig, flag));
}

ucbsiginterrupt(sig, flag)
	int sig, flag;
{
	struct sigvec sv;
	int ret;

	if ((ret = ucbsigvec(sig, 0, &sv)) < 0)
		return (ret);
	if (flag)
		sv.sv_flags |= SV_INTERRUPT;
	else
		sv.sv_flags &= ~SV_INTERRUPT;
	return (ucbsigvec(sig, &sv, 0));
}
