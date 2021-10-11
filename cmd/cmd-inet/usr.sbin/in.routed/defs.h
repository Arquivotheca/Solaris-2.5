/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)defs.h	1.6	92/07/14 SMI"	/* SVr4.0 1.2	*/

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
 * 	          All rights reserved.
 *  
 */


/*
 * Internal data structure definitions for
 * user routing process.  Based on Xerox NS
 * protocol specs with mods relevant to more
 * general addressing scheme.
 */
#include <sys/types.h>
#include <sys/socket.h>
#ifdef SYSV
#include <sys/stream.h>
#endif SYSV
#include <sys/time.h>

#include <net/route.h>
#include <netinet/in.h>
#include <protocols/routed.h>

#include <stdio.h>
#include <netdb.h>

#include "trace.h"
#include "interface.h"
#include "table.h"
#include "af.h"


/* Should be in <protocols/routed.h> */
#define	MIN_WAITTIME		2	/* min. interval to broadcast changes */
#define	MAX_WAITTIME		5	/* max. time to delay changes */

/*
 * When we find any interfaces marked down we rescan the
 * kernel every CHECK_INTERVAL seconds to see if they've
 * come up.
 */
#define	CHECK_INTERVAL	(1*60)

#define	LOOPBACKNET	0x7F000000
#define equal(a1, a2) \
	(bcmp((caddr_t)(a1), (caddr_t)(a2), sizeof (struct sockaddr)) == 0)
#define	min(a,b)	((a)>(b)?(b):(a))

struct	sockaddr_in addr;	/* address of daemon's socket */

int	s;			/* source and sink of all data */
int	supplier;		/* process should supply updates */
int	maysupply;		/* process must, may, or may not supply */
int	install;		/* if 1 call kernel */
int	lookforinterfaces;	/* if 1 probe kernel for new up interfaces */
int	externalinterfaces;	/* # of remote and local interfaces */
int	save_space;		/* if 1 and not supplier install only default
				 * routes to the routers. */
struct	timeval now;		/* current idea of time */
struct	timeval lastbcast;	/* last time all/changes broadcast */
struct	timeval lastfullupdate;	/* last time full table broadcast */
struct	timeval nextbcast;	/* time to wait before changes broadcast */
int	needupdate;		/* true if we need update at nextbcast */

char	packet[MAXPACKETSIZE+1];
struct	rip *msg;

char	**argv0;

extern	char *sys_errlist[];
extern	int errno;

struct	in_addr inet_makeaddr();
int	inet_addr();
char	*malloc();
int	exit();

void	rip_input();
int	cleanup();
void	toall();
void	sendpacket();
void	supply();
void	gwkludge();
void	addrouteforif();
void	timer();
void	hup();
void	dynamic_update();
void	timevaladd();
void	timevalsub();

#ifdef SYSV
int	bcmp();
int	bcopy();
int	bzero();
char 	*strcpy();
char 	*strncpy();
int	strcmp();
int	strlen();
#endif /* SYSV */

#ifndef LIBNSL_BUG_FIXED
#define inet_netof routed_inet_netof
#endif /* LIBNSL_BUG_FIXED */

