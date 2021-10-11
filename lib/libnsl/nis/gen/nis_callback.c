/*
 *	nis_callback.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_callback.c	1.13	93/05/11 SMI"

/*
 *	nis_callback.c
 *
 *	This module contains the functions that implement the callback
 *	facility. They are RPC library dependent.
 *
 * 	These callback functions set up and run the callback
 * 	facility of NIS+. The idea is simple, a psuedo service is created
 * 	by the client and registered with the portmapper. The program number
 * 	for that service is included in the request as is the principal
 * 	name of the _host_ where the request is being made. The server
 * 	then does rpc calls to that service to return results.
 */

#include <syslog.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#ifdef TDRPC
#include <sys/socket.h>
#else
#include <tiuser.h>
#include <netdir.h>
#include <sys/netconfig.h>
#endif
#include <netinet/in.h>
#include <rpc/rpc.h>
#include <rpc/clnt.h>
#include <rpc/types.h>
#include <rpc/pmap_prot.h>
#include <rpc/pmap_clnt.h>
#include <rpcsvc/nis.h>
#include <rpcsvc/nis_callback.h>
#include <thread.h>
#include <synch.h>
#include "nis_clnt.h"
#include "nis_local.h"

#ifndef TRUE
#define	TRUE 1
#define	FALSE 0
#endif

#ifdef TDRPC
#define	CB_MAXENDPOINTS	1
#else
#define	CB_MAXENDPOINTS 16
#endif  /* TDRPC */

/*
 * __cbdata is the internal state which the callback routines maintain
 * for clients. It is stored as a structure so that multithreaded clients
 * may eventually keep a static copy of this on their Thread local storage.
 */
static struct callback_data {
	nis_server 	cbhost;
	char		pkey_data[1024];
	endpoint	cbendp[CB_MAXENDPOINTS];
	SVCXPRT		*cbsvc[CB_MAXENDPOINTS];
	bool_t		complete;
	int		results;
	pid_t		cbpid;
	nis_error	cberror;
	void		*cbuser;
	int		(*cback)();
};

/*
 * Static function prototypes.
 */
void
__do_callback(struct svc_req *, SVCXPRT *);

bool_t
__callback_stub(cback_data *, struct svc_req *, struct callback_data *, int *);

bool_t
__callback_finish(void *, struct svc_req, struct callback_data *, int *);

bool_t
__callback_error(void *, struct svc_req *, struct callback_data *, int *);

#ifndef TDRPC
char *__get_clnt_uaddr(CLIENT *);
#endif

static thread_key_t cbdata_key;
static struct callback_data __cbdata_main;

/*
 * Callback functions. These functions set up and run the callback
 * facility of NIS. The idea is simple, a psuedo service is created
 * by the client and registered with the portmapper. The program number
 * for that service is included in the request as is the principal
 * name of the _host_ where the request is being made. The server
 * then does rpc calls to that service to return results.
 */


static struct callback_data *my_cbdata()
{
	struct callback_data *__cbdata;

	if (_thr_main())
		return (&__cbdata_main);
	else
		thr_getspecific(cbdata_key, (void **) &__cbdata);
	return (__cbdata);
}

static void destroy_cbdata(void * cbdata)
{
	if (cbdata)
		free(cbdata);
}

#ifndef TDRPC
static char *
__get_clnt_uaddr(cl)
	CLIENT	*cl;
{
	struct netconfig	*nc;
	struct netbuf		addr;
	char			*uaddr;

	nc = getnetconfigent(cl->cl_netid);
	if (! nc)
		return (NULL);
	clnt_control(cl, CLGET_SVC_ADDR, (char *) &addr);
	uaddr = taddr2uaddr(nc, &addr);
	freenetconfigent(nc);
	return (uaddr);
}
#endif

int
__nis_destroy_callback()
{
	struct callback_data *__cbdata;
	__cbdata = my_cbdata();

	if (!__cbdata)
		return (0);

	if (__cbdata->cbsvc[0]) {
		svc_destroy(__cbdata->cbsvc[0]);
		__cbdata->cbsvc[0] = NULL;
	}
	free(__cbdata);
	__cbdata = NULL;
	return (1);
}

/*
 * __nis_init_callback()
 * This function will initialize an RPC service handle for the
 * NIS client if one doesn't already exist. This handle uses a
 * TCP connection in TDRPC and a "COTS" connection in a TIRPC.
 * The server will either fork, or generate a thread to send us
 * data and if the connection breaks we want to know (that means
 * the server died and we have to return an error). It returns
 * an endpoint if successful and NULL if it fails.
 *
 * NOTE : since we send the server the complete endpoint, including
 * universal address, transport, and family, it doesn't need to contact
 * our portmapper to find out what port we are using. Consequently we
 * don't bother registering with the portmapper, this saves us from having
 * to determine a unique program number.
 */
nis_server *
__nis_init_callback(svc_clnt, cbfunc, userdata)
	CLIENT	*svc_clnt;	/* Client handle pointing at the service */
	int	(*cbfunc)();	/* Callback function			 */
	void	*userdata;	/* Userdata, stuffed away for later	 */
{
	struct sockaddr_in	addr;
	short			p;
	int			addrsize;
	int			nep; 	/* number of endpoints */
	char			netname[1024];
	struct callback_data *__cbdata;
#ifndef TDRPC
	struct netconfig	*nc;
	struct nd_mergearg	ma;
	void			*nch;
#endif

	if (cbfunc == NULL)
		return (NULL);

	if (_thr_main())
		__cbdata = &__cbdata_main;
	else
		__cbdata = (struct callback_data *)
			thr_get_storage(&cbdata_key, 0, destroy_cbdata);

	/* Check to see if we already have a service handle */
	if (__cbdata && (__cbdata->cbsvc[0] != NULL) &&
	    (__cbdata->cbpid == getpid()))  {
		__cbdata->cback = cbfunc;
		__cbdata->cbuser = userdata;
		__cbdata->results = 0;
		__cbdata->complete = FALSE;
		return (&(__cbdata->cbhost));
	}

	/* Nope, then let's create one... */

	if (__cbdata == NULL) {
		__cbdata = (struct callback_data *)calloc(1,
						sizeof (struct callback_data));
		if (!_thr_main())
			thr_setspecific(cbdata_key, __cbdata);
		ASSERT(my_cbdata() != NULL);
	}
	if (! __cbdata) {
		syslog(LOG_ERR, "__nis_init_callback: Client out of memory.");
		return (NULL);
	}

	__cbdata->cback = cbfunc;
	__cbdata->cbuser = userdata;
	__cbdata->cbpid = getpid();
	__cbdata->results = 0;
	__cbdata->complete = FALSE;
	__cbdata->cbhost.ep.ep_val = &(__cbdata->cbendp[0]);

	if (getnetname(netname) &&
	    getpublickey(netname, __cbdata->pkey_data)) {
		__cbdata->cbhost.name = strdup(netname);
		__cbdata->cbhost.key_type = NIS_PK_DH;
		__cbdata->cbhost.pkey.n_bytes = __cbdata->pkey_data;
		__cbdata->cbhost.pkey.n_len = strlen(__cbdata->pkey_data) + 1;
	} else {
		syslog(LOG_WARNING, "no public key for %s", netname);
		__cbdata->cbhost.name = strdup((char *)nis_local_principal());
		__cbdata->cbhost.key_type = NIS_PK_NONE;
		__cbdata->cbhost.pkey.n_bytes = NULL;
		__cbdata->cbhost.pkey.n_len = 0;
	}

	/* Create the service handle(s) */
#ifdef TDRPC
	/* Step 1. Create a TCP service transport */
	__cbdata->cbsvc[0] = svctcp_create(RPC_ANYSOCK, 128, 8192);
	if (__cbdata->cbsvc[0] == NULL) {
		syslog(LOG_ERR,
			"__nis_init_callback: Cannot create callback service.");
		return (NULL);
	}

	/* Step 2. Register it with the dispatch function */
	if (!svc_register(__cbdata->cbsvc[0], CB_PROG, 1, __do_callback, 0)) {
		syslog(LOG_ERR, "Unable to register callback service.");
		svc_destroy(__cbdata->cbsvc[0]);
		__cbdata->cbsvc[0] = NULL;
		return (NULL);
	}

	/* Step 3. Cons up a universal address for it */
	__cbdata->cbhost.ep.ep_len = 1;
	addrsize = sizeof (addr);
	getsockname(__cbdata->cbsvc[0]->xp_sock, &addr, &addrsize);
	if (addrsize) {
		char	ubuf[32];

		__cbdata->cbendp[0].family = strdup("INET");
		__cbdata->cbendp[0].proto = strdup("TCP");
		p = htons(addr.sin_port);
		get_myaddress(&addr);
		sprintf(ubuf, "%s.%d.%d", inet_ntoa(addr.sin_addr),
				((p >> 8) & 0xff), (p & 0xff));
		__cbdata->cbendp[0].uaddr = strdup(ubuf);
	} else {
		svc_destroy(__cbdata->cbsvc[0]);
		__cbdata->cbsvc[0] = NULL;
		return (NULL);
	}
#else
	/*
	 * This gets a bit tricky. Because we don't know which transport
	 * the service will choose to call us back on, we have something
	 * of a delimma in picking the correct one here. Because of this
	 * we pick all of the likely ones and pass them on to the remote
	 * server and let it figure it out.
	 * XXX Use the same one as we have a client handle for XXX
	 */
	nch = (void *)setnetconfig();
	nep = 0;
	while (nch && ((nc = (struct netconfig *)getnetconfig(nch)) != NULL) &&
		    (nep == 0)) {

		/* Step 0. XXX see if it is the same netid */
		if (strcmp(nc->nc_netid, svc_clnt->cl_netid) != 0)
			continue;

		/* Step 1. Check to see if it is a virtual circuit transport. */
		if ((nc->nc_semantics & NC_TPI_COTS) == 0)
			continue;

		/* Step 2. Try to create a service transport handle. */
		__cbdata->cbsvc[nep] = svc_tli_create(RPC_ANYFD, nc, NULL,
								128, 8192);
		if (! __cbdata->cbsvc[nep]) {
			syslog(LOG_WARNING,
				"__nis_init_callback: Can't create SVCXPRT.");
			continue;
		}

		/*
		 * This merge code works because we know the netids match
		 * if we want to use a connectionless transport for the
		 * initial call and a connection oriented one for the
		 * callback this won't work. Argh.! XXX
		 */
		ma.s_uaddr = taddr2uaddr(nc,
					&(__cbdata->cbsvc[nep]->xp_ltaddr));
		if (!ma.s_uaddr) {
			syslog(LOG_WARNING,
		    "__nis_init_callback: Can't get uaddr for %s transport.",
				    nc->nc_netid);
			continue;
		}
		ma.c_uaddr = __get_clnt_uaddr(svc_clnt);
		ma.m_uaddr = NULL;
		(void) netdir_options(nc, ND_MERGEADDR, 0, (void *)&ma);
		free(ma.s_uaddr);
		free(ma.c_uaddr);

		/* Step 3. Register it */
		svc_reg(__cbdata->cbsvc[nep], CB_PROG, 1, __do_callback, NULL);

		/* Step 4. Fill in the endpoint structure. */
		__cbdata->cbendp[nep].uaddr = ma.m_uaddr;
		__cbdata->cbendp[nep].family = strdup(nc->nc_protofmly);
		__cbdata->cbendp[nep].proto = strdup(nc->nc_proto);
		nep++;
	}
	endnetconfig(nch);

	__cbdata->cbhost.ep.ep_len = nep;
	if (__cbdata->cbsvc[0] == NULL) {
		syslog(LOG_ERR,
			"__nis_init_callback: cannot create callback service.");
		return (NULL);
	}
#endif
	return (&(__cbdata->cbhost));
}

/*
 * Stub to handle requests...
 * Note, as an optimization the server may return us more than one object.
 * This stub will feed them to the callback function one at a time.
 */
static bool_t
__callback_stub(argp, rqstp, __cbdata, results_ptr)
	cback_data	*argp;
	struct svc_req	*rqstp;
	struct callback_data  *__cbdata;
	int		*results_ptr;
{
	int		i;
	char		buf[1024];

	*results_ptr = 0;
	for (i = 0; (i < argp->entries.entries_len) && (!(*results_ptr)); i++) {
		strcpy(buf, argp->entries.entries_val[i]->zo_name);
		strcat(buf, ".");
		strcat(buf, argp->entries.entries_val[i]->zo_domain);
		*results_ptr = (*(__cbdata->cback))(buf,
				argp->entries.entries_val[i], __cbdata->cbuser);
	}
	return (1); /* please do reply */
}

static bool_t
__callback_finish(argp, rqstp, __cbdata, results_ptr)
	void		*argp;
	struct svc_req	*rqstp;
	struct callback_data  *__cbdata;
	int		*results_ptr; /* not used */
{
	__cbdata->cberror = NIS_SUCCESS;
	__cbdata->complete = TRUE;
	return (0); /* don't attempt a reply */
}

static bool_t
__callback_error(argp, rqstp, __cbdata, results_ptr)
	nis_error	*argp;
	struct svc_req	*rqstp;
	struct callback_data  *__cbdata;
	int 	*results_ptr;
{
	__cbdata->cberror = *argp;
	__cbdata->complete = TRUE;
	return (1);  /* non-zero => please do a reply */
}

/*
 * Our very own version of getdtablesize(), since it's not in SVR4
 */
int
nis_getdtblsize()
{
	static int size = -1;
	struct rlimit rl;
	static mutex_t fd_size_lock = DEFAULTMUTEX; /* lock level 4 */
	int fd_size;

	/* We do'nt bother with the R/W lock here  */
	/* since the "critical section is small":  */
	/* 99% of time size is greater then 0,	   */
	/* and the probability of high concurrency */
	/* inside the section is quite low, thus   */
	/* we use the cheapest possible lock	   */
	mutex_lock(&fd_size_lock);
	if (size < 0) {
		if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
			size = rl.rlim_cur;
			fd_size =  size;
		} else 	fd_size = FD_SETSIZE;
	} else fd_size = size;
	mutex_unlock(&fd_size_lock);
	return (fd_size);
}

/*
 * __nis_run_callback()
 *
 * This is the other function exported by this module. The function
 * duplicates the behaviour of svc_run() for regular rpc services,
 * however it has the additional benefit that it monitors
 * the state of the connection and if it goes away, it terminates
 * the service and returns. Finally, when it returns, it returns
 * the number of times the callback function was called for this
 * session, or -1 if the session ended erroneously.
 */
int
__nis_run_callback(srvid, srvproc, timeout, myserv)
	netobj		*srvid;		/* Server's netobj		*/
	u_long		srvproc;	/* RPC to call to check up 	*/
	struct timeval	*timeout;	/* User's timeout		*/
	CLIENT		*myserv;	/* Server talking to us 	*/
{
	fd_set readfds;
	int dtbsize = nis_getdtblsize();
	enum clnt_stat	cs;
	struct timeval	tv, cbtv;
	bool_t	is_up; /* is_up is TRUE if the server answers us */
	struct callback_data  *__cbdata;
	extern rwlock_t svc_fd_lock;	/* defined in RPC library */

	__cbdata = my_cbdata();

	cbtv.tv_sec = NIS_CBACK_TIMEOUT;
	cbtv.tv_usec = 0;
	if (timeout)
		tv = *timeout;
	else {
		/* Default timeout when timeout is null */
		tv.tv_sec = NIS_CBACK_TIMEOUT;
		tv.tv_usec = 0;
	}
	while (! __cbdata->complete) {
		rw_rdlock(&svc_fd_lock);	/* acquire svc_fdset lock */
		readfds = svc_fdset;
		rw_unlock(&svc_fd_lock);
		/*
		 * XXX This should be converted to use poll()
		 */
		switch (select(dtbsize, &readfds, (fd_set *)0, (fd_set *)0,
							&tv)) {
		case -1:
			/*
			 * We exit on any error other than EBADF.  For all
			 * other errors, we return a callback error.
			 */
			if (errno != EBADF) {
				continue;
			}
			(void) syslog(LOG_ERR, "callback: - select failed: %m");
			return (- NIS_CBERROR);
		case 0:
			/*
			 * possible data race condition
			 */
			if (__cbdata->complete) {
				syslog(LOG_INFO,
		"__run_callback: data race condition detected and avoided.");
				break;
			}

			/*
			 * Check to see if the thread servicing us is still
			 * alive
			 */

			cs = clnt_call(myserv, srvproc,
						xdr_netobj, (char *) srvid,
						xdr_bool, (char *) &is_up,
						cbtv);

			if (cs != RPC_SUCCESS || !is_up)
				return (- NIS_CBERROR);
			break;
		default:
			svc_getreqset(&readfds);
		}
	}
	if (__cbdata->cberror) {
		return (0 - __cbdata->cberror);	/* return error results */
	} else
		return (__cbdata->results);	/* Return success (>= 0) */
}

/*
 * __do_callback()
 *
 * This is the dispatcher routine for the callback service. It is
 * very simple as you can see.
 */
static void
__do_callback(rqstp, transp)
	struct svc_req *rqstp;
	register SVCXPRT *transp;
{
	union {
		cback_data 	callback_recieve_1_arg;
		nis_error	callback_error_1_arg;
	} argument;
	int  	result;
	bool_t  do_reply;
	bool_t (*xdr_argument)(), (*xdr_result)();
	bool_t (*local)();
	struct callback_data *__cbdata;


	__cbdata = my_cbdata();
	switch (rqstp->rq_proc) {
	case NULLPROC:
		(void) svc_sendreply(transp, xdr_void, (char *)NULL);
		return;

	case CBPROC_RECEIVE:
		xdr_argument = xdr_cback_data;
		xdr_result = xdr_bool;
		local = __callback_stub;
		(__cbdata->results)++; /* Count callback */
		break;

	case CBPROC_FINISH:
		xdr_argument = xdr_void;
		xdr_result = xdr_void;
		local = __callback_finish;
		break;

	case CBPROC_ERROR:
		xdr_argument = xdr_nis_error;
		xdr_result = xdr_void;
		local = __callback_error;
		break;

	default:
		svcerr_noproc(transp);
		return;
	}
	memset((char *)&argument, 0, sizeof (argument));
	if (!svc_getargs(transp, xdr_argument, (char *) &argument)) {
		svcerr_decode(transp);
		return;
	}
	do_reply = (*local)(&argument, rqstp, __cbdata, &result);
	if (do_reply && !svc_sendreply(transp, xdr_result, (char *) &result)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, xdr_argument, (char *) &argument)) {
		syslog(LOG_WARNING, "unable to free arguments");
	}
}
