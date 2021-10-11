/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)strsubr.c 1.145	95/09/26	SMI"
/*	From:	SVr4.0	"kernel:os/strsubr.c	1.37"		*/

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <sys/conf.h>  /* for the fmod routines */
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/session.h>
#include <sys/stream.h>
#include <sys/strsubr.h>
#include <sys/stropts.h>
#include <sys/var.h>
#include <sys/poll.h>
#include <sys/termio.h>
#include <sys/ttold.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/cmn_err.h>
#include <sys/sad.h>
#include <sys/priocntl.h>
#include <sys/procset.h>
#include <sys/tuneable.h>
#include <sys/map.h>
#include <sys/kmem.h>
#include <sys/siginfo.h>
#include <sys/vtrace.h>
#include <sys/callb.h>
#include <sys/debug.h>
#include <sys/modctl.h> /* for modload */

/* KPERF is for kernel perf. measurment tools */

#ifdef KPERF
#include <sys/disp.h>
#endif /* KPERF */

/*
 * WARNING:
 * The variables and routines in this file are private, belonging
 * to the STREAMS subsystem.  These should not be used by modules
 * or drivers.  Compatibility will not be guaranteed.
 */

#define	ncopyin(A, B, C, D)	copyin(A, B, C)		/* temporary */
#define	ncopyout(A, B, C, D)	copyout(A, B, C)	/* temporary */

/*
 * Id value used to distinguish between different multiplexor links.
 */
static long lnk_id;

/*
 * Queue scheduling control variables.
 */
char qrunflag;			/* set iff queues are enabled */
struct queue *qhead;		/* head of queues to run */
struct queue *qtail;		/*  last queue */
kcondvar_t services_to_run;	/* wake up background service thread */
kmutex_t service_queue;		/* protects qhead, qtail.  used with */
				/* services_to_run to dispatch threads */
kcondvar_t free_external_buf;	/* dispatches liberator thread */
kmutex_t callback_frees;	/* protects queue of freebs */
kthread_id_t  liberator;
struct free_rtn *frees;
char strbcflag;			/* bufcall functions ready to go */
struct bclist	strbcalls;	/* list of waiting bufcalls */
kmutex_t	bcall_monitor;	/* sleep/wakeup style monitor */
kcondvar_t	bcall_cv;	/* wait 'till executing bufcall completes */
int strscanflag;		/* true when strscan timeout is pending */
kmutex_t	muxifier;	/* single-threads multiplexor creation */
kmutex_t	sad_lock;	/* protects sad drivers autopush */
kthread_id_t	bkgrnd_thread;
extern	void	mhinit();
void		mp_strinit(void);
int 		putctl_wait(queue_t *, int);

/*
 * run_queues is set in known spots before we  do a putnext() on
 * STREAM head.  The idea is to fire off queuerun(), after putnext()
 * as opposed to wakeup the background() thread, and therefore save
 * the extra context switching.  To avoid missed wakeups, or face the
 * overhead of acquirng service_queue lock now we do a queuerun()
 * after the putnext() without checking qready().
 */
int run_queues = 0;

/*
 * Outer perimeter - this is a linked list of outer syncqs that are handed of
 * to the qwriter_outer_thread for it to process.
 */
struct writer_work {
	struct writer_work	*ww_next;
	syncq_t			*ww_outer;
};
struct writer_work	*writer_work, *writer_tail;
kmutex_t	writer_lock;
kcondvar_t	writer_wait;
kthread_id_t	writer_thread;

static struct mux_node *mux_nodes;	/* mux info for cycle checking */
kmutex_t strresources;			/* protects global resources */

perdm_t *perdev_syncq;
perdm_t *permod_syncq;
static kmutex_t perdm_lock;

extern struct qinit strdata;
extern struct qinit stwdata;
extern ulong kmem_avail();

static void	runservice();
void	runbufcalls();
queue_t	*dq_service();
void	background();
void	freebs();
static syncq_t *new_syncq(void);
static void free_syncq(syncq_t *);
static void outer_insert(syncq_t *outer, syncq_t *sq);
static void outer_remove(syncq_t *outer, syncq_t *sq);
static void qwriter_outer_thread(void);
static void write_now(syncq_t *outer);
static void queue_writer_work(syncq_t *outer);
static void set_qfull(queue_t *);
static void clr_qfull(queue_t *);
void set_nfsrv_ptr(queue_t *, queue_t *, queue_t *, queue_t *);
void set_nbsrv_ptr(queue_t *, queue_t *, queue_t *, queue_t *);
void reset_nfsrv_ptr(queue_t *, queue_t *, stdata_t *);
void reset_nbsrv_ptr(queue_t *, queue_t *, stdata_t *);
void set_qend(queue_t *);
static void propagate_syncq(queue_t *qp);
static syncq_t *setq_finddm(perdm_t *dmp, struct streamtab *stp);

#ifdef TRACE
int	enqueued;		/* count of enqueued services */
#endif /* TRACE */

static void	blocksq(syncq_t *, u_long, int);
static void	unblocksq(syncq_t *, u_long, int);
static int	dropsq(syncq_t *, u_long);
static void	emptysq(syncq_t *);
static sqlist_t	*build_sqlist(queue_t *, struct stdata *, int);
static void	free_sqlist(sqlist_t *);
static void	strsetuio(stdata_t *);

extern int _get_pc(void);

struct kmem_cache *stream_head_cache;
struct kmem_cache *queue_cache;
struct kmem_cache *syncq_cache;
struct kmem_cache *qband_cache;
struct kmem_cache *linkinfo_cache;
struct kmem_cache *strsig_cache;
struct kmem_cache *bufcall_cache;
struct kmem_cache *callbparams_cache;

static stdata_t *stream_head_list;
static linkinfo_t *linkinfo_list;

/*
 *  Qinit structure and Module_info structures
 *	for passthru read and write queues
 */


static void pass_wput(queue_t *, mblk_t *);
static queue_t *link_addpassthru(stdata_t *);
static void link_rempassthru(queue_t *);

struct  module_info passthru_info = {
	0,
	"passthru",
	0,
	INFPSZ,
	STRHIGH,
	STRLOW
};
struct  qinit passthru_rinit = {
	putnext,
	NULL,
	NULL,
	NULL,
	NULL,
	&passthru_info,
	NULL
};
struct  qinit passthru_winit = {
	(int (*)()) pass_wput,
	NULL,
	NULL,
	NULL,
	NULL,
	&passthru_info,
	NULL
};


/*
 * constructor/destructor routines for the stream head cache
 */
/* ARGSUSED */
static void
stream_head_constructor(void *buf, size_t size)
{
	stdata_t *stp = buf;

	mutex_init(&stp->sd_lock, "str head %x", MUTEX_DEFAULT, NULL);
	mutex_init(&stp->sd_reflock, "str head ref %x", MUTEX_DEFAULT, NULL);
	cv_init(&stp->sd_monitor, "stream head monitor", CV_DEFAULT, NULL);
	cv_init(&stp->sd_iocmonitor, "stream head ioc monitor",
		CV_DEFAULT, NULL);
	stp->sd_wrq = NULL;

	mutex_enter(&strresources);
	stp->sd_next = stream_head_list;
	stp->sd_prev = NULL;
	if (stp->sd_next)
		stp->sd_next->sd_prev = stp;
	stream_head_list = stp;
	mutex_exit(&strresources);
}

/* ARGSUSED */
static void
stream_head_destructor(void *buf, size_t size)
{
	stdata_t *stp = buf;

	mutex_enter(&strresources);
	if (stp->sd_next)
		stp->sd_next->sd_prev = stp->sd_prev;
	if (stp->sd_prev)
		stp->sd_prev->sd_next = stp->sd_next;
	else
		stream_head_list = stp->sd_next;
	mutex_exit(&strresources);

	mutex_destroy(&stp->sd_lock);
	mutex_destroy(&stp->sd_reflock);
	cv_destroy(&stp->sd_monitor);
	cv_destroy(&stp->sd_iocmonitor);
}

/*
 * constructor/destructor routines for the queue cache
 */
/* ARGSUSED */
static void
queue_constructor(void *buf, size_t size)
{
	queinfo_t *qip = buf;
	queue_t *qp = &qip->qu_rqueue;
	queue_t *wqp = &qip->qu_wqueue;
	syncq_t	*sq = &qip->qu_syncq;

	qp->q_first = NULL;
	qp->q_link = NULL;
	qp->q_count = 0;

	mutex_init(QLOCK(qp), "read q %x", MUTEX_DEFAULT, NULL);
	cv_init(&qp->q_wait, "str q wait", CV_DEFAULT, NULL);
	cv_init(&qp->q_sync, "str q sync", CV_DEFAULT, NULL);

	wqp->q_first = NULL;
	wqp->q_link = NULL;
	wqp->q_count = 0;

	mutex_init(QLOCK(wqp), "write q %x", MUTEX_DEFAULT, NULL);
	cv_init(&wqp->q_wait, "str q wait", CV_DEFAULT, NULL);
	cv_init(&wqp->q_sync, "str q sync", CV_DEFAULT, NULL);

	sq->sq_head = NULL;
	sq->sq_tail = NULL;
	sq->sq_callbpend = NULL;
	sq->sq_outer = NULL;
	sq->sq_onext = NULL;
	sq->sq_oprev = NULL;

	mutex_init(&sq->sq_lock, "sync q %x", MUTEX_DEFAULT, NULL);
	cv_init(&sq->sq_wait, "syncq wait", CV_DEFAULT, NULL);
	cv_init(&sq->sq_exitwait, "syncq exit wait", CV_DEFAULT, NULL);
}

/* ARGSUSED */
static void
queue_destructor(void *buf, size_t size)
{
	queinfo_t *qip = buf;
	queue_t *qp = &qip->qu_rqueue;
	queue_t *wqp = &qip->qu_wqueue;
	syncq_t	*sq = &qip->qu_syncq;

	mutex_destroy(&qp->q_lock);
	cv_destroy(&qp->q_wait);
	cv_destroy(&qp->q_sync);

	mutex_destroy(&wqp->q_lock);
	cv_destroy(&wqp->q_wait);
	cv_destroy(&wqp->q_sync);

	mutex_destroy(&sq->sq_lock);
	cv_destroy(&sq->sq_wait);
	cv_destroy(&sq->sq_exitwait);
}

/*
 * constructor/destructor routines for the syncq cache
 */
/* ARGSUSED */
static void
syncq_constructor(void *buf, size_t size)
{
	syncq_t	*sq = buf;

	sq->sq_head = NULL;
	sq->sq_tail = NULL;
	sq->sq_callbpend = NULL;
	sq->sq_outer = NULL;
	sq->sq_onext = NULL;
	sq->sq_oprev = NULL;

	mutex_init(&sq->sq_lock, "sync q %x", MUTEX_DEFAULT, NULL);
	cv_init(&sq->sq_wait, "syncq wait", CV_DEFAULT, NULL);
	cv_init(&sq->sq_exitwait, "syncq exit wait", CV_DEFAULT, NULL);
}

/* ARGSUSED */
static void
syncq_destructor(void *buf, size_t size)
{
	syncq_t	*sq = buf;

	ASSERT(sq->sq_head == NULL && sq->sq_tail == NULL);
	ASSERT(sq->sq_callbpend == NULL);
	ASSERT(sq->sq_outer == NULL);
	ASSERT(sq->sq_onext == NULL && sq->sq_oprev == NULL);

	mutex_destroy(&sq->sq_lock);
	cv_destroy(&sq->sq_wait);
	cv_destroy(&sq->sq_exitwait);
}

/*
 * Init routine run from main at boot time.
 */
void
strinit(void)
{
	int i;

	/*
	 * Set up mux_node structures.
	 */
	mux_nodes = kmem_zalloc((sizeof (struct mux_node) * devcnt), KM_SLEEP);
	for (i = 0; i < devcnt; i++)
		mux_nodes[i].mn_imaj = i;

	/*
	 * get all locks in place before threads start to run
	 */
	mutex_init(&bcall_monitor, "sleep/wakeup style monitor",
			MUTEX_DEFAULT, NULL);
	mutex_init(&strresources, "misc streams resources",
			MUTEX_DEFAULT, NULL);
	mutex_init(&callback_frees, "esballoc()ed frees", MUTEX_DEFAULT, NULL);
	mutex_init(&service_queue, "service queue", MUTEX_DEFAULT, NULL);
	mutex_init(&muxifier, "mux race breaker", MUTEX_DEFAULT, NULL);
	mutex_init(&sad_lock, "sad autopush", MUTEX_DEFAULT, NULL);
	cv_init(&services_to_run, "wake up service threads", CV_DEFAULT, NULL);
	cv_init(&free_external_buf, "esballoc'ed buffer callbacks",
			CV_DEFAULT, NULL);
	mutex_init(&writer_lock, "writer work lock", MUTEX_DEFAULT, NULL);
	cv_init(&writer_wait, "writer work semaphore", CV_DEFAULT, NULL);
	cv_init(&bcall_cv, "unbufcall condition var", CV_DEFAULT, NULL);

	perdev_syncq = kmem_zalloc(devcnt * sizeof (perdm_t), KM_SLEEP);
	permod_syncq = kmem_zalloc(fmodcnt * sizeof (perdm_t), KM_SLEEP);
	mutex_init(&perdm_lock, "perdm_lock",
		MUTEX_DEFAULT, NULL);

	/*
	 * Initialize message buffers
	 */
	mhinit();

	stream_head_cache = kmem_cache_create("stream_head_cache",
		sizeof (stdata_t), 0,
		stream_head_constructor, stream_head_destructor, NULL);

	queue_cache = kmem_cache_create("queue_cache", sizeof (queinfo_t), 0,
		queue_constructor, queue_destructor, NULL);

	syncq_cache = kmem_cache_create("syncq_cache", sizeof (syncq_t), 0,
		syncq_constructor, syncq_destructor, NULL);

	qband_cache = kmem_cache_create("qband_cache",
		sizeof (qband_t), 0, NULL, NULL, NULL);

	linkinfo_cache = kmem_cache_create("linkinfo_cache",
		sizeof (linkinfo_t), 0, NULL, NULL, NULL);

	/*
	 * For now strsig_cache, bufcall_cache, and callbparams_cache all
	 * just point to the same cache, since they're all lightly used
	 * and vaguely related.  To change this, just do separate calls
	 * to kmem_cache_create().
	 */
	strsig_cache = bufcall_cache = callbparams_cache =
		kmem_cache_create("strevent_cache",
		sizeof (strevent_t), 0, NULL, NULL, NULL);

	/*
	 * since ncpus at this point is one, this will create
	 * one cpu worth of service threads for now. If there
	 * are more cpus, mp_strinit() from main will create more.
	 */
	mp_strinit();
}

void
str_sendsig(vnode_t *vp, int event, long data)
{
	struct stdata *stp;

	ASSERT(vp->v_stream);
	stp = vp->v_stream;
	if (stp->sd_sigflags & event)
		strsendsig(stp->sd_siglist, event, data);
}

/*
 * Send SIGPOLL signal to all processes registered on the given signal
 * list that want a signal for the specified event.  Always called
 * with exclusive access to siglist.
 */
void
strsendsig(strsig_t *siglist, int event, long data)
{
	strsig_t *ssp;
	k_siginfo_t info;
	struct pid *pidp;
	proc_t  *proc;

	info.si_signo = SIGPOLL;
	info.si_errno = 0;
	for (ssp = siglist; ssp; ssp = ssp->ss_next) {
		if (ssp->ss_events & event) {
			if ((pidp = ssp->ss_pidp) != NULL) {

				/*
				 * XXX This unfortunately still generates
				 * a signal  when a fd is closed but
				 * the proc is active.
				 */
				mutex_enter(&pidlock);
				proc =  prfind(pidp->pid_id);
				if (proc == NULL) {
					mutex_exit(&pidlock);
					continue;
				}
				mutex_enter(&proc->p_lock);
				mutex_exit(&pidlock);
			} else {
				/* pid was released but still on event list */
				continue;
			}
			switch (event) {
			case S_INPUT:
				info.si_code = POLL_IN;
				info.si_band = data;
				goto addq;

			case S_OUTPUT:
				info.si_code = POLL_OUT;
				info.si_band = data;
				goto addq;

			case S_HIPRI:
				info.si_code = POLL_PRI;
				info.si_band = 0;
				goto addq;

			case S_MSG:
				info.si_code = POLL_MSG;
				info.si_band = data;
				goto addq;

			case S_ERROR:
				info.si_code = POLL_ERR;
				info.si_band = 0;
				info.si_errno = (int)data;
				goto addq;

			case S_HANGUP:
				info.si_code = POLL_HUP;
				info.si_band = 0;
addq:
				TRACE_3(TR_FAC_STREAMS_FR,
					TR_STRSENDSIG,
				    "strsendsig:addq %X code %X band %X",
				    proc, info.si_code, info.si_band);
				sigaddq(proc, NULL, &info, KM_NOSLEEP);
				break;

			case S_RDBAND:
				if (ssp->ss_events & S_BANDURG) {
					sigtoproc(proc, NULL, SIGURG, 0);
					break;
				}
				/* FALLTHRU */

			case S_RDNORM:
			case S_WRBAND:
				sigtoproc(proc, NULL, SIGPOLL, 0);
				break;

			default:
				cmn_err(CE_PANIC,
				    "strsendsig: unknown event %x\n", event);
			}
			mutex_exit(&proc->p_lock);
		}
	}
}

/*
 * Attach a stream device or module.
 * qp is a read queue; the new queue goes in so its next
 * read ptr is the argument, and the write queue corresponding
 * to the argument points to this queue.  Return 0 on success,
 * or a non-zero errno on failure.
 */
int
qattach(queue_t *qp, dev_t *devp, int flag, int table, int idx, cred_t *crp)
{
	register queue_t *rq, *wrq;
	struct streamtab *qinfop;
	perdm_t *dmp;
	int sflg;
	int error = 0;
	u_long qflag, sqtype;

	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	rq = allocq();
	wrq = WR(rq);
	if (table == CDEVSW) {
		qflag = devopsp[idx]->devo_cb_ops->cb_flag;
		qinfop = STREAMSTAB(idx);
		dmp = &perdev_syncq[idx];
		sflg = 0;
		error = devflg_to_qflag(qflag, &qflag, &sqtype);
		qflag |= QISDRV;
	} else {
		ASSERT(table == FMODSW);
		qflag = fmodsw[idx].f_flag;
		qinfop = fmodsw[idx].f_str;
		dmp = &permod_syncq[idx];

		/*
		 * Note: To keep the number of OPEN/CLOSES straight, a
		 * modules fmod lock is held, the only way to access
		 * this lock given a queue is to use the modname in
		 * streamtabs.  So the module name in streamtabs and
		 * fmodsw *MUST* be the same.
		 */
#ifdef DEBUG
		if (strcmp(fmodsw[idx].f_str->st_rdinit->qi_minfo->mi_idname,
		    fmodsw[idx].f_name) != 0) {
			cmn_err(CE_WARN,
			    "qattach: modname mismatch f_name: %s"
			    ", mi_idname : %s \n",
			    fmodsw[idx].f_name,
			    fmodsw[idx].f_str->st_rdinit->qi_minfo->mi_idname);
		}
#endif
		sflg = MODOPEN;
		error = devflg_to_qflag(qflag, &qflag, &sqtype);
	}
	if (error) {
		cmn_err(CE_CONT,
		    "stropen: bad MT flags in f_flag/cb_flag: 0x%x",
		    (int)(qflag & D_MTSAFETY_MASK));
		return (error);
	}
	TRACE_2(TR_FAC_STREAMS_FR, TR_QATTACH_FLAGS,
		"qattach:qflag == %X(%X)", qflag, *devp);
	STREAM(rq) = STREAM(wrq) = STREAM(qp);
	/* setq might sleep in allocator - avoid holding locks. */
	setq(rq, qinfop->st_rdinit, qinfop->st_wrinit, qinfop, dmp,
		qflag, sqtype);

	/*
	 * Open the attached module or driver.
	 */
	if (rq->q_flag & QUNSAFE) {
		/*
		 * Do not do an entersq on the queue since
		 * that would prevent entry of the put and svc procedures
		 * during a sleep() in the open. Instead we do all what entersq
		 * does except setting SQ_EXCL.
		 */
		claimq(rq);
		mutex_enter(&unsafe_driver);
		insertq(STREAM(rq), rq, 1);
	} else {
		/*
		 * If there is an outer perimeter get exclusive access during
		 * the open procedure.
		 * Bump up the reference count on the queue.
		 */
		entersq(rq->q_syncq, SQ_OPENCLOSE);
		blockq(rq);
	}

	if (rq->q_flag & QOLD) {
		if ((cmpdev(*devp) == NODEV) ||
		    ((*rq->q_qinfo->qi_qopen)(rq, cmpdev(*devp), flag, sflg) ==
		    OPENFAIL)) {
			if ((rq->q_flag & QUNSAFE)) {
				removeq(rq, 1);
				rq->q_next = NULL; wrq->q_next = NULL;
				releaseq(rq);
				mutex_exit(&unsafe_driver);
			} else {
				unblockq(rq);
				leavesq(rq->q_syncq, SQ_OPENCLOSE);
			}

			if (backq(wrq))
				if (backq(wrq)->q_next == wrq)
					qprocsoff(rq);
			rq->q_next = wrq->q_next = NULL;
			qdetach(rq, 0, 0, crp);
			if ((error = ttolwp(curthread)->lwp_error) == 0)
	/* XXX */
				error = ENXIO;
			return (error);
		}
	} else {
		if (error =
		    (*rq->q_qinfo->qi_qopen)(rq, devp, flag, sflg, crp)) {
			if (rq->q_flag & QUNSAFE) {
				removeq(rq, 1);
				rq->q_next = NULL; wrq->q_next = NULL;
				releaseq(rq);
				mutex_exit(&unsafe_driver);
			} else {
				unblockq(rq);
				leavesq(rq->q_syncq, SQ_OPENCLOSE);
			}

			if (backq(wrq))
				if (backq(wrq)->q_next == wrq)
					qprocsoff(rq);
			rq->q_next = wrq->q_next = NULL;
			qdetach(rq, 0, 0, crp);
			return (error);
		}
	}
	if (rq->q_flag & QUNSAFE) {
		releaseq(rq);
		mutex_exit(&unsafe_driver);
	} else {
		leavesq(rq->q_syncq, SQ_OPENCLOSE);
	}
	ASSERT(qprocsareon(rq));
	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	return (0);
}

/*
 * Handle second open of stream.  For modules, set the
 * last argument to MODOPEN and do not pass any open flags.
 * Ignore dummydev since this is not the first open.
 */
int
qreopen(queue_t *qp, dev_t *devp, int flag, cred_t *crp)
{
	int	error;
	dev_t dummydev;
	register queue_t *wqp = WR(qp);

	ASSERT(qp->q_flag & QREADR);
	entersq(qp->q_syncq, SQ_OPENCLOSE);
	if (qp->q_flag & QOLD) {	/* old interface */
		dev_t oldev;

		/*
		 * check if dev is too large for old interface
		 */
		if ((oldev = cmpdev(*devp)) == NODEV) {
			leavesq(qp->q_syncq, SQ_OPENCLOSE);
			return (ENXIO);
		}
		if ((*qp->q_qinfo->qi_qopen)(qp, oldev, (qp->q_next ? 0 : flag),
		    (wqp->q_next ? MODOPEN : 0), crp) == OPENFAIL) {
			/* XXX */
			if ((error = ttolwp(curthread)->lwp_error) == 0)
				error = ENXIO;
			leavesq(qp->q_syncq, SQ_OPENCLOSE);
			mutex_enter(&STREAM(qp)->sd_lock);
			STREAM(qp)->sd_flag |= STREOPENFAIL;
			mutex_exit(&STREAM(qp)->sd_lock);
			return (error);
		}
	} else {			/* new interface */
		dummydev = *devp;
		if (error = ((*qp->q_qinfo->qi_qopen)(qp, &dummydev,
			(wqp->q_next ? 0 : flag),
			(wqp->q_next ? MODOPEN : 0), crp))) {
			leavesq(qp->q_syncq, SQ_OPENCLOSE);
			mutex_enter(&STREAM(qp)->sd_lock);
			qp->q_stream->sd_flag |= STREOPENFAIL;
			mutex_exit(&STREAM(qp)->sd_lock);
			return (error);
		}
	}
	leavesq(qp->q_syncq, SQ_OPENCLOSE);

	/*
	 * successful MT-safe driver open should
	 * have done qprocson()
	 */
#ifndef LOCKNEST
	ASSERT(qprocsareon(RD(qp)));
#else
	qprocson(RD(qp));
#endif /* LOCKNEST */
	return (0);
}

/*
 * Detach a stream module or device.
 * If clmode == 1 then the module or driver was opened and its
 * close routine must be called.  If clmode == 0, the module
 * or driver was never opened or the open failed, and so its close
 * should not be called.
 */
void
qdetach(queue_t *qp, int clmode, int flag, cred_t *crp)
{
	register queue_t *wqp = WR(qp);
	ASSERT(STREAM(qp)->sd_flag & (STRCLOSE|STWOPEN|STRPLUMB));
	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());

	if (qready())
		queuerun();

	if (clmode) {
		if (qp->q_flag & QUNSAFE) {
			/*
			 * Do not do an entersq on the queue
			 * since that would prevent entry of the put and svc
			 * procedures during a sleep() in the close. Instead
			 * we do everything that entersq does except setting
			 * SQ_EXCL.
			 * Special code in put, putnext and entersq make sure
			 * that no thread can accidentally enter when when
			 * drop the unsafe driver lock after blocking the
			 * queue.
			 */
			mutex_enter(&unsafe_driver);
			claimq(qp);
		} else {
			entersq(qp->q_syncq, SQ_OPENCLOSE);
		}

		if (qp->q_flag & QOLD) {
			(*qp->q_qinfo->qi_qclose)(qp, flag);
		} else {
			(*qp->q_qinfo->qi_qclose)(qp, flag, crp);
		}
		if (qp->q_flag & QUNSAFE) {
			syncq_t	*sq;

			ASSERT(UNSAFE_DRIVER_LOCK_HELD());
			/*
			 * Need to set SQ_BLOCKED to make put, putnext and
			 * entersq notice that they can't enter the queue
			 * when the unsafe_driver lock is dropped.
			 * It is not possible to use blockq() since it would
			 * drop unsafe_driver while waiting and we need to
			 * prevent entry into the queue before unsafe_driver
			 * is released.
			 *
			 * If there are threads blocked on the unsafe_driver
			 * lock in entersq or put* this implies that sq_count
			 * is greater than one. Thus removeq will drop the
			 * unsafe_driver lock and cv_wait until sq_count
			 * goes down to 1. This will cause entersq to do a
			 * leavesq and retry the entry causing it to wait
			 * for SQ_BLOCKED to be reset. The put* routines
			 * will put the message on the syncq and than leave the
			 * queue. The net effect is that removeq will succeed
			 * when all the put* calls are gone but with a possible
			 * thread blocked in entersq. The only thread calling
			 * entersq should be runservice which are handled
			 * separately below.
			 *
			 * Note: this assumes that unsafe drivers have
			 * one syncq per pair of queues (i.e. this will break
			 * if e.g. there were a single system-wide unsafe
			 * syncq.)
			 */
			sq = qp->q_syncq;
			mutex_enter(SQLOCK(sq));
			ASSERT((sq->sq_flags & SQ_BLOCKED) == 0);
			sq->sq_flags |= SQ_BLOCKED;
			mutex_exit(SQLOCK(sq));
			/*
			 * do for the queue what qprocsoff() did for
			 * its safe brethren:  disable qenable(),
			 * half-remove queue from stream.
			 */
			disable_svc(qp);
			removeq(qp, 1);
			/*
			 * Do the missing piece to make it appear like we've
			 * done an entersq. Since sq_count == 1 (for this
			 * thread) and SQ_BLOCKED has been set it is not
			 * possible for SQ_EXCL to be set at this point.
			 * (It can not be set until after the unblockq below.)
			 */
			mutex_enter(SQLOCK(sq));
			ASSERT((sq->sq_flags & SQ_EXCL) == 0);
			sq->sq_flags |= SQ_EXCL;
			mutex_exit(SQLOCK(sq));
		}
		leavesq(qp->q_syncq, SQ_OPENCLOSE);
	} else {
		/*
		 * qi_qopen failed - set SQ_BLOCKED so that we can unblockq
		 * later. blockq requires one claim on the queue which why
		 * we have to claimq/releaseq.
		 */
		ASSERT(qp->q_next == NULL && wqp->q_next == NULL);
		claimq(qp);
		blockq(qp);
		releaseq(qp);
	}

	/*
	 * remove it from the service queue, if it is there.
	 */
	remove_runlist(qp);

	ASSERT(qp->q_syncq->sq_flags & SQ_BLOCKED &&
		wqp->q_syncq->sq_flags & SQ_BLOCKED);

	/*
	 * Allow any threads blocked in entersq to proceed and discover
	 * the QWCLOSE is set.
	 * Note: This assumes that all users of entersq check QWCLOSE.
	 * Currently runservice is the only entersq that can happen
	 * after removeq has finished.
	 * Remove the messages on the syncq otherwise unblockq will
	 * drain these syncqs causing the put procedures to be called.
	 */
	flush_syncq(qp->q_syncq, qp);
	flush_syncq(wqp->q_syncq, wqp);
	ASSERT((qp->q_flag & QPERMOD) ||
		((qp->q_syncq->sq_head == NULL) &&
		(wqp->q_syncq->sq_head == NULL)));

	/*
	 * Flush the queues before q_next is set to NULL. This is needed
	 * in order to backenable any downstream queue before we go away.
	 * Note: we are already removed from the stream so that the
	 * backenabling will not cause any messages to be delivered to our
	 * put procedures.
	 */
	flushq(qp, FLUSHALL);
	flushq(WR(qp), FLUSHALL);

	unblockq(qp);

	/*
	 * wait for any pending service processing to complete
	 */
	wait_svc(qp);

	/* Tidy up - removeq only does a half-remove from stream */
	qp->q_next = wqp->q_next = NULL;
	ASSERT(!(qp->q_flag & QENAB));
	ASSERT(!(wqp->q_flag & QENAB));

	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	/* freeq removes us from the outer perimeter if any */
	freeq(qp);
}

/* Prevent service procedures from being called */
void
disable_svc(queue_t *qp)
{
	register queue_t *wqp = WR(qp);

	ASSERT(qp->q_flag & QREADR);
	mutex_enter(QLOCK(qp));
	qp->q_flag |= QWCLOSE;
	mutex_exit(QLOCK(qp));
	mutex_enter(QLOCK(wqp));
	wqp->q_flag |= QWCLOSE;
	mutex_exit(QLOCK(wqp));
}

/* allow service procedures to be called again */
void
enable_svc(queue_t *qp)
{
	register queue_t *wqp = WR(qp);

	ASSERT(qp->q_flag & QREADR);
	mutex_enter(QLOCK(qp));
	qp->q_flag &= ~QWCLOSE;
	mutex_exit(QLOCK(qp));
	mutex_enter(QLOCK(wqp));
	wqp->q_flag &= ~QWCLOSE;
	mutex_exit(QLOCK(wqp));
}

/*
 * Remove queues from runlist if they are enabled.
 * Only reset QENAB if the queue was removed from the runlist.
 * A queue goes through 3 stages:
 *	It is on the service list and QENAB is set.
 *	It is removed from the service list but QENAB is still set.
 *	QENAB gets changed to QINSERVICE.
 *	QINSERVICE is reset (when the service procedure is done)
 * Thus we can not reset QENAB unless we actually removed it from the service
 * queue.
 */
void
remove_runlist(queue_t *qp)
{
	register queue_t *wqp = WR(qp);
	int did_rq = 0, did_wq = 0;
	ASSERT(qp->q_flag & QREADR);

	mutex_enter(&service_queue);
	if (qp->q_flag & QENAB)
		did_rq = rmv_qp(&qhead, &qtail, qp);
	if (wqp->q_flag & QENAB)
		did_wq = rmv_qp(&qhead, &qtail, wqp);
	mutex_exit(&service_queue);

	if (did_rq) {
		mutex_enter(QLOCK(qp));
		qp->q_flag &= ~QENAB;
		mutex_exit(QLOCK(qp));
	}
	if (did_wq) {
		mutex_enter(QLOCK(wqp));
		wqp->q_flag &= ~QENAB;
		mutex_exit(QLOCK(wqp));
	}
}

/*
 * wait for any pending service processing to complete.
 * The removal of queues from the runlist is not atomic with the
 * clearing of the QENABLED flag and setting the INSERVICE flag.
 * consequently it is possible for remove_runlist in strclose
 * to not find the queue on the runlist but for it to be QENABLED
 * and not yet INSERVICE -> hence wait_svc needs to check QENABLED
 * as well as INSERVICE.
 */
void
wait_svc(queue_t *qp)
{
	register queue_t *wqp = WR(qp);

	ASSERT(qp->q_flag & QREADR);

	mutex_enter(QLOCK(qp));
	while (qp->q_flag & (QINSERVICE|QENAB))
		cv_wait(&qp->q_wait, QLOCK(qp));
	mutex_exit(QLOCK(qp));
	mutex_enter(QLOCK(wqp));
	while (wqp->q_flag & (QINSERVICE|QENAB))
		cv_wait(&wqp->q_wait, QLOCK(wqp));
	mutex_exit(QLOCK(wqp));
}

/*
 * Put ioctl data from user land to ioctl buffers.  Return non-zero
 * errno for failure, 1 for success.
 * flag must be K_TO_K or U_TO_K. In addtion, STR_NOSIG is also
 * supported; i.e. allocb_wait() will not give up if a signal
 * arrives.
 */
int
putiocd(mblk_t *bp, mblk_t *ebp, char *arg, int flag, char *fmt)
{
	register mblk_t *tmp;
	register int count, n;
	mblk_t *obp = bp;
	int error = 0;

	ASSERT((flag & (U_TO_K | K_TO_K)) == U_TO_K ||
		(flag & (U_TO_K | K_TO_K)) == K_TO_K);

	if (bp->b_datap->db_type == M_IOCTL)
		count = ((struct iocblk *)bp->b_rptr)->ioc_count;
	else {
		ASSERT(bp->b_datap->db_type == M_COPYIN);
		count = ((struct copyreq *)bp->b_rptr)->cq_size;
	}
	/*
	 * strdoioctl validates ioc_count, so if this assert fails it
	 * cannot be due to user error.
	 */
	ASSERT(count >= 0);

	while (count) {
		n = MIN(MAXIOCBSZ, count);
		if (!(tmp = allocb_wait(n, BPRI_HI, (flag & STR_NOSIG),
		    &error))) {
			return (error);
		}
		error = strcopyin((caddr_t)arg, (caddr_t)tmp->b_wptr, n,
				fmt, flag & (U_TO_K | K_TO_K));
		if (error) {
			freeb(tmp);
			return (error);
		}
		if (fmt && (count > MAXIOCBSZ) && (flag & U_TO_K))
			adjfmtp(&fmt, tmp, n);
		arg += n;
		tmp->b_datap->db_type = M_DATA;
		tmp->b_wptr += n;
		count -= n;
		bp = (bp->b_cont = tmp);
	}

	/*
	 * If ebp was supplied, place it between the
	 * M_IOCTL block and the (optional) M_DATA blocks.
	 */
	if (ebp) {
		ebp->b_cont = obp->b_cont;
		obp->b_cont = ebp;
	}
	return (0);
}

/*
 * Copy ioctl data to user-land.  Return non-zero errno on failure,
 * 0 for success.
 */
int
getiocd(mblk_t *bp, char *arg, int copymode, char *fmt)
{
	register int count, n;
	int error;

	if (bp->b_datap->db_type == M_IOCACK)
		count = ((struct iocblk *)bp->b_rptr)->ioc_count;
	else {
		ASSERT(bp->b_datap->db_type == M_COPYOUT);
		count = ((struct copyreq *)bp->b_rptr)->cq_size;
	}
	ASSERT(count >= 0);

	for (bp = bp->b_cont; bp && count;
	/*LINTED*/
	    count -= n, bp = bp->b_cont, arg += n) {
		n = MIN(count, bp->b_wptr - bp->b_rptr);
		error = strcopyout((caddr_t)bp->b_rptr, arg, n, fmt, copymode);
		if (error)
			return (error);
		if (fmt && bp->b_cont && (copymode == U_TO_K))
			adjfmtp(&fmt, bp, n);
	}
	ASSERT(count == 0);
	return (0);
}

/*
 * Allocate a linkinfo table entry given the write queue of the
 * bottom module of the top stream and the write queue of the
 * stream head of the bottom stream.
 *
 * linkinfo table entries are freed by nulling the li_lblk.l_qbot field.
 */
linkinfo_t *
alloclink(queue_t *qup, queue_t *qdown, file_t *fpdown)
{
	linkinfo_t *linkp;

	linkp = kmem_cache_alloc(linkinfo_cache, KM_NOSLEEP);
	if (linkp == NULL)
		return (NULL);

	linkp->li_lblk.l_qtop = qup;
	linkp->li_lblk.l_qbot = qdown;
	linkp->li_fpdown = fpdown;

	mutex_enter(&strresources);
	linkp->li_next = linkinfo_list;
	linkp->li_prev = NULL;
	if (linkp->li_next)
		linkp->li_next->li_prev = linkp;
	linkinfo_list = linkp;
	linkp->li_lblk.l_index = ++lnk_id;
	ASSERT(lnk_id != 0);	/* this should never wrap in practice */
	mutex_exit(&strresources);

	return (linkp);
}

/*
 * Free a linkinfo entry.
 */
void
lbfree(linkinfo_t *linkp)
{
	mutex_enter(&strresources);
	if (linkp->li_next)
		linkp->li_next->li_prev = linkp->li_prev;
	if (linkp->li_prev)
		linkp->li_prev->li_next = linkp->li_next;
	else
		linkinfo_list = linkp->li_next;
	mutex_exit(&strresources);

	kmem_cache_free(linkinfo_cache, linkp);
}

/*
 * Check for a potential linking cycle.
 * Return 1 if a link will result in a cycle,
 * and 0 otherwise.
 */
int
linkcycle(stdata_t *upstp, stdata_t *lostp)
{
	register struct mux_node *np;
	register struct mux_edge *ep;
	register int i;
	long lomaj;
	long upmaj;
	/*
	 * if the lower stream is a pipe/FIFO, return, since link
	 * cycles can not happen on pipes/FIFOs
	 */
	if (lostp->sd_vnode->v_type == VFIFO)
		return (0);

	for (i = 0; i < devcnt; i++) {
		np = &mux_nodes[i];
		MUX_CLEAR(np);
	}
	lomaj = getmajor(lostp->sd_vnode->v_rdev);
	upmaj = getmajor(upstp->sd_vnode->v_rdev);
	np = &mux_nodes[lomaj];
	for (;;) {
		if (!MUX_DIDVISIT(np)) {
			if (np->mn_imaj == upmaj)
				return (1);
			if (np->mn_outp == NULL) {
				MUX_VISIT(np);
				if (np->mn_originp == NULL)
					return (0);
				np = np->mn_originp;
				continue;
			}
			MUX_VISIT(np);
			np->mn_startp = np->mn_outp;
		} else {
			if (np->mn_startp == NULL) {
				if (np->mn_originp == NULL)
					return (0);
				else {
					np = np->mn_originp;
					continue;
				}
			}
			ep = np->mn_startp;
			np->mn_startp = ep->me_nextp;
			ep->me_nodep->mn_originp = np;
			np = ep->me_nodep;
		}
	}
}

/*
 * Find linkinfo table entry corresponding to the parameters.
 */
linkinfo_t *
findlinks(stdata_t *stp, int index, int type)
{
	linkinfo_t *linkp;
	struct mux_edge *mep;
	struct mux_node *mnp;
	queue_t *qup;

	mutex_enter(&strresources);
	if ((type & LINKTYPEMASK) == LINKNORMAL) {
		qup = getendq(stp->sd_wrq);
		for (linkp = linkinfo_list; linkp; linkp = linkp->li_next) {
			if ((qup == linkp->li_lblk.l_qtop) &&
			    (!index || (index == linkp->li_lblk.l_index))) {
				mutex_exit(&strresources);
				return (linkp);
			}
		}
	} else {
		ASSERT((type & LINKTYPEMASK) == LINKPERSIST);
		mnp = &mux_nodes[getmajor(stp->sd_vnode->v_rdev)];
		mep = mnp->mn_outp;
		while (mep) {
			if ((index == 0) || (index == mep->me_muxid))
				break;
			mep = mep->me_nextp;
		}
		if (!mep) {
			mutex_exit(&strresources);
			return (NULL);
		}
		for (linkp = linkinfo_list; linkp; linkp = linkp->li_next) {
			if ((!linkp->li_lblk.l_qtop) &&
			    (mep->me_muxid == linkp->li_lblk.l_index)) {
				mutex_exit(&strresources);
				return (linkp);
			}
		}
	}
	mutex_exit(&strresources);
	return (NULL);
}

/*
 * Given a queue ptr, follow the chain of q_next pointers until you reach the
 * last queue on the chain and return it.
 */
queue_t *
getendq(queue_t *q)
{
	ASSERT(q != NULL);
	while (SAMESTR(q))
		q = q->q_next;
	return (q);
}

int
mlink(vnode_t *vp, int cmd, int arg, cred_t *crp, int *rvalp)
{
	struct stdata *stp;
	struct file *fpdown;
	struct strioctl strioc;
	struct linkinfo *linkp;
	struct stdata *stpdown;
	queue_t *passq;
	queue_t *rq;
	u_long	qflag;
	u_long	sqtype;
	perdm_t *dmp;
	int error = 0;

	stp = vp->v_stream;
	TRACE_1(TR_FAC_STREAMS_FR,
		TR_I_LINK, "I_LINK/I_PLINK:stp %X", stp);
	/*
	 * Test for invalid upper stream
	 */
	if (stp->sd_flag & STRHUP) {
		return (ENXIO);
	}
	if (vp->v_type == VFIFO) {
		return (EINVAL);
	}
	if (!stp->sd_strtab->st_muxwinit) {
		return (EINVAL);
	}
	if ((fpdown = GETF(arg)) == NULL) { /* gets rwlock */
		return (EBADF);
	}
	mutex_enter(&muxifier);
	if (stp->sd_flag & STPLEX) {
		mutex_exit(&muxifier);
		RELEASEF(arg);
		return (ENXIO);
	}

	/*
	 * Test for invalid lower stream.
	 */
	if (((stpdown = fpdown->f_vnode->v_stream) == NULL) ||
	    (stpdown == stp) || (stpdown->sd_flag &
	    (STPLEX|STRHUP|STRDERR|STWRERR|IOCWAIT)) ||
	    linkcycle(stp, stpdown)) {
		mutex_exit(&muxifier);
		RELEASEF(arg);
		return (EINVAL);
	}
	TRACE_1(TR_FAC_STREAMS_FR,
		TR_STPDOWN, "stpdown:%X", stpdown);
	rq = getendq(stp->sd_wrq);
	qflag = rq->q_flag & QMT_TYPEMASK;
	sqtype = rq->q_syncq->sq_type;
	if (cmd == I_PLINK)
		rq = NULL;
	if (!(linkp = alloclink(rq, stpdown->sd_wrq, fpdown))) {
		RELEASEF(arg);
		mutex_exit(&muxifier);
		return (EAGAIN);
	}
	strioc.ic_cmd = cmd;
	strioc.ic_timout = INFTIM;
	strioc.ic_len = sizeof (struct linkblk);
	strioc.ic_dp = (char *)&linkp->li_lblk;

	/*
	 * Add passthru queue below lower mux.  This will block
	 * syncqs of lower muxs read queue during I_LINK/I_UNLINK.
	 */
	passq = link_addpassthru(stpdown);

	/*
	 * STPLEX prevents any threads from entering the stream from
	 * above.
	 */
	mutex_enter(&stpdown->sd_lock);
	stpdown->sd_flag |= STPLEX;
	mutex_exit(&stpdown->sd_lock);

	rq = RD(stpdown->sd_wrq);
	ASSERT((rq->q_flag & QMT_TYPEMASK) == QMTSAFE);
	ASSERT(rq->q_syncq == SQ(rq) && WR(rq)->q_syncq == SQ(rq));
	rq->q_ptr = WR(rq)->q_ptr = NULL;
	/* setq might sleep in allocator - avoid holding locks. */
	/* Note: we are holding muxifier here. */
	dmp = &perdev_syncq[getmajor(vp->v_rdev)];
	setq(rq, stp->sd_strtab->st_muxrinit,
		stp->sd_strtab->st_muxwinit,
		stp->sd_strtab, dmp, qflag, sqtype);

	/*
	 * XXX Remove any "odd" messages from the queue.
	 * Keep only M_DATA, M_PROTO, M_PCPROTO.
	 */
	if (error = strdoioctl(stp, &strioc, NULL,
	    K_TO_K | STR_NOERROR | STR_NOSIG, STRLINK, crp, rvalp)) {

		lbfree(linkp);

		/*
		 * Restore the stream head queue and then remove
		 * the passq. Turn off STPLEX before we turn on
		 * the stream by removing the passq.
		 */
		rq->q_ptr = WR(rq)->q_ptr = stpdown;
		setq(rq, &strdata, &stwdata, NULL, NULL,
			QMTSAFE, SQ_CI|SQ_CO);

		mutex_enter(&stpdown->sd_lock);
		stpdown->sd_flag &= ~STPLEX;
		mutex_exit(&stpdown->sd_lock);

		link_rempassthru(passq);
		mutex_exit(&muxifier);
		RELEASEF(arg);
		return (error);
	}
	mutex_enter(&fpdown->f_tlock);
	fpdown->f_count++;
	mutex_exit(&fpdown->f_tlock);

	link_rempassthru(passq);
	RELEASEF(arg);
	if (error = mux_addedge(stp, stpdown, linkp->li_lblk.l_index)) {
		int type;

		if (cmd == I_LINK) {
			type = LINKIOCTL|LINKNORMAL;
			strioc.ic_cmd = I_UNLINK;
		} else {	/* I_PLINK */
			type = LINKIOCTL|LINKPERSIST;
			strioc.ic_cmd = I_PUNLINK;
		}
		/* munlink drops the muxifier lock */
		(void) munlink(stp, linkp, type, crp, rvalp);
		return (error);
	}

	/*
	 * Mark the upper stream as having dependent links
	 * so that strclose can clean it up.
	 */
	if (cmd == I_LINK) {
		mutex_enter(&stp->sd_lock);
		stp->sd_flag |= STRHASLINKS;
		mutex_exit(&stp->sd_lock);
	}
	/*
	 * Wake up any other processes that may have been
	 * waiting on the lower stream.  These will all
	 * error out.
	 */
	mutex_enter(&stpdown->sd_lock);
	cv_broadcast(&rq->q_wait);
	cv_broadcast(&WR(rq)->q_wait);
	cv_broadcast(&stpdown->sd_monitor);
	mutex_exit(&stpdown->sd_lock);
	mutex_exit(&muxifier);
	*rvalp = linkp->li_lblk.l_index;
	return (0);
}
/*
 * Unlink a multiplexor link.  Stp is the controlling stream for the
 * link, and linkp points to the link's entry in the linkinfo table.
 * The muxifier lock must be held on entry and is dropped on exit.
 */
int
munlink(stdata_t *stp, linkinfo_t *linkp, int flag, cred_t *crp, int *rvalp)
{
	struct strioctl strioc;
	struct stdata *stpdown;
	queue_t *rq, *wrq;
	queue_t	*passq;
	int error = 0;

	ASSERT(MUTEX_HELD(&muxifier));

	stpdown = linkp->li_fpdown->f_vnode->v_stream;
	/*
	 * Add passthru queue below lower mux.  This will block
	 * syncqs of lower muxs read queue during I_LINK/I_UNLINK.
	 */
	passq = link_addpassthru(stpdown);

	if ((flag & LINKTYPEMASK) == LINKNORMAL)
		strioc.ic_cmd = I_UNLINK;
	else
		strioc.ic_cmd = I_PUNLINK;
	strioc.ic_timout = INFTIM;
	strioc.ic_len = sizeof (struct linkblk);
	strioc.ic_dp = (char *)&linkp->li_lblk;

	error = strdoioctl(stp, &strioc, NULL,
	    K_TO_K | STR_NOERROR | STR_NOSIG, STRLINK, crp, rvalp);

	/*
	 * If there was an error and this is not called via strclose,
	 * return to the user.  Otherwise, pretend there was no error
	 * and close the link.
	 */
	if (error) {
		if (flag & LINKCLOSE) {
			cmn_err(CE_WARN, "KERNEL: munlink: could not perform "
			    "unlink ioctl, closing anyway (%d)\n", error);
		} else {
			link_rempassthru(passq);
			mutex_exit(&muxifier);
			return (error);
		}
	}

	mux_rmvedge(stp, linkp->li_lblk.l_index);
	/*
	 * We go ahead and drop muxifier here--it's a nasty global lock that
	 * can slow others down.  It's okay to since attempts to mlink() this
	 * stream will be stopped because STPLEX is still set in the stdata
	 * structure, and munlink() is stopped because mux_rmvedge has
	 * removed a record of this mux.
	 */
	mutex_exit(&muxifier);

	wrq = stpdown->sd_wrq;
	rq = RD(wrq);

	/*
	 * Get rid of outstanding service procedure runs, before we make
	 * it a stream head, since a stream head doesn't have any service
	 * procedure.
	 */
	disable_svc(rq);
	wait_svc(rq);
	/*
	 * Convert the mux lower queue into a stream head queue.
	 * Turn off STPLEX before we turn on the stream by removing the passq.
	 */
	rq->q_ptr = wrq->q_ptr = stpdown;
	setq(rq, &strdata, &stwdata, NULL, NULL, QMTSAFE, SQ_CI|SQ_CO);
	enable_svc(rq);

	/*
	 * XXX Remove any "odd" messages from the queue.
	 * Keep only M_DATA, M_PROTO, M_PCPROTO. Set STRPRI if M_PCPROTO.
	 */

	mutex_enter(&stpdown->sd_lock);
	stpdown->sd_flag &= ~STPLEX;
	mutex_exit(&stpdown->sd_lock);

	link_rempassthru(passq);

	(void) closef(linkp->li_fpdown);
	lbfree(linkp);
	return (0);
}

/*
 * Unlink all multiplexor links for which stp is the controlling stream.
 * Return 0, or a non-zero errno on failure.
 */
int
munlinkall(stdata_t *stp, int flag, cred_t *crp, int *rvalp)
{
	linkinfo_t *linkp;
	int error = 0;

	mutex_enter(&muxifier);
	while (linkp = findlinks(stp, 0, flag)) {
		/*
		 * munlink() releases the muxifier lock.
		 */
		if (error = munlink(stp, linkp, flag, crp, rvalp))
			return (error);
		mutex_enter(&muxifier);
	}
	mutex_exit(&muxifier);
	return (0);
}

/*
 * A multiplexor link has been made.  Add an
 * edge to the directed graph.  Returns 0 on success
 * and an error number on failure.
 */
int
mux_addedge(stdata_t *upstp, stdata_t *lostp, int muxid)
{
	register struct mux_node *np;
	register struct mux_edge *ep;
	long upmaj;
	long lomaj;

	upmaj = getmajor(upstp->sd_vnode->v_rdev);
	lomaj = getmajor(lostp->sd_vnode->v_rdev);
	np = &mux_nodes[upmaj];
	if (np->mn_outp) {
		ep = np->mn_outp;
		while (ep->me_nextp)
			ep = ep->me_nextp;
		if ((ep->me_nextp = kmem_alloc(sizeof (struct mux_edge),
		    KM_NOSLEEP)) == NULL) {
			cmn_err(CE_WARN, "Cannot allocate memory for mux_edge");
			return (EAGAIN);
		}
		ep = ep->me_nextp;
	} else {
		if ((np->mn_outp = kmem_alloc(sizeof (struct mux_edge),
		    KM_NOSLEEP)) == NULL) {
			cmn_err(CE_WARN, "Cannot allocate memory for mux_edge");
			return (EAGAIN);
		}
		ep = np->mn_outp;
	}
	ep->me_nextp = NULL;
	ep->me_muxid = muxid;
	ep->me_nodep = &mux_nodes[lomaj];
	return (0);
}

/*
 * A multiplexor link has been removed.  Remove the
 * edge in the directed graph.
 */
void
mux_rmvedge(stdata_t *upstp, int muxid)
{
	register struct mux_node *np;
	register struct mux_edge *ep;
	register struct mux_edge *pep = NULL;
	long upmaj;

	upmaj = getmajor(upstp->sd_vnode->v_rdev);
	np = &mux_nodes[upmaj];
	ASSERT(np->mn_outp != NULL);
	ep = np->mn_outp;
	while (ep) {
		if (ep->me_muxid == muxid) {
			if (pep)
				pep->me_nextp = ep->me_nextp;
			else
				np->mn_outp = ep->me_nextp;
			kmem_free(ep, sizeof (struct mux_edge));
			return;
		}
		pep = ep;
		ep = ep->me_nextp;
	}
	ASSERT(0);	/* should not reach here */
}

/*
 * Translate the device flags (from conf.h) to the corresponding
 * qflag and sq_flag (type) values.
 */
int
devflg_to_qflag(u_long devflag, u_long *qflagp, u_long *sqtypep)
{
	u_long qflag = 0;
	u_long sqtype = 0;

	if (devflag & D_OLD)
		qflag |= QOLD;

	/* Inner perimeter presence and scope */
	switch (devflag & D_MTINNER_MASK) {
	case D_MP:
		qflag |= QMTSAFE;
		sqtype |= SQ_CI;
		break;
	case D_MTPERQ|D_MP:
		qflag |= QPERQ;
		break;
	case D_MTQPAIR|D_MP:
		qflag |= QPAIR;
		break;
	case D_MTPERMOD|D_MP:
		qflag |= QPERMOD;
		break;
	case 0 :	/* unsafe driver */
		if (devflag & D_MTSAFETY_MASK)
			return (EINVAL);
		qflag |= QUNSAFE;
		sqtype |= SQ_UNSAFE;
		break;
	default:
		return (EINVAL);
	}

	/* Outer perimeter */
	if (devflag & D_MTOUTPERIM) {
		switch (devflag & D_MTINNER_MASK) {
		case D_MP:
		case D_MTPERQ|D_MP:
		case D_MTQPAIR|D_MP:
			break;
		default:
			return (EINVAL);
		}
		qflag |= QMTOUTPERIM;
	}

	/* Inner perimeter modifiers */
	if (devflag & D_MTPUTSHARED) {
		switch (devflag & D_MTINNER_MASK) {
		case D_MP:
			return (EINVAL);
		default:
			break;
		}
		sqtype |= SQ_CIPUT;
	}

	/* Default outer perimeter concurrency */
	sqtype |= SQ_CO;

	/* Outer perimeter modifiers */
	if (devflag & D_MTOCEXCL) {
		if (!(devflag & D_MTOUTPERIM)) {
			/* No outer perimeter */
			return (EINVAL);
		}
		sqtype &= ~SQ_COOC;
	}

	/* Synchronous Streams extended qinit structure */
	if (devflag & D_SYNCSTR)
		qflag |= QSYNCSTR;

	*qflagp = qflag;
	*sqtypep = sqtype;
	return (0);
}

/*
 * Set the interface values for a pair of queues (qinit structure,
 * packet sizes, water marks).
 * setq assumes that the caller does not have a claim (entersq or claimq)
 * on the queue.
 * The syncqp pointer is non-NULL only for the QPERMOD case.
 */
void
setq(
	queue_t *rq,
	struct qinit *rinit,
	struct qinit *winit,
	struct streamtab *stp,
	perdm_t *dmp,
	u_long qflag,
	u_long sqtype)
{
	register queue_t  *wq;
	syncq_t	*sq, *outer;

	ASSERT(rq->q_flag & QREADR);
	ASSERT((qflag & QMT_TYPEMASK) != 0);

	wq = WR(rq);
	rq->q_qinfo = rinit;
	rq->q_hiwat = rinit->qi_minfo->mi_hiwat;
	rq->q_lowat = rinit->qi_minfo->mi_lowat;
	rq->q_minpsz = rinit->qi_minfo->mi_minpsz;
	rq->q_maxpsz = rinit->qi_minfo->mi_maxpsz;
	wq->q_qinfo = winit;
	wq->q_hiwat = winit->qi_minfo->mi_hiwat;
	wq->q_lowat = winit->qi_minfo->mi_lowat;
	wq->q_minpsz = winit->qi_minfo->mi_minpsz;
	wq->q_maxpsz = winit->qi_minfo->mi_maxpsz;

	/* Remove old syncqs */
	sq = rq->q_syncq;
	outer = sq->sq_outer;
	if (outer != NULL) {
		ASSERT(wq->q_syncq->sq_outer == outer);
		outer_remove(outer, rq->q_syncq);
		if (wq->q_syncq != rq->q_syncq)
			outer_remove(outer, wq->q_syncq);
	}
	ASSERT(sq->sq_outer == NULL);
	ASSERT(sq->sq_onext == NULL && sq->sq_oprev == NULL);

	if (sq != SQ(rq)) {
		if (!(rq->q_flag & QPERMOD))
			free_syncq(sq);
		if (wq->q_syncq == rq->q_syncq)
			wq->q_syncq = NULL;
		rq->q_syncq = NULL;
	}
	if (wq->q_syncq != NULL && wq->q_syncq != sq &&
	    wq->q_syncq != SQ(rq)) {
		free_syncq(wq->q_syncq);
		wq->q_syncq = NULL;
	}
	ASSERT(rq->q_syncq == NULL || (rq->q_syncq->sq_head == NULL &&
				rq->q_syncq->sq_tail == NULL));
	ASSERT(wq->q_syncq == NULL || (wq->q_syncq->sq_head == NULL &&
				wq->q_syncq->sq_tail == NULL));

	sq = SQ(rq);
	ASSERT(sq->sq_head == NULL && sq->sq_tail == NULL);
	ASSERT(sq->sq_outer == NULL);
	ASSERT(sq->sq_onext == NULL && sq->sq_oprev == NULL);

	/*
	 * Create syncqs based on qflag and sqtype. Set the SQ_TYPES_IN_FLAGS
	 * bits in sq_flag based on the sqtype.
	 */
	ASSERT((sq->sq_flags & ~SQ_TYPES_IN_FLAGS) == 0);
	rq->q_syncq = wq->q_syncq = sq;
	sq->sq_type = sqtype;
	sq->sq_flags = (sqtype & SQ_TYPES_IN_FLAGS);

	rq->q_flag = (rq->q_flag & ~QMT_TYPEMASK) | QWANTR | qflag;
	wq->q_flag = (wq->q_flag & ~QMT_TYPEMASK) | QWANTR | qflag;

	if (qflag & QPERQ) {
		/* Allocate a separate syncq for the write side */
		sq = new_syncq();
		sq->sq_type = rq->q_syncq->sq_type;
		sq->sq_flags = rq->q_syncq->sq_flags;
		ASSERT(sq->sq_outer == NULL && sq->sq_onext == NULL &&
		    sq->sq_oprev == NULL);
		wq->q_syncq = sq;
	}
	if (qflag & QPERMOD) {
		ASSERT(dmp);
		if ((sq = dmp->dm_sq) == NULL) {
			/*
			 * Allocate the per-module syncq.  This is a
			 * once-in-the-life-of-the-system operation.
			 * XXX -- this should be done as part of the
			 * modload/unload callback into the streams
			 * framework -- once we have one.
			 */
			syncq_t *newsq = new_syncq();
			mutex_enter(&perdm_lock);
			if ((sq = dmp->dm_sq) != NULL ||
			    (sq = setq_finddm(dmp, stp)) != NULL) {
				free_syncq(newsq);
			} else {
				dmp->dm_sq = sq = newsq;
				dmp->dm_str = stp;
				sq->sq_type = sqtype;
				sq->sq_flags = sqtype & SQ_TYPES_IN_FLAGS;
			}
			mutex_exit(&perdm_lock);
		}
		/*
		 * Assert that we do have an inner perimeter syncq and that it
		 * does not have an outer perimeter associated with it.
		 */
		ASSERT(sq->sq_outer == NULL && sq->sq_onext == NULL &&
		    sq->sq_oprev == NULL);
		rq->q_syncq = wq->q_syncq = sq;
	}
	if (qflag & QMTOUTPERIM) {
		ASSERT(dmp);
		if ((outer = dmp->dm_sq) == NULL) {
			/*
			 * Allocate the outer syncq.  This is a
			 * once-in-the-life-of-the-system operation.
			 */
			syncq_t *newsq = new_syncq();
			mutex_enter(&perdm_lock);
			if ((outer = dmp->dm_sq) != NULL ||
			    (outer = setq_finddm(dmp, stp)) != NULL) {
				free_syncq(newsq);
			} else {
				dmp->dm_sq = outer = newsq;
				dmp->dm_str = stp;
				outer->sq_onext = outer->sq_oprev = outer;
				outer->sq_type = 0;
				outer->sq_flags = 0;
			}
			mutex_exit(&perdm_lock);
		}
		ASSERT(outer->sq_outer == NULL);
		outer_insert(outer, rq->q_syncq);
		if (wq->q_syncq != rq->q_syncq)
			outer_insert(outer, wq->q_syncq);
	}
	ASSERT((rq->q_syncq->sq_flags & SQ_TYPES_IN_FLAGS) ==
		(rq->q_syncq->sq_type & SQ_TYPES_IN_FLAGS));
	ASSERT((wq->q_syncq->sq_flags & SQ_TYPES_IN_FLAGS) ==
		(wq->q_syncq->sq_type & SQ_TYPES_IN_FLAGS));
	/*
	 * Initialize struio() types.
	 */
	if (rq->q_flag & QSYNCSTR)
		rq->q_struiot = rinit->qi_struiot;
	else
		rq->q_struiot = STRUIOT_NONE;
	if (wq->q_flag & QSYNCSTR)
		wq->q_struiot = winit->qi_struiot;
	else
		wq->q_struiot = STRUIOT_NONE;
}

static syncq_t *
setq_finddm(perdm_t *dmp, struct streamtab *stp)
{
	int i;

	ASSERT(MUTEX_HELD(&perdm_lock));

	for (i = 0; i < devcnt; i++) {
		if (perdev_syncq[i].dm_str == stp) {
			dmp->dm_sq = perdev_syncq[i].dm_sq;
			dmp->dm_str = stp;
			return (dmp->dm_sq);
		}
	}
	for (i = 0; i < fmodcnt; i++) {
		if (permod_syncq[i].dm_str == stp) {
			dmp->dm_sq = permod_syncq[i].dm_sq;
			dmp->dm_str = stp;
			return (dmp->dm_sq);
		}
	}
	return (NULL);
}

/*
 * Make a protocol message given control and data buffers.
 * n.b., this can block; be careful of what locks you hold when calling it.
 */
int
strmakemsg(
	struct strbuf *mctl,
	int count,
	struct uio *uiop,
	stdata_t *stp,
	long flag,
	mblk_t **mpp)
{
	register mblk_t *mp = NULL;
	register mblk_t *bp;
	caddr_t base;
	int pri;
	unsigned char msgtype;
	int wroff = (int)stp->sd_wroff;
	int offlg = 0;
	int error = 0;

	*mpp = NULL;
	if (flag & RS_HIPRI)
		pri = BPRI_MED;
	else
		pri = BPRI_LO;

	/*
	 * Create control part of message, if any.
	 */
	if ((mctl != NULL) && (mctl->len >= 0)) {
		register int ctlcount;
		int allocsz;

		if (flag & RS_HIPRI)
			msgtype = M_PCPROTO;
		else
			msgtype = M_PROTO;

		ctlcount = mctl->len;
		base = mctl->buf;

		/*
		 * Give modules a better chance to reuse M_PROTO/M_PCPROTO
		 * blocks by increasing the size to something more usable.
		 */
		allocsz = MAX(ctlcount, 64);

		/*
		 * Range checking has already been done; simply try
		 * to allocate a message block for the ctl part.
		 */
		while (!(bp = allocb(allocsz, pri))) {
			if (uiop->uio_fmode  & (FNDELAY|FNONBLOCK))
				return (EAGAIN);
			if (error = strwaitbuf(allocsz, pri))
				return (error);
		}

		bp->b_datap->db_type = msgtype;
		if (copyin(base, (caddr_t)bp->b_wptr, ctlcount)) {
			freeb(bp);
			return (EFAULT);
		}
		bp->b_wptr += ctlcount;
		mp = bp;
	}

	/*
	 * Create data part of message, if any.
	 */
	if (count >= 0) {
		register int size;
		register dblk_t *dp;

		size = count + (offlg ? 0 : wroff);
		while ((bp = allocb(size, pri)) == NULL) {
			if (uiop->uio_fmode  & (FNDELAY|FNONBLOCK))
				return (EAGAIN);
			if (error = strwaitbuf(size, pri)) {
				freemsg(mp);
				return (error);
			}
		}
		dp = bp->b_datap;
		if (wroff && !offlg++ &&
		    (wroff < dp->db_lim - bp->b_wptr)) {
			bp->b_rptr += wroff;
			bp->b_wptr += wroff;
		}
		size = MIN(count, dp->db_lim - bp->b_wptr);
		if (flag & STRUIO_POSTPONE) {
			/*
			 * Setup the stream uio portion of the
			 * dblk for subsequent use by struioget().
			 */
			dp->db_struioflag = STRUIO_SPEC;
			dp->db_struiobase = bp->b_rptr;
			dp->db_struioptr = bp->b_rptr;
			dp->db_struiolim = bp->b_wptr + size;
			*(long long *)dp->db_struioun.data = 0ll;
		} else {
			if (size != 0 && (error = uiomove((caddr_t)bp->b_wptr,
			    size, UIO_WRITE, uiop))) {
			freeb(bp);
			freemsg(mp);
			return (error);
		}
		}

		bp->b_wptr += size;
		count -= size;
		if (!mp)
			mp = bp;
		else
			linkb(mp, bp);

	}
	*mpp = mp;
	return (0);
}

/*
 * Wait for a buffer to become available.  Return non-zero errno
 * if not able to wait, 0 if buffer is probably there.
 */
int
strwaitbuf(int size, int pri)
{
	int id;

	mutex_enter(&bcall_monitor);
	if ((id = bufcall(size, pri, cv_broadcast,
	    (long)&ttoproc(curthread)->p_flag_cv)) == 0) {
		mutex_exit(&bcall_monitor);
		return (ENOSR);
	}
	if (!cv_wait_sig(&(ttoproc(curthread)->p_flag_cv), &bcall_monitor)) {
		unbufcall(id);
		mutex_exit(&bcall_monitor);
		return (EINTR);
	}
	unbufcall(id);
	mutex_exit(&bcall_monitor);
	return (0);
}

/*
 * This function waits for a read or write event to happen on a stream.
 * if STR_NOERROR is passed down in flag, then STPLEX is the
 * only possible stream head error.
 * STR_NOSIG is also supported to ignore signals during the duration
 * of the call
 */
int
strwaitq(stdata_t *stp, int flag, off_t count, int fmode, int *done)
{
	int slpflg, errs;
	int error = 0;
	kcondvar_t *sleepon;
	mblk_t *mp;
	long *rd_count;

	ASSERT(MUTEX_HELD(&stp->sd_lock));
	if ((flag & READWAIT) || (flag & GETWAIT)) {
		slpflg = RSLEEP;
		sleepon = &RD(stp->sd_wrq)->q_wait;
		errs = STRDERR|STPLEX;
	} else {
		slpflg = WSLEEP;
		sleepon = &stp->sd_wrq->q_wait;
		errs = STWRERR|STRHUP|STPLEX;
	}
	if (flag & STR_NOERROR)
		errs = STPLEX;

	if (stp->sd_wakeq & slpflg) {
		/*
		 * A strwakeq() is pending, no need to sleep.
		 */
		stp->sd_wakeq &= ~slpflg;
		*done = 0;
		return (error);
	}

	if (fmode & (FNDELAY|FNONBLOCK)) {
		if (!(flag & NOINTR))
			error = EAGAIN;
		*done = 1;
		return (error);
	}

	if (stp->sd_flag & errs) {
		*done = 1;
		error = strgeterr(stp, errs);
		return (error);
	}

	if (slpflg == RSLEEP) {
		/*
		 * If any module downstream has requested read notification
		 * by setting SNDMREAD flag using M_SETOPTS, send a message
		 * down stream.
		 */
		if ((flag & READWAIT) && (stp->sd_flag & SNDMREAD)) {
			mutex_exit(&stp->sd_lock);
			if (!(mp = allocb_wait(sizeof (long), BPRI_MED,
			    (flag & STR_NOSIG), &error))) {
				mutex_enter(&stp->sd_lock);
				*done = 1;
				return (error);
			}
			mp->b_datap->db_type = M_READ;
			rd_count = (long *)mp->b_wptr;
			*rd_count = count;
			mp->b_wptr += sizeof (long);
			/*
			 * Send the number of bytes requested by the
			 * read as the argument to M_READ.
			 */
			putnext(stp->sd_wrq, mp);
			if (qready())
				queuerun();
			mutex_enter(&stp->sd_lock);

			/*
			 * If any data arrived due to inline processing
			 * of putnext(), don't sleep.
			 */
			mp = RD(stp->sd_wrq)->q_first;
			while (mp) {
				if (!(mp->b_flag & MSGNOGET))
					break;
				mp = mp->b_next;
			}
			if (mp != NULL) {
				*done = 0;
				return (error);
			}
		}
	}

	stp->sd_flag |= slpflg;
	TRACE_5(TR_FAC_STREAMS_FR, TR_STRWAITQ_WAIT2,
		"strwaitq sleeps (2):%X, %X, %X, %X, %X",
		stp, flag, count, fmode, done);
	if (!str_cv_wait(sleepon, &stp->sd_lock, (u_long) -1,
	    flag & STR_NOSIG)) {
		TRACE_5(TR_FAC_STREAMS_FR, TR_STRWAITQ_INTR2,
			"strwaitq interrupt #2:%X, %X, %X, %X, %X",
			stp, flag, count, fmode, done);
		stp->sd_flag &= ~slpflg;
		cv_broadcast(sleepon);
		if (!(flag & NOINTR))
			error = EINTR;
		*done = 1;
		return (error);
	} else  /* EMPTY */
		TRACE_5(TR_FAC_STREAMS_FR, TR_STRWAITQ_WAKE2,
			"strwaitq awakes(2):%X, %X, %X, %X, %X",
			stp, flag, count, fmode, done);
	if (stp->sd_flag & errs) {
		*done = 1;
		error = strgeterr(stp, errs);
		return (error);
	}
	*done = 0;
	return (error);
}

int
str2num(char **str)		/* string to number, updating pointer */
{
	register int n;

	n = 0;
	for (; **str >= '0' && **str <= '9'; (*str)++)
		n = 10 * n + **str - '0';
	return (n);
}

/*
 * Update canon format pointer with "bytes" worth of data.
 */
void
adjfmtp(char **str, mblk_t *bp, int bytes)
{
	register caddr_t addr;
	register caddr_t lim;
	long num;

	addr = (caddr_t)bp->b_rptr;
	lim = addr + bytes;
	while (addr < lim) {
		switch (*(*str)++) {
		case 's':			/* short */
			addr = SALIGN(addr);
			addr = SNEXT(addr);
			break;
		case 'i':			/* integer */
			addr = IALIGN(addr);
			addr = INEXT(addr);
			break;
		case 'l':			/* long */
			addr = LALIGN(addr);
			addr = LNEXT(addr);
			break;
		case 'b':			/* byte */
			addr++;
			break;
		case 'c':			/* character */
			if ((num = str2num(str)) == 0) {
				while (*addr++)
					;
			} else
				addr += num;
			break;
		case 0:
			return;
		default:
			break;
		}
	}
}

/*
 * Perform job control discipline access checks.
 * Return 0 for success and the errno for failure.
 */

#define	cantsend(pp, sig) \
	(sigismember(&pp->p_ignore, sig) || sigisheld(pp, sig))

int
straccess(struct stdata *stp, enum jcaccess mode)
{
	extern kcondvar_t lbolt_cv;	/* XXX: should be in a header file */

	proc_t *pp;
	register sess_t *sp;

	if (stp->sd_sidp == NULL || stp->sd_vnode->v_type == VFIFO)
		return (0);

	pp = ttoproc(curthread);
	mutex_enter(&pp->p_lock);
	sp = pp->p_sessp;

	for (;;) {

		/*
		 * if this is not the calling process's controlling terminal
		 * or the calling process is already in the foreground
		 * then allow access
		 */

		if (sp->s_dev != stp->sd_vnode->v_rdev ||
		    pp->p_pgidp == stp->sd_pgidp) {
			mutex_exit(&pp->p_lock);
			return (0);
		}

		/*
		 * check to see if controlling terminal has been deallocated
		 */

		if (sp->s_vp == NULL) {
			if (cantsend(pp, SIGHUP)) {
				mutex_exit(&pp->p_lock);
				return (EIO);
			}
			mutex_exit(&pp->p_lock);
			pgsignal(pp->p_pgidp, SIGHUP);
			mutex_enter(&pp->p_lock);
		}

		else if (mode == JCGETP) {
			mutex_exit(&pp->p_lock);
			return (0);
		    }

		else if (mode == JCREAD) {
			if (cantsend(pp, SIGTTIN) || pp->p_detached) {
				mutex_exit(&pp->p_lock);
				return (EIO);
			}
			mutex_exit(&pp->p_lock);
			pgsignal(pp->p_pgidp, SIGTTIN);
			mutex_enter(&pp->p_lock);
		}

		else {  /* mode == JCWRITE or JCSETP */
			if ((mode == JCWRITE && !(stp->sd_flag & STRTOSTOP)) ||
			    cantsend(pp, SIGTTOU)) {
				mutex_exit(&pp->p_lock);
				return (0);
			}
			if (pp->p_detached) {
				mutex_exit(&pp->p_lock);
				return (EIO);
			}
			mutex_exit(&pp->p_lock);
			pgsignal(pp->p_pgidp, SIGTTOU);
			mutex_enter(&pp->p_lock);
		}

		/*
		 * We call cv_wait_sig_swap() to cause the appropriate
		 * action for the jobcontrol signal to take place.
		 * We will not actually go to sleep on &lbolt_cv;
		 * we will either stop or get a failure return.
		 */
		if (!cv_wait_sig_swap(&lbolt_cv, &pp->p_lock)) {
			mutex_exit(&pp->p_lock);
			return (EINTR);
		}
	}
}

/*
 * Return size of message of block type (bp->b_datap->db_type)
 */
int
xmsgsize(mblk_t *bp)
{
	register unsigned char type;
	register count = 0;

	type = bp->b_datap->db_type;

	for (; bp; bp = bp->b_cont) {
		if (type != bp->b_datap->db_type)
			break;
		ASSERT(bp->b_wptr >= bp->b_rptr);
		count += bp->b_wptr - bp->b_rptr;
	}
	return (count);
}

/*
 * Allocate a stream head.
 */
struct stdata *
shalloc(queue_t *qp)
{
	stdata_t *stp;

	stp = kmem_cache_alloc(stream_head_cache, KM_SLEEP);

	stp->sd_wrq = WR(qp);
	stp->sd_strtab = NULL;
	stp->sd_iocid = 0;
	stp->sd_mate = NULL;
	stp->sd_freezer = NULL;
	stp->sd_refcnt = 0;
	stp->sd_wakeq = 0;
	stp->sd_struiowrq = NULL;
	stp->sd_struiordq = NULL;
	stp->sd_struiodnak = 0;
	stp->sd_struionak = NULL;
#ifdef C2_AUDIT
	stp->sd_t_audit_data = NULL;
#endif
	return (stp);
}

/*
 * Free a stream head.
 */
void
shfree(stdata_t *stp)
{
	ASSERT(MUTEX_NOT_HELD(&stp->sd_lock));

	stp->sd_wrq = NULL;
	kmem_cache_free(stream_head_cache, stp);
}

/*
 * Allocate a pair of queues and a syncq for the pair
 */
queue_t *
allocq(void)
{
	queinfo_t *qip;
	queue_t *qp, *wqp;
	syncq_t	*sq;

	qip = kmem_cache_alloc(queue_cache, KM_SLEEP);

	qp = &qip->qu_rqueue;
	wqp = &qip->qu_wqueue;
	sq = &qip->qu_syncq;

	qp->q_last	= NULL;
	qp->q_next	= NULL;
	qp->q_ptr	= NULL;
	qp->q_flag	= QUSE | QREADR;
	qp->q_bandp	= NULL;
	qp->q_stream	= NULL;
	qp->q_syncq	= sq;
	qp->q_nband	= 0;
	qp->q_nfsrv	= NULL;
	qp->q_nbsrv	= NULL;
	qp->q_draining	= 0;

	wqp->q_last	= NULL;
	wqp->q_next	= NULL;
	wqp->q_ptr	= NULL;
	wqp->q_flag	= QUSE;
	wqp->q_bandp	= NULL;
	wqp->q_stream	= NULL;
	wqp->q_syncq	= sq;
	wqp->q_nband	= 0;
	wqp->q_nfsrv	= NULL;
	wqp->q_nbsrv	= NULL;
	wqp->q_draining	= 0;

	sq->sq_count	= 0;
	sq->sq_flags	= 0;
	sq->sq_type	= 0;
	sq->sq_callbflags = 0;
	sq->sq_cancelid	= 0;

	return (qp);
}

/*
 * Free a pair of queues and the "attached" syncq.
 * Discard any messages left on the syncq(s), remove the syncq(s) from the
 * outer perimeter, and free the syncq(s) if they are not the "attached" syncq.
 */
void
freeq(queue_t *qp)
{
	qband_t *qbp, *nqbp;
	syncq_t *sq, *outer;
	register queue_t *wqp = WR(qp);

	ASSERT(qp->q_flag & QREADR);

	flush_syncq(qp->q_syncq, qp);
	flush_syncq(wqp->q_syncq, wqp);
	outer = qp->q_syncq->sq_outer;
	if (outer != NULL) {
		outer_remove(outer, qp->q_syncq);
		if (wqp->q_syncq != qp->q_syncq)
			outer_remove(outer, wqp->q_syncq);
	}
	/*
	 * Free any syncqs that are outside what allocq returned.
	 */
	if (qp->q_syncq != SQ(qp) && !(qp->q_flag & QPERMOD))
		free_syncq(qp->q_syncq);
	if (qp->q_syncq != wqp->q_syncq && wqp->q_syncq != SQ(qp))
		free_syncq(wqp->q_syncq);

	ASSERT(MUTEX_NOT_HELD(QLOCK(qp)));
	ASSERT(MUTEX_NOT_HELD(QLOCK(wqp)));
	sq = SQ(qp);
	ASSERT(MUTEX_NOT_HELD(SQLOCK(sq)));
	ASSERT(qp->q_first == NULL && wqp->q_first == NULL);
	ASSERT(qp->q_link == NULL && wqp->q_link == NULL);
	ASSERT(qtail != qp && qtail != wqp);
	ASSERT(qp->q_count == 0 && wqp->q_count == 0);

	/* NOTE: Uncomment the assert below once bugid 1159635 is fixed. */
	/* ASSERT((qp->q_flag & QWANTW) == 0 && (wqp->q_flag & QWANTW) == 0); */

	ASSERT(sq->sq_head == NULL && sq->sq_tail == NULL);
	ASSERT(sq->sq_outer == NULL);
	ASSERT(sq->sq_onext == NULL && sq->sq_oprev == NULL);
	ASSERT(sq->sq_callbpend == NULL);

	qbp = qp->q_bandp;
	while (qbp) {
		nqbp = qbp->qb_next;
		freeband(qbp);
		qbp = nqbp;
	}
	qbp = wqp->q_bandp;
	while (qbp) {
		nqbp = qbp->qb_next;
		freeband(qbp);
		qbp = nqbp;
	}
	kmem_cache_free(queue_cache, qp);
}

/*
 * Allocate a qband structure.
 */
qband_t *
allocband(void)
{
	qband_t *qbp;

	qbp = kmem_cache_alloc(qband_cache, KM_NOSLEEP);
	if (qbp == NULL)
		return (NULL);

	qbp->qb_next	= NULL;
	qbp->qb_count	= 0;
	qbp->qb_first	= NULL;
	qbp->qb_last	= NULL;
	qbp->qb_flag	= 0;

	return (qbp);
}

/*
 * Free a qband structure.
 */
void
freeband(qband_t *qbp)
{
	kmem_cache_free(qband_cache, qbp);
}


/*
 * This routine is consolidation private for STREAMS internal use
 * This routine may only be called from sync routines (i.e.,
 * not from put or service procedures)
 * XXX - should probably create a kstream.c for new special
 * kernel interfaces
 */

/* ARGSUSED */
mblk_t *
allocb_wait(int size, uint pri, uint flags, int *error)
{
	MHP	mh;
	int	kmem_flag;

	kmem_flag = (flags & STR_NOSIG) ? KM_SLEEP : KM_NOSLEEP;

	if (size > MH_MAX_CACHE - sizeof (MH)) {
		while ((mh = kmem_alloc(size + sizeof (MH), kmem_flag))
		    == NULL) {
			ASSERT(kmem_flag == KM_NOSLEEP);
			if (*error = strwaitbuf(size, BPRI_HI)) {
				return (NULL);
			}
		}
		mh_constructor(mh, size + sizeof (MH));
	} else {
		while ((mh = kmem_cache_alloc(mh_table[(size +
		    (sizeof (MH) - 1)) >> MH_ALIGN_SHIFT], kmem_flag))
		    == NULL) {
			ASSERT(kmem_flag == KM_NOSLEEP);
			if (*error = strwaitbuf(size, BPRI_HI)) {
				return (NULL);
			}
		}
	}

	mh->mh_mblk.b_next = NULL;
	mh->mh_mblk.b_prev = NULL;
	mh->mh_mblk.b_cont = NULL;
	mh->mh_mblk.b_rptr = (unsigned char *)&mh[1];
	mh->mh_mblk.b_wptr = (unsigned char *)&mh[1];
	MH_SET_BAND_FLAG(&mh->mh_mblk.b_band, 0);
	MH_SET_REF_TYPE_REFMIN_UIOFLAG(&mh->mh_dblk.db_ref, 1, M_DATA, 1, 0);

	return (&mh->mh_mblk);

}

/*
 * This routine is consolidation private for STREAMS internal use
 * send a control message down
 * This routine may block in allocb_wait() and therefore
 * may only be called from the stream head or routines
 * that may block.
 */
int
putnextctl_wait(queue_t *q, int type)
{
	return (prn_putnextctl_wait(q, type, allocb_wait));
}
/*
 * XXX future consolidation function for putnextctl() too.
 * (as soon as unsafe stuff is removed)
 */
int
prn_putnextctl_wait(queue_t *q, int type, mblk_t *(*allocb_fn)())
{
	int	ret;

	claimstr(q);
	ret = prn_putctl_wait(q->q_next, type, allocb_fn);
	releasestr(q);
	return (ret);
}

/*
 * This routine is consolidation private for STREAMS internal use
 * Create and put a control message on queue.
 * Note: this routine may only be called from the stream head
 * or routines that may block.
 */
int
putctl_wait(queue_t *q, int type)
{
	return (prn_putctl_wait(q, type, allocb_wait));
}
/*
 * XXX future consolidation function for putctl() too.
 * (as soon as unsafe stuff is removed)
 */
int
prn_putctl_wait(queue_t *q, int type, mblk_t *(*allocb_fn)())
{
	register mblk_t *bp;
	int error;

	if ((datamsg(type) && (type != M_DELAY)) ||
		(bp = allocb_fn(0, BPRI_HI, 0, &error)) == NULL)
		return (0);
	bp->b_datap->db_type = (unsigned char) type;
	put(q, bp);
	return (1);
}

/*
 * Check linked list of strhead write queues with held messages;
 * put downstream those that have been held long enough.
 *
 * Run the service procedures of each enabled queue
 *	-- must not be reentered
 *
 * Called by service mechanism (processor dependent) if there
 * are queues to run.  The mechanism is reset.
 * See comment above background() procedure.
 */
void
queuerun(void)
{
	register queue_t *q;

	TRACE_1(TR_FAC_STREAMS_FR, TR_QRUN_START,
		"queuerun starts:enqueued %d", enqueued);

	do {
		if (strbcflag)
			runbufcalls();
		for (;;) {
			mutex_enter(&service_queue);
			q = dq_service();
			if (q == NULL)
				run_queues = 0;
			mutex_exit(&service_queue);
			if (q) {
				mutex_enter(QLOCK(q));
				q->q_link = NULL;
				q->q_flag &= ~QENAB;
				q->q_flag |= QINSERVICE;
				mutex_exit(QLOCK(q));
				TRACE_1(TR_FAC_STREAMS_FR, TR_QRUN_DQ,
					"queuerun dq's:q %X", q);
				runservice(q);
				TRACE_1(TR_FAC_STREAMS_FR, TR_QRUN_DONE,
					"queuerun finishes:q %X", q);
			} else {
				break;
			}
		}
	} while (strbcflag);

	TRACE_1(TR_FAC_STREAMS_FR, TR_QRUN_LEAVES,
		"queuerun leaves:enqueued %d\n", enqueued);
}

/*
 * Function to kick off queue scheduling for those system calls
 * that cause queues to be enabled (read, recv, write, send, ioctl).
 */
void
runqueues(void)
{
	TRACE_1(TR_FAC_STREAMS_FR, TR_RUNQUEUES,
		"runqueues: %x\n", (short)qrunflag);

	if (qrunflag) {
		queuerun();
		return;
	}
}

/*
 * dequeue service at head of service queue "qhead"
 */
queue_t *
dq_service(void)
{
	queue_t	*q = NULL;
	queue_t	*chaseq = NULL;

	ASSERT(MUTEX_HELD(&service_queue));
	for (q = qhead; q != NULL; q = q->q_link) {
		if (!(q->q_flag & QINSERVICE))
			break;
		TRACE_1(TR_FAC_STREAMS_FR,
			TR_DQ_SERVICE, "skipped q:%X", q);
		chaseq = q;
	}
	if (q != NULL) {
		ASSERT(!(q->q_flag & QINSERVICE));
		if (qhead == q)
			qhead = q->q_link;
		else {
			ASSERT(chaseq != NULL);
			chaseq->q_link = q->q_link;
		}
		if (qtail == q) {
			qtail = chaseq;
		}
		if (qhead == NULL) {
			qtail = NULL;
			qrunflag = 0;
		}

#ifdef TRACE
		enqueued--;
#endif /* TRACE */
	}
	return (q);
}

/*
 * remove "qp" from list "qhead".  assumes qhead properly protected.
 * Returns non-zero if found; zero otherwise.
 */
int
rmv_qp(queue_t **qhead, queue_t **qtail, queue_t *qp)
{
	struct queue *q, *prev = NULL;

	TRACE_3(TR_FAC_STREAMS_FR,
		TR_RMV_QP, "rmv_qp:(%X, %X, %X)", *qhead, *qtail, qp);
	for (q = *qhead; q; q = q->q_link)  {
		if (q == qp) {
			if (prev)
				prev->q_link = q->q_link;
			else
				*qhead = q->q_link;
			if (q == *qtail)
				*qtail = prev;
			q->q_link = NULL;
#ifdef TRACE
			enqueued--;
#endif /* TRACE */
			return (1);
		}
		prev = q;
	}
	return (0);
}

/*
 * run any possible bufcalls.
 */
void
runbufcalls(void)
{
	strbufcall_t *bcp;

	mutex_enter(&bcall_monitor);
	mutex_enter(&service_queue);
	strbcflag = 0;

	if (strbcalls.bc_head) {
		register int count;
		int	nevent;

		/*
		 * count how many events are on the list
		 * now so we can check to avoid looping
		 * in low memory situations
		 */
		nevent = 0;
		for (bcp = strbcalls.bc_head; bcp; bcp = bcp->bc_next)
			nevent++;

		/*
		 * get estimate of available memory from kmem_avail().
		 * awake all bufcall functions waiting for
		 * memory whose request could be satisfied
		 * by 'count' memory and let 'em fight for it.
		 */
		count = kmem_avail();
		ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
		while ((bcp = strbcalls.bc_head) != NULL && nevent) {
			--nevent;
			if (bcp->bc_size <= count) {
				int	unsafe;

				unsafe = bcp->bc_unsafe;
				bcp->bc_executor = curthread;
				mutex_exit(&service_queue);
				if (unsafe) {
					mutex_enter(&unsafe_driver);
					(*bcp->bc_func)(bcp->bc_arg);
					mutex_exit(&unsafe_driver);
				} else
					(*bcp->bc_func)(bcp->bc_arg);

				mutex_enter(&service_queue);
				bcp->bc_executor = NULL;
				cv_broadcast(&bcall_cv);
				strbcalls.bc_head = bcp->bc_next;
				kmem_cache_free(bufcall_cache, bcp);
			} else {
				/*
				 * too big, try again later - note
				 * that nevent was decremented above
				 * so we won't retry this one on this
				 * iteration of the loop
				 */
				if (bcp->bc_next != NULL) {
					strbcalls.bc_head = bcp->bc_next;
					bcp->bc_next = NULL;
					strbcalls.bc_tail->bc_next = bcp;
					strbcalls.bc_tail = bcp;
				}
			}
		}
		if (strbcalls.bc_head == NULL)
			strbcalls.bc_tail = NULL;
	}

	mutex_exit(&service_queue);
	mutex_exit(&bcall_monitor);
}


/*
 * actually run queue's service routine.
 */
static void
runservice(queue_t *q)
{
	register qband_t *qbp;

	ASSERT(q->q_qinfo->qi_srvp);
again:
	entersq(q->q_syncq, SQ_SVC);
	TRACE_2(TR_FAC_STREAMS_FR, TR_QRUNSERVICE_START,
		"runservice starts:%s(%X)", QNAME(q), q);

	if (!(q->q_flag & QWCLOSE))
		(*q->q_qinfo->qi_srvp)(q);

	TRACE_1(TR_FAC_STREAMS_FR, TR_QRUNSERVICE_END,
		"runservice ends:(%X)", q);

	leavesq(q->q_syncq, SQ_SVC);

	mutex_enter(QLOCK(q));
	if (q->q_flag & QENAB) {
		q->q_flag &= ~QENAB;
		mutex_exit(QLOCK(q));
		goto again;
	}
	q->q_flag &= ~QINSERVICE;
	q->q_flag &= ~QBACK;
	for (qbp = q->q_bandp; qbp; qbp = qbp->qb_next)
		qbp->qb_flag &= ~QB_BACK;
	/*
	 * Wakeup thread waiting for the service procedure
	 * to be run (strclose and qdetach).
	 */
	cv_broadcast(&q->q_wait);

	mutex_exit(QLOCK(q));
}

/*
 * "background" service processing thread.
 * The removal of queues from the runlist is not atomic with the
 * clearing of the QENABLED flag and setting the INSERVICE flag.
 * consequently it is possible for remove_runlist in strclose
 * to not find the queue on the runlist but for it to be QENABLED
 * and not yet INSERVICE -> hence wait_svc needs to check QENABLED
 * as well as INSERVICE.
 */
void
background(void)
{
	queue_t	*q;
	int	ate = 0;
	callb_cpr_t cprinfo;

	CALLB_CPR_INIT(&cprinfo, &service_queue, callb_generic_cpr, "bckgrnd");
	for (;;) {
		if (strbcflag)
			runbufcalls();
		mutex_enter(&service_queue);
		if (q = dq_service()) {
			TRACE_1(TR_FAC_STREAMS_FR, TR_BACKGROUND_DQ,
				"background dq:%X", q);
			mutex_exit(&service_queue);
			mutex_enter(QLOCK(q));
			q->q_link = NULL;
			q->q_flag &= ~QENAB;
			q->q_flag |= QINSERVICE;
			mutex_exit(QLOCK(q));
			runservice(q);
			ate++;
			TRACE_1(TR_FAC_STREAMS_FR, TR_BACKGROUND_DONE,
				"background finishes:%X", q);
		} else if (strbcflag) {
			mutex_exit(&service_queue);
		} else {
			TRACE_2(TR_FAC_STREAMS_FR, TR_BACKGROUND_AWAKE,
			"background:%s qlen %d", "sleeps", ate);
			CALLB_CPR_SAFE_BEGIN(&cprinfo);
			cv_wait(&services_to_run, &service_queue);
			CALLB_CPR_SAFE_END(&cprinfo, &service_queue);
			ate = 0;
			TRACE_2(TR_FAC_STREAMS_FR, TR_BACKGROUND_AWAKE,
			"background:%s qlen %d", "awakes", enqueued);
			mutex_exit(&service_queue);
		}
	}
}


/*
 * Main function of asynchronous freeb callback thread.
 * Dequeue frtn structures enqueued on frees list and call the user
 * free routine.
 */
void
freebs(void)
{
	struct	free_rtn	*freep;
	callb_cpr_t cprinfo;

	CALLB_CPR_INIT(&cprinfo, &callback_frees, callb_generic_cpr, "freebs");
	for (;;) {
		mutex_enter(&callback_frees);
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		while (frees == NULL)
			cv_wait(&free_external_buf, &callback_frees);
		CALLB_CPR_SAFE_END(&cprinfo, &callback_frees);
		freep = frees;
		frees = freep->free_next;
		/* reset the free_next pointer to avoid infinite loop */
		freep->free_next = NULL;
		mutex_exit(&callback_frees);

		if (freep->free_flags & STRFREE_UNSAFE) {
			mutex_enter(&unsafe_driver);
			(*freep->free_func)(freep->free_arg);
			mutex_exit(&unsafe_driver);
		} else
			(*freep->free_func)(freep->free_arg);
	}
}

/*
 * Allocate an fmodsw[] entry for a loadable streams module. First
 * check if one is already allocated.
 */
int
allocate_fmodsw(char *name)
{
	register struct fmodsw *fmp;
	register int mid;
	char lockname[64];

	mutex_enter(&fmodsw_lock);
	if ((mid = findmodbyname(name)) != -1) {
		mutex_exit(&fmodsw_lock);
		return (mid);
	}

	for (mid = 0, fmp = fmodsw; mid < fmodcnt; mid++, fmp++)
		if (!ALLOCATED_STREAM(fmp))
			break;

	if (mid == fmodcnt) {
		mutex_exit(&fmodsw_lock);
		return (-1);
	}

	/*
	 * copy in no more chars than will fit in destination
	 */
	(void) strncpy(fmp->f_name, name, sizeof (fmp->f_name) - 1);
	fmp->f_name[FMNAMESZ] = 0;	/* guarantee NULL-termination */
	fmp->f_lock = kmem_alloc(sizeof (krwlock_t), KM_SLEEP);
	(void) strncpy(lockname, name, 40);
	lockname[40] = '\0';
	(void) strcat(lockname, " loadable stream lock");
	ASSERT(strlen(lockname) <= 64);
	rw_init(fmp->f_lock, lockname, RW_DEFAULT, DEFAULT_WT);

	mutex_exit(&fmodsw_lock);
	return (mid);
}

/*
 * Currently not used.  Allocated entries are never released even if
 * the module is uninstalled/unloaded.  The same entry will be reused
 * if the module is reloaded.
 */
void
free_fmodsw(struct fmodsw *fmp)
{
	ASSERT(MUTEX_HELD(&fmodsw_lock));

	kmem_free(fmp->f_lock, sizeof (krwlock_t));
	fmp->f_lock = NULL;
	fmp->f_flag = 0;
	fmp->f_str = NULL;
	bzero(fmp->f_name, sizeof (fmp->f_name));
}

int
fmod_lock(int idx, int lock_type)
{
	register struct fmodsw *fmp;

	fmp = &fmodsw[idx];
	if (LOADABLE_STREAM(fmp))
		if (ALLOCATED_STREAM(fmp))
			rw_enter(fmp->f_lock, lock_type);
		else
			return (1);
	return (0);
}

int
fmod_unlock(int idx)
{
	register struct fmodsw *fmp;

	fmp = &fmodsw[idx];
	if (LOADABLE_STREAM(fmp))
		if (ALLOCATED_STREAM(fmp))
			rw_exit(fmp->f_lock);
		else
			return (1);
	return (0);
}

/*
 * Find module
 *
 * return index into fmodsw.
 * The fmodsw entry is locked for loadable streams.
 * If not found and it's one we'll autoload, then try to autoload it.
 * If not found, return -1.
 */
int
findmod(char *name)
{
	register int mid;
	struct fmodsw *fmp;

	/*
	 * mid will be => 0 if the module has been previously loaded
	 */
	mutex_enter(&fmodsw_lock);
	mid = findmodbyname(name);
	mutex_exit(&fmodsw_lock);
	if (mid == -1) {
		/*
		 * The module has never been loaded.
		 * First try and load the module.
		 * modload() will call mod_installstrmod() to
		 * create a new fmodsw entry.  If it does not, then
		 * this is not a streams module or some other
		 * failure occured; In either case fail.
		 */
		if (modload("strmod", name) == -1) {
			return (-1);
		}
		mutex_enter(&fmodsw_lock);
		mid = findmodbyname(name);
		mutex_exit(&fmodsw_lock);
		if (mid == -1)
			return (-1);
	}
	/*
	 * Entries are never removed, so no need for any locks here
	 */
	fmp = &fmodsw[mid];

	/*
	 * XXX I don't think there's such a thing
	 * as a non-loadable streams module anymore
	 */
	if (LOADABLE_STREAM(fmp)) {
		rw_enter(fmp->f_lock, RW_READER);
		/*
		 * another thread could unload the module
		 * after we just loaded it, so loop.
		 */
		while (!STREAM_INSTALLED(fmp)) {
			rw_exit(fmp->f_lock);
			if (modload("strmod", name) == -1) {
				return (-1);
			}
			rw_enter(fmp->f_lock, RW_READER);
		}
	}
	return (mid);
}

/*
 * Lookup a module by name and return the fmodsw index.
 */
int
findmodbyname(char *name)
{
	register int i, j;

	ASSERT(MUTEX_HELD(&fmodsw_lock));

	for (i = 0; i < fmodcnt; i++) {
		if (fmodsw[i].f_name[0] == '\0')
			continue;
		for (j = 0; j < FMNAMESZ + 1; j++) {
			if (fmodsw[i].f_name[j] != name[j])
				break;
			if (name[j] == '\0')
				return (i);
		}
	}
	return (-1);
}

/*
 * This routine only works if the module has already been
 * loaded once before
 */
int
findmodbyindex(int idx)
{
	struct fmodsw *fmp;

	if (idx < 0 || idx >= fmodcnt)
		return (0);

	fmp = &fmodsw[idx];
	if (!ALLOCATED_STREAM(fmp))
		return (0);

	if (LOADABLE_STREAM(fmp)) {
		rw_enter(fmp->f_lock, RW_READER);
		while (!STREAM_INSTALLED(fmp)) {
			rw_exit(fmp->f_lock);
			if (modload("strmod", fmp->f_name) == -1) {
				return (0);
			}
			rw_enter(fmp->f_lock, RW_READER);
		}
	}
	return (1);
}

/*
 * Set the QBACK or QB_BACK flag in the given queue for
 * the given priority band.
 */
void
setqback(queue_t *q, unsigned char pri)
{
	register int i;
	qband_t *qbp;
	qband_t **qbpp;

	ASSERT(MUTEX_HELD(QLOCK(q)));
	if (pri != 0) {
		if (pri > q->q_nband) {
			qbpp = &q->q_bandp;
			while (*qbpp)
				qbpp = &(*qbpp)->qb_next;
			while (pri > q->q_nband) {
				if ((*qbpp = allocband()) == NULL) {
					cmn_err(CE_WARN,
					    "setqback: can't allocate qband\n");
					return;
				}
				(*qbpp)->qb_hiwat = q->q_hiwat;
				(*qbpp)->qb_lowat = q->q_lowat;
				q->q_nband++;
				qbpp = &(*qbpp)->qb_next;
			}
		}
		qbp = q->q_bandp;
		i = pri;
		while (--i)
			qbp = qbp->qb_next;
		qbp->qb_flag |= QB_BACK;
	} else {
		q->q_flag |= QBACK;
	}
}
/* ARGSUSED */
int
strcopyin(char *from, char *to, u_int len, char *fmt, int copyflag)
{
	if (copyflag & U_TO_K) {
		ASSERT((copyflag & K_TO_K) == 0);
		if (ncopyin(from, to, len, fmt))
			return (EFAULT);
	} else {
		ASSERT(copyflag & K_TO_K);
		bcopy(from, to, len);
	}
	return (0);
}

/* ARGSUSED */
int
strcopyout(char *from, char *to, u_int len, char *fmt, int copyflag)
{
	if (copyflag & U_TO_K) {
		if (ncopyout(from, to, len, fmt))
			return (EFAULT);
	} else {
		ASSERT(copyflag & K_TO_K);
		bcopy(from, to, len);
	}
	return (0);
}

void
strsignal(stdata_t *stp, int sig, long band)
{
	TRACE_3(TR_FAC_STREAMS_FR, TR_SENDSIG,
		"strsignal:%X, %X, %X", stp, sig, band);

	mutex_enter(&stp->sd_lock);
	switch (sig) {
	case SIGPOLL:
		if (stp->sd_sigflags & S_MSG)
			strsendsig(stp->sd_siglist, S_MSG, band);
		break;

	default:
		if (stp->sd_pgidp) {
			pgsignal(stp->sd_pgidp, sig);
		}
		break;
	}
	mutex_exit(&stp->sd_lock);
}

void
strhup(stdata_t *stp)
{
	mutex_enter(&stp->sd_lock);
	if (stp->sd_sigflags & S_HANGUP)
		strsendsig(stp->sd_siglist, S_HANGUP, 0L);
	mutex_exit(&stp->sd_lock);
	pollwakeup(&stp->sd_pollist, POLLHUP);
}

void
stralloctty(sess_t *sp, stdata_t *stp)
{
	mutex_enter(&stp->sd_lock);
	mutex_enter(&pidlock);
	stp->sd_sidp = sp->s_sidp;
	stp->sd_pgidp = sp->s_sidp;
	PID_HOLD(stp->sd_pgidp);
	PID_HOLD(stp->sd_sidp);
	mutex_exit(&pidlock);
	mutex_exit(&stp->sd_lock);
}

void
strfreectty(stdata_t *stp)
{
	mutex_enter(&stp->sd_lock);
	pgsignal(stp->sd_pgidp, SIGHUP);
	mutex_enter(&pidlock);
	PID_RELE(stp->sd_pgidp);
	PID_RELE(stp->sd_sidp);
	stp->sd_pgidp = NULL;
	stp->sd_sidp = NULL;
	mutex_exit(&pidlock);
	mutex_exit(&stp->sd_lock);
	if (!(stp->sd_flag & STRHUP))
		strhup(stp);
}

/*
 * Unlink "all" persistent links.
 */
void
strpunlink(cred_t *crp)
{
	stdata_t *stp;
	int rval;

	/*
	 * for each allocated stream head, call munlinkall()
	 * with flag of LINKPERSIST to unlink any/all persistent
	 * links for the device.
	 */
	mutex_enter(&strresources);
	for (stp = stream_head_list; stp; stp = stp->sd_next) {
		if (stp->sd_wrq == NULL)
			continue;
		(void) munlinkall(stp, LINKIOCTL|LINKPERSIST, crp, &rval);
	}
	mutex_exit(&strresources);
}

int
strctty(proc_t *pp, stdata_t *stp)
{
	extern vnode_t *makectty();
	register sess_t *sp = pp->p_sessp;

	mutex_enter(&stp->sd_lock);
	/*
	 * No need to hold the session lock or do a TTYHOLD,
	 * because this is the only thread that can be the
	 * session leader and not have a controlling tty.
	 */

	if ((stp->sd_flag & (STRHUP|STRDERR|STWRERR|STPLEX)) == 0 &&
	    stp->sd_sidp == NULL &&		/* not allocated as ctty */
	    sp->s_sidp == pp->p_pidp &&		/* session leader */
	    sp->s_flag != SESS_CLOSE &&		/* session is not closing */
	    sp->s_vp == NULL) {			/* without ctty */
		mutex_exit(&stp->sd_lock);
		ASSERT(stp->sd_pgidp == NULL);
		alloctty(pp, makectty(stp->sd_vnode));
		stralloctty(sp, stp);
		mutex_enter(&stp->sd_lock);
		stp->sd_flag |= STRISTTY;	/* just to be sure */
		mutex_exit(&stp->sd_lock);
		return (1);
	}
	mutex_exit(&stp->sd_lock);
	return (0);
}

/*
 * Special freemsg that handles M_PASSFP messages.
 * Used by flushq, flushband and flush_syncq.
 */
void
freemsg_flush(mblk_t *mp)
{
	if (mp->b_datap->db_type == M_PASSFP) {
		file_t *fp = ((struct strrecvfd *)mp->b_rptr)->f.fp;

		(void) closef(fp);
	}
	freemsg(mp);
}

/*
 * enable first back queue with svc procedure.
 * Use pri == -1 to avoid the setqback
 */
void
backenable(queue_t *q, int pri)
{
	queue_t	*nq;

	/*
	 * our presence might not prevent other modules in our own
	 * stream from popping/pushing since the caller of getq might not
	 * have a claim on the queue (some drivers do a getq on somebody
	 * else's queue - they know that the queue itself is not going away
	 * but the framework has to guarantee q_next in that stream.)
	 */
	claimstr(q);

	/* find nearest back queue with service proc */
	for (nq = backq(q); nq && !nq->q_qinfo->qi_srvp; nq = backq(nq)) {
		ASSERT(STRMATED(q->q_stream) || STREAM(q) == STREAM(nq));
	}

	if (nq) {
		kthread_id_t freezer;
		/*
		 * backenable can be called either with no locks held
		 * or with the stream frozen (the latter occurs when a module
		 * calls rmvq with the stream frozen.) If the stream is frozen
		 * by the caller the caller will hold all qlocks in the stream.
		 */
		freezer = STREAM(q)->sd_freezer;
		if (freezer == curthread) {
			ASSERT(frozenstr(q));
			ASSERT(MUTEX_HELD(QLOCK(q)));
			ASSERT(MUTEX_HELD(QLOCK(nq)));
		} else
			mutex_enter(QLOCK(nq));
		if (pri != -1)
			setqback(nq, pri);
		qenable_locked(nq);
		if (freezer != curthread)
			mutex_exit(QLOCK(nq));
	}
	releasestr(q);
}

/*
 * Return the appropriate errno when one of flags_to_check is set
 * in sd_flags.
 * Returns 0 if none set.
 * If there is both a read and write error to check we prefer the read error.
 * The flags that are handled are:
 *	STPLEX		return EINVAL
 *	STRDERR		return sd_rerror (and clear if STRDERRNONPERSIST)
 *	STWRERR		return sd_werror (and clear if STWRERRNONPERSIST)
 *	STRHUP		return sd_werror
 */
int
strgeterr(stdata_t *stp, long flags_to_check)
{
	long sd_flag = stp->sd_flag & flags_to_check;
	int error = 0;

	ASSERT(MUTEX_HELD(&stp->sd_lock));
	ASSERT((flags_to_check & ~(STRDERR|STWRERR|STRHUP|STPLEX)) == 0);
	if (sd_flag & STPLEX)
		error = EINVAL;
	else if (sd_flag & STRDERR) {
		error = stp->sd_rerror;
		if (stp->sd_flag & STRDERRNONPERSIST) {
			/*
			 * Read errors are non-persistent i.e. discarded once
			 * returned.
			 */
			stp->sd_rerror = 0;
			stp->sd_flag &= ~STRDERR;
		}
	} else if (sd_flag & STWRERR) {
		error = stp->sd_werror;
		if (stp->sd_flag & STWRERRNONPERSIST) {
			/*
			 * Write errors are non-persistent i.e. discarded once
			 * returned.
			 */
			stp->sd_werror = 0;
			stp->sd_flag &= ~STWRERR;
		}
	} else if (sd_flag & STRHUP) {
		/* sd_werror set when STRHUP */
		error = stp->sd_werror;
	}
	return (error);
}


/*
 * single-thread open/close/push/pop
 * for twisted streams also
 */
int
strsyncplumb(stdata_t *stp, int flag, int cmd)
{
	int waited = 1;
	int error = 0;

	if (STRMATED(stp)) {
		struct stdata *stmatep = stp->sd_mate;

		STRLOCKMATES(stp);
		while (waited) {
			waited = 0;
			while (stmatep->sd_flag & (STWOPEN|STRCLOSE|STRPLUMB)) {
				if ((cmd == I_POP) &&
				    (flag & (FNDELAY|FNONBLOCK))) {
					STRUNLOCKMATES(stp);
					return (EAGAIN);
				}
				waited = 1;
				mutex_exit(&stp->sd_lock);
				if (!cv_wait_sig(&stmatep->sd_monitor,
				    &stmatep->sd_lock)) {
					mutex_exit(&stmatep->sd_lock);
					return (EINTR);
				}
				mutex_exit(&stmatep->sd_lock);
				STRLOCKMATES(stp);
			}
			while (stp->sd_flag & (STWOPEN|STRCLOSE|STRPLUMB)) {
				if ((cmd == I_POP) &&
					(flag & (FNDELAY|FNONBLOCK))) {
					STRUNLOCKMATES(stp);
					return (EAGAIN);
				}
				waited = 1;
				mutex_exit(&stmatep->sd_lock);
				if (!cv_wait_sig(&stp->sd_monitor,
				    &stp->sd_lock)) {
					mutex_exit(&stp->sd_lock);
					return (EINTR);
				}
				mutex_exit(&stp->sd_lock);
				STRLOCKMATES(stp);
			}
			if (stp->sd_flag & (STRDERR|STWRERR|STRHUP|STPLEX)) {
				error = strgeterr(stp,
						STRDERR|STWRERR|STRHUP|STPLEX);
				STRUNLOCKMATES(stp);
				return (error);
			}
		}
		stp->sd_flag |= STRPLUMB;
		STRUNLOCKMATES(stp);
	} else {
		mutex_enter(&stp->sd_lock);
		while (stp->sd_flag & (STWOPEN|STRCLOSE|STRPLUMB)) {
			if ((cmd == I_POP) && (flag & (FNDELAY|FNONBLOCK))) {
				mutex_exit(&stp->sd_lock);
				return (EAGAIN);
			}
			if (!cv_wait_sig(&stp->sd_monitor, &stp->sd_lock)) {
				mutex_exit(&stp->sd_lock);
				return (EINTR);
			}
			if (stp->sd_flag & (STRDERR|STWRERR|STRHUP|STPLEX)) {
				error = strgeterr(stp,
						STRDERR|STWRERR|STRHUP|STPLEX);
				mutex_exit(&stp->sd_lock);
				return (error);
			}
		}
		stp->sd_flag |= STRPLUMB;
		mutex_exit(&stp->sd_lock);
	}
	return (0);
}



/*
 * wait until we are the last thread in our Stream.
 * (so we don't change any q_next while there is anybody to notice).
 * if we are to guarantee that an unsafe driver's interrupt
 * handlers, timeouts, or other async events can reference
 * q_next we also need to ensure that unsafe_driver is held
 * across the relinking.
 *
 * Get all the locks necessary to change q_next.
 * Wait for sd_refcnt to reach 0 and wait for sq_count in
 * all syncqs to reach an acceptable level (1 for mysq and 0 for the
 * other syncqs.
 *
 * This routine is subject to starvation since it does not set any flag to
 * prevent threads from entering a module in the stream (i.e. sq_count can
 * increase on some syncq while it is waiting on some other syncq.)
 *
 * Assumes that only one thread attempts to call strlock for a given
 * stream. If this is not the case the two threads would deadlock.
 * This assumption is guaranteed since strlock is only called by insertq
 * and removeq and streams plumbing changes are single-threaded for
 * a given stream.
 *
 * For pipes, it is not difficult to atomically designate a pair of streams
 * to be mated. Once mated atomically by the framework the twisted pair remain
 * configured that way until dismantled atomically by the framework.
 * When plumbing takes place on a twisted stream it is necessary to ensure that
 * this operation is done exclusively on the twisted stream since two such
 * operations, each initiated on different ends of the pipe will deadlock
 * waiting for each other to complete.
 */
static void
strlock(struct stdata *stream, sqlist_t *sqlist, syncq_t *mysq)
{
	syncql_t *sql, *sql2;

retry:
	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	mutex_enter(&unsafe_driver);
	/*
	 * Wait for any claimstr to go away.
	 */
	if (STRMATED(stream)) {
		struct stdata *stp1, *stp2;

		STRLOCKMATES(stream);
		if (&(stream->sd_lock) > &((stream->sd_mate)->sd_lock)) {
			stp1 = stream;
			stp2 = stream->sd_mate;
		} else {
			stp2 = stream;
			stp1 = stream->sd_mate;
		}
		mutex_enter(&stp1->sd_reflock);
		if (stp1->sd_refcnt > 0) {
			STRUNLOCKMATES(stream);
			mutex_exit(&unsafe_driver);
			cv_wait(&stp1->sd_monitor, &stp1->sd_reflock);
			mutex_exit(&stp1->sd_reflock);
			goto retry;
		}
		mutex_enter(&stp2->sd_reflock);
		if (stp2->sd_refcnt > 0) {
			STRUNLOCKMATES(stream);
			mutex_exit(&stp1->sd_reflock);
			mutex_exit(&unsafe_driver);
			cv_wait(&stp2->sd_monitor, &stp2->sd_reflock);
			mutex_exit(&stp2->sd_reflock);
			goto retry;
		}
	} else {
		mutex_enter(&stream->sd_lock);
		mutex_enter(&stream->sd_reflock);
		while (stream->sd_refcnt > 0) {
			mutex_exit(&stream->sd_lock);
			mutex_exit(&unsafe_driver);
			cv_wait(&stream->sd_monitor, &stream->sd_reflock);
			mutex_exit(&stream->sd_reflock);
			mutex_enter(&unsafe_driver);
			mutex_enter(&stream->sd_lock);
			mutex_enter(&stream->sd_reflock);
		}
	}
	for (sql = sqlist->sqlist_head; sql; sql = sql->sql_next) {
		syncq_t *sq = sql->sql_sq;
		u_int maxcnt;

		mutex_enter(SQLOCK(sq));
		if (sq->sq_count == 0 ||
		    (sq == mysq && sq->sq_count == 1)) {
			ASSERT(sq != mysq || sq->sq_count == 1);
			continue;
		}
		/* Failed - drop all locks that we have acquired so far */
		if (STRMATED(stream)) {
			STRUNLOCKMATES(stream);
			mutex_exit(&stream->sd_reflock);
			mutex_exit(&stream->sd_mate->sd_reflock);
		} else {
			mutex_exit(&stream->sd_lock);
			mutex_exit(&stream->sd_reflock);
		}
		for (sql2 = sqlist->sqlist_head; sql2 != sql;
		    sql2 = sql2->sql_next)
			mutex_exit(SQLOCK(sql2->sql_sq));
		/*
		 * Wait until the count in this syncq drops to an acceptable
		 * level
		 */
		maxcnt = (sq == mysq) ? 1 : 0;
		while (sq->sq_count > maxcnt) {
			sq->sq_flags |= SQ_WANTWAKEUP;
			mutex_exit(&unsafe_driver);
			cv_wait(&sq->sq_wait, SQLOCK(sq));
			mutex_exit(SQLOCK(sq));
			mutex_enter(&unsafe_driver);
			mutex_enter(SQLOCK(sq));
		}
		ASSERT(sq->sq_count == maxcnt);
		mutex_exit(SQLOCK(sq));
		mutex_exit(&unsafe_driver);
		goto retry;
	}
}



/*
 * Drop the locks that strlock acquired
 */
static void
strunlock(struct stdata *stream, sqlist_t *sqlist)
{
	syncql_t *sql;

	if (STRMATED(stream)) {
		STRUNLOCKMATES(stream);
		mutex_exit(&stream->sd_reflock);
		mutex_exit(&stream->sd_mate->sd_reflock);
	} else {
		mutex_exit(&stream->sd_lock);
		mutex_exit(&stream->sd_reflock);
	}
	for (sql = sqlist->sqlist_head; sql; sql = sql->sql_next)
		mutex_exit(SQLOCK(sql->sql_sq));
}


/*
 * given two read queues, insert a new single one after another.
 */
void
insertq(struct stdata *stream, queue_t *new, int unsafe)
{
	queue_t	*after = RD(stream->sd_wrq);
	queue_t *wafter = stream->sd_wrq;
	queue_t *wnew = WR(new);
	sqlist_t *sqlist;
	int have_fifo = 0;

	ASSERT(unsafe == (UNSAFE_DRIVER_LOCK_NOT_HELD() ? 0 : 1));
	TRACE_4(TR_FAC_STREAMS_FR, TR_INSERTQ,
		"insertq:%X, %X %X, %X",
		after->q_qinfo, new->q_qinfo, after, new);
	ASSERT(after->q_flag & QREADR);
	ASSERT(new->q_flag & QREADR);

	if (unsafe)
		mutex_exit(&unsafe_driver);
	sqlist = build_sqlist(new, stream, STRMATED(stream));

	strlock(stream, sqlist, new->q_syncq);

	/* Do we have a FIFO? */
	if (wafter->q_next == after) {
		have_fifo = 1;
		wnew->q_next = new;
		new->q_next = after;
	} else {
		new->q_next = after;
		wnew->q_next = wafter->q_next;
	}

	set_nfsrv_ptr(new, wnew, after, wafter);
	set_nbsrv_ptr(new, wnew, after, wafter);

	if (have_fifo) {
		wafter->q_next = wnew;
	} else {
		if (wafter->q_next)
			OTHERQ(wafter->q_next)->q_next = new;
		wafter->q_next = wnew;
	}

	if (!unsafe)
		mutex_exit(&unsafe_driver);
	set_qend(new);
	/* The QEND flag might have to be updated for the upstream guy */
	set_qend(after);

	ASSERT(SAMESTR(new) == O_SAMESTR(new));
	ASSERT(SAMESTR(wnew) == O_SAMESTR(wnew));
	ASSERT(SAMESTR(after) == O_SAMESTR(after));
	ASSERT(SAMESTR(wafter) == O_SAMESTR(wafter));
	strsetuio(stream);
	strunlock(stream, sqlist);
	free_sqlist(sqlist);
}

/*
 * given a read queue, unlink it from any neighbors.
 */
void
removeq(queue_t *qp, int unsafe)
{
	register queue_t *wqp = WR(qp);
	struct stdata *stream;
	sqlist_t *sqlist;

	ASSERT(unsafe == (UNSAFE_DRIVER_LOCK_HELD() ? 1 : 0));
	TRACE_4(TR_FAC_STREAMS_FR, TR_REMOVEQ,
		"removeq:%X (%X) from %X and %X",
		qp->q_qinfo, qp, wqp->q_next, qp->q_next);
	ASSERT(qp->q_flag&QREADR);
	stream = STREAM(qp);
	ASSERT(stream);
	if (unsafe)
		mutex_exit(&unsafe_driver);

	sqlist = build_sqlist(qp, stream, STRMATED(stream));

	strlock(stream, sqlist, qp->q_syncq);

	reset_nfsrv_ptr(qp, wqp, stream);
	reset_nbsrv_ptr(qp, wqp, stream);

	ASSERT(wqp->q_next == NULL || backq(qp)->q_next == qp);
	ASSERT(qp->q_next == NULL || backq(wqp)->q_next == wqp);
	/* Do we have a FIFO? */
	if (wqp->q_next == qp) {
		stream->sd_wrq->q_next = RD(stream->sd_wrq);
	} else {
		if (wqp->q_next)
			backq(qp)->q_next = qp->q_next;
		if (qp->q_next)
			backq(wqp)->q_next = wqp->q_next;
	}
	mutex_exit(&unsafe_driver);
	/* The QEND flag might have to be updated for the upstream guy */
	if (qp->q_next)
		set_qend(qp->q_next);

	ASSERT(SAMESTR(stream->sd_wrq) == O_SAMESTR(stream->sd_wrq));
	ASSERT(SAMESTR(RD(stream->sd_wrq)) == O_SAMESTR(RD(stream->sd_wrq)));
	/*
	 * Move any messages destined for the put procedures to the next
	 * syncq in line.
	 */
	if (qp->q_next != NULL)
		propagate_syncq(qp);
	if (wqp->q_next != NULL)
		propagate_syncq(wqp);
	strsetuio(stream);
	strunlock(stream, sqlist);
	free_sqlist(sqlist);
	/*
	 * Make sure any messages that were propagated are drained.
	 * Also clear any QFULL bit caused by messages that were propagated.
	 */
	if (qp->q_next != NULL) {
		clr_qfull(qp);
		emptysq(qp->q_next->q_syncq);
	}
	if (wqp->q_next != NULL) {
		clr_qfull(wqp);
		emptysq(wqp->q_next->q_syncq);
	}
	if (unsafe)
		mutex_enter(&unsafe_driver);
}
/*
 * Prevent further entry by setting a flag (like SQ_FROZEN, SQ_BLOCKED or
 * SQ_WRITER) on a syncq.
 * If maxcnt is not -1 it assumes that caller has "maxcnt" claim(s) on the
 * sync queue and waits until sq_count reaches maxcnt.
 *
 * This routine is used for both inner and outer syncqs.
 */
static void
blocksq(syncq_t *sq, u_long flag, int maxcnt)
{
	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	mutex_enter(SQLOCK(sq));
	/*
	 * Wait for SQ_FROZEN/SQ_BLOCKED to be reset.
	 * SQ_FROZEN will be set if there is a frozen stream that has a
	 * queue which also  refers to this "shared" syncq.
	 * SQ_BLOCKED will be set if there is "off" queue which also
	 * refers to this "shared" syncq.
	 */
	while ((sq->sq_flags & flag) ||
	    (maxcnt != -1 && sq->sq_count > (unsigned)maxcnt)) {
		sq->sq_flags |= SQ_WANTWAKEUP;
		cv_wait(&sq->sq_wait, SQLOCK(sq));
	}
	sq->sq_flags |= flag;
	ASSERT(maxcnt == -1 || sq->sq_count == maxcnt);
	mutex_exit(SQLOCK(sq));
}

/*
 * Reset a flag that was set with blocksq.
 *
 * Can not use this routine to reset SQ_WRITER.
 *
 * If "isouter" is set then the syncq is assumed to be an outer perimeter
 * and drain_syncq is not called. Instead we rely on the qwriter_outer thread
 * to handle the queued qwriter operations.
 */
static void
unblocksq(syncq_t *sq, u_long resetflag, int isouter)
{
	u_short		flags;

	mutex_enter(SQLOCK(sq));
	ASSERT(resetflag != SQ_WRITER);
	ASSERT(sq->sq_flags & resetflag);
	flags = sq->sq_flags & ~resetflag;
	sq->sq_flags = flags;
	if (flags & (SQ_QUEUED|SQ_WANTWAKEUP)) {
		if (flags & SQ_WANTWAKEUP) {
			sq->sq_flags = flags & ~SQ_WANTWAKEUP;
			cv_broadcast(&sq->sq_wait);
		}
		if ((flags & SQ_QUEUED) && !(flags & (SQ_STAYAWAY|SQ_EXCL))) {
			if (!isouter)
				drain_syncq(sq);
		}
	}
	mutex_exit(SQLOCK(sq));
}

/*
 * Reset a flag that was set with blocksq.
 * Does not drain the syncq. Use emptysq() for that.
 * Returns 1 if SQ_QUEUED is set. Otherwise 0.
 */
static int
dropsq(syncq_t *sq, u_long resetflag)
{
	u_short		flags;

	mutex_enter(SQLOCK(sq));
	ASSERT(sq->sq_flags & resetflag);
	flags = sq->sq_flags & ~resetflag;
	if (flags & SQ_WANTWAKEUP) {
		flags &= ~SQ_WANTWAKEUP;
		cv_broadcast(&sq->sq_wait);
	}
	sq->sq_flags = flags;
	mutex_exit(SQLOCK(sq));
	if (flags & SQ_QUEUED)
		return (1);
	return (0);
}

/*
 * Empty all the messages on a syncq.
 */
static void
emptysq(syncq_t *sq)
{
	u_short		flags;

	mutex_enter(SQLOCK(sq));
	flags = sq->sq_flags;
	if ((flags & SQ_QUEUED) && !(flags & (SQ_STAYAWAY|SQ_EXCL))) {
		drain_syncq(sq);
	}
	mutex_exit(SQLOCK(sq));
}

/*
 * Assumes that caller has one claim on the queue (on the syncq referenced
 * by the read side queue for a PERQ queue).
 */
void
blockq(queue_t *q)
{
	syncq_t	*sq1, *sq2;

	if (!(q->q_flag & QREADR))
		q = RD(q);
	sq1 = q->q_syncq;
	sq2 = WR(q)->q_syncq;
	if (sq2 != sq1)
		blocksq(sq2, SQ_BLOCKED, 0);
	blocksq(sq1, SQ_BLOCKED, 1);
}

/*
 * Undo what blockq() did.
 */
void
unblockq(queue_t *q)
{
	syncq_t	*sq1, *sq2;

	if (!(q->q_flag & QREADR))
		q = RD(q);
	sq1 = q->q_syncq;
	sq2 = WR(q)->q_syncq;
	if (sq2 != sq1)
		unblocksq(sq2, SQ_BLOCKED, 0);
	unblocksq(sq1, SQ_BLOCKED, 0);
}

/*
 * Ordered insert while removing duplicates.
 */
static void
sqlist_insert(sqlist_t *sqlist, syncq_t *sqp)
{
	syncql_t *sqlp, **prev_sqlpp, *new_sqlp;

	prev_sqlpp = &sqlist->sqlist_head;
	while ((sqlp = *prev_sqlpp) != NULL) {
		if (sqlp->sql_sq >= sqp) {
			if (sqlp->sql_sq == sqp)	/* duplicate */
				return;
			break;
		}
		prev_sqlpp = &sqlp->sql_next;
	}
	new_sqlp = &sqlist->sqlist_array[sqlist->sqlist_index++];
	ASSERT((char *)new_sqlp < (char *)sqlist + sqlist->sqlist_size);
	new_sqlp->sql_next = sqlp;
	new_sqlp->sql_sq = sqp;
	*prev_sqlpp = new_sqlp;
}

/*
 * Walk the write side queues until we hit either the driver
 * or a twist in the stream (SAMESTR will return false in both
 * these cases) then turn around and walk the read side queues
 * back up to the stream head.
 */
static void
sqlist_insertall(sqlist_t *sqlist, queue_t *q)
{
	while (q != NULL) {
		sqlist_insert(sqlist, q->q_syncq);
		if (SAMESTR(q))
			q = q->q_next;
		else if (!(q->q_flag & QREADR))
			q = RD(q);
		else
			return;
	}
}

/*
 * Allocate and build a list of all syncqs in a stream and the syncq(s)
 * associated with the "q" parameter. The resulting list is sorted in a
 * canonical order and is free of duplicates.
 */
static sqlist_t *
build_sqlist(queue_t *q, struct stdata *stp, int do_twist)
{
	int sqlist_size;
	sqlist_t *sqlist;

	/*
	 * Allocate 2 syncql_t's for each pushed module.  Note that
	 * the sqlist_t structure already has 4 syncql_t's built in:
	 * 2 for the stream head, and 2 for the driver/other stream head.
	 */
	sqlist_size = 2 * sizeof (syncql_t) * stp->sd_pushcnt +
		sizeof (sqlist_t);
	if (do_twist)
		sqlist_size += 2 * sizeof (syncql_t) * stp->sd_mate->sd_pushcnt;
	sqlist = kmem_alloc(sqlist_size, KM_SLEEP);

	sqlist->sqlist_head = NULL;
	sqlist->sqlist_size = sqlist_size;
	sqlist->sqlist_index = 0;

	/*
	 * start with the current queue/qpair
	 */
	ASSERT(q->q_flag & QREADR);

	sqlist_insert(sqlist, q->q_syncq);
	sqlist_insert(sqlist, WR(q)->q_syncq);

	sqlist_insertall(sqlist, stp->sd_wrq);
	if (do_twist)
		sqlist_insertall(sqlist, stp->sd_mate->sd_wrq);

	return (sqlist);
}

/*
 * Free the list created by build_sqlist()
 */
static void
free_sqlist(sqlist_t *sqlist)
{
	kmem_free(sqlist, sqlist->sqlist_size);
}

/*
 * Prevent any new entries into any syncq in this stream.
 * Used by freezestr.
 */
void
strblock(queue_t *q)
{
	struct stdata	*stp;
	syncql_t	*sql;
	sqlist_t	*sqlist;

	q = RD(q);

	stp = STREAM(q);
	ASSERT(stp != NULL);

	/*
	 * Get a sorted list with all the duplicates removed containing
	 * all the syncqs referenced by this stream.
	 */
	sqlist = build_sqlist(q, stp, 0);
	for (sql = sqlist->sqlist_head; sql != NULL; sql = sql->sql_next)
		blocksq(sql->sql_sq, SQ_FROZEN, -1);
	free_sqlist(sqlist);
}

/*
 * Release the block on new entries into this stream
 */
void
strunblock(queue_t *q)
{
	struct stdata	*stp;
	syncql_t	*sql;
	sqlist_t	*sqlist;
	int		drain_needed;

	q = RD(q);

	/*
	 * Get a sorted list with all the duplicates removed containing
	 * all the syncqs referenced by this stream.
	 * Have to drop the SQ_FROZEN flag on all the syncqs before
	 * starting to drain them; otherwise the draining might
	 * cause a freezestr in some module on the stream (which
	 * would deadlock.)
	 */
	stp = STREAM(q);
	ASSERT(stp != NULL);
	sqlist = build_sqlist(q, stp, 0);
	drain_needed = 0;
	for (sql = sqlist->sqlist_head; sql != NULL; sql = sql->sql_next)
		drain_needed += dropsq(sql->sql_sq, SQ_FROZEN);
	if (drain_needed) {
		for (sql = sqlist->sqlist_head; sql != NULL;
		    sql = sql->sql_next)
			emptysq(sql->sql_sq);
	}
	free_sqlist(sqlist);
}

/* XXX should be #ifdef DEBUG but it is used by zs_hdlc.c! */
int
qprocsareon(queue_t *rq)
{
	/* Can not use SQ_BLOCKED since syncqs are shared */
	if (rq->q_next == NULL)
		return (0);
	return (WR(rq->q_next)->q_next == WR(rq));
}

#ifdef DEBUG
int
qclaimed(queue_t *q)
{
	return (q->q_syncq->sq_count > 0);
}

/*
 * Check if anyone has frozen this stream with freezestr
 */
int
frozenstr(queue_t *q)
{
	return ((q->q_syncq->sq_flags & SQ_FROZEN) != 0);
}
#endif /* DEBUG */

/*
 * Return 1 if the queue is not full past the extra amount allowed for
 * kernel printfs.  If the queue is full past that point, return 0.
 */
static int
canputextra(queue_t *q)
{
	if (!q)
		return (0);

	ASSERT(STREAM(q) && MUTEX_HELD(&STREAM(q)->sd_lock));
	/* sd_lock protects q_next from changing */

	q = q->q_nfsrv;
	ASSERT(q != NULL);

	if (q->q_count >= q->q_hiwat + 400) {
		return (0);
	}
	return (1);
}

/*
 * Check the uppermost write queue on the stream associated with vp for
 * space for a kernel message (from uprintf/writekmsg).  Allow some space
 * over the normal hiwater mark so we don't lose messages due to normal
 * flow control, but don't let the device (tty) run amok.
 * The sleep is interruptable.
 *
 * Returns 1 on success and 0 on failure.
 */
int
strcheckoutq(stdata_t *stp, int wait)
{
	int done = 0;

	ASSERT(stp->sd_wrq != NULL);

	if (stp->sd_flag & (STWRERR|STRHUP|STPLEX|STWOPEN|STRCLOSE))
		return (0);

	ASSERT(MUTEX_NOT_HELD(&pidlock));

	mutex_enter(&stp->sd_lock);
	if (!canputextra(stp->sd_wrq->q_next)) {
		if (!wait) {
			mutex_exit(&stp->sd_lock);
			return (0);
		}
		while (!canputnext(stp->sd_wrq)) {
			if (strwaitq(stp, WRITEWAIT|NOINTR, (off_t)0,
			    FWRITE, &done) || done) {
				mutex_exit(&stp->sd_lock);
				return (0);
			}
		}
	}
	mutex_exit(&stp->sd_lock);
	return (1);
}

/*
 * Variant of "strwrite" for printing kernel messages to a tty.
 * Arguments are a pointer to the stream head, the address of the
 * characters to print, and the number of characters to print.
 *
 * Flow control is ignored; it is assumed that if any special flow-control
 * checking is to be done, it's already been done by "strcheckoutq".
 *
 * Returns 1 on success and 0 on failure.
 */
int
stroutput(stdata_t *stp, char *base, int count, int wait)
{
	register queue_t *wqp;
	mblk_t *mp;
	struct uio uio;
	struct iovec iov;
	long rmin, rmax;
	long iosize;

	ASSERT(stp->sd_wrq != NULL);

	if (stp->sd_flag & (STWRERR|STRHUP|STPLEX|STWOPEN|STRCLOSE))
		return (0);

	/*
	 * Check the min/max packet size constraints.  If min packet size
	 * is non-zero, the write cannot be split into multiple messages
	 * and still guarantee the size constraints.
	 */
	wqp = stp->sd_wrq;
	rmin = wqp->q_next->q_minpsz;
	rmax = wqp->q_next->q_maxpsz;
	ASSERT(rmax >= 0 || rmax == INFPSZ);

	if (rmax == 0)
		return (0);

	if (strmsgsz != 0) {
		if (rmax == INFPSZ)
			rmax = strmsgsz;
		else
			rmax = MIN(strmsgsz, rmax);
	}

	if ((rmin > 0) && (count < rmin ||
	    (rmax != INFPSZ && count > rmax)))
		return (0);

	/*
	 * Do until count satisfied or error.
	 */
	iov.iov_base = base;
	iov.iov_len = count;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = 0;
	uio.uio_segflg = UIO_SYSSPACE;
	if (wait)
		uio.uio_fmode = 0;
	else
		uio.uio_fmode = FNONBLOCK;
	uio.uio_resid = count;
	do {
		/*
		 * Determine the size of the next message to be
		 * packaged.  May have to break write into several
		 * messages based on max packet size.
		 */
		if (rmax == INFPSZ)
			iosize = uio.uio_resid;
		else
			iosize = MIN(uio.uio_resid, rmax);

		ASSERT(iosize != 0);
		if (strmakemsg((struct strbuf *)NULL, iosize, &uio,
		    stp, (long)0, &mp) || !mp)
			return (0);

		/*
		 * Put block downstream.
		 */
		putnext(wqp, mp);
	} while (uio.uio_resid);
	return (1);
}

/*
 * Enter a queue.
 * Obsoleted interface. Should not be used.
 */
void
enterq(queue_t *q)
{
	entersq(q->q_syncq, SQ_CALLBACK);
}

void
leaveq(queue_t *q)
{
	leavesq(q->q_syncq, SQ_CALLBACK);
}

/*
 * Enter a perimeter. c_inner and c_outer specifies which concurrency bits
 * to check.
 * Wait if SQ_QUEUED is set to preserve ordering between messages and qwriter
 * calls and the running of open, close and service procedures.
 */
void
entersq(syncq_t *sq, int entrypoint)
{
	u_long		count;
	u_long		flags;
	u_long		type;
	u_long		c_inner = entrypoint & SQ_CI;
	u_long		c_outer = entrypoint & SQ_CO;

	/*
	 * Increment ref count to keep closes out of this queue.
	 */
	ASSERT(sq);
	ASSERT(c_inner && c_outer);
retry:
	mutex_enter(SQLOCK(sq));
	flags = sq->sq_flags;
	count = sq->sq_count;
	type = sq->sq_type;
	/*
	 * Wait until we can enter the inner perimeter.
	 * If we want exclusive access we wait until sq_count is 0.
	 * We have to do this before entering the outer perimeter in order
	 * to preserve put/close message ordering.
	 */
	while ((flags & SQ_GOAWAY) || (!(type & c_inner) && count != 0)) {
		ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
		sq->sq_flags = flags | SQ_WANTWAKEUP;
		cv_wait(&sq->sq_wait, SQLOCK(sq));
		count = sq->sq_count;
		flags = sq->sq_flags;
	}
	/* Check if we need to enter the outer perimeter */
	if (!(type & c_outer)) {
		/*
		 * We have to enter the outer perimeter exclusively before
		 * we can increment sq_count to avoid deadlock. This implies
		 * that we have to re-check sq_flags and sq_count.
		 */
		mutex_exit(SQLOCK(sq));
		outer_enter(sq->sq_outer, SQ_GOAWAY);
		mutex_enter(SQLOCK(sq));
		flags = sq->sq_flags;
		count = sq->sq_count;
		while ((flags & (SQ_EXCL|SQ_BLOCKED|SQ_FROZEN)) ||
		    (!(type & c_inner) && count != 0)) {
			ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
			sq->sq_flags = flags | SQ_WANTWAKEUP;
			cv_wait(&sq->sq_wait, SQLOCK(sq));
			count = sq->sq_count;
			flags = sq->sq_flags;
		}
	}
	sq->sq_count = count + 1;
	ASSERT(sq->sq_count != 0);	/* Wraparound */
	if (type & c_inner) {
		/* Shared entry - done */
		mutex_exit(SQLOCK(sq));
		return;
	}
	/* Exclusive entry */
	ASSERT(count == 0);
	sq->sq_flags = flags | SQ_EXCL;
	if (!(flags & SQ_UNSAFE)) {
		mutex_exit(SQLOCK(sq));
		return;
	}
	/* Handle unsafe close synchronization */
	ASSERT(type & c_outer);
	mutex_exit(SQLOCK(sq));
	mutex_enter(&unsafe_driver);
	/*
	 * Check that nothing changed e.g. due to a pending
	 * close. This check is needed since qdetach only
	 * uses the unsafe_driver lock when closing an unsafe
	 * queue (which is necessary to support sleep in
	 * unsafe close routines.) Thus it is possible to
	 * sucessfully "enter" the syncq and then block on
	 * the unsafe_driver lock until the close routine
	 * has completed.
	 */
	mutex_enter(SQLOCK(sq));
	flags = sq->sq_flags;
	if (flags & (SQ_GOAWAY & ~SQ_EXCL)) {
		mutex_exit(SQLOCK(sq));
		leavesq(sq, entrypoint);
		goto retry;
	}
	mutex_exit(SQLOCK(sq));
}

/*
 * leave a syncq.  announce to framework that closes may procede.
 * c_inner and c_outer specifies which concurrency bits
 * to check.
 */
void
leavesq(syncq_t *sq, int entrypoint)
{
	u_long		count;
	u_long		flags;
	u_long		type;
	u_long		c_inner = entrypoint & SQ_CI;
	u_long		c_outer = entrypoint & SQ_CO;

	/*
	 * decrement ref count, drain the syncq if possible, and wake up
	 * any waiting close.
	 */
	ASSERT(sq);
	ASSERT(c_inner && c_outer);
	mutex_enter(SQLOCK(sq));
	flags = sq->sq_flags;
	count = sq->sq_count;
	type = sq->sq_type;
	if (flags & (SQ_QUEUED|SQ_UNSAFE|SQ_WANTWAKEUP|SQ_WANTEXWAKEUP)) {
		if (flags & SQ_UNSAFE)
			mutex_exit(&unsafe_driver);
		if (flags & SQ_WANTWAKEUP) {
			cv_broadcast(&sq->sq_wait);
			flags &= ~SQ_WANTWAKEUP;
		}
		/* Wakeup sleeping qwait/qwait_sig */
		if (flags & SQ_WANTEXWAKEUP) {
			cv_broadcast(&sq->sq_exitwait);
			flags &= ~SQ_WANTEXWAKEUP;
		}
		if ((flags & SQ_QUEUED) && !(flags & SQ_STAYAWAY)) {
			/*
			 * The syncq needs to be drained. "Exit" the syncq
			 * before calling drain_syncq.
			 */
			ASSERT(count != 0);
			sq->sq_count = count - 1;
			ASSERT((flags & SQ_EXCL) || (type & c_inner));
			sq->sq_flags = flags & ~SQ_EXCL;
			drain_syncq(sq);
			mutex_exit(SQLOCK(sq));
			/* Check if we need to exit the outer perimeter */
			/* XXX will this ever by true? */
			if (!(type & c_outer))
				outer_exit(sq->sq_outer);
			return;
		}
	}
	ASSERT(count != 0);
	sq->sq_count = count - 1;
	ASSERT((flags & SQ_EXCL) || (type & c_inner));
	sq->sq_flags = flags & ~SQ_EXCL;
	mutex_exit(SQLOCK(sq));

	/* Check if we need to exit the outer perimeter */
	if (!(sq->sq_type & c_outer))
		outer_exit(sq->sq_outer);
}

/*
 * Prevent q_next from changing in this stream by incrementing sq_count.
 */
void
claimq(queue_t *qp)
{
	syncq_t	*sq = qp->q_syncq;

	mutex_enter(SQLOCK(sq));
	sq->sq_count++;
	ASSERT(sq->sq_count != 0);	/* Wraparound */
	mutex_exit(SQLOCK(sq));
}

/*
 * Undo claimq.
 */
void
releaseq(queue_t *qp)
{
	syncq_t	*sq = qp->q_syncq;
	u_long flags, count;

	mutex_enter(SQLOCK(sq));
	ASSERT(sq->sq_count > 0);
	count = sq->sq_count;
	flags = sq->sq_flags;
	ASSERT(count != 0);
	sq->sq_count = count - 1;
	if (flags & (SQ_WANTWAKEUP|SQ_QUEUED)) {
		if (flags & SQ_WANTWAKEUP) {
			cv_broadcast(&sq->sq_wait);
			sq->sq_flags = flags & ~SQ_WANTWAKEUP;
		}
		if ((flags & SQ_QUEUED) && !(flags & (SQ_STAYAWAY|SQ_EXCL))) {
			drain_syncq(sq);
		}
	}
	mutex_exit(SQLOCK(sq));
}

/*
 * Prevent q_next from changing in this stream by incrementing sd_refcnt.
 */
void
claimstr(qp)
queue_t	*qp;
{
	struct stdata *stp = STREAM(qp);

	mutex_enter(&stp->sd_reflock);
	stp->sd_refcnt++;
	ASSERT(stp->sd_refcnt != 0);	/* Wraparound */
	mutex_exit(&stp->sd_reflock);
}

/*
 * Undo claimstr.
 */
void
releasestr(qp)
queue_t	*qp;
{
	struct stdata *stp = STREAM(qp);

	mutex_enter(&stp->sd_reflock);
	ASSERT(stp->sd_refcnt != 0);
	stp->sd_refcnt--;
	cv_broadcast(&stp->sd_monitor);
	mutex_exit(&stp->sd_reflock);
}

static syncq_t *
new_syncq(void)
{
	syncq_t	*sq;

	sq = kmem_cache_alloc(syncq_cache, KM_SLEEP);

	sq->sq_count = 0;
	sq->sq_callbflags = 0;
	sq->sq_cancelid = 0;

	return (sq);
}

static void
free_syncq(syncq_t *sq)
{
	ASSERT(sq->sq_head == NULL && sq->sq_tail == NULL);
	ASSERT(sq->sq_outer == NULL);
	ASSERT(sq->sq_callbpend == NULL);
	ASSERT(sq->sq_onext == NULL && sq->sq_oprev == NULL);
	kmem_cache_free(syncq_cache, sq);
}

/* Outer perimeter code */

/*
 * The outer syncq uses the fields and flags in the syncq slightly
 * differently from the inner syncqs.
 *	sq_count	Incremented when there are pending or running
 *			writers at the outer perimeter to prevent the set of
 *			inner syncqs that belong to the outer perimeter from
 *			changing.
 *	sq_head/tail	List of deferred qwriter(OUTER) operations.
 *
 *	SQ_BLOCKED	Set to prevent traversing of sq_next,sq_prev while
 *			inner syncqs are added to or removed from the
 *			outer perimeter.
 *	SQ_QUEUED	sq_head/tail has messages queued or a message
 *			is about to be queued.
 *	SQ_WRITER	A thread is currently traversing all the inner syncqs
 *			setting the SQ_WRITER flag.
 */

/*
 * Get write access at the outer perimeter.
 * Note that read acces is done by entersq, putnext, and put by simply
 * incrementing sq_count in the inner syncq.
 *
 * Waits until "flags" is no longer set in the outer to prevent multiple
 * threads from having write access at the same time. SQ_WRITER has to be part
 * of "flags".
 *
 * Increases sq_count on the outer syncq to keep away outer_insert/remove
 * until the outer_exit is finished.
 *
 * outer_enter is vulnerable to starvation since it does not prevent new
 * threads from entering the inner syncqs while it is waiting for sq_count to
 * go to zero.
 */
void
outer_enter(syncq_t *outer, u_long flags)
{
	syncq_t	*sq;
	int	wait_needed;

	ASSERT(outer->sq_outer == NULL && outer->sq_onext != NULL &&
	    outer->sq_oprev != NULL);
	ASSERT(flags & SQ_WRITER);

retry:
	mutex_enter(SQLOCK(outer));
	while (outer->sq_flags & flags) {
		outer->sq_flags |= SQ_WANTWAKEUP;
		cv_wait(&outer->sq_wait, SQLOCK(outer));
	}

	ASSERT(!(outer->sq_flags & SQ_WRITER));
	outer->sq_flags |= SQ_WRITER;
	outer->sq_count++;
	ASSERT(outer->sq_count != 0);	/* wraparound */
	wait_needed = 0;
	/*
	 * Set SQ_WRITER on all the inner syncqs while holding
	 * the SQLOCK on the outer syncq. This ensures that the changing
	 * of SQ_WRITER is atomic under the outer SQLOCK.
	 */
	for (sq = outer->sq_onext; sq != outer; sq = sq->sq_onext) {
		mutex_enter(SQLOCK(sq));
		sq->sq_flags |= SQ_WRITER;
		if (sq->sq_count > 0)
			wait_needed = 1;
		mutex_exit(SQLOCK(sq));
	}
	mutex_exit(SQLOCK(outer));

	/*
	 * Get everybody out of the syncqs sequentially.
	 */
	if (wait_needed) {
		for (sq = outer->sq_onext; sq != outer; sq = sq->sq_onext) {
			mutex_enter(SQLOCK(sq));
			while (sq->sq_count > 0) {
				sq->sq_flags |= SQ_WANTWAKEUP;
				cv_wait(&sq->sq_wait, SQLOCK(sq));
			}
			mutex_exit(SQLOCK(sq));
		}
		/*
		 * Verify that none of the flags got set while we
		 * were waiting for the sq_counts to drop.
		 * If this happens we exit and retry entering the
		 * outer perimeter.
		 */
		mutex_enter(SQLOCK(outer));
		if (outer->sq_flags & (flags & ~SQ_WRITER)) {
			mutex_exit(SQLOCK(outer));
			outer_exit(outer);
			goto retry;
		}
		mutex_exit(SQLOCK(outer));
	}
}

/*
 * Drop the write access at the outer perimeter.
 * Read acces is dropped implicitely (by putnext, put, and leavesq) by
 * decrementing sq_count.
 */
/* ARGSUSED */
void
outer_exit(syncq_t *outer)
{
	syncq_t	*sq;
	int	drain_needed;

	ASSERT(outer->sq_outer == NULL && outer->sq_onext != NULL &&
	    outer->sq_oprev != NULL);
	ASSERT(MUTEX_NOT_HELD(SQLOCK(outer)));

	/*
	 * Atomically (from the perspective of threads calling become_writer)
	 * drop the write access at the outer perimeter by holding
	 * SQLOCK(outer) across all the dropsq calls and the resetting of
	 * SQ_WRITER.
	 * This defines a locking order between the outer perimeter
	 * SQLOCK and the inner perimeter SQLOCKs.
	 */
	mutex_enter(SQLOCK(outer));
	ASSERT(outer->sq_flags & SQ_WRITER);
	if (outer->sq_flags & SQ_QUEUED)
		write_now(outer);
	/*
	 * sq_onext is stable since sq_count has not yet been decreased.
	 * Reset the SQ_WRITER flags in all syncqs.
	 * After dropping SQ_WRITER on the outer syncq we empty all the
	 * inner syncqs.
	 */
	drain_needed = 0;
	for (sq = outer->sq_onext; sq != outer; sq = sq->sq_onext)
		drain_needed += dropsq(sq, SQ_WRITER);
	ASSERT(!(outer->sq_flags & SQ_QUEUED));
	outer->sq_flags &= ~SQ_WRITER;
	if (drain_needed) {
		mutex_exit(SQLOCK(outer));
		for (sq = outer->sq_onext; sq != outer; sq = sq->sq_onext)
			emptysq(sq);
		mutex_enter(SQLOCK(outer));
	}
	if (outer->sq_flags & SQ_WANTWAKEUP) {
		outer->sq_flags &= ~SQ_WANTWAKEUP;
		cv_broadcast(&outer->sq_wait);
	}
	ASSERT(outer->sq_count > 0);
	outer->sq_count--;
	mutex_exit(SQLOCK(outer));
}

/*
 * Add another syncq to an outer perimeter.
 * Block out all other access to the outer perimeter while it is being
 * changed using blocksq.
 * Assumes that the caller has *not* done an outer_enter.
 *
 * Vulnerable to starvation in blocksq.
 */
static void
outer_insert(syncq_t *outer, syncq_t *sq)
{
	ASSERT(outer->sq_outer == NULL && outer->sq_onext != NULL &&
	    outer->sq_oprev != NULL);
	ASSERT(sq->sq_outer == NULL && sq->sq_onext == NULL &&
	    sq->sq_oprev == NULL);	/* Can't be in an outer perimeter */

	/* Get exclusive access to the outer perimeter list */
	blocksq(outer, SQ_BLOCKED, 0);
	ASSERT(outer->sq_flags & SQ_BLOCKED);
	ASSERT(!(outer->sq_flags & SQ_WRITER));

	mutex_enter(SQLOCK(sq));
	sq->sq_outer = outer;
	outer->sq_onext->sq_oprev = sq;
	sq->sq_onext = outer->sq_onext;
	outer->sq_onext = sq;
	sq->sq_oprev = outer;
	mutex_exit(SQLOCK(sq));
	unblocksq(outer, SQ_BLOCKED, 1);
}

/*
 * Remove a syncq from an outer perimeter.
 * Block out all other access to the outer perimeter while it is being
 * changed using blocksq.
 * Assumes that the caller has *not* done an outer_enter.
 *
 * Vulnerable to starvation in blocksq.
 */
static void
outer_remove(syncq_t *outer, syncq_t *sq)
{
	ASSERT(outer->sq_outer == NULL && outer->sq_onext != NULL &&
	    outer->sq_oprev != NULL);
	ASSERT(sq->sq_outer == outer);

	/* Get exclusive access to the outer perimeter list */
	blocksq(outer, SQ_BLOCKED, 0);
	ASSERT(outer->sq_flags & SQ_BLOCKED);
	ASSERT(!(outer->sq_flags & SQ_WRITER));

	mutex_enter(SQLOCK(sq));
	sq->sq_outer = NULL;
	sq->sq_onext->sq_oprev = sq->sq_oprev;
	sq->sq_oprev->sq_onext = sq->sq_onext;
	sq->sq_oprev = sq->sq_onext = NULL;
	mutex_exit(SQLOCK(sq));
	unblocksq(outer, SQ_BLOCKED, 1);
}

/*
 * Queue a defered qwriter(OUTER) callback for this outer perimeter.
 * If this is the first callback for this outer perimeter then add
 * this outer perimeter to the list of outer perimeters that
 * the qwriter_outer_thread will process.
 *
 * Increments sq_count in the outer syncq to prevent the membership
 * of the outer perimeter (in terms of inner syncqs) to change while
 * the callback is pending.
 */
static void
queue_writer(syncq_t *outer, void (*func)(), queue_t *q, mblk_t *mp)
{
	ASSERT(MUTEX_HELD(SQLOCK(outer)));

	mp->b_prev = (mblk_t *)func;
	mp->b_queue = q;
	mp->b_next = NULL;
	outer->sq_count++;	/* Decremented when dequeued */
	ASSERT(outer->sq_count != 0);	/* Wraparound */
	if (outer->sq_head == NULL) {
		/* First message. */
		outer->sq_head = outer->sq_tail = mp;
		outer->sq_flags |= SQ_QUEUED;
		mutex_exit(SQLOCK(outer));
		/* signal qwriter_outer thread */
		queue_writer_work(outer);
	} else {
		outer->sq_tail->b_next = mp;
		outer->sq_tail = mp;
		mutex_exit(SQLOCK(outer));
	}
}

/*
 * Try and upgrade to write access at the outer perimeter. If this can
 * not be done without blocking then queue the callback to be done
 * by the qwriter_outer_thread.
 *
 * This routine can only be called from put or service procedures plus
 * asynchronous callback routines that have properly entered to
 * queue (with entersq.) Thus qwriter(OUTER) assumes the caller has one claim
 * on the syncq associated with q.
 */
void
qwriter_outer(queue_t *q, mblk_t *mp, void (*func)())
{
	syncq_t	*osq, *sq, *outer;
	int	failed;

	osq = q->q_syncq;
	outer = osq->sq_outer;
	if (outer == NULL)
		panic("qwriter(PERIM_OUTER): no outer perimeter");
	ASSERT(outer->sq_outer == NULL && outer->sq_onext != NULL &&
	    outer->sq_oprev != NULL);

	mutex_enter(SQLOCK(outer));
	/*
	 * If some thread is traversing sq_next, or if we are blocked by
	 * outer_insert or outer_remove, or if the we already have queued
	 * callbacks, then queue this callback for later processing.
	 *
	 * Also queue the qwriter for an interrupt thread in order
	 * to reduce the time spent running at high IPL.
	 */
	if ((outer->sq_flags & SQ_GOAWAY) ||
	    (curthread->t_flag & T_INTR_THREAD)) {
		/*
		 * Queue the become_writer request.
		 * The queueing is atomic under SQLOCK(outer) in order
		 * to synchronize with outer_exit.
		 * queue_writer will drop the outer SQLOCK
		 */
		if (outer->sq_flags & SQ_BLOCKED) {
			/* Must set SQ_WRITER on inner perimeter */
			mutex_enter(SQLOCK(osq));
			osq->sq_flags |= SQ_WRITER;
			mutex_exit(SQLOCK(osq));
		} else {
			if (!(outer->sq_flags & SQ_WRITER)) {
				/*
				 * The outer could have been SQ_BLOCKED thus
				 * SQ_WRITER might not be set on the inner.
				 */
				mutex_enter(SQLOCK(osq));
				osq->sq_flags |= SQ_WRITER;
				mutex_exit(SQLOCK(osq));
			}
			ASSERT(osq->sq_flags & SQ_WRITER);
		}
		queue_writer(outer, func, q, mp);
		return;
	}
	/*
	 * We are half-way to exclusive access to the outer perimeter.
	 * Prevent any outer_enter, qwriter(OUTER), or outer_insert/remove
	 * while the inner syncqs are traversed.
	 */
	outer->sq_flags |= SQ_WRITER;
	outer->sq_count++;
	ASSERT(outer->sq_count != 0);	/* wraparound */
	/*
	 * Check if we can run the function immediately. Mark all
	 * syncqs with the writer flag to prevent new entries into
	 * put and service procedures.
	 *
	 * Set SQ_WRITER on all the inner syncqs while holding
	 * the SQLOCK on the outer syncq. This ensures that the changing
	 * of SQ_WRITER is atomic under the outer SQLOCK.
	 */
	failed = 0;
	for (sq = outer->sq_onext; sq != outer; sq = sq->sq_onext) {
		u_int maxcnt = (sq == osq) ? 1 : 0;

		mutex_enter(SQLOCK(sq));
		if (sq->sq_count > maxcnt)
			failed = 1;
		sq->sq_flags |= SQ_WRITER;
		mutex_exit(SQLOCK(sq));
	}
	if (failed) {
		/*
		 * Some other thread has a read claim on the outer perimeter.
		 * Queue the callback for deferred processing.
		 *
		 * queue_writer will set SQ_QUEUED before we drop SQ_WRITER
		 * so that other qwriter(OUTER) calls will queue their
		 * callbacks as well. queue_writer increments sq_count so we
		 * decrement to compensate for the our increment.
		 *
		 * Dropping SQ_WRITER enables the writer thread to work
		 * on this outer perimeter.
		 */
		queue_writer(outer, func, q, mp);
		/* queue_writer dropper the lock */
		mutex_enter(SQLOCK(outer));
		ASSERT(outer->sq_count > 0);
		outer->sq_count--;
		ASSERT(outer->sq_flags & SQ_WRITER);
		outer->sq_flags &= ~SQ_WRITER;
		if (outer->sq_flags & SQ_WANTWAKEUP) {
			outer->sq_flags &= ~SQ_WANTWAKEUP;
			cv_broadcast(&outer->sq_wait);
		}
		mutex_exit(SQLOCK(outer));
		return;
	} else
		mutex_exit(SQLOCK(outer));

	/* Can run it immediately */
	(*func)(q, mp);

	outer_exit(outer);
}

/*
 * Dequeue all writer callbacks from the outer perimeter and run them.
 */
static void
write_now(syncq_t *outer)
{
	mblk_t		*mp;
	queue_t		*q;
	void	(*func)();

	ASSERT(MUTEX_HELD(SQLOCK(outer)));
	ASSERT(outer->sq_outer == NULL && outer->sq_onext != NULL &&
	    outer->sq_oprev != NULL);
	while ((mp = outer->sq_head) != NULL) {
		outer->sq_head = mp->b_next;
		ASSERT(outer->sq_count != 0);
		outer->sq_count--;	/* Incremented when enqueued. */
		mutex_exit(SQLOCK(outer));
		/*
		 * Drop the message if the queue is closing.
		 * Make sure that the queue is "claimed" when the callback
		 * is run in order to satisfy various ASSERTs.
		 */
		q = mp->b_queue;
		func = (void (*)())mp->b_prev;
		mp->b_next = mp->b_prev = NULL;
		if (q->q_flag & QWCLOSE) {
			freemsg(mp);
		} else {
			claimq(q);
			(*func)(q, mp);
			releaseq(q);
		}
		mutex_enter(SQLOCK(outer));
	}
	outer->sq_flags &= ~SQ_QUEUED;
	outer->sq_tail = NULL;
	ASSERT(MUTEX_HELD(SQLOCK(outer)));
}


/*
 * Writer thread operations
 */

/*
 * Queue an outer perimeter to be processes by the qwriter_outer thread.
 */
static void
queue_writer_work(syncq_t *outer)
{
	struct writer_work	*ww;

	ASSERT(MUTEX_NOT_HELD(SQLOCK(outer)));

	ww = kmem_alloc(sizeof (struct writer_work), KM_SLEEP);
	ww->ww_outer = outer;
	ww->ww_next = NULL;
	mutex_enter(&writer_lock);
	if (writer_work == NULL) {
		writer_work = writer_tail = ww;
		cv_signal(&writer_wait);
	} else {
		writer_tail->ww_next = ww;
		writer_tail = ww;
	}
	mutex_exit(&writer_lock);
}

static void
qwriter_outer_thread(void)
{
	struct writer_work *ww;
	syncq_t	*outer;
	callb_cpr_t cprinfo;

	CALLB_CPR_INIT(&cprinfo, &writer_lock, callb_generic_cpr, "qot");
	for (;;) {
		mutex_enter(&writer_lock);
		CALLB_CPR_SAFE_BEGIN(&cprinfo);
		while ((ww = writer_work) == NULL) {
			cv_wait(&writer_wait, &writer_lock);
		}
		CALLB_CPR_SAFE_END(&cprinfo, &writer_lock);
		writer_work = ww->ww_next;
		mutex_exit(&writer_lock);
		outer = ww->ww_outer;
		kmem_free((caddr_t)ww, sizeof (struct writer_work));

		/*
		 * Note that SQ_WRITER is used on the outer perimeter
		 * to signal that a qwriter(OUTER) is either investigating
		 * running or that it is actually running a function.
		 */
		outer_enter(outer, SQ_BLOCKED|SQ_WRITER);

		/*
		 * All inner syncq are empty and have SQ_WRITER set
		 * to block entering the outer perimeter.
		 *
		 * We do not need to explicitely call write_now since
		 * outer_exit does it for us.
		 */
		outer_exit(outer);
	}
	/* NOTREACHED */
}

/* Handling of delayed messages on the inner syncq. */

/*
 * The list of messages on the inner syncq is referenced through
 * sq_head and sq_tail. The list of messages is doubly linked using
 * b_next and b_prev. The first message has a NULL b_prev and
 * the last message has a NULL b_next.
 */

/*
 * Perform delayed processing. The caller has to make sure that it is safe
 * to enter the syncq (e.g. by checking that none of the SQ_STAYAWAY bits are
 * set.)
 * Assume that the caller has NO claims on the syncq.
 *
 * drain_syncq has to terminate when one of the SQ_STAYAWAY bits gets set
 * in order to preserve qwriter(OUTER) ordering constraints.
 */
void
drain_syncq(syncq_t *sq)
{
	queue_t	*qp;
	mblk_t	*bp, *next, *prev;
	mblk_t	*bp2;
	int	do_clr;
	int	queue_unsafe;
	u_long	flags;
	u_long	type;
	void	(*func)();

	TRACE_1(TR_FAC_STREAMS_FR, TR_DRAIN_SYNCQ_START,
		"drain_syncq start:%X", sq);
	ASSERT(MUTEX_HELD(SQLOCK(sq)));
	ASSERT((sq->sq_outer == NULL && sq->sq_onext == NULL &&
		sq->sq_oprev == NULL) ||
		(sq->sq_outer != NULL && sq->sq_onext != NULL &&
		sq->sq_oprev != NULL));
	ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
	/*
	 * sq_head might be NULL since SQ_QUEUED is not reset by
	 * drain_syncq until after sq_head has been set to NULL.
	 */
	ASSERT(sq->sq_flags & SQ_QUEUED);
	flags = sq->sq_flags;
	type = sq->sq_type;
	/*
	 * Attempt to enter the inner perimeter.
	 * We set SQ_EXCL except for SQ_CIPUT to avoid message reordering.
	 * For SQ_CIPUT we set q_draining to prevent multiple threads from
	 * draining into the same queue.
	 */
	if (flags & SQ_EXCL)
		return;
	if (!(type & SQ_CIPUT)) {
		if (sq->sq_count > 0)
			return;
		flags |= SQ_EXCL;
	}
	sq->sq_flags = flags;
	sq->sq_count++;
	ASSERT(sq->sq_count != 0);	/* wraparound */
	queue_unsafe = (flags & SQ_UNSAFE);
	prev = NULL;
	/*LINTED: variable may be used before set: next*/
	for (bp = sq->sq_head; bp != NULL; bp = next) {
		ASSERT(bp->b_queue->q_syncq == sq);
		if (flags & SQ_STAYAWAY)
			break;

		next = bp->b_next;
		/* Check if we can do the drain now */
		qp = bp->b_queue;
		func = (void (*)())bp->b_prev;
		/*
		 * If we are doing concurrent drain but the function is
		 * not the put procedure it must be a qwriter(INNER)
		 * callback. In this case we attempt to upgrade to
		 * exclusive. If the upgrade fails we give up.
		 */
		if ((type & SQ_CIPUT) &&
		    (func != (void (*)())qp->q_qinfo->qi_putp)) {
			if (sq->sq_count > 1) {
				/* Can't upgrade - other threads inside */
				break;
			}
			ASSERT((flags & SQ_EXCL) == 0);
			sq->sq_flags = flags | SQ_EXCL;
		}
		if (qp->q_draining) {
			/*
			 * Already draining this queue - continue looking
			 * for messages to drain (for other queues).
			 */
			ASSERT(type & SQ_CIPUT);
			prev = bp;
			continue;
		}
		qp->q_draining = 1;	/* Protected by SQLOCK */

		/* delete bp from sq_head/sq_tail */
		if (prev == NULL) {
			/* Deleting first */
			ASSERT(sq->sq_head == bp);
			sq->sq_head = next;
		} else
			prev->b_next = next;
		if (next == NULL) {
			/* Deleting last */
			ASSERT(sq->sq_tail == bp);
			sq->sq_tail = prev;
		}
		bp->b_prev = bp->b_next = NULL;

		/*
		 * Clear QFULL in the next service procedure queue if
		 * this is the last message destined to that queue.
		 */
		do_clr = 1;
		for (bp2 = sq->sq_head; bp2; bp2 = bp2->b_next) {
			if (bp2->b_queue == qp) {
				do_clr = 0;
				break;
			}
		}
		mutex_exit(SQLOCK(sq));
		if (do_clr)
			clr_qfull(qp);

		ASSERT(bp->b_datap->db_ref != 0);

		if (queue_unsafe) {
			mutex_enter(&unsafe_driver);
			(void) (*func)(qp, bp);
			mutex_exit(&unsafe_driver);
		} else
			(void) (*func)(qp, bp);

		mutex_enter(SQLOCK(sq));
		flags = sq->sq_flags;
		/*
		 * Always clear SQ_EXCL when  CIPUT is set in order to handle
		 * qwriter(INNER).
		 */
		if (type & SQ_CIPUT) {
			ASSERT((func == (void (*)())qp->q_qinfo->qi_putp) ||
				(flags & SQ_EXCL));
			flags &= ~SQ_EXCL;
			sq->sq_flags = flags;
		}
		ASSERT(qp->q_draining);
		qp->q_draining = 0;
		/*
		 * Have to start over at sq_head since 'next' might have
		 * been drained by some other thread.
		 */
		prev = NULL;
		next = sq->sq_head;
	}
	if (sq->sq_head == NULL) {
		ASSERT(sq->sq_tail == NULL);
		flags &= ~SQ_QUEUED;
	} else
		ASSERT((flags & SQ_GOAWAY) || (type & SQ_CIPUT) ||
			sq->sq_head->b_queue->q_draining);

	ASSERT(sq->sq_head == NULL || sq->sq_tail != NULL);

	if (!(type & SQ_CIPUT))
		flags &= ~SQ_EXCL;
	ASSERT((flags & SQ_EXCL) == 0);
	if (flags & SQ_WANTWAKEUP) {
		flags &= ~SQ_WANTWAKEUP;
		cv_broadcast(&sq->sq_wait);
	}
	if (flags & SQ_WANTEXWAKEUP) {
		flags &= ~SQ_WANTEXWAKEUP;
		cv_broadcast(&sq->sq_exitwait);
	}
	sq->sq_flags = flags;
	ASSERT(sq->sq_count != 0);
	sq->sq_count--;
	TRACE_1(TR_FAC_STREAMS_FR, TR_DRAIN_SYNCQ_END,
		"drain_syncq end:%X", sq);
}

/*
 * Limit the number of threads that might attempt to drain a syncq
 * concurrently. This is needed to prevent the same thread from
 * draining a D_MTPERMOD|D_MTPUTSHARED syncq (such as IP) recursively
 * thereby running out of stack.
 */
u_long sq_max_draincnt = 10;

/*
 * Add a message to the syncq. Set SQ_QUEUED if this was the first
 * message. If func is NULL it is assumed to be the put procecedure
 * of the specified queue. When the message is drained from the syncq
 * the framework will call:
 *	(*func)(q, mp);
 */
void
fill_syncq(syncq_t *sq, queue_t *q, mblk_t *mp, void (*func)())
{
	u_long		flags;

	ASSERT(MUTEX_HELD(SQLOCK(sq)));
	ASSERT(q->q_syncq == sq);
	ASSERT((sq->sq_outer == NULL && sq->sq_onext == NULL &&
		sq->sq_oprev == NULL) ||
		(sq->sq_outer != NULL && sq->sq_onext != NULL &&
		sq->sq_oprev != NULL));

	TRACE_4(TR_FAC_STREAMS_FR, TR_FILL_SYNCQ,
		"fill_syncq:%X %X %X %X", sq, q, mp, func);
	/*
	 * Set QFULL in next service procedure queue if there are already
	 * two or more messages on the syncq.
	 */
	if (sq->sq_head != sq->sq_tail) {
		/*
		 * Have to drop SQLOCK across set_qfull do avoid locking
		 * hirarchy violation. This is ok as long as we
		 *	- increment sq_count to prevent a close from
		 *	  blowing away the qeueue
		 *	- check if it is possible to drain the syncq after
		 *	  the message has been put on the syncq (since
		 *	  SQ_GOAWAY could have been reset while the lock
		 *	  was dropped.)
		 */
		sq->sq_count++;
		ASSERT(sq->sq_count != 0);	/* Wraparound */
		mutex_exit(SQLOCK(sq));

		set_qfull(q);

		mutex_enter(SQLOCK(sq));
		ASSERT(sq->sq_count > 0);
		sq->sq_count--;
		if (sq->sq_flags & SQ_WANTWAKEUP) {
			cv_broadcast(&sq->sq_wait);
			sq->sq_flags &= ~SQ_WANTWAKEUP;
		}
	}
	if (func == NULL)
		func = (void (*)())q->q_qinfo->qi_putp;
	mp->b_queue = q;
	mp->b_prev = (mblk_t *)func;
	mp->b_next = NULL;
	flags = sq->sq_flags;
	if (sq->sq_head == NULL) {
		ASSERT(sq->sq_tail == NULL);
		sq->sq_head = sq->sq_tail = mp;
		sq->sq_flags = flags | SQ_QUEUED;
	} else {
		ASSERT(sq->sq_tail != NULL);
		ASSERT(sq->sq_tail->b_next == NULL);
		sq->sq_tail->b_next = mp;
		sq->sq_tail = mp;
		ASSERT(flags & SQ_QUEUED);
	}
	if (!(flags & (SQ_STAYAWAY|SQ_EXCL)) &&
	    (sq->sq_count < sq_max_draincnt))
		drain_syncq(sq);
}

/*
 * Remove all messages from a syncq (if qp is NULL) or remove all messages
 * that would be put into qp by drain_syncq.
 * Used when deleting the syncq (qp == NULL) or when detaching
 * a queue (qp != NULL).
 */
void
flush_syncq(syncq_t *sq, queue_t *qp)
{
	mblk_t		*bp, *next, *prev;

	/* Quick check if any work */
	if (sq->sq_head == NULL)
		return;

	mutex_enter(SQLOCK(sq));
	/*
	 * Walk sq_head and remove all mblks that
	 *	- match qp if qp is set
	 *	- all if qp is not set
	 */
	prev = NULL;
	/* LINTED 'next' may be used before set */
	for (bp = sq->sq_head; bp != NULL; bp = next) {
		next = bp->b_next;
		ASSERT(bp->b_queue && bp->b_queue->q_syncq == sq);
		if (qp == NULL || qp == bp->b_queue) {
			/* delete bp from sq_head/sq_tail */
			if (prev == NULL) {
				/* Deleting first */
				ASSERT(sq->sq_head == bp);
				sq->sq_head = next;
			} else
				prev->b_next = next;
			if (next == NULL) {
				/* Deleting last */
				ASSERT(sq->sq_tail == bp);
				sq->sq_tail = prev;
			}
			bp->b_prev = bp->b_next = NULL;
			/* Drop SQLOCK across clr_qfull */
			mutex_exit(SQLOCK(sq));

			/*
			 * We avoid doing the test that drain_syncq does and
			 * unconditionally clear qfull for every flushed
			 * message. Since flush_syncq is only called during
			 * close this should not be a problem.
			 */
			clr_qfull(bp->b_queue);
			mutex_enter(SQLOCK(sq));
			freemsg_flush(bp);
		} else
			prev = bp;
	}
	if (sq->sq_head == NULL) {
		ASSERT(sq->sq_tail == NULL);
		sq->sq_flags &= ~SQ_QUEUED;
	}
	ASSERT(sq->sq_head == NULL || sq->sq_tail != NULL);
	mutex_exit(SQLOCK(sq));
}

/*
 * Propage messages from a syncq to the next syncq.
 * Propagate all messages on the syncq that are associated with the specified
 * queue.
 *
 * Assumes that the stream is strlock()'ed.
 */
static void
propagate_syncq(queue_t *qp)
{
	mblk_t		*bp, *next, *prev;
	void		(*func)();
	syncq_t		*sq;
	queue_t		*nqp;
	syncq_t		*nsq;

	ASSERT(qp->q_next);
	sq = qp->q_syncq;
	nqp = qp->q_next;
	nsq = nqp->q_syncq;
	ASSERT(MUTEX_HELD(SQLOCK(sq)));
	ASSERT(MUTEX_HELD(SQLOCK(nsq)));

	func = (void (*)())nqp->q_qinfo->qi_putp;

	prev = NULL;
	/* LINTED 'next' may be used before set */
	for (bp = sq->sq_head; bp != NULL; bp = next) {
		next = bp->b_next;
		ASSERT(bp->b_queue && bp->b_queue->q_syncq == sq);
		if (qp == bp->b_queue) {
			/* delete bp from sq_head/sq_tail */
			if (prev == NULL) {
				/* Deleting first */
				ASSERT(sq->sq_head == bp);
				sq->sq_head = next;
			} else
				prev->b_next = next;
			if (next == NULL) {
				/* Deleting last */
				ASSERT(sq->sq_tail == bp);
				sq->sq_tail = prev;
			}
			/* Free messages not destined for the put procedure */
			if ((int (*)())bp->b_prev != qp->q_qinfo->qi_putp) {
				bp->b_prev = bp->b_next = NULL;
				freemsg_flush(bp);
				continue;
			}
			/* Insert in nsq */
			bp->b_queue = nqp;
			bp->b_prev = (mblk_t *)func;
			bp->b_next = NULL;
			if (nsq->sq_head == NULL) {
				ASSERT(nsq->sq_tail == NULL);
				nsq->sq_head = nsq->sq_tail = bp;
				nsq->sq_flags |= SQ_QUEUED;
			} else {
				ASSERT(nsq->sq_tail != NULL);
				ASSERT(nsq->sq_tail->b_next == NULL);
				nsq->sq_tail->b_next = bp;
				nsq->sq_tail = bp;
				ASSERT(nsq->sq_flags & SQ_QUEUED);
			}
		} else
			prev = bp;
	}
	if (sq->sq_head == NULL) {
		ASSERT(sq->sq_tail == NULL);
		sq->sq_flags &= ~SQ_QUEUED;
	}
	ASSERT(sq->sq_head == NULL || sq->sq_tail != NULL);
}

/*
 * Try and upgrade to exclusive access at the inner perimeter. If this can
 * not be done without blocking then request will be queued on the syncq
 * and drain_syncq will run it later.
 *
 * This routine can only be called from put or service procedures plus
 * asynchronous callback routines that have properly entered to
 * queue (with entersq.) Thus qwriter_inner assumes the caller has one claim
 * on the syncq associated with q.
 */
void
qwriter_inner(queue_t *q, mblk_t *mp, void (*func)())
{
	syncq_t	*sq;
	u_long	count, flags;

	sq = q->q_syncq;

	mutex_enter(SQLOCK(sq));
	ASSERT(sq->sq_count >= 1);
	ASSERT(sq->sq_type & (SQ_CIPUT|SQ_CISVC));

	count = sq->sq_count;
	flags = sq->sq_flags;
	if (count == 1) {
		/*
		 * Can upgrade. This case also handles nested qwriter calls
		 * (when the qwriter callback function calls qwriter). In that
		 * case SQ_EXCL is already set.
		 */
		sq->sq_flags = flags | SQ_EXCL;
		mutex_exit(SQLOCK(sq));
		(*func)(q, mp);
		/*
		 * Assumes that leavesq, putnext, and drain_syncq will reset
		 * SQ_EXCL for SQ_CIPUT/SQ_CISVC queues. We leave SQ_EXCL on
		 * until putnext, leavesq, or drain_syncq drops it.
		 * That way we handle nested qwriter(INNER) without dropping
		 * SQ_EXCL until the outermost qwriter callback routine is
		 * done.
		 */
		return;
	}
	fill_syncq(sq, q, mp, func);
	mutex_exit(SQLOCK(sq));
}

/*
 * Synchronous callback support functions
 */

/*
 * Allocate a callback parameter structure.
 * Assumes that caller initializes the flags and the id.
 */
callbparams_t *
callbparams_alloc(syncq_t *sq, void (*fun)(), char *arg)
{
	callbparams_t *params;

	ASSERT(MUTEX_HELD(SQLOCK(sq)));

	mutex_exit(SQLOCK(sq));
	params = kmem_cache_alloc(callbparams_cache, KM_SLEEP);
	params->sq = sq;
	params->fun = fun;
	params->arg = arg;
	mutex_enter(SQLOCK(sq));
	params->next = sq->sq_callbpend;
	sq->sq_callbpend = params;
	return (params);
}

void
callbparams_free(syncq_t *sq, callbparams_t *params)
{
	callbparams_t **pp, *p;

	ASSERT(MUTEX_HELD(SQLOCK(sq)));

	for (pp = &sq->sq_callbpend; (p = *pp) != NULL; pp = &p->next) {
		if (p == params) {
			*pp = p->next;
			kmem_cache_free(callbparams_cache, p);
			return;
		}
	}
	printf("callbparams_free: not found\n");
}

void
callbparams_free_id(syncq_t *sq, int id, long flag)
{
	callbparams_t **pp, *p;

	ASSERT(MUTEX_HELD(SQLOCK(sq)));

	for (pp = &sq->sq_callbpend; (p = *pp) != NULL; pp = &p->next) {
		if (p->id == id && p->flags == flag) {
			*pp = p->next;
			kmem_cache_free(callbparams_cache, p);
			return;
		}
	}
	printf("callbparams_free_id: not found\n");
}

/*
 * Callback wrapper function used by once-only callbacks that can be
 * cancelled (qtimeout and qbufcall)
 * Contains inline version of entersq(sq, SQ_CALLBACK) that can be
 * cancelled by the qun* functions.
 */
void
qcallbwrapper(callbparams_t *params)
{
	syncq_t	*sq;
	u_long	count;
	u_long	flags, waitflags, type;

	sq = params->sq;
	mutex_enter(SQLOCK(sq));
	flags = sq->sq_flags;
	count = sq->sq_count;
	type = sq->sq_type;
	/* Can not handle exlusive entry at outer perimeter */
	ASSERT(type & SQ_COCB);
	ASSERT(!(type & SQ_UNSAFE));
	if (type & SQ_CICB)
		waitflags = SQ_STAYAWAY | SQ_QUEUED;
	else
		waitflags = SQ_STAYAWAY | SQ_QUEUED | SQ_EXCL;

	while ((flags & waitflags) || (!(type & SQ_CICB) && count != 0)) {
		ASSERT(UNSAFE_DRIVER_LOCK_NOT_HELD());
		if ((sq->sq_callbflags & params->flags) &&
		    (sq->sq_cancelid == params->id)) {
			/* timeout has been cancelled */
			sq->sq_callbflags |= SQ_CALLB_BYPASSED;
			callbparams_free(sq, params);
			mutex_exit(SQLOCK(sq));
			return;
		}
		sq->sq_flags = flags | SQ_WANTWAKEUP;
		cv_wait(&sq->sq_wait, SQLOCK(sq));
		count = sq->sq_count;
		flags = sq->sq_flags;
	}
	sq->sq_count = count + 1;
	ASSERT(sq->sq_count != 0);	/* Wraparound */
	if (!(type & SQ_CICB)) {
		ASSERT(count == 0);
		sq->sq_flags = flags | SQ_EXCL;
	}
	mutex_exit(SQLOCK(sq));

	(*params->fun)(params->arg);

	/*
	 * XXX we drop the lock only for leavesq to re-acquire it.
	 * Inline leavesq for better performance.
	 */
	mutex_enter(SQLOCK(sq));
	callbparams_free(sq, params);
	mutex_exit(SQLOCK(sq));
	leavesq(sq, SQ_CALLBACK);
}

/* ARGSUSED */
void
putnext_tail(syncq_t *sq, mblk_t *mp, u_long flags, u_long count)
{
	if (flags & SQ_UNSAFE)
		mutex_exit(&unsafe_driver);
	if (flags & SQ_WANTWAKEUP) {
		cv_broadcast(&sq->sq_wait);
		flags &= ~SQ_WANTWAKEUP;
	}
	if (flags & SQ_WANTEXWAKEUP) {
		cv_broadcast(&sq->sq_exitwait);
		flags &= ~SQ_WANTEXWAKEUP;
	}
	if ((flags & SQ_QUEUED) && !(flags & SQ_STAYAWAY)) {
		ASSERT(count != 0);
		sq->sq_count = count - 1;
		ASSERT(flags & (SQ_EXCL|SQ_CIPUT));
		sq->sq_flags = flags & ~SQ_EXCL;
		drain_syncq(sq);
		mutex_exit(SQLOCK(sq));
		TRACE_3(TR_FAC_STREAMS_FR, TR_PUTNEXT_END,
			"putnext_end:(%X, %X, %X) done", NULL, mp, sq);
		return;
	}
	ASSERT(count != 0);
	sq->sq_count = count - 1;
	ASSERT(flags & (SQ_EXCL|SQ_CIPUT));
	sq->sq_flags = flags & ~SQ_EXCL;
	mutex_exit(SQLOCK(sq));
	TRACE_3(TR_FAC_STREAMS_FR, TR_PUTNEXT_END,
		"putnext_end:(%X, %X, %X) done", NULL, mp, sq);
}

void
putnext_from_unsafe(queue_t *qp, mblk_t *mp)
{
	ASSERT(UNSAFE_DRIVER_LOCK_HELD());
	/* Do the claimstr before dropping the unsafe driver lock */
	claimstr(qp);
	mutex_exit(&unsafe_driver);
	put(qp->q_next, mp);
	releasestr(qp);
	mutex_enter(&unsafe_driver);
	TRACE_3(TR_FAC_STREAMS_FR, TR_PUTNEXT_END,
		"putnext_end:(%X, %X, %X) done", qp, mp, NULL);
}

/*
 * Return non-zero when failed entering the perimeter.
 */
int
putnext_to_unsafe(
	queue_t *qp,
	mblk_t *mp,
	u_long flags,
	u_long count,
	syncq_t *sq)
{
	sq->sq_count = count + 1;
	ASSERT(sq->sq_count != 0);	/* Wraparound */
	sq->sq_flags = flags | SQ_EXCL;
	mutex_exit(SQLOCK(sq));
	mutex_enter(&unsafe_driver);
	/*
	 * Check that nothing changed e.g. due to a pending
	 * close. This check is needed since qdetach only
	 * uses the unsafe_driver lock when closing an unsafe
	 * queue (which is necessary to support sleep in
	 * unsafe close routines.) Thus it is possible to
	 * sucessfully "enter" the syncq and then block on
	 * the unsafe_driver lock until the close routine
	 * has completed.
	 */
	mutex_enter(SQLOCK(sq));
	flags = sq->sq_flags;
	if (flags & SQ_STAYAWAY) {
		/* XXX re-ordering of messages? */
		fill_syncq(sq, qp, mp, NULL);
		mutex_exit(SQLOCK(sq));
		leavesq(sq, SQ_PUT);
		TRACE_3(TR_FAC_STREAMS_FR, TR_PUTNEXT_END,
			"putnext_end:(%X, %X, %X) done", qp, mp, sq);
		return (-1);
	}
	mutex_exit(SQLOCK(sq));
	return (0);
}

void
set_qend(queue_t *q)
{
	mutex_enter(QLOCK(q));
	if (!O_SAMESTR(q))
		q->q_flag |= QEND;
	else
		q->q_flag &= ~QEND;
	mutex_exit(QLOCK(q));
	q = OTHERQ(q);
	mutex_enter(QLOCK(q));
	if (!O_SAMESTR(q))
		q->q_flag |= QEND;
	else
		q->q_flag &= ~QEND;
	mutex_exit(QLOCK(q));
}

void
set_qfull(queue_t *q)
{
	q = q->q_nfsrv;
	mutex_enter(QLOCK(q));
	q->q_flag |= QFULL;
	mutex_exit(QLOCK(q));
}


void
clr_qfull(queue_t *q)
{
	queue_t	*oq = q;

	q = q->q_nfsrv;
	/* Fast check if there is any work to do before getting the lock. */
	if ((q->q_flag & (QFULL|QWANTW)) == 0)
		return;

	/*
	 * Do not reset QFULL (and backenable) if the q_count is the reason
	 * for QFULL being set.
	 */
	mutex_enter(QLOCK(q));
	if (q->q_count < q->q_hiwat) {
		q->q_flag &= ~QFULL;
		if ((q->q_flag & QWANTW) &&
		    (q->q_count < q->q_lowat || q->q_lowat == 0)) {
			q->q_flag &= ~QWANTW;
			mutex_exit(QLOCK(q));
			backenable(oq, 0);
		} else
			mutex_exit(QLOCK(q));
	} else
		mutex_exit(QLOCK(q));
}

#undef QLOCK

/*
 * replacement for QLOCK macro for those that can't use it.
 */
kmutex_t *
QLOCK(queue_t *q)
{
	return (&(q)->q_lock);
}

/*
 * This is a general comment describing how the performance
 * enhancement to STREAMS by caching pointers at the queue
 * level for a queue's next service procedure forward, "q_nfsrv",
 * (canput,canputnext,etc.) and next service procedure
 * backward, "q_nbsrv", (backeneble) works.
 *
 * Basically, we detemine at the time a queue is inserted
 * where its next forward service procedure and backwward
 * service procedures are and then update them.
 *
 * The q_nfsrv pointer is set as per the way canput() and canputnext() were
 * originally coded.  If the queue to be inserted has a service procedure
 * then the q_nfsrv pointer points to itself.  If queue to be inserted
 * does not have a service procedure, then the q_nfsrv pointer points
 * to the next queue forward that has a service procedure.  If the queue
 * at the logical end of the stream(driver for write side, stream head for
 * the read side) does not have a service procedure then the q_nfsrv
 * pointer for the end points to itself.
 *
 * Regarding the q_nbsrv pointer, it is set as per the way backenable()
 * was originally coded.  Unlike canput and canputnext, backenabling does
 * not concern itself with the fact that the queue in question might have
 * a service procedure.  It begins its search at the queue immediately
 * behind it.  Since backenable may accept a NULL pointer (i.e. there is
 * no queue to backenable), the queues at the beginning of a stream
 * (the stream head for the write side, the driver for the read side)
 * are set to NULL.
 *
 * Pipes, fifos and connld have their q_nbsrv and q_nfsrv
 * pointers initialized when they are created.
 */

/*
 * Insert a module/driver.  Set the forward service procedure pointer.
 */
void
set_nfsrv_ptr(
	queue_t  *rnew,		/* read queue pointer to new module */
	queue_t  *wnew,		/* write queue pointer to new module */
	queue_t  *strhd_rqp,	/* read queue pointer to the stream head */
	queue_t  *strhd_wqp)	/* write queue pointer to the stream head */
{
	queue_t *qp;

	if (strhd_wqp->q_next == NULL) {
		/* Insert the driver, initialize the driver and stream head */
		wnew->q_nfsrv = wnew;
		if (rnew->q_qinfo->qi_srvp)
			rnew->q_nfsrv = rnew;
		else
			rnew->q_nfsrv = strhd_rqp;
		strhd_rqp->q_nfsrv = strhd_rqp;
		strhd_wqp->q_nfsrv = strhd_wqp;
	} else {
		/* set up write side q_nfsrv pointer */
		if (wnew->q_qinfo->qi_srvp) {
			wnew->q_nfsrv = wnew;
		} else {
			wnew->q_nfsrv = strhd_wqp->q_next->q_nfsrv;
		}

		/* set up read side q_nfsrv pointer */
		if (rnew->q_qinfo->qi_srvp) {
			qp = RD(strhd_wqp->q_next);
			while (qp && qp->q_nfsrv == strhd_rqp) {
				qp->q_nfsrv = rnew;
				qp = backq(qp);
			}
			rnew->q_nfsrv = rnew;
		} else
			rnew->q_nfsrv = strhd_rqp;
	}
}

/*
 * Insert a module/driver.  Set the bacward service procedure pointer.
 */
void
set_nbsrv_ptr(
	queue_t  *rnew,		/* read queue pointer to new module */
	queue_t  *wnew,		/* write queue pointer to new module */
	queue_t  *strhd_rqp,	/* read queue pointer to the stream head */
	queue_t  *strhd_wqp)	/* write queue pointer to the stream head */
{
	queue_t *qp;

	if (strhd_wqp->q_next == NULL) {
		/* Insert the driver, initialize the driver and stream head */
		wnew->q_nbsrv = strhd_wqp;
		if (rnew->q_qinfo->qi_srvp) {
			strhd_rqp->q_nbsrv = rnew;
		} else {
			strhd_rqp->q_nbsrv = NULL;
		}
		strhd_wqp->q_nbsrv = NULL;
		rnew->q_nbsrv = NULL;
	} else {
		/* set up write side q_nbsrv pointer */
		wnew->q_nbsrv = strhd_wqp->q_next->q_nbsrv;
		if (wnew->q_qinfo->qi_srvp) {
			qp = strhd_wqp->q_next;
			while (qp && qp->q_nbsrv == strhd_wqp) {
				qp->q_nbsrv = wnew;
				qp = qp->q_next;
			}
		}

		/* set up read side q_nbsrv pointer */
		rnew->q_nbsrv = strhd_rqp->q_nbsrv;
		if (rnew->q_qinfo->qi_srvp)
			strhd_rqp->q_nbsrv = rnew;
	}
}

/*
 * Remove a module/driver.  Reset the forward service procedure pointer.
 */
void
reset_nfsrv_ptr(queue_t *rqp, queue_t *wqp, stdata_t *stream)
{
	queue_t *tmp_qp;

	/* reset the write side q_nfsrv pointer: nothing to do here */

	/* reset the read side q_nfsrv pointer */
	if (rqp->q_qinfo->qi_srvp) {
		if (wqp->q_next) {	/* non-driver case */
			tmp_qp = RD(wqp->q_next);
			while (tmp_qp && tmp_qp->q_nfsrv == rqp) {
				tmp_qp->q_nfsrv = RD(stream->sd_wrq);
				tmp_qp = backq(tmp_qp);
			}
		}
	}
}

/*
 * Remove a module/driver.  Reset the backward service procedure pointer.
 */
void
reset_nbsrv_ptr(queue_t *rqp, queue_t *wqp, stdata_t *stream)
{
	queue_t *tmp_qp;

	/* reset the write side q_nbsrv pointer */
	if (wqp->q_qinfo->qi_srvp) {
		tmp_qp = wqp->q_next;
		while (tmp_qp && tmp_qp->q_nbsrv == wqp) {
			tmp_qp->q_nbsrv = stream->sd_wrq;
			tmp_qp = tmp_qp->q_next;
		}
	}
	/* reset the read side q_nbsrv pointer */
	RD(stream->sd_wrq)->q_nbsrv = rqp->q_nbsrv;
}

/*
 * This creates "cpus" worth of stream threads
 * called from main after all cpus are detected
 * and from main for initial stream init.
 * mp_strinit() is called single threaded.
 */
void
mp_strinit(void)
{
	static last_ncpu = 0;

	while (last_ncpu < ncpus) {
		bkgrnd_thread = thread_create(NULL, 2 * NBPG, background,
			0, 0, &p0, TS_RUN, 60);
		if (bkgrnd_thread == (kthread_id_t)NULL)
			cmn_err(CE_PANIC, "strinit:thread_create() failed\n");
		liberator = thread_create(NULL, 2 * NBPG, freebs, 0, 0, &p0,
			TS_RUN, 60);
		if (liberator == (kthread_id_t)NULL)
			cmn_err(CE_PANIC, "strinit:thread_create() failed\n");
		writer_thread = thread_create(NULL, 2 * NBPG,
			qwriter_outer_thread, 0, 0, &p0, TS_RUN, 60);
		if (writer_thread == (kthread_id_t)NULL)
			cmn_err(CE_PANIC, "strinit:thread_create() failed\n");
		last_ncpu++;
	}
}

/*
 * This routine should be called after all stream geometry changes to update
 * the streamhead cached struio() rd/wr queue pointers. Note must be called
 * with the streamlock()ed.
 *
 * Note: only enables Synchronous STREAMS for a side of a Stream which has
 *	 an explict synchronous barrier module queue. That is, a queue that
 *	 has specified a struio() type.
 */
static void
strsetuio(stp)
	stdata_t *stp;
{
	queue_t *wrq;

	if (stp->sd_flag & STPLEX) {
		/*
		 * Not stremahead, but a mux, so no Synchronous STREAMS.
		 */
		stp->sd_struiowrq = NULL;
		stp->sd_struiordq = NULL;
		return;
	}
	/*
	 * Scan the write queue(s) while synchronous
	 * until we find a qinfo uio type specified.
	 */
	wrq = stp->sd_wrq->q_next;
	while (wrq) {
		if (wrq->q_struiot == STRUIOT_NONE) {
			wrq = 0;
			break;
		}
		if (wrq->q_struiot != STRUIOT_DONTCARE)
			break;
		if (! SAMESTR(wrq)) {
			wrq = 0;
			break;
		}
		wrq = wrq->q_next;
	}
	stp->sd_struiowrq = wrq;
	/*
	 * Scan the read queue(s) while synchronous
	 * until we find a qinfo uio type specified.
	 */
	wrq = stp->sd_wrq->q_next;
	while (wrq) {
		if (RD(wrq)->q_struiot == STRUIOT_NONE) {
			wrq = 0;
			break;
		}
		if (RD(wrq)->q_struiot != STRUIOT_DONTCARE)
			break;
		if (! SAMESTR(wrq)) {
			wrq = 0;
			break;
		}
		wrq = wrq->q_next;
	}
	stp->sd_struiordq = wrq ? RD(wrq) : 0;
}

/*
 * pass_wput, unblocks the passthru queues, so that
 * messages can arrive at muxs lower read queue, before
 * I_LINK/I_UNLINK is acked/nacked.
 */
static void
pass_wput(queue_t *q, mblk_t *mp)
{
	syncq_t *sq;

	sq = RD(q)->q_syncq;
	if (sq->sq_flags & SQ_BLOCKED)
		unblocksq(sq, SQ_BLOCKED, 0);
	putnext(q, mp);
}

/*
 * Set up queues for the link/unlink.
 * Create a new queue and block it and then insert it
 * below the stream head on the lower stream.
 * This prevents any messages from arriving during the setq
 * as well as while the mux is processing the LINK/I_UNLINK.
 * The blocked passq is unblocked once the LINK/I_UNLINK has
 * been acked or nacked or if a message is generated and sent
 * down muxs write put procedure.
 * see pass_wput().
 */
static queue_t *
link_addpassthru(stdata_t *stpdown)
{
	queue_t *passq;

	passq = allocq();
	STREAM(passq) = STREAM(WR(passq)) = stpdown;
	/*
	 * We must make sure pushcnt is accurate, so that build_sqlist()
	 * computes the correct number of syncq's on the stream.
	 */
	mutex_enter(&STREAM(passq)->sd_lock);
	stpdown->sd_pushcnt++;
	mutex_exit(&STREAM(passq)->sd_lock);
	/* setq might sleep in allocator - avoid holding locks. */
	setq(passq, &passthru_rinit, &passthru_winit, NULL, NULL,
		QPERQ, SQ_CI|SQ_CO);
	claimq(passq);
	blocksq(passq->q_syncq, SQ_BLOCKED, 1);
	insertq(STREAM(passq), passq, 0);
	releaseq(passq);
	return (passq);
}
/*
 * Let messages flow up into the mux by removing
 * the passq.
 */
static void
link_rempassthru(queue_t *passq)
{
	claimq(passq);
	removeq(passq, 0);
	mutex_enter(&STREAM(passq)->sd_lock);
	STREAM(passq)->sd_pushcnt--;
	mutex_exit(&STREAM(passq)->sd_lock);
	releaseq(passq);
	freeq(passq);
}

/*
 * wait for an event with optional timeout and optional return if
 * a signal is sent to the thread
 * tim:  -1 : no timeout
 *       otherwise the value is relative time in milliseconds to wait
 * nosig:  if 0 then signals will be ignored, otherwise signals
 *       will terminate wait
 * returns 1 on success, 0 if signal was encountered, -1 if timeout
 * was reached.
 */
int
str_cv_wait(kcondvar_t *cvp, kmutex_t *mp, long tim, int nosigs)
{
	int ret;
	unsigned long now;
	extern void time_to_wait(unsigned long *, long);

	if (tim < 0) {
		if (nosigs) {
			cv_wait(cvp, mp);
			ret = 1;
		} else {
			ret = cv_wait_sig(cvp, mp);
		}
	} else if (tim > 0) {
		/*
		 * convert milliseconds to clock ticks
		 */
		tim = (tim / 1000) * HZ + ((((tim % 1000) * HZ) + 999) / 1000);
		time_to_wait(&now, tim);
		if (nosigs) {
			ret = cv_timedwait(cvp, mp, now);
		} else {
			ret = cv_timedwait_sig(cvp, mp, now);
		}
	} else {
		ret = -1;
	}
	return (ret);
}
