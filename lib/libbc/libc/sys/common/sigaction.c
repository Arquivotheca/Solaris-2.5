#ident	"@(#)sigaction.c	1.10	95/02/08 SMI"   /* SVr4.0 1.20  */

#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include "signalmap.h"

#pragma init(signal_init)
extern void (*handlers[])();
extern void maphandler(int, int, struct sigcontext*, char*);
extern void (*_siguhandler[])();		/* libc */
extern void _sigvechandler(int, void*, void*);	/* libucb */

typedef struct {
	unsigned long	__sigbits[4];
} S5_sigset_t;

typedef struct {
	int		sa_flags;
	void		(*sa_handler)();
	S5_sigset_t	sa_mask;
	int		sa_resv[2];
} S5_sigaction;

#define	S5_SA_ONSTACK	0x00000001
#define	S5_SA_RESETHAND	0x00000002
#define	S5_SA_RESTART	0x00000004
#define	S5_SA_NOCLDSTOP	0x00020000

int
sigaction(sig, act, oact)
int sig;
struct sigaction *act, *oact;
{
	S5_sigaction	S5_act;
	S5_sigaction	S5_oact;
	int		ret;
	int		newsig;
	void		(*oldhand)();
	void		(*oldsiguhand)();

	newsig = maptonewsig(sig);
	oldhand = handlers[newsig];
	oldsiguhand = _siguhandler[newsig];
	if (act == NULL) {
		ret = __sigaction(newsig, (S5_sigaction *)NULL, &S5_oact);
	} else {
		S5_act.sa_flags = 0;
		if (act->sa_flags & SA_ONSTACK)
			S5_act.sa_flags |= S5_SA_ONSTACK;
		if (act->sa_flags & SA_RESETHAND)
			S5_act.sa_flags |= S5_SA_RESETHAND;
		if (act->sa_flags & SA_NOCLDSTOP)
			S5_act.sa_flags |= S5_SA_NOCLDSTOP;
		if (!(act->sa_flags & SA_INTERRUPT))
			S5_act.sa_flags |= S5_SA_RESTART;
		/*
		 * _sigvechandler() receives control from the OS.
		 * It calls through _siguhandler[] to maphandler(),
		 * which maps the signal number new-to-old, and
		 * calls the user's handler through handlers[].
		 */
		handlers[newsig] = act->sa_handler;
		_siguhandler[newsig] = maphandler;
		if ((act->sa_handler == SIG_DFL) ||
		    (act->sa_handler == SIG_IGN))
			S5_act.sa_handler = act->sa_handler;
		else
			S5_act.sa_handler = _sigvechandler;
		S5_act.sa_mask.__sigbits[0] = maptonewmask(act->sa_mask);
		S5_act.sa_mask.__sigbits[1] = 0;
		S5_act.sa_mask.__sigbits[2] = 0;
		S5_act.sa_mask.__sigbits[3] = 0;

		ret = __sigaction(newsig, &S5_act, &S5_oact);
	}

	if ((oact != NULL) && (ret != -1)) {
		oact->sa_flags = 0;
		if (S5_oact.sa_flags & S5_SA_ONSTACK)
			oact->sa_flags |= SA_ONSTACK;
		if (S5_oact.sa_flags & S5_SA_RESETHAND)
			oact->sa_flags |= SA_RESETHAND;
		if (S5_oact.sa_flags & S5_SA_NOCLDSTOP)
			oact->sa_flags |= SA_NOCLDSTOP;
		if (!(S5_oact.sa_flags & S5_SA_RESTART))
			oact->sa_flags |= SA_INTERRUPT;
		if ((S5_oact.sa_handler == SIG_DFL) ||
		    (S5_oact.sa_handler == SIG_IGN))
			oact->sa_handler = S5_oact.sa_handler;
		else
			oact->sa_handler = oldhand;
		oact->sa_mask = maptooldmask(S5_oact.sa_mask.__sigbits[0]);
	}
	if (ret == -1) {
		handlers[newsig] = oldhand;
		_siguhandler[newsig] = oldsiguhand;
	}
	return (ret);
}

static void
signal_init() {
#define	S5_SIGPOLL	22
	_signal(S5_SIGPOLL, SIG_IGN);
#undef S5_SIGPOLL
}

int
sigprocmask(how, set, oset)
int how;
sigset_t *set, *oset;
{
	int		how_map[] = {0, 1, 2, 0, 3};
	int		ret;
	S5_sigset_t	s5_set, s5_oset;

	if (set == NULL) /* query */
		ret = _sigprocmask(how_map[how], NULL, &s5_oset);
	else {
		memset(&s5_set, 0, sizeof (S5_sigset_t));
		s5_set.__sigbits[0] = maptonewmask(*set);
		ret = _sigprocmask(how_map[how], &s5_set, &s5_oset);
	}
	if ((oset != NULL) && (ret == 0))
		*oset = maptooldmask(s5_oset.__sigbits[0]);
	return (ret);
}

int
sigpending(set)
sigset_t *set;
{
	S5_sigset_t	s5_set;
	int		ret;

	ret = _sigpending(&s5_set);
	*set = maptooldmask(s5_set.__sigbits[0]);
	return (ret);
}
