/*
 *	nislib.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nislib.c	1.32	95/02/07 SMI"

/*
 *	nislib.c
 *
 *	This module contains the user visible functions for lookup, and list,
 *	add name service calls add/remove/modify, all information base calls
 *	add_entry/remove_entry/modify_entry, and mkdir, rmdir, and checkpoint.
 * 	nis server. It should be broken up into at least three separate modules
 *
 */

#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <rpcsvc/nis.h>
#include "nis_clnt.h"
#include "nis_local.h"

/*
 * Prototypes for static functions.
 */
nis_result 	*__nis_path_list(nis_object *, int, nis_result *, ib_request *,
				u_long, int (*)(nis_name, nis_object *, void *),
									void *);
nis_result 	* nis_ibops(ib_request *, u_long);
nis_result 	* nis_nameops(nis_name, nis_object *, u_long);
static nis_result *nis_list_partial(nis_result	*, ib_request *, u_long,
				int (*)(), void *);

/*
 * nis_freeresult()
 *
 * This function calls the XDR free function for the user to free up the
 * memory associated with a result structure. NB: It isn't a macro because
 * it needs to be exposed to the client. Internally the xdr routine should
 * be used to save a procedure call.
 */
void
nis_freeresult(res)
	nis_result	*res;
{
	xdr_free(xdr_nis_result, (char *) res);
	free(res);
}

/*
 * __nis_make_binding()
 *
 * This function is used to return a binding to a candidate server.
 * It has two side effects :
 *	1) It keeps track of time in the cache code
 *	2) it optionally mallocs a NIS+ result structure.
 *
 * 'name' contains the name of the object that will be operated on.
 * If the operation is a lookup or a table search, then this code
 * returns a directory object that describes that objects directory
 * and thus a server that will have knowledge about the object.
 *
 * However, since the directory object, and the directory itself
 * can be on two different machines, when listing directories we
 * attempt to bind to the name passed. This only occurs when nis_list
 * is called and the search criteria is NULL (which is the only legal
 * search on a directory).
 *
 */
nis_result *
__nis_make_binding(res, name, slistp, name_first)
	nis_result	**res;
	nis_name	name;
	directory_obj	*slistp;
	int		name_first;	/* True if binding to name first */
{
	nis_error	stat;

	__start_clock(CLOCK_CACHE);
	if (*res == NULL) {
		*res = (nis_result *)calloc(1, sizeof (nis_result));
		if (! *res)
			return (nis_make_error(0, 0, 0,
						__stop_clock(CLOCK_CACHE), 0));
	}

	if (name_first) {
		(*res)->status = __nis_CacheBind(name, slistp);
		if ((*res)->status != NIS_SUCCESS)
			(*res)->status = __nis_CacheBind(nis_domain_of(name),
									slistp);
	} else {
		(*res)->status = __nis_CacheBind(nis_domain_of(name), slistp);
		if ((*res)->status != NIS_SUCCESS)
			(*res)->status = __nis_CacheBind(name, slistp);
	}
	(*res)->aticks = __stop_clock(CLOCK_CACHE);

	return (*res);
}

/*
 * __nis_path_list()
 *
 * This internal function will list all of the tables in a path.
 * if the ALL_RESULTS flag is set, it keeps on going, and going.
 * otherwise it returns on the first match.
 *
 * NB: The nis_list() function initializes the request's search
 * criteria, we just swap in table names.
 *
 * All possible returns from call to NIS_IBLIST (all NIS_xxx)
 *
 * Successful returns
 *
 * CBRESULTS, SUCCESS, S_SUCCESS  - found something
 * NOTFOUND, PARTIAL,  - looked but no data.
 * PERMISSION - can't read the table
 *
 * Errors that generate syslog warnings
 * NOTMASTER, NOT_ME, BADNAME, NOSUCHTABLE, NOSUCHNAME
 * BADATTRIBUTE, INVALIDOBJ
 *
 * Errors that fail silently but change final result to "soft"
 * NOMEMORY, TRYAGAIN, SYSTEMERROR, BADOBJECT
 *
 * Fatal errors :
 * NOCALLBACK
 */
static nis_result *
__nis_path_list(tbl_obj, sf, res, req, flags, cback, cbdata)
	nis_object	*tbl_obj;	/* Path of tables to search */
	int		sf;		/* search first object */
	nis_result	*res;		/* result structure to use */
	ib_request	*req;		/* Request structure */
	unsigned long	flags;		/* Flags to this function */
	int		(*cback)(nis_name, nis_object *, void *);
	void		*cbdata;
{
	nis_name	pathlist[NIS_MAXPATHDEPTH];	/* Parsed table path */
	char		pathbuf[NIS_MAXPATHLEN];
	char		linkname[NIS_MAXNAMELEN];
	char		firstpath[NIS_MAXNAMELEN];
	nis_result	*local_res;		/* local result */
	nis_name	table;			/* current table */
	int		tnum, 			/* # of tables to search */
			i, j, 			/* counters */
			cur_obj, link_num,	/* obj, link counters */
			soft_error = 0; 	/* error detected */
	unsigned long	aticks = 0, 		/* profiling vars */
			dticks = 0,
			cticks = 0,
			zticks = 0;
	nis_object	*obj_list;		/* list returned from call */
	int		num_objs,
			total_objs;		/* # of objects returned */
	struct obj_lists {			/* returned objects from each */
		nis_object	*objs;
		int		len;
	} 		ret_objs[NIS_MAXPATHDEPTH];

	/* construct a list of tables to search */
	strncpy(pathbuf, tbl_obj->TA_data.ta_path, NIS_MAXPATHLEN);
	if (sf) {
		sprintf(firstpath, "%s.%s", tbl_obj->zo_name,
						tbl_obj->zo_domain);
		pathlist[0] = firstpath;
		tnum = __nis_parse_path(pathbuf, &pathlist[1],
						NIS_MAXPATHDEPTH - 1);
		tnum++;
	} else
		tnum = __nis_parse_path(pathbuf, pathlist, NIS_MAXPATHDEPTH);

	/* Take any existing objects from the result passed in */
	ret_objs[0].objs = res->objects.objects_val;
	ret_objs[0].len = res->objects.objects_len;
	total_objs = ret_objs[0].len;
	res->objects.objects_val = NULL;
	res->objects.objects_len = 0;

	/*
	 * Either search until a match is found, or if ALL_RESULTS is
	 * set, search until path is exhausted.
	 */
	for (i = 0; i < tnum; i++) {
		table = pathlist[i];
		/* Ignore non-fully qualified names in path */
		if (table[strlen(table) - 1] != '.') {
			syslog(LOG_WARNING,
	"nis_list: non fully qualified name in table path, %s, ignored.\n",
									table);
			continue;
		}
		/* swap in the table name from the path */
		req->ibr_name = table;
		/* prepare to receive the objects returned */
		ret_objs[i+1].objs = NULL;
		ret_objs[i+1].len = 0;
		local_res = __nis_core_lookup(req, flags, 1, cbdata, cback);
		aticks += local_res->aticks;
		dticks += local_res->dticks;
		cticks += local_res->cticks;
		zticks += local_res->zticks;
		obj_list = local_res->objects.objects_val;
		num_objs = local_res->objects.objects_len;

		switch (local_res->status) {
		case NIS_SUCCESS :
			/* put these into the array */
			ret_objs[i+1].objs = obj_list;
			ret_objs[i+1].len = num_objs;
			total_objs += num_objs;
			/* zero this so freeresult won't free them */
			local_res->objects.objects_val = NULL;
			local_res->objects.objects_len = 0;
			/* fall through to the CBRESULTS code */
		case NIS_CBRESULTS :
		case NIS_CBERROR :
			break;
		case NIS_PARTIAL :
		case NIS_PERMISSION :
		case NIS_NOTMASTER :
			/* these errors, just break */
			break;
		case NIS_LINKNAMEERROR : /* message generated above */
			soft_error = 1;
			break;
		case NIS_NOTFOUND :
		case NIS_NOT_ME :
		case NIS_BADNAME :
		case NIS_NOSUCHTABLE :
		case NIS_NOSUCHNAME :
		case NIS_BADATTRIBUTE :
		case NIS_RPCERROR :
		case NIS_NAMEUNREACHABLE :
		case NIS_INVALIDOBJ :
			/* generate message and set soft_error */
			syslog(LOG_WARNING,
"nis_list: NIS+ error %s encountered on name %s.%s in table %s's path.",
				nis_sperrno(local_res->status),
				tbl_obj->zo_name, tbl_obj->zo_domain, table);
		default :
			soft_error = TRUE;
			break;
		}
		/*
		 * POLICY : When one table in a path is unreachable,
		 * should we continue on or stop with an error?
		 * ANSWER : Continue on. Loss of a portion of the namespace
		 * should not cause disruptions in all of the namespace.
		 * NB: This can have interesting side effects such that a
		 * name may suddenly change "value" because it is being
		 * resolved from a different place.
		 *
		 * If we're not returning all results and we've had a
		 * successful call, we just return those results.
		 */
		if (((flags & ALL_RESULTS) == 0) &&
		    ((local_res->status == NIS_SUCCESS) ||
		    (local_res->status == NIS_CBRESULTS))) {
			res->status = local_res->status;
			res->aticks += aticks;
			res->dticks += dticks;
			res->zticks += zticks;
			res->objects.objects_val = obj_list;
			res->objects.objects_len = num_objs;
			/* reset so that caller does not free it */
			req->ibr_name = NULL;
			nis_freeresult(local_res);
			/* return same result structure back to them */
			return (res);
		}

		/* otherwise just free local result (we've got the objs) */
		nis_freeresult(local_res);
	}

	/* name is already freed so null this out */
	req->ibr_name = NULL;

	/*
	 * At this point, we've either exhausted the list of tables
	 * (total_objs == 0), or we've asked for all results so the
	 * ret_objs[] array has some data in it (total_objs > 0)
	 * if soft_error is set we will adjust our result status
	 * appropriately.
	 */
	if (total_objs) {
		/* now build a list of objects that should be returned */
		obj_list = (nis_object *) calloc(total_objs,
							sizeof (nis_object));
		if (obj_list == NULL) {
			res->status = NIS_NOMEMORY;
			res->aticks += aticks;
			res->dticks += dticks;
			res->zticks += zticks;
			for (i = 0; i < (tnum+1); i++) {
				if (ret_objs[i].objs == NULL)
					continue;
				for (j = 0; j < ret_objs[i].len; j++)
					xdr_free(xdr_nis_object,
						(char *)&(ret_objs[i].objs[j]));
				free(ret_objs[i].objs);
			}
			return (res);
		}

		/* copyout all objects into this new array */
		cur_obj = 0;
		for (i = 0; i < (tnum+1); i++) {
			if (ret_objs[i].objs == NULL)
				continue;
			for (j = 0; j < ret_objs[i].len; j++)
				obj_list[cur_obj++] = ret_objs[i].objs[j];
			free(ret_objs[i].objs);
		}
		res->objects.objects_val = obj_list;
		res->objects.objects_len = cur_obj;
		if (cur_obj)
			res->status = NIS_SUCCESS;
		else
			res->status = NIS_NOTFOUND;
	} else {
		if (cback)
			res->status = NIS_CBRESULTS;
		else
			res->status = NIS_NOTFOUND;
	}

	if (soft_error && (res->status == NIS_SUCCESS))
		res->status = NIS_S_SUCCESS;
	else if (soft_error && (res->status == NIS_NOTFOUND))
		res->status = NIS_S_NOTFOUND;
	res->aticks += aticks;
	res->dticks += dticks;
	res->cticks += cticks;
	res->zticks += zticks;
	return (res);
}

#ifdef TDRPC
extern int _svcauth_des();
extern int getpublickey();
#endif

/*
 * nis_lookup()
 *
 * This is the main lookup function of the name service. It will look
 * for the named object and return it. If the object was a link and
 * the flag FOLLOW_LINKS was set it will look up the item named by
 * the LINK, if that is an indexed name the lookup may return multiple
 * objects. If the name is not fully qualified and EXPAND_NAME is set
 * this function will expand the name into several candidate names.
 */
nis_result *
nis_lookup(name, flags)
	nis_name name;
	u_long	 flags;
{
	nis_error	nis_err = NIS_SUCCESS;
	nis_name	*namelist;
	nis_result	*res;
	ib_request	req;
	int		i;
	unsigned long	aticks = 0,
			cticks = 0,
			dticks = 0,
			zticks = 0;
#ifdef TDRPC
	int		(*foo)();

	/* These force the correct versions to get loaded */
	foo = getpublickey;
	foo = _svcauth_des;
#endif

	__start_clock(CLOCK_CLIENT);
	memset((char *) &req, 0, sizeof (ib_request));
	req.ibr_name = name;
	i = strlen(name);
	if ((flags & EXPAND_NAME) == 0 || (i > 0 && name[i-1] == '.')) {
		res = __nis_core_lookup(&req, flags, 0, NULL, NULL);
		res->cticks = __stop_clock(CLOCK_CLIENT);
		return (res);
	}
	namelist = nis_getnames(name);
	if (! namelist) {
		res = nis_make_error(NIS_NOMEMORY, 0, 0, 0, 0);
		res->cticks = __stop_clock(CLOCK_CLIENT);
		return (res);
	}
	for (i = 0; namelist[i]; i++) {
		req.ibr_name = namelist[i];
		res = __nis_core_lookup(&req, flags, 0, NULL, NULL);
		switch (res->status) {
			/*
			 * All of the errors that indicate the name
			 * is bound.
			 * NB: We include the "nis_list" errors as well
			 * as the core_lookup call could have followed
			 * a link into a table operation.
			 */
			case NIS_SUCCESS :
			case NIS_PARTIAL :
			case NIS_CBRESULTS :
			case NIS_CBERROR :
			case NIS_CLNTAUTH :
			case NIS_SRVAUTH :
			case NIS_PERMISSION :
			case NIS_LINKNAMEERROR:
			case NIS_NOTMASTER :
				res->aticks += aticks;
				res->dticks += dticks;
				res->zticks += zticks;
				res->cticks += cticks;
				res->cticks += __stop_clock(CLOCK_CLIENT);
				nis_freenames(namelist);
				return (res);
			default :
				aticks += res->aticks;
				cticks += res->cticks;
				dticks += res->dticks;
				zticks += res->zticks;
				if (nis_err == NIS_SUCCESS)
					nis_err = res->status;
				nis_freeresult(res);
		}
	}
	nis_freenames(namelist);
	cticks += __stop_clock(CLOCK_CLIENT);
	if (nis_err == NIS_SUCCESS) {
		syslog(LOG_WARNING, "nis_lookup: empty namelist");
		nis_err = NIS_NOTFOUND;    /* fix up in case namelist empty */
	}
	res = nis_make_error(nis_err, aticks, cticks, dticks, zticks);
	return (res);
}

/*
 * nis_list()
 *
 * This function takes a "standard" NIS name with embedded search criteria
 * and does a list on the object.
 */
nis_result *
nis_list(name, flags, cback, cbdata)
	nis_name	name;	/* list name like '[foo=bar].table.name' */
	u_long		flags;		/* Flags for the search */
	int		(*cback)();	/* Callback function. */
	void		*cbdata;	/* Callback private data */
{
	nis_error	nis_err = NIS_SUCCESS;
	nis_name	*namelist;
	nis_object	*obj;
	nis_result	*res;
	ib_request	req;
	nis_error	stat;
	nis_name	s;
	unsigned long	zticks = 0,
			aticks = 0,
			dticks = 0,
			cticks = 0;
	link_obj	*ldata;
	int		i, done, foundit;

	/* start the client profiling clock */
	__start_clock(CLOCK_CLIENT);

	/* Parse the request into a table name and attr/value pairs */
	stat = nis_get_request(name, NULL, NULL, &req);
	if (stat != NIS_SUCCESS) {
		res = nis_make_error(stat, 0, 0, 0, 0);
		res->cticks = __stop_clock(CLOCK_CLIENT);
		return (res);
	}

	/*
	 * process the ALL_RESULTS flag specially. First fetch
	 * the table object to get the path, then call path list
	 * to read all of the data. Note if we returned the object
	 * on a list we could save an RPC here.
	 */
	if (flags & ALL_RESULTS) {
		res = nis_lookup(req.ibr_name, flags);
		if (res->status != NIS_SUCCESS) {
			nis_free_request(&req);
			return (res);
		}
		aticks = res->aticks;
		cticks = res->cticks;
		dticks = res->dticks;
		zticks = res->zticks;
		obj = res->objects.objects_val;
		if ((res->objects.objects_len > 1) ||
		    (__type_of(obj) != TABLE_OBJ)) {
			/* Note : can't do all results on directory obj. */
			xdr_free(xdr_nis_result, (char *)res);
			nis_free_request(&req);
			memset((char *)res, 0, sizeof (nis_result));
			res->status = NIS_BADOBJECT;
			res->aticks = aticks;
			res->dticks = dticks;
			res->cticks = cticks;
			res->zticks = zticks;
			return (res);
		}
		res->objects.objects_val = NULL;
		res->objects.objects_len = 0;
		free(req.ibr_name); /* won't be needing this */
		req.ibr_name = NULL;
		res = __nis_path_list(obj, 1, res, &req, flags, cback, cbdata);
		nis_free_request(&req);
		xdr_free(xdr_nis_object, (char *)obj);
		free(obj);
		res->aticks += aticks;
		res->dticks += dticks;
		res->cticks += cticks;
		res->zticks += zticks;
		return (res);
	}

	/*
	 * Normal requests.  The server will return NIS_PARTIAL
	 * if we specify a search criteria and the table exists
	 * but the entry does not exist within the table.  We
	 * need to handle this return value by checking for a
	 * table path in nis_list_partial().
	 */
	i = strlen(name);
	if ((flags & EXPAND_NAME) == 0 || (i > 0 && name[i-1] == '.')) {
		res = __nis_core_lookup(&req, flags, 1, cbdata, cback);
		free(req.ibr_name);
		if (res->status == NIS_PARTIAL)
			res = nis_list_partial(res, &req, flags, cback, cbdata);
	} else {
		namelist = nis_getnames(req.ibr_name);
		if (! namelist) {
			res = nis_make_error(NIS_NOMEMORY, 0, 0, 0, 0);
			nis_free_request(&req);
			res->cticks = __stop_clock(CLOCK_CLIENT);
			return (res);
		}
		free(req.ibr_name); /* non fully qualified name */

		for (i = 0, done = 0; !done && namelist[i]; i++) {
			/* replace with the candidate name */
			req.ibr_name = namelist[i];
			res = __nis_core_lookup(&req, flags, 1, cbdata, cback);
			if (res->status == NIS_PARTIAL)
				res = nis_list_partial(res, &req,
							flags, cback, cbdata);
			switch (res->status) {
				/*
				 * All of the errors that indicate the name
				 * is bound.
				 */
				case NIS_SUCCESS :
				case NIS_CBRESULTS :
				case NIS_CBERROR :
				case NIS_CLNTAUTH :
				case NIS_SRVAUTH :
				case NIS_PERMISSION :
				case NIS_NOTMASTER :
					done = 1;
					break;
				default :
					aticks += res->aticks;
					cticks += res->cticks;
					dticks += res->dticks;
					zticks += res->zticks;
					if (nis_err == NIS_SUCCESS)
						nis_err = res->status;
					nis_freeresult(res);
					break;
			}
		}
		if (! done) {
			if (nis_err == NIS_SUCCESS) {
				syslog(LOG_WARNING, "nis_list: empty namelist");
				nis_err = NIS_NOTFOUND; /* if empty namelist */
			}
			res = nis_make_error(nis_err, aticks, cticks,
							    dticks, zticks);
		}
		nis_freenames(namelist); /* not needed any longer */
	}
	req.ibr_name = NULL; /* already freed */

	/*
	 * Returns from __nis_core_lookup :
	 *	NIS_SUCCESS,  	Table/Name found, search suceeded.
	 *	NIS_CBRESULTS,	Table/Name found, search suceeded to callback
	 *	NIS_PARTIAL,  	found the name but didn't match any entries.
	 *	NIS_CLNTAUTH,	Found table, couldn't authenticate callback
	 *	NIS_SRVAUTH	Found table, couldn't authenticate server
	 *	NIS_PERMISSION	Found table, couldn't read it.
	 *	NIS_NOTMASTER	Found table, wasn't master (master requested)
	 *	NIS_RPCERROR	unable to communicate with service.
	 *	NIS_XXX 	Error somewhere.
	 *
	 */
	res->aticks += aticks;
	res->cticks += cticks;
	res->dticks += dticks;
	res->zticks += zticks;
	res->cticks += __stop_clock(CLOCK_CLIENT);
	nis_free_request(&req);
	return (res);
}

/*
 * Deal with a "PARTIAL" result. Given a NIS name of
 * [search-criteria].table-name, this occurs when the
 * server found an object whose name was 'table-name' but
 * either the object couldn't be searched because it was the
 * wrong type or the search resulted in no results.
 *
 * If the object that matched 'table-name' was a LINK object
 * core lookup will have followed it for us.
 *
 * We increment a local copy of the statistics and then reset
 * them in 'res' before returning.
 */
static
nis_result *
nis_list_partial(res, req, flags, cback, cbdata)
	nis_result	*res;
	ib_request	*req;
	u_long		flags;		/* Flags for the search */
	int		(*cback)();	/* Callback function. */
	void		*cbdata;	/* Callback private data */
{
	nis_object	*obj;
	table_obj	*tdata;
	unsigned long aticks = res->aticks;
	unsigned long cticks = res->cticks;
	unsigned long dticks = res->dticks;
	unsigned long zticks = res->zticks;

	obj = res->objects.objects_val;
	if (__type_of(obj) ==  DIRECTORY_OBJ) {
		/*
		 * POLICY : What is the error when you search a
		 * a DIRECTORY and the results are no entries ?
		 * ANSWER : A NOT FOUND error, assuming the server
		 * did not return a "bad attribute" error, AND
		 * we do _NOT_ return the directory object that the
		 * server returned.
		 */
		xdr_free(xdr_nis_result, (char *)res);
		memset((char *) res, 0, sizeof (nis_result));
		res->status = NIS_NOTFOUND;
	} else if (__type_of(obj) != TABLE_OBJ) {
		/*
		 * This shouldn't happen because the server should
		 * catch it when it attempts the search.
		 */
		xdr_free(xdr_nis_result, (char *)res);
		memset((char *) res, 0, sizeof (nis_result));
		res->status = NIS_NOTSEARCHABLE;
	} else {
		/*
		 * Now we know its a table object and that our search failed.
		 */
		tdata = &(obj->TA_data);
		if (((flags & FOLLOW_PATH) != 0) && (tdata->ta_path) &&
		    (strlen(tdata->ta_path) > (size_t) 0)) {
			obj = res->objects.objects_val;
			res->objects.objects_val = NULL;
			res->objects.objects_len = 0;
			res = __nis_path_list(obj, 0, res, req, flags,
								cback, cbdata);
			/* free up the table object */
			xdr_free(xdr_nis_object, (char *)obj);
			free(obj);
			/* ticks are updated by __nis_path_list */
			aticks = res->aticks;
			cticks = res->cticks;
			dticks = res->dticks;
			zticks = res->zticks;
		} else {
			xdr_free(xdr_nis_result, (char *)res);
			memset((char *) res, 0, sizeof (nis_result));
			/*
			 *  If a search criteria was specified, indicate
			 *  that we didn't find the entry.  If there was
			 *  no search criteria, then we return success
			 *  (i.e., table was listed successfully, but
			 *  there were no entries in the table).
			 */
			if (req->ibr_srch.ibr_srch_len)
				res->status = NIS_NOTFOUND;
			else if (cback)
				res->status = NIS_CBRESULTS;
			else
				res->status = NIS_SUCCESS;
		}
	}
	res->aticks = aticks;
	res->dticks = dticks;
	res->zticks = zticks;
	res->cticks = cticks;
	return (res);
}

static nis_result *
call_master(nis_name name,
	    int binding_policy,
	    u_long func,
	    xdrproc_t xdr_req_func,
	    char *req,
	    int timeout)
{
	nis_result		*res = 0;
	directory_obj		slist;
	u_long			aticks = 0,
				cticks = 0,
				dticks = 0,
				zticks = 0;
	nis_server		*srv;
	CLIENT			*master;
	enum clnt_stat		status;
	struct timeval 		tv;
	int			soft_errors = 0;

	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	/*
	 * bind to a server that serve's this object
	 * NB: This also malloc's a nis_result struct for us.
	 */
new_binding:
	memset((char *)&slist, 0, sizeof (directory_obj));
	res = __nis_make_binding(&res, name, &slist, binding_policy);
	if (NIS_RES_STATUS(res) != NIS_SUCCESS) {
		xdr_free(xdr_directory_obj, (char *)&slist);
		return (res);
	}
	aticks = res->aticks;
	srv = slist.do_servers.do_servers_val; /* master server */

	/*
	 * Open a channel to the master server...
	 */
new_handle:
	master = __nis_get_server(&slist, MASTER_ONLY);
	if (! master) {
		res->status = NIS_NAMEUNREACHABLE;
		res->aticks = aticks;
		xdr_free(xdr_directory_obj, (char *)&slist);
		return (res);
	}

tryagain:
	memset((char *)(res), 0, sizeof (nis_result));
	status = clnt_call(master, func,
			xdr_req_func, req,
			(xdrproc_t)xdr_nis_result, (char *)(res), tv);
	switch (status) {
		case RPC_AUTHERROR :
			res->status = NIS_SRVAUTH;
			break;
		case RPC_SUCCESS :
			break;
		default :
			res->status = NIS_RPCERROR;
			break;
	}
	if (res->status == NIS_RPCERROR || res->status == NIS_SRVAUTH) {
		++soft_errors;
		if (soft_errors == 1 && res->status == NIS_SRVAUTH) {
			__nis_bad_auth_server(master);
			__nis_CacheRemoveEntry(&slist);
			xdr_free(xdr_directory_obj, (char *)&slist);
			goto new_binding;
		}
		if (soft_errors > 1)
			syslog(LOG_WARNING,
				"nameops/ibops: authentication or rpc failure");
		else {
			__nis_release_server(master, BAD_SERVER);
			goto new_handle;
		}
	}

	cticks += res->cticks;	/* save the stats */
	dticks += res->dticks;
	zticks += res->zticks;
	aticks += res->aticks;
	if (res->status == NIS_TRYAGAIN) {
		sleep(5);
		goto tryagain;
	}
	__nis_release_server(master,
	    (status == RPC_SUCCESS)? GOOD_SERVER: BAD_SERVER);

	res->cticks = cticks;	/* retrieve final tally */
	res->dticks = dticks;
	res->zticks = zticks;
	res->aticks = aticks;
	xdr_free(xdr_directory_obj, (char *)&slist);
	return (res);
}

/*
 * nis_nameops()
 *
 * This generic function calls all of the name operations.
 */

static nis_result *
nis_nameops(name, obj, func)
	nis_name	name;
	nis_object	*obj;
	u_long		func;
{
	nis_result		*res;
	ns_request		req;
	nis_name		oname, odomain;
	nis_name		oowner, ogroup;
	char			nname[1024], ndomain[1024];

	if (obj) {
		/*
		 * Enforce correct name policy on NIS+ objects stored
		 * into the namespace. This code insures that zo_name
		 * and zo_domain are correct.
		 */
		oname = obj->zo_name;
		strcpy(nname, nis_leaf_of(name));
		obj->zo_name = nname;
		odomain = obj->zo_domain;
		strcpy(ndomain, nis_domain_of(name));
		obj->zo_domain = ndomain;
		if (ndomain[strlen(ndomain)-1] != '.')
			strcat(ndomain, ".");

		oowner = obj->zo_owner;
		if (obj->zo_owner == 0)
			obj->zo_owner = nis_local_principal();

		ogroup = obj->zo_group;
		if (obj->zo_group == 0)
			obj->zo_group = nis_local_group();
	}

	memset((char *)&req, 0, sizeof (req));
	req.ns_name = name;
	if (obj) {
		req.ns_object.ns_object_len = 1;
		req.ns_object.ns_object_val = obj;
	} else {
		req.ns_object.ns_object_len = 0;
		req.ns_object.ns_object_val = NULL;
	}

	res = call_master(name, 0, func, (xdrproc_t)xdr_ns_request,
				(char *) &req, NIS_GEN_TIMEOUT);

	if (obj) {
		obj->zo_name = oname;
		obj->zo_domain = odomain;
		obj->zo_owner = oowner;
		obj->zo_group = ogroup;
	}
	return (res);
}

/*
 * nis_add()
 *
 * This function will add an object to the namespace. If it is a
 * table type object the server will create a table for it as well.
 */

nis_result *
nis_add(name, obj)
	nis_name	name;
	nis_object	*obj;
{
	nis_result	*res;

	__start_clock(CLOCK_CLIENT); /* start the client clock */
	res = nis_nameops(name, obj, NIS_ADD);
	res->cticks = __stop_clock(CLOCK_CLIENT);
	return (res);
}

static void
nis_flush_cache(nis_name name, nis_object *obj)
{
	if (obj == 0 || (obj && __type_of(obj) == DIRECTORY_OBJ)) {
		directory_obj dobj;
		if (__nis_CacheSearch(name, &dobj) == NIS_SUCCESS &&
		    nis_dir_cmp(name, dobj.do_name) == SAME_NAME) {
			__nis_CacheRemoveEntry(&dobj);
			xdr_free((xdrproc_t)xdr_directory_obj, (char *)&dobj);
		}
	}
}

/*
 * nis_remove()
 *
 * This function will remove an object from the namespace. If it is a
 * table type object the server will destroy the table for it as well.
 */

nis_result *
nis_remove(name, obj)
	nis_name	name;
	nis_object	*obj;
{
	nis_result	*res;

	__start_clock(CLOCK_CLIENT); /* start the client clock */
	res =  nis_nameops(name, obj, NIS_REMOVE);
	if (res->status == NIS_SUCCESS)
		nis_flush_cache(name, obj);
	res->cticks = __stop_clock(CLOCK_CLIENT);
	return (res);
}

/*
 * nis_modify()
 *
 * This function will modify an object in the namespace.
 */

nis_result *
nis_modify(name, obj)
	nis_name	name;
	nis_object	*obj;
{
	nis_result	*res;

	__start_clock(CLOCK_CLIENT); /* start the client clock */
	res = nis_nameops(name, obj, NIS_MODIFY);
	if (res->status == NIS_SUCCESS)
		nis_flush_cache(name, obj);
	res->cticks = __stop_clock(CLOCK_CLIENT);
	return (res);
}


/*
 * nis_ibops()
 *
 * This generic function calls all of the table operations.
 *
 * Note that although we use a virtual circuit, there are no keepalives.
 * Because of this, the length of the timeout is vital, and we attempt
 * to tune it here for the various operations.
 */

/* a single modify has been seen to take as long as 180 sec. */
#define	NIS_MODIFY_TIMEOUT	300 /* 5 minutes */

/* netgroup.org_dir.ssi has 48K entries, and will take almost this long */
#define	NIS_REMOVE_MULT_TIMEOUT	(2*60*60) /* 2 hours */


static nis_result *
nis_ibops(req, func)
	ib_request	*req;
	u_long		func;
{
	nis_result		*res;
	nis_object		*obj = NULL;
	nis_name		oname, odomain;
	nis_name		oowner, ogroup;
	char			nname[NIS_MAXNAMELEN], ndomain[NIS_MAXNAMELEN];
	int			timeout;	/* in seconds */

	if (req->ibr_obj.ibr_obj_len) {
		/*
		 * Enforce correct name policy on objects stored into
		 * tables. This code insures that zo_name and zo_domain
		 * are correct.
		 */
		obj = req->ibr_obj.ibr_obj_val;
		oname = obj->zo_name;
		strcpy(nname, nis_leaf_of(req->ibr_name));
		obj->zo_name = nname;
		odomain = obj->zo_domain;
		strcpy(ndomain, nis_domain_of(req->ibr_name));
		obj->zo_domain = ndomain;
		if (ndomain[strlen(ndomain)-1] != '.')
			strcat(ndomain, ".");

		oowner = obj->zo_owner;
		if (obj->zo_owner == 0)
			obj->zo_owner = nis_local_principal();

		ogroup = obj->zo_group;
		if (obj->zo_group == 0)
			obj->zo_owner = nis_local_group();
	}

	/* determine the timeout (heuristic) */
	switch (func) {
	    case NIS_IBMODIFY:
		timeout = NIS_MODIFY_TIMEOUT;
		break;
	    case NIS_IBREMOVE:
		if (req->ibr_flags & REM_MULTIPLE)
			timeout = NIS_REMOVE_MULT_TIMEOUT;
		else
			timeout = NIS_MODIFY_TIMEOUT;
		break;
	    default:
		timeout = NIS_GEN_TIMEOUT;
		break;
	}

	res = call_master(req->ibr_name, 0, func, (xdrproc_t)xdr_ib_request,
				(char *)req, timeout);

	if (obj) {
		obj->zo_name = oname;
		obj->zo_domain = odomain;
		obj->zo_owner = oowner;
		obj->zo_group = ogroup;
	}
	return (res);
}


/*
 * nis_add_entry()
 *
 * This function will add an entry to the named NIS table.
 */
nis_result *
nis_add_entry(name, obj, flags)
	nis_name	name;		/* Table to use 		*/
	nis_object	*obj;		/* Entry object to add. 	*/
	u_long		flags;		/* Semantic modification flags	*/
{
	nis_result	*res;
	ib_request	req;
	nis_error	stat;

	__start_clock(CLOCK_CLIENT);
	stat = nis_get_request(name, obj, NULL, &req);
	if (stat != NIS_SUCCESS)
		return (nis_make_error(stat, 0, __stop_clock(CLOCK_CLIENT),
									0, 0));
	req.ibr_flags = flags;
	res = nis_ibops(&req, NIS_IBADD);
	nis_free_request(&req); /* free up memory associated with request */
	res->cticks += __stop_clock(CLOCK_CLIENT);
	return (res);
}

/*
 * nis_remove_entry()
 *
 * This function will remove an entry to the named NIS table.
 */
nis_result *
nis_remove_entry(name, obj, flags)
	nis_name	name;		/* Table to use 		*/
	nis_object	*obj;		/* Entry object to remove. 	*/
	u_long		flags;		/* semantic modification flags	*/
{
	nis_result	*res;
	ib_request	req;
	nis_error	stat;

	__start_clock(CLOCK_CLIENT);
	stat = nis_get_request(name, obj, NULL, &req);
	if (stat != NIS_SUCCESS)
		return (nis_make_error(stat, 0, __stop_clock(CLOCK_CLIENT),
									0, 0));

	req.ibr_flags = flags;
	res = nis_ibops(&req, NIS_IBREMOVE);
	nis_free_request(&req); /* free up memory associated with request */
	res->cticks += __stop_clock(CLOCK_CLIENT);
	return (res);
}

/*
 * nis_modify_entry()
 *
 * This function will modify an entry to the named NIS table.
 */
nis_result *
nis_modify_entry(name, obj, flags)
	nis_name	name;		/* Table to use 		*/
	nis_object	*obj;		/* Entry object to modify. 	*/
	u_long		flags;		/* Semantic modification flags	*/
{
	nis_result	*res;
	ib_request	req;
	nis_error	stat;

	__start_clock(CLOCK_CLIENT);
	stat = nis_get_request(name, obj, NULL, &req);
	if (stat != NIS_SUCCESS)
		return (nis_make_error(stat, 0, __stop_clock(CLOCK_CLIENT),
									0, 0));

	req.ibr_flags = flags;
	res = nis_ibops(&req, NIS_IBMODIFY);
	nis_free_request(&req); /* free up memory associated with request */
	res->cticks += __stop_clock(CLOCK_CLIENT);
	return (res);
}

/*
 * nis_first_entry()
 *
 * This function will fetch the "first" entry in a table.
 * NOTE : These currently use the ibops function which always binds
 * the the master server. This is good because for them to work the
 * always have to talk to the same server however it is bad in that
 * talking to the same slave would be ok too. XXX
 */
nis_result *
nis_first_entry(table)
	nis_name	table;		/* Table to read 	*/
{
	nis_result	*res;
	ib_request	req;
	nis_error	stat;

	__start_clock(CLOCK_CLIENT);
	stat = nis_get_request(table, NULL, NULL, &req);
	if (stat != NIS_SUCCESS)
		return (nis_make_error(stat, 0, __stop_clock(CLOCK_CLIENT),
									0, 0));

	if (req.ibr_srch.ibr_srch_len)
		return (nis_make_error(NIS_TOOMANYATTRS, 0,
					__stop_clock(CLOCK_CLIENT), 0, 0));

	res = nis_ibops(&req, NIS_IBFIRST);
	nis_free_request(&req);
	/* XXX at this point we should put the server in the cookie. */
	/* free up memory associated with request */
	res->cticks += __stop_clock(CLOCK_CLIENT);
	return (res);
}

/*
 * nis_next_entry()
 *
 * This function will fetch the "first" entry in a table.
 */
nis_result *
nis_next_entry(table, cookie)
	nis_name	table;		/* Table to read 	*/
	netobj		*cookie;	/* First/Next Cookie	*/
{
	nis_result	*res;
	ib_request	req;
	nis_error	stat;

	__start_clock(CLOCK_CLIENT);
	stat = nis_get_request(table, NULL, cookie, &req);
	if (stat != NIS_SUCCESS)
		return (nis_make_error(stat, 0, __stop_clock(CLOCK_CLIENT),
									0, 0));

	res = nis_ibops(&req, NIS_IBNEXT);
	/* free up memory associated with request */
	nis_free_request(&req);
	res->cticks += __stop_clock(CLOCK_CLIENT);
	return (res);
}

nis_result *
nis_checkpoint(name)
	nis_name	name;
{
	nis_result		*res = 0;
	nis_result		*bind;
	cp_result		cpr;
	directory_obj		slist;
	u_long			aticks = 0;
	CLIENT			*cur_server;
	struct timeval		tm;
	enum clnt_stat		status;
	int			soft_errors = 0;

	__start_clock(CLOCK_CLIENT);

	res = (nis_result *)(calloc(1, sizeof (nis_result)));

new_binding:
	memset((char *)&slist, 0, sizeof (directory_obj));
	bind = __nis_make_binding(&res, name, &slist, 1);

	/* If status isn't SUCCESS we couldn't contact a server */
	if (NIS_RES_STATUS(bind) != NIS_SUCCESS) {
		bind->cticks = __stop_clock(CLOCK_CLIENT);
		return (bind);
	}

	/*
	 * Find the master server...
	 */
new_handle:
	cur_server = __nis_get_server(&slist, MASTER_ONLY);

	if (! cur_server)  {
		syslog(LOG_ERR, "Unable to bind to master server for name '%s'",
			name);
		xdr_free(xdr_directory_obj, (char *)&slist);
		nis_freeresult(res);
		return (nis_make_error(NIS_NAMEUNREACHABLE, aticks,
					__stop_clock(CLOCK_CLIENT), 0, 0));
	}

	/* Attempt the operation... */
	tm.tv_sec = NIS_GEN_TIMEOUT;
	tm.tv_usec = 0;
	status = clnt_call(cur_server, NIS_CHECKPOINT,
				(xdrproc_t)xdr_nis_name, (char *)&name,
				(xdrproc_t)xdr_cp_result, (char *)&cpr, tm);

	if (status != RPC_SUCCESS) {
		++soft_errors;
		if (soft_errors == 1 && status == RPC_AUTHERROR) {
			__nis_bad_auth_server(cur_server);
			__nis_CacheRemoveEntry(&slist);
			xdr_free(xdr_directory_obj, (char *)&slist);
			goto new_binding;
		}
		__nis_release_server(cur_server, BAD_SERVER);
		if (soft_errors > 1) {
			syslog(LOG_WARNING, "nis_checkpoint: RPC Error '%s'",
						clnt_sperrno(status));
			switch (status) {
			case RPC_AUTHERROR :
				res->status = NIS_SRVAUTH;
				break;
			default :
				res->status = NIS_RPCERROR;
				break;
			}
		} else
			goto new_handle;
	} else {
		__nis_release_server(cur_server, GOOD_SERVER);
		res->zticks = cpr.cp_zticks;
		res->dticks = cpr.cp_dticks;
		res->aticks += aticks;
		res->cticks += __stop_clock(CLOCK_CLIENT);
		res->status = cpr.cp_status;
	}
	xdr_free(xdr_directory_obj, (char *)&slist);
	return (res);
}

/*
 * nis_mkdir()
 *
 * This function is designed to allow a client to remotely create
 * a directory on a NIS server. When the server is contacted, it
 * will look up the directory object and determine if it should
 * really execute this command and if it should then everythings
 * cool. It returns an error if it can't create the directory.
 */

nis_error
nis_mkdir(name, srv)
	nis_name	name;
	nis_server	*srv;
{
	nis_error	result;
	enum clnt_stat	status;
	CLIENT		*clnt;
	struct timeval	tv;


	clnt = nis_make_rpchandle(srv, 0, NIS_PROG, NIS_VERSION,
					ZMH_VC+ZMH_AUTH, 2048, 2048);
	if (! clnt) {
		syslog(LOG_ERR, "nis_mkdir: Unable to bind to server %s.",
								    srv->name);
		return (NIS_NAMEUNREACHABLE);
	}
	tv.tv_sec = NIS_GEN_TIMEOUT;
	tv.tv_usec = 0;
	status  = clnt_call(clnt, NIS_MKDIR,
				(xdrproc_t) xdr_nis_name, (char *) &name,
				(xdrproc_t)xdr_nis_error, (char *) &result,
				tv);
	switch (status) {
	    case RPC_SUCCESS:
		/* result set in clnt_call */
		break;
	    case RPC_AUTHERROR:
		result = NIS_PERMISSION;
		break;
	    default:
		syslog(LOG_WARNING, "nis_mkdir: RPC Error '%s'",
						clnt_sperrno(status));
		result = NIS_RPCERROR;
		break;
	}
	auth_destroy(clnt->cl_auth);
	clnt_destroy(clnt);
	return (result);
}

/*
 * nis_rmdir()
 *
 * This function is designed to allow a client to remotely remove
 * a directory on a NIS server. When the server is contacted, it
 * will look up the directory object and determine if it should
 * really execute this command and if it should then everythings
 * cool. It returns an error if it can't remove the directory.
 */

nis_error
nis_rmdir(name, srv)
	nis_name	name;
	nis_server	*srv;
{
	nis_error	result;
	CLIENT		*clnt;
	enum clnt_stat	status;
	struct timeval	tv;

	clnt = nis_make_rpchandle(srv, 0, NIS_PROG, NIS_VERSION,
					ZMH_VC+ZMH_AUTH, 2048, 2048);
	if (! clnt) {
		syslog(LOG_ERR, "nis_rmdir: Unable to bind to server %s.",
								srv->name);
		return (NIS_NAMEUNREACHABLE);
	}
	tv.tv_sec = NIS_GEN_TIMEOUT;
	tv.tv_usec = 0;
	status  = clnt_call(clnt, NIS_RMDIR,
			(xdrproc_t) xdr_nis_name, (char *) &name,
			(xdrproc_t)xdr_nis_error, (char *) &result,
			tv);
	switch (status) {
	    case RPC_SUCCESS:
		/* result set in clnt_call */
		break;
	    case RPC_AUTHERROR:
		result = NIS_PERMISSION;
		break;
	    default:
		syslog(LOG_WARNING, "nis_rmdir: RPC Error '%s'",
						clnt_sperrno(status));
		result = NIS_RPCERROR;
		break;
	}
	auth_destroy(clnt->cl_auth);
	clnt_destroy(clnt);
	return (result);
}

/*
 * A version of nis_list that takes a callback function, but doesn't do
 * callbacks over the wire (it gets the objects in the reply and then
 * feeds them to the callback function itself).
 */

nis_result *
__nis_list_localcb(name, flags, cback, cbdata)
	nis_name	name;	/* list name like '[foo=bar].table.name' */
	u_long		flags;		/* Flags for the search */
	int		(*cback)();	/* Callback function. */
	void		*cbdata;	/* Callback private data */
{
	nis_result *res;
	int no;
	nis_object *o;
	char *tab;
	int i;

	/*
	 * Do list without callbacks
	 */
	if ((res = nis_list(name, flags, 0, 0)) == 0)
		return (0);

	/*
	 * Run callback locally
	 */
	if (cback)
		switch (res->status) {
		case NIS_SUCCESS:
		case NIS_S_SUCCESS:
			/*
			 * Always at least one object on success
			 */
			no = res->objects.objects_len;
			o = res->objects.objects_val;
			/*
			 * Figure out the table name
			 */
			if (tab = strchr(name, ']')) {
				tab++;
				while (isspace(*tab) || (*tab == ','))
					tab++;
			} else
				tab = name;
			/*
			 * Run callback
			 */
			for (i = 0; i < no; i++)
				if ((*cback)(tab, &(o[i]), cbdata))
					break;
			/*
			 * Free objects
			 */
			for (i = 0; i < no; i++)
				xdr_free(xdr_nis_object, (char *)&(o[i]));
			free(res->objects.objects_val);
			/*
			 * Fixup result
			 */
			res->objects.objects_len = 0;
			res->objects.objects_val = 0;
			res->status = NIS_CBRESULTS;
			break;
		};

	return (res);
}
