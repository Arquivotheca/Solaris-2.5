/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/


#ident	"@(#)ypserv_proc.c	1.8	92/07/14 SMI"        /* SVr4.0 1.6   */

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*	PROPRIETARY NOTICE (Combined)
*
* This source code is unpublished proprietary information
* constituting, or derived under license from AT&T's UNIX(r) System V.
* In addition, portions of such source code were derived from Berkeley
* 4.3 BSD under license from the Regents of the University of
* California.
*
*
*
*	Copyright Notice 
*
* Notice of copyright on this source code product does not indicate 
*  publication.
*
*	(c) 1986,1987,1988,1989,1990  Sun Microsystems, Inc
*	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
*          All rights reserved.
*/ 
/*
 *
 * This contains YP server code which supplies the set of functions 
 * requested using rpc.   The top level functions in this module
 * are those which have symbols of the form YPPROC_xxxx defined in
 * yp_prot.h, and symbols of the form YPOLDPROC_xxxx defined in ypsym.h.
 * The latter exist to provide compatibility to the old version of the yp
 * protocol/server, and may emulate the behaviour of the previous software
 * by invoking some other program.
 * 
 * This module also contains functions which are used by (and only by) the
 * top-level functions here.
 *  
 */

#include <dirent.h>
#include <limits.h>
#include <rpc/rpc.h>
#include "ypsym.h"
#include "ypdefs.h"
USE_YP_PREFIX
#include <ctype.h>

extern char *environ;
#ifndef YPXFR_PROC
#define YPXFR_PROC "/usr/sbin/ypxfr"
#endif
static char ypxfr_proc[] = YPXFR_PROC;
#ifndef YPPUSH_PROC
#define YPPUSH_PROC "/usr/sbin/yppush"
#endif
struct yppriv_sym {
	char *sym;
	unsigned len;
};
static char err_fork[] = "ypserv:  %s fork failure.\n";
#define FORK_ERR logprintf( err_fork, fun)
static char err_execl[] = "ypserv:  %s execl failure.\n";
#define EXEC_ERR logprintf( err_execl, fun)
static char err_respond[] = "ypserv: %s can't respond to rpc request.\n";
#define RESPOND_ERR logprintf( err_respond, fun)
static char err_free[] = "ypserv: %s can't free args.\n";
#define FREE_ERR logprintf( err_free, fun)
static char err_map[] = "ypserv: %s no such map or access denied.\n";
#define MAP_ERR logprintf( err_map, fun)
static char err_alias[] = "ypserv: %s no alias for map or domain.\n";
#define ALIAS_ERR logprintf( err_alias, fun)
static char err_vers[] = "ypserv: %s version not supported.\n";
#define VERS_ERR logprintf( err_vers, fun)

static void ypfilter();
static bool isypsym();
static bool xdrypserv_ypall();

extern void logprintf();
extern bool ypcheck_domain();
extern int yp_getalias();
extern bool ypget_map_master();
extern bool ypget_map_order();
extern datum dbm_do_nextkey(); 

/*
 * This determines whether or not a passed domain is served by this server,
 * and returns a boolean.  Used by both old and new protocol versions.
 */
void
ypdomain(transp, always_respond)
	SVCXPRT *transp;
	bool always_respond;
{
	char domain_name[YPMAXDOMAIN + 1];
	char domain_alias[YPMAXDOMAIN + 1];
	char *pdomain_name = domain_name;
	bool isserved;
	char *fun = "ypdomain";

	memset(pdomain_name, 0, YPMAXDOMAIN + 1);

	if (!svc_getargs(transp, (xdrproc_t)xdr_ypdomain_wrap_string,
			 (char *)&pdomain_name) ) {
		svcerr_decode(transp);
		return;
	}

	if (yp_getalias(domain_name, domain_alias, NAME_MAX) < 0) {
		ALIAS_ERR;
		(void) strcpy(domain_alias, domain_name);
	}

	isserved = ypcheck_domain(domain_alias);

	if (isserved || always_respond) {
		
		if (!svc_sendreply(transp, xdr_bool, (char *) &isserved) ) {
			RESPOND_ERR;
		}
		if (!isserved)
			logprintf("Domain %s not supported\n",
				domain_name);

	} else {
		/*
		 * This case is the one in which the domain is not
		 * supported, and in which we are not to respond in the
		 * unsupported case.  We are going to make an error happen
		 * to allow the portmapper to end his wait without the
		 * normal timeout period.  The assumption here is that
		 * the only process in the world which is using the function
		 * in its no-answer-if-nack form is the portmapper, which is
		 * doing the krock for pseudo-broadcast.  If some poor fool
		 * calls this function as a single-cast message, the nack
		 * case will look like an incomprehensible error.  Sigh...
		 * (The traditional Unix disclaimer)
		 */
	
		svcerr_decode(transp);
		logprintf("Domain %s not supported (broadcast)\n",
				domain_name);
	}
}

/*
 * This implements the yp "match" function.
 */
/*ARGSUSED*/
void
ypmatch(rqstp, transp) 
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	char domain_alias[YPMAXDOMAIN + 1];
	char map_alias[MAXNAMLEN + 1];
	struct ypreq_key req;
	struct ypresp_val resp;
	char *fun = "ypmatch";
	DBM *fdb;
	
	req.domain = req.map = NULL;
	req.keydat.dptr = NULL;
	resp.valdat.dptr = NULL;
	resp.valdat.dsize = 0;

	memset((char *)&req, 0, sizeof(req));

	if (!svc_getargs(transp, (xdrproc_t)xdr_ypreq_key, (char *)&req) ) {
		svcerr_decode(transp);
		return;
	}

	if ((yp_getalias(req.domain, domain_alias, NAME_MAX) < 0) ||
	    (yp_getalias(req.map, map_alias, MAXALIASLEN) < 0)) {
		ALIAS_ERR;
		(void) strcpy(domain_alias, req.domain);
		(void) strcpy(map_alias, req.map);
	}

	if ((fdb = ypset_current_map(map_alias, domain_alias, 
	    (unsigned *)&resp.status)) != NULL) {

		resp.valdat = dbm_fetch(fdb, req.keydat);

		if (resp.valdat.dptr != NULL)
			resp.status = YP_TRUE;
		else
			resp.status = YP_NOKEY;
	}

	if (!svc_sendreply(transp, (xdrproc_t)xdr_ypresp_val, (char *)&resp) ) {
		RESPOND_ERR;
	}

	if (!svc_freeargs(transp, (xdrproc_t)xdr_ypreq_key, (char *)&req) ) {
		FREE_ERR;
	}
}


/*
 * This implements the yp "get first" function.
 */
/*ARGSUSED*/
void
ypfirst(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	char domain_alias[YPMAXDOMAIN + 1];
	char map_alias[MAXNAMLEN + 1];
	struct ypreq_nokey req;
	struct ypresp_key_val resp;
	char *fun = "ypfirst";
	DBM *fdb;
	
	req.domain = req.map = NULL;
	resp.keydat.dptr = resp.valdat.dptr = NULL;
	resp.keydat.dsize = resp.valdat.dsize = 0;

	memset((char *)&req, 0, sizeof(req));

	if (!svc_getargs(transp, (xdrproc_t)xdr_ypreq_nokey, (char *)&req) ) {
		svcerr_decode(transp);
		return;
	}

	if ((yp_getalias(req.domain, domain_alias, NAME_MAX) < 0) ||
	    (yp_getalias(req.map, map_alias, MAXALIASLEN) < 0)) {
		ALIAS_ERR;
		(void) strcpy(domain_alias, req.domain);
		(void) strcpy(map_alias, req.map);
	}

	if ((fdb = ypset_current_map(map_alias, domain_alias, 
	    (unsigned *)&resp.status)) != NULL) {
		ypfilter(fdb, NULL, &resp.keydat, &resp.valdat, (int *)&resp.status);
	}

	if (!svc_sendreply(transp, (xdrproc_t)xdr_ypresp_key_val, (char *)&resp) ) {
		RESPOND_ERR;
	}

	if (!svc_freeargs(transp, (xdrproc_t)xdr_ypreq_nokey, (char *)&req) ) {
		FREE_ERR;
	}
}

/*
 * This implements the yp "get next" function.
 */
/*ARGSUSED*/
void
ypnext(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	char domain_alias[YPMAXDOMAIN + 1];
	char map_alias[MAXNAMLEN + 1];
	struct ypreq_key req;
	struct ypresp_key_val resp;
	char *fun = "ypnext";
	DBM *fdb;
	
	req.domain = req.map = req.keydat.dptr = NULL;
	req.keydat.dsize = 0;
	resp.keydat.dptr = resp.valdat.dptr = NULL;
	resp.keydat.dsize = resp.valdat.dsize = 0;

	memset((char *)&req, 0, sizeof(req));

	if (!svc_getargs(transp, (xdrproc_t)xdr_ypreq_key, (char *)&req) ) {
		svcerr_decode(transp);
		return;
	}

	if ((yp_getalias(req.domain, domain_alias, NAME_MAX) < 0) ||
	    (yp_getalias(req.map, map_alias, MAXALIASLEN) < 0)) {
		ALIAS_ERR;
		(void) strcpy(domain_alias, req.domain);
		(void) strcpy(map_alias, req.map);
	}

	if ((fdb = ypset_current_map(map_alias, domain_alias, 
	    (unsigned *)&resp.status)) != NULL) {
		ypfilter(fdb, &req.keydat, &resp.keydat, &resp.valdat, 
		    (int *)&resp.status);
	}
	
	if (!svc_sendreply(transp, (xdrproc_t)xdr_ypresp_key_val, (char *)&resp) ) {
		RESPOND_ERR;
	}

	if (!svc_freeargs(transp, (xdrproc_t)xdr_ypreq_key, (char *)&req) ) {
		FREE_ERR;
	}

}

/*
 * This implements the  "transfer map" function.  It takes the domain and
 * map names and the callback information provided by the requester (yppush
 * on some node), and execs a ypxfr process to do the actual transfer.  
 */
/*ARGSUSED*/
void
ypxfr(rqstp, transp, prog)
	struct svc_req *rqstp;
	SVCXPRT *transp;
	int prog;
{
	struct ypreq_newxfr newreq;
	struct ypreq_xfr oldreq;
	struct ypresp_val resp;  /* not returned to the caller */
	char transid[10];
	char proto[10];
	char name[64];
	char domain_alias[YPMAXDOMAIN];
	char map_alias[YPMAXMAP];
	int pid = -1;
	char *fun = "ypxfr";
	char my_name[256];

	if (prog == YPPROC_NEWXFR) {
		newreq.ypxfr_domain = newreq.ypxfr_map = 
		    newreq.ypxfr_owner = NULL;
		newreq.ypxfr_ordernum = 0;
		memset((char *)&newreq, 0, sizeof(newreq));
		if (!svc_getargs(transp, (xdrproc_t)xdr_ypreq_newxfr,
				 (char *) &newreq) ) {
			svcerr_decode(transp);
			return;
		}
#ifdef DEBUG
		(void) gethostname(my_name, 256);
		if (strcmp(newreq.name, my_name) != 0)
			fprintf(stderr, "ypserv: yppush request was meant for %s and not me !!\n", newreq.name);
#endif		 
		(void) sprintf(transid, "%d", (int) newreq.transid);
		(void) sprintf(proto, "%d", (int) newreq.proto);
		(void) sprintf(name, "%s", newreq.ypxfr_owner);
		if ((yp_getalias(newreq.ypxfr_domain, domain_alias, NAME_MAX) < 0) ||
		    (yp_getalias(newreq.ypxfr_map, map_alias, MAXALIASLEN) < 0)) {
			ALIAS_ERR;
			(void) strcpy(domain_alias, newreq.ypxfr_domain);
			(void) strcpy(map_alias, newreq.ypxfr_map);
		}
	} else if (prog == YPPROC_XFR) {
		oldreq.ypxfr_domain = oldreq.ypxfr_map =
			oldreq.ypxfr_owner = NULL;
		oldreq.ypxfr_ordernum = 0;
		memset((char *)&oldreq, 0, sizeof(oldreq));
		if (!svc_getargs(transp, (xdrproc_t)xdr_ypreq_xfr, 
				 (char *) &oldreq)) {
			svcerr_decode(transp);
			return;
		}
		(void) sprintf(transid, "%d", (int) oldreq.transid);
		(void) sprintf(proto, "%d", (int) oldreq.proto);
		(void) sprintf(name, "%s", oldreq.ypxfr_owner);
		(void) strcpy(domain_alias, oldreq.ypxfr_domain);
		(void) strcpy(map_alias, oldreq.ypxfr_map);
	} else {
		VERS_ERR;
	}

	/* Check that the map exists and is accessable */
	if (ypset_current_map(map_alias, domain_alias,
	    (unsigned *)&resp.status)) {
		pid = (int) fork();
		if (pid == -1) {
			FORK_ERR;
		} else if (pid == 0) {
			if (prog == YPPROC_NEWXFR || prog == YPPROC_XFR) {
				if (execl(ypxfr_proc, "ypxfr", "-d", 
				    domain_alias, "-C", transid, proto, 
				    name, map_alias, NULL)) 
					EXEC_ERR;
			} else {
				VERS_ERR;
			}
			_exit(1);
		}

	} else {
		MAP_ERR;
	}
	if (!svc_sendreply(transp, xdr_void, 0) ) {
		RESPOND_ERR;
	}
	
	if (prog == YPPROC_NEWXFR) {
		if (!svc_freeargs(transp, (xdrproc_t)xdr_ypreq_newxfr,
				  (char *)&newreq) ) {
			FREE_ERR;
		}
	}
}

/*
 * This implements the "get all" function.
 */
/*ARGSUSED*/
void
ypall(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	char domain_alias[YPMAXDOMAIN + 1];
	char map_alias[MAXNAMLEN + 1];
	struct ypreq_nokey req;
	struct ypresp_val resp;  /* not returned to the caller */
	int pid;
	char *fun = "ypall";

	req.domain = req.map = NULL;

	memset((char *)&req, 0, sizeof(req));

	if (!svc_getargs(transp, (xdrproc_t)xdr_ypreq_nokey, (char *)&req) ) {
		svcerr_decode(transp);
		return;
	}

	pid = (int) fork();
	
	if (pid) {
		
		if (pid == -1) {
			FORK_ERR;
		}

		if (!svc_freeargs(transp, (xdrproc_t)xdr_ypreq_nokey, (char *)&req) ) {
			FREE_ERR;
		}

		return;
	}
	
	if ((yp_getalias(req.domain, domain_alias, NAME_MAX) < 0) ||
	    (yp_getalias(req.map, map_alias, MAXALIASLEN) < 0)) {
		ALIAS_ERR;
		(void) strcpy(domain_alias, req.domain);
		(void) strcpy(map_alias, req.map);
	}
	(void) strcpy(req.domain, domain_alias);
	(void) strcpy(req.map, map_alias);

	ypclr_current_map();

	/*
	 * This is the child process.  The work gets done by xdrypserv_ypall/
	 * we must clear the "current map" first so that we do not
	 * share a seek pointer with the parent server.
	 */
	
	if (!svc_sendreply(transp, (xdrproc_t)xdrypserv_ypall, (char *)&req) ) {
		RESPOND_ERR;
	}

	if (!svc_freeargs(transp, (xdrproc_t)xdr_ypreq_nokey, (char *)&req) ) {
		FREE_ERR;
	}
	
	exit(0);
}

/*
 * This implements the "get master name" function.
 */
void
ypmaster(transp)
	SVCXPRT *transp;
{
	char domain_alias[YPMAXDOMAIN + 1];
	char map_alias[MAXNAMLEN + 1];
	struct ypreq_nokey req;
	struct ypresp_master resp;
	char *nullstring = "";
	char *fun = "ypmaster";
	
	req.domain = req.map = NULL;
	resp.master = nullstring;
	resp.status  = YP_TRUE;

	memset((char *)&req, 0, sizeof(req));

	if (!svc_getargs(transp, (xdrproc_t)xdr_ypreq_nokey, (char *)&req) ) {
		svcerr_decode(transp);
		return;
	}

	if ((yp_getalias(req.domain, domain_alias, NAME_MAX) < 0) ||
	    (yp_getalias(req.map, map_alias, MAXALIASLEN) < 0)) {
		ALIAS_ERR;
		(void) strcpy(domain_alias, req.domain);
		(void) strcpy(map_alias, req.map);
	}

	if (ypset_current_map(map_alias, domain_alias, 
	    (unsigned *)&resp.status)) {
		if (!ypget_map_master(map_alias, domain_alias, 
		    &resp.master) ) {
			resp.status = YP_BADDB;
		}
	}
	
	if (!svc_sendreply(transp, (xdrproc_t)xdr_ypresp_master, (char *)&resp) ) {
		RESPOND_ERR;
	}

	if (!svc_freeargs(transp, (xdrproc_t)xdr_ypreq_nokey, (char *)&req) ) {
		FREE_ERR;
	}
}

/*
 * This implements the "get order number" function.
 */
void
yporder(transp)
	SVCXPRT *transp;
{
	char domain_alias[YPMAXDOMAIN + 1];
	char map_alias[MAXNAMLEN + 1];
	struct ypreq_nokey req;
	struct ypresp_order resp;
	char *fun = "yporder";
	
	req.domain = req.map = NULL;
	resp.status  = YP_TRUE;
	resp.ordernum  = 0;

	memset((char *)&req, 0, sizeof(req));

	if (!svc_getargs(transp, (xdrproc_t)xdr_ypreq_nokey, (char *)&req) ) {
		svcerr_decode(transp);
		return;
	}

	resp.ordernum = 0;

	if ((yp_getalias(req.domain, domain_alias, NAME_MAX) < 0) ||
	    (yp_getalias(req.map, map_alias, MAXALIASLEN) < 0)) {
		ALIAS_ERR;
		(void) strcpy(domain_alias, req.domain);
		(void) strcpy(map_alias, req.map);
	}

	if (ypset_current_map(map_alias, domain_alias, 
	    (unsigned *)&resp.status)) {
		if (!ypget_map_order(map_alias, domain_alias, 
		    (unsigned *)&resp.ordernum) ) {
			resp.status = YP_BADDB;
		}
	}

	if (!svc_sendreply(transp, (xdrproc_t)xdr_ypresp_order, (char *)&resp) ) {
		RESPOND_ERR;
	}

	if (!svc_freeargs(transp, (xdrproc_t)xdr_ypreq_nokey, (char *)&req) ) {
		FREE_ERR;
	}
}

void
ypmaplist(transp)
	SVCXPRT *transp;
{
	char domain_name[YPMAXDOMAIN + 1];
	char domain_alias[YPMAXDOMAIN + 1];
	char *pdomain_name = domain_name;
	char *fun = "ypmaplist";
	struct ypresp_maplist maplist;
	struct ypmaplist *tmp;

	maplist.list = (struct ypmaplist *) NULL;

	memset(pdomain_name, 0, YPMAXDOMAIN+1);

	if (!svc_getargs(transp, (xdrproc_t)xdr_ypdomain_wrap_string,
			 (char *) &pdomain_name) ) {
		svcerr_decode(transp);
		return;
	}

	if (yp_getalias(domain_name, domain_alias, NAME_MAX) < 0) {
		ALIAS_ERR;
		(void) strcpy(domain_alias, domain_name);
	}

	maplist.status = yplist_maps(domain_alias, &maplist.list);
	
	if (!svc_sendreply(transp, (xdrproc_t)xdr_ypresp_maplist, (char *)&maplist)) {
		RESPOND_ERR;
	}

	while (maplist.list) {
		tmp = maplist.list->ypml_next;
		free((char *)maplist.list);
		maplist.list = tmp;
	}
}

/*
 * Ancillary functions used by the top-level functions within this module
 */

/*
 * This returns TRUE if a given key is a yp-private symbol, otherwise FALSE
 */
static bool
isypsym(key)
	datum *key;
{

	if ((key->dptr == NULL) || (key->dsize < yp_prefix_sz) ||
	    memcmp(yp_prefix, key->dptr, yp_prefix_sz) ) {
		return (FALSE);
	}
	return (TRUE);
}

/*
 * This provides private-symbol filtration for the enumeration functions.
 */
static void
ypfilter(fdb, inkey, outkey, val, status)
	DBM *fdb;
	datum *inkey;
	datum *outkey;
	datum *val;
	int *status;
{
	datum k;

	if (inkey) {

		if (isypsym(inkey) ) {
			*status = YP_BADARGS;
			return;
		}
		
		k = dbm_do_nextkey(fdb, *inkey);
	} else {
		k = dbm_firstkey(fdb);
	}
	
	while (k.dptr && isypsym(&k)) {
		k = dbm_nextkey(fdb);
	}
		
	if (k.dptr == NULL) {
		*status = YP_NOMORE;
		return;
	}

	*outkey = k;
	*val = dbm_fetch(fdb, k);

	if (val->dptr != NULL) {
		*status = YP_TRUE;
	} else {
		*status = YP_BADDB;
	}
}
		
/*
 * Serializes a stream of struct ypresp_key_val's.  This is used
 * only by the ypserv side of the transaction.
 */
static bool
xdrypserv_ypall(xdrs, req)
	XDR * xdrs;
	struct ypreq_nokey *req;
{
	bool_t more = TRUE;
	struct ypresp_key_val resp;
	DBM *fdb;

	resp.keydat.dptr = resp.valdat.dptr = (char *) NULL;
	resp.keydat.dsize = resp.valdat.dsize = 0;
	
	if ((fdb = ypset_current_map(req->map, req->domain,
			&resp.status)) != NULL) {
		ypfilter(fdb, (datum *)NULL, &resp.keydat, &resp.valdat, 
		    (int *)&resp.status);

		while (resp.status == YP_TRUE) {
			if (!xdr_bool(xdrs, &more) ) {
				return (FALSE);
			}

			if (!xdr_ypresp_key_val(xdrs, &resp) ) {
				return (FALSE);
			}

			ypfilter(fdb, &resp.keydat, &resp.keydat, &resp.valdat,
			    (int *) &resp.status);
		}
	}
	
	if (!xdr_bool(xdrs, &more) ) {
		return (FALSE);
	}

	if (!xdr_ypresp_key_val(xdrs, &resp) ) {
		return (FALSE);
	}

	more = FALSE;
	
	if (!xdr_bool(xdrs, * &more) ) {
		return (FALSE);
	}

	return (TRUE);
}
