/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/*
 * rpc.h, Just includes the billions of rpc header files necessary to
 * do remote procedure calling.
 *
 */

#ifndef _RPC_RPC_H
#define	_RPC_RPC_H

#pragma ident	"@(#)rpc.h	1.11	93/02/04 SMI"

/*	rpc.h 1.13 88/12/17 SMI	*/

#include <rpc/types.h>		/* some typedefs */

#ifndef _KERNEL
#include <tiuser.h>
#include <fcntl.h>
#include <memory.h>
#else
#include <sys/tiuser.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <sys/t_kuser.h>
#endif

#include <rpc/xdr.h>		/* generic (de)serializer */
#include <rpc/auth.h>		/* generic authenticator (client side) */
#include <rpc/clnt.h>		/* generic client side rpc */

#include <rpc/rpc_msg.h>	/* protocol for rpc messages */
#include <rpc/auth_sys.h>	/* protocol for unix style cred */
#include <rpc/auth_des.h>	/* protocol for des style cred */
#include <rpc/auth_kerb.h>	/* protocol for kerberos style cred */

#include <rpc/svc.h>		/* service manager and multiplexer */
#include <rpc/svc_auth.h>	/* service side authenticator */

#ifndef _KERNEL
#include <rpc/rpcb_clnt.h>	/* rpcbind interface functions */
#endif

#endif	/* !_RPC_RPC_H */
