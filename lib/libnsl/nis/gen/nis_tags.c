/*
 *	nis_tags.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_tags.c	1.9	93/12/01 SMI"

/*
 * nis_tags.c
 *
 * This module contains the library functions that manipulate the
 * server state and statistics. It also includes the implementations
 * nis_getservlist and nis_freeservlist
 */

#include <string.h>
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>
#include "nis_clnt.h"
#include "nis_local.h"

/*
 * nis_freeservlist(list)
 *
 * This function will free all of the memory allocated (or partially
 * allocated) for a nis server list.
 */
void
nis_freeservlist(servers)
	nis_server	**servers;
{
	register nis_server	**list;
	int			i;

	list = servers;
	if (! list)
		return;

	for (; *list; list++) {
		if ((*list)->name)
			free((*list)->name);
		if ((*list)->pkey.n_len)
			free((*list)->pkey.n_bytes);
		if ((*list)->ep.ep_val) {
			for (i = 0; i < (*list)->ep.ep_len; i++) {
				if ((*list)->ep.ep_val[i].uaddr)
					free((*list)->ep.ep_val[i].uaddr);
				if ((*list)->ep.ep_val[i].proto)
					free((*list)->ep.ep_val[i].proto);
				if ((*list)->ep.ep_val[i].family)
					free((*list)->ep.ep_val[i].family);
			}
			free((*list)->ep.ep_val);
		}
		free(*list);
	}
	free(servers);
	return;
}

/*
 * nis_getservlist(name)
 *
 * This function will return list of servers for the indicated domain.
 * the first server in the list is the master for that domain, subsequent
 * servers are replicas. The results of this call should be freed with
 * a call to nis_freeservlist().
 */

nis_server **
nis_getservlist(name)
	nis_name	name;
{
	directory_obj	slist;
	nis_server	**res;
	int		ns;	/* Number of servers 	*/
	nis_server	*srvs;	/* Array of servers 	*/
	int		nep;	/* Number of endpoints	*/
	endpoint	*eps;	/* Array of endpoints	*/
	int		i, k;
	nis_error	err;

	err = __nis_CacheBind(name, &slist);
	if (err != NIS_SUCCESS) {
		xdr_free(xdr_directory_obj, (char*)&slist);
		return (NULL);
	}

	ns = slist.do_servers.do_servers_len;
	srvs = slist.do_servers.do_servers_val;

	res = (nis_server **)calloc(ns+1, sizeof (nis_server *));
	if (! res) {
		xdr_free(xdr_directory_obj, (char*)&slist);
		return (NULL);
	}

	for (i = 0; i < ns; i++) {
		res[i] = (nis_server *)calloc(1, sizeof (nis_server));
		if (! res[i]) {
			nis_freeservlist(res);
			xdr_free(xdr_directory_obj, (char*)&slist);
			return (NULL);
		}
		res[i]->name = strdup(srvs[i].name);
		if (! res[i]->name) {
			xdr_free(xdr_directory_obj, (char*)&slist);
			nis_freeservlist(res);
			return (NULL);
		}
		if ((srvs[i].key_type != NIS_PK_NONE) && (srvs[i].pkey.n_len)) {
			res[i]->pkey.n_bytes =
				(char *)malloc(srvs[i].pkey.n_len);
			if (!(res[i]->pkey.n_bytes)) {
				nis_freeservlist(res);
				xdr_free(xdr_directory_obj, (char*)&slist);
				return (NULL);
			}
			memcpy(res[i]->pkey.n_bytes, srvs[i].pkey.n_bytes,
				srvs[i].pkey.n_len);
			res[i]->pkey.n_len = srvs[i].pkey.n_len;
			res[i]->key_type = srvs[i].key_type;
		}

		nep = srvs[i].ep.ep_len;
		eps = srvs[i].ep.ep_val;
		res[i]->ep.ep_len = nep;
		res[i]->ep.ep_val = (endpoint *)calloc(nep, sizeof (endpoint));
		if (! res[i]->ep.ep_val) {
			nis_freeservlist(res);
			xdr_free(xdr_directory_obj, (char*)&slist);
			return (NULL);
		}
		for (k = 0; k < nep; k++) {
			res[i]->ep.ep_val[k].uaddr = strdup(eps[k].uaddr);
			if (! res[i]->ep.ep_val[k].uaddr) {
				nis_freeservlist(res);
				xdr_free(xdr_directory_obj, (char*)&slist);
				return (NULL);
			}
			res[i]->ep.ep_val[k].family = strdup(eps[k].family);
			if (! res[i]->ep.ep_val[k].family) {
				nis_freeservlist(res);
				xdr_free(xdr_directory_obj, (char*)&slist);
				return (NULL);
			}
			res[i]->ep.ep_val[k].proto = strdup(eps[k].proto);
			if (! res[i]->ep.ep_val[k].proto) {
				nis_freeservlist(res);
				xdr_free(xdr_directory_obj, (char*)&slist);
				return (NULL);
			}
		}
	}
	return (res);
}

/*
 * nis_tagproc(server, proc, tags, num);
 *
 * This internal function can call either of the tag list functions.
 * Both nis_status and nis_servstate call it with a different procedure
 * number.
 */
static nis_error
__nis_tagproc(srv, proc, tags, ntags, result)
	nis_server	*srv;	/* Server to talk to 	*/
	u_long		proc;	/* Procedure to call 	*/
	nis_tag		*tags;	/* Tags to send		*/
	int		ntags;	/* The number available	*/
	nis_tag		**result; /* the resulting tags */
{
	CLIENT		*clnt;
	nis_taglist	tlist, tresult;
	int		i;
	enum clnt_stat	stat;
	struct timeval	tv;

	clnt = nis_make_rpchandle(srv, 0, NIS_PROG, NIS_VERSION,
						ZMH_DG+ZMH_AUTH, 0, 0);
	if (! clnt)
		return (NIS_NAMEUNREACHABLE);

	tv.tv_sec = NIS_TAG_TIMEOUT;
	tv.tv_usec = 0;
	tlist.tags.tags_len = ntags;
	tlist.tags.tags_val = tags;
	memset((char *)&tresult, 0, sizeof (tresult));

	stat = clnt_call(clnt, proc, xdr_nis_taglist, (char *) &tlist,
			xdr_nis_taglist, (char *) &tresult, tv);
	if (stat != RPC_SUCCESS) {
		*result = NULL;
	} else
		*result = tresult.tags.tags_val;
	if (clnt->cl_auth)
		auth_destroy(clnt->cl_auth);
	clnt_destroy(clnt);
	return ((*result) ? NIS_SUCCESS : NIS_RPCERROR);
}

/*
 * nis_status(server, tags, num);
 *
 * This function is used to retrieve statistics from the NIS server.
 * The variable 'server' contains a pointer to a struct nis_server
 * which has the name of the server one wishes to gather statistics
 * from.
 */
nis_error
nis_stats(srv, tags, ntags, result)
	nis_server	*srv;
	nis_tag		*tags;
	int		ntags;
	nis_tag		**result;
{
	return (__nis_tagproc(srv, NIS_STATUS, tags, ntags, result));
}

/*
 * nis_servstate(server, tags, num);
 *
 * This function is used to set state variables on a particular server
 * The variable 'server' contains a pointer to a struct nis_server
 * which has the name of the server one wishes to gather statistics
 * from.
 */
nis_error
nis_servstate(srv, tags, ntags, result)
	nis_server	*srv;
	nis_tag		*tags;
	int		ntags;
	nis_tag		**result;
{
	return (__nis_tagproc(srv, NIS_SERVSTATE, tags, ntags, result));
}

/*
 * nis_freetags()
 *
 * This function frees up memory associated with the result of a tag
 * based call. It must be called to free a taglist returned by nis_stats
 * or nis_servstate;
 */

void
nis_freetags(tags, ntags)
	nis_tag	*tags;
	int	ntags;
{
	int	i;

	if (! tags)
		return;
	for (i = 0; i < ntags; i++) {
		if (tags[i].tag_val)
			free(tags[i].tag_val);
	}
	free(tags);
	return;
}
