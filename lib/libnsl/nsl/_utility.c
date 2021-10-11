/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_utility.c	1.28	95/05/10 SMI"	/* SVr4.0 1.11	*/

#include "timt.h"
#include <sys/param.h>
#include <sys/types.h>
#include <stdlib.h>
#include <rpc/trace.h>
#include <errno.h>
#include <sys/stropts.h>
#include <sys/stream.h>
#include <sys/tihdr.h>
#include <sys/timod.h>
#include <tiuser.h>
#include <sys/fcntl.h>
#include <sys/signal.h>
#include "tli.h"

#define	DEFSIZE 2048

/*
 * The following used to be in tiuser.h, but was causing too much namespace
 * pollution.
 */
#define	ROUNDUP(X)	((X + 0x03)&~0x03)

static struct _ti_user	*find_tilink(int s);
static struct _ti_user	*add_tilink(int s);

mutex_t	_ti_userlock = DEFAULTMUTEX;	/* Protects hash_bucket[] */

/*
 * Checkfd - checks validity of file descriptor
 */

struct _ti_user *
_t_checkfd(fd)
int fd;
{
	sigset_t mask;
	struct _ti_user *tiptr;

	trace2(TR__t_checkfd, 0, fd);
	if (fd < 0) {
		t_errno = TBADF;
		trace2(TR__t_checkfd, 1, fd);
		return (NULL);
	}
	tiptr = NULL;
	MUTEX_LOCK_SIGMASK(&_ti_userlock, mask);
	if ((tiptr = find_tilink(fd)) != NULL) {
		MUTEX_UNLOCK_SIGMASK(&_ti_userlock, mask);
		return (tiptr);
	}
	MUTEX_UNLOCK_SIGMASK(&_ti_userlock, mask);

	/*
	 * Not found - check if descriptor is a valid TLI descriptor.
	 */
	if (ioctl(fd, I_FIND, "timod") <= 0) {
		int sv_errno = errno;
		t_errno = TBADF;
		trace2(TR__t_checkfd, 1, fd);
		errno = sv_errno;
		return (NULL);
	}
	/*
	 * Maybe the descriptor is valid but we did an exec()
	 * and lost the information, Try to reconstitute the info.
	 */
	MUTEX_LOCK_PROCMASK(&_ti_userlock, mask);
	tiptr = _t_create(fd, NULL);
	if (tiptr == NULL) {
		int sv_errno = errno;
		MUTEX_UNLOCK_PROCMASK(&_ti_userlock, mask);
		trace2(TR__t_checkfd, 1, fd);
		errno = sv_errno;
		return (NULL);
	}
	MUTEX_UNLOCK_PROCMASK(&_ti_userlock, mask);
	trace2(TR__t_checkfd, 1, fd);
	return (tiptr);
}

/*
 * copy data to output buffer and align it as in input buffer
 * This is to ensure that if the user wants to align a network
 * addr on a non-word boundry then it will happen.
 */
void
_t_aligned_copy(buf, len, init_offset, datap, rtn_offset)
char *buf;
char *datap;
long *rtn_offset;
{
	trace1(TR__t_aligned_copy, 0);
	*rtn_offset = ROUNDUP(init_offset) + ((unsigned int)datap&0x03);
	memcpy((char *)(buf + *rtn_offset), datap, (int)len);
	trace1(TR__t_aligned_copy, 1);
}


/*
 * Max - return max between two ints
 */
int
_t_max(x, y)
int x;
int y;
{
	trace3(TR__t_max, 0, x, y);
	if (x > y) {
		trace3(TR__t_max, 1, x, y);
		return (x);
	} else {
		trace3(TR__t_max, 1, x, y);
		return (y);
	}
}

/*
 * put data and control info in look buffer
 *
 * The only thing that can be in look buffer is a T_discon_ind,
 * T_ordrel_ind or a T_uderr_ind.
 */
void
_t_putback(tiptr, dptr, dsize, cptr, csize)
struct _ti_user *tiptr;
caddr_t dptr;
int dsize;
caddr_t cptr;
int csize;
{
	trace3(TR__t_putback, 0, dsize, csize);
	memcpy(tiptr->ti_lookdbuf, dptr, dsize);
	memcpy(tiptr->ti_lookcbuf, cptr, csize);
	tiptr->ti_lookdsize = dsize;
	tiptr->ti_lookcsize = csize;
	tiptr->ti_lookflg++;
	trace3(TR__t_putback, 1, dsize, csize);

}

/*
 * Is there something that needs attention?
 */
int
_t_is_event(fd, tiptr)
int fd;
struct _ti_user *tiptr;
{
	int size, retval;

	trace2(TR__t_is_event, 0, fd);
	if ((retval = ioctl(fd, I_NREAD, &size)) < 0) {
		int sv_errno = errno;
		t_errno = TSYSERR;
		trace2(TR__t_is_event, 1, fd);
		errno = sv_errno;
		return (1);
	}

	if (retval || tiptr->ti_lookflg) {
		t_errno = TLOOK;
		trace2(TR__t_is_event, 1, fd);
		return (1);
	}
	trace2(TR__t_is_event, 1, fd);
	return (0);
}

/*
 * wait for T_OK_ACK
 */
int
_t_is_ok(fd, tiptr, type)
int fd;
register struct _ti_user *tiptr;
long type;
{

	struct strbuf ctlbuf;
	struct strbuf rcvbuf;
	register union T_primitives *pptr;
	int flags = 0;
	int retval, cntlflag;
	int size;

	trace2(TR__t_is_ok, 0, fd);
	cntlflag = fcntl(fd, F_GETFL, 0);
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~(O_NDELAY | O_NONBLOCK));

	ctlbuf.len = 0;
	ctlbuf.buf = tiptr->ti_ctlbuf;
	ctlbuf.maxlen = tiptr->ti_ctlsize;
	rcvbuf.maxlen = tiptr->ti_rcvsize;
	rcvbuf.len = 0;
	rcvbuf.buf = tiptr->ti_rcvbuf;
	flags = RS_HIPRI;

	while ((retval = getmsg(fd, &ctlbuf, &rcvbuf, &flags)) < 0) {
		int sv_errno = errno;
		if (errno == EINTR)
			continue;
		t_errno = TSYSERR;
		trace2(TR__t_is_ok, 1, fd);
		errno = sv_errno;
		return (0);
	}

	/* did I get entire message */
	if (retval) {
		t_errno = TSYSERR;
		trace2(TR__t_is_ok, 1, fd);
		errno = EIO;
		return (0);
	}

	/*
	 * is ctl part large enough to determine type?
	 */
	if (ctlbuf.len < sizeof (long)) {
		t_errno = TSYSERR;
		trace2(TR__t_is_ok, 1, fd);
		errno = EPROTO;
		return (0);
	}

	fcntl(fd, F_SETFL, cntlflag);

	pptr = (union T_primitives *)ctlbuf.buf;

	switch (pptr->type) {
		case T_OK_ACK:
			if ((ctlbuf.len < sizeof (struct T_ok_ack)) ||
			    (pptr->ok_ack.CORRECT_prim != type)) {
				t_errno = TSYSERR;
				trace2(TR__t_is_ok, 1, fd);
				errno = EPROTO;
				return (0);
			}
			trace2(TR__t_is_ok, 1, fd);
			return (1);

		case T_ERROR_ACK:
			if ((ctlbuf.len < sizeof (struct T_error_ack)) ||
			    (pptr->error_ack.ERROR_prim != type)) {
				t_errno = TSYSERR;
				trace2(TR__t_is_ok, 1, fd);
				errno = EPROTO;
				return (0);
			}
			/*
			 * if error is out of state and there is something
			 * on read queue, then indicate to user that
			 * there is something that needs attention
			 */
			if (pptr->error_ack.TLI_error == TOUTSTATE) {
				if ((retval = ioctl(fd, I_NREAD, &size)) < 0) {
					int sv_errno = errno;
					t_errno = TSYSERR;
					trace2(TR__t_is_ok, 1, fd);
					errno = sv_errno;
					return (0);
				}
				if (retval)
					t_errno = TLOOK;
				else
					t_errno = TOUTSTATE;
				trace2(TR__t_is_ok, 1, fd);
			} else {
				t_errno = pptr->error_ack.TLI_error;
				trace2(TR__t_is_ok, 1, fd);
				if (t_errno == TSYSERR)
					errno = pptr->error_ack.UNIX_error;
			}
			return (0);

		default:
			t_errno = TSYSERR;
			trace2(TR__t_is_ok, 1, fd);
			errno = EPROTO;
			return (0);
	}
}

/*
 * timod ioctl
 */
int
_t_do_ioctl(fd, buf, size, cmd, retlen)
char *buf;
int *retlen;
{
	int retval;
	struct strioctl strioc;

	trace1(TR__t_do_ioctl, 0);
	strioc.ic_cmd = cmd;
	strioc.ic_timout = -1;
	strioc.ic_len = size;
	strioc.ic_dp = buf;

	if ((retval = ioctl(fd, I_STR, &strioc)) < 0) {
		int sv_errno = errno;
		t_errno = TSYSERR;
		trace1(TR__t_do_ioctl, 1);
		errno = sv_errno;
		return (0);
	}

	if (retval) {
		t_errno = retval&0xff;
		trace1(TR__t_do_ioctl, 1);
		if (t_errno == TSYSERR)
			errno = (retval >>  8)&0xff;
		return (0);
	}
	if (retlen)
		*retlen = strioc.ic_len;
	trace1(TR__t_do_ioctl, 1);
	return (1);
}

/*
 * alloc scratch buffers and look buffers
 */

/* ARGSUSED */
static int
_t_alloc_bufs(fd, tiptr, info)
register struct _ti_user *tiptr;
struct T_info_ack info;
{
	unsigned size1, size2;
	unsigned csize, dsize, asize, osize;
	char *ctlbuf, *rcvbuf;
	char *lookdbuf, *lookcbuf;

	trace2(TR__t_alloc_bufs, 0, fd);
	csize = _t_setsize(info.CDATA_size);
	dsize = _t_setsize(info.DDATA_size);

	size1 = _t_max(csize, dsize);

	if (size1 > 0) {
		if ((rcvbuf = calloc(1, size1)) == NULL) {
			int sv_errno = errno;
			trace2(TR__t_alloc_bufs, 1, fd);
			errno = sv_errno;
			return (-1);
		}
		if ((lookdbuf = calloc(1, size1)) == NULL) {
			int sv_errno = errno;
			free(rcvbuf);
			trace2(TR__t_alloc_bufs, 1, fd);
			errno = sv_errno;
			return (-1);
		}
	} else {
		rcvbuf = NULL;
		lookdbuf = NULL;
	}

	asize = _t_setsize(info.ADDR_size);
	osize = _t_setsize(info.OPT_size);

	size2 = sizeof (union T_primitives) + asize + sizeof (long) + osize
		+ sizeof (long);

	if ((ctlbuf = calloc(1, size2)) == NULL) {
		int sv_errno = errno;
		if (size1 > 0) {
			free(rcvbuf);
			free(lookdbuf);
		}
		trace2(TR__t_alloc_bufs, 1, fd);
		errno = sv_errno;
		return (-1);
	}

	if ((lookcbuf = calloc(1, size2)) == NULL) {
		int sv_errno = errno;
		if (size1 > 0) {
			free(rcvbuf);
			free(lookdbuf);
		}
		free(ctlbuf);
		trace2(TR__t_alloc_bufs, 1, fd);
		errno = sv_errno;
		return (-1);
	}


	tiptr->ti_rcvsize = size1;
	tiptr->ti_rcvbuf = rcvbuf;
	tiptr->ti_ctlsize = size2;
	tiptr->ti_ctlbuf = ctlbuf;
	tiptr->ti_lookcbuf = lookcbuf;
	tiptr->ti_lookdbuf = lookdbuf;
	tiptr->ti_lookcsize = 0;
	tiptr->ti_lookdsize = 0;
	tiptr->ti_lookflg = 0;
	tiptr->ti_flags = USED | info.PROVIDER_flag;
	tiptr->ti_maxpsz = info.TIDU_size;
	tiptr->ti_tsdusize = info.TSDU_size;
	tiptr->ti_servtype = info.SERV_type;
	tiptr->ti_state = T_UNINIT;
	tiptr->ti_ocnt = 0;
	trace2(TR__t_alloc_bufs, 1, fd);
	return (0);
}

/*
 * set sizes of buffers
 */
static int
_t_setsize(infosize)
long infosize;
{
	trace2(TR__t_setsize, 0, infosize);
	switch (infosize)
	{
		case -1:
			trace2(TR__t_setsize, 1, infosize);
			return (DEFSIZE);
		case -2:
			trace2(TR__t_setsize, 1, infosize);
			return (0);
		default:
			trace2(TR__t_setsize, 1, infosize);
			return (infosize);
	}
}

static void
_init_tiptr(tiptr)
struct _ti_user *tiptr;
{
	trace1(TR__init_tiptr, 0);
	tiptr->ti_flags = 0;
	tiptr->ti_rcvsize = 0;
	tiptr->ti_rcvbuf = NULL;
	tiptr->ti_ctlsize = 0;
	tiptr->ti_ctlbuf = NULL;
	tiptr->ti_lookdbuf = NULL;
	tiptr->ti_lookcbuf = NULL;
	tiptr->ti_lookdsize = 0;
	tiptr->ti_lookcsize = 0;
	tiptr->ti_maxpsz = 0;
	tiptr->ti_tsdusize = 0;
	tiptr->ti_servtype = 0;
	tiptr->ti_lookflg = 0;
	tiptr->ti_state = 0;
	tiptr->ti_ocnt = 0;
	/*
	 * Note: ti_fd field not to be NULL'd here
	 *       ti_lock not to be mutex_init'd here
	 */
	trace1(TR__init_tiptr, 1);
}


/*
 * If `action' == SIG_BLOCK we block SIGPOLL and return
 * the original mask in `mask'.
 * Otherwise we restore the set of blocked signals to `mask'.
 */
void
_t_blocksigpoll(mask, action)
	sigset_t	*mask;
	int		action;
{
	sigset_t	new;

	trace2(TR__t_blocksigpoll, 0, action);
	if (action == SIG_BLOCK) {
		_sigfillset(&new);
		(void) _sigprocmask(SIG_BLOCK, &new, mask);
	} else	{
		(void) _sigprocmask(SIG_SETMASK, mask, (sigset_t *)NULL);
	}
	trace2(TR__t_blocksigpoll, 1, action);
}

void
_t_blockallsignals(maskp)
	sigset_t	*maskp;
{
	sigset_t	new;

	trace2(TR__t_blockallsignals, 0, maskp);
	(void) _sigfillset(&new);
	(void) _sigprocmask(SIG_BLOCK, &new, maskp);
	trace2(TR__t_blockallsignals, 1, maskp);
}

void
_t_restoresigmask(maskp)
	sigset_t *maskp;
{
	trace2(TR__t_restoresigmask, 0, maskp);
	(void) _sigprocmask(SIG_SETMASK, maskp, (sigset_t *)NULL);
	trace2(TR__t_restoresigmask, 1, maskp);
}


/*
 * Link manipulation routines.
 *
 * NBUCKETS hash buckets are used to give fast
 * access. The number is derived the file descriptor softlimit
 * number (64).
 */

#define	NBUCKETS	64
static struct _ti_user		*hash_bucket[NBUCKETS];

/*
 * Allocates a new link and returns a pointer to it.
 * Assumes that the caller is holding _ti_userlock.
 * and has signals blocked even when not linked to
 * libthread
 */
static struct _ti_user *
add_tilink(s)
	register int			s;
{
	register struct _ti_user	*tiptr;
	register struct _ti_user	*prevptr;
	register struct _ti_user	*curptr;
	register int			x;

	x = s % NBUCKETS;
	if (hash_bucket[x] != NULL) {
		/*
		 * Walk along the bucket looking for
		 * duplicate entry or the end.
		 */
		for (curptr = hash_bucket[x]; curptr != NULL;
						curptr = curptr->ti_next) {
			if (curptr->ti_fd == s) {
				/*
				 * This can happen when the user has close(2)'ed
				 * a descripter and then been allocated it again
				 * via t_open().
				 *
				 * We will re-use the existing _ti_user struct
				 * in this case rather than using the one
				 * we allocated above.  If there are buffers
				 * associated with the existing _ti_user
				 * struct, they may not be the correct size,
				 * so we can not use it.  We free them
				 * here and re-allocate a new ones
				 * later on.
				 */
				if (curptr->ti_rcvbuf != NULL)
					free(curptr->ti_rcvbuf);
				if (curptr->ti_lookdbuf != NULL)
					free(curptr->ti_lookdbuf);
				free(curptr->ti_ctlbuf);
				free(curptr->ti_lookcbuf);
				_init_tiptr(curptr);
				return (curptr);
			}
			prevptr = curptr;
		}
		/*
		 * Allocate and link in a new one.
		 */
		if ((tiptr = (struct _ti_user *)malloc(sizeof (*tiptr)))
		    == NULL)
			return (NULL);
		_init_tiptr(tiptr);
		prevptr->ti_next = tiptr;
		tiptr->ti_prev = prevptr;
	} else	{
		/*
		 * First entry.
		 */
		if ((tiptr = (struct _ti_user *)malloc(sizeof (*tiptr)))
		    == NULL)
			return (NULL);
		_init_tiptr(tiptr);
		hash_bucket[x] = tiptr;
		tiptr->ti_prev = NULL;
	}
	tiptr->ti_next = NULL;
	tiptr->ti_fd = s;
#ifdef _REENTRANT
	mutex_init(&tiptr->ti_lock, USYNC_THREAD, NULL);
#endif /* _REENTRANT */
	return (tiptr);
}

/*
 * Find a link by descriptor
 * Assumes that the caller is holding _ti_userlock.
 */
static struct _ti_user *
find_tilink(s)
	register int			s;
{
	register struct _ti_user	*curptr;
	register int			x;

	if (s >= 0) {
		x = s % NBUCKETS;
		if (hash_bucket[x] != NULL) {
			/*
			 * Walk along the bucket looking for
			 * the descripter.
			 */
			for (curptr = hash_bucket[x]; curptr != NULL;
						curptr = curptr->ti_next) {
				if (curptr->ti_fd == s)
					return (curptr);
			}
		}
	}
	errno = EINVAL;
	return (NULL);
}

/*
 * Assumes that the caller is holding _ti_userlock.
 * Also assumes all signals are blocked even when not
 * linked to libthread
 */
int
delete_tilink(s)
	register int			s;
{
	register struct _ti_user	*curptr;
	register int			x;

	/*
	 * Find the link.
	 */
	x = s % NBUCKETS;
	if (hash_bucket[x] != NULL) {
		/*
		 * Walk along the bucket looking for
		 * the descripter.
		 */
		for (curptr = hash_bucket[x]; curptr != NULL;
						curptr = curptr->ti_next) {
			if (curptr->ti_fd == s) {
				register struct _ti_user        *nextptr;
				register struct _ti_user	*prevptr;

				nextptr = curptr->ti_next;
				prevptr = curptr->ti_prev;
				if (prevptr)
					prevptr->ti_next = nextptr;
				else
					hash_bucket[x] = nextptr;
				if (nextptr)
					nextptr->ti_prev = prevptr;

				/*
				 * free resource associated with the curptr
				 */
				if (curptr->ti_rcvbuf != NULL)
					free(curptr->ti_rcvbuf);
				if (curptr->ti_lookdbuf != NULL)
					free(curptr->ti_lookdbuf);
				free(curptr->ti_ctlbuf);
				free(curptr->ti_lookcbuf);
#ifdef _REENTRANT
				mutex_destroy(&curptr->ti_lock);
#endif /* _REENTRANT */
				free(curptr);
				return (0);
			}
		}
	}
	errno = EINVAL;
	return (-1);
}

/*
 * Allocate a TLI state structure and synch it with the kernel
 * *tiptr is returned
 * Assumes that the caller is holding the _ti_userlock and has blocked process
 * signals.
 */
struct _ti_user *
_t_create(fd, info)
register int	fd;
register struct t_info *info;
{
	struct T_info_ack	inforeq;
	struct _ti_user	*ntiptr;
	sigset_t mask;
	int retval, rstate;
	struct strpeek arg;
	char ctlbuf[sizeof (long)];
	char databuf[sizeof (long)]; /* size unimportant - anything > 0 */

	trace2(TR__t_create, 0, flags);
	inforeq.PRIM_type = T_INFO_REQ;
	if (!_t_do_ioctl(fd, (caddr_t)&inforeq, sizeof (struct T_info_req),
		TI_GETINFO, &retval)) {
		int sv_errno = errno;
		close(fd);
		trace2(TR__t_create, 1, flags);
		errno = sv_errno;
		return (NULL);
	}
	if (retval != sizeof (struct T_info_ack)) {
		t_errno = TSYSERR;
		close(fd);
		trace2(TR__t_create, 1, flags);
		errno = EIO;
		return (NULL);
	}

	if (info != NULL) {
		info->addr = inforeq.ADDR_size;
		info->options = inforeq.OPT_size;
		info->tsdu = inforeq.TSDU_size;
		info->etsdu = inforeq.ETSDU_size;
		info->connect = inforeq.CDATA_size;
		info->discon = inforeq.DDATA_size;
		info->servtype = inforeq.SERV_type;
	}

	/*
	 * if first time done then initialize data structure
	 * and allocate buffers
	 */
	ntiptr = add_tilink(fd);
	if (ntiptr == NULL) {
		t_errno = TSYSERR;
		close(fd);
		trace2(TR__t_create, 1, flags);
		errno = ENOMEM;
		return (NULL);
	}
	MUTEX_LOCK_SIGMASK(&ntiptr->ti_lock, mask);

	/*
	 * Allocate buffers for the new descriptor
	 */
	if (_t_alloc_bufs(fd, ntiptr, inforeq) < 0) {
		int sv_errno = errno;
		t_close(fd);
		t_errno = TSYSERR;
		MUTEX_UNLOCK_SIGMASK(&ntiptr->ti_lock, mask);
		trace2(TR__t_create, 1, flags);
		errno = sv_errno;
		return (NULL);
	}
	/*
	 * Restore state from kernel (caveat some heauristics)
	 */
	switch (inforeq.CURRENT_state) {

	case TS_UNBND:
		ntiptr->ti_state = T_UNBND;
		break;

	case TS_IDLE:
		rstate = T_IDLE;
		/*
		 * Peek at message on stream head (if any)
		 * and see if it is data
		 */
		arg.ctlbuf.buf = ctlbuf;
		arg.ctlbuf.maxlen = sizeof (ctlbuf);
		arg.ctlbuf.len = 0;

		arg.databuf.buf = databuf;
		arg.databuf.maxlen = sizeof (databuf);
		arg.databuf.len = 0;

		arg.flags = 0;

		if ((retval = ioctl(fd, I_PEEK, &arg)) < 0)  {
			int sv_errno = errno;

			t_errno = TSYSERR;
			MUTEX_UNLOCK_SIGMASK(&ntiptr->ti_lock, mask);
			trace2(TR__t_create, 1, fd);
			errno = sv_errno;
			return (NULL);
		}
		if (!retval) {
			/*
			 * this is to handle data ahead of T_DISCON_IND
			 * indications that might be at the stream head waiting
			 * to be read (T_DATA_IND or M_DATA)
			 */
			if (((arg.ctlbuf.len == 4) &&
				((*(long *) arg.ctlbuf.buf) == T_DATA_IND)) ||
			    ((arg.ctlbuf.len == 0) && arg.databuf.len)) {
				rstate = T_DATAXFER;
			}
		}
		ntiptr->ti_state = rstate;
		break;

	case TS_WRES_CIND:
		ntiptr->ti_state = T_INCON;
		break;

	case TS_WCON_CREQ:
		ntiptr->ti_state = T_OUTCON;
		break;

	case TS_DATA_XFER:
		ntiptr->ti_state = T_DATAXFER;
		break;

	case TS_WIND_ORDREL:
		ntiptr->ti_state = T_OUTREL;
		break;

	case TS_WREQ_ORDREL:
		rstate = T_INREL;
		/*
		 * Peek at message on stream head (if any)
		 * and see if it is data
		 */
		arg.ctlbuf.buf = ctlbuf;
		arg.ctlbuf.maxlen = sizeof (ctlbuf);
		arg.ctlbuf.len = 0;

		arg.databuf.buf = databuf;
		arg.databuf.maxlen = sizeof (databuf);
		arg.databuf.len = 0;

		arg.flags = 0;

		if ((retval = ioctl(fd, I_PEEK, &arg)) < 0)  {
			int sv_errno = errno;

			t_errno = TSYSERR;
			MUTEX_UNLOCK_SIGMASK(&ntiptr->ti_lock, mask);
			trace2(TR__t_create, 1, fd);
			errno = sv_errno;
			return (NULL);
		}
		if (!retval) {
			/*
			 * this is to handle data ahead of T_ORDREL_IND
			 * indications that might be at the stream head waiting
			 * to be read (T_DATA_IND or M_DATA)
			 */
			if (((arg.ctlbuf.len == 4) &&
				((*(long *) arg.ctlbuf.buf) == T_DATA_IND)) ||
			    ((arg.ctlbuf.len == 0) && arg.databuf.len)) {
				rstate = T_DATAXFER;
			}
		}
		ntiptr->ti_state = rstate;
		break;
	default:
		ntiptr->ti_state = T_FAKE;
		t_errno = TSTATECHNG;
		MUTEX_UNLOCK_SIGMASK(&ntiptr->ti_lock, mask);
		trace2(TR__t_create, 1, fd);
		return (NULL);
	}
	MUTEX_UNLOCK_SIGMASK(&ntiptr->ti_lock, mask);
	return (ntiptr);
}



