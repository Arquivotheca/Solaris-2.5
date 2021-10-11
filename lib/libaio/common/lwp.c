/* 
 *	Copyright (c) 1992, Sun Microsystems, Inc.
 */
 
 
#pragma	ident	"@(#)lwp.c 1.2	95/01/23	SMI" 
 
#include <signal.h>
#include <siginfo.h>
#include <sys/ucontext.h>
#include <sys/reg.h>
#include <sys/param.h>
#include <sys/asm_linkage.h>
#include <sys/lwp.h>
#include <sys/t_lock.h>
#include <libaio.h>
#include <errno.h>
#include <synch32.h>

int
_aio_lwp_exec(func, aiop, set, flag)
	void (*func)();
	struct aio_req *aiop;
	sigset_t *set;
	int flag;
{
	struct aio_worker *aiowp;
	caddr_t stk = 0;
	ucontext_t uc;
	int stksize;

	if (_aio_alloc_stack(__aiostksz, &stk) == 0) {
		return (-1);
	}
	stksize = __aiostksz - sizeof (struct aio_worker);
	aiowp = (struct aio_worker *)(stk + stksize);
	/* initialize worker's private data */
	memset((caddr_t)aiowp, 0, sizeof (aiowp));
	if (_workers == NULL) {
		_workers = aiowp;
		aiowp->work_forw = aiowp;
		aiowp->work_backw = aiowp;
		_nextworker = aiowp;
	} else {
		_workers->work_backw->work_forw = aiowp;
		aiowp->work_backw = _workers->work_backw;
		aiowp->work_forw = _workers;
		_workers->work_backw = aiowp;
		if (flag)
			_nextworker = aiowp;
	}
	if (aiop) {
		aiop->req_worker = aiowp;
		aiowp->work_head1 = aiop;
		aiowp->work_tail1 = aiop;
		aiowp->work_next1 = aiop;
		aiowp->work_cnt1 = 1;
	}
	aiowp->work_stk = stk;
	stksize -= sizeof (double);
	memset((caddr_t)&uc, 0, sizeof (ucontext_t));
	memcpy(&uc.uc_sigmask, set, sizeof (sigset_t));
	_lwp_makecontext(&uc, func, aiowp, aiowp, stk, stksize);
	if (_lwp_create(&uc, NULL, &aiowp->work_lid)) {
		_aio_free_stack(__aiostksz, stk);
		return (-1);
	}
	return (0);
}
