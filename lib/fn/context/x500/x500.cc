/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)x500.cc	1.1	94/12/05 SMI"

#include "x500context.hh"


/*
 * this is the entry point into the CX500Context module as a shared object
 */

extern "C"
FN_ctx_svc_t *
x500__fn_ctx_svc_handle_from_ref_addr(
	const FN_ref_addr_t	*caddr,
	const FN_ref_t		*cref,
	FN_status_t		*cs)
{
	int			err = 0;
	FN_status		*s = (FN_status *)cs;
	const FN_ref		*ref = (const FN_ref *)cref;
	const FN_ref_addr	*addr = (const FN_ref_addr *)caddr;
	FN_ctx_svc		*newthing = new CX500Context(*addr, *ref, err);

	if (err) {
		s->set_code(FN_E_COMMUNICATION_FAILURE);
		delete newthing;
		newthing = 0;

	} else if (! newthing)
		s->set_code(FN_E_INSUFFICIENT_RESOURCES);
	else
		s->set_success();

	x500_debug("x500__fn_ctx_svc_handle_from_ref_addr(): %s\n",
	    err ? "error" : "ok");

	return ((FN_ctx_svc_t *)newthing);
}


extern "C"
FN_ctx_t *
x500__fn_ctx_handle_from_ref_addr(
	const FN_ref_addr_t	*addr,
	const FN_ref_t		*ref,
	FN_status_t		*s)
{
	FN_ctx_svc_t	*newthing =
			x500__fn_ctx_svc_handle_from_ref_addr(addr, ref, s);
	FN_ctx		*ctxobj = (FN_ctx_svc *)newthing;

	x500_debug("x500__fn_ctx_handle_from_ref_addr()\n");

	return ((FN_ctx_t *)ctxobj);
}
