/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_Context.cc	1.7 94/11/24 SMI"

#include "FNSP_Context.hh"
#include "FNSP_NullContext.hh"
#include "FNSP_OrgContext.hh"
#include "FNSP_ENSContext.hh"
#include "FNSP_FlatContext.hh"
#include "FNSP_WeakSlashContext.hh"
#include "FNSP_DotContext.hh"
#include "FNSP_HostnameContext.hh"
#include "FNSP_UsernameContext.hh"
#include "fnsp_internal.hh"

FNSP_Context *
FNSP_Context::from_ref(const FN_ref& ref,
    FN_status& stat)
{
	const FN_ref_addr *addr = 0;
	void *ip;

	for (addr = ref.first(ip); addr; addr = ref.next(ip)) {
		if (FNSP_nisplus_address_p(*addr))
			break;
	}

	if (addr == 0) {
		stat.set(FN_E_MALFORMED_REFERENCE);
		return (0);
	}

	unsigned context_type = FNSP_address_context_type(*addr);
	FNSP_Context *answer = 0;

	switch (context_type) {
	case FNSP_nsid_context:
	case FNSP_user_context:
	case FNSP_host_context:
		answer = FNSP_FlatContext::from_address(*addr, ref, stat);
		break;

	case FNSP_organization_context:
		answer = FNSP_OrgContext::from_address(*addr, ref, stat);
		break;

	case FNSP_enterprise_context:
		answer = FNSP_ENSContext::from_address(*addr, ref, stat);
		break;

	case FNSP_null_context:
		answer = FNSP_NullContext::from_ref(ref, stat);
		break;

	case FNSP_service_context:
	case FNSP_generic_context:
		answer = FNSP_WeakSlashContext::from_address(*addr, ref, stat);
		break;

	case FNSP_site_context:
		/* hierarchical dot separated names */
		answer = FNSP_DotContext::from_address(*addr, ref, stat);
		break;

	case FNSP_hostname_context:
		answer = FNSP_HostnameContext::from_address(*addr, ref, stat);
		break;

	case FNSP_username_context:
		answer = FNSP_UsernameContext::from_address(*addr, ref, stat);
		break;

	default:
		stat.set(FN_E_MALFORMED_REFERENCE);
		break;
	}

	return (answer);
}


FN_ref *
FNSP_Context::create_fnsp_subcontext(const FN_composite_name &n,
    unsigned context_type,
    FN_status& s,
    unsigned representation_type,
    const FN_identifier *ref_type)
{
	FN_ref *ret = 0;
	FN_composite_name *rn;
	FN_status s1;
	void *p;
	const FN_string *fn = n.first(p);
	if (fn == 0) {
		// s.set_error_context(FN_E_ILLEGAL_NAME, *this, n);
		FN_status s2;
		FN_ref *ref = get_ref(s2);
		s.set(FN_E_ILLEGAL_NAME, ref, 0, &n);
		if (ref)
			delete ref;
		return (0);
	}

	rn = n.suffix(p);
	if (rn) {
		if (rn->is_empty()) {
			// remaining component is "" so create NNS context
			ret = n_create_subcontext_nns(*fn,
			    context_type,
			    representation_type,
			    ref_type,
			    s1);
			if (!s1.is_success()) {
				const FN_composite_name *srn;
				if (srn = s1.remaining_name()) {
					// construct complete remaining name
					rn->prepend_name(*srn);
				}
				s.set(s1.code(), s1.resolved_ref(), 0, rn);
			} else
				s.set_success();
			delete rn;
		} else {
			// more components: Not allowed.
			delete rn;
			//  s.set_error_context(FN_E_ILLEGAL_NAME, *this, n);
			FN_status s2;
			FN_ref *ref = get_ref(s2);
			s.set(FN_E_ILLEGAL_NAME, ref, 0, &n);
			if (ref)
				delete ref;
			return (0);
		}
	} else {
		// only one component
		ret = n_create_subcontext(*fn,
		    context_type,
		    representation_type,
		    ref_type,
		    s);
	}
	return (ret);
}
