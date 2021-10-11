/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)onc_fn_printer_nisplus.cc	1.2 94/11/08 SMI"

#include <xfn/fn_printer_p.hh>
#include "FNSP_nisplusPrinternameContext.hh"
#include "FNSP_nisplusPrinterObject.hh"
#include "fnsp_printer_internal.hh"

// Generate a FNSP_printer_Context given a reference

extern "C" {
FN_ctx_svc_t*
onc_fn_printer_nisplus__fn_ctx_svc_handle_from_ref_addr(
    const FN_ref_addr_t *caddr, const FN_ref_t* cref,
    FN_status_t* cstat)
{
	const FN_ref_addr *addr = (FN_ref_addr *) caddr;
	const FN_ref *ref = (FN_ref*)(cref);
	FN_status *stat = (FN_status*)(cstat);

	FN_ctx_svc *answer = 0;

	const FN_identifier *ref_type = ref->type();
	if ((*ref_type) == FNSP_printername_reftype_name())
		answer = FNSP_nisplusPrinternameContext::from_address(
		    *addr, *ref, *stat);
	else if ((*ref_type) == FNSP_printer_reftype_name())
		answer = FNSP_nisplusPrinterObject::from_address(
		    *addr, *ref, *stat);
	else
		stat->set(FN_E_MALFORMED_REFERENCE);

	return ((FN_ctx_svc_t*)answer);
}

FN_ctx_t*
onc_fn_printer_nisplus__fn_ctx_handle_from_ref_addr(
    const FN_ref_addr_t *addr, const FN_ref_t* ref,
    FN_status_t* stat)
{
	FN_ctx* sctx = (FN_ctx_svc*)
	    onc_fn_printer_nisplus__fn_ctx_svc_handle_from_ref_addr(addr,
	    ref, stat);
	return ((FN_ctx_t*)sctx);
}

} // For extern C stuff
