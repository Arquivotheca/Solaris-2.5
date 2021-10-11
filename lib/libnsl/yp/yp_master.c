#ident	"@(#)yp_master.c	1.11	95/09/26 SMI"
/*
 * Copyright (c) 1986-1992, 1995 by Sun Microsystems Inc.
 */

#define	NULL 0
#include <rpc/rpc.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include "yp_b.h"
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

static int domaster();


extern struct timeval _ypserv_timeout;
extern int __yp_dobind();
extern int __yp_dobind_rsvdport();
extern unsigned int _ypsleeptime;
extern void free_dom_binding ();

/*
 * This checks parameters, and implements the outer "until binding success"
 * loop.
 */
int
yp_master (domain, map, master)
	char *domain;
	char *map;
	char **master;
{
	int domlen;
	int maplen;
	int reason;
	struct dom_binding *pdomb;

	trace1(TR_yp_master, 0);
	if ((map == NULL) || (domain == NULL)) {
		trace1(TR_yp_master, 1);
		return (YPERR_BADARGS);
	}

	domlen = (int) strlen(domain);
	maplen = (int) strlen(map);

	if ((domlen == 0) || (domlen > YPMAXDOMAIN) ||
	    (maplen == 0) || (maplen > YPMAXMAP) ||
	    (master == NULL)) {
		trace1(TR_yp_master, 1);
		return (YPERR_BADARGS);
	}

	for (;;) {

		if (reason = __yp_dobind(domain, &pdomb)) {
			trace1(TR_yp_master, 1);
			return (reason);
		}

		if (pdomb->dom_binding->ypbind_hi_vers >= YPVERS) {

			reason = domaster(domain, map, pdomb, _ypserv_timeout,
			    master);

			__yp_rel_binding(pdomb);
			if (reason == YPERR_RPC) {
				yp_unbind(domain);
				(void) sleep(_ypsleeptime);
			} else {
				break;
			}
		} else {
			__yp_rel_binding(pdomb);
			trace1(TR_yp_master, 1);
			return (YPERR_VERS);
		}
	}

	trace1(TR_yp_master, 1);
	return (reason);
}


/*
 * This function is identical to 'yp_master' with the exception that it calls
 * '__yp_dobind_rsvdport' rather than '__yp_dobind'
 */
int
__yp_master_rsvdport (domain, map, master)
	char *domain;
	char *map;
	char **master;
{
	int domlen;
	int maplen;
	int reason;
	struct dom_binding *pdomb;

	trace1(TR_yp_master, 0);
	if ((map == NULL) || (domain == NULL)) {
		trace1(TR_yp_master, 1);
		return (YPERR_BADARGS);
	}

	domlen = (int) strlen(domain);
	maplen = (int) strlen(map);

	if ((domlen == 0) || (domlen > YPMAXDOMAIN) ||
	    (maplen == 0) || (maplen > YPMAXMAP) ||
	    (master == NULL)) {
		trace1(TR_yp_master, 1);
		return (YPERR_BADARGS);
	}

	for (;;) {

		if (reason = __yp_dobind_rsvdport(domain, &pdomb)) {
			trace1(TR_yp_master, 1);
			return (reason);
		}

		if (pdomb->dom_binding->ypbind_hi_vers >= YPVERS) {

			reason = domaster(domain, map, pdomb, _ypserv_timeout,
			    master);

			/*
			 * Have to free the binding since the reserved
			 * port bindings are not cached.
			 */
			__yp_rel_binding(pdomb);
			free_dom_binding(pdomb);
			if (reason == YPERR_RPC) {
				yp_unbind(domain);
				(void) sleep(_ypsleeptime);
			} else {
				break;
			}
		} else {
			/*
			 * Have to free the binding since the reserved
			 * port bindings are not cached.
			 */
			__yp_rel_binding(pdomb);
			free_dom_binding(pdomb);
			trace1(TR_yp_master, 1);
			return (YPERR_VERS);
		}
	}

	trace1(TR_yp_master, 1);
	return (reason);
}

/*
 * This talks v2 to ypserv
 */
static int
domaster (domain, map, pdomb, timeout, master)
	char *domain;
	char *map;
	struct dom_binding *pdomb;
	struct timeval timeout;
	char **master;
{
	struct ypreq_nokey req;
	struct ypresp_master resp;
	unsigned int retval = 0;

	trace1(TR_domaster, 0);
	req.domain = domain;
	req.map = map;
	memset((char *) &resp, 0, sizeof (struct ypresp_master));

	/*
	 * Do the get_master request.  If the rpc call failed, return with
	 * status from this point.
	 */

	if (clnt_call(pdomb->dom_client,
			YPPROC_MASTER, (xdrproc_t)xdr_ypreq_nokey,
		    (char *)&req, (xdrproc_t)xdr_ypresp_master, (char *)&resp,
		    timeout) != RPC_SUCCESS) {
		trace1(TR_domaster, 1);
		return (YPERR_RPC);
	}

	/* See if the request succeeded */

	if (resp.status != YP_TRUE) {
		retval = ypprot_err((unsigned) resp.status);
	}

	/* Get some memory which the user can get rid of as he likes */

	if (!retval && ((*master = malloc(strlen(resp.master) + 1))
	    == NULL)) {
		retval = YPERR_RESRC;

	}

	if (!retval) {
		(void) strcpy(*master, resp.master);
	}

	CLNT_FREERES(pdomb->dom_client,
		(xdrproc_t)xdr_ypresp_master, (char *)&resp);
	trace1(TR_domaster, 1);
	return (retval);
}
