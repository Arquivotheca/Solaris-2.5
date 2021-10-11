/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_GlobalContext.cc	1.5 94/11/20 SMI"

#include "FNSP_GlobalContext.hh"
#include <string.h>  /* for strncmp */

// This file contains the implementation of the 'global' context --
// the context that determines whether the input name should be directed
// towards X.500 or DNS.

// It is implemented as a strongly separated context.
// In other words, the first component of the composite name is examined to
// make the choice between global naming services.

// All operations (c_* and c_*_nns) call decide_and_continue().
// This calls get_naming_service_ref(),
// which returns the reference of root context of the
// appropriate naming service.
// and set status to 'continue' so that resolution
// would continue in the appropriate context.

// The heuristics used to determine the naming service are:
// no (unescaped) '=' sign -> DNS, otherwise, X.500 (simple, eh :-))
//

extern FN_ref *FNSP_dns_reference();
extern FN_ref *FNSP_x500_reference();

enum {
	FNSP_EMPTY_NAME,
	FNSP_DNS_NAME,
	FNSP_X500_NAME,
	FNSP_INVALID_NAME
	};

static const FN_string x500_indicator_str((unsigned char *)"=");
static const FN_string x500_escape_str((unsigned char *)"\\");

static unsigned int
determine_ns(const FN_string &name)
{
	unsigned indicator_location;
	if (name.is_empty())
		return (FNSP_EMPTY_NAME);
	else if ((indicator_location =
	    name.next_substring(x500_indicator_str)) ==
	    FN_STRING_INDEX_NONE)
		return (FNSP_DNS_NAME);
	else {
		// found indicator, see if it had been escaped
		if (indicator_location == 0)
			return (FNSP_INVALID_NAME);
		else if (name.compare_substring(indicator_location-1,
		    indicator_location-1,
		    x500_escape_str) != 0)
			// not escaped
			return (FNSP_X500_NAME);
	}
	// found escape
	const FN_string *subname = new FN_string(name, indicator_location+1,
	    FN_STRING_INDEX_NONE);
	if (subname == 0 || subname->is_empty())
		return (FNSP_DNS_NAME);
	else
		// check subname for indicator
		return (determine_ns(*subname));
}


FNSP_GlobalContext::decide_and_continue(const FN_string &name,
    FN_status_csvc& cstat,
    unsigned int status_for_empty_name)
{
	FN_ref *ref = 0;
	switch (determine_ns(name)) {
	case FNSP_DNS_NAME:
		ref = FNSP_dns_reference();
		cstat.set_error(FN_E_SPI_CONTINUE, *ref, name);
		break;
	case FNSP_X500_NAME:
		ref = FNSP_x500_reference();
		cstat.set_error(FN_E_SPI_CONTINUE, *ref, name);
		break;
	case FNSP_EMPTY_NAME:
		cstat.set_error(status_for_empty_name,
		    *my_reference, name);
		break;
	case FNSP_INVALID_NAME:
		cstat.set_error(FN_E_ILLEGAL_NAME,
		    *my_reference, name);
		break;
	}
	if (ref)
		delete ref;
	return (0);
}

FNSP_GlobalContext::FNSP_GlobalContext(const FN_ref &ref)
{
	// Given reference is my_reference
	my_reference = new FN_ref(ref);
}

FNSP_GlobalContext::~FNSP_GlobalContext()
{
	delete my_reference;
}

FNSP_GlobalContext*
FNSP_GlobalContext::from_ref(const FN_ref_addr &addr,
    const FN_ref &ref,
    FN_status &stat)
{
	// sanity check
	extern char *FNSP_global_addr_contents;
	extern size_t FNSP_global_addr_contents_size;

	if (FNSP_global_addr_contents_size != addr.length() ||
	    (strncmp((const char *)addr.data(),
	    FNSP_global_addr_contents, addr.length()) != 0)) {
		stat.set(FN_E_MALFORMED_REFERENCE);
		return (0);
	}

	FNSP_GlobalContext *answer = new FNSP_GlobalContext(ref);

	if (answer && answer->my_reference)
		stat.set_success();
	else {
		if (answer) {
			delete answer;
			answer = 0;
		}
		stat.set(FN_E_INSUFFICIENT_RESOURCES);
	}
	return (answer);
}


FN_ref *
FNSP_GlobalContext::get_ref(FN_status &stat) const
{
	stat.set_success();

	return (new FN_ref(*my_reference));
}

FN_ref *
FNSP_GlobalContext::c_lookup(const FN_string &name,
    unsigned int /* lookup_flags */,
    FN_status_csvc& cstat)
{
	if (name.is_empty()) {
		cstat.set_success();
		return (new FN_ref(*my_reference));
	} else {
		decide_and_continue(name, cstat);
		return (0);
	}
}

FN_namelist*
FNSP_GlobalContext::c_list_names(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

FN_bindinglist*
FNSP_GlobalContext::c_list_bindings(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

int
FNSP_GlobalContext::c_bind(const FN_string &name, const FN_ref &,
    unsigned, FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

int
FNSP_GlobalContext::c_unbind(const FN_string &name,
    FN_status_csvc& cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

int
FNSP_GlobalContext::c_rename(const FN_string &name,
    const FN_composite_name &,
    unsigned, FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

FN_ref *
FNSP_GlobalContext::c_create_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

int
FNSP_GlobalContext::c_destroy_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

FN_attrset*
FNSP_GlobalContext::c_get_syntax_attrs(const FN_string &name,
    FN_status_csvc &cstat)
{
	// %%% can something sensible be returned here for empty name case?
	decide_and_continue(name, cstat);
	return (0);
}

FN_attribute*
FNSP_GlobalContext::c_attr_get(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

int
FNSP_GlobalContext::c_attr_modify(const FN_string &name,
    unsigned int,
    const FN_attribute&,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

FN_valuelist*
FNSP_GlobalContext::c_attr_get_values(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

FN_attrset*
FNSP_GlobalContext::c_attr_get_ids(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

FN_multigetlist*
FNSP_GlobalContext::c_attr_multi_get(const FN_string &name,
    const FN_attrset *,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

int
FNSP_GlobalContext::c_attr_multi_modify(const FN_string &name,
    const FN_attrmodlist&,
    FN_attrmodlist**,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}


FN_ref *
FNSP_GlobalContext::c_lookup_nns(const FN_string &name,
    unsigned int /* lookup_flags */,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat, FN_E_NAME_NOT_FOUND);
	return (0);
}

FN_namelist*
FNSP_GlobalContext::c_list_names_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat, FN_E_NAME_NOT_FOUND);
	return (0);
}

FN_bindinglist*
FNSP_GlobalContext::c_list_bindings_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat, FN_E_NAME_NOT_FOUND);
	return (0);
}

// This function should never be called.
int
FNSP_GlobalContext::c_bind_nns(const FN_string &name,
    const FN_ref &,
    unsigned, FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

int
FNSP_GlobalContext::c_unbind_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

// This function should never be called.
int
FNSP_GlobalContext::c_rename_nns(const FN_string &name,
    const FN_composite_name &,
    unsigned,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat, FN_E_NAME_NOT_FOUND);
	return (0);
}


FN_ref *
FNSP_GlobalContext::c_create_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}
int
FNSP_GlobalContext::c_destroy_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat);
	return (0);
}

FN_attrset*
FNSP_GlobalContext::c_get_syntax_attrs_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat, FN_E_NAME_NOT_FOUND);
	return (0);
}

FN_attribute*
FNSP_GlobalContext::c_attr_get_nns(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat, FN_E_NAME_NOT_FOUND);
	return (0);
}

int
FNSP_GlobalContext::c_attr_modify_nns(const FN_string &name,
    unsigned int,
    const FN_attribute&,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat, FN_E_NAME_NOT_FOUND);
	return (0);
}

FN_valuelist*
FNSP_GlobalContext::c_attr_get_values_nns(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat, FN_E_NAME_NOT_FOUND);
	return (0);
}

FN_attrset*
FNSP_GlobalContext::c_attr_get_ids_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat, FN_E_NAME_NOT_FOUND);
	return (0);
}

FN_multigetlist*
FNSP_GlobalContext::c_attr_multi_get_nns(const FN_string &name,
    const FN_attrset *,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat, FN_E_NAME_NOT_FOUND);
	return (0);
}

int
FNSP_GlobalContext::c_attr_multi_modify_nns(const FN_string &name,
    const FN_attrmodlist&,
    FN_attrmodlist**,
    FN_status_csvc &cstat)
{
	decide_and_continue(name, cstat, FN_E_NAME_NOT_FOUND);
	return (0);
}


extern "C"
{
FN_ctx_svc_t*
initial__fn_ctx_svc_handle_from_ref_addr(const FN_ref_addr_t* a,
    const FN_ref_t* r,
    FN_status_t *s)
{
	FN_ref *rr = (FN_ref *)r;
	FN_status *ss = (FN_status *)s;
	FN_ref_addr *aa = (FN_ref_addr *)a;

	FN_ctx_svc* newthing = FNSP_GlobalContext::from_ref(*aa, *rr, *ss);
	return ((FN_ctx_svc_t*)newthing);
}

FN_ctx_t*
initial__fn_ctx_handle_from_ref_addr(const FN_ref_addr_t* a,
    const FN_ref_t* r,
    FN_status_t *s)
{
	FN_ctx_svc_t* newthing =
	    initial__fn_ctx_svc_handle_from_ref_addr(a, r, s);

	// FN_ctx_t* ctxobj = fn_ctx_handle_from_fn_ctx_svc(newthing);

	FN_ctx* ctxobj = (FN_ctx_svc*)newthing;

	return ((FN_ctx_t*)ctxobj);
}
}
