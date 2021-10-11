/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_OrgContext_impl.cc	1.3 94/11/20 SMI"

#include <sys/time.h>

#include "FNSP_OrgContext.hh"
#include "../FNSP_Syntax.hh"
#include "xfn/fn_p.hh"


static const FN_string FNSP_Service_Name((unsigned char *)"service");
static const FN_string FNSP_Service_Name2((unsigned char *)"_service");

FNSP_OrgContext::~FNSP_OrgContext()
{
	if (my_reference) delete my_reference;
}

// check for null pointers to my_reference and my_address in constructors

#ifdef DEBUG
FNSP_OrgContext::FNSP_OrgContext(const FN_identifier *addr_type,
	const FN_string &dirname)
{
	my_reference = FNSP_reference(*addr_type,
				    dirname, FNSP_organization_context);
}

FNSP_OrgContext::FNSP_OrgContext(const FN_ref &from_ref)
{
	my_reference = new FN_ref(from_ref);
}

#endif /* DEBUG */

FNSP_OrgContext::FNSP_OrgContext(const FN_ref_addr & /* from_addr */,
	const FN_ref &from_ref)
{
	my_reference = new FN_ref(from_ref);
}

FNSP_OrgContext*
FNSP_OrgContext::from_address(
    const FN_ref_addr &from_addr,
    const FN_ref &from_ref,
    FN_status &stat)
{
	FNSP_OrgContext *answer = new FNSP_OrgContext(from_addr, from_ref);

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

FN_ref*
FNSP_OrgContext::get_ref(FN_status &stat) const
{
	stat.set_success();
	return (new FN_ref(*my_reference));
}

const FN_ref*
FNSP_OrgContext::resolve(const FN_string &name, FN_status_csvc &cstat)
{
	const FN_ref *answer = 0;

	if (name.is_empty()) {
		// No name was given; resolves to current reference of context
		answer = my_reference;
		cstat.set_success();
	} else {
		// We do not support sub-organizations in files/nis
		cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, name);
	}
	return (answer);
}

FN_ref*
FNSP_OrgContext::c_lookup(const FN_string &name, unsigned int,
	FN_status_csvc &stat)
{
	const FN_ref *answer = resolve(name, stat);
	if (answer)
		return (new FN_ref(*answer));
	else
		return (0);
}

FN_namelist*
FNSP_OrgContext::c_list_names(const FN_string &name, FN_status_csvc &cstat)
{
	if (resolve(name, cstat)) {
		FN_nameset *answer = new FN_nameset;
		if (answer == 0)
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
		else {
			cstat.set_success();
			return (new FN_namelist_svc(answer));
		}
	}
	return (0);
}

FN_bindinglist*
FNSP_OrgContext::c_list_bindings(const FN_string &name, FN_status_csvc &cstat)
{
	if (resolve(name, cstat)) {
		FN_bindingset* answer = new FN_bindingset;
		if (answer == 0)
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
		else {
			cstat.set_success();
			return (new FN_bindinglist_svc(answer));
		}
	}
	return (0);
}

int
FNSP_OrgContext::c_bind(const FN_string &name, const FN_ref&,
	unsigned, FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_unbind(const FN_string &name, FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_rename(const FN_string &name, const FN_composite_name&,
	unsigned, FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_ref*
FNSP_OrgContext::c_create_subcontext(const FN_string &name,
	FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_destroy_subcontext(const FN_string &name,
	FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_attrset*
FNSP_OrgContext::c_get_syntax_attrs(const FN_string &name,
	FN_status_csvc &cstat)
{
	const FN_ref *ref = resolve(name, cstat);

	if (cstat.is_success()) {
		// No suborganizations allowed;
		// Flat context
		FN_attrset* answer =
		    FNSP_Syntax(FNSP_nsid_context)->get_syntax_attrs();
		if (answer)
			return (answer);
		cstat.set(FN_E_INSUFFICIENT_RESOURCES);
	}
	return (0);
}

FN_attribute*
FNSP_OrgContext::c_attr_get(const FN_string &name,
    const FN_identifier&, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_attr_modify(const FN_string &name,
	unsigned int, const FN_attribute&, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_valuelist*
FNSP_OrgContext::c_attr_get_values(const FN_string &name,
	const FN_identifier&, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_attrset*
FNSP_OrgContext::c_attr_get_ids(const FN_string &name, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_multigetlist*
FNSP_OrgContext::c_attr_multi_get(const FN_string &name,
	const FN_attrset *, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_attr_multi_modify(const FN_string &name,
	const FN_attrmodlist&, FN_attrmodlist**, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

// lookup("/")
FN_ref*
FNSP_OrgContext::c_lookup_nns(const FN_string &name,
	unsigned int, /* lookup_flags */ FN_status_csvc &cstat)
{
	if (resolve(name, cstat)) {
		// if we are here, name can only be ""
		// found name; now look for nns
		FN_ref *answer = make_nsid_ref();
		if (answer == 0)
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
		else
			return (answer);
	}
	return (0);
}

// Return names of bindings associated with org//
FN_namelist *
FNSP_OrgContext::c_list_names_nns(const FN_string &name, FN_status_csvc &cstat)
{
	if (resolve(name, cstat)) {
		// if we are here, name can only be ""
		FN_nameset* answer = new FN_nameset;
		if (answer == 0) {
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
			return (0);
		}
		answer->add(FNSP_Service_Name);
		answer->add(FNSP_Service_Name2);
		return (new FN_namelist_svc(answer));
	}

	return (0);
}

// Return bindings, in reality only "service" associated with org//
FN_bindinglist*
FNSP_OrgContext::c_list_bindings_nns(const FN_string &name,
			FN_status_csvc &cstat)
{
	if (resolve(name, cstat)) {
		// if we are here, name can only be ""
		FN_bindingset *bs = new FN_bindingset;

		if (bs == 0) {
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
			return (0);
		}

		FN_ref *ref = make_service_ref();
		if (ref == 0) {
			delete bs;
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
			return (0);
		}

		bs->add(FNSP_Service_Name, *ref);
		bs->add(FNSP_Service_Name2, *ref);
		delete ref;
		return (new FN_bindinglist_svc(bs));
	}
	return (0);
}

int FNSP_OrgContext::c_bind_nns(const FN_string &name,
    const FN_ref&, unsigned, FN_status_csvc &cstat)
{
	// Not supported for Files and NIS
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_unbind_nns(const FN_string &name, FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_rename_nns(const FN_string &name,
	const FN_composite_name&, unsigned, FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_ref*
FNSP_OrgContext::c_create_subcontext_nns(const FN_string &name,
		    FN_status_csvc &cstat)
{
	// Not supported for Files and NIS
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}


int
FNSP_OrgContext::c_destroy_subcontext_nns(const FN_string &name,
	FN_status_csvc &cstat)
{
	// Not supported for Files and NIS
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_attrset*
FNSP_OrgContext::c_get_syntax_attrs_nns(const FN_string &name,
	FN_status_csvc &cstat)
{
	FN_ref* nns_ref = c_lookup_nns(name, 0, cstat);

	if (cstat.is_success()) {
		FN_attrset* answer =
		    FNSP_Syntax(FNSP_nsid_context)->get_syntax_attrs();
		delete nns_ref;
		if (answer)
			return (answer);
		cstat.set(FN_E_INSUFFICIENT_RESOURCES);
	}
	return (0);
}

FN_attribute*
FNSP_OrgContext::c_attr_get_nns(const FN_string &name,
	const FN_identifier&, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_attr_modify_nns(const FN_string &name,
	unsigned int, const FN_attribute&, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_valuelist*
FNSP_OrgContext::c_attr_get_values_nns(const FN_string &name,
	const FN_identifier&, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_attrset*
FNSP_OrgContext::c_attr_get_ids_nns(const FN_string &name, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_multigetlist*
FNSP_OrgContext::c_attr_multi_get_nns(const FN_string &name,
	const FN_attrset *, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_attr_multi_modify_nns(const FN_string &name,
	const FN_attrmodlist&, FN_attrmodlist**, FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}
