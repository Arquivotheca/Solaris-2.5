/*
 * Copyright (c) 1984 - 1991 by Sun Microsystems, Inc.
 */

/*
 * clnt.h - Client side remote procedure call interface.
 */

#ifndef _RPC_CLNT_SOC_H
#define	_RPC_CLNT_SOC_H

#pragma ident	"@(#)clnt_soc.h	1.11	93/11/12 SMI"

/* derived from clnt_soc.h 1.3 88/12/17 SMI 	*/

/*
 * All the following declarations are only for backward compatibility
 * with SUNOS 4.0.
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <rpc/xdr.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	UDPMSGSIZE	8800	/* rpc imposed limit on udp msg size */

/*
 * callrpc(host, prognum, versnum, procnum, inproc, in, outproc, out)
 *	char *host;
 *	u_long prognum, versnum, procnum;
 *	xdrproc_t inproc, outproc;
 *	char *in, *out;
 */
#ifdef __STDC__
extern int callrpc(char *, u_long, u_long, u_long, xdrproc_t, char *,
		    xdrproc_t, char *);
#else
extern int callrpc();
#endif


/*
 * TCP based rpc
 * CLIENT *
 * clnttcp_create(raddr, prog, vers, fdp, sendsz, recvsz)
 *	struct sockaddr_in *raddr;
 *	u_long prog;
 *	u_long version;
 *	int *fdp;
 *	u_int sendsz;
 *	u_int recvsz;
 */
#ifdef __STDC__
extern CLIENT *clnttcp_create(struct sockaddr_in *, u_long, u_long,
				int *, u_int, u_int);
#else
extern CLIENT *clnttcp_create();
#endif


/*
 * UDP based rpc.
 * CLIENT *
 * clntudp_create(raddr, program, version, wait, fdp)
 *	struct sockaddr_in *raddr;
 *	u_long program;
 *	u_long version;
 *	struct timeval wait;
 *	int *fdp;
 *
 * Same as above, but you specify max packet sizes.
 * CLIENT *
 * clntudp_bufcreate(raddr, program, version, wait, fdp, sendsz, recvsz)
 *	struct sockaddr_in *raddr;
 *	u_long program;
 *	u_long version;
 *	struct timeval wait;
 *	int *fdp;
 *	u_int sendsz;
 *	u_int recvsz;
 *
 */
#ifdef __STDC__
extern CLIENT *clntudp_create(struct sockaddr_in *, u_long, u_long,
				struct timeval, int *);
extern CLIENT *clntudp_bufcreate(struct sockaddr_in *, u_long, u_long,
				struct timeval, int *, u_int, u_int);
#else
extern CLIENT *clntudp_create();
extern CLIENT *clntudp_bufcreate();
#endif

/*
 * Memory based rpc (for speed check and testing)
 * CLIENT *
 * clntraw_create(prog, vers)
 *	u_long prog;
 *	u_long vers;
 */
#ifdef __STDC__
extern CLIENT *clntraw_create(u_long, u_long);
#else
extern CLIENT *clntraw_create();
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _RPC_CLNT_SOC_H */
