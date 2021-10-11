/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)onc_fn_nisplus.cc	1.6	94/11/24 SMI"


#include "FNSP_Context.hh"
#include "FNSP_NullContext.hh"
#include "FNSP_OrgContext.hh"
#include "FNSP_ENSContext.hh"
#include "FNSP_FlatContext.hh"
#include "FNSP_WeakSlashContext.hh"
#include "FNSP_DotContext.hh"
#include "FNSP_HostnameContext.hh"
#include "FNSP_UsernameContext.hh"

// Generate a FNSP_Context given a reference

extern "C"
FNSP_Context*
FNSP_Context_from_ref(const FN_ref& ref, FN_status& stat)
{
	return (FNSP_Context::from_ref(ref, stat));
}

// Generate an FN_ctx given a an address with type "onc_fn_nisplus"

extern "C"
FN_ctx_svc_t *
onc_fn_nisplus__fn_ctx_svc_handle_from_ref_addr(const FN_ref_addr_t *caddr,
    const FN_ref_t *cref, FN_status_t *cstat)
{
	const FN_ref_addr *addr = (const FN_ref_addr *)(caddr);
	const FN_ref *ref = (const FN_ref *)(cref);
	FN_status *stat = (FN_status *)(cstat);

	unsigned context_type = FNSP_address_context_type(*addr);
	FN_ctx_svc *answer = 0;

	switch (context_type) {
	case FNSP_organization_context:
		answer = FNSP_OrgContext::from_address(*addr, *ref, *stat);
		break;

	case FNSP_enterprise_context:
		answer = FNSP_ENSContext::from_address(*addr, *ref, *stat);
		break;

	case FNSP_nsid_context:
	case FNSP_user_context:
	case FNSP_host_context:
		answer = FNSP_FlatContext::from_address(*addr, *ref, *stat);
		break;

	case FNSP_null_context:
		answer = FNSP_NullContext::from_ref(*ref, *stat);
		break;

	case FNSP_service_context:
	case FNSP_generic_context:
		answer = FNSP_WeakSlashContext::from_address(*addr,
		    *ref, *stat);
		break;

	case FNSP_site_context:
		/* hierarchical dot separated names */
		answer = FNSP_DotContext::from_address(*addr, *ref, *stat);
		break;

	case FNSP_hostname_context:
		answer = FNSP_HostnameContext::from_address(*addr, *ref, *stat);
		break;

	case FNSP_username_context:
		answer = FNSP_UsernameContext::from_address(*addr, *ref, *stat);
		break;

	default:
		stat->set(FN_E_MALFORMED_REFERENCE);
		break;
	}

	return ((FN_ctx_svc_t *)answer);
}

extern "C"
FN_ctx_t *
onc_fn_nisplus__fn_ctx_handle_from_ref_addr(const FN_ref_addr_t *addr,
    const FN_ref_t *ref, FN_status_t *stat)
{
	FN_ctx *sctx = (FN_ctx_svc *)
		onc_fn_nisplus__fn_ctx_svc_handle_from_ref_addr(addr,
		    ref, stat);

	return ((FN_ctx_t *)sctx);
}
