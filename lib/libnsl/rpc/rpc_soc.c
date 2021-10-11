
#ident	"@(#)rpc_soc.c	1.19	94/11/07 SMI"

/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 */

#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)rpc_soc.c 1.41 89/05/02 Copyr 1988 Sun Micro";
#endif

#ifdef PORTMAP
/*
 * rpc_soc.c
 *
 * The backward compatibility routines for the earlier implementation
 * of RPC, where the only transports supported were tcp/ip and udp/ip.
 * Based on berkeley socket abstraction, now implemented on the top
 * of TLI/Streams
 */

#include "rpc_mt.h"
#include <stdio.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <rpc/rpc.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netdir.h>
#include <errno.h>
#include <sys/syslog.h>
#include <rpc/pmap_clnt.h>
#include <rpc/pmap_prot.h>
#include <rpc/nettype.h>
#include <stdlib.h>

int __rpc_bindresvport();

extern mutex_t	rpcsoc_lock;

/*
 * A common clnt create routine
 */
static CLIENT *
clnt_com_create(raddr, prog, vers, sockp, sendsz, recvsz, tp)
	register struct sockaddr_in *raddr;
	u_long prog;
	u_long vers;
	int *sockp;
	u_int sendsz;
	u_int recvsz;
	char *tp;
{
	CLIENT *cl;
	int madefd = FALSE;
	int fd = *sockp;
	struct t_info tinfo;
	struct netconfig *nconf;
	int port;
	struct netbuf bindaddr;
	extern int __rpc_minfd;

	trace5(TR_clnt_com_create, 0, prog, vers, sendsz, recvsz);
	mutex_lock(&rpcsoc_lock);
	if ((nconf = __rpc_getconfip(tp)) == NULL) {
		rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
		mutex_unlock(&rpcsoc_lock);
		trace3(TR_clnt_com_create, 1, prog, vers);
		return ((CLIENT *)NULL);
	}
	if (fd == RPC_ANYSOCK) {
		fd = t_open(nconf->nc_device, O_RDWR, &tinfo);
		if (fd == -1)
			goto syserror;
		if (fd < __rpc_minfd)
			fd = __rpc_raise_fd(fd);
		madefd = TRUE;
	} else {
		if (t_getinfo(fd, &tinfo) == -1)
			goto syserror;
	}

	if (raddr->sin_port == 0) {
		u_int proto;
		u_short sport;

		mutex_unlock(&rpcsoc_lock);	/* pmap_getport is recursive */
		proto = strcmp(tp, "udp") == 0 ? IPPROTO_UDP : IPPROTO_TCP;
		sport = pmap_getport(raddr, prog, vers, proto);
		if (sport == 0) {
			goto err;
		}
		raddr->sin_port = htons(sport);
		mutex_lock(&rpcsoc_lock);	/* pmap_getport is recursive */
	}

	/* Transform sockaddr_in to netbuf */
	bindaddr.maxlen = bindaddr.len =  __rpc_get_a_size(tinfo.addr);
	bindaddr.buf = (char *)raddr;

	(void) __rpc_bindresvport(fd, (struct sockaddr_in *)NULL, &port, 0);
	cl = clnt_tli_create(fd, nconf, &bindaddr, prog, vers,
				sendsz, recvsz);
	if (cl) {
		if (madefd == TRUE) {
			/*
			 * The fd should be closed while destroying the handle.
			 */
			(void) CLNT_CONTROL(cl, CLSET_FD_CLOSE, (char *)NULL);
			*sockp = fd;
		}
		(void) freenetconfigent(nconf);
		mutex_unlock(&rpcsoc_lock);
		trace3(TR_clnt_com_create, 1, prog, vers);
		return (cl);
	}
	goto err;

syserror:
	rpc_createerr.cf_stat = RPC_SYSTEMERROR;
	rpc_createerr.cf_error.re_errno = errno;
	rpc_createerr.cf_error.re_terrno = t_errno;

err:	if (madefd == TRUE)
		(void) t_close(fd);
	(void) freenetconfigent(nconf);
	mutex_unlock(&rpcsoc_lock);
	trace3(TR_clnt_com_create, 1, prog, vers);
	return ((CLIENT *)NULL);
}

CLIENT *
clntudp_bufcreate(raddr, prog, vers, wait, sockp, sendsz, recvsz)
	register struct sockaddr_in *raddr;
	u_long prog;
	u_long vers;
	struct timeval wait;
	int *sockp;
	u_int sendsz;
	u_int recvsz;
{
	CLIENT *cl;

	trace5(TR_clntudp_bufcreate, 0, prog, vers, sendsz, recvsz);
	cl = clnt_com_create(raddr, prog, vers, sockp, sendsz, recvsz, "udp");
	if (cl == (CLIENT *)NULL) {
		trace3(TR_clntudp_bufcreate, 1, prog, vers);
		return ((CLIENT *)NULL);
	}
	(void) CLNT_CONTROL(cl, CLSET_RETRY_TIMEOUT, (char *)&wait);
	trace3(TR_clntudp_bufcreate, 1, prog, vers);
	return (cl);
}

CLIENT *
clntudp_create(raddr, program, version, wait, sockp)
	struct sockaddr_in *raddr;
	u_long program;
	u_long version;
	struct timeval wait;
	int *sockp;
{
	CLIENT *dummy;

	trace3(TR_clntudp_create, 0, program, version);
	dummy = clntudp_bufcreate(raddr, program, version, wait, sockp,
					UDPMSGSIZE, UDPMSGSIZE);
	trace3(TR_clntudp_create, 1, program, version);
	return (dummy);
}

CLIENT *
clnttcp_create(raddr, prog, vers, sockp, sendsz, recvsz)
	struct sockaddr_in *raddr;
	u_long prog;
	u_long vers;
	register int *sockp;
	u_int sendsz;
	u_int recvsz;
{
	CLIENT *dummy;

	trace5(TR_clnttcp_create, 0, prog, vers, sendsz, recvsz);
	dummy = clnt_com_create(raddr, prog, vers, sockp, sendsz,
			recvsz, "tcp");
	trace3(TR_clnttcp_create, 1, prog, vers);
	return (dummy);
}

CLIENT *
clntraw_create(prog, vers)
	u_long prog;
	u_long vers;
{
	CLIENT *dummy;

	trace3(TR_clntraw_create, 0, prog, vers);
	dummy = clnt_raw_create(prog, vers);
	trace3(TR_clntraw_create, 1, prog, vers);
	return (dummy);
}

/*
 * A common server create routine
 */
static SVCXPRT *
svc_com_create(fd, sendsize, recvsize, netid)
	register int fd;
	u_int sendsize;
	u_int recvsize;
	char *netid;
{
	struct netconfig *nconf;
	SVCXPRT *svc;
	int madefd = FALSE;
	int port;
	int res;

	trace4(TR_svc_com_create, 0, fd, sendsize, recvsize);
	if ((nconf = __rpc_getconfip(netid)) == NULL) {
		(void) syslog(LOG_ERR, "Could not get %s transport", netid);
		trace2(TR_svc_com_create, 1, fd);
		return ((SVCXPRT *)NULL);
	}
	if (fd == RPC_ANYSOCK) {
		fd = t_open(nconf->nc_device, O_RDWR, (struct t_info *)NULL);
		if (fd == -1) {
			(void) freenetconfigent(nconf);
			(void) syslog(LOG_ERR,
			"svc%s_create: could not open connection", netid);
			trace2(TR_svc_com_create, 1, fd);
			return ((SVCXPRT *)NULL);
		}
		madefd = TRUE;
	}

	res = __rpc_bindresvport(fd, (struct sockaddr_in *)NULL, &port, 8);
	svc = svc_tli_create(fd, nconf, (struct t_bind *)NULL,
				sendsize, recvsize);
	(void) freenetconfigent(nconf);
	if (svc == (SVCXPRT *)NULL) {
		if (madefd)
			(void) t_close(fd);
		trace2(TR_svc_com_create, 1, fd);
		return ((SVCXPRT *)NULL);
	}
	if (res == -1) {
		port = (((struct sockaddr_in *)svc->xp_ltaddr.buf)->sin_port);
	}
	svc->xp_port = ntohs(port);
	trace2(TR_svc_com_create, 1, fd);
	return (svc);
}

SVCXPRT *
svctcp_create(fd, sendsize, recvsize)
	register int fd;
	u_int sendsize;
	u_int recvsize;
{
	SVCXPRT *dummy;

	trace4(TR_svctcp_create, 0, fd, sendsize, recvsize);
	dummy = svc_com_create(fd, sendsize, recvsize, "tcp");
	trace4(TR_svctcp_create, 1, fd, sendsize, recvsize);
	return (dummy);
}

SVCXPRT *
svcudp_bufcreate(fd, sendsz, recvsz)
	register int fd;
	u_int sendsz, recvsz;
{
	SVCXPRT *dummy;

	trace4(TR_svcudp_bufcreate, 0, fd, sendsz, recvsz);
	dummy = svc_com_create(fd, sendsz, recvsz, "udp");
	trace4(TR_svcudp_bufcreate, 1, fd, sendsz, recvsz);
	return (dummy);
}

SVCXPRT *
svcfd_create(fd, sendsize, recvsize)
	int fd;
	u_int sendsize;
	u_int recvsize;
{
	SVCXPRT *dummy;

	trace4(TR_svcfd_create, 0, fd, sendsize, recvsize);
	dummy = svc_fd_create(fd, sendsize, recvsize);
	trace4(TR_svcfd_create, 1, fd, sendsize, recvsize);
	return (dummy);
}


SVCXPRT *
svcudp_create(fd)
	register int fd;
{
	SVCXPRT *dummy;

	trace2(TR_svcudp_create, 0, fd);
	dummy = svc_com_create(fd, UDPMSGSIZE, UDPMSGSIZE, "udp");
	trace2(TR_svcudp_create, 1, fd);
	return (dummy);
}

SVCXPRT *
svcraw_create()
{
	SVCXPRT *dummy;

	trace1(TR_svcraw_create, 0);
	dummy = svc_raw_create();
	trace1(TR_svcraw_create, 1);
	return (dummy);
}

/*
 * Bind a fd to a privileged IP port.
 * This is slightly different from the code in netdir_options
 * because it has a different interface - main thing is that it
 * needs to know its own address.  We also wanted to set the qlen.
 * t_getname() can be used for those purposes and perhaps job can be done.
 */
int
__rpc_bindresvport(fd, sin, portp, qlen)
	int fd;
	struct sockaddr_in *sin;
	int *portp;
	int qlen;
{
	int res;
	static short port;
	struct sockaddr_in myaddr;
	int i;
	struct t_bind tbindstr, *tres;
	struct t_info tinfo;
	extern mutex_t portnum_lock;

	/* VARIABLES PROTECTED BY portnum_lock: port */

#define	STARTPORT 600
#define	ENDPORT (IPPORT_RESERVED - 1)
#define	NPORTS	(ENDPORT - STARTPORT + 1)

	trace3(TR_bindresvport, 0, fd, qlen);
	if (geteuid()) {
		errno = EACCES;
		trace2(TR_bindresvport, 1, fd);
		return (-1);
	}
	if ((i = t_getstate(fd)) != T_UNBND) {
		if (t_errno == TBADF)
			errno = EBADF;
		if (i != -1)
			errno = EISCONN;
		trace2(TR_bindresvport, 1, fd);
		return (-1);
	}
	if (sin == (struct sockaddr_in *)NULL) {
		sin = &myaddr;
		get_myaddress(sin);
	} else if (sin->sin_family != AF_INET) {
		errno = EPFNOSUPPORT;
		trace2(TR_bindresvport, 1, fd);
		return (-1);
	}

	/* Transform sockaddr_in to netbuf */
	if (t_getinfo(fd, &tinfo) == -1) {
		trace2(TR_bindresvport, 1, fd);
		return (-1);
	}
	tres = (struct t_bind *)t_alloc(fd, T_BIND, T_ADDR);
	if (tres == NULL) {
		trace2(TR_bindresvport, 1, fd);
		return (-1);
	}

	tbindstr.qlen = qlen;
	tbindstr.addr.buf = (char *)sin;
	tbindstr.addr.len = tbindstr.addr.maxlen = __rpc_get_a_size(tinfo.addr);
	sin = (struct sockaddr_in *)tbindstr.addr.buf;

	res = -1;
	mutex_lock(&portnum_lock);
	if (port == 0)
		port = (getpid() % NPORTS) + STARTPORT;
	for (i = 0; i < NPORTS; i++) {
		sin->sin_port = htons(port++);
		if (port > ENDPORT)
			port = STARTPORT;
		res = t_bind(fd, &tbindstr, tres);
		if (res == 0) {
			if ((tbindstr.addr.len == tres->addr.len) &&
				(memcmp(tbindstr.addr.buf, tres->addr.buf,
					(int)tres->addr.len) == 0))
				break;
			(void) t_unbind(fd);
			res = -1;
		} else if (t_errno != TSYSERR || errno != EADDRINUSE)
			break;
	}
	mutex_unlock(&portnum_lock);

	if ((portp != NULL) && (res == 0))
		*portp = sin->sin_port;
	(void) t_free((char *)tres, T_BIND);
	trace2(TR_bindresvport, 1, fd);
	return (res);
}

/*
 * Get clients IP address.
 * don't use gethostbyname, which would invoke yellow pages
 * Remains only for backward compatibility reasons.
 * Used mainly by the portmapper so that it can register
 * with itself. Also used by pmap*() routines
 */
int
get_myaddress(addr)
	struct sockaddr_in *addr;
{
	trace1(TR_get_myaddress, 0);
	memset((char *)addr, 0, sizeof (struct sockaddr_in));
	addr->sin_port = htons(PMAPPORT);
	addr->sin_family = AF_INET;
	trace1(TR_get_myaddress, 1);
	return (0);
}

/*
 * Get port used by specified service on specified host.
 * Exists for source compatibility only.
 * Obsoleted by rpcb_getaddr().
 */
int
getrpcport(host, prognum, versnum, proto)
	char *host;
	int prognum;
	int versnum;
	int proto;
{
	struct sockaddr_in addr;
	struct hostent *hp;

	if ((hp = gethostbyname(host)) == NULL)
		return (0);
	memcpy((char *) &addr.sin_addr, hp->h_addr, hp->h_length);
	addr.sin_family = AF_INET;
	addr.sin_port =  0;
	return ((int) pmap_getport(&addr, prognum, versnum, proto));
}

/*
 * For connectionless "udp" transport. Obsoleted by rpc_call().
 */
callrpc(host, prognum, versnum, procnum, inproc, in, outproc, out)
	char *host;
	u_long prognum, versnum, procnum;
	xdrproc_t inproc, outproc;
	char *in, *out;
{
	int dummy;

	trace4(TR_callrpc, 0, prognum, versnum, procnum);
	dummy = (int)rpc_call(host, prognum, versnum, procnum, inproc,
				in, outproc, out, "udp");
	trace4(TR_callrpc, 1, prognum, versnum, procnum);
	return (dummy);
}

/*
 * For connectionless kind of transport. Obsoleted by rpc_reg()
 */
registerrpc(prognum, versnum, procnum, progname, inproc, outproc)
	u_long prognum, versnum, procnum;
	char *(*progname)();
	xdrproc_t inproc, outproc;
{
	int dummy;

	trace4(TR_registerrpc, 0, prognum, versnum, procnum);
	dummy = rpc_reg(prognum, versnum, procnum, progname, inproc,
				outproc, "udp");
	trace4(TR_registerrpc, 1, prognum, versnum, procnum);
	return (dummy);
}

/*
 * All the following clnt_broadcast stuff is convulated; it supports
 * the earlier calling style of the callback function
 */
static thread_key_t	clnt_broadcast_key;
static resultproc_t	clnt_broadcast_result_main;

/*
 * Need to translate the netbuf address into sockaddr_in address.
 * Dont care about netid here.
 */
static bool_t
rpc_wrap_bcast(resultp, addr, nconf)
	char *resultp;		/* results of the call */
	struct netbuf *addr;	/* address of the guy who responded */
	struct netconfig *nconf; /* Netconf of the transport */
{
	bool_t dummy;
	resultproc_t clnt_broadcast_result;

	trace1(TR_rpc_wrap_bcast, 0);
	if (_thr_main())
		clnt_broadcast_result = clnt_broadcast_result_main;
	else
		thr_getspecific(clnt_broadcast_key,
			(void **) &clnt_broadcast_result);
	dummy = (*clnt_broadcast_result)(resultp,
				(struct sockaddr_in *)addr->buf);
	trace1(TR_rpc_wrap_bcast, 1);
	return (dummy);
}

/*
 * Broadcasts on UDP transport. Obsoleted by rpc_broadcast().
 */
enum clnt_stat
clnt_broadcast(prog, vers, proc, xargs, argsp, xresults, resultsp, eachresult)
	u_long		prog;		/* program number */
	u_long		vers;		/* version number */
	u_long		proc;		/* procedure number */
	xdrproc_t	xargs;		/* xdr routine for args */
	caddr_t		argsp;		/* pointer to args */
	xdrproc_t	xresults;	/* xdr routine for results */
	caddr_t		resultsp;	/* pointer to results */
	resultproc_t	eachresult;	/* call with each result obtained */
{
	enum clnt_stat dummy;
	extern mutex_t tsd_lock;

	trace4(TR_clnt_broadcast, 0, prog, vers, proc);
	if (_thr_main())
		clnt_broadcast_result_main = eachresult;
	else {
		if (clnt_broadcast_key == 0) {
			mutex_lock(&tsd_lock);
			if (clnt_broadcast_key == 0)
				thr_keycreate(&clnt_broadcast_key, free);
			mutex_unlock(&tsd_lock);
		}
		thr_setspecific(clnt_broadcast_key, (void *) eachresult);
	}
	dummy = rpc_broadcast(prog, vers, proc, xargs, argsp, xresults,
				resultsp, (resultproc_t) rpc_wrap_bcast, "udp");
	trace4(TR_clnt_broadcast, 1, prog, vers, proc);
	return (dummy);
}

/*
 * Create the client des authentication object. Obsoleted by
 * authdes_seccreate().
 */
AUTH *
authdes_create(servername, window, syncaddr, ckey)
	char *servername;		/* network name of server */
	u_int window;			/* time to live */
	struct sockaddr_in *syncaddr;	/* optional hostaddr to sync with */
	des_block *ckey;		/* optional conversation key to use */
{
	char *hostname = NULL;
	AUTH *dummy;

	trace2(TR_authdes_create, 0, window);
	if (syncaddr) {
		/*
		 * Change addr to hostname, because that is the way
		 * new interface takes it.
		 */
		struct netconfig *nconf;
		struct netbuf nb_syncaddr;
		struct nd_hostservlist *hlist;
		AUTH *nauth;
		int fd;
		struct t_info tinfo;

		if ((nconf = __rpc_getconfip("udp")) == NULL &&
		    (nconf = __rpc_getconfip("tcp")) == NULL)
			goto fallback;

		/* Transform sockaddr_in to netbuf */
		if ((fd = t_open(nconf->nc_device, O_RDWR, &tinfo)) == -1) {
			(void) freenetconfigent(nconf);
			goto fallback;
		}
		(void) t_close(fd);
		nb_syncaddr.maxlen = nb_syncaddr.len =
			__rpc_get_a_size(tinfo.addr);
		nb_syncaddr.buf = (char *)syncaddr;
		if (netdir_getbyaddr(nconf, &hlist, &nb_syncaddr)) {
			(void) freenetconfigent(nconf);
			goto fallback;
		}
		if (hlist && hlist->h_cnt > 0 && hlist->h_hostservs)
			hostname = hlist->h_hostservs->h_host;
		nauth = authdes_seccreate(servername, window, hostname, ckey);
		(void) netdir_free((char *)hlist, ND_HOSTSERVLIST);
		(void) freenetconfigent(nconf);
		trace2(TR_authdes_create, 1, window);
		return (nauth);
	}
fallback:
	dummy = authdes_seccreate(servername, window, hostname, ckey);
	trace2(TR_authdes_create, 1, window);
	return (dummy);
}

#endif /* PORTMAP */
