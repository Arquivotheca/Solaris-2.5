/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

#ifndef _RPC_SVCAUTH_H
#define	_RPC_SVCAUTH_H

#pragma ident	"@(#)svc_auth.h	1.10	93/11/12 SMI"

/*	 svc_auth.h 1.10 88/10/25 SMI	*/

/*
 * svc_auth.h, Service side of rpc authentication.
 */

#include <rpc/svc.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Server side authenticator
 */
#ifdef __STDC__
extern enum auth_stat __authenticate(struct svc_req *, struct rpc_msg *);
#else
extern enum auth_stat __authenticate();
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _RPC_SVCAUTH_H */
