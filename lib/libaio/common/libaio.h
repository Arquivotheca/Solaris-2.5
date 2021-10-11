#include	<sys/types.h>
#include	<sys/lwp.h>
#include	<synch.h>
#include	<asynch.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<setjmp.h>
#include	<siginfo.h>

#define _REENTRANT

#ifdef DEBUG
extern int assfail(char *, char *, int);
#define	ASSERT(EX) ((void)((EX) || assfail(#EX, __FILE__, __LINE__)))
#else
#define	ASSERT(EX)
#endif

#undef MUTEX_HELD
#define	MUTEX_HELD(x)	LOCK_HELD(&(((lwp_mutex_t *)(x))->mutex_lockw))

#define	SIGAIOCANCEL	SIGPROF	/* special aio cancelation signal */

typedef struct aio_args {
	int 		fd;
	caddr_t		buf;
	int		bufsz;
	int		offset;
} aio_args_t;

/*
 * size of aio_req should be power of 2. this helps to improve the
 * effectiveness of the hashing function.
 */
typedef struct aio_req {
	/*
	 * fields protected by _aio_mutex lock.
	 */
	struct aio_req *req_link;	/* hash chain link */
	struct aio_req *req_next;	/* request/done queue link */
	/*
	 * fields require no locking.
	 */
	union lock_un {
		char	pad[24];
		mutex_t lock;		/* protects state and worker's req q */
	} req_mutex;
	int			req_state;	/* AIO_REQ_QUEUED, ... */
	struct aio_worker	*req_worker;	/* associate req. with worker */
	aio_result_t		*req_resultp;	/* address of result buffer */
	int			req_op;		/* read or write */
	aio_args_t 		req_args;	/* arglist */
} aio_req_t;
#define	req_lock req_mutex.lock

/* values for aios_state */

#define	AIO_REQ_QUEUED		1
#define	AIO_REQ_INPROGRESS	2
#define	AIO_REQ_CANCELLED	3
#define	AIO_REQ_DONE 		4
#define	AIO_REQ_FREE		5

struct aio_worker {
	char pad[1000];			/* make aio_worker look like a thread */
	/*
	 * fields protected by _aio_mutex lock
	 */
	struct aio_worker *work_forw;	/* forward link in list of workers */
	struct aio_worker *work_backw;	/* backwards link in list of workers */
	/*
	 * fields require no locking.
	 */
	caddr_t work_stk;		/* worker's stack base */
	lwpid_t work_lid;		/* worker's LWP id */
	mutex_t work_qlock1;		/* lock for work queue one */
	struct aio_req *work_head1;	/* head of work request queue one */
	struct aio_req *work_tail1;	/* tail of work request queue one */
	struct aio_req *work_next1;	/* work queue one's next pointer */
	struct aio_req *work_prev1;	/* last request done from queue one */
	int work_cnt1;			/* length of work queue one */
	int work_done1;			/* number of requests done */
	int work_minload1;		/* min length of queue */
	struct aio_req *work_req;	/* active work request */
	int work_idleflg;		/* when set, worker is idle */
	cond_t work_idle_cv;		/* place to sleep when idle */
	mutex_t work_lock;		/* protects work flags */
	sigjmp_buf work_jmp_buf;	/* cancellation point */
	char work_cancel_flg;		/* flag set when at cancellation pt */
};

extern void aiocancel_all(int);
extern void __sigcancelhndlr(int, siginfo_t *, ucontext_t *);
extern int _aio_create_worker(struct aio_req *, int);
extern int _aio_lwp_exec();
extern void _aio_do_request(struct aio_worker *);
extern void _aio_cancel_on(struct aio_worker *);
extern void _aio_cancel_off(struct aio_worker *);
extern int _aio_work_done(struct aio_worker *);
extern int _aio_req_add(struct aio_req *);
extern struct aio_req * _aio_req_get(struct aio_worker *);
extern aio_result_t * _aio_req_done(void);
extern void __aiosendsig(void);

extern struct aio_worker *_nextworker;	/* worker chosen for next request */
extern struct aio_worker *_workers;	/* list of all workers */
extern int __aiostksz;			/* stack size for workers */
extern int _aio_in_use;			/* AIO is initialized */
extern lwp_mutex_t __aio_mutex;		/* global aio lock that's SIGIO-safe */
extern int _max_workers;		/* max number of workers permitted */
extern int _min_workers;		/* min number of workers */
extern sigset_t _worker_set;		/* worker's signal mask */
extern int _aio_outstand_cnt;		/* number of outstanding requests */
extern int _aio_worker_cnt;		/* number of AIO workers */
extern struct sigaction _origact2;	/* original action for SIGWAITING */
extern ucontext_t _aio_ucontext;	/* ucontext of defered signal */
extern int _sigio_enabled;		/* when set, send SIGIO signal */
extern int __sigio_pending;		/* count of pending SIGIO signals */
extern int __sigio_masked;		/* when set, SIGIO is masked */
extern int __sigio_maskedcnt;		/* count number times bit mask is set */
extern int __pid;			/* process's PID */
