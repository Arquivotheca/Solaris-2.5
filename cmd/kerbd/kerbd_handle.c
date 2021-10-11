/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/


#ident	"@(#)kerbd_handle.c	1.3	92/01/29 SMI"
/*	from "key_call.c	1.4	90/02/13 SMI" */   /* SVr4.0 1.4 */

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
*	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
*	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
*          All rights reserved.
*/
/*
 * kerbd_handle.c, Interface to kerbd
 *
 */

#include <rpc/rpc.h>
#include <stdio.h>
#include <string.h>
#include <netconfig.h>
#include <sys/utsname.h>
#include "kerbd.h"

#ifdef DEBUG
#define	dprt(msg)	(void) fprintf(stderr, "%s\n", msg);
#else
#define	dprt(msg)
#endif /* DEBUG */


/*
 * Keep the handle cached.  This call may be made quite often.
 */
CLIENT *
getkerbd_handle()
{
	void *localhandle;
	struct netconfig *nconf;
	struct netconfig *tpconf;
	static CLIENT *clnt;
	struct timeval wait_time;
	struct utsname u;
	static char *hostname;
	static bool_t first_time = TRUE;

#define	TOTAL_TIMEOUT	30	/* total timeout talking to kerbd */
#define	TOTAL_TRIES	5	/* Number of tries */

	if (clnt)
		return (clnt);
	if (!(localhandle = setnetconfig()))
		return (NULL);
	tpconf = NULL;
	if (first_time == TRUE) {
		if (uname(&u) == -1)
			return ((CLIENT *) NULL);
		if ((hostname = strdup(u.nodename)) == (char *) NULL)
			return ((CLIENT *) NULL);
		first_time = FALSE;
	}
	while (nconf = getnetconfig(localhandle)) {
		if (strcmp(nconf->nc_protofmly, NC_LOOPBACK) == 0) {
			if (nconf->nc_semantics == NC_TPI_CLTS) {
				clnt = clnt_tp_create(hostname,
					KERBPROG, KERBVERS, nconf);
				if (clnt) {
					dprt("got CLTS\n");
					break;
				}
			} else {
				tpconf = nconf;
			}
		}
	}
	if ((clnt == NULL) && (tpconf)) {
		/* Now, try the connection-oriented loopback transport */
		clnt = clnt_tp_create(hostname, KERBPROG, KERBVERS, tpconf);
#ifdef DEBUG
		if (clnt) {
			dprt("got COTS\n");
		}
#endif DEBUG
	}
	endnetconfig(localhandle);

	if (clnt == NULL)
		return (NULL);

	clnt->cl_auth = authsys_create("", getuid(), 0, 0, NULL);
	if (clnt->cl_auth == NULL) {
		clnt_destroy(clnt);
		clnt = NULL;
		return (NULL);
	}
	wait_time.tv_sec = TOTAL_TIMEOUT/TOTAL_TRIES;
	wait_time.tv_usec = 0;
	(void) clnt_control(clnt, CLSET_RETRY_TIMEOUT, (char *)&wait_time);

	return (clnt);
}
