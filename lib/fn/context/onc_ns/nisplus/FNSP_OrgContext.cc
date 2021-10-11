/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_OrgContext.cc	1.8 95/01/29 SMI"

#include <sys/time.h>

#include "FNSP_OrgContext.hh"
#include "fnsp_internal.hh"
#include "../FNSP_Syntax.hh"

static const FN_string empty_name((unsigned char *)"");

#define	CAREFUL

FNSP_OrgContext::~FNSP_OrgContext()
{
	if (my_reference) delete my_reference;
	if (my_address) delete my_address;
}

FNSP_OrgContext::FNSP_OrgContext(const FN_string &dirname)
{
	my_reference = FNSP_reference(FNSP_nisplus_address_type_name(),
	    dirname, FNSP_organization_context);

	my_address = new FNSP_Address(dirname, FNSP_organization_context);

	// check for null pointers
}

FNSP_OrgContext::FNSP_OrgContext(const FN_ref &from_ref)
{
	my_address = new FNSP_Address(from_ref);

	my_reference = new FN_ref(from_ref);
	// check for null pointers
}

FNSP_OrgContext::FNSP_OrgContext(const FN_ref_addr &from_addr,
    const FN_ref &from_ref)
{
	my_reference = new FN_ref(from_ref);

	my_address = new FNSP_Address(from_addr);

	// check for null pointers
}

FNSP_OrgContext*
FNSP_OrgContext::from_address(const FN_ref_addr &from_addr,
    const FN_ref &from_ref,
    FN_status &stat)
{
	FNSP_OrgContext *answer = new FNSP_OrgContext(from_addr, from_ref);

	if (answer && answer->my_reference && answer->my_address)
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
FNSP_OrgContext::get_ref(FN_status &stat) const
{
	stat.set_success();

	return (new FN_ref(*my_reference));
}

FN_ref *
FNSP_OrgContext::resolve(const FN_string &name, FN_status_csvc &cstat)
{
	int stat_set = 0;
	unsigned status;
	FN_status stat;
	FN_ref *answer;

	if (name.is_empty()) {
		// No name was given; resolves to current reference of context
		answer = new FN_ref(*my_reference);
		if (answer)
			cstat.set_success();
		else
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
	} else {
		answer = FNSP_resolve_orgname(my_address->get_internal_name(),
		    name, my_address->get_access_flags(),
		    status, stat, stat_set);
		if (status == FN_SUCCESS)
			cstat.set_success();
		else if (stat_set == 0) {
			cstat.set_error(status, *my_reference, name);
		} else {
			// convert FN_status to FN_status_csvc
			FN_string *rname;
			rname = stat.remaining_name()->string();
			cstat.set_error(stat.code(), *(stat.resolved_ref()),
			    *rname);
		}
	}
	return (answer);
}

FN_ref *
FNSP_OrgContext::c_lookup(const FN_string &name, unsigned int,
    FN_status_csvc &stat)
{
	// orgs cannot be linked
	return (resolve(name, stat));
}

FN_namelist*
FNSP_OrgContext::c_list_names(const FN_string &name, FN_status_csvc &cstat)
{
	FN_ref *ref = resolve(name, cstat);
	unsigned status;
	FN_nameset* answer = 0;

	if (cstat.is_success()) {
		FN_string *dirname = FNSP_reference_to_internal_name(*ref);
		if (dirname) {
			answer = FNSP_list_orgnames(*dirname,
			    my_address->get_access_flags(), status);
			delete dirname;
			if (status != FN_SUCCESS)
				cstat.set_error(status, *ref, empty_name);
		} else // ??? resolved but not to a FNSP reference?
			cstat.set_error(status, *ref, empty_name);
		if (ref)
			delete ref;
	}

	if (answer)
		return (new FN_namelist_svc(answer));
	else
		return (0);
}

FN_bindinglist*
FNSP_OrgContext::c_list_bindings(const FN_string &name, FN_status_csvc &cstat)
{
	FN_ref *ref = resolve(name, cstat);
	FN_status stat;
	unsigned status;
	FN_bindingset* answer = 0;

	if (cstat.is_success()) {
		FN_string *dirname = FNSP_reference_to_internal_name(*ref);
		if (dirname) {
			answer = FNSP_list_orgbindings(*dirname,
			    my_address->get_access_flags(), status);
			if (status != FN_SUCCESS)
				cstat.set_error(status, *ref, empty_name);
			delete dirname;
		} else
			// ??? resolved but not to a FNSP reference?
			// probably corrupted
			cstat.set_error(FN_E_MALFORMED_REFERENCE, *ref,
			    empty_name);
		if (ref)
			delete ref;
	}

	if (answer)
		return (new FN_bindinglist_svc(answer));
	else
		return (0);
}

int
FNSP_OrgContext::c_bind(const FN_string &name, const FN_ref &,
    unsigned, FN_status_csvc &cstat)
{
	/* not supported for ORG */
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_unbind(const FN_string &name, FN_status_csvc &cstat)
{
	/* not supported for ORG */
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}


int
FNSP_OrgContext::c_rename(const FN_string &name, const FN_composite_name &,
    unsigned, FN_status_csvc &cstat)
{
	/* not supported for ORG */
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}


FN_ref *
FNSP_OrgContext::c_create_subcontext(const FN_string &name,
    FN_status_csvc& cstat)
{
	// Should create a NIS+ domain with given name!
	// Not supported for ORG (use NIS+ tools)
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}


int
FNSP_OrgContext::c_destroy_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	// Should not be supported.  Rather dangerous.
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_attrset*
FNSP_OrgContext::c_get_syntax_attrs(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_ref *ref = resolve(name, cstat);

	if (cstat.is_success()) {
		FN_attrset* answer =
		    FNSP_Syntax(FNSP_organization_context)->get_syntax_attrs();
		delete ref;
		if (answer) {
			return (answer);
		}
		cstat.set_error(FN_E_INSUFFICIENT_RESOURCES, *my_reference,
		    name);
		return (0);
	}
	return (0);
}

FN_attribute*
FNSP_OrgContext::c_attr_get(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_attr_modify(const FN_string &name,
    unsigned int,
    const FN_attribute&,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_valuelist*
FNSP_OrgContext::c_attr_get_values(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_attrset*
FNSP_OrgContext::c_attr_get_ids(const FN_string &name,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_multigetlist*
FNSP_OrgContext::c_attr_multi_get(const FN_string &name,
    const FN_attrset *,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_attr_multi_modify(const FN_string &name,
    const FN_attrmodlist&,
    FN_attrmodlist**,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

// Given reference for an organization,
// extract the directory name of the org from the reference,
// and construct the object name for the nns context associated
// with the org.
// If 'dirname_holder' is supplied, use it to return the directory name of org.
static FN_string *
get_org_nns_objname(const FN_ref &ref,
    unsigned &status,
    FN_string**dirname_holder = 0)
{
	FN_string *dirname = FNSP_reference_to_internal_name(ref);
	FN_string *nnsobjname = 0;

	if (dirname) {
		nnsobjname = FNSP_compose_ctx_tablename(empty_name, *dirname);
		if (dirname_holder)
			*dirname_holder = dirname;
		else
			delete dirname;
	} else
		status = FN_E_MALFORMED_REFERENCE;

	return (nnsobjname);
}


// == Lookup (name:)
// %%% cannot be linked (reference generated algorithmically)
// %%% If supported, must store link somewhere and change
// %%% entire ctx implementation, which depends on non-linked repr
// %%%
FN_ref *
FNSP_OrgContext::c_lookup_nns(const FN_string &name,
    unsigned int, /* lookup_flags */
    FN_status_csvc& cstat)
{
	FN_ref *ref = resolve(name, cstat);
	FN_ref *answer = 0;
	unsigned status;

	if (cstat.is_success()) {
		// found name; now look for nns
		FN_string *nnsobjname = get_org_nns_objname(*ref, status);
		if (nnsobjname != 0) {
#ifndef CAREFUL
			answer = FNSP_reference(
			    FNSP_nisplus_address_type_name(),
			    *nnsobjname,
			    FNSP_nsid_context);
			if (answer == 0)
				cstat.set(FN_E_INSUFFICIENT_RESOURCES);
#else
			FNSP_Address nnsaddr(*nnsobjname, FNSP_nsid_context);
			status = FNSP_context_exists(nnsaddr);
			switch (status) {
			case FN_SUCCESS:
				answer = FNSP_reference(
				    FNSP_nisplus_address_type_name(),
				    *nnsobjname,
				    FNSP_nsid_context);
				if (answer == 0)
					cstat.set(FN_E_INSUFFICIENT_RESOURCES);
				break;
			case FN_E_NOT_A_CONTEXT:
				// %%% was context_not_found
				cstat.set_error(FN_E_NAME_NOT_FOUND, *ref,
				    empty_name);
				break;
			default:
				cstat.set_error(status, *ref, empty_name);
			}
#endif
			delete nnsobjname;
		} else {
			cstat.set_error(status, *ref, empty_name);
		}
	}
	if (ref)
		delete ref;
	return (answer);
}

FN_namelist*
FNSP_OrgContext::c_list_names_nns(const FN_string &name, FN_status_csvc& cstat)
{
	FN_ref *ref = resolve(name, cstat);
	unsigned status;
	FN_nameset* answer = 0;

	if (cstat.is_success()) {
		FN_string *nnsobjname = get_org_nns_objname(*ref, status);
		if (nnsobjname) {
			FNSP_Address nnsaddr(*nnsobjname, FNSP_nsid_context);
			answer = FNSP_list_names(nnsaddr, status);
			delete nnsobjname;
			// nns context not there -> ':' not found
			if (status == FN_E_NOT_A_CONTEXT)
				// %%% was CONTEXT_NOT_FOUND
				status = FN_E_NAME_NOT_FOUND;
			if (status != FN_SUCCESS)
				cstat.set_error(status, *ref, empty_name);
		} else
			cstat.set_error(status, *ref, empty_name);
	}

	delete ref;

	if (answer)
		return (new FN_namelist_svc(answer));
	else
		return (0);
}

FN_bindinglist*
FNSP_OrgContext::c_list_bindings_nns(const FN_string &name,
    FN_status_csvc& cstat)
{
	FN_ref *ref = resolve(name, cstat);
	unsigned status;

	if (cstat.is_success()) {
		FN_string *nnsobjname = get_org_nns_objname(*ref, status);
		if (nnsobjname == 0) {
			cstat.set_error(status, *ref, empty_name);
		} else {
			FN_ref *nns_ref;
#ifndef CAREFUL
			status = FN_SUCCESS;
#else
			FNSP_Address nnsaddr(*nnsobjname, FNSP_nsid_context);
			status = FNSP_context_exists(nnsaddr);
#endif
			switch (status) {
			case FN_SUCCESS:
				nns_ref = FNSP_reference(
				    FNSP_nisplus_address_type_name(),
				    *nnsobjname,
				    FNSP_nsid_context);
				if (nns_ref == 0)
					cstat.set(FN_E_INSUFFICIENT_RESOURCES);
				else {
					cstat.set(FN_E_SPI_CONTINUE,
					    nns_ref, 0, 0);
					delete nns_ref;
				}
				break;
			default:
				cstat.set_error(status, *ref, empty_name);
			}
			delete nnsobjname;
		}
	}
	delete ref;
	return (0);
}


// Does it make sense to allow bind_nns, given that we hardwire
// where its contexts are stored (under org_ctx_dir)?  Probably not.
int
FNSP_OrgContext::c_bind_nns(const FN_string &name,
    const FN_ref &,
    unsigned,
    FN_status_csvc &cstat)
{
	// should we do a lookup first so that we can return
	// NotAContext when appropriate?

	FN_ref *nameref = resolve(name, cstat);

	if (cstat.is_success())
		cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *nameref,
		    empty_name);
	// else keep cstat from resolve

	delete nameref;
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
    const FN_composite_name &,
    unsigned,
    FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_ref *
FNSP_OrgContext::c_create_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_ref *orgref = resolve(name, cstat);
	FN_ref *ref = 0;
	unsigned status;

	if (cstat.is_success()) {
		FN_string *nnsobjname, *dirname = 0;
		nnsobjname = get_org_nns_objname(*orgref, status, &dirname);

		if (nnsobjname) {
			FNSP_Address nnsaddr(*nnsobjname, FNSP_nsid_context);
			unsigned estatus = FNSP_context_exists(nnsaddr);
			switch (estatus) {
			case FN_SUCCESS:
				status = FN_E_NAME_IN_USE;
				// must destroy explicitly first
				break;
			case FN_E_NOT_A_CONTEXT:
				// %%% was context_not_found
				ref = FNSP_create_context(nnsaddr, status,
				    dirname);
				break;
			default:
				// cannot determine state of subcontext
				status = estatus;
			}
			delete nnsobjname;
			delete dirname;
			if (status != FN_SUCCESS)
				cstat.set_error(status, *orgref, empty_name);
		} else
			cstat.set_error(status, *ref, empty_name);
	}

	if (orgref)
		delete orgref;

	return (ref);
}


int
FNSP_OrgContext::c_destroy_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_ref *orgref = resolve(name, cstat);

	if (cstat.is_success()) {
		FN_string *nnsobjname, *dirname = 0;
		unsigned status;

		nnsobjname = get_org_nns_objname(*orgref, status, &dirname);
		if (nnsobjname && dirname) {
			FNSP_Address nnsaddr(*nnsobjname, FNSP_nsid_context);
			status = FNSP_destroy_context(nnsaddr, dirname);
			delete nnsobjname;
			delete dirname;
			if (status != FN_SUCCESS)
				cstat.set_error(status, *orgref, empty_name);
		} else
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
	}

	delete orgref;
	return (cstat.is_success());
}

FN_attrset*
FNSP_OrgContext::c_get_syntax_attrs_nns(const FN_string &name,
    FN_status_csvc& cstat)
{
	FN_ref *nns_ref = c_lookup_nns(name, 0, cstat);

	if (cstat.is_success()) {
		FN_attrset* answer =
		    FNSP_Syntax(FNSP_nsid_context)->get_syntax_attrs();
		delete nns_ref;
		if (answer) {
			return (answer);
		}
		cstat.set(FN_E_INSUFFICIENT_RESOURCES);
		return (0);
	}
	return (0);
}

FN_attribute*
FNSP_OrgContext::c_attr_get_nns(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_attr_modify_nns(const FN_string &name,
    unsigned int,
    const FN_attribute&,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_valuelist*
FNSP_OrgContext::c_attr_get_values_nns(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_attrset*
FNSP_OrgContext::c_attr_get_ids_nns(const FN_string &name,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_multigetlist*
FNSP_OrgContext::c_attr_multi_get_nns(const FN_string &name,
    const FN_attrset *,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_OrgContext::c_attr_multi_modify_nns(const FN_string &name,
    const FN_attrmodlist&,
    FN_attrmodlist**,
    FN_status_csvc &cs)
{
	cs.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_ref *
FNSP_OrgContext::n_create_subcontext(const FN_string &name,
    unsigned,
    unsigned,
    const FN_identifier *,
    FN_status &stat)
{
	FN_composite_name cname(name);
	stat.set(FN_E_OPERATION_NOT_SUPPORTED, my_reference, 0, &cname);
	return (0);
}

FN_ref *
FNSP_OrgContext::n_create_subcontext_nns(const FN_string &name,
    unsigned context_type,
    unsigned representation_type,
    const FN_identifier *ref_type,
    FN_status &stat)
{
	FN_composite_name cname(name);
	if (context_type != FNSP_nsid_context ||
	    representation_type != FNSP_normal_repr ||
	    ref_type != 0) {
		stat.set(FN_E_OPERATION_NOT_SUPPORTED, my_reference, 0,
		    &cname);
		return (0);
	}

	FN_status_csvc cstat;
	FN_ref *answer = c_create_subcontext_nns(name, cstat);
	if (cstat.is_success())
		stat.set_success();
	else
		stat.set(cstat.code(), my_reference, 0, &cname);
	return (answer);
}
