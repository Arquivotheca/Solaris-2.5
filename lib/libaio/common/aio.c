#ident	"@(#)aio.c	1.18	95/09/15 SMI"

/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#include	<sys/asm_linkage.h>
#include	<sys/types.h>
#include	<sys/param.h>
#include	<sys/errno.h>
#include	<sys/procset.h>
#include	<sys/signal.h>
#include	<sys/siginfo.h>
#include	<sys/stat.h>
#include	<sys/time.h>
#include	<sys/ucontext.h>
#include	<unistd.h>
#include	<signal.h>
#include	<fcntl.h>
#include	<errno.h>
#include	<limits.h>
#include	<libaio.h>
#include	<stdio.h>

int _aiorw(int, caddr_t, int, off_t, int, aio_result_t *, int);
int _aio_hash_insert(aio_result_t *, struct aio_req *);
aio_req_t *_aio_hash_del(aio_result_t *);
aio_req_t *_aio_req_alloc(void);
void _aio_req_free(struct aio_req *);
int _aio_req_add(struct aio_req *);
aio_req_t *_aio_req_get(struct aio_worker *);
aio_result_t * _aio_req_done(void);
void _aio_req_delete(struct aio_req *);
void _aio_lock(void);
void _aio_unlock(void);

int _pagesize;

#define	AIOREQSZ		(sizeof (struct aio_req))
#define	AIOCLICKS		((_pagesize)/AIOREQSZ)
#define	HASHSZ			256	/* power of 2 */
#define	AIOHASH(resultp)	(((unsigned)(resultp) >> 3) & (HASHSZ-1))

/*
 * switch for kernel async I/O
 */
int _kaio_ok = 0;			/* 0 = disabled, 1 = on, -1 = error */

int _aioreqsize = AIOREQSZ;

#ifdef DEBUG
int *_donecnt;				/* per worker AIO done count */
int *_idlecnt;				/* per worker idle count */
int *_qfullcnt;				/* per worker full q count */
int *_firstqcnt;			/* num times queue one is used */
int *_newworker;			/* num times new worker is created */
int _clogged = 0;			/* num times all queues are locked */
int _qlocked = 0;			/* num times submitter finds q locked */
int _aio_submitcnt = 0;
int _aio_submitcnt2 = 0;
int _submitcnt = 0;
int _avesubmitcnt = 0;
int _aiowaitcnt = 0;
int _startaiowaitcnt = 1;
int _avedone = 0;
int _new_workers = 0;
#endif

struct aio_worker *_workers;		/* circular list of AIO workers */
struct aio_worker *_nextworker;		/* next worker in list of workers */
struct aio_worker *_lastworker;		/* last worker to have a full q */

struct aio_req *_aio_done_tail;		/* list of done requests */
struct aio_req *_aio_done_head;

lwp_mutex_t __aio_initlock;		/* makes aio initialization  atomic */
lwp_mutex_t __aio_mutex;		/* protects counts, and linked lists */
lwp_mutex_t __aio_cachefillock;		/* single-thread aio cache filling */
lwp_cond_t __aio_cachefillcv;		/* sleep cv for cache filling */

int __aiostksz;				/* aio worker's stack size */
int __aio_cachefilling = 0;		/* set when aio cache is filling */
int __sigio_masked = 0;			/* bit mask for SIGIO signal */
int __sigio_maskedcnt = 0;		/* mask count for SIGIO signal */
int __pid;
static int hashsz = 0;
static struct aio_req **_aio_hash;
static struct aio_req *_aio_freelist;
static int _aio_freelist_cnt;

static struct sigaction act;

cond_t _aio_done_cv;

/*
 * Input queue of requests which is serviced by the aux. lwps.
 */
cond_t _aio_idle_cv;
static struct aio_req *aio_in;

int _aio_cnt = 0;
int _aio_donecnt = 0;

int _max_workers = 50;			/* max number of workers permitted */
int _min_workers = 4;			/* min number of workers */
int _maxworkload = 32;			/* max length of worker's request q */
int _minworkload = 2;			/* min number of request in q */
int _aio_outstand_cnt = 0;		/* # of queued requests */
int _aio_worker_cnt = 0;		/* number of workers to do requests */
int _idle_workers = 0;			/* number of idle workers */
int __uaio_ok = 0;			/* AIO has been enabled */
sigset_t _worker_set;			/* worker's signal mask */

int _aiowait_flag;			/* when set, aiowait() is inprogress */

struct aio_worker *_kaiowp;		/* points to kaio cleanup thread */
/*
 * called by the child when the main thread forks. the child is
 * cleaned up so that it can use libaio.
 */
_aio_forkinit()
{
	int i;

	__uaio_ok = 0;
	_workers = NULL;
	_nextworker = NULL;
	_aio_done_tail = NULL;
	_aio_done_head = NULL;
	_aio_hash = NULL;
	_aio_freelist = NULL;
	_aio_freelist_cnt = 0;
	_aio_outstand_cnt = 0;
	_aio_worker_cnt = 0;
	_idle_workers = 0;
	_kaio_ok = 0;
#ifdef	DEBUG
	_clogged = 0;
	_qlocked = 0;
#endif
}

#ifdef DEBUG
/*
 * print out a bunch of interesting statistics when the process
 * exits.
 */
void
_aio_stats()
{
	int i;
	char *fmt;
	int cnt;
	FILE *fp;

	fp = fopen("/tmp/libaio.log", "w");
	if ((int)fp == NULL)
		return;
	fprintf(fp, "size of AIO request struct = %d bytes\n", _aioreqsize);
	fprintf(fp, "number of AIO workers = %d\n", _aio_worker_cnt);
	cnt = _aio_worker_cnt + 1;
	for (i = 2; i <= cnt; i++) {
		fmt = "%d done %d, idle = %d, qfull = %d, newworker = %d\n";
		fprintf(fp, fmt, i, _donecnt[i], _idlecnt[i], _qfullcnt[i],
		    _newworker[i]);
	}
	fprintf(fp, "num times submitter found next work queue locked = %d\n",
	    _qlocked);
	fprintf(fp, "num times submitter found all work queues locked = %d\n",
	    _clogged);
	fprintf(fp, "average submit request = %d\n", _avesubmitcnt);
	fprintf(fp, "average number of submit requests per new worker = %d\n",
	    _avedone);
}
#endif

/*
 * libaio is initialized when an AIO request is made. important
 * constants are initialized like the max number of workers that
 * libaio can create, and the minimum number of workers permitted before
 * imposing some restrictions. also, some workers are created.
 */
int
__uaio_init()
{
	sigset_t set;
	int i;
	int size;
	int err = 0;
	extern void _aio_stats();

	_lwp_mutex_lock(&__aio_initlock);
	if (!__uaio_ok) {
		__pid = getpid();

		act.sa_handler = NULL;
		act.sa_flags = SA_SIGINFO;
		if (sigaction(SIGAIOCANCEL, &act, NULL) == -1) {
			_lwp_mutex_unlock(&__aio_initlock);
			return (-1);
		}
#ifdef DEBUG
		size = _max_workers * sizeof (int) * 5 + sizeof (int);
		_donecnt = (int *)malloc(size);
		memset((caddr_t)_donecnt, 0, size);
		_idlecnt = _donecnt + _max_workers;
		_qfullcnt = _idlecnt + _max_workers;
		_firstqcnt = _qfullcnt + _max_workers;
		_newworker = _firstqcnt + _max_workers;
		atexit(_aio_stats);
#endif
		size = (HASHSZ * sizeof (int) + sizeof (int));
		_aio_hash = (struct aio_req **)malloc(size);
		if (_aio_hash == NULL) {
			_lwp_mutex_unlock(&__aio_initlock);
			return (-1);
		}
		memset((caddr_t)_aio_hash, 0, size);

		/* initialize worker's signal mask to only catch SIGAIOCANCEL */
		sigfillset(&_worker_set);
		sigdelset(&_worker_set, SIGAIOCANCEL);

		i = 0;
		while (i++ < _min_workers)
			_aio_create_worker(NULL, NULL);

		_lwp_mutex_unlock(&__aio_initlock);
		__uaio_ok = 1;
		return (0);
	}
	_lwp_mutex_unlock(&__aio_initlock);
}

/*
 * special kaio cleanup thread sits in a loop in the
 * kernel waiting for pending kaio requests to complete.
 */
void
_kaio_cleanup_thread(void *arg)
{
	_kaio(AIOSTART);
}

/*
 * initialize kaio.
 */
void
_kaio_init(void)
{
	caddr_t stk;
	int stksize;
	int error = 0;
	sigset_t set;
	ucontext_t uc;

	_lwp_mutex_lock(&__aio_initlock);
	if (!_kaio_ok) {
		_pagesize = PAGESIZE;
		__aiostksz = 8 * _pagesize;
		__init_stacks(__aiostksz, _max_workers);
		if (_aio_alloc_stack(__aiostksz, &stk) == 0)
			error =  ENOMEM;
		else {
			_kaiowp = (struct aio_worker *)(stk + __aiostksz -
			    sizeof (struct aio_worker));
			_kaiowp->work_stk = stk;
			stksize = __aiostksz - sizeof (struct aio_worker);
			_lwp_makecontext(&uc, _kaio_cleanup_thread, NULL,
			    _kaiowp, stk, stksize);
			sigfillset(&set);
			memcpy(&uc.uc_sigmask, &set, sizeof (sigset_t));
			error = _kaio(AIOINIT);
			if (!error)
				error = _lwp_create(&uc, NULL,
				    &_kaiowp->work_lid);
			if (error)
				_aio_free_stack_unlocked(__aiostksz, stk);
		}
		if (error)
			_kaio_ok = -1;
		else
			_kaio_ok = 1;
	}
	_lwp_mutex_unlock(&__aio_initlock);
}

int
aioread(fd, buf, bufsz, offset, whence, resultp)
	int fd;
	caddr_t buf;
	int bufsz;
	off_t offset;
	int whence;
	aio_result_t *resultp;
{
	return (_aiorw(fd, buf, bufsz, offset, whence, resultp, AIOREAD));
}

int
aiowrite(fd, buf, bufsz, offset, whence, resultp)
	int fd;
	caddr_t buf;
	int bufsz;
	off_t offset;
	int whence;
	aio_result_t *resultp;
{
	return (_aiorw(fd, buf, bufsz, offset, whence, resultp, AIOWRITE));
}

int
_aiorw(fd, buf, bufsz, offset, whence, resultp, mode)
	int fd;
	caddr_t buf;
	int bufsz;
	off_t offset;
	int whence;
	aio_result_t *resultp;
	int mode;
{
	aio_req_t *aiorp = NULL;
	aio_args_t *ap = NULL;
	offset_t loffset = 0;
	struct stat stat;
	sigset_t oset;
	int idlers;
	int err = 0;
	int kerr;

	switch (whence) {

	case SEEK_SET:
		loffset = offset;
		break;
	case SEEK_CUR:
		if ((loffset = lseek(fd, 0, SEEK_CUR)) == -1)
			err = -1;
		else
			loffset += offset;
		break;
	case SEEK_END:
		if (fstat(fd, &stat) == -1)
			err = -1;
		else
			loffset = offset + stat.st_size;
		break;
	default:
		errno = EINVAL;
		err = -1;
	}

	if (err)
		return (err);

	/* initialize kaio */
	if (!_kaio_ok)
		_kaio_init();

	/*
	 * Try kernel aio first.
	 * If errno is ENOTSUP, fall back to the lwp implementation.
	 */
	if (_kaio_ok > 0) {
		resultp->aio_errno = 0;
		kerr = _kaio((resultp->aio_return == AIO_INPROGRESS) ?
				(mode | AIO_POLL_BIT) : mode,
				fd, buf, bufsz, loffset, resultp);
		if (kerr == 0)
			return (0);
		else if (errno != ENOTSUP)
			return (-1);
	}

	if (!__uaio_ok) {
		if (__uaio_init() == -1)
			return (-1);
	}

	aiorp = _aio_req_alloc();
	if (aiorp == (aio_req_t *)-1) {
		errno = EAGAIN;
		return (-1);
	}

	if (mode == AIOWRITE)
		aiorp->req_op = AIOWRITE;
	else
		aiorp->req_op = AIOREAD;

	aiorp->req_resultp = resultp;
	ap = &(aiorp->req_args);
	ap->fd = fd;
	ap->buf = buf;
	ap->bufsz = bufsz;
	ap->offset = loffset;

	_aio_lock();
	if (_aio_hash_insert(resultp, aiorp)) {
		_aio_req_free(aiorp);
		_aio_unlock();
		errno = EINVAL;
		return (-1);
	} else {
		_aio_unlock();
		_aio_req_add(aiorp);
		return (0);
	}
}

aiocancel(resultp)
	aio_result_t *resultp;
{
	aio_req_t *aiorp;
	struct aio_worker *aiowp;
	int oldstate;
	int outstand_cnt;
	int err = 0;


	if (!__uaio_ok) {
		errno = EINVAL;
		return (-1);
	}

	_aio_lock();
	aiorp = _aio_hash_del(resultp);
	_aio_unlock();
	if (aiorp == NULL) {
		_aio_lock();
		if (!_aio_outstand_cnt) {
			errno = EINVAL;
			_aio_unlock();
			return (-1);
		}
		_aio_unlock();
		errno = EACCES;
		return (-1);
	} else {
		_lwp_mutex_lock(&aiorp->req_lock);
		oldstate = aiorp->req_state;
		aiorp->req_state = AIO_REQ_CANCELLED;
		aiowp = aiorp->req_worker;
		_lwp_mutex_unlock(&aiorp->req_lock);
		if (oldstate == AIO_REQ_INPROGRESS) {
			if (err = _lwp_kill(aiowp->work_lid, SIGAIOCANCEL)) {
				errno = err;
				return (-1);
			}
			return (0);
		}
		if (oldstate == AIO_REQ_DONE) {
			_aio_lock();
			outstand_cnt = _aio_outstand_cnt;
			_aio_unlock();
			if (outstand_cnt == 0)
				errno = EINVAL;
			else
				errno = EACCES;
			return (-1);
		}
		return (0);
	}
}

/*
 * This must be asynch safe
 */
aio_result_t *
aiowait(uwait)
	struct timeval *uwait;
{
	aio_result_t *uresultp, *kresultp, *resultp;
	int timedwait = 0;
	int timedout = 0;
	int kaio_errno = 0;
	struct timeval curtime, end, *wait = NULL, twait;

	if (uwait) {
		if ((uwait->tv_sec > 0) || (uwait->tv_usec > 0)) {
			gettimeofday(&curtime, NULL);
			end.tv_sec = uwait->tv_sec + curtime.tv_sec;
			end.tv_usec = uwait->tv_usec + curtime.tv_usec;
			*(struct timeval *)&twait = *uwait;
			wait = &twait;
			timedwait++;
		} else {
			/* polling */
			kresultp = (aio_result_t *)_kaio(AIOWAIT, -1, 0);
			if ((int)kresultp != -1 && kresultp != NULL &&
			    (int)kresultp != 1)
				return (kresultp);
			_aio_lock();
			uresultp = _aio_req_done();
			if (uresultp != NULL && (int)uresultp != -1) {
				_aio_unlock();
				return (uresultp);
			}
			_aio_unlock();
			if ((int)uresultp == -1 && (int)kresultp == -1) {
				errno = EINVAL;
				return ((aio_result_t *) -1);
			} else
				return (NULL);
		}
	}
	while (1) {
		_aio_lock();
		uresultp = _aio_req_done();
		if (uresultp != NULL && (int)uresultp != -1) {
			_aio_unlock();
			resultp = uresultp;
			break;
		}
		_aiowait_flag++;
		_aio_unlock();
		kresultp = (aio_result_t *)_kaio(AIOWAIT, wait, (int)uresultp);
		kaio_errno = errno;
		_aio_lock();
		_aiowait_flag--;
		_aio_unlock();
		if ((int)kresultp == 1) {
			/* aiowait() awakened by an aionotify() */
			continue;
		} else if (kresultp != NULL && (int)kresultp != -1) {
			resultp = kresultp;
			break;
		} else if ((int)kresultp == -1 && kaio_errno == EINVAL &&
		    (int)uresultp == -1) {
			errno = kaio_errno;
			resultp = (aio_result_t *)-1;
			break;
		} else if ((int)kresultp == -1 && kaio_errno == EINTR) {
			errno = kaio_errno;
			resultp = (aio_result_t *)-1;
			break;
		} else if (timedwait) {
			gettimeofday(&curtime, NULL);
			wait->tv_sec = end.tv_sec - curtime.tv_sec;
			wait->tv_usec = end.tv_usec - curtime.tv_usec;
			if (wait->tv_sec < 0 || (wait->tv_sec == 0 &&
			    wait->tv_usec <= 0)) {
				resultp = NULL;
				break;
			}
		} else {
			ASSERT((kresultp == NULL && uresultp == NULL));
			resultp = NULL;
			break;
		}
	}
	return (resultp);
}

/*
 * If closing by file descriptor: we will simply cancel all the outstanding
 * aio`s and return. Those aio's in question will have either noticed the
 * cancellation notice before, during, or after initiating io.
 */
void
aiocancel_all(fd)
	int fd;
{
	aio_req_t *aiorp;
	aio_req_t **aiorpp;
	struct aio_worker *first, *next;
	int ostate;
	lwpid_t lid;
	int err = 0;

	if (_aio_outstand_cnt == 0)
		return;
	/*
	 * first search each worker's work queue for requests to
	 * cancel.
	 */
	first = _nextworker;
	next = first;
	do {
		/* cancel work from the first work queue */
		_lwp_mutex_lock(&next->work_qlock1);
		_aio_cancel_work(next->work_tail1, fd);
		_lwp_mutex_unlock(&next->work_qlock1);
	} while ((next = next->work_forw) != first);
	/*
	 * finally, check if there are requests on the done queue that
	 * should be canceled.
	 */
	_aio_lock();
	if (fd < 0) {
		_aio_done_tail = NULL;
		_aio_done_head = NULL;
	} else {
		aiorpp = &_aio_done_tail;
		while ((aiorp = *aiorpp) != NULL) {
			if (aiorp->req_args.fd == fd)
				*aiorpp = aiorp->req_next;
			aiorpp = &aiorp->req_next;
		}
	}
	_aio_unlock();
}

/*
 * cancel requests from a given work queue. if the file descriptor
 * parameter, fd, is non NULL, then only cancel those requests in
 * this queue that are to this file descriptor, fd. if the "fd"
 * parameter is NULL, then cancel all requests.
 */
_aio_cancel_work(aiorp, fd)
	aio_req_t *aiorp;
	int fd;
{
	int ostate;
	int lid;

	while (aiorp != NULL) {
		if (fd < 0 || aiorp->req_args.fd == fd) {
			_lwp_mutex_lock(&aiorp->req_lock);
			ostate = aiorp->req_state;
			aiorp->req_state = AIO_REQ_CANCELLED;
			_lwp_mutex_unlock(&aiorp->req_lock);
			if (ostate == AIO_REQ_INPROGRESS) {
				lid = aiorp->req_worker->work_lid;
				_lwp_kill(lid, SIGAIOCANCEL);
			}
		}
		aiorp = aiorp->req_next;
	}
}

/*
 * this is the worker's main routine. it keeps executing queued
 * requests until terminated. it blocks when its queue is empty.
 * All workers take work from the same queue.
 */
void
_aio_do_request(aiowp)
	struct aio_worker *aiowp;
{
	int err = 0;
	struct aio_args *arg;
	aio_req_t *aiorp;		/* current AIO request */
	struct aio_result_t *resultp;
	int cancelled = 0;
	int retval;

	aiowp->work_lid = _lwp_self();

cancelit:
	if (sigsetjmp(aiowp->work_jmp_buf, 1)) {
		goto cancelit;
	}

	while (1) {
		while ((aiorp = _aio_req_get(aiowp)) == NULL) {
			_aio_idle(aiowp);
		}
#ifdef DEBUG
		_donecnt[aiowp->work_lid]++;
#endif
		_lwp_mutex_lock(&aiorp->req_lock);
		if (aiorp->req_state == AIO_REQ_CANCELLED) {
			_lwp_mutex_unlock(&aiorp->req_lock);
			continue;
		}
		aiorp->req_state = AIO_REQ_INPROGRESS;
		_aio_cancel_on(aiowp);
		_lwp_mutex_unlock(&aiorp->req_lock);
		arg = &aiorp->req_args;
		resultp = aiorp->req_resultp;
		if (aiorp->req_op == AIOREAD) {
			retval = aio_pread(arg->fd, arg->buf, arg->bufsz,
			    arg->offset, &err);
		} else {
			retval = aio_pwrite(arg->fd, arg->buf, arg->bufsz,
			    arg->offset, &err);
		}
		_aio_cancel_off(aiowp);
		_lwp_mutex_lock(&aiorp->req_lock);
		if (aiorp->req_state == AIO_REQ_CANCELLED) {
			_lwp_mutex_unlock(&aiorp->req_lock);
			continue;
		}
		aiorp->req_state = AIO_REQ_DONE;
		_lwp_mutex_unlock(&aiorp->req_lock);
		resultp->aio_return = retval;
		resultp->aio_errno = err;
	}
}


/*
 * worker is set idle when its work queue is empty. if the worker has
 * done some work, these completed requests are placed on a common
 * done list. the worker checks again that it has no more work and then
 * goes to sleep waiting for more work.
 */
_aio_idle(aiowp)
	struct aio_worker *aiowp;
{
	/* put completed requests on aio_done_list */
	if (aiowp->work_done1)
		_aio_work_done(aiowp);

	_lwp_mutex_lock(&aiowp->work_lock);
	if (aiowp->work_cnt1 == 0) {
#ifdef DEBUG
		_idlecnt[aiowp->work_lid]++;
#endif
		aiowp->work_idleflg = 1;
		___lwp_cond_wait(&aiowp->work_idle_cv, &aiowp->work_lock, NULL);
		/*
		 * idle flag is cleared before worker is awakened
		 * by aio_req_add().
		 */
		return;
	}
	_lwp_mutex_unlock(&aiowp->work_lock);
}

/*
 * A worker's completed AIO requests are placed onto a global
 * done queue. The application is only sent a SIGIO signal if
 * the process has a handler enabled and it is not waiting via
 * aiowait().
 */
_aio_work_done(aiowp)
	struct aio_worker *aiowp;
{
	int done_cnt = 0;
	struct aio_req *head = NULL, *tail;

	_lwp_mutex_lock(&aiowp->work_qlock1);
	head = aiowp->work_prev1;
	head->req_next = NULL;
	tail = aiowp->work_tail1;
	done_cnt = aiowp->work_done1;
	aiowp->work_done1 = 0;
	aiowp->work_tail1 = aiowp->work_next1;
	aiowp->work_prev1 = NULL;
	_lwp_mutex_unlock(&aiowp->work_qlock1);
	_lwp_mutex_lock(&__aio_mutex);
	_aio_donecnt += done_cnt;
	ASSERT(_aio_donecnt > 0);
	ASSERT(done_cnt <= _aio_outstand_cnt);
	_aio_outstand_cnt -= done_cnt;
	ASSERT(head != NULL && tail != NULL);
	if (_aio_done_tail == NULL) {
		_aio_done_head = head;
		_aio_done_tail = tail;
	} else {
		_aio_done_head->req_next = tail;
		_aio_done_head = head;
	}

	if (_aiowait_flag) {
		_lwp_mutex_unlock(&__aio_mutex);
		_kaio(AIONOTIFY);
	} else {
		_lwp_mutex_unlock(&__aio_mutex);
		if (_sigio_enabled) {
			kill(__pid, SIGIO);
		}
	}
}

/*
 * the done queue consists of AIO requests that are in either the
 * AIO_REQ_DONE or AIO_REQ_CANCELLED state. requests that were cancelled
 * are discarded. if the done queue is empty then NULL is returned.
 * otherwise the address of a done aio_result_t is returned.
 */
struct aio_result_t *
_aio_req_done()
{
	struct aio_req *next;
	aio_result_t *resultp;
	int state;

	ASSERT(MUTEX_HELD(&__aio_mutex));

	while ((next = _aio_done_tail) != NULL) {
		_aio_done_tail = next->req_next;
		ASSERT(_aio_donecnt > 0);
		_aio_donecnt--;
		_aio_hash_del(next->req_resultp);
		resultp = next->req_resultp;
		state = next->req_state;
		_aio_req_free(next);
		if (state == AIO_REQ_DONE)
			return (resultp);
	}
	/* is queue empty? */
	if (next == NULL && _aio_outstand_cnt == 0)
		return ((aio_result_t *) -1);
	return (NULL);
}

/*
 * add an AIO request onto the next work queue. a circular list of
 * workers is used to choose the next worker. each worker has two
 * work queues. if the lock for the first queue is busy then the
 * request is placed on the second queue. the request is always
 * placed on one of the two queues depending on which one is locked.
 */
_aio_req_add(aiorp)
	aio_req_t *aiorp;
{
	struct aio_worker *aiowp;
	struct aio_worker *first;
	int clogged = 0;
	int idleflg;
	int qactive;
	int qfullflg = 0;
	int createworker = 0;

	aiorp->req_next = NULL;
	aiorp->req_state = AIO_REQ_QUEUED;
	_aio_lock();
	_aio_outstand_cnt++;
	_aio_unlock();
	_aio_cnt++;
#ifdef DEBUG
	_aio_submitcnt++;
	_aio_submitcnt2++;
#endif
	ASSERT(_nextworker != NULL);
	aiowp = _nextworker;
	/*
	 * try to acquire the next worker's work queue. if it is locked,
	 * then search the list of workers until a queue is found unlocked,
	 * or until the list is completely traversed at which point another
	 * worker will be created.
	 */
	first = aiowp;
	while (_lwp_mutex_trylock(&aiowp->work_qlock1)) {
#ifdef DEBUG
		_qlocked++;
#endif
		if (((aiowp = aiowp->work_forw)) == first) {
			clogged = 1;
			break;
		}
	}
	if (clogged) {
#ifdef DEBUG
		_new_workers++;
		_clogged++;
#endif
		if (_aio_create_worker(aiorp, 0))
			_aiopanic("_aio_req_add: clogged");
		return;
	}
	ASSERT(MUTEX_HELD(&aiowp->work_qlock1));
	aiowp->work_minload1++;
	if (_aio_worker_cnt < _max_workers &&
	    aiowp->work_minload1 > _minworkload) {
		aiowp->work_minload1 = 0;
		_lwp_mutex_unlock(&aiowp->work_qlock1);
#ifdef DEBUG
		_qfullcnt[aiowp->work_lid]++;
#endif
#ifdef DEBUG
		_new_workers++;
		_newworker[aiowp->work_lid]++;
		_avedone = _aio_submitcnt2/_new_workers;
#endif
		_lwp_mutex_lock(&__aio_mutex);
		_nextworker = aiowp->work_forw;
		_lwp_mutex_unlock(&__aio_mutex);
		if (_aio_create_worker(aiorp, 0))
			_aiopanic("aio_req_add: add more workers");
		return;
	}
	/*
	 * Put request onto worker's work queue.
	 */
	if (aiowp->work_tail1 == NULL) {
		ASSERT(aiowp->work_cnt1 == 0);
		aiowp->work_tail1 = aiorp;
		aiowp->work_next1 = aiorp;
	} else {
		aiowp->work_head1->req_next = aiorp;
		if (aiowp->work_next1 == NULL)
			aiowp->work_next1 = aiorp;
	}
	aiorp->req_worker = aiowp;
	aiowp->work_head1 = aiorp;
	qactive = aiowp->work_cnt1++;
	_lwp_mutex_unlock(&aiowp->work_qlock1);
	_lwp_mutex_lock(&__aio_mutex);
	_nextworker = aiowp->work_forw;
	_lwp_mutex_unlock(&__aio_mutex);
	/*
	 * Awaken worker if it is not currently active.
	 */
	if (!qactive) {
		_lwp_mutex_lock(&aiowp->work_lock);
		idleflg = aiowp->work_idleflg;
		aiowp->work_idleflg = 0;
		_lwp_mutex_unlock(&aiowp->work_lock);
		if (idleflg)
			_lwp_cond_signal(&aiowp->work_idle_cv);
	}
}

/*
 * get an AIO request for a specified worker. each worker has
 * two work queues. find the first one that is not empty and
 * remove this request from the queue and return it back to the
 * caller. if both queues are empty, then return a NULL.
 */
struct aio_req *
_aio_req_get(aiowp)
	struct aio_worker *aiowp;
{
	struct aio_req *next;

	_lwp_mutex_lock(&aiowp->work_qlock1);
	if (next = aiowp->work_next1) {
		aiowp->work_prev1 = next;
		ASSERT(aiowp->work_done1 >= 0);
		aiowp->work_done1++;
		ASSERT(next != next->req_next);
		aiowp->work_req = next;
		aiowp->work_next1 = next->req_next;
		aiowp->work_cnt1--;
		aiowp->work_minload1--;
#ifdef DEBUG
		_firstqcnt[aiowp->work_lid]++;
#endif
	}
	ASSERT(next != NULL || (next == NULL && aiowp->work_cnt1 == 0));
	_lwp_mutex_unlock(&aiowp->work_qlock1);
	return (next);
}

/*
 * An AIO request is indentified by an aio_result_t pointer. The AIO
 * library maps this aio_result_t pointer to its internal representation
 * via a hash table. This function adds an aio_result_t pointer to
 * the hash table.
 */
int
_aio_hash_insert(resultp, aiorp)
	aio_result_t *resultp;
	aio_req_t *aiorp;
{
	int i;
	aio_req_t *headp, *next, **last;

	ASSERT(MUTEX_HELD(&__aio_mutex));
	i = AIOHASH(resultp);
	last = (_aio_hash + i);
	while ((next = *last) != NULL) {
		if (resultp == next->req_resultp)
			return (-1);
		last = &next->req_link;
	}
	*last = aiorp;
	ASSERT(aiorp->req_link == NULL);
	return (0);
}

/*
 * remove an entry from the hash table.
 */
struct aio_req *
_aio_hash_del(resultp)
	aio_result_t *resultp;
{
	struct aio_req *next, **prev;
	int i;

	ASSERT(MUTEX_HELD(&__aio_mutex));
	i = AIOHASH(resultp);
	prev = (_aio_hash + i);
	while ((next = *prev) != NULL) {
		if (resultp == next->req_resultp) {
			*prev = next->req_link;
			return (next);
		}
		prev = &next->req_link;
	}
	ASSERT(next == NULL);
	return ((struct aio_req *)NULL);
}

/*
 * Allocate and free aios. They are cached.
 */
aio_req_t *
_aio_req_alloc(void)
{
	aio_req_t *aiorp;
	int err;

	_aio_lock();
	while (_aio_freelist == NULL) {
		_aio_unlock();
		err = 0;
		_lwp_mutex_lock(&__aio_cachefillock);
		if (__aio_cachefilling)
			_lwp_cond_wait(&__aio_cachefillcv, &__aio_cachefillock);
		else
			err = _fill_aiocache(HASHSZ);
		_lwp_mutex_unlock(&__aio_cachefillock);
		if (err)
			return ((aio_req_t *)-1);
		_aio_lock();
	}
	aiorp = _aio_freelist;
	_aio_freelist = _aio_freelist->req_link;
	aiorp->req_link = NULL;
	aiorp->req_next = NULL;
	_aio_freelist_cnt--;
	_aio_unlock();
	return (aiorp);
}

/*
 * fill the aio request cache with empty aio request structures.
 */
_fill_aiocache(n)
{
	aio_req_t *next, *aiorp, *first;
	int cnt;
	int ptr;
	int i;

	__aio_cachefilling = 1;
	if ((ptr = (int)malloc(sizeof (struct aio_req) * n)) == -1) {
		__aio_cachefilling = 0;
		_lwp_cond_broadcast(&__aio_cachefillcv);
		return (-1);
	}
	if (ptr & 0x7)
		_aiopanic("_fill_aiocache");
	first = (struct aio_req *)ptr;
	next = first;
	cnt = n - 1;
	for (i = 0; i < cnt; i++) {
		aiorp = next++;
		aiorp->req_state = AIO_REQ_FREE;
		aiorp->req_link = next;
		memset((caddr_t)&aiorp->req_lock, 0, sizeof (lwp_mutex_t));
	}
	__aio_cachefilling = 0;
	_lwp_cond_broadcast(&__aio_cachefillcv);
	next->req_link = NULL;
	memset((caddr_t)&next->req_lock, 0, sizeof (lwp_mutex_t));
	_aio_lock();
	_aio_freelist_cnt = n;
	_aio_freelist = first;
	_aio_unlock();
	return (0);
}

/*
 * put an aio request back onto the freelist.
 */
void
_aio_req_free(aiorp)
	aio_req_t *aiorp;
{
	ASSERT(MUTEX_HELD(&__aio_mutex));

	aiorp->req_state = AIO_REQ_FREE;
	aiorp->req_link = _aio_freelist;
	_aio_freelist = aiorp;
	_aio_freelist_cnt++;
}

/*
 * global aio lock that masks SIGIO signals.
 */
void
_aio_lock(void)
{
	__sigio_masked = 1;
	_lwp_mutex_lock(&__aio_mutex);
	__sigio_maskedcnt++;
}

/*
 * release global aio lock. send SIGIO signal if one
 * is pending.
 */
void
_aio_unlock(void)
{
	if (__sigio_maskedcnt--)
		__sigio_masked = 0;
	_lwp_mutex_unlock(&__aio_mutex);
	if (__sigio_pending)
		__aiosendsig();
}
