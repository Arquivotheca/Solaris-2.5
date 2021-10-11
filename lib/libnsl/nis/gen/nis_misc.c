/*
 *	nis_misc.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_misc.c	1.10	93/05/11 SMI"

/*
 * nis_misc.c
 *
 * This module contains miscellaneous library functions.
 */

#include <string.h>
#include <syslog.h>
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>
#include <tiuser.h>
#include <netdir.h>
#include <netinet/in.h>
#include "nis_clnt.h"
#include "nis_local.h"

static void nis_sort_server_endpoints_inet(nis_server *);
extern void *__inet_get_local_interfaces();


int
__clear_directory_local(nis_name n)
{
	return (1);
}

int (*__clear_directory_ptr)(nis_name) = __clear_directory_local;

/*
 * This internal function free the log_result structure
 */
static
void
destroy_result(void * result_ptr)
{
	if (result_ptr)
		free(result_ptr);
}

/*
 * __nis_pingproc()
 *
 * This function will send a  ping "message" to a remote server.
 * It doesn't bother to see if there are any results since the ping
 * is defined to be unreliable.
 */
void
__nis_pingproc(srv, name, mtime)
	nis_server	*srv;	/* Server to talk to 		*/
	nis_name	name;	/* Directory that changed 	*/
	u_long		mtime;	/* When it changed		*/
{
	CLIENT		*clnt;
	ping_args	args;
	struct timeval	tv;

	clnt = nis_make_rpchandle(srv, 0, NIS_PROG, NIS_VERSION, ZMH_DG, 0, 0);
	if (! clnt)
		return;

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	args.dir = name;
	args.stamp = mtime;
	(void) clnt_call(clnt, NIS_PING, xdr_ping_args, (char *) &args,
			xdr_void, 0, tv);
	clnt_destroy(clnt);
}

/*
 * nis_ping(name);
 *
 * This function is used to ping all of the server that serve a given directory.
 * the point of the ping is to inform them that something has changed in the
 * directory and they should go off and find what it is. Note we avoid pinging
 * server[0] because this is the master server. If it doesn't know that
 * something changed then we are in trouble! The object parameter is
 * optional for clients (REQUIRED FOR SERVERS) this is the object
 * describing the directory.
 */
void
nis_ping(name, mtime, obj)
	nis_name	name;
	u_long		mtime;
	nis_object	*obj;
{
	nis_server	**srvs;
	nis_server	*s, *list;
	int		i, ns;

	if (obj) {
		if (name == 0)
			name = obj->DI_data.do_name;
		list = obj->DI_data.do_servers.do_servers_val;
		ns = obj->DI_data.do_servers.do_servers_len;
		for (i = 1, s = &(list[1]); i < ns; i++, s = &(list[i]))
			__nis_pingproc(s, name, mtime);
	} else {
		srvs = nis_getservlist(name);
		if (! srvs)
			return;
		/* NB: start at 1 'cuz srv[0] == MASTER */
		for (i = 1, s = srvs[1]; s; i++, s = srvs[i])
			__nis_pingproc(s, name, mtime);
		nis_freeservlist(srvs);
	}
}


/*
 * nis_dumplog_r(host, name, time)
 *
 * This function will dump log entries from the indicated host to the
 * caller. It is used by the replica servers to get the updates that have
 * occurred on a directory since the indicated time.
 */

log_result *
nis_dumplog_r(host, name, dtime, result_ptr)
	nis_server	*host;	/* Server to talk to		*/
	nis_name	name;	/* Directory name to dump.	*/
	u_long		dtime;	/* Last _valid_ timestamp.	*/
	log_result	*result_ptr;
{
	CLIENT			*clnt;
	dump_args		da;
	struct timeval		tv;
	enum clnt_stat		stat;

	memset((char *)result_ptr, 0, sizeof (*result_ptr));
	clnt = nis_make_rpchandle(host, 0, NIS_PROG, NIS_VERSION,
				ZMH_VC+ZMH_AUTH, 0, 0);
	if (! clnt) {
		result_ptr->lr_status = NIS_NAMEUNREACHABLE;
		return (result_ptr);
	}
	memset((char *)&da, 0, sizeof (da));
	da.da_dir = name;
	da.da_time = dtime;
	tv.tv_sec = NIS_DUMP_TIMEOUT;
	tv.tv_usec = 0;
	stat = clnt_call(clnt, NIS_DUMPLOG,
			xdr_dump_args, (char *) &da,
			xdr_log_result, (char *) result_ptr, tv);
	auth_destroy(clnt->cl_auth);
	clnt_destroy(clnt);
	if (stat != RPC_SUCCESS)
		result_ptr->lr_status = NIS_RPCERROR;

	return (result_ptr);
}

log_result *
nis_dumplog(host, name, dtime)
	nis_server	*host;	/* Server to talk to		*/
	nis_name	name;	/* Directory name to dump.	*/
	u_long		dtime;	/* Last _valid_ timestamp.	*/
{
	static			thread_key_t result_key;
	static			log_result result_main;
	log_result		*result_ptr;

	if (_thr_main())
		result_ptr = &result_main;
	else {
		result_ptr = (log_result *)
			thr_get_storage(&result_key, sizeof (*result_ptr),
			destroy_result);
		if (result_ptr == NULL) {
#ifdef DEBUG
			printf("nis_dumplog:thr_get_storage return NULL\n");
#endif
			syslog(LOG_ERR, "nis_dumplog: Client out of memory.");
			return (NULL);
		}
	}
	return (nis_dumplog_r(host, name, dtime, result_ptr));
}



/*
 * nis_dump_r(host, name, cback, result_ptr)
 *
 * This function will dump an entire directory from the indicated host.
 * It uses a callback function to minimize the memory requirements on
 * the client and server.
 */

log_result *
nis_dump_r(host, name, cback, result_ptr)
	nis_server	*host;	/* Server to talk to		*/
	nis_name	name;	/* Directory name to dump.	*/
	int		(*cback)(); /* Callback function	*/
	log_result	*result_ptr;
{
	CLIENT			*clnt;
	dump_args		da;
	struct timeval		tv;
	enum clnt_stat		stat;
	int			err;

	memset((char *)result_ptr, 0, sizeof (*result_ptr));
	clnt = nis_make_rpchandle(host, 0, NIS_PROG, NIS_VERSION,
				ZMH_VC+ZMH_AUTH, 0, 0);
	if (!clnt) {
		result_ptr->lr_status = NIS_NAMEUNREACHABLE;
		return (result_ptr);
	}
	memset((char *)&da, 0, sizeof (da));
	da.da_dir = name;
	da.da_time = 0;
	da.da_cbhost.da_cbhost_len = 1;
	da.da_cbhost.da_cbhost_val = __nis_init_callback(clnt, cback, NULL);
	if (! da.da_cbhost.da_cbhost_val) {
		result_ptr->lr_status = NIS_CBERROR;
		auth_destroy(clnt->cl_auth);
		clnt_destroy(clnt);
		return (result_ptr);
	}

	/*
	 * The value of the NIS_DUMP_TIMEOUT is applicable only for the
	 * dump to get initiated.
	 */
	tv.tv_sec = NIS_DUMP_TIMEOUT;
	tv.tv_usec = 0;
	stat = clnt_call(clnt, NIS_DUMP, xdr_dump_args, (char *) &da,
			xdr_log_result, (char *) result_ptr, tv);
	if (stat != RPC_SUCCESS) {
		result_ptr->lr_status = NIS_RPCERROR;
	} else if (result_ptr->lr_status == NIS_CBRESULTS) {
		(*__clear_directory_ptr)(name);
		err = __nis_run_callback(&(result_ptr->lr_cookie),
					NIS_CALLBACK, 0, clnt);
		if (err < 0)
			result_ptr->lr_status = NIS_CBERROR;
	}
	auth_destroy(clnt->cl_auth);
	clnt_destroy(clnt);
	return (result_ptr);
}

log_result *
nis_dump(host, name, cback)
	nis_server	*host;	/* Server to talk to		*/
	nis_name	name;	/* Directory name to dump.	*/
	int		(*cback)(); /* Callback function	*/
{
	static			thread_key_t result_key;
	static			log_result result_main;
	log_result		*result_ptr;

	if (_thr_main())
		result_ptr = &result_main;
	else {
		result_ptr = (log_result *)
			thr_get_storage(&result_key, sizeof (*result_ptr),
			destroy_result);
		if (result_ptr == NULL) {
#ifdef DEBUG
			printf("nis_dump:thr_get_storage return NULL\n");
#endif
			syslog(LOG_ERR, "nis_dump: Client out of memory.");
			return (NULL);
		}
	}
	return (nis_dump_r(host, name, cback, result_ptr));
}

/*
 *  Sort server endpoints so that local addresses appear
 *  before remote addresses.
 */
void
nis_sort_directory_servers(directory_obj *slist)
{
	int i;

	int nsvrs = slist->do_servers.do_servers_len;
	nis_server *svrs = slist->do_servers.do_servers_val;

	for (i = 0; i < nsvrs; i++) {
		nis_sort_server_endpoints_inet(&svrs[i]);
	}
}

static
int
is_local(void *local_interfaces, struct netconfig *ncp, char *uaddr)
{
	struct netbuf *taddr;
	struct sockaddr_in *addr;
	struct in_addr inaddr;
	int local;

	taddr = uaddr2taddr(ncp, uaddr);
	if (taddr == 0) {
		syslog(LOG_ERR, "is_local:  can't convert uaddr %s (%d)",
			uaddr, _nderror);
		return (0);
	}
	addr = (struct sockaddr_in *)taddr->buf;
	if (addr == 0)
		return (0);
	inaddr = addr->sin_addr;
	local = __inet_address_is_local(local_interfaces, inaddr);
	netdir_free((char *)taddr, ND_ADDR);
	return (local);
}

static
int
is_remote(void *local_interfaces, struct netconfig *ncp, char *uaddr)
{
	return (!is_local(local_interfaces, ncp, uaddr));
}

static
void
swap_endpoints(endpoint *e1, endpoint *e2)
{
	char *t;

	t = e1->uaddr;
	e1->uaddr = e2->uaddr;
	e2->uaddr = t;

	t = e1->family;
	e1->family = e2->family;
	e2->family = t;

	t = e1->proto;
	e1->proto = e2->proto;
	e2->proto = t;
}

/*
 *  Sort a list of server endpoints so that address for local interfaces
 *  occur before remote interfaces.  If an error occurs (e.g., no memory),
 *  we just clean up and return; we end up not sorting the endpoints, but
 *  this is just for optimization anyway.
 *
 *  There is a lot of work in this routine, so it should not be called
 *  frequently.
 */
static
void
nis_sort_server_endpoints_inet(nis_server *svr)
{
	int i;
	int j;
	int neps = svr->ep.ep_len;
	endpoint *eps = svr->ep.ep_val;
	struct netconfig *ncp;
	void *local_interfaces;
	void *nch;

	nch = setnetconfig();
	if (nch == 0)
		return;

	/* find any inet entry so we can do uaddr2taddr */
	while ((ncp = getnetconfig(nch)) != 0) {
		if (strcmp(ncp->nc_protofmly, "inet") == 0)
			break;
	}
	if (ncp == 0)
		return;

	local_interfaces = __inet_get_local_interfaces();
	if (local_interfaces == 0) {
		endnetconfig(nch);
		return;
	}

	/*
	 *  Sort endpoints so local inet addresses are first.  The
	 *  variable 'i' points to the beginning of the array,
	 *  and 'j' points to the end.  We advance 'i' as long
	 *  as it indexes a non-inet endpoint or a local endpoint.
	 *  We retract 'j' as long as it indexes a non-inet endpoint
	 *  or a remote endpoint.  If either of these cases fail,
	 *  then 'i' is pointing at a remote endpoint and 'j' is
	 *  pointing at a local endpoint.  We swap them, adjust
	 *  the indexes, and continue.  When the indexes cross
	 *  we are done.
	 */
	i = 0;
	j = neps - 1;
	while (i < j) {
		if (strcmp(eps[i].family, "inet") != 0 ||
		    is_local(local_interfaces, ncp, eps[i].uaddr)) {
			i++;
			continue;
		}

		if (strcmp(eps[j].family, "inet") != 0 ||
		    is_remote(local_interfaces, ncp, eps[j].uaddr)) {
			--j;
			continue;
		}

		swap_endpoints(&eps[i], &eps[j]);
		i++;
		--j;
	}

	/* clean up */
	__inet_free_local_interfaces(local_interfaces);
	endnetconfig(nch);
}
