/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

#ifndef _RPC_PMAPRMT_H
#define	_RPC_PMAPRMT_H

#pragma ident	"@(#)pmap_rmt.h	1.10	93/11/12 SMI"

/*	@(#)pmap_rmt.h 1.8 89/03/21 SMI	*/

#ifndef _KERNEL

#include <rpc/pmap_prot.h>

#else	/* ndef _KERNEL */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Structures and XDR routines for parameters to and replies from
 * the portmapper remote-call-service.
 */

struct rmtcallargs {
	u_long prog, vers, proc, arglen;
	caddr_t args_ptr;
	xdrproc_t xdr_args;
};

#ifdef __STDC__
bool_t xdr_rmtcall_args(XDR *, struct rmtcallargs *);
#else
bool_t xdr_rmtcall_args();
#endif

struct rmtcallres {
	u_long *port_ptr;
	u_long resultslen;
	caddr_t results_ptr;
	xdrproc_t xdr_results;
};
typedef struct rmtcallres rmtcallres;
#ifdef __STDC__
bool_t xdr_rmtcall_args(XDR *, struct rmtcallargs *);
#else
bool_t xdr_rmtcall_args();
#endif

#ifdef __cplusplus
}
#endif

#endif	/* ndef _KERNEL */

#endif	/* _RPC_PMAPRMT_H */
