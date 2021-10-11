/*
 *	nis_lookup.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_lookup.c	1.27	95/04/05 SMI"

/*
 *	nis_lookup.c
 *
 * This module contains just the core lookup functions.
 */
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>
#include "nis_clnt.h"
#include "nis_local.h"

unsigned __nis_max_hard_lookup_time = 300;

/*
 * The bones of the lookup and list function:  this function binds to, then
 * calls the appropriate NIS+ server for the given name. If HARD_LOOKUP
 * is set, then it blocks until it gets an answer.  We do HARD_LOOKUP by
 * retrying as long as "tryagain" is set.  We clear the cache occasionally
 * so that we will rebind properly if we are not able to communicate
 * with any server for a while and then suddenly can (e.g., network
 * down for a while and then it comes back up, or if all servers are
 * rebooting and just arent' available yet (e.g., after a power failure)).
 *
 * NB: This function now follows links if flags is set up correctly. This
 * localized this policy to this function and eliminated about 4
 * implementations of the same code in other modules.
 */
nis_result *
__nis_core_lookup(req, flags, list_op, cbdata, cback)
	ib_request	*req;		/* name parameters		*/
	u_long		flags;		/* user flags			*/
	int		list_op;	/* list semantics 		*/
	void		*cbdata;	/* Callback data		*/
	int		(*cback)();	/* Callback (for list calls) 	*/
{
	nis_result	*res;		/* the result we return */
	ns_request 	nsr;		/* for lookup calls	*/
	directory_obj	slist;		/* to hold binding info	*/
	enum clnt_stat	stat;		/* rpc lib status	*/
	CLIENT		*cur_server;	/* server handle	*/
	struct timeval	tv;		/* timeout		*/
	int		error, linknum,	/* state vars		*/
			newname,	/* binding flag	*/
			binding_policy;	/* true for name_first binding */
	char		curname[NIS_MAXNAMELEN]; /* name used	*/
	ib_request	local_req;	/* local version of request */
	nis_object	link_obj;	/* used when following links */
	int		soft_errors = 0;
	int		soft_error_limit;
	int		flushed_cache = 0;
	unsigned long	aticks = 0,	/* profiling variables	*/
			cticks = 0,
			dticks = 0,
			zticks = 0;
	int		tryagain = (flags & HARD_LOOKUP);
	int		have_binding = 0;
	u_int		times_thru, sec;

	res = (nis_result *)calloc(1, sizeof (nis_result));
	if (! res)
		return (NULL);	/* Crash and burn */

	/* set up timeout, used by clnt_call below */
	tv.tv_sec = NIS_GEN_TIMEOUT;
	tv.tv_usec = 0;

	/*
	 * start out using the request as passed to us, we clone it
	 * to prevent damage to the user's version. Then we use a local
	 * name buffer that we can update when we're following links.
	 */
	strcpy(curname, req->ibr_name);
	local_req = *req;
	local_req.ibr_name = &(curname[0]);

	memset((char *)&link_obj, 0, sizeof (nis_object));
	linknum = 0;

	/*
	 * NOTE : Binding bug : due to the implementation of the
	 *	NIS+ service, when you want to list a directory you
	 *	need to bind not to a server that serves the directory
	 *	object, but to a server that serves the directory itself.
	 *	This will often be different machines.
	 *
	 *	However, we can mitigate the impact of always trying to
	 *	bind to the table if we're searching a table by checking
	 *	for search criteria. (listing directories can't have a
	 *	search criteria). So if we're a list, and we don't have
	 *	a search criteria, bind to the "name" passed first. Otherwise
	 *	attempt to bind to domain_of(name) first.
	 *
	 *	NIS_S_SUCCESS means the cache knew about the directory but
	 *	the directory object has expired.
	 */
	binding_policy = (list_op && (local_req.ibr_srch.ibr_srch_len == 0));
	newname = 1;
	flushed_cache = 0;
	times_thru = 0;
	while (1) {
		times_thru++;
		/* get a directory obj describing the desired dir */
		if (newname || ! have_binding) {
			memset((char *)&slist, 0, sizeof (directory_obj));
			res = __nis_make_binding(&res, curname, &slist,
						binding_policy);
			if (res->status != NIS_SUCCESS &&
			    res->status != NIS_S_SUCCESS) {
				have_binding = 0;
			} else {
				have_binding = 1;
			}
			/* Determine what the soft error limit should be */
			if (flags&MASTER_ONLY)
				soft_error_limit = 1;
			else
				soft_error_limit =
					slist.do_servers.do_servers_len;
			aticks += res->aticks;

			soft_errors = 0;
			newname = 0;
		}

		/* if no binding or previous error, return error we received */
		if ((res->status != NIS_SUCCESS) &&
		    (res->status != NIS_S_SUCCESS)) {
			/* check to see if we were following a link */
			if (__type_of(&link_obj) == LINK_OBJ) {
				res->objects.objects_val =
					    nis_clone_object(&link_obj, NULL);
				if (! res->objects.objects_val) {
					res->status = NIS_NOMEMORY;
					break;
				}
				res->objects.objects_len = 1;
				res->status = NIS_LINKNAMEERROR;
				/* free our copy */
				xdr_free(xdr_nis_object, (char *) &link_obj);
			}
			/*
			 *  We break on NIS_NOSUCHNAME, NIS_NOSUCHTABLE,
			 *  and NIS_BADNAME, even on HARD_LOOKUP, because
			 *  they will never succeed.
			 */
			if (! tryagain ||
			    res->status == NIS_NOSUCHNAME ||
			    res->status == NIS_NOSUCHTABLE ||
			    res->status == NIS_BADNAME) {
				break;
			}
		}


		/*
		 * select a server, from the list that serve dir
		 * note: No way to get authenication error back from
		 * this function call.  We can only do this if we
		 * have a binding.
		 */
		if (have_binding)
			cur_server = __nis_get_server(&slist, flags);
		else
			cur_server = 0;

		if ((! have_binding || ! cur_server) && (flags & HARD_LOOKUP)) {
			syslog(LOG_WARNING,
			    "NIS+ server for %s not responding, still trying",
								curname);
			if (flags & HARD_LOOKUP) {
				/*
				 * We have to practice an exponential backoff
				 * to keep this code from abusing the network
				 * when we run with HARD_LOOKUP enabled.
				 * __nis_max_hard_lookup_time is max time to
				 * wait (currently 5 minutes)
				 *
				 * Yes this code could be better but it should
				 * not be called very often either.  If it is
				 * efficiency is the least of your problems.
				 */
				sec = 2 << times_thru;
				if (sec > __nis_max_hard_lookup_time) {
					sec = __nis_max_hard_lookup_time;
					--times_thru;
				}
				sleep(sec);
			} else
				sleep(NIS_HARDSLEEP);
			if (flushed_cache == 0 && have_binding) {
				__nis_CacheRemoveEntry(&slist);
				flushed_cache = 1;
				newname = 1;
				xdr_free(xdr_directory_obj, (char *) &slist);
				res->status = NIS_SUCCESS; /* try again */
			}
			tryagain = 1;
			continue;
		} else if (! cur_server) {
			/* may in fact be an auth error */
			res->status = NIS_NAMEUNREACHABLE;
			res->aticks = aticks;
			xdr_free(xdr_nis_object, (char *) &link_obj);
			break;	/* free slist and return */
		}
		tryagain = 0;    /* we got a server */

		/* set up the callback handle if need be */
		if (cback) {
			local_req.ibr_cbhost.ibr_cbhost_val =
			    __nis_init_callback(cur_server, cback, cbdata);
			if (local_req.ibr_cbhost.ibr_cbhost_val == NULL) {
				res->status = NIS_NOCALLBACK;
				xdr_free(xdr_nis_object, (char *) &link_obj);
				break; /* free slist and return */
			}
			local_req.ibr_cbhost.ibr_cbhost_len = 1;
		}

		/*
		 * Depending on name or list_op either list it or look it up
		 * in the namespace.
		 */
		memset((char *)res, 0, sizeof (nis_result));
		if (list_op) {
			stat = clnt_call(cur_server, NIS_IBLIST,
					xdr_ib_request, (char *) &local_req,
					xdr_nis_result, (char *) res, tv);
		} else {
			memset((char *)&nsr, 0, sizeof (nsr));
			nsr.ns_name = &(curname[0]);
			stat = clnt_call(cur_server, NIS_LOOKUP,
					xdr_ns_request, (char *) &nsr,
					xdr_nis_result, (char *) res, tv);
		}

		/*
		 * note even if link_obj was never used this works because
		 * the xdr_free becomes a no-op. If we were following links
		 * then this cleans up the last object we followed and possibly
		 * invalidates the search attribute pointers in local_req
		 */
		xdr_free(xdr_nis_object, (char *) &link_obj);
		memset((char *) &link_obj, 0, sizeof (nis_object));

		/*
		 * If the RPC failed coerce the status into an NIS_RPCERROR
		 * and try the next server in the list.
		 * If there are no more servers, above code will return a
		 * NIS_NAMEUNREACHABLE error when __nis_get_server() returns
		 * NULL.
		 */
		if (stat == RPC_AUTHERROR) {
			res->status = NIS_SRVAUTH;
		} else if (stat != RPC_SUCCESS) {
			res->status = NIS_RPCERROR;
		}

		/* note the statistics from the result */
		zticks += res->zticks;
		dticks += res->dticks;
		cticks += res->cticks;
		aticks += res->aticks;

		/* be prepared to return the totals in res */
		res->zticks = zticks;
		res->dticks = dticks;
		res->cticks = cticks;
		res->aticks = aticks;

		/*
		 * now process the result of the call to the service.
		 * We either return to the caller from this switch
		 * statement or go around again.
		 */
		switch (res->status) {
		case NIS_SRVAUTH :
			/* first auth error, see if refreshing cache helps. */
			if (flushed_cache == 0) {
				__nis_bad_auth_server(cur_server);
				__nis_CacheRemoveEntry(&slist);
				flushed_cache = 1;
				newname = 1;
				xdr_free(xdr_directory_obj, (char *) &slist);
				res->status = NIS_SUCCESS; /* try again */
				break;
			}
			/* otherwise, continue with soft error handling */
		case NIS_RPCERROR :
		case NIS_NOT_ME :
			/*
			 * NOT_ME means the replica hasn't had a chance
			 * to create a directory yet. So ignore it as
			 * though it didn't answer.
			 */
			__nis_release_server(cur_server, BAD_SERVER);
			++soft_errors;
			if (soft_errors <= soft_error_limit ||
			    (flags & HARD_LOOKUP)) {
				res->status = NIS_SUCCESS; /* try again */
				tryagain = 1;
			}
			break;
		case NIS_CBRESULTS :
			/*
			 * Callback calling, start the callback service
			 * running and collect results.
			 */
			error = __nis_run_callback(&(NIS_RES_COOKIE(res)),
				    NIS_CALLBACK, 0, cur_server);
			if (error < 0)
				res->status = -error;
			break; /* will loop to top and return */
		case NIS_SUCCESS :
			/*
			 * We actually found something, if it is a link
			 * and we are "following" links, then we set that
			 * up. Otherwise we can just return it.
			 */
			__nis_release_server(cur_server, GOOD_SERVER);
			if (((flags & FOLLOW_LINKS) == 0) ||
			    (__type_of(res->objects.objects_val) != LINK_OBJ)) {
				xdr_free(xdr_directory_obj, (char *) &slist);
				return (res);
			}
			linknum++;
			if (linknum > NIS_MAXLINKS) {
				res->status = NIS_LINKNAMEERROR;
				break;
			}

			/* clone the link, freed after the clnt_call */
			(void) nis_clone_object(res->objects.objects_val,
								&link_obj);

			strcpy(curname, link_obj.LI_data.li_name);
			newname++;
			flushed_cache = 0; /* incase we had a AUTH err before */
			xdr_free(xdr_directory_obj, (char *) &slist);

			/* check to see if it points at entries */
			if (link_obj.LI_data.li_attrs.li_attrs_len) {
				/* can only happen once */
				local_req.ibr_srch.ibr_srch_len =
					link_obj.LI_data.li_attrs.li_attrs_len;
				local_req.ibr_srch.ibr_srch_val =
					link_obj.LI_data.li_attrs.li_attrs_val;
				list_op = TRUE;
				/* must be a table w/ searchable columns */
				binding_policy = 0;
			}

			/* free result but keep the pointer */
			xdr_free(xdr_nis_result, (char *) res);
			memset((char *) res, 0, sizeof (nis_result));
			res->status = NIS_SUCCESS;
			break;
		case NIS_PARTIAL :
			/*
			 * This is what the list call will return when
			 * it hits a link so check for that. note tables
			 * don't care about search criteria in the link
			 * (its an error) so the link object cloning isn't
			 * needed.
			 */
			__nis_release_server(cur_server, GOOD_SERVER);
			if (((flags & FOLLOW_LINKS) == 0) ||
			    (__type_of(res->objects.objects_val) != LINK_OBJ)) {
				break; /* just return it */
			}
			linknum++;
			if ((linknum > NIS_MAXLINKS) ||
	    (res->objects.objects_val[0].LI_data.li_attrs.li_attrs_len != 0)) {
				res->status = NIS_LINKNAMEERROR;
				break;
			}

			strcpy(curname,
				res->objects.objects_val[0].LI_data.li_name);
			newname++;
			flushed_cache = 0;
			xdr_free(xdr_directory_obj, (char *) &slist);

			/* free result but keep the pointer */
			xdr_free(xdr_nis_result, (char *) res);
			memset((char *) res, 0, sizeof (nis_result));
			res->status = NIS_SUCCESS;
			break;

		default:
			__nis_release_server(cur_server, GOOD_SERVER);
			/* All other cases, return the result */
			break;
		} /* switch res->status */
	} /* the "forever" loop */
	xdr_free(xdr_directory_obj, (char *) &slist);
	return (res);
}

/*
 * __nis_finddirectory_r()
 *
 * This is the MT safe version of nis_finddirectory().  As it's NOT a
 * public API it's got __ in front of it's name.  It takes a third argument
 * to avoid returning a local static.  The only internal user of
 * nis_finddirectory() is in libnsl/nis/cache/client_cache.cc in the routine
 * NisBindCache :: bindDir() which now uses THIS function call instead.
 */
fd_result *
__nis_finddirectory_r(slist, name, res)
	directory_obj	*slist;
	nis_name	name;
	fd_result	*res;
{
	fd_args		argp;
	CLIENT		*cur_server;
	enum clnt_stat	status;
	struct timeval	tm;
	u_long		flags;

	memset((char *)&argp, 0, sizeof (argp));
	memset((char *)res, 0, sizeof (*res));
	argp.dir_name = name;
	argp.requester = nis_local_host();
	tm.tv_sec = NIS_FINDDIR_TIMEOUT;
	tm.tv_usec = 0;
	flags = USE_DGRAM | NO_AUTHINFO;
	do {
		cur_server = __nis_get_server(slist, flags);
		if (!cur_server) {
			return (NULL);
		}
		status = clnt_call(cur_server, NIS_FINDDIRECTORY,
			(xdrproc_t) xdr_fd_args, (char *)&argp,
			(xdrproc_t) xdr_fd_result, (char *)res, tm);
		__nis_release_server(cur_server,
		    (status == RPC_SUCCESS)? GOOD_SERVER: BAD_SERVER);
		if (status == RPC_SUCCESS)
			break;
	} while (1);
	return (res);
}

static thread_key_t res_key;
static fd_result res_main;

/*
 * nis_finddirectory()
 *
 * This function will ask one of the servers of the given directory
 * where some unknown directory "name" is. This function is called
 * from within the __nis_CacheBind() code so is generally not needed by
 * the client. If the directory cannot be found it returns NULL.
 */


fd_result *
nis_finddirectory(slist, name)
	directory_obj	*slist;
	nis_name	name;
{
	fd_result *res;

	if (_thr_main())
		res = (fd_result *)&res_main;
	else {
		res = thr_get_storage(&res_key, sizeof (fd_result), free);
		if (!res)
			return (NULL);
	}

	return (__nis_finddirectory_r(slist, name, res));
}
