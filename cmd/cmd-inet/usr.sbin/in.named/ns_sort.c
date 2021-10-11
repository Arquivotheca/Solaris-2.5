/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)ns_sort.c	1.6	93/04/16 SMI"	/* SVr4.0 1.1 */

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 *
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 *
 *
 *
 * 		Copyright Notice
 *
 * Notice of copyright on this source code product does not indicate
 * publication.
 *
 * 	(c) 1986,1987,1988.1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *			All rights reserved.
 *
 */


#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <netinet/in.h>
#include <syslog.h>
#include <arpa/nameser.h>
#include "ns.h"
#include "db.h"

extern	char *p_type(), *p_class();

extern	int	debug;
extern  FILE	*ddt;

struct netinfo*
local(from)
	struct sockaddr_in *from;
{
	extern struct netinfo *nettab, netloop, **enettab;
	struct netinfo *ntp;

	if (from->sin_addr.s_addr == netloop.my_addr.s_addr)
		return (&netloop);
	for (ntp = nettab; ntp != *enettab; ntp = ntp->next) {
		if (ntp->net == (from->sin_addr.s_addr & ntp->mask))
			return (ntp);
	}
	return (NULL);
}

void
sort_response(cp, ancount, lp, eom)
	register char *cp;
	register int ancount;
	struct netinfo *lp;
	u_char *eom;
{
	register struct netinfo *ntp;
	extern struct netinfo *nettab;

#ifdef DEBUG
	if (debug > 2)
	    fprintf(ddt, "sort_response(%d)\n", ancount);
#endif DEBUG
	if (ancount > 1) {
		if (sort_rr(cp, ancount, lp, eom))
			return;
		for (ntp = nettab; ntp != NULL; ntp = ntp->next) {
			if ((ntp->net == lp->net) && (ntp->mask == lp->mask))
				continue;
			if (sort_rr(cp, ancount, ntp, eom))
				break;
		}
	}
}

int
sort_rr(cp, count, ntp, eom)
	register u_char *cp;
	int count;
	register struct netinfo *ntp;
	u_char *eom;
{
	int type, class, dlen, n, c;
	struct in_addr inaddr;
	u_char *rr1;

#ifdef DEBUG
	if (debug > 2) {
	    inaddr.s_addr = ntp->net;
	    fprintf(ddt, "sort_rr( x%x, %d, %s)\n", cp, count,
							inet_ntoa(inaddr));
	}
#endif DEBUG
	rr1 = NULL;
	for (c = count; c > 0; --c) {
	    n = dn_skipname(cp, eom);
	    if (n < 0)
		return (1);		/* bogus, stop processing */
	    cp += n;
	    if (cp + QFIXEDSZ > eom)
		return (1);
	    GETSHORT(type, cp);
	    GETSHORT(class, cp);
	    cp += sizeof (u_long);
	    GETSHORT(dlen, cp);
	    if (dlen > eom - cp)
		return (1);		/* bogus, stop processing */
	    switch (type) {
	    case T_A:
		switch (class) {
		case C_IN:
		case C_HS:
#ifdef SYSV
			memcpy((void *)&inaddr, cp, sizeof (inaddr));
#else
			bcopy(cp, (char *)&inaddr, sizeof (inaddr));
#endif
			if (rr1 == NULL)
				rr1 = cp;
			if ((ntp->mask & inaddr.s_addr) == ntp->net) {
#ifdef DEBUG
			    if (debug > 1) {
				fprintf(ddt, "net %s best choice\n",
					inet_ntoa(inaddr));
			    }
#endif DEBUG
			    if (rr1 != cp) {
#ifdef SYSV
				memcpy(cp, rr1, sizeof (inaddr));
				memcpy(rr1, (void *)&inaddr, sizeof (inaddr));
#else
				bcopy(rr1, cp, sizeof (inaddr));
				bcopy((char *)&inaddr, rr1, sizeof (inaddr));
#endif
			    }
			    return (1);
			}
			break;
		}
		break;
	    }
	    cp += dlen;
	}
	return (0);
}
