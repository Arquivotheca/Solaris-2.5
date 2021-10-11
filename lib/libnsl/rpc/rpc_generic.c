/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

#pragma ident	"@(#)rpc_generic.c	1.22	95/09/19 SMI"

/*
 * rpc_generic.c, Miscl routines for RPC.
 *
 */

#include "rpc_mt.h"
#include <stdio.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <rpc/rpc.h>
#include <rpc/nettype.h>
#include <sys/param.h>
#include <sys/mkdev.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/resource.h>
#include <netconfig.h>
#include <malloc.h>
#include <syslog.h>
#include <string.h>

#if defined(sparc)
#define _STAT _stat
#define _FSTAT _fstat
#else  /* !sparc */
#define _STAT stat
#define _FSTAT fstat
#endif /* sparc */

static struct handle {
	NCONF_HANDLE *nhandle;
	int nflag;		/* Whether NETPATH or NETCONFIG */
	int nettype;
};

struct _rpcnettype {
	const char *name;
	const int type;
} _rpctypelist[] = {
	"netpath", _RPC_NETPATH,
	"visible", _RPC_VISIBLE,
	"circuit_v", _RPC_CIRCUIT_V,
	"datagram_v", _RPC_DATAGRAM_V,
	"circuit_n", _RPC_CIRCUIT_N,
	"datagram_n", _RPC_DATAGRAM_N,
	"tcp", _RPC_TCP,
	"udp", _RPC_UDP,
	0, _RPC_NONE
};

/*
 * Cache the result of getrlimit(), so we don't have to do an
 * expensive call every time.
 */
int
__rpc_dtbsize()
{
	static int tbsize;
	struct rlimit rl;

	trace1(TR___rpc_dtbsize, 0);
	if (tbsize) {
		trace1(TR___rpc_dtbsize, 1);
		return (tbsize);
	}
	if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
		trace1(TR___rpc_dtbsize, 1);
		return (tbsize = rl.rlim_max);
	}
	/*
	 * Something wrong.  I'll try to save face by returning a
	 * pessimistic number.
	 */
	trace1(TR___rpc_dtbsize, 1);
	return (32);
}


/*
 * Find the appropriate buffer size
 */
u_int
__rpc_get_t_size(size, bufsize)
	int size;	/* Size requested */
	long bufsize;	/* Supported by the transport */
{
	trace3(TR___rpc_get_t_size, 0, size, bufsize);
	if (bufsize == -2) {
		/* transfer of data unsupported */
		trace3(TR___rpc_get_t_size, 1, size, bufsize);
		return ((u_int)0);
	}
	if (size == 0) {
		if ((bufsize == -1) || (bufsize == 0)) {
			/*
			 * bufsize == -1 : No limit on the size
			 * bufsize == 0 : Concept of tsdu foreign. Choose
			 *			a value.
			 */
			trace3(TR___rpc_get_t_size, 1, size, bufsize);
			return ((u_int)RPC_MAXDATASIZE);
		} else {
			trace3(TR___rpc_get_t_size, 1, size, bufsize);
			return ((u_int)bufsize);
		}
	}
	if ((bufsize == -1) || (bufsize == 0)) {
		trace3(TR___rpc_get_t_size, 1, size, bufsize);
		return ((u_int)size);
	}
	/* Check whether the value is within the upper max limit */
	trace3(TR___rpc_get_t_size, 1, size, bufsize);
	return (size > bufsize ? (u_int)bufsize : (u_int)size);
}

/*
 * Find the appropriate address buffer size
 */
u_int
__rpc_get_a_size(size)
	long size;	/* normally tinfo.addr */
{
	trace2(TR___rpc_get_a_size, 0, size);
	if (size >= 0) {
		trace2(TR___rpc_get_a_size, 1, size);
		return ((u_int)size);
	}
	if (size <= -2) {
		trace2(TR___rpc_get_a_size, 1, size);
		return ((u_int)0);
	}
	/*
	 * (size == -1) No limit on the size. we impose a limit here.
	 */
	trace2(TR___rpc_get_a_size, 1, size);
	return ((u_int)RPC_MAXADDRSIZE);
}

static char *
strlocase(p)
	char *p;
{
	char *t = p;

	trace1(TR_strlocase, 0);
	for (; *p; p++)
		if (isupper(*p))
			*p = tolower(*p);
	trace1(TR_strlocase, 1);
	return (t);
}

/*
 * Returns the type of the network as defined in <rpc/nettype.h>
 * If nettype is NULL, it defaults to NETPATH.
 */
static int
getnettype(nettype)
	char *nettype;
{
	int i;

	trace1(TR_getnettype, 0);
	if ((nettype == NULL) || (nettype[0] == NULL)) {
		trace1(TR_getnettype, 1);
		return (_RPC_NETPATH);	/* Default */
	}

	nettype = strlocase(nettype);
	for (i = 0; _rpctypelist[i].name; i++)
		if (strcmp(nettype, _rpctypelist[i].name) == 0) {
			trace1(TR_getnettype, 1);
			return (_rpctypelist[i].type);
		}
	trace1(TR_getnettype, 1);
	return (_rpctypelist[i].type);
}

/*
 * For the given nettype (tcp or udp only), return the first structure found.
 * This should be freed by calling freenetconfigent()
 */
struct netconfig *
__rpc_getconfip(nettype)
	char *nettype;
{
	char *netid;
	char *netid_tcp = (char *) NULL;
	char *netid_udp = (char *) NULL;
	static char *netid_tcp_main;
	static char *netid_udp_main;
	static thread_key_t tcp_key, udp_key;
	int main_thread;
	struct netconfig *dummy;
	extern mutex_t tsd_lock;

	trace1(TR___rpc_getconfip, 0);
	if ((main_thread = _thr_main())) {
		netid_udp = netid_udp_main;
		netid_tcp = netid_tcp_main;
	} else {
		if (tcp_key == 0) {
			mutex_lock(&tsd_lock);
			if (tcp_key == 0)
				thr_keycreate(&tcp_key, free);
			mutex_unlock(&tsd_lock);
		}
		thr_getspecific(tcp_key, (void **) &netid_tcp);
		if (udp_key == 0) {
			mutex_lock(&tsd_lock);
			if (udp_key == 0)
				thr_keycreate(&udp_key, free);
			mutex_unlock(&tsd_lock);
		}
		thr_getspecific(udp_key, (void **) &netid_udp);
	}
	if (!netid_udp && !netid_tcp) {
		struct netconfig *nconf;
		void *confighandle;

		if (!(confighandle = setnetconfig())) {
			trace1(TR___rpc_getconfip, 1);
			return (NULL);
		}
		while (nconf = getnetconfig(confighandle)) {
			if (strcmp(nconf->nc_protofmly, NC_INET) == 0) {
				if (strcmp(nconf->nc_proto, NC_TCP) == 0) {
					netid_tcp = strdup(nconf->nc_netid);
					if (main_thread)
						netid_tcp_main = netid_tcp;
					else
						thr_setspecific(tcp_key,
							(void *) netid_tcp);
				} else
				if (strcmp(nconf->nc_proto, NC_UDP) == 0) {
					netid_udp = strdup(nconf->nc_netid);
					if (main_thread)
						netid_udp_main = netid_udp;
					else
						thr_setspecific(udp_key,
							(void *) netid_udp);
				}
			}
		}
		endnetconfig(confighandle);
	}
	if (strcmp(nettype, "udp") == 0)
		netid = netid_udp;
	else if (strcmp(nettype, "tcp") == 0)
		netid = netid_tcp;
	else {
		trace1(TR___rpc_getconfip, 1);
		return ((struct netconfig *)NULL);
	}
	if ((netid == NULL) || (netid[0] == NULL)) {
		trace1(TR___rpc_getconfip, 1);
		return ((struct netconfig *)NULL);
	}
	dummy = getnetconfigent(netid);
	trace1(TR___rpc_getconfip, 1);
	return (dummy);
}


/*
 * Returns the type of the nettype, which should then be used with
 * __rpc_getconf().
 */
void *
__rpc_setconf(nettype)
	char *nettype;
{
	struct handle *handle;

	trace1(TR___rpc_setconf, 0);
	handle = (struct handle *) malloc(sizeof (struct handle));
	if (handle == NULL) {
		trace1(TR___rpc_setconf, 1);
		return (NULL);
	}
	switch (handle->nettype = getnettype(nettype)) {
	case _RPC_NETPATH:
	case _RPC_CIRCUIT_N:
	case _RPC_DATAGRAM_N:
		if (!(handle->nhandle = setnetpath())) {
			free(handle);
			trace1(TR___rpc_setconf, 1);
			return (NULL);
		}
		handle->nflag = TRUE;
		break;
	case _RPC_VISIBLE:
	case _RPC_CIRCUIT_V:
	case _RPC_DATAGRAM_V:
	case _RPC_TCP:
	case _RPC_UDP:
		if (!(handle->nhandle = setnetconfig())) {
			free(handle);
			trace1(TR___rpc_setconf, 1);
			return (NULL);
		}
		handle->nflag = FALSE;
		break;
	default:
		trace1(TR___rpc_setconf, 1);
		return (NULL);
	}

	trace1(TR___rpc_setconf, 1);
	return (handle);
}

/*
 * Returns the next netconfig struct for the given "net" type.
 * __rpc_setconf() should have been called previously.
 */
struct netconfig *
__rpc_getconf(vhandle)
	void *vhandle;
{
	struct handle *handle;
	struct netconfig *nconf;

	trace1(TR___rpc_getconf, 0);
	handle = (struct handle *)vhandle;
	if (handle == NULL) {
		trace1(TR___rpc_getconf, 1);
		return (NULL);
	}
	/*CONSTANTCONDITION*/
	while (1) {
		if (handle->nflag)
			nconf = getnetpath(handle->nhandle);
		else
			nconf = getnetconfig(handle->nhandle);
		if (nconf == (struct netconfig *)NULL)
			break;
		if ((nconf->nc_semantics != NC_TPI_CLTS) &&
			(nconf->nc_semantics != NC_TPI_COTS) &&
			(nconf->nc_semantics != NC_TPI_COTS_ORD))
			continue;
		switch (handle->nettype) {
		case _RPC_VISIBLE:
			if (!(nconf->nc_flag & NC_VISIBLE))
				continue;
			/*FALLTHROUGH*/
		case _RPC_NETPATH:	/* Be happy */
			break;
		case _RPC_CIRCUIT_V:
			if (!(nconf->nc_flag & NC_VISIBLE))
				continue;
			/*FALLTHROUGH*/
		case _RPC_CIRCUIT_N:
			if ((nconf->nc_semantics != NC_TPI_COTS) &&
				(nconf->nc_semantics != NC_TPI_COTS_ORD))
				continue;
			break;
		case _RPC_DATAGRAM_V:
			if (!(nconf->nc_flag & NC_VISIBLE))
				continue;
			/*FALLTHROUGH*/
		case _RPC_DATAGRAM_N:
			if (nconf->nc_semantics != NC_TPI_CLTS)
				continue;
			break;
		case _RPC_TCP:
			if (((nconf->nc_semantics != NC_TPI_COTS) &&
				(nconf->nc_semantics != NC_TPI_COTS_ORD)) ||
				strcmp(nconf->nc_protofmly, NC_INET) ||
				strcmp(nconf->nc_proto, NC_TCP))
				continue;
			break;
		case _RPC_UDP:
			if ((nconf->nc_semantics != NC_TPI_CLTS) ||
				strcmp(nconf->nc_protofmly, NC_INET) ||
				strcmp(nconf->nc_proto, NC_UDP))
				continue;
			break;
		}
		break;
	}
	trace1(TR___rpc_getconf, 1);
	return (nconf);
}

void
__rpc_endconf(vhandle)
	void * vhandle;
{
	struct handle *handle;

	trace1(TR___rpc_endconf, 0);
	handle = (struct handle *) vhandle;
	if (handle == NULL) {
		trace1(TR___rpc_endconf, 1);
		return;
	}
	if (handle->nflag) {
		endnetpath(handle->nhandle);
	} else {
		endnetconfig(handle->nhandle);
	}
	free(handle);
	trace1(TR___rpc_endconf, 1);
}

/*
 * Used to ping the NULL procedure for clnt handle.
 * Returns NULL if fails, else a non-NULL pointer.
 */
void *
rpc_nullproc(clnt)
	CLIENT *clnt;
{
	struct timeval TIMEOUT = {25, 0};

	trace2(TR_rpc_nullproc, 0, clnt);
	if (clnt_call(clnt, NULLPROC, (xdrproc_t) xdr_void, (char *)NULL,
		(xdrproc_t) xdr_void, (char *)NULL, TIMEOUT) != RPC_SUCCESS) {
		trace2(TR_rpc_nullproc, 1, clnt);
		return ((void *) NULL);
	}
	trace2(TR_rpc_nullproc, 1, clnt);
	return ((void *) clnt);
}

/*
 * Given a fd, find the transport device it is using and return the
 * netconf entry corresponding to it.
 * Note: It assumes servtpe parameter is 0 when uninitialized.
 *	That is true for xprt->xp_type field.
 */
struct netconfig * __rpcfd_to_nconf(fd, servtype)
int fd, servtype;
{
	struct stat statbuf;
	void *hndl;
	struct netconfig *nconf, *newnconf = NULL;
	major_t fdmajor;
	struct t_info tinfo;

	trace2(TR___rpcfd_to_nconf, 0, fd);
	if (_FSTAT(fd, &statbuf) == -1) {
		trace2(TR___rpcfd_to_nconf, 1, fd);
		return (NULL);
	}

	fdmajor = major(statbuf.st_rdev);
	if (servtype == 0) {
		if (t_getinfo(fd, &tinfo) == -1) {
			(void) syslog(LOG_ERR, "__rpcfd_to_nconf : %s",
					"could not get transport information");
			trace2(TR___rpcfd_to_nconf, 1, fd);
			return (NULL);
		}
		servtype = tinfo.servtype;
	}

	hndl = setnetconfig();
	if (hndl == NULL) {
		trace2(TR___rpcfd_to_nconf, 1, fd);
		return (NULL);
	}
	/*
	 * Go through all transports listed in /etc/netconfig looking for
	 *	transport device in use on fd.
	 * - Match on service type first
	 * - if that succeeds, match on major numbers (used for new local
	 *	transport code that is self cloning)
	 * - if that fails, assume transport device uses clone driver
	 *	and try match the fdmajor with minor number of device path
	 *	which will be the major number of transport device since it
	 *	uses the clone driver.
	 */

	while (nconf = getnetconfig(hndl)) {
		if (__rpc_matchserv(servtype, nconf->nc_semantics) == TRUE) {
			if (!_STAT(nconf->nc_device, &statbuf)) {
				if (fdmajor == major(statbuf.st_rdev))
					break; /* self cloning driver ? */
				if (fdmajor == minor(statbuf.st_rdev))
					break; /* clone driver! */
			}
		}
	}
	if (nconf)
		newnconf = getnetconfigent(nconf->nc_netid);
	endnetconfig(hndl);
	trace2(TR___rpcfd_to_nconf, 1, fd);
	return (newnconf);
}

int
__rpc_matchserv(servtype, nc_semantics)
int servtype;
unsigned long nc_semantics;
{
	switch (servtype) {
	case T_COTS:
		if (nc_semantics == NC_TPI_COTS)
			return (TRUE);
		break;

	case T_COTS_ORD:
		if (nc_semantics == NC_TPI_COTS_ORD)
			return (TRUE);
		break;

	case T_CLTS:
		if (nc_semantics == NC_TPI_CLTS)
			return (TRUE);
		break;

	default:
		/* FALSE! */
		break;

	}
	return (FALSE);
}
