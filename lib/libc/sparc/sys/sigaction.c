/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sigaction.c	1.8	94/04/19 SMI"	/* SVr4 1.5	*/

#ifdef __STDC__
#pragma weak sigaction = _sigaction
#endif
#include "synonyms.h"
#include <signal.h>
#include <errno.h>
#include <siginfo.h>
#include <ucontext.h>

void (*_siguhandler[NSIG])() = { 0 };

static void
sigacthandler(sig, sip, uap)
	int sig;
	siginfo_t *sip;
	ucontext_t *uap;
{
	(*_siguhandler[sig])(sig, sip, uap);

	/*
	 * If this is a floating point exception and the queue
	 * is non-empty, pop the top entry from the queue.  This
	 * is to maintain expected behavior.
	 */
	if ((sig == SIGFPE) && uap->uc_mcontext.fpregs.fpu_qcnt) {
		fpregset_t *fp = &uap->uc_mcontext.fpregs;

		if (--fp->fpu_qcnt > 0) {
			unsigned char i;
			struct fq *fqp;

			fqp = fp->fpu_q;
			for (i = 0; i < fp->fpu_qcnt; i++)
				fqp[i] = fqp[i+1];
		}
	}

	setcontext(uap);
}

sigaction(sig, nact, oact)
	int sig;
	const struct sigaction *nact;
	struct sigaction *oact;
{
	struct sigaction tact;
	register struct sigaction *tactp;
	void (*ohandler)();

	if (sig <= 0 || sig >= NSIG) {
		errno = EINVAL;
		return (-1);
	}

	ohandler = _siguhandler[sig];

	if (tactp = (struct sigaction *)nact) {
		tact = *nact;
		tactp = &tact;
		if (tactp->sa_handler != SIG_DFL &&
		    tactp->sa_handler != SIG_IGN) {
			_siguhandler[sig] = tactp->sa_handler;
			tactp->sa_handler = sigacthandler;
		}
	}

	if (__sigaction(sig, tactp, oact) == -1) {
		_siguhandler[sig] = ohandler;
		return (-1);
	}
	if (nact &&
		(nact->sa_handler == SIG_DFL || nact->sa_handler == SIG_IGN))
		_siguhandler[sig] = nact->sa_handler;

	if (oact && oact->sa_handler != SIG_DFL && oact->sa_handler != SIG_IGN)
		oact->sa_handler = ohandler;

	return (0);
}
