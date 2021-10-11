/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

#ident	"@(#)clnt_generic.c	1.4	90/02/13 SMI"

#if !defined(lint) && defined(SCCSIDS)
static char sccsid[] = "@(#)clnt_generic.c 1.32 89/03/16 Copyr 1988 Sun Micro";
#endif

#include <stdio.h>
#include <rpc/rpc.h>
#include <rpc/trace.h>
#include <errno.h>
#include <tiuser.h>
#include <rpc/nettype.h>
#include <netdir.h>
#include <string.h>

/*
 * Generic client creation with version checking the value of
 * vers_out is set to the highest server supported value
 * vers_low <= vers_out <= vers_high  AND an error results
 * if this can not be done.
 */
CLIENT *
clnt_create_vers(hostname, prog, vers_out, vers_low, vers_high, nettype)
	const char *hostname;
	unsigned long prog;
	unsigned long *vers_out;
	unsigned long vers_low;
	unsigned long vers_high;
	const char *nettype;
{
	CLIENT *clnt;
	struct timeval to;
	enum clnt_stat rpc_stat;
	struct rpc_err rpcerr;

	trace4(TR_clnt_create_vers, 0, prog, vers_low, vers_high);
	clnt = clnt_create(hostname, prog, vers_high, nettype);
	if (clnt == NULL) {
		trace4(TR_clnt_create_vers, 1, prog, vers_low, vers_high);
		return (NULL);
	}
	to.tv_sec = 10;
	to.tv_usec = 0;
	rpc_stat = clnt_call(clnt, NULLPROC, (xdrproc_t) xdr_void,
			(char *) NULL, (xdrproc_t) xdr_void, (char *) NULL, to);
	if (rpc_stat == RPC_SUCCESS) {
		*vers_out = vers_high;
		trace4(TR_clnt_create_vers, 1, prog, vers_low, vers_high);
		return (clnt);
	}
	if (rpc_stat == RPC_PROGVERSMISMATCH) {
		unsigned long minvers, maxvers;

		clnt_geterr(clnt, &rpcerr);
		minvers = rpcerr.re_vers.low;
		maxvers = rpcerr.re_vers.high;
		if (maxvers < vers_high)
			vers_high = maxvers;
		if (minvers > vers_low)
			vers_low = minvers;
		if (vers_low > vers_high) {
			goto error;
		}
		CLNT_CONTROL(clnt, CLSET_VERS, (char *)&vers_high);
		rpc_stat = clnt_call(clnt, NULLPROC, (xdrproc_t) xdr_void,
				(char *) NULL, (xdrproc_t) xdr_void,
				(char *) NULL, to);
		if (rpc_stat == RPC_SUCCESS) {
			*vers_out = vers_high;
			trace4(TR_clnt_create_vers, 1, prog,
				vers_low, vers_high);
			return (clnt);
		}
	}
	clnt_geterr(clnt, &rpcerr);

error:	rpc_createerr.cf_stat = rpc_stat;
	rpc_createerr.cf_error = rpcerr;
	clnt_destroy(clnt);
	trace4(TR_clnt_create_vers, 1, prog, vers_low, vers_high);
	return (NULL);
}

/*
 * Top level client creation routine.
 * Generic client creation: takes (servers name, program-number, nettype) and
 * returns client handle. Default options are set, which the user can
 * change using the rpc equivalent of ioctl()'s.
 *
 * It tries for all the netids in that particular class of netid until
 * it succeeds.
 * XXX The error message in the case of failure will be the one
 * pertaining to the last create error.
 *
 * It calls clnt_tp_create();
 */
CLIENT *
clnt_create(hostname, prog, vers, nettype)
	const char *hostname;				/* server name */
	u_long prog;				/* program number */
	u_long vers;				/* version number */
	const char *nettype;				/* net type */
{
	struct netconfig *nconf;
	CLIENT *clnt = NULL;
	void *handle;
	enum clnt_stat	save_cf_stat = RPC_SUCCESS;
	struct rpc_err	save_cf_error;

	trace3(TR_clnt_create, 0, prog, vers);
	if ((handle = __rpc_setconf((char *) nettype)) == NULL) {
		rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
		trace3(TR_clnt_create, 1, prog, vers);
		return ((CLIENT *)NULL);
	}

	rpc_createerr.cf_stat = RPC_SUCCESS;
	while (clnt == (CLIENT *)NULL) {
		if ((nconf = __rpc_getconf(handle)) == NULL) {
			if (rpc_createerr.cf_stat == RPC_SUCCESS)
				rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
			break;
		}
		clnt = clnt_tp_create(hostname, prog, vers, nconf);
		if (clnt)
			break;
		else
			/*
			 *	Since we didn't get a name-to-address
			 *	translation failure here, we remember
			 *	this particular error.  The object of
			 *	this is to enable us to return to the
			 *	caller a more-specific error than the
			 *	unhelpful ``Name to address translation
			 *	failed'' which might well occur if we
			 *	merely returned the last error (because
			 *	the local loopbacks are typically the
			 *	last ones in /etc/netconfig and the most
			 *	likely to be unable to translate a host
			 *	name).
			 */
			if (rpc_createerr.cf_stat != RPC_N2AXLATEFAILURE) {
				save_cf_stat = rpc_createerr.cf_stat;
				save_cf_error = rpc_createerr.cf_error;
			}
	}

	/*
	 *	Attempt to return an error more specific than ``Name to address
	 *	translation failed''
	 */
	if ((rpc_createerr.cf_stat == RPC_N2AXLATEFAILURE) &&
		(save_cf_stat != RPC_SUCCESS)) {
		rpc_createerr.cf_stat = save_cf_stat;
		rpc_createerr.cf_error = save_cf_error;
	}
	__rpc_endconf(handle);
	trace3(TR_clnt_create, 1, prog, vers);
	return (clnt);
}

/*
 * Generic client creation: takes (servers name, program-number, netconf) and
 * returns client handle. Default options are set, which the user can
 * change using the rpc equivalent of ioctl()'s : clnt_control()
 * It finds out the server address from rpcbind and calls clnt_tli_create()
 */
CLIENT *
clnt_tp_create(hostname, prog, vers, nconf)
	const char *hostname;				/* server name */
	u_long prog;				/* program number */
	u_long vers;				/* version number */
	const struct netconfig *nconf;	/* net config struct */
{
	struct netbuf *svcaddr;			/* servers address */
	CLIENT *cl = NULL;			/* client handle */
	extern struct netbuf *__rpcb_findaddr();

	trace3(TR_clnt_tp_create, 0, prog, vers);
	if (nconf == (struct netconfig *)NULL) {
		rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
		trace3(TR_clnt_tp_create, 1, prog, vers);
		return ((CLIENT *)NULL);
	}
	/*
	 * Get the address of the server
	 */

	if ((svcaddr = __rpcb_findaddr(prog, vers, nconf, hostname,
		&cl)) == NULL) {
		/* appropriate error number is set by rpcbind libraries */
		trace3(TR_clnt_tp_create, 1, prog, vers);
		return ((CLIENT *)NULL);
	}
	if (cl == (CLIENT *)NULL) {
		cl = clnt_tli_create(RPC_ANYFD, nconf, svcaddr,
					prog, vers, 0, 0);
	} else {
		/* Reuse the CLIENT handle and change the appropriate fields */
		if (CLNT_CONTROL(cl, CLSET_SVC_ADDR, (void *)svcaddr) == TRUE) {
			if (cl->cl_netid == NULL)
				cl->cl_netid = strdup(nconf->nc_netid);
			if (cl->cl_tp == NULL)
				cl->cl_tp = strdup(nconf->nc_device);
			(void) CLNT_CONTROL(cl, CLSET_PROG, (void *)&prog);
			(void) CLNT_CONTROL(cl, CLSET_VERS, (void *)&vers);
		} else {
			CLNT_DESTROY(cl);
			cl = clnt_tli_create(RPC_ANYFD, nconf, svcaddr,
					prog, vers, 0, 0);
		}
	}
	netdir_free((char *) svcaddr, ND_ADDR);
	trace3(TR_clnt_tp_create, 1, prog, vers);
	return (cl);
}

/*
 * Generic client creation:  returns client handle.
 * Default options are set, which the user can
 * change using the rpc equivalent of ioctl()'s : clnt_control().
 * If fd is RPC_ANYFD, it will be opened using nconf.
 * It will be bound if not so.
 * If sizes are 0; appropriate defaults will be chosen.
 */
CLIENT *
clnt_tli_create(fd, nconf, svcaddr, prog, vers, sendsz, recvsz)
	register int fd;		/* fd */
	const struct netconfig *nconf;	/* netconfig structure */
	const struct netbuf *svcaddr;		/* servers address */
	u_long prog;			/* program number */
	u_long vers;			/* version number */
	u_int sendsz;			/* send size */
	u_int recvsz;			/* recv size */
{
	CLIENT *cl;			/* client handle */
	struct t_info tinfo;		/* transport info */
	bool_t madefd;			/* whether fd opened here */
	long servtype;

	trace5(TR_clnt_tli_create, 0, prog, vers, sendsz, recvsz);

	if (fd == RPC_ANYFD) {
		if (nconf == (struct netconfig *)NULL) {
			rpc_createerr.cf_stat = RPC_UNKNOWNPROTO;
			trace3(TR_clnt_tli_create, 1, prog, vers);
			return ((CLIENT *)NULL);
		}
		fd = t_open(nconf->nc_device, O_RDWR, NULL);
		if (fd == -1)
			goto err;
		madefd = TRUE;
		if (t_bind(fd, (struct t_bind *)NULL,
			(struct t_bind *)NULL) == -1)
				goto err;

		switch (nconf->nc_semantics) {
		case NC_TPI_CLTS:
			servtype = T_CLTS;
			break;
		case NC_TPI_COTS:
			servtype = T_COTS;
			break;
		case NC_TPI_COTS_ORD:
			servtype = T_COTS_ORD;
			break;
		default:
			if (t_getinfo(fd, &tinfo) == -1)
				goto err;
			servtype = tinfo.servtype;
			break;
		}
	} else {
		int state;		/* Current state of provider */

		/*
		 * Sync the opened fd.
		 * Check whether bound or not, else bind it
		 */
		if (((state = t_sync(fd)) == -1) ||
		    ((state == T_UNBND) && (t_bind(fd, (struct t_bind *)NULL,
				(struct t_bind *)NULL) == -1)) ||
		    (t_getinfo(fd, &tinfo) == -1))
			goto err;
		servtype = tinfo.servtype;
		madefd = FALSE;
	}

	switch (servtype) {
	case T_COTS:
	case T_COTS_ORD:
		cl = clnt_vc_create(fd, svcaddr, prog, vers, sendsz, recvsz);
		break;
	case T_CLTS:

		cl = clnt_dg_create(fd, svcaddr, prog, vers, sendsz, recvsz);
		break;
	default:
		goto err;
	}

	if (cl == (CLIENT *)NULL)
		goto err1; /* borrow errors from clnt_dg/vc creates */
	if (nconf) {
		cl->cl_netid = strdup(nconf->nc_netid);
		cl->cl_tp = strdup(nconf->nc_device);
	} else {
		cl->cl_netid = "";
		cl->cl_tp = "";
	}
	if (madefd)
		(void) CLNT_CONTROL(cl, CLSET_FD_CLOSE, (char *)NULL);

	trace3(TR_clnt_tli_create, 1, prog, vers);
	return (cl);

err:
	rpc_createerr.cf_stat = RPC_TLIERROR;
	rpc_createerr.cf_error.re_errno = errno;
	rpc_createerr.cf_error.re_terrno = t_errno;
err1:	if (madefd)
		(void) t_close(fd);
	trace3(TR_clnt_tli_create, 1, prog, vers);
	return ((CLIENT *)NULL);
}
