/*
 * Copyright (c) 1986-1995 by Sun Microsystems Inc.
 */

#ident	"@(#)svc_vc.c	1.30	95/04/13 SMI"

/*
 * svc_vc.c, Server side for Connection Oriented RPC.
 *
 * Actually implements two flavors of transporter -
 * a rendezvouser (a listener and connection establisher)
 * and a record stream.
 */

#include "rpc_mt.h"
#include <stdio.h>
#include <rpc/rpc.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mkdev.h>
#include <sys/poll.h>
#include <syslog.h>
#include <rpc/nettype.h>
#include <tiuser.h>
#include <string.h>
#include <stropts.h>
#include "svc_mt.h"

#ifndef MIN
#define	MIN(a, b)	(((a) < (b)) ? (a) : (b))
#endif

extern int	__td_setnodelay();
extern int	t_getname();
extern bool_t	abort();
extern char	*t_errlist[];
extern void 	*malloc(), free();

static struct xp_ops 	*svc_vc_ops();
static struct xp_ops 	*svc_vc_rendezvous_ops();
static void		svc_vc_destroy();
static int 		read_vc();
static int 		write_vc();
static SVCXPRT 		*makefd_xprt();

struct cf_rendezvous { /* kept in xprt->xp_p1 for rendezvouser */
	u_int sendsize;
	u_int recvsize;
	struct t_call *t_call;
	struct t_bind *t_bind;
	long cf_tsdu;
	int tcp_flag;
};

struct cf_conn {	/* kept in xprt->xp_p1 for actual connection */
	u_int sendsize;
	u_int recvsize;
	enum xprt_stat strm_stat;
	u_long x_id;
	long cf_tsdu;
	XDR xdrs;
	char verf_body[MAX_AUTH_BYTES];
};

static int t_rcvall();
extern int __xdrrec_setfirst();
extern int __xdrrec_resetfirst();
extern int __is_xdrrec_first();
/*
 * This is intended as a performance improvement on the old string handling
 * stuff by read only moving data into the  text segment.
 * Format = <routine> : <error>
 */

static const char errstring[] = " %s : %s";

/* Routine names */

static const char svc_vc_create_str[] = "svc_vc_create";
static const char svc_fd_create_str[] = "svc_fd_create";
static const char makefd_xprt_str[] = "svc_vc_create: makefd_xprt ";
static const char rendezvous_request_str[] = "rendezvous_request";
static const char do_accept_str[] = "do_accept";

/* error messages */

static const char no_mem_str[] = "out of memory";
static const char no_tinfo_str[] = "could not get transport information";

void
svc_vc_xprtfree(xprt)
	SVCXPRT			*xprt;
{
	SVCXPRT_EXT		*xt = xprt ? SVCEXT(xprt) : NULL;
	struct cf_rendezvous	*r = xprt ?
				    (struct cf_rendezvous *)xprt->xp_p1 : NULL;

	if (!xprt)
		return;

	if (xprt->xp_tp)
		free(xprt->xp_tp);
	if (xprt->xp_netid)
		free(xprt->xp_netid);
	if (xt && (xt->parent == NULL)) {
		if (xprt->xp_ltaddr.buf)
			free(xprt->xp_ltaddr.buf);
		if (xprt->xp_rtaddr.buf)
			free(xprt->xp_rtaddr.buf);
	}
	if (r) {
		if (r->t_call)
			t_free((char *)r->t_call, T_CALL);
		if (r->t_bind)
			t_free((char *)r->t_bind, T_BIND);
		free((char *)r);
	}
	svc_xprt_free(xprt);
}

/*
 * Usage:
 *	xprt = svc_vc_create(fd, sendsize, recvsize);
 * Since connection streams do buffered io similar to stdio, the caller
 * can specify how big the send and receive buffers are. If recvsize
 * or sendsize are 0, defaults will be chosen.
 * fd should be open and bound.
 */
SVCXPRT *
svc_vc_create_private(fd, sendsize, recvsize)
	register int fd;
	u_int sendsize;
	u_int recvsize;
{
	register struct cf_rendezvous *r;
	SVCXPRT *xprt;
	struct t_info tinfo;

	trace4(TR_svc_vc_create, 0, fd, sendsize, recvsize);
	if ((xprt = svc_xprt_alloc()) == (SVCXPRT *)NULL) {
		(void) syslog(LOG_ERR, errstring,
		svc_vc_create_str, no_mem_str);
		trace2(TR_svc_vc_create, 1, fd);
		return ((SVCXPRT *)NULL);
	}
	svc_flags(xprt) |= SVC_RENDEZVOUS;

	r = (struct cf_rendezvous *)mem_alloc(sizeof (*r));
	if (r == (struct cf_rendezvous *)NULL) {
		(void) syslog(LOG_ERR, errstring,
			svc_vc_create_str, no_mem_str);
		svc_vc_xprtfree(xprt);
		trace2(TR_svc_vc_create, 1, fd);
		return ((SVCXPRT *)NULL);
	}
	if (t_getinfo(fd, &tinfo) == -1) {
		(void) syslog(LOG_ERR, errstring,
			svc_vc_create_str, no_tinfo_str);
		svc_vc_xprtfree(xprt);
		trace2(TR_svc_vc_create, 1, fd);
		return ((SVCXPRT *)NULL);
	}
	/*
	 * Find the receive and the send size
	 */
	r->sendsize = __rpc_get_t_size((int)sendsize, tinfo.tsdu);
	r->recvsize = __rpc_get_t_size((int)recvsize, tinfo.tsdu);
	if ((r->sendsize == 0) || (r->recvsize == 0)) {
		syslog(LOG_ERR,
		"svc_vc_create:  transport does not support data transfer");
		svc_vc_xprtfree(xprt);
		trace2(TR_svc_vc_create, 1, fd);
		return ((SVCXPRT *)NULL);
	}

	r->t_call = (struct t_call *) t_alloc(fd, T_CALL, T_ADDR | T_OPT);
	if (r->t_call == NULL) {
		(void) syslog(LOG_ERR, errstring,
			svc_vc_create_str, no_mem_str);
		svc_vc_xprtfree(xprt);
		trace2(TR_svc_vc_create, 1, fd);
		return ((SVCXPRT *)NULL);
	}

	r->t_bind = (struct t_bind *) t_alloc(fd, T_BIND, T_ADDR);
	if (r->t_bind == (struct t_bind *) NULL) {
		(void) syslog(LOG_ERR, errstring,
			svc_vc_create_str, no_mem_str);
		svc_vc_xprtfree(xprt);
		trace2(TR_svc_vc_create, 1, fd);
		return ((SVCXPRT *) NULL);
	}

	r->cf_tsdu = tinfo.tsdu;
	r->tcp_flag = FALSE;
	xprt->xp_fd = fd;
	xprt->xp_p1 = (caddr_t)r;
	xprt->xp_p2 = NULL;
	xprt->xp_verf = _null_auth;
	xprt->xp_ops = svc_vc_rendezvous_ops();

	trace2(TR_svc_vc_create, 1, fd);
	return (xprt);
}

SVCXPRT *
svc_vc_create(fd, sendsize, recvsize)
	register int fd;
	u_int sendsize;
	u_int recvsize;
{
	SVCXPRT *xprt;

	if ((xprt = svc_vc_create_private(fd, sendsize, recvsize)) != NULL)
		xprt_register(xprt);
	return (xprt);
}

SVCXPRT *
svc_vc_xprtcopy(parent)
	SVCXPRT			*parent;
{
	SVCXPRT			*xprt;
	struct cf_rendezvous	*r, *pr;
	int			fd = parent->xp_fd;

	if ((xprt = svc_xprt_alloc()) == NULL)
		return (NULL);

	SVCEXT(xprt)->parent = parent;
	SVCEXT(xprt)->flags = SVCEXT(parent)->flags;

	xprt->xp_fd = fd;
	xprt->xp_ops = svc_vc_rendezvous_ops();
	if (parent->xp_tp)
		xprt->xp_tp = (char *) strdup(parent->xp_tp);
	if (parent->xp_netid)
		xprt->xp_netid = (char *) strdup(parent->xp_netid);

	/*
	 * can share both local and remote address
	 */
	xprt->xp_ltaddr = parent->xp_ltaddr;
	xprt->xp_rtaddr = parent->xp_rtaddr; /* XXX - not used for rendezvous */
	xprt->xp_type = parent->xp_type;
	xprt->xp_verf = parent->xp_verf;

	if ((r = (struct cf_rendezvous *) malloc(sizeof (*r))) == NULL) {
		svc_vc_xprtfree(xprt);
		return (NULL);
	}
	xprt->xp_p1 = (caddr_t)r;
	pr = (struct cf_rendezvous *)parent->xp_p1;
	r->sendsize = pr->sendsize;
	r->recvsize = pr->recvsize;
	r->cf_tsdu = pr->cf_tsdu;
	r->tcp_flag = pr->tcp_flag;
	r->t_call = (struct t_call *) t_alloc(fd, T_CALL, T_ADDR | T_OPT);
	if (r->t_call == NULL) {
		svc_vc_xprtfree(xprt);
		return (NULL);
	}
	r->t_bind = (struct t_bind *) t_alloc(fd, T_BIND, T_ADDR);
	if (r->t_bind == NULL) {
		svc_vc_xprtfree(xprt);
		return (NULL);
	}

	return (xprt);
}

/*
 * XXX : Used for setting flag to indicate that this is TCP
 */

int
__svc_vc_setflag(xprt, flag)
	SVCXPRT *xprt;
	int flag;
{
	struct cf_rendezvous *r;

	r = (struct cf_rendezvous *) xprt->xp_p1;
	r->tcp_flag = TRUE;
	return (1);
}

/*
 * used for the actual connection.
 */
SVCXPRT *
svc_fd_create_private(fd, sendsize, recvsize)
	int fd;
	u_int sendsize;
	u_int recvsize;
{
	struct t_info tinfo;
	SVCXPRT *dummy;
	struct t_bind *tres = NULL;	/* bind info */

	trace4(TR_svc_fd_create, 0, fd, sendsize, recvsize);
	if (t_getinfo(fd, &tinfo) == -1) {
		(void) syslog(LOG_ERR, errstring,
			svc_fd_create_str,
			no_tinfo_str);
		trace2(TR_svc_fd_create, 1, fd);
		return ((SVCXPRT *)NULL);
	}
	/*
	 * Find the receive and the send size
	 */
	sendsize = __rpc_get_t_size((int)sendsize, tinfo.tsdu);
	recvsize = __rpc_get_t_size((int)recvsize, tinfo.tsdu);
	if ((sendsize == 0) || (recvsize == 0)) {
		syslog(LOG_ERR, errstring, svc_fd_create_str,
			"transport does not support data transfer");
		trace2(TR_svc_fd_create, 1, fd);
		return ((SVCXPRT *)NULL);
	}
	dummy = makefd_xprt(fd, sendsize, recvsize, tinfo.tsdu);
	/* Assign the local bind address */
	tres = (struct t_bind *)t_alloc(fd, T_BIND, T_ADDR);
	if (tres == NULL) {
		(void) syslog(LOG_ERR, "svc_fd_create: No memory!");
		trace2(TR_svc_fd_create, 1, fd);
		return ((SVCXPRT *)NULL);
	}
	if (t_getname(fd, tres, LOCALNAME) == -1)
		tres->addr.len = 0;
	dummy->xp_ltaddr = tres->addr;
	tres->addr.buf = NULL;
	(void) t_free((char *)tres, T_BIND);
	/* Fill in type of service */
	dummy->xp_type = tinfo.servtype;
	trace2(TR_svc_fd_create, 1, fd);
	return (dummy);
}

SVCXPRT *
svc_fd_create(fd, sendsize, recvsize)
	register int fd;
	u_int sendsize;
	u_int recvsize;
{
	SVCXPRT *xprt;

	if ((xprt = svc_fd_create_private(fd, sendsize, recvsize)) != NULL)
		xprt_register(xprt);
	return (xprt);
}

void
svc_fd_xprtfree(xprt)
	SVCXPRT		*xprt;
{
	SVCXPRT_EXT	*xt = xprt ? SVCEXT(xprt) : NULL;
	struct cf_conn	*cd = xprt ? (struct cf_conn *)xprt->xp_p1 : NULL;

	if (!xprt)
		return;

	if (xprt->xp_tp)
		free(xprt->xp_tp);
	if (xprt->xp_netid)
		free(xprt->xp_netid);
	if (xt && (xt->parent == NULL)) {
		if (xprt->xp_ltaddr.buf)
			free(xprt->xp_ltaddr.buf);
		if (xprt->xp_rtaddr.buf)
			free(xprt->xp_rtaddr.buf);
	}
	if (cd) {
		XDR_DESTROY(&(cd->xdrs));
		free((char *)cd);
	}
	if (xt && (xt->parent == NULL) && xprt->xp_p2) {
		free((caddr_t)((struct netbuf *)xprt->xp_p2)->buf);
		free((caddr_t)xprt->xp_p2);
	}
	svc_xprt_free(xprt);
}

static SVCXPRT *
makefd_xprt(fd, sendsize, recvsize, tsdu)
	int fd;
	u_int sendsize;
	u_int recvsize;
	long tsdu;
{
	register SVCXPRT *xprt;
	register struct cf_conn *cd;
	extern rwlock_t svc_fd_lock;

	trace5(TR_makefd_xprt, 0, fd, sendsize, recvsize, tsdu);
	xprt = svc_xprt_alloc();
	if (xprt == (SVCXPRT *)NULL) {
		(void) syslog(LOG_ERR, errstring, makefd_xprt_str, no_mem_str);
		trace2(TR_makefd_xprt, 1, fd);
		return ((SVCXPRT *)NULL);
	}
	svc_flags(xprt) |= SVC_CONNECTION;

	cd = (struct cf_conn *)mem_alloc(sizeof (struct cf_conn));
	if (cd == (struct cf_conn *)NULL) {
		(void) syslog(LOG_ERR, errstring, makefd_xprt_str, no_mem_str);
		svc_fd_xprtfree(xprt);
		trace2(TR_makefd_xprt, 1, fd);
		return ((SVCXPRT *)NULL);
	}
	cd->sendsize = sendsize;
	cd->recvsize = recvsize;
	cd->strm_stat = XPRT_IDLE;
	cd->cf_tsdu = tsdu;
	xdrrec_create(&(cd->xdrs), sendsize, 0, (caddr_t)xprt,
			(int(*)())NULL, write_vc);

	rw_wrlock(&svc_fd_lock);
	if (svc_xdrs == NULL) {
		svc_xdrs = (XDR **) mem_alloc((FD_SETSIZE + 1) *
							sizeof (XDR *));
		memset(svc_xdrs, 0, (FD_SETSIZE + 1) * sizeof (XDR *));
	}
	if (svc_xdrs[fd] != NULL) {
		XDR_DESTROY(svc_xdrs[fd]);
	} else if ((svc_xdrs[fd] = (XDR *) mem_alloc(sizeof (XDR))) == NULL) {
		(void) syslog(LOG_ERR, errstring, makefd_xprt_str, no_mem_str);
		svc_fd_xprtfree(xprt);
		trace2(TR_makefd_xprt, 1, fd);
		rw_unlock(&svc_fd_lock);
		return ((SVCXPRT *)NULL);
	}
	memset(svc_xdrs[fd], 0, sizeof (XDR));
	xdrrec_create(svc_xdrs[fd], 0, recvsize, (caddr_t)xprt,
			read_vc, (int(*)())NULL);
	rw_unlock(&svc_fd_lock);

	xprt->xp_p1 = (caddr_t)cd;
	xprt->xp_p2 = NULL;
	xprt->xp_verf.oa_base = cd->verf_body;
	xprt->xp_ops = svc_vc_ops();	/* truely deals with calls */
	xprt->xp_fd = fd;
	trace2(TR_makefd_xprt, 1, fd);
	return (xprt);
}

SVCXPRT *
svc_fd_xprtcopy(parent)
	SVCXPRT			*parent;
{
	SVCXPRT			*xprt;
	struct cf_conn		*cd, *pcd;

	if ((xprt = svc_xprt_alloc()) == NULL)
		return (NULL);

	SVCEXT(xprt)->parent = parent;
	SVCEXT(xprt)->flags = SVCEXT(parent)->flags;

	xprt->xp_fd = parent->xp_fd;
	xprt->xp_ops = svc_vc_ops();
	if (parent->xp_tp)
		xprt->xp_tp = (char *) strdup(parent->xp_tp);
	if (parent->xp_netid)
		xprt->xp_netid = (char *) strdup(parent->xp_netid);
	/*
	 * share local and remote addresses with parent
	 */
	xprt->xp_ltaddr = parent->xp_ltaddr;
	xprt->xp_rtaddr = parent->xp_rtaddr;
	xprt->xp_type = parent->xp_type;

	if ((cd = (struct cf_conn *) malloc(sizeof (struct cf_conn))) == NULL) {
		svc_fd_xprtfree(xprt);
		return (NULL);
	}
	pcd = (struct cf_conn *)parent->xp_p1;
	cd->sendsize = pcd->sendsize;
	cd->recvsize = pcd->recvsize;
	cd->strm_stat = pcd->strm_stat;
	cd->x_id = pcd->x_id;
	cd->cf_tsdu = pcd->cf_tsdu;
	xdrrec_create(&(cd->xdrs), cd->sendsize, 0, (caddr_t)xprt,
			(int(*)())NULL, write_vc);
	xprt->xp_verf.oa_base = cd->verf_body;
	xprt->xp_p1 = (char *)cd;
	xprt->xp_p2 = parent->xp_p2;	/* shared */

	return (xprt);
}

/*
 * This routine is called by svc_getreqset(), when a packet is recd.
 * The listener process creates another end point on which the actual
 * connection is carried. It returns FALSE to indicate that it was
 * not a rpc packet (falsely though), but as a side effect creates
 * another endpoint which is also registered, which then always
 * has a request ready to be served.
 */
/* ARGSUSED1 */
static bool_t
rendezvous_request(xprt, msg)
	register SVCXPRT *xprt;
	struct rpc_msg *msg; /* needed for ANSI-C typechecker */
{
	struct cf_rendezvous *r;
	char *tpname = NULL;
	char devbuf[256];
	static void do_accept();

	trace1(TR_rendezvous_request, 0);
	r = (struct cf_rendezvous *) xprt->xp_p1;

again:
	switch (t_look(xprt->xp_fd)) {
	case T_DISCONNECT:
		(void) t_rcvdis(xprt->xp_fd, NULL);
		trace1(TR_rendezvous_request, 1);
		return (FALSE);

	case T_LISTEN:

		if (t_listen(xprt->xp_fd, r->t_call) == -1) {
			if ((t_errno == TSYSERR) && (errno == EINTR))
				goto again;

			if (t_errno == TLOOK) {
				if (t_look(xprt->xp_fd) == T_DISCONNECT)
				    (void) t_rcvdis(xprt->xp_fd, NULL);
			}
			trace1(TR_rendezvous_request, 1);
			return (FALSE);
		}
		break;
	default:
		trace1(TR_rendezvous_request, 1);
		return (FALSE);
	}
	/*
	 * Now create another endpoint, and accept the connection
	 * on it.
	 */

	if (xprt->xp_tp) {
		tpname = xprt->xp_tp;
	} else {
		/*
		 * If xprt->xp_tp is NULL, then try to extract the
		 * transport protocol information from the transport
		 * protcol corresponding to xprt->xp_fd
		 */
		struct netconfig *nconf;
		tpname = devbuf;
		if ((nconf = __rpcfd_to_nconf(xprt->xp_fd, xprt->xp_type))
				== NULL) {
			(void) syslog(LOG_ERR, errstring,
					rendezvous_request_str,
					"no suitable transport");
			goto err;
		}
		strcpy(tpname, nconf->nc_device);
		freenetconfigent(nconf);
	}

	do_accept(xprt->xp_fd, tpname, xprt->xp_netid, r->t_call, r);

err:
	trace1(TR_rendezvous_request, 1);
	return (FALSE); /* there is never an rpc msg to be processed */
}

static void
do_accept(srcfd, tpname, netid, tcp, r)
int	srcfd;
char	*tpname, *netid;
struct t_call	*tcp;
struct cf_rendezvous	*r;
{
	int	destfd;
	struct t_call	t_call;
	struct t_call	*tcp2 = (struct t_call *) NULL;
	struct t_info	tinfo;
	SVCXPRT	*xprt = (SVCXPRT *) NULL;

	trace1(TR_do_accept, 0);

	destfd = t_open(tpname, O_RDWR, &tinfo);
	if (destfd == -1) {
		if (t_errno == TSYSERR && errno == EMFILE)
			(void) syslog(LOG_ERR, errstring, do_accept_str,
				(const char *) "too many open files");
		else
			(void) syslog(LOG_ERR, errstring, do_accept_str,
				(const char *) "can't open connection");
		(void) t_snddis(srcfd, tcp);
		trace1(TR_do_accept, 1);
		return;
	} else if (destfd < 256) {
		int nfd;

		nfd = fcntl(destfd, F_DUPFD, 256);
		if (nfd != -1) {
			if (t_close(destfd) == -1) {
				(void) syslog(LOG_ERR,
		"could not t_close() old fd %d; mem & fd leak error: %s",
						destfd, t_errlist[t_errno]);
			}
			destfd = nfd;
			if (t_sync(destfd) == -1) {
				(void) t_snddis(srcfd, tcp);
				(void) syslog(LOG_ERR,
				"could not t_sync() duped fd %d: error: %s",
						destfd, t_errlist[t_errno]);
				return;
			}
		}
	}
	(void) fcntl(destfd, F_SETFD, 1); /* make it "close on exec" */
	if ((tinfo.servtype != T_COTS) && (tinfo.servtype != T_COTS_ORD)) {
		/* Not a connection oriented mode */
		(void) syslog(LOG_ERR, errstring, do_accept_str,
				"do_accept:  illegal transport");
		(void) t_close(destfd);
		(void) t_snddis(srcfd, tcp);
		trace1(TR_do_accept, 1);
		return;
	}


	if (t_bind(destfd, (struct t_bind *) NULL, r->t_bind) == -1) {
		(void) syslog(LOG_ERR, errstring, do_accept_str,
			"t_bind failed");
		(void) t_close(destfd);
		(void) t_snddis(srcfd, tcp);
		trace1(TR_do_accept, 1);
		return;
	}

	if (r->tcp_flag)	/* if TCP, set NODELAY flag */
		__td_setnodelay(destfd);

	/*
	 * This connection is not listening, hence no need to set
	 * the qlen.
	 */

	/*
	 * XXX: The local transport chokes on its own listen
	 * options so we zero them for now
	 */
	t_call = *tcp;
	t_call.opt.len = 0;
	t_call.opt.maxlen = 0;
	t_call.opt.buf = (char *) NULL;

	while (t_accept(srcfd, destfd, &t_call) == -1) {
		switch (t_errno) {
		case TLOOK:
again:
			switch (t_look(srcfd)) {
			case T_CONNECT:
			case T_DATA:
			case T_EXDATA:
				/* this should not happen */
				break;

			case T_DISCONNECT:
				(void) t_rcvdis(srcfd,
					(struct t_discon *) NULL);
				break;

			case T_LISTEN:
				if (tcp2 == (struct t_call *) NULL)
					tcp2 = (struct t_call *) t_alloc(srcfd,
					    T_CALL, T_ADDR | T_OPT);
				if (tcp2 == (struct t_call *) NULL) {

					(void) t_close(destfd);
					(void) t_snddis(srcfd, tcp);
					trace1(TR_do_accept, 1);
					return;
				}
				if (t_listen(srcfd, tcp2) == -1) {
					switch (t_errno) {
					case TSYSERR:
						if (errno == EINTR)
							goto again;
						break;

					case TLOOK:
						goto again;
					}
					(void) t_free((char *)tcp2, T_CALL);
					(void) t_close(destfd);
					(void) t_snddis(srcfd, tcp);
					trace1(TR_do_accept, 1);
					return;
					/* NOTREACHED */
				}
				do_accept(srcfd, tpname, netid, tcp2, r);
				(void) t_free((char *) tcp2, T_CALL);
				tcp2 = (struct t_call *) NULL;
				break;

			case T_ORDREL:
				(void) t_rcvrel(srcfd);
				(void) t_sndrel(srcfd);
				break;
			}
			break;

		case TBADSEQ:
			/*
			 * This can happen if the remote side has
			 * disconnected before the connection is
			 * accepted.  In this case, a disconnect
			 * should not be sent on srcfd (important!
			 * the listening fd will be hosed otherwise!).
			 * This error is not logged since this is an
			 * operational situation that is recoverable.
			 */
			(void) t_close(destfd);
			trace1(TR_do_accept, 1);
			return;
			/* NOTREACHED */

		case TOUTSTATE:
			/*
			 * This can happen if the t_rcvdis() or t_rcvrel()/
			 * t_sndrel() put srcfd into the T_IDLE state.
			 */
			if (t_getstate(srcfd) == T_IDLE) {
				(void) t_close(destfd);
				(void) t_snddis(srcfd, tcp);
				trace1(TR_do_accept, 1);
				return;
			}
			/* else FALL THROUGH TO */

		default:
			(void) syslog(LOG_ERR,
			"cannot accept connection:  %s (current state %d)",
			t_errlist[t_errno], t_getstate(srcfd));
			(void) t_close(destfd);
			(void) t_snddis(srcfd, tcp);
			trace1(TR_do_accept, 1);
			return;
			/* NOTREACHED */
		}
	}

	/*
	 * make a new transporter
	 */
	xprt = makefd_xprt(destfd, r->sendsize, r->recvsize, r->cf_tsdu);
	if (xprt == (SVCXPRT *) NULL) {
		(void) t_close(destfd);
		trace1(TR_do_accept, 1);
		return;
	}

	/*
	 * Copy the new local and remote bind information
	 */

	xprt->xp_rtaddr.len = tcp->addr.len;
	xprt->xp_rtaddr.maxlen = tcp->addr.len;
	xprt->xp_rtaddr.buf = malloc(tcp->addr.len);
	memcpy(xprt->xp_rtaddr.buf, tcp->addr.buf, tcp->addr.len);

	xprt->xp_tp = strdup(tpname);
	xprt->xp_netid = strdup(netid);
	if ((xprt->xp_tp == (char *) NULL) ||
	    (xprt->xp_netid == (char *) NULL)) {
		(void) syslog(LOG_ERR, errstring,
			do_accept_str, no_mem_str);
		if (xprt)
			svc_vc_destroy(xprt);
		(void) t_close(destfd);
		trace1(TR_do_accept, 1);
		return;
	}
	if (tcp->opt.len > 0) {
		struct netbuf *netptr;

		xprt->xp_p2 = malloc(sizeof (struct netbuf));

		if (xprt->xp_p2 != (char *) NULL) {
			netptr = (struct netbuf *) xprt->xp_p2;

			netptr->len = tcp->opt.len;
			netptr->maxlen = tcp->opt.len;
			netptr->buf = malloc(tcp->opt.len);
			memcpy(netptr->buf, tcp->opt.buf, tcp->opt.len);
		}
	}
/*	(void) ioctl(destfd, I_POP, (char *) NULL);    */

	xprt_register(xprt);
	trace1(TR_do_accept, 1);
}

/* ARGSUSED */
static enum xprt_stat
rendezvous_stat(xprt)
	SVCXPRT *xprt; /* needed for ANSI-C typechecker */
{
	trace1(TR_rendezvous_stat, 0);
	trace1(TR_rendezvous_stat, 1);
	return (XPRT_IDLE);
}

static void
svc_vc_destroy(xprt)
	register SVCXPRT *xprt;
{
	trace1(TR_svc_vc_destroy, 0);
	mutex_lock(&svc_mutex);
	_svc_vc_destroy_private(xprt);
	mutex_unlock(&svc_mutex);
	trace1(TR_svc_vc_destroy, 1);
}

void
_svc_vc_destroy_private(xprt)
	register SVCXPRT *xprt;
{
	if (svc_mt_mode != RPC_SVC_MT_NONE) {
		if (SVCEXT(xprt)->parent)
			xprt = SVCEXT(xprt)->parent;
		svc_flags(xprt) |= SVC_DEFUNCT;
		if (SVCEXT(xprt)->refcnt > 0)
			return;
	}

	xprt_unregister(xprt);
	t_close(xprt->xp_fd);

	if (svc_mt_mode != RPC_SVC_MT_NONE) {
		svc_xprt_destroy(xprt);
	} else {
		if (svc_type(xprt) == SVC_RENDEZVOUS)
			svc_vc_xprtfree(xprt);
		else
			svc_fd_xprtfree(xprt);
	}
}

static bool_t
svc_vc_control(xprt, rq, in)
	register SVCXPRT *xprt;
	const u_int	rq;
	void		*in;
{
	trace3(TR_svc_vc_control, 0, xprt, rq);
	switch (rq) {
	default:
		trace1(TR_svc_vc_control, 1);
		return (FALSE);
	}
}

/*
 * All read operations timeout after 35 seconds.
 * A timeout is fatal for the connection.
 */
#define	WAIT_PER_TRY	35000	/* milliseconds */

/*
 * reads data from the vc conection.
 * any error is fatal and the connection is closed.
 * (And a read of zero bytes is a half closed stream => error.)
 */
static int
read_vc(xprt, buf, len)
	register SVCXPRT *xprt;
	caddr_t buf;
	register int len;
{
	register int fd = xprt->xp_fd;
	register XDR *xdrs = svc_xdrs[fd];
	struct pollfd pfd;
	int ret;

	trace2(TR_read_vc, 0, len);

	/*
	 * Make sure the connection is not already dead.
	 */
	if (svc_failed(xprt)) {
		trace1(TR_read_vc, 1);
		return (-1);
	}

	if (!__is_xdrrec_first(xdrs)) {

		pfd.fd = fd;
		pfd.events = POLLIN | POLLPRI | POLLRDNORM | POLLRDBAND;

		do {
			if ((ret = poll(&pfd, 1, WAIT_PER_TRY)) <= 0) {
				/*
				 * If errno is EINTR, ERESTART, or EAGAIN
				 * ignore error and repeat poll
				 */
				if (ret < 0 && (errno == EINTR ||
				    errno == ERESTART || errno == EAGAIN))
					continue;
				goto fatal_err;
			}
		} while (pfd.revents == 0);
		if (pfd.revents & POLLNVAL)
			goto fatal_err;
	}
	__xdrrec_resetfirst(xdrs);
	if ((len = t_rcvall(fd, buf, len)) > 0) {
		trace1(TR_read_vc, 1);
		return (len);
	}

fatal_err:
	((struct cf_conn *)(xprt->xp_p1))->strm_stat = XPRT_DIED;
	svc_flags(xprt) |= SVC_FAILED;
	trace1(TR_read_vc, 1);
	return (-1);
}

/*
 * Receive the required bytes of data, even if it is fragmented.
 */
static int
t_rcvall(fd, buf, len)
	int fd;
	char *buf;
	int len;
{
	int flag;
	int final = 0;
	int res;

	trace3(TR_t_rcvall, 0, fd, len);
	do {
		res = t_rcv(fd, buf, (unsigned)len, &flag);
		if (res == -1) {
			if (t_errno == TLOOK) {
				switch (t_look(fd)) {
				case T_DISCONNECT:
					t_rcvdis(fd, NULL);
					break;
				case T_ORDREL:
					t_rcvrel(fd);
					(void) t_sndrel(fd);
					break;
				default:
					break;
				}
			}
			break;
		}
		final += res;
		buf += res;
		len -= res;
	} while (len && (flag & T_MORE));
	trace2(TR_t_rcvall, 1, fd);
	return (res == -1 ? -1 : final);
}

/*
 * writes data to the vc connection.
 * Any error is fatal and the connection is closed.
 */
static int
write_vc(xprt, buf, len)
	register SVCXPRT *xprt;
	caddr_t buf;
	int len;
{
	register int i, cnt;
	int flag;
	long maxsz;

	trace2(TR_write_vc, 0, len);
	maxsz = ((struct cf_conn *)(xprt->xp_p1))->cf_tsdu;
	if ((maxsz == 0) || (maxsz == -1)) {
		if ((len = t_snd(xprt->xp_fd, buf, (unsigned)len,
				(int)0)) == -1) {
			if (t_errno == TLOOK) {
				switch (t_look(xprt->xp_fd)) {
				case T_DISCONNECT:
					t_rcvdis(xprt->xp_fd, NULL);
					break;
				case T_ORDREL:
					t_rcvrel(xprt->xp_fd);
					(void) t_sndrel(xprt->xp_fd);
					break;
				default:
					break;
				}
			}
			((struct cf_conn *)(xprt->xp_p1))->strm_stat
					= XPRT_DIED;
			svc_flags(xprt) |= SVC_FAILED;
		}
		trace1(TR_write_vc, 1);
		return (len);
	}

	/*
	 * This for those transports which have a max size for data.
	 */
	for (cnt = len, i = 0; cnt > 0; cnt -= i, buf += i) {
		flag = cnt > maxsz ? T_MORE : 0;
		if ((i = t_snd(xprt->xp_fd, buf,
			(unsigned)MIN(cnt, maxsz), flag)) == -1) {
			if (t_errno == TLOOK) {
				switch (t_look(xprt->xp_fd)) {
				case T_DISCONNECT:
					t_rcvdis(xprt->xp_fd, NULL);
					break;
				case T_ORDREL:
					t_rcvrel(xprt->xp_fd);
					break;
				default:
					break;
				}
			}
			((struct cf_conn *)(xprt->xp_p1))->strm_stat
					= XPRT_DIED;
			svc_flags(xprt) |= SVC_FAILED;
			trace1(TR_write_vc, 1);
			return (-1);
		}
	}
	trace1(TR_write_vc, 1);
	return (len);
}

static enum xprt_stat
svc_vc_stat(xprt)
	SVCXPRT *xprt;
{
	SVCXPRT *parent = SVCEXT(xprt)->parent ? SVCEXT(xprt)->parent : xprt;

	trace1(TR_svc_vc_stat, 0);
	if (svc_failed(parent) || svc_failed(xprt)) {
		trace1(TR_svc_vc_stat, 1);
		return (XPRT_DIED);
	}
	if (! xdrrec_eof(svc_xdrs[xprt->xp_fd])) {
		trace1(TR_svc_vc_stat, 1);
		return (XPRT_MOREREQS);
	}
	/*
	 * xdrrec_eof could have noticed that the connection is dead, so
	 * check status again.
	 */
	if (svc_failed(parent) || svc_failed(xprt)) {
		trace1(TR_svc_vc_stat, 1);
		return (XPRT_DIED);
	}
	trace1(TR_svc_vc_stat, 1);
	return (XPRT_IDLE);
}



static bool_t
svc_vc_recv(xprt, msg)
	SVCXPRT *xprt;
	register struct rpc_msg *msg;
{
	register struct cf_conn *cd = (struct cf_conn *)(xprt->xp_p1);
	register XDR *xdrs = svc_xdrs[xprt->xp_fd];

	trace1(TR_svc_vc_recv, 0);
	xdrs->x_op = XDR_DECODE;
	if (!xdrrec_skiprecord(xdrs))
		return (FALSE);

	__xdrrec_setfirst(xdrs);
	if (xdr_callmsg(xdrs, msg)) {
		cd->x_id = msg->rm_xid;
		trace1(TR_svc_vc_recv, 1);
		return (TRUE);
	}
	trace1(TR_svc_vc_recv, 1);
	return (FALSE);
}

static bool_t
svc_vc_getargs(xprt, xdr_args, args_ptr)
	SVCXPRT *xprt;
	xdrproc_t xdr_args;
	caddr_t args_ptr;
{
	bool_t dummy1;

	trace1(TR_svc_vc_getargs, 0);

	dummy1 = (*xdr_args)(svc_xdrs[xprt->xp_fd], args_ptr);
	if (svc_mt_mode != RPC_SVC_MT_NONE)
		svc_args_done(xprt);
	trace1(TR_svc_vc_getargs, 1);
	return (dummy1);
}

static bool_t
svc_vc_freeargs(xprt, xdr_args, args_ptr)
	SVCXPRT *xprt;
	xdrproc_t xdr_args;
	caddr_t args_ptr;
{
	register XDR *xdrs = &(((struct cf_conn *)(xprt->xp_p1))->xdrs);
	bool_t dummy2;

	trace1(TR_svc_vc_freeargs, 0);
	xdrs->x_op = XDR_FREE;
	dummy2 = (*xdr_args)(xdrs, args_ptr);
	trace1(TR_svc_vc_freeargs, 1);
	return (dummy2);
}

static bool_t
svc_vc_reply(xprt, msg)
	SVCXPRT *xprt;
	register struct rpc_msg *msg;
{
	register struct cf_conn *cd = (struct cf_conn *)(xprt->xp_p1);
	register XDR *xdrs = &(cd->xdrs);
	register bool_t stat;

	trace1(TR_svc_vc_reply, 0);

#ifdef __lock_lint
	mutex_lock(&svc_send_mutex(SVCEXT(xprt)->parent));
#else
	if (svc_mt_mode != RPC_SVC_MT_NONE)
		mutex_lock(&svc_send_mutex(SVCEXT(xprt)->parent));
#endif

	xdrs->x_op = XDR_ENCODE;
	msg->rm_xid = cd->x_id;
	stat = xdr_replymsg(xdrs, msg);
	(void) xdrrec_endofrecord(xdrs, TRUE);

#ifdef __lock_lint
	mutex_unlock(&svc_send_mutex(SVCEXT(xprt)->parent));
#else
	if (svc_mt_mode != RPC_SVC_MT_NONE)
		mutex_unlock(&svc_send_mutex(SVCEXT(xprt)->parent));
#endif

	trace1(TR_svc_vc_reply, 1);
	return (stat);
}

static struct xp_ops *
svc_vc_ops()
{
	static struct xp_ops ops;
	extern mutex_t ops_lock;

/* VARIABLES PROTECTED BY ops_lock: ops */

	trace1(TR_svc_vc_ops, 0);
	mutex_lock(&ops_lock);
	if (ops.xp_recv == NULL) {
		ops.xp_recv = svc_vc_recv;
		ops.xp_stat = svc_vc_stat;
		ops.xp_getargs = svc_vc_getargs;
		ops.xp_reply = svc_vc_reply;
		ops.xp_freeargs = svc_vc_freeargs;
		ops.xp_destroy = svc_vc_destroy;
		ops.xp_control = svc_vc_control;
	}
	mutex_unlock(&ops_lock);
	trace1(TR_svc_vc_ops, 1);
	return (&ops);
}

static struct xp_ops *
svc_vc_rendezvous_ops()
{
	static struct xp_ops ops;
	extern mutex_t ops_lock;

	trace1(TR_svc_vc_rendezvous_ops, 0);
	mutex_lock(&ops_lock);
	if (ops.xp_recv == NULL) {
		ops.xp_recv = rendezvous_request;
		ops.xp_stat = rendezvous_stat;
		ops.xp_getargs = abort;
		ops.xp_reply = abort;
		ops.xp_freeargs = abort,
		ops.xp_destroy = svc_vc_destroy;
		ops.xp_control = svc_vc_control;
	}
	mutex_unlock(&ops_lock);
	trace1(TR_svc_vc_rendezvous_ops, 1);
	return (&ops);
}

/*
 * PRIVATE RPC INTERFACE
 *
 * This is a hack to let NIS+ clean up connections that have already been
 * closed.  This problem arises because rpc.nisd forks a child to handle
 * existing connections when it does checkpointing.  The child may close
 * some of these connections.  But the descriptors still stay open in the
 * parent, and because TLI descriptors don't support persistent EOF
 * condition (like sockets do), the parent will never detect that these
 * descriptors are dead.
 *
 * The following internal procedure __svc_nisplus_fdcleanup_hack() - should
 * be removed as soon as rpc.nisd is rearchitected to do the right thing.
 * This procedure should not find its way into any header files.
 *
 * This procedure should be called only when rpc.nisd knows that there
 * are no children servicing clients.
 */

#include <sys/timod.h>

extern SVCXPRT **svc_xports;
extern int _t_do_ioctl();

static bool_t
fd_is_dead(fd)
	int fd;
{
	struct T_info_ack inforeq;
	int retval;

	inforeq.PRIM_type = T_INFO_REQ;
	if (!_t_do_ioctl(fd, (caddr_t)&inforeq, sizeof (struct T_info_req),
						TI_GETINFO, &retval))
		return (TRUE);
	if (retval != sizeof (struct T_info_ack))
		return (TRUE);

	switch (inforeq.CURRENT_state) {
	case TS_UNBND:
	case TS_IDLE:
		return (TRUE);
	default:
		break;
	}
	return (FALSE);
}

void
__svc_nisplus_fdcleanup_hack()
{
	SVCXPRT *xprt;
	SVCXPRT *dead_xprt[FD_SETSIZE];
	int i;

	extern rwlock_t svc_fd_lock;

	rw_rdlock(&svc_fd_lock);
	if (svc_xports == NULL) {
		rw_unlock(&svc_fd_lock);
		return;
	}
	for (i = 0; i < FD_SETSIZE; i++) {
		dead_xprt[i] = NULL;
		if ((xprt = svc_xports[i]) == NULL)
			continue;
		if (svc_type(xprt) != SVC_CONNECTION)
			continue;
		if (fd_is_dead(i))
			dead_xprt[i] = xprt;
	}
	rw_unlock(&svc_fd_lock);

	for (i = 0; i < FD_SETSIZE; i++) {
		if (dead_xprt[i])
			SVC_DESTROY(dead_xprt[i]);
	}
}
