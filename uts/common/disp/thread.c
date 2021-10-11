/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)thread.c	1.76	95/08/18 SMI"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/stack.h>
#include <sys/pcb.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/sysinfo.h>
#include <sys/var.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/proc.h>
#include <sys/debug.h>
#include <sys/inline.h>
#include <sys/disp.h>
#include <sys/class.h>
#include <vm/seg_kp.h>
#include <sys/machlock.h>
#include <sys/kmem.h>
#include <sys/varargs.h>
#include <sys/turnstile.h>
#include <sys/poll.h>
#include <sys/vtrace.h>
#include <sys/callb.h>
#include <c2/audit.h>
#include <sys/tnf.h>

struct kmem_cache *thread_cache;	/* cache of free threads */
struct kmem_cache *lwp_cache;		/* cache of free lwps */

/*
 * allthreads is only for use by kmem_readers.  All kernel loops can use
 * the current thread as a start/end point.
 */
static kthread_id_t allthreads = &t0;	/* circular list of all threads */

extern kmutex_t reaplock;		/* protects deathrow */
static kcondvar_t reaper_cv;		/* synchronization var */
kthread_t	*thread_deathrow;	/* circular list of reapable threads */
kthread_t	*lwp_deathrow;		/* circular list of reapable threads */
int	thread_reapcnt = 0;		/* number of threads on deathrow */
int	lwp_reapcnt = 0;		/* number of lwps on deathrow */
int	reaplimit = 16;			/* delay reaping until reaplimit */

extern int nthread;
extern struct seg *segkp;

id_t	syscid;				/* system scheduling class ID */
void *segkp_thread;			/* cookie for segkp pool */

int lwp_cache_sz = 32;
int t_cache_sz = 8;
static u_int next_t_id = 1;

/*
 * Min/Max stack sizes for lwp's
 */
#define	LWP_MAX_STKSIZE	(256 * 1024)
#define	LWP_MIN_STKSIZE	(8 * 1024)

int	lwp_default_stksize;

/*
 * forward declarations for internal thread specific data (tsd)
 */
static void	tsd_init(void);
static caddr_t	tsd_realloc(caddr_t, size_t, size_t);

void
thread_init(void)
{
	kthread_id_t tp;
	extern char sys_name[];
	extern void idle();
	extern void *segkp_lwp;		/* cookie for segkp pool */
	extern void *segkp_thread;	/* cookie for segkp pool */
	struct cpu *cpu = CPU;

	thread_cache = kmem_cache_create("thread_cache", sizeof (kthread_t),
		PTR24_ALIGN, NULL, NULL, NULL);

	lwp_cache = kmem_cache_create("lwp_cache", sizeof (klwp_t),
		0, NULL, NULL, NULL);

	if (lwp_default_stksize != DEFAULTSTKSZ) {
		if (lwp_default_stksize % PAGESIZE != 0 ||
		    lwp_default_stksize > LWP_MAX_STKSIZE ||
		    lwp_default_stksize < LWP_MIN_STKSIZE) {
			if (lwp_default_stksize)
				cmn_err(CE_WARN, "Illegal stack size. Using %d",
					DEFAULTSTKSZ);
			lwp_default_stksize = DEFAULTSTKSZ;
		}
	}
	segkp_lwp = segkp_cache_init(segkp, lwp_cache_sz,
	    lwp_default_stksize,
	    (KPD_NOWAIT | KPD_HASREDZONE | KPD_LOCKED));

	segkp_thread = segkp_cache_init(segkp, t_cache_sz,
	    DEFAULTSTKSZ,
	    (KPD_HASREDZONE | KPD_LOCKED | KPD_NO_ANON | KPD_NOWAIT));

	(void) getcid(sys_name, &syscid);
	curthread->t_cid = syscid;	/* current thread is t0 */

	tsd_init();			/* thread specific data */

	/*
	 * Set up the first CPU's idle thread.
	 * It runs whenever the CPU has nothing worthwhile to do.
	 */
	tp = thread_create(NULL, NBPG, idle, NULL, 0, &p0, TS_STOPPED, -1);
	cpu->cpu_idle_thread = tp;
	tp->t_preempt = 1;
	tp->t_disp_queue = &cpu->cpu_disp;
	tp->t_bound_cpu = cpu;
	tp->t_affinitycnt = 1;
}

/*
 * Create a thread.
 * 	If stk is NULL, the thread is created at the base of the stack
 *	and cannot be swapped.
 */
kthread_id_t
thread_create(stk, stksize, proc, arg, len, pp, state, pri)
	caddr_t	stk;
	int	stksize;
	void	(*proc)();
	caddr_t arg;
	int len;
	proc_t *pp;
	int state;
	int pri;
{
	kthread_id_t t;
	extern struct classfuncs sys_classfuncs;

	/*
	 * Since we are allocating another thread,
	 * check to see whether we've got enough
	 * turnstiles in the pool. If not, try to
	 * dynamically allocate more turnstiles.
	 */
	if (!tstile_more(nthread+1, (state == TS_ONPROC)))
		return (NULL);
	if (stk == NULL) {
		/*
		 * alloc both thread and stack in segkp chunk
		 */
		if (stksize == 0) {
			stksize = DEFAULTSTKSZ;
			stk = (caddr_t)segkp_cache_get(segkp_thread);
		} else {
			stksize = roundup(stksize, PAGESIZE);
			stk = (caddr_t)segkp_get(segkp, stksize,
			    (KPD_HASREDZONE | KPD_NO_ANON | KPD_LOCKED));
		}

		if (stk == NULL)
			return (NULL);
		/*
		 * The machine-dependent mutex code may require that
		 * thread pointers (since they may be used for mutex owner
		 * fields) have certain alignment requirements.
		 * PTR24_ALIGN is the size of the alignment quanta.
		 * XXX - assumes stack grows toward low addresses.
		 */
		if (stksize <= sizeof (kthread_t) + PTR24_ALIGN)
			cmn_err(CE_PANIC, "thread_create: proposed stack size"
			    " too small to hold thread.");
#ifdef STACK_GROWTH_DOWN
		stksize -= SA(sizeof (kthread_t) + PTR24_ALIGN - 1);
		stksize &= -PTR24_ALIGN;	/* make thread aligned */
		t = (kthread_id_t)(stk + stksize);
		bzero((caddr_t)t, sizeof (kthread_t));
		t->t_stk = stk + stksize;
#else	/* stack grows to larger addresses */
		stksize -= SA(sizeof (kthread_t));
		t = (kthread_id_t)(stk);
		bzero((caddr_t)t, sizeof (kthread_t));
		t->t_stk = stk + sizeof (kthread_t);
#endif	/* STACK_GROWTH_DOWN */
		t->t_flag |= T_TALLOCSTK;
		t->t_swap = stk;
	} else {
		if ((t = kmem_cache_alloc(thread_cache, KM_NOSLEEP)) == NULL)
			return (NULL);
		bzero((caddr_t)t, sizeof (kthread_t));
		ASSERT(((u_int)t & (PTR24_ALIGN - 1)) == 0);
		/*
		 * Initialize t_stk to the kernel stack pointer to use
		 * upon entry to the kernel
		 */
#ifdef STACK_GROWTH_DOWN
		t->t_stk = stk + stksize;
#else
		t->t_stk = stk;			/* 3b2-like */
#endif /* STACK_GROWTH_DOWN */
	}

	/*
	 * link thread onto a list of all threads.
	 */
	t->t_next = curthread;		/* before mutex to force store order */
	mutex_enter(&pidlock);
	nthread++;
	t->t_did = next_t_id++;
	t->t_prev = curthread->t_prev;
	curthread->t_prev->t_next = t;
	curthread->t_prev = t;
	mutex_exit(&pidlock);

	/*
	 * p_cred could be NULL if it thread_create is called before cred_init
	 * is called in main.
	 */
	if (pp->p_cred)
		crhold(t->t_cred = pp->p_cred);
#ifdef	C2_AUDIT
	if (audit_active)
		audit_thread_create(t, state);
#endif
	t->t_startpc = proc;
	t->t_procp = pp;
	t->t_clfuncs = &sys_classfuncs.thread;
	t->t_cid = syscid;
	t->t_pri = pri;
	t->t_stime = lbolt;
	t->t_schedflag = TS_LOAD | TS_DONT_SWAP;
	t->t_bind_cpu = PBIND_NONE;
	/*
	 * Threads should never have a NULL t_cpu pointer so assign it
	 * here.
	 * We need to disable kernel preemption for this thread
	 * while we call disp_lowpri_cpu() so the hint CPU can't
	 * go offline.
	 */
	kpreempt_disable();
	t->t_cpu = disp_lowpri_cpu(CPU);
	t->t_disp_queue = &t->t_cpu->cpu_disp;
	kpreempt_enable();
	CPU_STAT_ADDQ(CPU, cpu_sysinfo.nthreads, 1);
#ifdef TRACE
	trace_kthread_label(t, -1);
#endif	/* TRACE */
#ifndef NPROBE
	/* Kernel probe */
	tnf_thread_create(t);
#endif /* NPROBE */
	LOCK_INIT_CLEAR(&t->t_lock);

	/*
	 * Callers who give us a NULL proc must do their own
	 * stack initialization.  e.g. lwp_create()
	 */
	if (proc != NULL) {
		t->t_stk = thread_stk_init(t->t_stk);
		(void) thread_load(t, proc, arg, len);
	}

	/*
	 * Initialize thread state and the dispatcher lock pointer.
	 */
	switch (state) {
	case TS_RUN:
		(void) splhigh();	/* get dispatcher spl */
		THREAD_SET_STATE(t, TS_STOPPED, &transition_lock);
		CL_SETRUN(t);
		thread_unlock(t);
		break;

	case TS_ONPROC:
		THREAD_ONPROC(t, t->t_cpu);
		break;

	case TS_FREE:
		/*
		 * Free state will be used for intr threads.
		 * The interrupt routine must set the thread dispatcher
		 * lock pointer (t_lockp) if starting on another CPU.
		 */
		THREAD_SET_STATE(t, TS_FREE, &CPU->cpu_thread_lock);
		break;

	case TS_STOPPED:
		THREAD_SET_STATE(t, TS_STOPPED, &stop_lock);
		break;

	default:			/* TS_SLEEP, TS_ZOMB or TS_TRANS */
		cmn_err(CE_PANIC, "thread_create:  invalid state %d\n", state);
	}
	return (t);
}

void
thread_exit()
{
	kthread_t *t = curthread;

	tsd_exit();		/* Clean up this thread's TSD */

	if (t->t_pollstate != NULL) {
		pollstate_t *ps = t->t_pollstate;
		/*
		 * Be sure no one is referencing in pollwakeup();
		 */
		mutex_enter(&ps->ps_no_exit);
		ASSERT(ps->ps_busy >= 0);
		while (ps->ps_busy > 0)
			cv_wait(&ps->ps_busy_cv, &ps->ps_no_exit);
		mutex_exit(&ps->ps_no_exit);
		if (ps->ps_pollfd != NULL)
			kmem_free(ps->ps_pollfd,
				ps->ps_nfds * sizeof (pollfd_t));
		if (ps->ps_polldat != NULL)
			kmem_free(ps->ps_polldat,
				ps->ps_nfds * sizeof (polldat_t));
		/*
		 * Destroy the mutexes and the cv's we created for
		 * poll() before we release the memory.
		 */
		mutex_destroy(&ps->ps_lock);
		mutex_destroy(&ps->ps_no_exit);
		cv_destroy(&ps->ps_cv);
		cv_destroy(&ps->ps_busy_cv);
		kmem_free(ps, sizeof (pollstate_t));
		t->t_pollstate = NULL;
	}
#ifndef NPROBE
	/* Kernel probe */
	if (t->t_tnf_tpdp)
		tnf_thread_exit();
#endif /* NPROBE */

	t->t_preempt++;
	/*
	 * remove thread from the all threads list so that
	 * death-row can use the same pointers.
	 */
	mutex_enter(&pidlock);
	t->t_next->t_prev = t->t_prev;
	t->t_prev->t_next = t->t_next;
	ASSERT(allthreads != t);	/* t0 never exits */
	mutex_exit(&pidlock);

	t->t_state = TS_ZOMB;	/* set zombie thread */
	swtch_from_zombie();	/* give up the CPU */
	/* NOTREACHED */
}

void
thread_free(t)
	kthread_id_t	t;
{
	ASSERT(t != &t0 && t->t_state == TS_FREE);
	ASSERT(t->t_door == NULL);

	t->t_pri = 0;
	t->t_pc = 0;
	t->t_sp = 0;
	t->t_wchan0 = 0;
	t->t_wchan = NULL;
	if (t->t_cred != NULL) {
		crfree(t->t_cred);
		t->t_cred = 0;
	}
#ifdef	C2_AUDIT
	if (audit_active)
		audit_thread_free(t);
#endif
#ifndef NPROBE
	if (t->t_tnf_tpdp)
		tnf_thread_free(t);
#endif /* NPROBE */
	if (t->t_cldata) {
		CL_EXITCLASS(t->t_cid, (caddr_t *)t->t_cldata);
	}
	if (t->t_rprof != NULL) {
		kmem_free(t->t_rprof, sizeof (*t->t_rprof));
		t->t_rprof = NULL;
	}
	t->t_lockp = NULL;	/* nothing should try to lock this thread now */
	if (t->t_lwp)
		lwp_freeregs(t->t_lwp);
	if (t->t_ctx)
		freectx(t);
	t->t_stk = NULL;
	lock_clear(&t->t_lock);
	/*
	* Free thread struct and its stack.
	 */
	if (t->t_flag & T_TALLOCSTK) {
		/* thread struct is embedded in stack */
		segkp_release(segkp, t->t_swap);
		mutex_enter(&pidlock);
		nthread--;
		mutex_exit(&pidlock);
	} else {
		if (t->t_swap) {
			segkp_release(segkp, t->t_swap);
			t->t_swap = NULL;
		}
		if (t->t_lwp) {
			kmem_cache_free(lwp_cache, t->t_lwp);
			t->t_lwp = NULL;
		}
		mutex_enter(&pidlock);
		nthread--;
		mutex_exit(&pidlock);
		kmem_cache_free(thread_cache, t);
	}
}

/*
 * cleanup zombie threads that are on deathrow.
 */
void
thread_reaper()
{
	kthread_t *t, *l, *next;
	callb_cpr_t cprinfo;

	CALLB_CPR_INIT(&cprinfo, &reaplock, callb_generic_cpr, "t_reaper");
	for (;;) {
		mutex_enter(&reaplock);
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		while (thread_deathrow == NULL && lwp_deathrow == NULL) {
			cv_wait(&reaper_cv, &reaplock);
		}
		CALLB_CPR_SAFE_END(&cprinfo, &reaplock);
		t = thread_deathrow;
		l = lwp_deathrow;
		thread_deathrow = NULL;
		lwp_deathrow = NULL;
		thread_reapcnt = 0;
		lwp_reapcnt = 0;
		mutex_exit(&reaplock);

		/*
		 * Reap threads
		 */
		while (t != NULL) {
			next = t->t_forw;
			thread_free(t);
			t = next;
		}
		/*
		 * Reap lwps
		 */
		while (l != NULL) {
			next = l->t_forw;
			thread_free(l);
			l = next;
		}
	}
}

/*
 * this is called by resume() to determine if the zombie thread
 * should be put onto deathrow. The thread's state is also changed
 * to TS_FREE which indicates that is reapable
 */
void
reapq_add(t)
	kthread_t *t;
{
	void deathrow_enq(kthread_t *);
	mutex_enter(&reaplock);
	if (!(t->t_proc_flag & TP_TWAIT))
		deathrow_enq(t);
	t->t_state = TS_FREE;
	lock_clear(&t->t_lock);
	mutex_exit(&reaplock);
}

/*
 * Put a thread onto death-row.
 * May be called from idle thread so it must not block (just spin).
 */
void
deathrow_enq(t)
	kthread_t *t;
{
	ASSERT(MUTEX_HELD(&reaplock));

	if (ttolwp(t)) {
		if (lwp_deathrow == NULL) {
			lwp_deathrow = t;
			t->t_forw = NULL;
		} else {
			t->t_forw = lwp_deathrow;
			lwp_deathrow = t;
		}
		lwp_reapcnt++;
	} else {
		if (thread_deathrow == NULL) {
			thread_deathrow = t;
			t->t_forw = NULL;
		} else {
			t->t_forw = thread_deathrow;
			thread_deathrow = t;
		}
		thread_reapcnt++;
	}
	if (lwp_reapcnt + thread_reapcnt > reaplimit)
		cv_signal(&reaper_cv);
}

/*
 * Install a device context for the current thread
 */
void
installctx(t, arg, save, restore, fork, free)
	kthread_id_t	t;
	int	arg;
	void	(*save)();
	void	(*restore)();
	void	(*fork)();
	void	(*free)();
{
	struct ctxop *ctx;

	ctx = (struct ctxop *)kmem_alloc(sizeof (struct ctxop), KM_SLEEP);
	ctx->save_op = save;
	ctx->restore_op = restore;
	ctx->fork_op = fork;
	ctx->free_op = free;
	ctx->arg = arg;
	ctx->next = t->t_ctx;
	t->t_ctx = ctx;
}

void
savectx(kthread_id_t t)
{
	struct ctxop *ctx;

	ASSERT(t == curthread);
	for (ctx = t->t_ctx; ctx != 0; ctx = ctx->next) {
		(ctx->save_op)(ctx->arg);
	}
}

void
restorectx(kthread_id_t t)
{
	struct ctxop *ctx;

	ASSERT(t == curthread);
	for (ctx = t->t_ctx; ctx != 0; ctx = ctx->next) {
		(ctx->restore_op)(ctx->arg);
	}
}

void
forkctx(t, ct)
	kthread_id_t t, ct;
{
	struct ctxop *ctx;

	for (ctx = t->t_ctx; ctx != 0; ctx = ctx->next) {
		(ctx->fork_op)(t, ct);
	}
}

/*
 * Freectx is called from thread_free and exec to get
 * rid of old device context.
 */
void
freectx(t)
	kthread_id_t t;
{
	struct ctxop *ctx;

	while ((ctx = t->t_ctx) != NULL) {
		t->t_ctx = ctx->next;
		(ctx->free_op)(ctx->arg);
		kmem_free(ctx, sizeof (struct ctxop));
	}
}

/*
 * Unpin an interrupted thread.
 *	When an interrupt occurs, the interrupt is handled on the stack
 *	of an interrupt thread, taken from a pool linked to the CPU structure.
 *
 *	When swtch() is switching away from an interrupt thread because it
 *	blocked or was preempted, this routine is called to complete the
 *	saving of the interrupted thread state, and returns the interrupted
 *	thread pointer so it may be resumed.
 *
 *	Called by swtch() only at high spl.
 */
kthread_id_t
thread_unpin()
{
	kthread_id_t	t = curthread;	/* current thread */
	kthread_id_t	itp;		/* interrupted thread */
	int		i;		/* interrupt level */
	extern int	intr_passivate();

	ASSERT(t->t_intr != NULL);

	itp = t->t_intr;		/* interrupted thread */
	t->t_intr = NULL;		/* clear interrupt ptr */

	/*
	 * Get state from interrupt thread for the one
	 * it interrupted.
	 */

	i = intr_passivate(t, itp);

	TRACE_5(TR_FAC_INTR, TR_INTR_PASSIVATE,
		"intr_passivate:level %d curthread %x (%T) ithread %x (%T)",
		i, t, t, itp, itp);

	/*
	 * Dissociate the current thread from the interrupted thread's LWP.
	 */
	t->t_lwp = NULL;

	/*
	 * Interrupt handlers above the level that spinlocks block must
	 * not block.
	 */
#if DEBUG
	if (i < 0 || i > LOCK_LEVEL)
		cmn_err(CE_PANIC, "thread_unpin: intr_level out of range. %x\n",
			i);
#endif

	/*
	 * Set flag to keep CPU's spl level high enough
	 * to block this interrupt, then recompute the CPU's
	 * base interrupt level from the active interrupts.
	 */
	CPU->cpu_intr_actv |= (1 << i);
	set_base_spl();

	return (itp);
}

/*
 * Release an interrupt
 *
 * When an interrupt occurs, the stack of a new "interrupt thread"
 * is used.  The interrupt priority of the processor is held at a
 * level high enough to block the interrupt until the interrupt thread
 * exits or calls release_interrupt().
 *
 * Since lowering the interrupt level requires us to be able to create
 * a new thread, a thread must be allocated.  If there aren't enough
 * resources to allocate a new thread, the interrupt is not released.
 */
void
release_interrupt()
{
	kthread_id_t	t = curthread;	/* current thread */
	int		s;
	struct cpu	*cp;		/* current CPU */
	extern int	intr_level();

	if ((t->t_flag & T_INTR_THREAD) == 0)
		return;
	s = spl7();			/* protect CPU thread lists */
	cp = CPU;
	if (thread_create_intr(cp)) {
		/*
		 * Failed to allocate.  Cannot release interrupt.
		 */
		(void) splx(s);
		return;
	}

	/*
	 * Release the mask for this interrupt level, and recompute the
	 * CPU's base priority level from the new mask.
	 */
	cp->cpu_intr_actv &= ~(1 << intr_level(t));
	set_base_spl();
	t->t_flag &= ~T_INTR_THREAD;	/* clear interrupt thread flag */
	(void) splx(s);
	/*
	 * Give interrupted threads a chance to run.
	 */
	setrun(t);
	swtch();
}

/*
 * Create and initialize an interrupt thread.
 *	Returns non-zero on error.
 *	Called at spl7() or better.
 */
#define	INTR_STACK_SIZE	roundup((8*1024), DEFAULTSTKSZ)

int
thread_create_intr(cp)
	struct cpu	*cp;
{
	kthread_id_t	tp;

	tp = thread_create(NULL, INTR_STACK_SIZE,
		(void (*)())thread_create_intr, NULL, 0, &p0, TS_ONPROC, 0);
	if (tp == NULL) {
		return (-1);
	}

	/*
	 * Set the thread in the TS_FREE state.  The state will change
	 * to TS_ONPROC only while the interrupt is active.  Think of these
	 * as being on a private free list for the CPU.
	 * Being TS_FREE keeps inactive interrupt threads out of the kernel
	 * debugger (kadb)'s threadlist.
	 * We cannot call thread_create with TS_FREE because of the current
	 * checks there for ONPROC.  Fix this when thread_create takes flags.
	 */
	tp->t_state = TS_FREE;

	/*
	 * Nobody should ever reference the credentials of an interrupt
	 * thread so make it NULL to catch any such references.
	 */
	tp->t_cred = NULL;
	tp->t_flag |= T_INTR_THREAD;
	tp->t_cpu = cp;
	tp->t_bound_cpu = cp;
	tp->t_disp_queue = &cp->cpu_disp;
	tp->t_affinitycnt = 1;
	tp->t_preempt = 1;

	/*
	 * Don't make a user-requested binding on this thread so that
	 * the processor can be offlined.
	 */
	tp->t_bind_cpu = PBIND_NONE;	/* no USER-requested binding */

	/*
	 * Link onto CPU's interrupt pool.
	 */
	tp->t_link = cp->cpu_intr_thread;
	cp->cpu_intr_thread = tp;
	return (0);
}
/*
 * TSD -- THREAD SPECIFIC DATA
 */
static kmutex_t		tsd_mutex;	 /* linked list spin lock */
static u_int		tsd_nkeys;	 /* size of destructor array */
/* per-key destructor funcs */
static void 		(**tsd_destructor)(void *);
/* list of tsd_thread's */
static struct tsd_thread	*tsd_list;

/*
 * Default destructor
 *	Needed because NULL destructor means that the key is unused
 */
/* ARGSUSED */
void
tsd_defaultdestructor(void *value)
{
}

/*
 * Create a key (index into per thread array)
 *	Locks out tsd_create, tsd_destroy, and tsd_exit
 *	May allocate memory with lock held
 */
void
tsd_create(u_int *keyp, void (*destructor)(void *))
{
	int	i;
	u_int	nkeys;

	/*
	 * if key is allocated, do nothing
	 */
	mutex_enter(&tsd_mutex);
	if (*keyp) {
		mutex_exit(&tsd_mutex);
		return;
	}
	/*
	 * find an unused key
	 */
	if (destructor == NULL)
		destructor = tsd_defaultdestructor;

	for (i = 0; i < tsd_nkeys; ++i)
		if (tsd_destructor[i] == NULL)
			break;

	/*
	 * if no unused keys, increase the size of the destructor array
	 */
	if (i == tsd_nkeys) {
		if ((nkeys = tsd_nkeys << 1) == 0)
			nkeys = 1;
		tsd_destructor = (void (**)(void *))tsd_realloc(
		    (caddr_t)tsd_destructor,
		    (size_t)(tsd_nkeys * sizeof (void (*)(void *))),
		    (size_t)(nkeys * sizeof (void (*)(void *))));
		tsd_nkeys = nkeys;

	}

	/*
	 * allocate the next available unused key
	 */
	tsd_destructor[i] = destructor;
	*keyp = i + 1;
	mutex_exit(&tsd_mutex);
}

/*
 * Destroy a key -- this is for unloadable modules
 *	Assumes that the caller is preventing tsd_set and tsd_get
 *	Locks out tsd_create, tsd_destroy, and tsd_exit
 *	May free memory with lock held
 */
void
tsd_destroy(u_int *keyp)
{
	u_int			key;
	struct tsd_thread	*tsd;

	/*
	 * lock out the other tsd functions except tsd_get and tsd_set
	 */
	mutex_enter(&tsd_mutex);
	key = *keyp;
	*keyp = 0;

	ASSERT(key <= tsd_nkeys);

	/*
	 * if the key is valid
	 */
	if (key != 0) {
		u_int k = key - 1;
		/*
		 * for every thread with TSD, call key's destructor
		 */
		for (tsd = tsd_list; tsd; tsd = tsd->ts_next) {
			/*
			 * no TSD for key in this thread
			 */
			if (key > tsd->ts_nkeys)
				continue;
			/*
			 * call destructor for key
			 */
			if (tsd->ts_value[k] && tsd_destructor[k])
				(*tsd_destructor[k])(tsd->ts_value[k]);
			/*
			 * reset value for key
			 */
			tsd->ts_value[k] = NULL;
		}
		/*
		 * actually free the key (NULL destructor == unused)
		 */
		tsd_destructor[k] = NULL;
	}

	mutex_exit(&tsd_mutex);
}

/*
 * Quickly return the per thread value that was stored with the specified key
 *	Assumes the caller is protecting key from tsd_create and tsd_destroy
 */
void *
tsd_get(u_int key)
{
	struct tsd_thread	*tsd	= curthread->t_tsd;

	/*
	 * if key is valid and this thread has a value for this key
	 *	return value
	 * else
	 *	return NULL
	 */
	if (key && tsd != NULL && key <= tsd->ts_nkeys)
		return (tsd->ts_value[key - 1]);
	return (NULL);
}

/*
 * Set a per thread value indexed with the specified key
 *	Assumes the caller is protecting key from tsd_create and tsd_destroy
 *	May Lock out tsd_destroy (and tsd_create)
 *	May allocate memory with lock held
 */
int
tsd_set(u_int key, void *value)
{
	struct tsd_thread	*tsd	= curthread->t_tsd;

	/*
	 * if there is no key, return error
	 */
	if (key == 0)
		return (EINVAL);
	/*
	 * Allocate the tsd area if necessary.
	 */
	if (tsd == NULL)
		tsd = curthread->t_tsd = kmem_zalloc(sizeof (struct tsd_thread),
						KM_SLEEP);
	/*
	 * if key is valid
	 */
	if (key <= tsd->ts_nkeys) {
		/*
		 * set the new value and return
		 */
		tsd->ts_value[key - 1] = value;
		return (0);
	}

	ASSERT(key <= tsd_nkeys);

	/*
	 * lock out tsd_destroy
	 */
	mutex_enter(&tsd_mutex);

	/*
	 * link this thread onto linked list of threads with TSD
	 */
	if (tsd->ts_nkeys == 0) {
		if ((tsd->ts_next = tsd_list) != NULL)
			tsd_list->ts_prev = tsd;
		tsd_list = tsd;
	}

	/*
	 * allocate thread local storage and assign value for key
	 */
	tsd->ts_value = (void **)tsd_realloc(
	    (caddr_t)tsd->ts_value,
	    (size_t)(tsd->ts_nkeys * sizeof (void *)),
	    (size_t)(key * sizeof (void *)));
	tsd->ts_nkeys = key;

	/*
	 * set the new value
	 */
	tsd->ts_value[key - 1] = value;

	mutex_exit(&tsd_mutex);
	return (0);
}
/*
 * Return the per thread value that was stored with the specified key
 *	If necessary, create the key and the value
 *	Assumes the caller is protecting *keyp from tsd_destroy
 */
void *
tsd_getcreate(u_int *keyp, void (*destructor)(void *),
	void *(*allocator)(void))
{
	void			*value;
	u_int			key	= *keyp;
	struct tsd_thread	*tsd	= curthread->t_tsd;

	/*
	 * Allocate the tsd area if necessary.
	 */
	if (tsd == NULL)
		tsd = curthread->t_tsd = kmem_zalloc(sizeof (struct tsd_thread),
						KM_SLEEP);

	/*
	 * if key is valid and this thread has a value for this key
	 *	return the value
	 */
	if (key && key <= tsd->ts_nkeys && (value = tsd->ts_value[key - 1]))
		return (value);

	/*
	 * create the key
	 */
	if (key == 0)
		tsd_create(keyp, destructor);
	/*
	 * allocate a value and set it
	 */
	(void) tsd_set(*keyp, value = (*allocator)());

	return (value);
}

/*
 * Called from thread_exit() to run the destructor function for each tsd
 *	Locks out tsd_create and tsd_destroy
 *	Assumes that the destructor *DOES NOT* use tsd
 */
void
tsd_exit(void)
{
	int			i;
	struct tsd_thread	*tsd	= curthread->t_tsd;

	/*
	 * No TSD; nothing to do
	 */
	if (tsd == NULL)
		return;

	if (tsd->ts_nkeys == 0) {
		kmem_free(tsd, sizeof (struct tsd_thread));
		curthread->t_tsd = NULL;
		return;
	}

	/*
	 * lock out tsd_create and tsd_destroy
	 */
	mutex_enter(&tsd_mutex);
	for (i = 0; i < tsd->ts_nkeys; i++) {
		/*
		 * call destructor on existing value
		 */
		if (tsd->ts_value[i] && tsd_destructor[i])
			(*tsd_destructor[i])(tsd->ts_value[i]);
		/*
		 * reset the new value
		 */
		tsd->ts_value[i] = NULL;
	}
	/*
	 * remove from linked list of threads with TSD
	 */
	if (tsd->ts_next)
		tsd->ts_next->ts_prev = tsd->ts_prev;
	if (tsd->ts_prev)
		tsd->ts_prev->ts_next = tsd->ts_next;
	if (tsd_list == tsd)
		tsd_list = tsd->ts_next;
	mutex_exit(&tsd_mutex);

	/*
	 * free up the TSD
	 */
	kmem_free(tsd->ts_value, tsd->ts_nkeys * sizeof (void *));
	kmem_free(tsd, sizeof (struct tsd_thread));
	curthread->t_tsd = NULL;
}
/*
 * initialize the global tsd state (called once by thread_init())
 */
static void
tsd_init(void)
{
	mutex_init(&tsd_mutex, "tsd lock", MUTEX_DEFAULT, DEFAULT_WT);
}
/*
 * realloc
 */
static caddr_t
tsd_realloc(caddr_t old, size_t osize, size_t nsize)
{
	caddr_t	new;

	new = (caddr_t)kmem_zalloc(nsize, KM_SLEEP);
	if (old) {
		bcopy(old, new, osize);
		kmem_free(old, osize);
	}
	return (new);
}

/*
 * Check to see if an interrupt thread might be active at a given ipl.
 * If so return true.
 * We must be conservative--it is ok to give a false yes, but a false no
 * will cause disaster.  (But if the situation changes after we check it is
 * ok--the caller is trying to ensure that an interrupt routine has been
 * exited).
 * This is used when trying to remove an interrupt handler from an autovector
 * list in avintr.c.
 */

int
intr_active(struct cpu *cp, int level)
{
	if (level < LOCK_LEVEL)
		return (cp->cpu_thread != cp->cpu_dispthread);
	else
		return (cp->cpu_on_intr);
}

/*
 * Return non-zero if an interrupt is being serviced.
 */
int
servicing_interrupt()
{
	/*
	 * Note: single-OR used on purpose to return non-zero if T_INTR_THREAD
	 * flag set or CPU->cpu_on_intr is non-zero (indicating high-level
	 * interrupt).
	 */
	return ((curthread->t_flag & T_INTR_THREAD) | CPU->cpu_on_intr);
}
