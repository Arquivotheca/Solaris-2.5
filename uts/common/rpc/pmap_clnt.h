/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

#ifndef _RPC_PMAPCLNT_H
#define	_RPC_PMAPCLNT_H

#pragma ident	"@(#)pmap_clnt.h	1.11	93/11/12 SMI"

/*	@(#)pmap_clnt.h 1.14 88/10/25 SMI	*/

/*
 * pmap_clnt.h
 * Supplies C routines to get to portmap services.
 */

#include <netinet/in.h>

#ifdef __STDC__
#include <rpc/clnt.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Usage:
 *	success = pmap_set(program, version, protocol, port);
 *	success = pmap_unset(program, version);
 *	port = pmap_getport(address, program, version, protocol);
 *	head = pmap_getmaps(address);
 *	clnt_stat = pmap_rmtcall(address, program, version, procedure,
 *		xdrargs, argsp, xdrres, resp, tout, port_ptr)
 *		(works for udp only.)
 * 	clnt_stat = clnt_broadcast(program, version, procedure,
 *		xdrargs, argsp,	xdrres, resp, eachresult)
 *		(like pmap_rmtcall, except the call is broadcasted to all
 *		locally connected nets.  For each valid response received,
 *		the procedure eachresult is called.  Its form is:
 *	done = eachresult(resp, raddr)
 *		bool_t done;
 *		caddr_t resp;
 *		struct sockaddr_in raddr;
 *		where resp points to the results of the call and raddr is the
 *		address if the responder to the broadcast.
 */

#ifdef __STDC__
extern bool_t pmap_set(unsigned long, unsigned long, int, unsigned short port);
extern bool_t pmap_unset(u_long, u_long);
extern struct pmaplist *pmap_getmaps(struct sockaddr_in *);
extern u_short pmap_getport(struct sockaddr_in *, u_long, u_long, u_int);
#ifndef _KERNEL
enum clnt_stat clnt_broadcast(u_long, u_long, u_long, xdrproc_t, char *,
				xdrproc_t, char *, resultproc_t);
enum clnt_stat pmap_rmtcall(struct sockaddr_in *, u_long, u_long, u_long,
				xdrproc_t, caddr_t, xdrproc_t, caddr_t,
				struct timeval, u_long *);
#endif
#else
extern bool_t pmap_set();
extern bool_t pmap_unset();
extern struct pmaplist *pmap_getmaps();
extern u_short pmap_getport();
#ifndef _KERNEL
enum clnt_stat clnt_broadcast();
enum clnt_stat pmap_rmtcall();
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _RPC_PMAPCLNT_H */
