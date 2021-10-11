/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/*
 * svc.h, Server-side remote procedure call interface.
 */

#ifndef _RPC_SVC_SOC_H
#define	_RPC_SVC_SOC_H

#pragma ident	"@(#)svc_soc.h	1.11	93/11/12 SMI"
/*	svc_soc.h 1.8 89/05/01 SMI	*/

/*
 * All the following declarations are only for backward compatibility
 * with SUNOS 4.0.
 */

#ifndef _KERNEL
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *  Approved way of getting address of caller
 */
#define	svc_getcaller(x)((struct sockaddr_in *)(x)->xp_rtaddr.buf)
#endif	/* _KERNEL */

/*
 * Service registration
 *
 * svc_register(xprt, prog, vers, dispatch, protocol)
 *	SVCXPRT *xprt;
 *	u_long prog;
 *	u_long vers;
 *	void (*dispatch)();
 *	int protocol;   like TCP or UDP, zero means do not register
 */
#ifdef __STDC__
extern bool_t svc_register(SVCXPRT *, u_long, u_long,
			    void (*)(struct svc_req *, SVCXPRT *), int);
#else
extern bool_t svc_register();
#endif

#ifndef _KERNEL
/*
 * Service un-registration
 *
 * svc_unregister(prog, vers)
 *	u_long prog;
 *	u_long vers;
 */
#ifdef __STDC__
extern void svc_unregister(u_long, u_long);
#else
extern void svc_unregister();
#endif

/*
 * Memory based rpc for testing and timing.
 */
#ifdef __STDC__
extern SVCXPRT *svcraw_create(void);
#else
extern SVCXPRT *svcraw_create();
#endif

/*
 * Udp based rpc. For compatibility reasons
 */
#ifdef __STDC__
extern SVCXPRT *svcudp_create(int);
extern SVCXPRT *svcudp_bufcreate(int, u_int, u_int);
#else
extern SVCXPRT *svcudp_create();
extern SVCXPRT *svcudp_bufcreate();
#endif

/*
 * Tcp based rpc.
 */
#ifdef __STDC__
extern SVCXPRT *svctcp_create(int, u_int, u_int);
extern SVCXPRT *svcfd_create(int, u_int, u_int);
#else
extern SVCXPRT *svctcp_create();
extern SVCXPRT *svcfd_create();
#endif
#endif	/* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* !_RPC_SVC_SOC_H */
