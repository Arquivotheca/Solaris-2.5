/*
 * Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)glue.cc	1.5 94/10/20 SMI"

#include <stdio.h>

#include "cx.hh"


/*
 * this is the entry point into the DNScontext module as a shared object
 *
 * NOTE:
 *	1. declared extern "C" so we can (easily) predict the function name
 *	   in the .so.  the caller binds to this name using dlsym().
 */

extern "C"
FN_ctx_svc_t *
inet_domain__fn_ctx_svc_handle_from_ref_addr(
	const FN_ref_addr_t *caddr,
	const FN_ref_t * /*cr*/,
	FN_status_t *cs)
{
	FN_status	*s = (FN_status *)cs;
	FN_ref_addr	*ap = (FN_ref_addr *)caddr;

	if (DNS_ctx::get_trace_level())
		fprintf(stderr,
		    "inet_domain__fn_ctx_svc_handle_from_address: call\n");

	s->set_success();

	FN_ctx_svc	*newthing = new DNS_ctx(*ap);

	return ((FN_ctx_svc_t *)newthing);
}


extern "C"
FN_ctx_t *
inet_domain__fn_ctx_handle_from_ref_addr(
	const FN_ref_addr_t *addr,
	const FN_ref_t *r,
	FN_status_t *s)
{
	if (DNS_ctx::get_trace_level())
		fprintf(stderr,
		    "inet_domain__fn_ctx_handle_from_address: call\n");

	FN_ctx_svc_t *newthing =
	    inet_domain__fn_ctx_svc_handle_from_ref_addr(addr, r, s);

	FN_ctx *ctxobj = (FN_ctx_svc *)newthing;

	return ((FN_ctx_t *)ctxobj);
}
