/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved   */

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_utility.c	1.54	95/09/19 SMI"   /* SVr4.0 1.20  */

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 *		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 *	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
 *	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *		  All rights reserved.
 *
 */

#include "sockmt.h"
#include <sys/param.h>
#include <sys/types.h>
#include <sys/mkdev.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stropts.h>
#include <sys/stream.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/socket.h>
#include <tiuser.h>
#include <sys/tihdr.h>
#include <sys/sockmod.h>
#include <sys/uio.h>
#include <signal.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <poll.h>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <netconfig.h>
#include "sock.h"

/*
 * The following used to be in tiuser.h, but was causing too much namespace
 * pollution.
 */
#define	ROUNDUP(X)	((X + 0x03)&~0x03)

static struct _si_user	*find_silink_unlocked(int s);
static struct _si_user	*add_silink(int s);
static int delete_silink(int s);
static int tlitosyserr(int terr);
static int recvaccrights(struct _si_user *siptr, char *buf, int len,
		struct sockaddr *from, int *fromlen,
		char *accrights, int *accrightslen,
		int fmode);
static int send_clts_rights(struct _si_user *siptr, char *buf, int len,
		struct sockaddr *to, int tolen,
		char *accrights, int accrightslen);
static int send_cots_rights(struct _si_user *siptr, char *buf, int len,
		char *accrights, int accrightslen);
static int msgpeek(int s, struct strbuf *ctlbuf, struct strbuf *rcvbuf,
		int fmode);
static int _s_alloc(int s, struct _si_user **siptr, int new);
static int _s_alloc_bufs(struct _si_user *siptr);
static void _s_free(struct _si_user *siptr);
static struct netconfig *_s_match_netconf(int family, int type, int proto,
		void **nethandle);

/*
 * Global, used to enable debugging.
 */
int			_s_sockdebug;

/*
 * The following two string arrays map a number as specified
 * by a user of sockets, to the string as would be returned
 * by a call to getnetconfig().
 *
 * They are used by _s_match_netconf();
 *
 * proto_sw contains protocol entries for which there is a corresponding
 * /dev device. All others would presumably use raw IP and download the
 * desired protocol.
 */
static char *proto_sw[] = {
	"",
	"icmp",		/* 1 = ICMP */
	"",
	"",
	"",
	"",
	"tcp",		/* 6 = TCP */
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"udp",		/* 17 = UDP */
};

static char *family_sw[] = {
	"-",		/* 0 = AF_UNSPEC */
	"loopback",	/* 1 = AF_UNIX */
	"inet",		/* 2 = AF_INET */
	"implink",	/* 3 = AF_IMPLINK */
	"pup",		/* 4 = AF_PUP */
	"chaos",	/* 5 = AF_CHAOS */
	"ns",		/* 6 = AF_NS */
	"nbs",		/* 7 = AF_NBS */
	"ecma",		/* 8 = AF_ECMA */
	"datakit",	/* 9 = AF_DATAKIT */
	"ccitt",	/* 10 = AF_CCITT */
	"sna",		/* 11 = AF_SNA */
	"decnet",	/* 12 = AF_DECnet */
	"dli",		/* 13 = AF_DLI */
	"lat",		/* 14 = AF_LAT */
	"hylink",	/* 15 = AF_HYLINK */
	"appletalk",	/* 16 = AF_APPLETALK */
	"nit",		/* 17 = AF_NIT */
	"ieee802",	/* 18 = AF_802 */
	"osi",		/* 19 = AF_OSI */
	"x25",		/* 20 = AF_X25 */
	"osinet",	/* 21 = AF_OSINET */
	"gosip",	/* 22 = AF_GOSIP */
	"ipx",		/* 23 = AF_IPX */
};

static mutex_t	_si_userlock = DEFAULTMUTEX;	/* Protects hash_bucket[] */

/*
 * Checkfd - checks validity of file descriptor
 */
struct _si_user *
_s_checkfd(s)
	register int		s;
{
	struct _si_user		*siptr;
	sigset_t			mask;

	MUTEX_LOCK_SIGMASK(&_si_userlock, mask);
	if ((siptr = find_silink_unlocked(s)) != NULL) {
		MUTEX_UNLOCK_SIGMASK(&_si_userlock, mask);
		return (siptr);
	}
	MUTEX_UNLOCK_SIGMASK(&_si_userlock, mask);

	/*
	 * Maybe the descripter is valid, but we did an exec()
	 * and lost the information. Try to re-constitute the info.
	 */
	SOCKDEBUG((struct _si_user *)NULL,
		"_s_checkfd: s %d: Not found, trying to reconstitute\n", s);

	MUTEX_LOCK_PROCMASK(&_si_userlock, mask);
	if (_s_alloc(s, &siptr, 0) < 0) {
		MUTEX_UNLOCK_PROCMASK(&_si_userlock, mask);
		return (NULL);
	}
	MUTEX_UNLOCK_PROCMASK(&_si_userlock, mask);

	return	(siptr);
}

/*
 * Do common socket creation
 */
struct _si_user *
_s_socreate(family, type, protocol)
	register int			family;
	register int			type;
	int				protocol;
{
	register int			s;
	struct _si_user	*siptr;
	register struct netconfig	*net;
	void				*nethandle;

	sigset_t			mask;
	int				retval;
	struct si_sockparams		sockparams;

	net = _s_match_netconf(family, type, protocol, &nethandle);
	if (net == NULL)
		return (NULL);

	if (strcmp(net->nc_proto, NC_NOPROTO) != 0)
		protocol = 0;
	if ((s = open(net->nc_device, O_RDWR)) < 0)
		return (NULL);
	/*
	 * Sockmod will fail open with EALREADY if a sockmod is already pushed
	 */
redo_push:
	if (_ioctl(s, I_PUSH, "sockmod") < 0) {
		if (errno == EINTR)
			goto redo_push;
		if (errno != EALREADY) {
			(void) close(s);
			return (NULL);
		}
	}

	/*
	 * deposit socket parameters with  sockmod
	 * (can be retrieved even after fork/exec for use in accept())
	 */
	sockparams.sp_family = family;
	sockparams.sp_type = type;
	sockparams.sp_protocol = protocol;

	do {
		retval = _s_do_ioctl(s, (caddr_t)&sockparams,
			sizeof (struct si_sockparams), SI_SOCKPARAMS, NULL);
	} while (! retval && errno == EINTR);

	/*
	 * Set stream head close time to 0.
	 */
	retval = 0;
	(void) _ioctl(s, I_SETCLTIME, &retval);

	/*
	 * Turn on SIGPIPE stream head write
	 * option.
	 */

	do {
		retval = _ioctl(s, I_SWROPT, SNDPIPE);
	} while (retval < 0 && errno == EINTR);

	if (retval < 0) {
		(void) close(s);
		return (NULL);
	}

	/*
	 * Get a new library entry and sync it
	 * with sockmod.
	 */
	MUTEX_LOCK_PROCMASK(&_si_userlock, mask);
	if (_s_alloc(s, &siptr, 1) < 0) {
		(void) close(s);
		MUTEX_UNLOCK_PROCMASK(&_si_userlock, mask);
		return (NULL);
	}
	MUTEX_UNLOCK_PROCMASK(&_si_userlock, mask);

	if (protocol) {
		/*
		 * Need to send down the protocol number.
		 */
		if (__setsockopt(siptr, SOL_SOCKET, SO_PROTOTYPE,
				(caddr_t)&protocol, sizeof (protocol)) < 0) {
			int sv_errno = errno;
			(void) _s_close(siptr);
			errno = sv_errno;
			return (NULL);
		}
	}
	endnetconfig(nethandle); /* finished with netconfig struct */
	return (siptr);
}

/*
 * Match config entry for protocol
 * requested.
 */
static struct netconfig *
_s_match_netconf(family, type, proto, nethandle)
	register int			family;
	register int			type;
	register int			proto;
	void				**nethandle;
{
	register struct netconfig	*net;
	register struct netconfig	*maybe;
	register char			*oproto;

	if (family < 0 || family >= sizeof (family_sw)/sizeof (char *) ||
				proto < 0 || proto >= IPPROTO_MAX)  {
		errno = EPROTONOSUPPORT;
		return (NULL);
	}
	if (proto) {
		if (proto >= sizeof (proto_sw)/sizeof (char *))
			oproto = "";
		else	oproto = proto_sw[proto];
	}

	/*
	 * Loop through each entry in netconfig
	 * until one matches or we reach the end.
	 */
	if ((*nethandle = setnetconfig()) == NULL) {
		(void) syslog(LOG_ERR, "_s_match_netconf: setnetconfig failed");
		return (NULL);
	}

	maybe = NULL;
	while ((net = getnetconfig(*nethandle)) != NULL) {
		/*
		 * We make a copy of net->nc_semantics rather than modifying
		 * it in place because the network selection code shares the
		 * structures returned by getnetconfig() among all its callers.
		 * See bug #1160886 for more details.
		 */
		unsigned long semantics = net->nc_semantics;

		if (semantics == NC_TPI_COTS_ORD)
			semantics = NC_TPI_COTS;
		if (proto) {
			if (strcmp(net->nc_protofmly, family_sw[family]) == 0 &&
			    semantics == type &&
			    strcmp(net->nc_proto, oproto) == 0)
				break;

			if (strcmp(net->nc_protofmly, family_sw[family]) == 0 &&
			    type == SOCK_RAW &&
			    semantics == SOCK_RAW &&
			    strcmp(net->nc_proto, NC_NOPROTO) == 0 &&
			    maybe == NULL)
				maybe = net;	/* in case no exact match */

			continue;
		} else	{
			if (strcmp(net->nc_protofmly, family_sw[family]) == 0 &&
					semantics == type) {
				break;
			}
		}
	}
	if (net == NULL && maybe)
		net = maybe;

	if (net == NULL) {
		endnetconfig(*nethandle);
		errno = EPROTONOSUPPORT;
		return (NULL);
	}

	return (net);
}

/*
 * Try to resynchronize the user level state with the kernel
 *
 * Assumes that the caller is holding siptr lock.
 */
int
_s_synch(siptr)
register struct _si_user		*siptr;
{
	register int			s = siptr->fd;
	struct si_udata			udata;
	int				retlen;
	int				retval;
	int				rval;
	sigset_t			procmask;

	do {
		rval = _s_do_ioctl(s, (caddr_t)&udata,
				sizeof (struct si_udata), SI_GETUDATA, &retlen);
	} while (! rval && errno == EINTR);

	if (! rval) {
		/*
		 * Could be EPIPE, ECONNREFUSED, ECONNRESET, or some other
		 * t_discon_ind error that got stuck on the stream head.
		 */
		siptr->udata.so_state |=
			SS_CANTSENDMORE|SS_CANTRCVMORE;
		siptr->udata.so_state &= ~SS_ISCONNECTED;
		return (0);
	}

	if (retlen != sizeof (struct si_udata)) {
		_s_free(siptr);
		errno = EPROTO;
		return (-1);
	}

	siptr->flags = 0;
	siptr->udata = udata;

	/* Block signals for malloc */
	_s_blockallsignals(&procmask);
	if (_s_alloc_bufs(siptr) < 0) {
		_s_restoresigmask(&procmask);
		return (-1);
	}
	_s_restoresigmask(&procmask);

	/*
	 * Get SIGIO and SIGURG disposition
	 * and cache them.
	 */
	retval = 0;
	if (_ioctl(s, I_GETSIG, &retval) < 0 && errno != EINVAL) {
		(void) syslog(LOG_ERR, "ioctl: I_GETSIG failed %d\n", errno);
		return (-1);
	}
	/*
	 * Normal return. Have to clear errno
	 * since other library code (notably _s_sosend/recv) uses
	 * errno to determine if an error has
	 * occured.
	 */
	errno = 0;

	if (retval & (S_RDNORM|S_WRNORM))
		siptr->flags |= S_SIGIO;

	if (retval & (S_RDBAND|S_BANDURG))
		siptr->flags |= S_SIGURG;

	/* Mark fcntl flags as invalid */
	s_invalfflags(siptr);

	SOCKDEBUG(siptr, "_s_synch: siptr: %x\n", siptr);
	return (0);
}

/*
 * Allocate a socket state structure and synch it with the kernel.
 * *siptr is returned.
 * Assumes that the caller is holding _si_userlock.
 * signals are blocked even when not linked to libthread
 * therefore no retry of interrupted (EINTR) calls and
 * possibility of interrupts while modifying global data
 * (MUTEX_LOCK_PROCMASK translates to sigprocmask when libthread is not
 * linked)
 */
static int
_s_alloc(s, siptr, new)
register int				s;
register struct _si_user		**siptr;
register int				new;
{
	register struct _si_user	*nsiptr;
	struct si_udata			udata;
	int				retlen;
	int				retval;
	sigset_t			mask;

	if (!_s_do_ioctl(s, (caddr_t)&udata, sizeof (struct si_udata),
			SI_GETUDATA, &retlen)) {
		return (-1);
	}

	if (retlen != sizeof (struct si_udata)) {
		errno = EPROTO;
		return (-1);
	}

	/*
	 * Allocate a link and initialize it.
	 */
	nsiptr = add_silink(s);
	if (nsiptr == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	MUTEX_LOCK_SIGMASK(&nsiptr->lock, mask);
	nsiptr->udata = udata;		/* structure copy */
	nsiptr->flags = 0;
	if (_s_alloc_bufs(nsiptr) < 0) {
		int sv_errno = errno;
		MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);
		_s_free(nsiptr);
		errno = sv_errno;
		return (-1);
	}
	MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);
	if (!new) {
		/*
		 * Get SIGIO and SIGURG disposition
		 * and cache them. Not done when the socket was just opened.
		 */
		retval = 0;
		if (_ioctl(s, I_GETSIG, &retval) < 0 && errno != EINVAL) {
			int sv_errno = errno;
			(void) syslog(LOG_ERR, "ioctl: I_GETSIG failed %d\n",
					errno);
			MUTEX_UNLOCK_SIGMASK(&nsiptr->lock, mask);
			_s_free(nsiptr);
			errno = sv_errno;
			return (-1);
		}
		/*
		 * Normal return. Have to clear errno
		 * since other library code (notably _s_sosend/recv) uses
		 * errno to determine if an error has
		 * occured.
		 */
		errno = 0;

		if (retval & (S_RDNORM|S_WRNORM))
			nsiptr->flags |= S_SIGIO;

		if (retval & (S_RDBAND|S_BANDURG))
			nsiptr->flags |= S_SIGURG;

		/* Invalidate cached fcntl flags */
		s_invalfflags(nsiptr);
	}

	*siptr = nsiptr;
	return (0);
}

/*
 * Get access rights and associated data.
 *
 * Only UNIX domain supported.
 *
 * Returns:
 *	>0	Number of bytes read on success
 *	-1	If an error occurred.
 */
static int
recvaccrights(siptr, buf, len, from, fromlen, accrights, accrightslen,
		fmode)
	struct _si_user		*siptr;
	char			*buf;
	int			len;
	register struct sockaddr *from;
	register int		*fromlen;
	char			*accrights;
	int			*accrightslen;
	register int		fmode;
{
	register int			i;
	register int			nfd;
	int				*fdarray;
	struct strrecvfd		pipe;
	int				retval;
	struct sockaddr_un		addr;
	sigset_t			mask;

	/*
	 * Make receiving access rights atomic with respect to signals
	 */
	_s_blockallsignals(&mask);

	if (siptr->udata.sockparams.sp_family != AF_UNIX) {
		_s_restoresigmask(&mask);
		errno = EOPNOTSUPP;
		return (-1);
	}

	/*
	 * First get the pipe channel.
	 */
	if (_ioctl(siptr->fd, I_RECVFD, &pipe) < 0) {
		_s_restoresigmask(&mask);
		return (-1);
	}

	/*
	 * To ensure the following operations are atomic
	 * as far as the user is concerned, we reset
	 * (O_NDELAY|O_NONBLOCK) if they are on.
	 */
	if (fmode & (O_NDELAY|O_NONBLOCK)) {
		if (_fcntl(siptr->fd, F_SETFL,
			    fmode & ~(O_NDELAY|O_NONBLOCK)) < 0) {
			retval = -1;
			goto recv_rights_done;
		}
		else
			s_invalfflags(siptr);
		siptr->fflags = s_getfflags(siptr) & ~(O_NDELAY|O_NONBLOCK);
	}

	/*
	 * We do the same whether the flags say MSG_PEEK or
	 * not.
	 */
	SOCKDEBUG(siptr, "recvaccrights: getting access rights\n", 0);

	/*
	 * Dispose of rights, copying them into the users
	 * buffer if possible.
	 */
	fdarray = (int *)accrights;
	nfd = *accrightslen/sizeof (int);
	*accrightslen = 0;
	i = 0;
	for (;;) {
		struct strrecvfd	stfd;

		retval = 0;
		if (_ioctl(pipe.fd, I_RECVFD, &stfd) < 0)
			break;
		else	{
			SOCKDEBUG(siptr, "recvaccrights: got fd %d\n", stfd.fd);
			if (i != nfd) {
				fdarray[i] = stfd.fd;
				*accrightslen += sizeof (int);
				i++;
			} else	{
				(void) close(stfd.fd);
			}
		}
	}

	if (errno == EBADMSG) {
		/*
		 * We have read all the access rights, get any data.
		 */
		errno = 0;
		if (siptr->udata.servtype == T_CLTS) {
			/*
			 * First get the source address.
			 */
			(void) memset((caddr_t)&addr, 0, sizeof (addr));
			if (read(pipe.fd, (caddr_t)&addr, sizeof (addr))
						!= sizeof (addr)) {
				errno = EPROTO;
				retval = -1;
				goto recv_rights_done;
			}
			if (from && fromlen) {
				if (*fromlen > sizeof (addr))
					*fromlen = sizeof (addr);
				(void) memcpy((char *)from, (caddr_t)&addr,
						*fromlen);
			}
		}

		retval = read(pipe.fd, buf, len);
		SOCKDEBUG(siptr, "recvaccrights: got %d bytes\n", retval);
	} else	{
		/*
		 * No data.
		 */
		if (errno == ENXIO) {
			errno = 0;
			retval = 0;
		}
	}

recv_rights_done:
	(void) close(pipe.fd);
	if (fmode & (O_NDELAY|O_NONBLOCK)) {
		(void) _fcntl(siptr->fd, F_SETFL, fmode);
		s_invalfflags(siptr);
	}
	_s_restoresigmask(&mask);
	return (retval);
}

/*
 * Peeks at a message. If no messages are
 * present it will block in a poll().
 * Note ioctl(I_PEEK) does not block.
 *
 * Returns:
 *	0	On success
 *	-1	On error. In particular, EBADMSG is returned if access
 *		are present.
 */
static int
msgpeek(s, ctlbuf, rcvbuf, fmode)
	register int		s;
	register struct strbuf	*ctlbuf;
	register struct strbuf	*rcvbuf;
	register int		fmode;
{
	register int		retval;
	struct strpeek		strpeek;

	strpeek.ctlbuf.buf = ctlbuf->buf;
	strpeek.ctlbuf.maxlen = ctlbuf->maxlen;
	strpeek.ctlbuf.len = 0;
	strpeek.databuf.buf = rcvbuf->buf;
	strpeek.databuf.maxlen = rcvbuf->maxlen;
	strpeek.databuf.len = 0;
	strpeek.flags = 0;

	for (;;) {
		do {
			retval = _ioctl(s, I_PEEK, &strpeek);
		} while (retval < 0 && errno == EINTR);

		if (retval < 0)
			return (-1);

		if (retval == 1) {
			ctlbuf->len = strpeek.ctlbuf.len;
			rcvbuf->len = strpeek.databuf.len;
			return (0);
		} else	if ((fmode & (O_NDELAY|O_NONBLOCK)) == 0) {
			/*
			 * Sit in a poll()
			 */
			struct pollfd	fds[1];

			fds[0].fd = s;
			fds[0].events = POLLIN;
			fds[0].revents = 0;
			for (;;) {
				if (poll(fds, 1L, -1) < 0)
					return (-1);
				if (fds[0].revents != 0)
					break;
			}
		} else	{
			errno = EAGAIN;
			return (-1);
		}
	}
}

/*
 * Common receive code.
 */
int
_s_soreceivexx(siptr, flags, buf, len, from, fromlen, accrights, accrightslen,
		rcvmaskp)
	struct _si_user		*siptr;
	register int		flags;
	char			*buf;
	int			len;
	register struct sockaddr *from;
	register int		*fromlen;
	char			*accrights;
	int			*accrightslen;
	sigset_t		*rcvmaskp;
{
	register int			fmode;
	register int			s;
	sigset_t			mask;
	struct strbuf			ctlbuf;
	struct strbuf			rcvbuf;
	int				flg;
	int so_state = siptr->udata.so_state;
	int servtype;
	int didalloc = 0;

	errno = 0;
	servtype = siptr->udata.servtype;
	if (so_state & SS_CANTRCVMORE) {
		int bytes;

		/*
		 * Make sure there's nothing enqueued on the
		 * streamhead.  It's possible for sockmod to return
		 * so_state & SS_CANTRCVMORE on a SI_GETUDATA while
		 * there is still something enqueued on the
		 * streamhead.  In that case, just ignore the flag.
		 *
		 * XXX  Maybe make sockmod keep track of messages on
		 * the stream head, or mask SS_CANTRCVMORE since it
		 * seems to be managed by libsocket anyway?
		 */
		if ((ioctl(siptr->fd, I_NREAD, (char *) &bytes)) <= 0) {
			errno = 0;
			return (0);
		}
		/* One or more messages at stream head */
		siptr->udata.so_state &= ~SS_CANTRCVMORE;
	}

	if (servtype == T_COTS || servtype == T_COTS_ORD) {
		if ((so_state & SS_ISCONNECTED) == 0) {
			if (_s_synch(siptr) < 0)
				return (-1);
			/* _s_synch might have modified errno */
			errno = 0;

			so_state = siptr->udata.so_state;
			servtype = siptr->udata.servtype;
			if ((so_state & SS_ISCONNECTED) == 0) {
				errno = ENOTCONN;
				return (-1);
			}
		}
	}

	if ((so_state & SS_ISBOUND) == 0) {
		/*
		 * Need to bind it for TLI.
		 */
		if (__bind(siptr, NULL, 0, NULL, NULL) < 0)
			return (-1);
	}

	s = siptr->fd;

	if (len == 0 && accrightslen == 0)
		return (0);

	rcvbuf.buf = buf;

	ctlbuf.buf = NULL;
tryagain:
	rcvbuf.maxlen = len;
	rcvbuf.len = 0;

	ctlbuf.len = 0;
	if (ctlbuf.buf == NULL) {
		/*
		 * first entry (not through tryagain: label)
		 * acquire space for ctlbuf
		 */
		if (siptr->ctlbuf) {
			ctlbuf.maxlen = siptr->ctlsize;
			ctlbuf.buf = siptr->ctlbuf;
			siptr->ctlbuf = NULL;
		} else {
			/*
			 * siptr->ctlbuf is in use
			 * allocate and free after use.
			 */
			if ((ctlbuf.maxlen = _s_cbuf_alloc(siptr,
						&ctlbuf.buf)) < 0)
				goto rcvout;
			didalloc = 1;
		}
	}

	if (flags & MSG_OOB) {
		int rval;
		/*
		 * Handles the case when MSG_PEEK is set
		 * or not.
		 * Note: it is safe to block signals here. Ioctl does not
		 * block if no oob data is available. It returns EWOULDBLOCK
		 * in sockmod.
		 */
		_s_blockallsignals(&mask);
		rval = _s_do_ioctl(s, rcvbuf.buf, rcvbuf.maxlen, flags,
					&rcvbuf.len);
		_s_restoresigmask(&mask);

		if (! rval)
			goto rcvout;
		SOCKDEBUG(siptr, "lrecvmsg: Got %d bytes OOB data\n",
						rcvbuf.len);
	} else if (flags & MSG_PEEK) {
		fmode = s_getfflags(siptr);

		if (msgpeek(s, &ctlbuf, &rcvbuf, fmode) < 0) {
			if (errno == EBADMSG) {
				errno = 0;
				rcvbuf.len = recvaccrights(siptr, buf,
				    len, from, fromlen, accrights,
				    accrightslen, fmode);
			}
			goto rcvout;
		}
	} else	{
		int 	retval;

		flg = 0;

		/*
		 * Historical note: There used to be a comment here about
		 * having to prevent spurious SIGPOLL and code to call
		 * _s_blockallsignals for every call to getmsg. The
		 * original comment read:
		 * "Have to prevent spurious SIGPOLL signals
		 * which can be caused by the mechanism used
		 * to cause a SIGURG."
		 *
		 * Note: To enable signals to happen in MT case, we
		 * drop the lock and enable signals for the blocking
		 * getmsg call and acquire it again when done.
		 */
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, *rcvmaskp);
		if ((retval = getmsg(s, &ctlbuf, &rcvbuf, &flg)) < 0) {
			int saved_errno = errno;
			MUTEX_LOCK_SIGMASK(&siptr->lock, *rcvmaskp);
			fmode = s_getfflags(siptr);
			if (saved_errno == EBADMSG) {

				errno = 0;
				rcvbuf.len = recvaccrights(siptr, buf,
				    len, from, fromlen, accrights,
				    accrightslen, fmode);
			}
			goto rcvout;
		}

		if (servtype == T_CLTS) {
			struct strbuf	databuf;
			char		dbuf[256];

			while (retval & MOREDATA) {
				/*
				 * Discard all the extra data.
				 */
				databuf.maxlen = sizeof (dbuf);
				databuf.len = 0;
				databuf.buf = dbuf;
				if ((retval = getmsg(s, NULL,
					    &databuf, &flg)) < 0) {
					errno = EIO;
					goto rcvout;
				}
				if (retval & MORECTL) {
					/*
					 * There shouldn't be any control part.
					 */
					errno = EIO;
					goto rcvout;
				}
			}
		}
		MUTEX_LOCK_SIGMASK(&siptr->lock, *rcvmaskp);
	}

	if (rcvbuf.len == -1)
		rcvbuf.len = 0;

	if (ctlbuf.len == sizeof (struct T_exdata_ind) &&
				*(long *)ctlbuf.buf == T_EXDATA_IND &&
				rcvbuf.len == 0) {
		/*
		 * Must be the message indicating the position
		 * of urgent data in the data stream - the user
		 * should not see this.
		 */
		if (flags & MSG_PEEK) {
			/*
			 * Better make sure it goes.
			 */
			flg = 0;
			_s_blockallsignals(&mask);
			(void) getmsg(s, &ctlbuf, &rcvbuf, &flg);
			_s_restoresigmask(&mask);
		}
		goto tryagain;
	}


	/*
	 * Copy in source address if requested.
	 */
rcvout:
	if (errno == 0 && from && *fromlen) {
		if (servtype == T_CLTS) {
			if (ctlbuf.len != 0) {
				register struct T_unitdata_ind *udata_ind;

				udata_ind = (struct T_unitdata_ind *)ctlbuf.buf;
				*fromlen = _s_cpaddr(siptr,
					(char *)from,
					*fromlen,
					udata_ind->SRC_offset + ctlbuf.buf,
					udata_ind->SRC_length);

			}
		} else	{
			/*
			 * The ctlbuf is not free at this point so there
			 * is malloc/free cost in the __getpeername call
			 * which can be avoided if we can avoid doing this call
			 */
			if (__getpeername(siptr, from, fromlen) < 0) {
				errno = 0;
				*fromlen = 0;
			}
		}
	}
	if (didalloc)
		free(ctlbuf.buf);
	else
		siptr->ctlbuf = ctlbuf.buf;

	if (errno)
		return (-1);
	return (rcvbuf.len);
}

/*
 * Common send code.
 */
int
_s_sosendxx(siptr, flags, buf, len, to, tolen, accrights, accrightslen,
		sndmaskp)
	struct _si_user		*siptr;
	register int		flags;
	char			*buf;
	int			len;
	register struct sockaddr *to;
	register int		tolen;
	char			*accrights;
	int			accrightslen;
	sigset_t		*sndmaskp;
{
	register int		s;
	register int		retval;
	struct strbuf		ctlbuf;
	struct strbuf		databuf;
	int so_state = siptr->udata.so_state;
	int servtype;
	int didalloc = 0;

	errno = 0;
	if (so_state & SS_CANTSENDMORE) {
		(void) kill(getpid(), SIGPIPE);
		errno = EPIPE;
		return (-1);
	}

	servtype = siptr->udata.servtype;
	s = siptr->fd;
	if ((servtype == T_CLTS && tolen <= 0) || servtype != T_CLTS) {
		if ((so_state & SS_ISCONNECTED) == 0) {
			if (_s_synch(siptr) < 0)
				return (-1);
			/* _s_synch might have modified errno */
			errno = 0;

			so_state = siptr->udata.so_state;
			servtype = siptr->udata.servtype;
			if ((so_state & SS_ISCONNECTED) == 0) {
				if (servtype == T_CLTS)
					errno = EDESTADDRREQ;
				else	errno = ENOTCONN;
				return (-1);
			}
		}
	}

	if ((so_state & SS_ISBOUND) == 0) {
		/*
		 * Need to bind it for TLI.
		 */
		if (__bind(siptr, NULL, 0, NULL, NULL) < 0)
			return (-1);
	}

	if (flags & MSG_DONTROUTE) {
		int	val;

		val = 1;
		if (__setsockopt(siptr, SOL_SOCKET, SO_DONTROUTE, (char *)&val,
				sizeof (val)) < 0)
			return (-1);
	}

	/*
	 * Access rights only in UNIX domain.
	 */
	if (accrightslen) {
		if (siptr->udata.sockparams.sp_family != AF_UNIX) {
			errno = EOPNOTSUPP;
			goto sndout_nobuf;
		}
	}

	if (flags & MSG_OOB) {
		/*
		 * If the socket is SOCK_DGRAM or
		 * AF_UNIX which we know is not to support
		 * MSG_OOB or the TP does not support the
		 * notion of expedited data then we fail.
		 *
		 * Otherwise we hope that the TP knows
		 * what to do.
		 */
		if (siptr->udata.sockparams.sp_family == AF_UNIX ||
				servtype == T_CLTS ||
				siptr->udata.etsdusize == 0) {
			errno = EOPNOTSUPP;
			goto sndout_nobuf;
		}
	}
	/*
	 * acquire ctlbuf for use
	 */
	ctlbuf.len = 0;
	if (siptr->ctlbuf) {
		ctlbuf.buf = siptr->ctlbuf;
		siptr->ctlbuf = NULL;
		ctlbuf.maxlen = siptr->ctlsize;
	} else {
		/*
		 * siptr->ctlbuf is in use
		 * allocate and free after use.
		 */
		if ((ctlbuf.maxlen = _s_cbuf_alloc(siptr,
						&ctlbuf.buf)) < 0)
			return (-1);
		didalloc = 1;
	}


	if (servtype == T_CLTS) {
		register struct T_unitdata_req	*udata_req;
		register char			*tmpbuf;
		register int			tmpcnt;
		struct ux_dev			ux_dev;

		if (len < 0 || len > siptr->udata.tidusize) {
			errno = EMSGSIZE;
			goto sndout;
		}

		if ((so_state & SS_ISCONNECTED) == 0) {
			switch (siptr->udata.sockparams.sp_family) {
			case AF_INET:
				if (tolen != sizeof (struct sockaddr_in))
					errno = EINVAL;
				break;

			case AF_UNIX: {
				struct stat		rstat;
				struct sockaddr_un	*un;
				int			i;

				if (tolen <= 0)
					break;

				un = (struct sockaddr_un *)to;

				if (tolen > sizeof (*un) ||
				    (i = _s_uxpathlen(un)) ==
				    sizeof (un->sun_path)) {
					errno = EINVAL;
					goto sndout;
				}
				un->sun_path[i] = 0;

				/*
				 * Substitute the user supplied address with the
				 * one that will have actually got bound to.
				 */
				if (un->sun_family != AF_UNIX) {
					errno = EINVAL;
					goto sndout;
				}

				/*
				 * stat the file.
				 */
#if defined(i386)
				if (_xstat(_STAT_VER, (caddr_t)un->sun_path,
						&rstat) < 0)
#elif defined(sparc)
				if (_stat((caddr_t)un->sun_path, &rstat) < 0)
#else
#error Undefined architecture
#endif
					goto sndout;

				if ((rstat.st_mode & S_IFMT) != S_IFIFO) {
					errno = ENOTSOCK;
					goto sndout;
				}

				(void) memset((caddr_t)&ux_dev, 0,
						sizeof (ux_dev));
				ux_dev.dev = rstat.st_dev;
				ux_dev.ino = rstat.st_ino;

				to = (struct sockaddr *)&ux_dev;
				tolen = sizeof (ux_dev);
				break;
			}

			default:
				if (tolen > siptr->udata.addrsize)
					errno = EINVAL;
				break;
			}
			if (errno)
				goto sndout;
		}

		if (accrightslen) {
			retval = send_clts_rights(siptr, buf, len, to,
						tolen, accrights, accrightslen);
			goto sndout;
		}

		tmpbuf = ctlbuf.buf;
		udata_req = (struct T_unitdata_req *)tmpbuf;
		udata_req->PRIM_type = T_UNITDATA_REQ;
		udata_req->DEST_length = _s_min(tolen, siptr->udata.addrsize);
		udata_req->DEST_offset = 0;
		udata_req->OPT_length = 0;
		udata_req->OPT_offset = 0;
		tmpcnt = sizeof (*udata_req);

		if (udata_req->DEST_length) {
			_s_aligned_copy(tmpbuf, udata_req->DEST_length, tmpcnt,
				(char *)to, (int *)&udata_req->DEST_offset);
			tmpcnt += udata_req->DEST_length;
		}

		ctlbuf.len = tmpcnt;

		databuf.len = len == 0 ? -1 : len;
		databuf.buf = buf;

		/*
		 * Calls to send data ( putmsg) can potentially
		 * block, for MT case, we drop the lock and enable signals here
		 * and aquire it back.
		 */
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, *sndmaskp);
		if (putmsg(s, &ctlbuf, &databuf, 0) >= 0) {
			/* Not error */
			retval = databuf.len == -1 ? 0 : databuf.len;
		}
		MUTEX_LOCK_SIGMASK(&siptr->lock, *sndmaskp);
		goto sndout;
	} else	{
		register struct T_data_req	*data_req;
		register int			tmp;
		register int			tmpcnt;
		register int			error;
		char				*tmpbuf;

		if (accrightslen) {
			retval = send_cots_rights(siptr, buf, len,
						accrights, accrightslen);
			goto sndout;
		}

		/*
		 * Calls to send data (write or putmsg) can potentially
		 * block, for MT case, we drop the lock and enable signals here
		 * and acquire it back
		 */
		MUTEX_UNLOCK_SIGMASK(&siptr->lock, *sndmaskp);

		data_req = (struct T_data_req *) ctlbuf.buf;

		ctlbuf.len = sizeof (*data_req);

		tmp = len;
		tmpbuf = buf;
		do {
			if (siptr->udata.tsdusize != 0 || (flags & MSG_OOB)) {
				/*
				 * transport provider supports TSDU concept
				 * (unlike TCP) or it is expedited data.
				 * In this case do the fragmentation
				 */
				if (flags & MSG_OOB)
					data_req->PRIM_type = T_EXDATA_REQ;
				else
					data_req->PRIM_type = T_DATA_REQ;
				if (tmp <= siptr->udata.tidusize) {
					data_req->MORE_flag = 0;
					tmpcnt = tmp;
				} else {
					data_req->MORE_flag = 1;
					tmpcnt = siptr->udata.tidusize;
				}
				databuf.len = tmpcnt;
				databuf.buf = tmpbuf;
				error = putmsg(s, &ctlbuf, &databuf, 0);
			} else {
				/*
				 * transport provider does *not* support TSDU
				 * concept (e.g. TCP) and it is not expedited
				 * data. A perf. optimization is used.
				 */
				tmpcnt = tmp;
				error = write(s, tmpbuf, tmpcnt);
				if (error != tmpcnt && error >= 0) {
					/* Amount that was actually sent */
					tmpcnt = error;
				}
			}
			if (error < 0) {
				if (len == tmp) {
					MUTEX_LOCK_SIGMASK(&siptr->lock,
						*sndmaskp);
					goto sndout;
				} else	{
					errno = 0;
					retval = len - tmp;
					MUTEX_LOCK_SIGMASK(&siptr->lock,
						*sndmaskp);
					goto sndout;
				}
			}
			tmp -= tmpcnt;
			tmpbuf += tmpcnt;
		} while (tmp);
		retval = len - tmp;
		MUTEX_LOCK_SIGMASK(&siptr->lock, *sndmaskp);
	}
sndout:
	if (didalloc)
		free(ctlbuf.buf);
	else
		siptr->ctlbuf = ctlbuf.buf;

sndout_nobuf:
	if (flags & MSG_DONTROUTE) {
		int	val;

		val = 0;
		(void) __setsockopt(siptr, SOL_SOCKET, SO_DONTROUTE,
			(char *)&val, sizeof (val));
	}
	if (errno) {
		if (errno == ENXIO || errno == EIO)
			errno = EPIPE;
		return (-1);
	}
	return	(retval);
}

static int
send_clts_rights(siptr, buf, len, to, tolen, accrights, accrightslen)
	struct _si_user		*siptr;
	char			*buf;
	int			len;
	register struct sockaddr *to;
	register int		tolen;
	char			*accrights;
	int			accrightslen;
{
	int				pipefd[2];
	int				*rights;
	int				retval;
	int				i;
	struct sockaddr_un		un_addr;
	int				addrlen;
	sigset_t			mask;
	int				saved_errno;


	/*
	 * Make sending of access rigts atomic with respect to signals
	 */
	_s_blockallsignals(&mask);

	/*
	 * Get a pipe.
	 */
	if (pipe(pipefd) < 0) {
		_s_restoresigmask(&mask);
		return (-1);
	}

	/*
	 * Link the transport.
	 */
	if (_s_do_ioctl(siptr->fd, (char *)to, tolen,
				SI_TCL_LINK, NULL) == 0)  {
		retval = -1;
		goto send_clts_done;
	}

	/*
	 * Send one end of the pipe.
	 */
	if (_ioctl(siptr->fd, I_SENDFD, pipefd[1]) < 0) {
		retval = -1;
		goto send_clts_done;
	}

	/*
	 * Send the fd's.
	 */
	SOCKDEBUG(siptr, "send_clts_rights: nmbr of fd's %d\n",
				accrightslen/sizeof (int));
	rights = (int *)accrights;
	for (i = 0; i < accrightslen/sizeof (int); i++) {
		if (_ioctl(pipefd[0], I_SENDFD, rights[i]) < 0) {
			retval = -1;
			goto send_clts_done;
		}
	}

	/*
	 * Send our address.
	 */
	(void) memset((caddr_t)&un_addr, 0, sizeof (un_addr));
	addrlen = sizeof (un_addr);
	if (__getsockname(siptr, (struct sockaddr *)&un_addr, &addrlen) < 0)
		goto send_clts_done;

	addrlen = sizeof (un_addr);
	if (write(pipefd[0], (caddr_t)&un_addr, addrlen) != addrlen) {
		errno = EPROTO;
		goto send_clts_done;
	}

	/*
	 * Send the data.
	 */
	if (len) {
		if (write(pipefd[0], buf, len) != len) {
			retval = -1;
			goto send_clts_done;
		} else	retval = len;
	}

	/*
	 * Unlink the transport.
	*/
send_clts_done:
	saved_errno = errno;
	(void) _s_do_ioctl(siptr->fd, NULL, 0, SI_TCL_UNLINK, NULL);
	(void) close(pipefd[0]);
	(void) close(pipefd[1]);

	_s_restoresigmask(&mask);

	errno = saved_errno;
	return (retval);
}

static
send_cots_rights(siptr, buf, len, accrights, accrightslen)
	struct _si_user		*siptr;
	char			*buf;
	int			len;
	char			*accrights;
	int			accrightslen;
{
	int				pipefd[2];
	ulong				intransit;
	int				*fds;
	int				retval;
	int				i;
	sigset_t			mask;

	/*
	 * Make sending of access rigts atomic with respect to signals
	 */
	_s_blockallsignals(&mask);
	/*
	 * Get a pipe.
	 */
	if (pipe(pipefd) < 0) {
		_s_restoresigmask(&mask);
		return (-1);
	}

	/*
	 * Ensure nothing in progress.
	 */
	intransit = 0;
	for (;;) {
		if (_s_do_ioctl(siptr->fd, (caddr_t)&intransit,
				sizeof (intransit), SI_GETINTRANSIT, &i) == 0) {
			retval = -1;
			goto send_cots_done;
		}
		if (i != sizeof (intransit)) {
			errno = EPROTO;
			retval = -1;
			goto send_cots_done;
		}
		if (intransit != 0)
			(void) sleep(1);
		else	break;
	}

	/*
	 * Send pipe fd.
	 */
	if (_ioctl(siptr->fd, I_SENDFD, pipefd[1]) < 0) {
		retval = -1;
		goto send_cots_done;
	}

	/*
	 * Send the fd's.
	 */
	fds = (int *)accrights;
	for (i = 0; i < accrightslen/sizeof (int); i++) {
		if (_ioctl(pipefd[0], I_SENDFD, fds[i]) < 0) {
			retval = -1;
			goto send_cots_done;
		}
	}
	/*
	 * Send the data.
	 */
	if (write(pipefd[0], buf, len) != len) {
		errno = EPROTO;
		retval = -1;
		goto send_cots_done;
	}
	retval = len;

send_cots_done:
	(void) close(pipefd[0]);
	(void) close(pipefd[1]);

	if (retval >= 0)
		errno = 0;
	_s_restoresigmask(&mask);

	return (retval);
}

/*
 * Copy data to output buffer and align it as in input buffer
 * This is to ensure that if the user wants to align a network
 * addr on a non-word boundry then it will happen.
 */
void
_s_aligned_copy(buf, len, init_offset, datap, rtn_offset)
	register char	*buf;
	register char	*datap;
	register int	*rtn_offset;
	register int	len;
	register int	init_offset;
{
		*rtn_offset = ROUNDUP(init_offset) + ((unsigned int)datap&0x03);
		(void) memcpy((caddr_t)(buf + *rtn_offset), datap, (int)len);
}


/*
 * Max - return max between two ints
 */
int
_s_max(x, y)
	int	x;
	int	y;
{
	if (x > y)
		return (x);
	else	return (y);
}

int
_s_min(x, y)
	int	x;
	int	y;
{
	if (x < y)
		return (x);
	else	return (y);
}


/*
 * Wait for T_OK_ACK
 */
int
_s_is_ok(siptr, type, ctlbufp)
	register struct _si_user	*siptr;
	long				type;
	struct strbuf			*ctlbufp;
{

	register union T_primitives	*pptr;
	int				flags;
	int				retval;
	int				fmode;

	fmode = s_getfflags(siptr);
	if (fmode & (O_NDELAY|O_NONBLOCK)) {
		(void) _fcntl(siptr->fd, F_SETFL,
				fmode & ~(O_NDELAY|O_NONBLOCK));
		s_invalfflags(siptr);
	}

	ctlbufp->len = 0;
	flags = RS_HIPRI;

	while ((retval = getmsg(siptr->fd, ctlbufp, NULL, &flags)) < 0) {
		if (errno == EINTR)
			continue;
		return (0);
	}

	/*
	 * Did I get entire message
	 */
	if (retval) {
		errno = EIO;
		return (0);
	}

	/*
	 * Is ctl part large enough to determine type?
	 */
	if (ctlbufp->len < sizeof (long)) {
		errno = EPROTO;
		return (0);
	}

	if (fmode & (O_NDELAY|O_NONBLOCK)) {
		(void) _fcntl(siptr->fd, F_SETFL, fmode);
		s_invalfflags(siptr);
	}
	pptr = (union T_primitives *)ctlbufp->buf;
	switch (pptr->type) {
		case T_OK_ACK:
			if ((ctlbufp->len < sizeof (struct T_ok_ack)) ||
			    (pptr->ok_ack.CORRECT_prim != type)) {
				errno = EPROTO;
				return (0);
			}
			return (1);

		case T_ERROR_ACK:
			if ((ctlbufp->len < sizeof (struct T_error_ack)) ||
			    (pptr->error_ack.ERROR_prim != type)) {
				errno = EPROTO;
				return (0);
			}
			if (pptr->error_ack.TLI_error == TSYSERR)
				errno = pptr->error_ack.UNIX_error;
			else	errno = tlitosyserr(pptr->error_ack.TLI_error);
			return (0);

		default:
			errno = EPROTO;
			return (0);
	}
}
/*
 * Translate a TLI error into a system error as best we can.
 */
static ushort	tli_errs[] = {
		0,		/* no error		*/
		EADDRNOTAVAIL,	/* TBADADDR		*/
		ENOPROTOOPT,	/* TBADOPT		*/
		EACCES,		/* TACCES		*/
		EBADF,		/* TBADF		*/
		EADDRNOTAVAIL,	/* TNOADDR		*/
		EPROTO,		/* TOUTSTATE		*/
		EPROTO,		/* TBADSEQ		*/
		0,		/* TSYSERR		*/
		EPROTO,		/* TLOOK		*/
		EMSGSIZE,	/* TBADDATA		*/
		EMSGSIZE,	/* TBUFOVFLW		*/
		EPROTO,		/* TFLOW		*/
		EWOULDBLOCK,	/* TNODATA		*/
		EPROTO,		/* TNODIS		*/
		EPROTO,		/* TNOUDERR		*/
		EINVAL,		/* TBADFLAG		*/
		EPROTO,		/* TNOREL		*/
		EOPNOTSUPP,	/* TNOTSUPPORT		*/
		EPROTO,		/* TSTATECHNG		*/
};

static int
tlitosyserr(terr)
	register int	terr;
{
	if (terr > (sizeof (tli_errs) / sizeof (ushort)))
		return (EPROTO);
	else	return (tli_errs[terr]);
}


/*
 * sockmod ioctl
 */
int
_s_do_ioctl(s, buf, size, cmd, retlen)
	register char		*buf;
	register int		*retlen;
{
	register int		retval;
	struct strioctl		strioc;

	strioc.ic_cmd = cmd;
	strioc.ic_timout = -1;
	strioc.ic_len = size;
	strioc.ic_dp = buf;

	if ((retval = _ioctl(s, I_STR, &strioc)) < 0) {
		/*
		 * Map the errno as appropriate.
		 */
		switch (errno) {
			case ENOTTY:
			case ENODEV:
				errno = ENOTSOCK;
				break;

			case EINVAL:
				break;

			case ENXIO:
				errno = EPIPE;

			default:
				break;
		}
		return (0);
	}

	if (retval) {
		if ((retval & 0xff) == TSYSERR)
			errno = (retval >>  8) & 0xff;
		else	{
			errno = tlitosyserr(retval & 0xff);
		}
		return (0);
	}
	if (retlen)
		*retlen = strioc.ic_len;
	return (1);
}

/*
 * Allocate buffers
 * Assumes that the caller is holding siptr->lock.
 * Also assumes caller has blocked signals (for safe
 * malloc/free operations)
 */
static int
_s_alloc_bufs(siptr)
	register struct _si_user	*siptr;
{
	unsigned			size2;
	register struct si_udata	*udata = &siptr->udata;

	size2 = sizeof (union T_primitives) + udata->addrsize + sizeof (long) +
			udata->optsize + sizeof (long);

	if (size2 == siptr->ctlsize)
		return (0);

	if (siptr->ctlbuf != NULL)
		free(siptr->ctlbuf);

	if ((siptr->ctlbuf = malloc(size2)) == NULL) {
		errno = ENOMEM;
		return (-1);
	}

	siptr->ctlsize = size2;
	return (0);
}

/*
 * Assumes caller has blocked signals (for safe
 * malloc/free operations)
 */
int
_s_cbuf_alloc(siptr, retbuf)
	register struct _si_user	*siptr;
	char				**retbuf;
{
	unsigned			size2;
	register struct si_udata	*udata = &siptr->udata;

	size2 = sizeof (union T_primitives) + udata->addrsize + sizeof (long) +
			udata->optsize + sizeof (long);

	if ((*retbuf = malloc(size2)) == NULL) {
		errno = ENOMEM;
		return (-1);
	}
	return (size2);
}

void
_s_close(siptr)
	register struct _si_user	*siptr;

{
	register int			fd;

	fd = siptr->fd;
	_s_free(siptr);
	(void) close(fd);
}

/*
 * Like _s_close but does not close the file descriptor.
 */
static void
_s_free(struct _si_user	*siptr)
{
	register int			fd;
	sigset_t			mask;

	fd = siptr->fd;
	MUTEX_LOCK_PROCMASK(&_si_userlock, mask);
	free(siptr->ctlbuf);
	(void) delete_silink(fd);
	MUTEX_UNLOCK_PROCMASK(&_si_userlock, mask);
}

/*
 * Link manipulation routines.
 *
 * NBUCKETS hash buckets are used to give fast
 * access. The hashing is based on the descripter
 * number. The number of hashbuckets assumes a
 * maximum of about 100 descripters => a maximum
 * of 10 entries per bucket.
 */

#define	NBUCKETS	10
static struct _si_user		*hash_bucket[NBUCKETS];

/*
 * Allocates a new link and returns a pointer to it.
 * Assumes that the caller is holding _si_userlock.
 * and has signals blocked even when not linked to
 * libthread
 */
static struct _si_user *
add_silink(s)
	register int			s;
{
	register struct _si_user	*siptr;
	register struct _si_user	*prevptr;
	register struct _si_user	*curptr;
	register int			x;

	x = s % NBUCKETS;
	if (hash_bucket[x] != NULL) {
		/*
		 * Walk along the bucket looking for
		 * duplicate entry or the end.
		 */
		for (curptr = hash_bucket[x]; curptr != NULL;
						curptr = curptr->next) {
			if (curptr->fd == s) {
				/*
				 * This can happen when the user has close(2)'ed
				 * a descripter and then been allocated it again
				 * via socket().
				 *
				 * We will re-use the existing _si_user struct
				 * in this case rather than allocating a
				 * new one.  If there is a ctlbuf
				 * associated with the existing _si_user
				 * struct, we keep it for now and
				 * si_alloc_bufs checks that it has the correct
				 * size.
				 *
				 * Clear the flags fields. The udata field
				 * will be set by the caller.
				 */
				curptr->flags = 0;
				s_invalfflags(curptr);
				return (curptr);
			}
			prevptr = curptr;
		}
		if ((siptr = (struct _si_user *)malloc(sizeof (*siptr))) ==
		    NULL)
			return (NULL);

		/*
		 * Link in the new one.
		 */
		prevptr->next = siptr;
		siptr->prev = prevptr;
		siptr->next = NULL;

	} else	{
		if ((siptr = (struct _si_user *)malloc(sizeof (*siptr))) ==
		    NULL)
			return (NULL);
		/*
		 * First entry.
		 */
		hash_bucket[x] = siptr;
		siptr->next = NULL;
		siptr->prev = NULL;
	}
	/* Initialize values. udata will be set by the caller. */
	siptr->fd = s;
	siptr->ctlsize = 0;
	siptr->ctlbuf = NULL;
	siptr->flags = 0;
	s_invalfflags(siptr);
#ifdef _REENTRANT
	mutex_init(&siptr->lock, USYNC_THREAD, NULL);
#endif /* _REENTRANT */
	return (siptr);
}

/*
 * Find a link by descriptor, holding _si_userlock while doing so.
 */
struct _si_user *
find_silink(s)
	register int		s;
{
	struct _si_user		*siptr;
	sigset_t		mask;

	MUTEX_LOCK_SIGMASK(&_si_userlock, mask);
	siptr = find_silink_unlocked(s);
	MUTEX_UNLOCK_SIGMASK(&_si_userlock, mask);

	return (siptr);
}

/*
 * Find a link by descriptor
 * Assumes that the caller is holding _si_userlock.
 */
static struct _si_user *
find_silink_unlocked(s)
	register int			s;
{
	register struct _si_user	*curptr;
	register int			x;

	if (s >= 0) {
		x = s % NBUCKETS;
		if (hash_bucket[x] != NULL) {
			/*
			 * Walk along the bucket looking for
			 * the descripter.
			 */
			for (curptr = hash_bucket[x]; curptr != NULL;
							curptr = curptr->next) {
				if (curptr->fd == s)
					return (curptr);
			}
		}
	}
	errno = EINVAL;
	return (NULL);
}

/*
 * Assumes that the caller is holding _si_userlock.
 * Also assumes all signals are blocked even when not
 * linked to libthread
 */
static int
delete_silink(s)
	register int			s;
{
	register struct _si_user	*curptr;
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
						curptr = curptr->next) {
			if (curptr->fd == s) {
				register struct _si_user	*nextptr;
				register struct _si_user	*prevptr;

				nextptr = curptr->next;
				prevptr = curptr->prev;
				if (prevptr)
					prevptr->next = nextptr;
				else
					hash_bucket[x] = nextptr;
				if (nextptr)
					nextptr->prev = prevptr;

#ifdef _REENTRANT
				mutex_destroy(&curptr->lock);
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
 * Return the number of bytes in the UNIX
 * pathname, not including the null terminator
 * (if any).
 */
int
_s_uxpathlen(un)
	register struct sockaddr_un	*un;
{
	register int			i;

	for (i = 0; i < sizeof (un->sun_path); i++)
		if (un->sun_path[i] == NULL)
			return (i);
	return (sizeof (un->sun_path));
}

int
_s_cpaddr(siptr, to, tolen, from, fromlen)
	register struct _si_user	*siptr;
	register char			*to;
	register int			tolen;
	register char			*from;
	register int			fromlen;
{
	(void) memset(to, 0, tolen);
	if (siptr->udata.sockparams.sp_family == AF_INET) {
		if (tolen > sizeof (struct sockaddr_in))
			tolen = sizeof (struct sockaddr_in);
	} else	if (tolen > fromlen)
			tolen = fromlen;
	(void) memcpy(to, from, _s_min(fromlen, tolen));
	return (tolen);
}

void
_s_blockallsignals(maskp)
	sigset_t	*maskp;
{
	sigset_t	new;

	(void) _sigfillset(&new);
	(void) _sigprocmask(SIG_BLOCK, &new, maskp);
}

void
_s_restoresigmask(maskp)
	sigset_t *maskp;
{
	(void) _sigprocmask(SIG_SETMASK, maskp, (sigset_t *)NULL);
}
