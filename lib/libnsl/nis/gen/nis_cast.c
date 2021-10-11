/*
 *	nis_cast.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_cast.c	1.17	95/02/24 SMI"


/*
 * nis_cast: multicast to a specific group of hosts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <rpc/rpc.h>
#ifndef TDRPC
#include <rpc/clnt_soc.h>
#include <rpc/nettype.h>
#include <netconfig.h>
#include <netdir.h>
#include <rpc/pmap_prot.h>  /* for PMAP_CALLIT */
#else
#include <rpc/pmap_prot.h>
#include <rpc/pmap_rmt.h>
#include <sys/socket.h>
#endif

#include <locale.h>
#include <rpcsvc/nis.h>
#include "nis_clnt.h"
#include "nis_local.h"

#define	h_mask_on_p(x, i)  ((i) < NIS_MAXREPLICAS && ((x)[i] == 1))

extern int __nis_debuglevel;
extern long __nis_nis_librand(long);

/*
 * To be able to reach 4.X NIS+ servers, we will be sending the remote ping
 * message to portmapper and not rpcbind.
 */
#define	USE_PMAP_CALLIT

#ifdef TDRPC
/*
 * XXX: These structures are defined in 5.0 pmap_*.h files but not in
 * 4.1 RPC headers.  So, for now just copying the two new
 * structures that this code uses.
 */
/*
 * Client-side only representation of rmtcallargs structure.
 *
 * The routine that XDRs the rmtcallargs structure must deal with the
 * opaque arguments in the "args" structure.  xdr_rmtcall_args() needs to be
 * passed the XDR routine that knows the args' structure.  This routine
 * doesn't need to go over-the-wire (and it wouldn't make sense anyway) since
 * the application being called knows the args structure already.  So we use a
 * different "XDR" structure on the client side, p_rmtcallargs, which includes
 * the args' XDR routine.
 */
struct p_rmtcallargs {
	u_long prog;
	u_long vers;
	u_long proc;
	struct {
		u_int args_len;
		char *args_val;
	} args;
	xdrproc_t	xdr_args;	/* encodes args */
};

struct p_rmtcallres {
	u_long port;
	struct {
		u_int res_len;
		char *res_val;
	} res;
	xdrproc_t	xdr_res;	/* decodes res */
};

static bool_t xdr_rmtcallargs_my(XDR *, struct p_rmtcallargs *);
static bool_t xdr_rmtcallres_my(XDR *, struct p_rmtcallres *);

#endif

#ifndef TDRPC
static void free_transports();
static enum clnt_stat find_addrs(struct netconfig *,
				    nis_server *,
				    struct nd_addrlist **);

static struct tstamps {
	struct tstamps *ts_next;
	int ts_hinx;		/* host index */
};

/* A list of addresses - all belonging to the same transport */

static struct addrs {
	struct addrs *addr_next;	/* next address */
	int addr_inx;			/* host index */
	struct nd_addrlist *addr_addrs;
	struct tstamps *addr_if_tstamps; /* time stamp list */
};

/* A list of connectionless transports */

static struct transp {
	struct transp		*tr_next;
	int			tr_fd;
	char			*tr_device;
	struct t_bind		*tr_taddr;
	struct addrs		*tr_addrs;
};


/*
 * __nis_cast_proc(): this provides a pseudo multicast feature where the list of
 * the servers is in the directory object.  Very similar to rpc_broadcast().
 */
enum clnt_stat
__nis_cast_proc(dobj, hostmask, procnum, xdr_inproc, in, xdr_outproc, out,
		eachresult, mydata, mytimeout)
	directory_obj *dobj;	/* contains servers whom I should be pinging */
	h_mask hostmask;	/* indication of which hosts to try */
	u_long procnum;		/* procedure number to call */
	xdrproc_t xdr_inproc;	/* XDR input functions */
	void *in;		/* input argument */
	xdrproc_t xdr_outproc;	/* XDR output functions */
	void *out;		/* output argument */
	bool_t (eachresult());	/* function to call with each result */
	caddr_t mydata;		/* data passed with eachresult */
	int mytimeout;		/* timeout (sec).  Can be 0 for messaging */
{
	enum clnt_stat stat = RPC_SUCCESS;
	AUTH *sys_auth = authsys_create_default();
	XDR xdr_stream;
	register XDR *xdrs = &xdr_stream;
	int outlen;
	int flag;
	int sent, addr_cnt, rcvd, if_cnt;
	fd_set readfds, mask;
	register u_long xid;		/* xid - unique per addr */
	register int i;
	struct rpc_msg msg;
	struct timeval t;
	char outbuf[UDPMSGSIZE], inbuf[UDPMSGSIZE];
	struct t_unitdata t_udata, t_rdata;
	struct nd_addrlist *retaddrs = NULL;
	struct transp *tr_head;
	struct transp *trans, *prev_trans;
	struct addrs *a, *prev_addr;
	struct tstamps *ts, *prev_ts;
	NCONF_HANDLE *nc = NULL;
	struct netconfig *nconf;
	struct rlimit rl;
	int dtbsize, curr, start;
	struct nis_server *host_array;
	int num_hosts;
	bool_t done = FALSE;
	int timeout = mytimeout;
#ifdef USE_PMAP_CALLIT
	struct p_rmtcallargs rarg;	/* Remote arguments */
	struct p_rmtcallres rres;	/* Remote results */
#endif

	if (sys_auth == (AUTH *) NULL) {
		stat = RPC_SYSTEMERROR;
		goto done_broad;
	}

	host_array = dobj->do_servers.do_servers_val;
	num_hosts = dobj->do_servers.do_servers_len;

	/*
	 * For each connectionless transport get a list of
	 * host addresses.  Any single host may have
	 * addresses on several transports.
	 */
	addr_cnt = sent = rcvd = 0;
	nc = setnetconfig();
	if (nc == NULL) {
		stat = RPC_CANTSEND;
		goto done_broad;
	}
	tr_head = NULL;
	FD_ZERO(&mask);

	if (getrlimit(RLIMIT_NOFILE, &rl) == 0)
		dtbsize = rl.rlim_cur;
	else
		dtbsize = FD_SETSIZE;

	while (nconf = getnetconfig(nc)) {
		if (!(nconf->nc_flag & NC_VISIBLE) ||
		    nconf->nc_semantics != NC_TPI_CLTS ||
		    (strcmp(nconf->nc_protofmly, NC_LOOPBACK) == 0))
			continue;
		trans = (struct transp *)malloc(sizeof (*trans));
		if (trans == NULL) {
			syslog(LOG_ERR, "nis_cast: no memory");
			stat = RPC_CANTSEND;
			goto done_broad;
		}
		memset(trans, 0, sizeof (*trans));
		if (tr_head == NULL)
			tr_head = trans;
		else
			prev_trans->tr_next = trans;
		prev_trans = trans;

		trans->tr_fd = t_open(nconf->nc_device, O_RDWR, NULL);
		if (trans->tr_fd < 0) {
			syslog(LOG_ERR, "nis_cast: t_open: %s:%m",
			    nconf->nc_device);
			stat = RPC_CANTSEND;
			goto done_broad;
		}
		if (t_bind(trans->tr_fd, (struct t_bind *) NULL,
		    (struct t_bind *) NULL) < 0) {
			syslog(LOG_ERR, "nis_cast: t_bind: %m");
			stat = RPC_CANTSEND;
			goto done_broad;
		}
		trans->tr_taddr =
		    (struct t_bind *) t_alloc(trans->tr_fd, T_BIND, T_ADDR);
		if (trans->tr_taddr == (struct t_bind *) NULL) {
			syslog(LOG_ERR, "nis_cast: t_alloc: %m");
			stat = RPC_SYSTEMERROR;
			goto done_broad;
		}

		trans->tr_device = nconf->nc_device;
		FD_SET(trans->tr_fd, &mask);

		start = __nis_librand() % num_hosts;
		for (i = 0; i < num_hosts; i++) {
			curr = (start+i) % num_hosts;
			if (h_mask_on_p(hostmask, curr)) {
				if (__nis_debuglevel)
					syslog(LOG_ERR,
					    "nis_cast: ignoring host %s",
					    host_array[curr].name);
				continue;
			}
			stat = find_addrs(nconf, &host_array[curr], &retaddrs);
			if (stat == RPC_SUCCESS && retaddrs != NULL) {
				a = (struct addrs *)malloc(sizeof (*a));
				if (a == NULL) {
					syslog(LOG_ERR, "nis_cast: no memory");
					stat = RPC_CANTSEND;
					goto done_broad;
				}
				memset(a, 0, sizeof (*a));
				if (trans->tr_addrs == NULL)
					trans->tr_addrs = a;
				else
					prev_addr->addr_next = a;
				prev_addr = a;
				a->addr_if_tstamps = NULL;
				a->addr_inx = curr;
				a->addr_addrs = retaddrs;
				if_cnt = retaddrs->n_cnt;
				while (if_cnt--) {
					ts = (struct tstamps *)
						malloc(sizeof (*ts));
					if (ts == NULL) {
						syslog(LOG_ERR,
						    "nis_cast: no memory");
						stat = RPC_CANTSEND;
						goto done_broad;
					}
					memset(ts, 0, sizeof (*ts));
					if (a->addr_if_tstamps == NULL)
						a->addr_if_tstamps = ts;
					else
						prev_ts->ts_next = ts;
					prev_ts = ts;
					/* need not distinguish transports */
					ts->ts_hinx = curr;
					addr_cnt++;
				}
			} else if (stat == RPC_CANTSEND) {
				goto done_broad;
			} else {
				if (__nis_debuglevel)
					syslog(LOG_ERR,
				    "nis_cast: %s: address not known",
						host_array[curr].name);
			}
		}
	}
	if (addr_cnt == 0) {
		syslog(LOG_ERR, "nis_cast: couldn't find addresses");
		stat = RPC_CANTSEND;
		goto done_broad;
	}

	(void) gettimeofday(&t, (struct timezone *) 0);
	xid = (getpid() ^ t.tv_sec ^ t.tv_usec) & ~0xFF;
	t.tv_usec = 0;

	/* serialize the RPC header */
	msg.rm_direction = CALL;
	msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
#ifndef USE_PMAP_CALLIT
	msg.rm_call.cb_prog = RPCBPROG;
	msg.rm_call.cb_vers = RPCBVERS;
	msg.rm_call.cb_proc = RPCBPROC_CALLIT
#else
	/*
	 * We must use a portmap version (2) so that we can speak to
	 * 4.X machines and 5.X machines with a single ping packet.
	 */
	msg.rm_call.cb_prog = PMAPPROG;
	msg.rm_call.cb_vers = PMAPVERS;	/* version 2 */
	msg.rm_call.cb_proc = PMAPPROC_CALLIT;
	rarg.prog = NIS_PROG;
	rarg.vers = NIS_VERSION;
	rarg.proc = procnum;
	rarg.args.args_val = in;
	rarg.xdr_args = (xdrproc_t) xdr_inproc;
	rres.res.res_val = out;
	rres.xdr_res = (xdrproc_t) xdr_outproc;
#endif
	msg.rm_call.cb_cred = sys_auth->ah_cred;
	msg.rm_call.cb_verf = sys_auth->ah_verf;
	xdrmem_create(xdrs, outbuf, sizeof (outbuf), XDR_ENCODE);
#ifndef USE_PMAP_CALLIT
	/*
	 * XXX: Does this code really work: where is the corresponding
	 * xdr_rpcb_rmtcallargs part.
	 */
	if (! xdr_callmsg(xdrs, &msg)) {
		stat = RPC_CANTENCODEARGS;
		goto done_broad;
	}
#else
	if ((! xdr_callmsg(xdrs, &msg)) ||
	    (! xdr_rmtcallargs(xdrs, &rarg))) {
		stat = RPC_CANTENCODEARGS;
		goto done_broad;
	}
#endif
	outlen = (int)xdr_getpos(xdrs);
	xdr_destroy(xdrs);

	t_udata.opt.len = 0;
	t_udata.udata.buf = outbuf;
	t_udata.udata.len = outlen;

	/*
	 * Basic loop: send packet to all hosts and wait for response(s).
	 * The response timeout grows larger per iteration.
	 * A unique xid is assigned to each address in order to
	 * correctly match the replies.  We allow a timeout of 0 as well to
	 * support one-way messages.
	 */
	for (t.tv_sec = 3; timeout >= 0; t.tv_sec += 2) {
		timeout -= t.tv_sec;
		if (timeout < 0)
			t.tv_sec += timeout;
		sent = 0;
		for (trans = tr_head; trans; trans = trans->tr_next) {
			for (a = trans->tr_addrs; a; a = a->addr_next) {
				struct netbuf *if_netbuf =
					a->addr_addrs->n_addrs;
				ts = a->addr_if_tstamps;
				if_cnt = a->addr_addrs->n_cnt;
				while (if_cnt--) {
					/* put host identity in xid */
					*((u_long *)outbuf) =
						htonl(xid + ts->ts_hinx);
					ts = ts->ts_next;
					t_udata.addr = *if_netbuf++;
					/*
					 * xid is the first thing in
					 * preserialized buffer
					 */
					if (t_sndudata(trans->tr_fd, &t_udata)
						!= 0) {
						continue;
					}
					sent++;
				}
			}
		}
		if (sent == 0) {		/* no packets sent ? */
			stat = RPC_CANTSEND;
			goto done_broad;
		}

		if (t.tv_sec == 0) {
			if (mytimeout == 0)
				/* this could be set for message passing mode */
				stat = RPC_SUCCESS;
			else
				stat = RPC_TIMEDOUT;
			goto done_broad;
		}
		/*
		 * Have sent all the packets.  Now collect the responses...
		 */
		rcvd = 0;
	recv_again:
		msg.acpted_rply.ar_verf = _null_auth;
#ifndef USE_PMAP_CALLIT
		/* XXX: xdr_rpcb_rmtcallres() should have been used instead */
		msg.acpted_rply.ar_results.proc = xdr_void;
#else
		msg.acpted_rply.ar_results.where = (caddr_t)&rres;
		msg.acpted_rply.ar_results.proc = (xdrproc_t)xdr_rmtcallres;
#endif
		readfds = mask;
		switch (select(dtbsize, &readfds,
			(fd_set *) NULL, (fd_set *) NULL, &t)) {

		case 0:  /* timed out */
			if (rcvd == 0) {
				stat = RPC_TIMEDOUT;
				continue;
			} else
				goto done_broad;

		case -1:  /* some kind of error */
			if (errno == EINTR)
				goto recv_again;
			syslog(LOG_ERR, "nis_cast: select: %m");
			if (rcvd == 0)
				stat = RPC_CANTRECV;
			goto done_broad;

		}  /* end of select results switch */

		for (trans = tr_head; trans; trans = trans->tr_next) {
			if (FD_ISSET(trans->tr_fd, &readfds))
				break;
		}
		if (trans == NULL)
			goto recv_again;

	try_again:
		t_rdata.addr = trans->tr_taddr->addr;
		t_rdata.udata.buf = inbuf;
		t_rdata.udata.maxlen = sizeof (inbuf);
		t_rdata.udata.len = 0;
		t_rdata.opt.len = 0;
		if (t_rcvudata(trans->tr_fd, &t_rdata, &flag) < 0) {
			if (t_errno == TSYSERR && errno == EINTR)
				goto try_again;
			/*
			 * Ignore any T_UDERR look errors.  We should
			 * never see any ICMP port unreachables when
			 * broadcasting but it has been observed with
			 * broken IP implementations.
			 */
			if (t_errno == TLOOK &&
			    t_look(trans->tr_fd) == T_UDERR &&
			    t_rcvuderr(trans->tr_fd, NULL) == 0)
				goto recv_again;

			syslog(LOG_ERR, "nis_cast: t_rcvudata: %s:%m",
			    trans->tr_device);
			stat = RPC_CANTRECV;
			continue;
		}
		if (t_rdata.udata.len < sizeof (u_long))
			goto recv_again;
		if (flag & T_MORE) {
			syslog(LOG_ERR,
			    "nis_cast: t_rcvudata: %s: buffer overflow",
			    trans->tr_device);
			goto recv_again;
		}
		/*
		 * see if reply transaction id matches sent id.
		 * If so, decode the results.
		 * Note: received addr is ignored, it could be different
		 * from the send addr if the host has more than one addr.
		 */
		xdrmem_create(xdrs, inbuf,
				(u_int) t_rdata.udata.len, XDR_DECODE);
		if (xdr_replymsg(xdrs, &msg)) {
			if (msg.rm_reply.rp_stat == MSG_ACCEPTED &&
			    (msg.acpted_rply.ar_stat == SUCCESS)) {
				rcvd++;
				if (eachresult)
					done = (*eachresult) (out,
						msg.rm_xid & 0xFF, mydata);
				stat = RPC_SUCCESS;
			}
			/* otherwise, we just ignore the errors ... */
		}
		xdrs->x_op = XDR_FREE;
		msg.acpted_rply.ar_results.proc = xdr_void;
		(void) (*xdr_outproc)(xdrs, out);
		(void) xdr_replymsg(xdrs, &msg);
		XDR_DESTROY(xdrs);
		if (done)
			goto done_broad;
		else
			goto recv_again;
	}
	if (!rcvd)
		stat = RPC_TIMEDOUT;

done_broad:
	if (nc)
		endnetconfig(nc);
	free_transports(tr_head);
	AUTH_DESTROY(sys_auth);
	return (stat);

}

static bool_t
my_eachresult(data, hostnum, userarg)
	caddr_t	data;
	int	hostnum;
	caddr_t userarg;
{
	int	*hostp = (int *)userarg;

	if (hostnum < *hostp) {
		*hostp = hostnum;
		return (TRUE);
	}
	/* some error */
	return (FALSE);
}

/*
 * Modified version of nfs_cast.
 *
 * This routine is designed to be able to "ping"
 * a list of hosts to find the host that is
 * up and available and responds fastest.
 * It uses the address information in the
 * nis_server objects supplied in the 'host_array'
 * as targets to ping.
 * A response to a ping is no guarantee that the host is running NIS+.
 *
 * Returns as soon as a single response is received.
 */
enum clnt_stat
__nis_cast(host_array, num_hosts, hostmask, fastest, timeout)
	nis_server *host_array; /* array of nis_server objects */
	int num_hosts;		/* size of array */
	h_mask hostmask;	/* indication of which hosts to try */
	int *fastest;		/* return host found here */
	int timeout;		/* timeout (sec) */
{
	directory_obj dobj; /* array of nis_server objects */

	/* Create a dummy directory object */
	dobj.do_servers.do_servers_val = host_array;
	dobj.do_servers.do_servers_len = num_hosts;
	*fastest = num_hosts;
	return (__nis_cast_proc(&dobj, hostmask, NULLPROC,
				xdr_void, (void *)NULL,
				xdr_void, (void *)NULL,
				my_eachresult, (caddr_t) fastest, timeout));
}

static void
free_transports(trans)
	struct transp *trans;
{
	struct transp *t, *tmpt;
	struct addrs *a, *tmpa;
	struct tstamps *ts, *tmpts;

	for (t = trans; t; t = tmpt) {
		if (t->tr_taddr)
			(void) t_free((char *)t->tr_taddr, T_BIND);
		if (t->tr_fd >= 0)
			(void) t_close(t->tr_fd);
		for (a = t->tr_addrs; a; a = tmpa) {
			for (ts = a->addr_if_tstamps; ts; ts = tmpts) {
				tmpts = ts->ts_next;
				free(ts);
			}
			(void) netdir_free((char *)a->addr_addrs, ND_ADDRLIST);
			tmpa = a->addr_next;
			free(a);
		}
		tmpt = t->tr_next;
		free(t);
	}
}

/*
 *  Find the addresses contained in the host object that matches
 *  the given target family/proto.  Return the matching addresses
 *  in taddr form, and set retaddrs to point to it.
 *
 *  Return RPC_CANTSEND if fatal error encountered (e.g. no memory).
 *  Return RPC_SUCCESS otherwise.  Note that retaddrs could be returned null.
*/

static
enum clnt_stat
find_addrs(nconf, host, retaddrs)
	struct netconfig *nconf;
	nis_server *host;
	struct nd_addrlist **retaddrs;
{
	struct nd_addrlist *addrs;
	struct endpoint *host_ends;
	int i, num_ends, ends_found, end_cnt;
	enum clnt_stat stat;
	struct netbuf *taddr;
	char *hlist;
	char *target_family;
	char *target_proto;

	if (retaddrs)
		*retaddrs = NULL;
	stat = RPC_SUCCESS;

	target_family = nconf->nc_protofmly;
	target_proto = nconf->nc_proto;

	host_ends = host->ep.ep_val;
	num_ends = host->ep.ep_len;
	hlist = (char *) malloc(num_ends);
	if (hlist == NULL) {
		syslog(LOG_ERR, "nis_cast: no memory");
		stat = RPC_CANTSEND;
	}
	memset(hlist, 0, num_ends);

	/* find address that matches target family/proto */
	ends_found = 0;
	for (i = 0; i < num_ends; i++) {
		if (strcasecmp(host_ends[i].family, target_family) == 0 &&
		    strcasecmp(host_ends[i].proto, target_proto) == 0) {
			hlist[i] = 1;
			++ends_found;
		}
	}

	if (ends_found == 0) {
		goto free_hlist;
	}

	addrs = (struct nd_addrlist *)malloc(sizeof (struct nd_addrlist));
	if (addrs == NULL) {
		syslog(LOG_ERR, "nis_cast: no memory");
		stat = RPC_CANTSEND;
		goto free_hlist;
	}

	addrs->n_cnt = ends_found;
	addrs->n_addrs = (struct netbuf *)
		calloc(ends_found, sizeof (struct netbuf));
	if (addrs->n_addrs == NULL) {
		syslog(LOG_ERR, "nis_cast: no memory");
		stat = RPC_CANTSEND;
		free(addrs);
		goto free_hlist;
	}

	/*
	 * Translate matching addresses to taddrs.
	 * We really only need one address, so we break
	 * on the first successful one.
	 */
	end_cnt = 0;
	for (i = 0; i < num_ends; i++) {
		if (hlist[i] == 1) {
			taddr = uaddr2taddr(nconf, host_ends[i].uaddr);
			if (taddr == NULL) {
				--addrs->n_cnt;
				syslog(LOG_ERR,
	    "nis_cast: Unable to convert universal address %s for %s (%d).",
	    host_ends[i].uaddr, host->name, _nderror);
			} else if (end_cnt < ends_found) {
				addrs->n_addrs[end_cnt++] = *taddr; /* copy */
				free(taddr);  /* don't use netdir_free */
#if 0
				break;
#endif /* 0 */
			} else {
				syslog(LOG_ERR, "nis_cast: accounting error.");
				stat = RPC_CANTSEND;
				goto free_hlist;
			}
		}
	}

	if (end_cnt) {
		if (retaddrs)
			*retaddrs = addrs;
	} else {
		/* couldn't get any addresses */
		free(addrs->n_addrs);
		free(addrs);
	}

free_hlist:
	if (hlist)
		free(hlist);
	return (stat);
}

#else  TDRPC

extern enum
clnt_stat nis_find_sockaddr(nis_server *, struct sockaddr_in *, char *);

/*
 * __nis_cast_proc(): this provides a pseudo multicast feature where the list of
 * the servers is in the directory object.  Very similar to rpc_broadcast().
 */
enum clnt_stat
__nis_cast_proc(dobj, hostmask, procnum, xdr_inproc, in, xdr_outproc, out,
		eachresult, mydata, mytimeout)
	directory_obj *dobj;	/* contains servers whom I should be pinging */
	h_mask hostmask;	/* indication of which hosts to try */
	u_long procnum;		/* procedure number to call */
	xdrproc_t xdr_inproc;	/* XDR input functions */
	void *in;		/* input argument */
	xdrproc_t xdr_outproc;	/* XDR output functions */
	void *out;		/* output argument */
	bool_t (eachresult());	/* function to call with each result */
	caddr_t mydata;		/* data passed with eachresult */
	int mytimeout;		/* timeout (sec).  Can be 0 for messaging */
{
	enum clnt_stat stat = RPC_SUCCESS;
	AUTH *unix_auth = authunix_create_default();
	XDR xdr_stream;
	register XDR *xdrs = &xdr_stream;
	int outlen, inlen, fromlen;
	int sent;
	int sock;
	fd_set readfds, mask;
	bool_t done = FALSE;
	register u_long xid;		/* xid - unique per addr */
	struct sockaddr_in baddr;	/* "to" address */
	struct sockaddr_in raddr;	/* "from" address (ignored) */
	register int i;
	struct rpc_msg msg;
	struct timeval t;
	char outbuf[UDPMSGSIZE], inbuf[UDPMSGSIZE];
	int start, curr;
	struct nis_server *host_array;
	int num_hosts;
	bool_t done = FALSE;
	int timeout = mytimeout;
#ifdef USE_PMAP_CALLIT
	struct p_rmtcallargs rarg;	/* Remote arguments */
	struct p_rmtcallres rres;	/* Remote results */
#endif

	if (unix_auth == (AUTH *) NULL) {
		stat = RPC_SYSTEMERROR;
		goto done_broad;
	}
	host_array = dobj->do_servers.do_servers_val;
	num_hosts = dobj->do_servers.do_servers_len;

	/*
	 * initialization: create a socket, a broadcast address, and
	 * preserialize the arguments into a send buffer.
	 */
	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		syslog(LOG_ERR, "Cannot create socket for NIS+: %m");
		stat = RPC_CANTSEND;
		goto done_broad;
	}
	FD_ZERO(&mask);
	FD_SET(sock, &mask);
	memset((char *)&baddr, 0, sizeof (baddr));
	baddr.sin_family = AF_INET;
	(void) gettimeofday(&t, (struct timezone *) 0);
	xid = (getpid() ^ t.tv_sec ^ t.tv_usec) & ~0xFF;
	t.tv_usec = 0;

	/* serialize the RPC header */
	msg.rm_direction = CALL;
	msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	msg.rm_call.cb_prog = PMAPPROG;
	msg.rm_call.cb_vers = PMAPVERS;
#ifndef USE_PMAP_CALLIT
	msg.rm_call.cb_proc = NULLPROC;
#else
	/*
	 * We must use a common version (2) so that we can speak to
	 * 4.1 machines and 5.0 machines with a single ping packet.
	 */
	msg.rm_call.cb_proc = PMAPPROC_CALLIT;
	rarg.prog = NIS_PROG;
	rarg.vers = NIS_VERSION;
	rarg.proc = procnum;
	rarg.args.args_val = in;
	rarg.xdr_args = (xdrproc_t) xdr_inproc;
	rres.res.res_val = out;
	rres.xdr_res = (xdrproc_t) xdr_outproc;
#endif
	msg.rm_call.cb_cred = unix_auth->ah_cred;
	msg.rm_call.cb_verf = unix_auth->ah_verf;
	xdrmem_create(xdrs, outbuf, sizeof (outbuf), XDR_ENCODE);
#ifndef USE_PMAP_CALLIT
	/*
	 * XXX: Does this code really work: where is the corresponding
	 * part for xdr_rmtcallargs() that does the argument marshalling?
	 */
	if (! xdr_callmsg(xdrs, &msg)) {
		stat = RPC_CANTENCODEARGS;
		goto done_broad;
	}
#else
	if ((! xdr_callmsg(xdrs, &msg)) ||
	    (! xdr_rmtcallargs_my(xdrs, &rarg))) {
		stat = RPC_CANTENCODEARGS;
		goto done_broad;
	}
#endif
	outlen = (int)xdr_getpos(xdrs);
	xdr_destroy(xdrs);

	/*
	 * Basic loop: send packet to all hosts and wait for response(s).
	 * The response timeout grows larger per iteration.
	 * A unique xid is assigned to each request in order to
	 * correctly match the replies.  We allow a timeout of 0 as well to
	 * support one-way messages.
	 */
	for (t.tv_sec = 3; timeout >= 0; t.tv_sec += 2) {
		timeout -= t.tv_sec;
		if (timeout < 0)
			t.tv_sec = timeout - t.tv_sec;
		sent = 0;

		start = __nis_librand() % num_hosts;
		for (i = 0; i < num_hosts; i++) {
			curr = (start+i) % num_hosts;
			if (h_mask_on_p(hostmask, curr)) {
				if (__nis_debuglevel)
					syslog(LOG_ERR,
					    "nis_cast: ignoring host %s",
					    host_array[curr].name);
				continue;
			}
			stat = nis_find_sockaddr(&host_array[curr], &baddr,
			    "udp");
			if (stat == RPC_SUCCESS) {
				/* xid is first in preserialized buffer */
				*((u_long *)outbuf) = htonl(xid + curr);
				if (sendto(sock, outbuf, outlen, 0,
				    (struct sockaddr *)&baddr,
				    sizeof (struct sockaddr)) != outlen) {
					syslog(LOG_ERR,
					"nis_cast: Cannot send packet: %m");
					continue;
				}
				sent++;
			} else {
				/* couldn't find address to use */
				if (__nis_debuglevel)
					syslog(LOG_ERR,
					    "nis_cast: %s: address not known",
					    host_array[curr].name);
			}
		}

		if (sent == 0) {		/* no packets sent ? */
			stat = RPC_CANTSEND;
			goto done_broad;
		}

		if (t.tv_sec == 0) {
			if (mytimeout == 0)
				/* this could be set for message passing mode */
				stat = RPC_SUCCESS;
			else
				stat = RPC_TIMEDOUT;
			goto done_broad;
		}

		/*
		 * Have sent all the packets.  Now collect the responses...
		 */
	recv_again:
		msg.acpted_rply.ar_verf = _null_auth;
#ifndef USE_PMAP_CALLIT
		msg.acpted_rply.ar_results.proc = xdr_void;
#else
		msg.acpted_rply.ar_results.where = (caddr_t)&rres;
		msg.acpted_rply.ar_results.proc =
			(xdrproc_t)xdr_rmtcallres_my;
#endif
		readfds = mask;
		switch (select(32, &readfds, (fd_set *) NULL,
		    (fd_set *) NULL, &t)) {

		case 0:  /* timed out */
			stat = RPC_TIMEDOUT;
			continue;

		case -1:  /* some kind of error */
			if (errno == EINTR)
				goto recv_again;
			syslog(LOG_ERR, "nis_cast: select: %m");
			stat = RPC_CANTRECV;
			goto done_broad;

		}  /* end of select results switch */

		if (!FD_ISSET(sock, &readfds))
			goto recv_again;

	try_again:
		fromlen = sizeof (struct sockaddr);
		inlen = recvfrom(sock, inbuf, sizeof (inbuf), 0,
			(struct sockaddr *)&raddr, &fromlen);
		if (inlen < 0) {
			if (errno == EINTR)
				goto try_again;
			syslog(LOG_ERR,
			    "nis_cast: cannot receive reply: %m");
			stat = RPC_CANTRECV;
			goto done_broad;
		}
		if (inlen < sizeof (u_long))
			goto recv_again;
		/*
		 * see if reply transaction id matches sent id.
		 * If so, get host identify from xid and we're done.
		 */
		xdrmem_create(xdrs, inbuf, inlen, XDR_DECODE);
		if (xdr_replymsg(xdrs, &msg)) {
			if (msg.rm_reply.rp_stat == MSG_ACCEPTED &&
			    (msg.acpted_rply.ar_stat == SUCCESS)) {
				rcvd++;
				if (eachresult)
					done = (*eachresult) (out,
						msg.rm_xid & 0xFF, mydata);
				stat = RPC_SUCCESS;
			}

			/* otherwise, we just ignore the errors ... */
		}
		xdrs->x_op = XDR_FREE;
		msg.acpted_rply.ar_results.proc = xdr_void;
		(void) (*xdr_outproc)(xdrs, out);
		(void) xdr_replymsg(xdrs, &msg);
		XDR_DESTROY(xdrs);
		if (done)
			goto done_broad;
		else
			goto recv_again;
	}
	stat = RPC_TIMEDOUT;

done_broad:
	(void) close(sock);
	AUTH_DESTROY(unix_auth);
	return (stat);
}

/*
 * XXX: These two XDR functions added only because they are not available in
 * 4.X RPC library.
 */
/*
 * XDR remote call arguments
 * written for XDR_ENCODE direction only
 *
 * XXX: There was a similar function called in 4.1, so we are changing its
 * name to xdr_rmtcallargs_my
 */
bool_t
xdr_rmtcallargs_my(xdrs, cap)
	register XDR *xdrs;
	register struct p_rmtcallargs *cap;
{
	u_int lenposition, argposition, position;
	register    long *buf;

	buf = XDR_INLINE(xdrs, 3 * BYTES_PER_XDR_UNIT);
	if (buf == NULL) {
		if (!xdr_u_long(xdrs, &(cap->prog)) ||
		    !xdr_u_long(xdrs, &(cap->vers)) ||
		    !xdr_u_long(xdrs, &(cap->proc))) {
			return (FALSE);
		}
	} else {
		IXDR_PUT_U_LONG(buf, cap->prog);
		IXDR_PUT_U_LONG(buf, cap->vers);
		IXDR_PUT_U_LONG(buf, cap->proc);
	}

	/*
	 * All the jugglery for just getting the size of the arguments
	*/
	lenposition = XDR_GETPOS(xdrs);
	if (! xdr_u_int(xdrs, &(cap->args.args_len)))  {
		return (FALSE);
	}
	argposition = XDR_GETPOS(xdrs);
	if (! (*cap->xdr_args)(xdrs, cap->args.args_val)) {
		return (FALSE);
	}
	position = XDR_GETPOS(xdrs);
	cap->args.args_len = position - argposition;
	XDR_SETPOS(xdrs, lenposition);
	if (! xdr_u_int(xdrs, &(cap->args.args_len))) {
		return (FALSE);
	}
	XDR_SETPOS(xdrs, position);
	return (TRUE);
}


/*
 * XDR remote call results
 * written for XDR_DECODE direction only
 *
 * XXX: There was a similar function called in 4.1, so we are changing its
 * name to xdr_rmtcallres_my
 */
bool_t
xdr_rmtcallres_my(xdrs, crp)
	register XDR *xdrs;
	register struct p_rmtcallres *crp;
{
	bool_t  dummy;

	if (xdr_u_long(xdrs, &crp->port) &&
	    xdr_u_int(xdrs, &crp->res.res_len)) {

		dummy = (*(crp->xdr_res))(xdrs, crp->res.res_val);
		return (dummy);
	}
	return (FALSE);
}
#endif TDRPC
