/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)onc_fn_files.cc	1.2 94/11/08 SMI"

#include "FNSP_OrgContext.hh"
#include "FNSP_FlatContext.hh"

#include <xfn/fn_p.hh>
#include <xfn/fn_printer_p.hh>

static const FN_identifier
FNSP_files_address_type((unsigned char *) "onc_fn_files");

static const FN_identifier
FNSP_printer_files_address_type((unsigned char *) "onc_fn_printer_files");

static const FN_identifier
FNSP_printername_reftype((unsigned char *) "onc_fn_printername");

static const FN_string
FNSP_printername_files_address_contents((unsigned char *) "printers");

FN_ref *
FNSP_OrgContext::make_nsid_ref()
{
	FN_string* orgname =
		FNSP_reference_to_internal_name(*my_reference);

	if (orgname) {
		FN_ref *answer = FNSP_reference(FNSP_files_address_type,
						*orgname,
						FNSP_nsid_context);
		delete orgname;
		return (answer);
	}
	return (0);
}

FN_ref *
FNSP_OrgContext::make_service_ref()
{
	FN_string* orgname =
		FNSP_reference_to_internal_name(*my_reference);

	if (orgname) {
		FN_ref *answer = FNSP_reference(FNSP_files_address_type,
						*orgname,
						FNSP_service_context);
		delete orgname;
		return (answer);
	}
	return (0);
}

FN_ref *
FNSP_FlatContext::make_service_ref()
{
	return (FNSP_reference(FNSP_files_address_type,
	    my_address->get_internal_name(), FNSP_service_context));
}

FN_ref *
FNSP_FlatContext::make_printername_ref()
{
	return (FNSP_reference(FNSP_printer_files_address_type,
	    FNSP_printername_reftype,
	    FNSP_printername_files_address_contents,
	    FNSP_printername_context,
	    FNSP_normal_repr));
}


// Generate an FN_ctx_svc_t given an address with type "onc_fn_files"
extern "C"
FN_ctx_svc_t*
onc_fn_files__fn_ctx_svc_handle_from_ref_addr(const FN_ref_addr_t* caddr,
		    const FN_ref_t* cref,
		    FN_status_t* cstat)
{
	const FN_ref_addr *addr = (FN_ref_addr*)(caddr);
	const FN_ref *ref = (FN_ref*)(cref);
	FN_status *stat = (FN_status*)(cstat);
	unsigned context_type = FNSP_address_context_type(*addr);
	FN_ctx_svc *answer = 0;

	switch (context_type) {
	case FNSP_organization_context:
		answer = FNSP_OrgContext::from_address(*addr, *ref, *stat);
		break;

	case FNSP_nsid_context:
	case FNSP_service_context:
		answer = FNSP_FlatContext::from_address(*addr, *ref, *stat);
		break;

	case FNSP_site_context:
	case FNSP_enterprise_context:
	case FNSP_username_context:
	case FNSP_hostname_context:
	case FNSP_user_context:
	case FNSP_host_context:
	case FNSP_null_context:
	case FNSP_generic_context:
		stat->set(FN_E_OPERATION_NOT_SUPPORTED);
		break;

	default:
		stat->set(FN_E_MALFORMED_REFERENCE);
		break;
	}

	return ((FN_ctx_svc_t*)answer);
}

extern "C"
FN_ctx_t*
onc_fn_files__fn_ctx_handle_from_ref_addr(const FN_ref_addr_t* addr,
		    const FN_ref_t* ref,
		    FN_status_t* stat)
{
	FN_ctx* sctx = (FN_ctx_svc*)
	    onc_fn_files__fn_ctx_svc_handle_from_ref_addr(addr, ref, stat);
	return ((FN_ctx_t*)sctx);
}
