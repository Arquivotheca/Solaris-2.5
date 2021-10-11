/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/*
 * rpc_com.h, Common definitions for both the server and client side.
 * All for the topmost layer of rpc
 *
 */

#ifndef _RPC_RPCCOM_H
#define	_RPC_RPCCOM_H

#pragma ident	"@(#)rpc_com.h	1.18	95/01/09 SMI"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * File descriptor to be used on xxx_create calls to get default descriptor
 */
#define	RPC_ANYSOCK	-1
#define	RPC_ANYFD	RPC_ANYSOCK
/*
 * The max size of the transport, if the size cannot be determined
 * by other means.
 */
#define	RPC_MAXDATASIZE 9000
#define	RPC_MAXADDRSIZE 1024

/*
 * The type of user of the STREAMS module rpcmod.
 */
#define	RPC_CLIENT	1
#define	RPC_SERVER	2
#define	RPC_TEST	3

#ifdef __STDC__
extern u_int __rpc_get_t_size(int, long);
extern u_int __rpc_get_a_size(long);
extern int __rpc_dtbsize(void);
extern struct netconfig *__rpcfd_to_nconf(int, int);
extern int __rpc_matchserv(int, unsigned long);
extern  int  __rpc_get_default_domain(char **);
#else
extern u_int __rpc_get_t_size();
extern u_int __rpc_get_a_size();
extern int __rpc_dtbsize();
extern struct netconfig *__rpcfd_to_nconf();
extern int __rpc_matchserv();
extern  int __rpc_get_default_domain();
#endif

#ifndef _KERNEL

#ifdef __STDC__
bool_t rpc_control(int, void *);
#else
bool_t rpc_control();
#endif

/*
 * rpc_control commands
 */
#define	RPC_SVC_MTMODE_SET	1	/* set MT mode */
#define	RPC_SVC_MTMODE_GET	2	/* get MT mode */
#define	RPC_SVC_THRMAX_SET	3	/* set maximum number of threads */
#define	RPC_SVC_THRMAX_GET	4	/* get maximum number of threads */
#define	RPC_SVC_THRTOTAL_GET	5	/* get total number of threads */
#define	RPC_SVC_THRCREATES_GET	6	/* get total threads created */
#define	RPC_SVC_THRERRORS_GET	7	/* get total thread create errors */
#define	__RPC_CLNT_MINFD_SET	8	/* set min fd used for a clnt handle */
#define	__RPC_CLNT_MINFD_GET	9	/* get min fd used for a clnt handle */

#endif /* !_KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* _RPC_RPCCOM_H */
