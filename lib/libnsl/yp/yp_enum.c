/*
 * Copyright (c) 1986-1992 by Sun Microsystems Inc.
 */

#define	NULL 0
#include <rpc/rpc.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include "yp_b.h"
#include <rpcsvc/yp_prot.h>
#include <rpcsvc/ypclnt.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int dofirst(), donext();

extern struct timeval _ypserv_timeout;
extern unsigned int _ypsleeptime;
extern int __yp_dobind();

/*
 * This requests the yp server associated with a given domain to return the
 * first key/value pair from the map data base.  The returned key should be
 * used as an input to the call to ypclnt_next.  This part does the parameter
 * checking, and the do-until-success loop.
 */
int
yp_first (domain, map, key, keylen, val, vallen)
	char *domain;
	char *map;
	char **key;		/* return: key array */
	int  *keylen;		/* return: bytes in key */
	char **val;		/* return: value array */
	int  *vallen;		/* return: bytes in val */
{
	int domlen;
	int maplen;
	struct dom_binding *pdomb;
	int reason;

	trace1(TR_yp_first, 0);
	if ((map == NULL) || (domain == NULL)) {
		trace1(TR_yp_first, 1);
		return (YPERR_BADARGS);
	}

	domlen = (int) strlen(domain);
	maplen = (int) strlen(map);

	if ((domlen == 0) || (domlen > YPMAXDOMAIN) ||
	    (maplen == 0) || (maplen > YPMAXMAP)) {
		trace1(TR_yp_first, 1);
		return (YPERR_BADARGS);
	}

	for (;;) {

		if (reason = __yp_dobind(domain, &pdomb)) {
			trace1(TR_yp_first, 1);
			return (reason);
		}

		if (pdomb->dom_binding->ypbind_hi_vers == YPVERS) {

			reason = dofirst(domain, map, pdomb, _ypserv_timeout,
			    key, keylen, val, vallen);

			__yp_rel_binding(pdomb);
			if (reason == YPERR_RPC) {
				yp_unbind(domain);
				(void) sleep(_ypsleeptime);
			} else {
				break;
			}
		} else {
			__yp_rel_binding(pdomb);
			trace1(TR_yp_first, 1);
			return (YPERR_VERS);
		}
	}
	trace1(TR_yp_first, 1);
	return (reason);
}

/*
 * This part of the "get first" interface talks to ypserv.
 */

static int
dofirst (domain, map, pdomb, timeout, key, keylen, val, vallen)
	char *domain;
	char *map;
	struct dom_binding *pdomb;
	struct timeval timeout;
	char **key;
	int  *keylen;
	char **val;
	int  *vallen;

{
	struct ypreq_nokey req;
	struct ypresp_key_val resp;
	unsigned int retval = 0;

	trace1(TR_dofirst, 0);
	req.domain = domain;
	req.map = map;
	resp.keydat.dptr = resp.valdat.dptr = NULL;
	resp.keydat.dsize = resp.valdat.dsize = 0;

	/*
	 * Do the get first request.  If the rpc call failed, return with status
	 * from this point.
	 */

	memset((char *)&resp, 0, sizeof (struct ypresp_key_val));

	if (clnt_call(pdomb->dom_client, YPPROC_FIRST,
		(xdrproc_t)xdr_ypreq_nokey,
	    (char *)&req, (xdrproc_t)xdr_ypresp_key_val,
		(char *)&resp, timeout) != RPC_SUCCESS) {
		trace1(TR_dofirst, 1);
		return (YPERR_RPC);
	}

	/* See if the request succeeded */

	if (resp.status != YP_TRUE) {
		retval = ypprot_err((unsigned) resp.status);
	}

	/* Get some memory which the user can get rid of as he likes */

	if (!retval) {

		if ((*key =
		    (char *) malloc((unsigned)
			resp.keydat.dsize + 2)) != NULL) {

			if ((*val = (char *) malloc(
			    (unsigned) resp.valdat.dsize + 2)) == NULL) {
				free((char *) *key);
				retval = YPERR_RESRC;
			}

		} else {
			retval = YPERR_RESRC;
		}
	}

	/* Copy the returned key and value byte strings into the new memory */

	if (!retval) {
		*keylen = resp.keydat.dsize;
		(void) memcpy(*key, resp.keydat.dptr,
		    (unsigned) resp.keydat.dsize);
		(*key)[resp.keydat.dsize] = '\n';
		(*key)[resp.keydat.dsize + 1] = '\0';

		*vallen = resp.valdat.dsize;
		(void) memcpy(*val, resp.valdat.dptr,
		    (unsigned) resp.valdat.dsize);
		(*val)[resp.valdat.dsize] = '\n';
		(*val)[resp.valdat.dsize + 1] = '\0';
	}

	CLNT_FREERES(pdomb->dom_client,
		(xdrproc_t)xdr_ypresp_key_val, (char *)&resp);
	trace1(TR_dofirst, 1);
	return (retval);
}

/*
 * This requests the yp server associated with a given domain to return the
 * "next" key/value pair from the map data base.  The input key should be
 * one returned by ypclnt_first or a previous call to ypclnt_next.  The
 * returned key should be used as an input to the next call to ypclnt_next.
 * This part does the parameter checking, and the do-until-success loop.
 */
int
yp_next (domain, map, inkey, inkeylen, outkey, outkeylen, val, vallen)
	char *domain;
	char *map;
	char *inkey;
	int  inkeylen;
	char **outkey;		/* return: key array associated with val */
	int  *outkeylen;	/* return: bytes in key */
	char **val;		/* return: value array associated with outkey */
	int  *vallen;		/* return: bytes in val */
{
	int domlen;
	int maplen;
	struct dom_binding *pdomb;
	int reason;


	trace1(TR_yp_next, 0);
	if ((map == NULL) || (domain == NULL) || (inkey == NULL)) {
		trace1(TR_yp_next, 1);
		return (YPERR_BADARGS);
	}

	domlen = (int) strlen(domain);
	maplen = (int) strlen(map);

	if ((domlen == 0) || (domlen > YPMAXDOMAIN) ||
	    (maplen == 0) || (maplen > YPMAXMAP)) {
		trace1(TR_yp_next, 1);
		return (YPERR_BADARGS);
	}

	for (;;) {
		if (reason = __yp_dobind(domain, &pdomb)) {
			trace1(TR_yp_next, 1);
			return (reason);
		}

		if (pdomb->dom_binding->ypbind_hi_vers == YPVERS) {

			reason = donext(domain, map, inkey, inkeylen, pdomb,
			    _ypserv_timeout, outkey, outkeylen, val, vallen);

			__yp_rel_binding(pdomb);
			if (reason == YPERR_RPC) {
				yp_unbind(domain);
				(void) sleep(_ypsleeptime);
			} else {
				break;
			}
		} else {
			__yp_rel_binding(pdomb);
			trace1(TR_yp_next, 1);
			return (YPERR_VERS);
		}
	}

	trace1(TR_yp_next, 1);
	return (reason);
}

/*
 * This part of the "get next" interface talks to ypserv.
 */
static int
donext (domain, map, inkey, inkeylen, pdomb, timeout, outkey, outkeylen,
    val, vallen)
	char *domain;
	char *map;
	char *inkey;
	int  inkeylen;
	struct dom_binding *pdomb;
	struct timeval timeout;
	char **outkey;		/* return: key array associated with val */
	int  *outkeylen;	/* return: bytes in key */
	char **val;		/* return: value array associated with outkey */
	int  *vallen;		/* return: bytes in val */

{
	struct ypreq_key req;
	struct ypresp_key_val resp;
	unsigned int retval = 0;

	trace2(TR_donext, 0, inkeylen);
	req.domain = domain;
	req.map = map;
	req.keydat.dptr = inkey;
	req.keydat.dsize = inkeylen;

	resp.keydat.dptr = resp.valdat.dptr = NULL;
	resp.keydat.dsize = resp.valdat.dsize = 0;

	/*
	 * Do the get next request.  If the rpc call failed, return with status
	 * from this point.
	 */

	if (clnt_call(pdomb->dom_client,
	    YPPROC_NEXT, (xdrproc_t)xdr_ypreq_key, (char *)&req,
		    (xdrproc_t) xdr_ypresp_key_val, (char *)&resp,
	    timeout) != RPC_SUCCESS) {
		trace1(TR_donext, 1);
		return (YPERR_RPC);
	}

	/* See if the request succeeded */

	if (resp.status != YP_TRUE) {
		retval = ypprot_err((unsigned) resp.status);
	}

	/* Get some memory which the user can get rid of as he likes */

	if (!retval) {
		if ((*outkey = (char *) malloc((unsigned)
		    resp.keydat.dsize + 2)) != NULL) {

			if ((*val = (char *) malloc((unsigned)
			    resp.valdat.dsize + 2)) == NULL) {
				free((char *) *outkey);
				retval = YPERR_RESRC;
			}

		} else {
			retval = YPERR_RESRC;
		}
	}

	/* Copy the returned key and value byte strings into the new memory */

	if (!retval) {
		*outkeylen = resp.keydat.dsize;
		(void) memcpy(*outkey, resp.keydat.dptr,
		    (unsigned) resp.keydat.dsize);
		(*outkey)[resp.keydat.dsize] = '\n';
		(*outkey)[resp.keydat.dsize + 1] = '\0';

		*vallen = resp.valdat.dsize;
		(void) memcpy(*val, resp.valdat.dptr,
		    (unsigned) resp.valdat.dsize);
		(*val)[resp.valdat.dsize] = '\n';
		(*val)[resp.valdat.dsize + 1] = '\0';
	}

	CLNT_FREERES(pdomb->dom_client, (xdrproc_t)xdr_ypresp_key_val,
		    (char *)&resp);
	trace1(TR_donext, 1);
	return (retval);
}
