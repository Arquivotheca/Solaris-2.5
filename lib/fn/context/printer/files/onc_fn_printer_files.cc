/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)onc_fn_printer_files.cc	1.3	94/11/24 SMI"

#include "../FNSP_PrinternameContext.hh"

// Generate an FNS Context given an address with type "onc_fn_files"

extern "C" {
FN_ctx_svc_t *
onc_fn_printer_files__fn_ctx_svc_handle_from_ref_addr(
    const FN_ref_addr_t *caddr, const FN_ref_t *cref,
    FN_status_t *cstat)
{
	const FN_ref *ref = (const FN_ref *)(cref);
	const FN_ref_addr *addr = (const FN_ref_addr *) caddr;
	FN_status *stat = (FN_status *)(cstat);

	FN_ctx_svc *answer  =
	    FNSP_PrinternameContext::from_address(*addr, *ref, *stat);

	return ((FN_ctx_svc_t *)answer);
}

FN_ctx_t *
onc_fn_printer_files__fn_ctx_handle_from_ref_addr(
    const FN_ref_addr_t *addr, const FN_ref_t *ref,
    FN_status_t *stat)
{
	FN_ctx *sctx = (FN_ctx_svc *)
	    onc_fn_printer_files__fn_ctx_svc_handle_from_ref_addr(addr,
	    ref, stat);
	return ((FN_ctx_t *)sctx);
}

} // For extern C stuff
