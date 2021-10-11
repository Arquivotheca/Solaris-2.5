/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_NullContext.cc	1.9 94/11/28 SMI"

#include <sys/time.h>

#include "FNSP_NullContext.hh"
#include "../FNSP_Syntax.hh"
#include <xfn/FN_valuelist_svc.hh>

// A NullContext is one in which there no bindings are allowed.
// The only operations of relevance are those associated with the NNS.
// The reference of an NullContext is derived from that of its NNS.
//
// NullContexts can be associated with any type of FNSP Contexts.

FNSP_NullContext::~FNSP_NullContext()
{
	delete nns_reference;
	delete my_reference;
	delete my_address_type;
}

#ifdef NOTNEEDED

// Create a null context whose nns_reference is constructed from
// the given ref_type and <ctx_type, repr_type, dirname>.
// The reference of the null context is constructed from the nns_reference.

FNSP_NullContext::FNSP_NullContext(const FN_identifier &addr,
    const FN_identifier &ref_type,
    const FN_string &dirname,
    unsigned ctx_type, unsigned repr_type)
{
	unsigned status;
	my_address_type = new FN_identifier(addr);
	nns_reference = FNSP_reference(
	    *my_address_type,
	    ref_type, dirname, ctx_type, repr_type);
	my_reference = FNSP_null_context_reference_from(
	    *my_address_type,
	    *nns_reference,
	    status);
	// check for null pointers and status
}

#endif // NOTNEEDED

// Create a null context.
// from == derived:
// nns_reference = given reference;
// my_reference constructed from nns_reference
// from == copy:
// my_reference = given reference;
// nns_reference derived from my_reference

FNSP_NullContext::FNSP_NullContext(const FN_identifier &addr,
    const FN_ref &ref,
    ReferenceType from,
    unsigned context_type)
{
	unsigned status;
	my_address_type = new FN_identifier(addr);
	if (from == derived) {
		// Given reference is nns_reference
		nns_reference = new FN_ref(ref);
		my_reference = FNSP_null_context_reference_from(
		    *my_address_type,
		    *nns_reference,
		    status,
		    context_type);
	} else {
		// Given reference is my_reference
		my_reference = new FN_ref(ref);
		nns_reference = FNSP_null_context_reference_to(*my_reference,
		    status);
	}

	// check for null pointers and status;
}

FNSP_NullContext*
FNSP_NullContext::from_ref(const FN_ref &from_ref, FN_status &stat)
{
	void *ip;
	const FN_ref_addr *ref_addr;
	const FN_identifier *id;

	ref_addr = from_ref.first(ip);
	while (!ref_addr)
		ref_addr = from_ref.next(ip);

	if (!ref_addr) {
		stat.set(FN_E_NO_SUPPORTED_ADDRESS);
		return (0);
	}

	id = ref_addr->type();
	FNSP_NullContext *answer = new FNSP_NullContext(*id, from_ref);

	if (answer && answer->my_reference && answer->nns_reference)
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
FNSP_NullContext::get_ref(FN_status &stat) const
{
	stat.set_success();

	return (new FN_ref(*my_reference));
}


const FN_ref *
FNSP_NullContext::resolve(const FN_string &name, FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		// In a null context, the only successful resolution is one
		// in which no name is given, in which case, we just return
		// the reference of the current context
		cstat.set_success();
		return (my_reference);
	} else {
		cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, name);
		return (0);
	}
}


FN_ref *
FNSP_NullContext::c_lookup(const FN_string &name,
    unsigned int /* lookup_flags */,
    FN_status_csvc &cstat)
{
	const FN_ref *ref = resolve(name, cstat);
	FN_ref *answer = 0;

	if (ref) {
		answer = new FN_ref(*ref);
		if (answer == 0)
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
	}

	// %%% links
	return (answer);
}

FN_namelist*
FNSP_NullContext::c_list_names(const FN_string &name, FN_status_csvc &cstat)
{
	const FN_ref *ref = resolve(name, cstat);  // set cstat
	FN_nameset* answer = 0;

	if (cstat.is_success()) {
		// There are no names bound under a null context;
		answer = new FN_nameset;
		if (answer == 0)
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
	}

	if (answer)
		return (new FN_namelist_svc(answer));
	else
		return (0);
}

FN_bindinglist*
FNSP_NullContext::c_list_bindings(const FN_string &name, FN_status_csvc &cstat)
{
	const FN_ref *ref = resolve(name, cstat); // set cstat
	FN_bindingset* answer = 0;

	if (cstat.is_success()) {
		// there are no names bound under a null context
		answer = new FN_bindingset;
		if (answer == 0)
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
	}

	if (answer)
		return (new FN_bindinglist_svc(answer));
	else
		return (0);
}


int
FNSP_NullContext::c_bind(const FN_string &name, const FN_ref &,
    unsigned, FN_status_csvc &cstat)
{
	// Cannot bind anything in an emtpy context
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_NullContext::c_unbind(const FN_string &name, FN_status_csvc &cstat)
{
	// Cannot unbind anything in an emtpy context
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_NullContext::c_rename(const FN_string &name, const FN_composite_name &,
    unsigned, FN_status_csvc &cstat)
{
	// Cannot bind anything in an emtpy context
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_ref *
FNSP_NullContext::c_create_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	// There can be no subcontexts in a null context
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_NullContext::c_destroy_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	// There are no subcontexts in a null context
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);

}

FN_attrset*
FNSP_NullContext::c_get_syntax_attrs(const FN_string &name,
    FN_status_csvc &cstat)
{
	const FN_ref *ref = resolve(name, cstat);
	FN_attrset* answer = 0;

	if (cstat.is_success()) {
		answer = FNSP_Syntax(FNSP_null_context)->get_syntax_attrs();
		if (answer == 0)
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
	}
	return (answer);
}

FN_attribute*
FNSP_NullContext::c_attr_get(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}

int
FNSP_NullContext::c_attr_modify(const FN_string &name,
    unsigned int,
    const FN_attribute&,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}

FN_valuelist*
FNSP_NullContext::c_attr_get_values(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}

FN_attrset*
FNSP_NullContext::c_attr_get_ids(const FN_string &name,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}

FN_multigetlist*
FNSP_NullContext::c_attr_multi_get(const FN_string &name,
    const FN_attrset *,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}

int
FNSP_NullContext::c_attr_multi_modify(const FN_string &name,
    const FN_attrmodlist&,
    FN_attrmodlist**,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}


FN_ref *
FNSP_NullContext::c_lookup_nns(const FN_string &name,
    unsigned int /* lookup_flags */,
    FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		FN_ref *ref = new FN_ref(*nns_reference);  /* copy */

		if (ref) {
			cstat.set_success();
			return (ref);
		} else
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
	} else
		// No names bound under a null context
		cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, name);

	// %%% links

	return (0);
}

FN_namelist*
FNSP_NullContext::c_list_names_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	if (name.is_empty())
		cstat.set(FN_E_SPI_CONTINUE, nns_reference, 0, 0);
	else
		// No names bound under a null context
		cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, name);

	return (0);
}

FN_bindinglist*
FNSP_NullContext::c_list_bindings_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	if (name.is_empty())
		cstat.set(FN_E_SPI_CONTINUE, nns_reference, 0, 0);
	else
		// No names bound under a null context
		cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, name);
	return (0);
}

// This function should never be called.
int
FNSP_NullContext::c_bind_nns(const FN_string &name,
    const FN_ref &,
    unsigned, FN_status_csvc &cstat)
{
	// Cannot bind any names or their nns directly in a null context
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_NullContext::c_unbind_nns(const FN_string &name, FN_status_csvc &cstat)
{
	// Cannot bind any names or their nns directly in a null context
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

// This function should never be called.
int
FNSP_NullContext::c_rename_nns(const FN_string &name,
    const FN_composite_name &,
    unsigned,
    FN_status_csvc &cstat)
{
	// Cannot bind any names or their nns directly in a null context
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}


FN_ref *
FNSP_NullContext::c_create_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	// Cannot bind any names or their nns directly in a null context
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}
int
FNSP_NullContext::c_destroy_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	// Cannot bind any names or their nns directly in a null context
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

FN_attrset*
FNSP_NullContext::c_get_syntax_attrs_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		cstat.set(FN_E_SPI_CONTINUE, nns_reference, 0, 0);
	} else
		// No names bound in a null context
		cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, name);
	return (0);
}

FN_attribute*
FNSP_NullContext::c_attr_get_nns(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}

int
FNSP_NullContext::c_attr_modify_nns(const FN_string &name,
    unsigned int,
    const FN_attribute&,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}

FN_valuelist*
FNSP_NullContext::c_attr_get_values_nns(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}

FN_attrset*
FNSP_NullContext::c_attr_get_ids_nns(const FN_string &name,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}

FN_multigetlist*
FNSP_NullContext::c_attr_multi_get_nns(const FN_string &name,
    const FN_attrset *,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}

int
FNSP_NullContext::c_attr_multi_modify_nns(const FN_string &name,
    const FN_attrmodlist&,
    FN_attrmodlist**,
    FN_status_csvc &cs)
{
	cs.set_error_context(FN_E_OPERATION_NOT_SUPPORTED, *this, &name);
	return (0);
}

FN_ref *
FNSP_NullContext::n_create_subcontext(const FN_string &name,
    unsigned,
    unsigned,
    const FN_identifier *,
    FN_status &stat)
{
	// Cannot bind any names or their nns directly in a null context
	FN_composite_name cname(name);
	stat.set(FN_E_OPERATION_NOT_SUPPORTED, my_reference, 0, &cname);
	return (0);
}

FN_ref *
FNSP_NullContext::n_create_subcontext_nns(const FN_string &name,
    unsigned,
    unsigned,
    const FN_identifier *,
    FN_status &stat)
{
	// Cannot bind any names or their nns directly in a null context
	FN_composite_name cname(name);
	stat.set(FN_E_OPERATION_NOT_SUPPORTED, my_reference, 0, &cname);
	return (0);
}
