/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_FlatContext.cc	1.9 95/01/09 SMI"

#include <sys/time.h>

#include "FNSP_FlatContext.hh"
#include "fnsp_internal.hh"
#include "../FNSP_Syntax.hh"

//  A flat naming system is one in which only non-hierarchical names are bound.
//  All names in the naming system are bound in a single context.
//  These names are junctions.
//
//  The bindings of the junctions
//  are stored in the binding table of the flat context.
//  The reference bound to a flat name is the same as its nns.
//

static const FN_composite_name empty_name((const unsigned char *)"");

FNSP_FlatContext::~FNSP_FlatContext()
{
	if (my_reference) delete my_reference;
	if (my_address) delete my_address;
}

FNSP_FlatContext::FNSP_FlatContext(const FN_string &dirname,
    unsigned context_type)
{
	my_reference = FNSP_reference(FNSP_nisplus_address_type_name(),
	    dirname, context_type);
	my_address = new FNSP_Address(dirname, context_type);
	// check for null pointers
}

FNSP_FlatContext::FNSP_FlatContext(const FN_ref &from_ref)
{
	my_reference = new FN_ref(from_ref);
	my_address = new FNSP_Address(from_ref);
	// check for null pointers
}

FNSP_FlatContext::FNSP_FlatContext(const FN_ref_addr &from_addr,
    const FN_ref &from_ref)
{
	my_reference = new FN_ref(from_ref);
	my_address = new FNSP_Address(from_addr);
	// check for null pointers
}

FNSP_FlatContext*
FNSP_FlatContext::from_address(const FN_ref_addr &from_addr,
    const FN_ref &from_ref,
    FN_status &stat)
{
	FNSP_FlatContext *answer = new FNSP_FlatContext(from_addr, from_ref);

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
FNSP_FlatContext::get_ref(FN_status &stat) const
{
	FN_ref *answer = new FN_ref(*my_reference);

	if (answer)
		stat.set_success();
	else
		stat.set(FN_E_INSUFFICIENT_RESOURCES);

	return (answer);
}



// Return reference bound to name (junction)

FN_ref *
FNSP_FlatContext::resolve(const FN_string &name,
			    unsigned int lookup_flags,
			    FN_status_csvc &cstat)

{
	unsigned status;
	FN_ref *answer = 0;

	answer = FNSP_lookup_binding(*my_address, name, status);
	if (status == FN_SUCCESS) {
		if (!(lookup_flags&FN_SPI_LEAVE_TERMINAL_LINK) &&
		    answer->is_link()) {
			cstat.set_continue_context(*answer, *this);
			delete answer;
			answer = 0;
		} else
			cstat.set_success();
	} else
		cstat.set_error(status, *my_reference, name);

	return (answer);
}

FN_ref *
FNSP_FlatContext::c_lookup(const FN_string &name,
    unsigned int lookup_flags,
    FN_status_csvc& cstat)
{
	FN_ref *answer = 0;
	if (name.is_empty()) {
		// Return reference of current context
		answer = new FN_ref(*my_reference);
		if (answer)
			cstat.set_success();
		else
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
	} else
		answer = resolve(name, lookup_flags, cstat);

	return (answer);
}


// When the given name is null, return names bound in current context.
// When the given name is not null, resolve the name and set continue
// status for list operation to be performed on that reference

FN_namelist*
FNSP_FlatContext::c_list_names(const FN_string &name, FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		// listing names bound in current context
		FN_nameset *answer = 0;
		unsigned status;

		answer = FNSP_list_names(*my_address, status);
		if (status == FN_SUCCESS && answer) {
			cstat.set_success();
			return (new FN_namelist_svc(answer));
		} else
			cstat.set_error(status, *my_reference, name);
	} else {
		// resolve name and have list be performed there
		FN_ref *ref = resolve(name, 0, cstat);

		if (cstat.is_success()) {
			cstat.set(FN_E_SPI_CONTINUE, ref, 0, &empty_name);
			delete ref;
		}
	}

	return (0);
}

// When the given name is null, return bindings bound in current context.
// When the given name is not null, resolve the name and if successful,
// set continue status for operation to occur in that context.

FN_bindinglist*
FNSP_FlatContext::c_list_bindings(const FN_string &name, FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		// list names in current (flat) context
		FN_bindingset *answer = 0;
		unsigned status;

		answer = FNSP_list_bindings(*my_address, status);
		if (status == FN_SUCCESS && answer) {
			cstat.set_success();
			return (new FN_bindinglist_svc(answer));
		} else
			cstat.set_error(status, *my_reference, name);
	} else {
		// resolve name and set continue status
		FN_ref *ref = resolve(name, 0, cstat);

		if (cstat.is_success()) {
			cstat.set(FN_E_SPI_CONTINUE, ref, 0, &empty_name);
			delete ref;
		}
	}
	return (0);
}

int
FNSP_FlatContext::c_bind(const FN_string &name, const FN_ref &ref,
    unsigned int bind_flags, FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED,
		    *my_reference, name);
		return (0);
	} else {
		unsigned status;

		status = FNSP_add_binding(*my_address, name, ref, bind_flags);
		if (status == FN_SUCCESS)
			cstat.set_success();
		else
			cstat.set_error(status, *my_reference, name);

		return (status == FN_SUCCESS);
	}
}

int
FNSP_FlatContext::c_unbind(const FN_string &name, FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED,
		    *my_reference, name);
		return (0);
	} else {
		unsigned status;
		status = FNSP_remove_binding(*my_address, name);

		if (status == FN_SUCCESS)
			cstat.set_success();
		else
			cstat.set_error(status, *my_reference, name);
		return (status == FN_SUCCESS);
	}
}

int
FNSP_FlatContext::c_rename(const FN_string &name,
    const FN_composite_name &newname,
    unsigned rflags, FN_status_csvc &cstat)
{
	unsigned status;

	if (name.is_empty()) {
		cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference,
		    name);
		return (0);
	}

	void *p;
	const FN_string *fn = newname.first(p);
	if (fn == 0) {
		cstat.set_error(FN_E_ILLEGAL_NAME, *my_reference, name);
		return (0);
	}

	FN_composite_name *rn = newname.suffix(p);
	if (rn) {
		// support only atomic name to be renamed
		delete rn;
		FN_string *newname_string = newname.string();
		cstat.set_error(FN_E_ILLEGAL_NAME, *my_reference,
		    *newname_string);
		delete newname_string;
		return (0);
	}

	status = FNSP_rename_binding(*my_address, name, *fn, rflags);
	if (status == FN_E_NOT_A_CONTEXT) {
		// %%% was context_not_found
		// bindings table did not even exist
		status = FN_E_CONFIGURATION_ERROR;
	}

	if (status == FN_SUCCESS)
		cstat.set_success();
	else
		cstat.set_error(status, *my_reference, name);

	return (status == FN_SUCCESS);
}

// Flat context has no subcontexts
FN_ref *
FNSP_FlatContext::c_create_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_FlatContext::c_destroy_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference,
		    name);
		return (0);
	}

	unsigned status;
	status = FNSP_destroy_and_unbind(*my_address, name);

	if (status == FN_SUCCESS)
		cstat.set_success();
	else
		cstat.set_error(status, *my_reference, name);

	return (status == FN_SUCCESS);
}

FN_attrset*
FNSP_FlatContext::c_get_syntax_attrs(const FN_string &name,
    FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		// return syntax of current (flat) context
		FN_attrset *answer = 0;
		answer = FNSP_Syntax(my_address->get_context_type())
		    ->get_syntax_attrs();
		if (answer)
			cstat.set_success();
		else
			cstat.set(FN_E_INSUFFICIENT_RESOURCES);
		return (answer);
	} else {
		// resolve name and set continue status
		FN_ref *ref = resolve(name, 0, cstat);
		if (cstat.is_success())
			cstat.set(FN_E_SPI_CONTINUE, ref, 0, &empty_name);

		if (ref)
			delete ref;
	}
	return (0);
}

FN_attribute*
FNSP_FlatContext::c_attr_get(const FN_string &name,
    const FN_identifier &id,
    FN_status_csvc &cs)
{
	unsigned status;
	FN_attrset *attrset = FNSP_get_attrset(*my_address, name, status);

	if (attrset == 0)
		status = FN_E_NO_SUCH_ATTRIBUTE;

	if (status != FN_SUCCESS) {
		cs.set_error(status, *my_reference, name);
		return (0);
	}

	// Get the required attribute
	FN_attribute *attr = 0;
	const FN_attribute *old_attr = attrset->get(id);
	if (old_attr) {
		attr = new FN_attribute(*old_attr);
		cs.set_success();
	} else
		cs.set_error(FN_E_NO_SUCH_ATTRIBUTE, *my_reference, name);

	delete attrset;
	return (attr);
}

int
FNSP_FlatContext::c_attr_modify(const FN_string &aname,
    unsigned int flags,
    const FN_attribute& attr,
    FN_status_csvc& cs)
{
	unsigned status = FNSP_modify_attribute(*my_address,
	    aname, attr, flags);

	if (status == FN_SUCCESS) {
		cs.set_success();
		return (1);
	} else {
		cs.set_error(status, *my_reference, aname);
		return (0);
	}
}

FN_valuelist*
FNSP_FlatContext::c_attr_get_values(const FN_string &name,
    const FN_identifier &id,
    FN_status_csvc &cs)
{
	FN_valuelist_svc *answer = 0;
	FN_attribute *attribute = c_attr_get(name, id, cs);
	if (cs.is_success())
		answer = new FN_valuelist_svc(attribute);

	return (answer);
}

FN_attrset*
FNSP_FlatContext::c_attr_get_ids(const FN_string &name,
    FN_status_csvc &cs)
{
	unsigned status;
	FN_attrset *attrset = FNSP_get_attrset(*my_address, name, status);
	if (status != FN_SUCCESS) {
		cs.set_error(status, *my_reference, name);
		return (0);
	}

	if (attrset == 0)
		attrset = new FN_attrset;

	cs.set_success();
	return (attrset);
}

FN_multigetlist*
FNSP_FlatContext::c_attr_multi_get(const FN_string &name,
    const FN_attrset *attrset,
    FN_status_csvc &cs)
{
	FN_multigetlist_svc *answer;

	FN_attrset *result = c_attr_get_ids(name, cs);
	if (!result)
		return (0);

	if (attrset == 0) {
		answer = new FN_multigetlist_svc(result);
		return (answer);
	}

	void *ip;
	int howmany, i;
	const FN_identifier *id;
	const FN_attribute *attr;
	const FN_attribute *new_attr;
	FN_attrset *new_attrset = new FN_attrset;
	howmany = attrset->count();
	attr = attrset->first(ip);
	for (i = 0; attr && i < howmany; i++) {
		id = attr->identifier();
		if (new_attr = result->get(*id))
			new_attrset->add(*new_attr);
		attr = attrset->next(ip);
	}
	delete result;
	answer = new FN_multigetlist_svc(new_attrset);
	return (answer);
}

int
FNSP_FlatContext::c_attr_multi_modify(const FN_string &name,
    const FN_attrmodlist &modlist,
    FN_attrmodlist **un_modlist,
    FN_status_csvc &cs)
{
	void *ip;
	unsigned int mod_op, status;
	const FN_attribute *attribute;

	attribute = modlist.first(ip, mod_op);
	while (attribute) {
		status = c_attr_modify(name, mod_op, *attribute, cs);
		if (!cs.is_success())
			break;
		attribute = modlist.next(ip, mod_op);
	}
	if (!attribute)
		return (status);

	if ((*un_modlist)) {
		(*un_modlist) = new FN_attrmodlist;
		while (attribute) {
			(*un_modlist)->add(mod_op, *attribute);
			attribute = modlist.next(ip, mod_op);
		}
	}
	return (status);
}



// Return binding of given name.
// If given name is null, return not found because
// there can be no nns associated with these types of flat contexts
FN_ref *
FNSP_FlatContext::c_lookup_nns(const FN_string &name,
    unsigned int lookup_flags,
    FN_status_csvc& cstat)
{
	FN_ref *answer = 0;

	if (name.is_empty()) {
		cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, name);
	} else
		answer = c_lookup(name, lookup_flags, cstat);

	return (answer);
}

// If given name is null, return names bound under nns of current context.
// If given name is not null, return names bound under nns of given name.
// In both cases, resolve to desired context first, then return
// FN_E_SPI_CONTINUE.

FN_namelist*
FNSP_FlatContext::c_list_names_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_ref *ref = c_lookup_nns(name, 0, cstat);

	if (cstat.is_success())
		cstat.set(FN_E_SPI_CONTINUE, ref, 0, 0);

	if (ref)
		delete ref;
	return (0);
}

// If given name is null, return names bound under nns of current context.
// If given name is not null, return names bound under nns of given name.
// In both cases, resolve to desired context first,
// then return FN_E_SPI_CONTINUE.
// In other words, do exactly the same thing as c_list_names_nns.
FN_bindinglist*
FNSP_FlatContext::c_list_bindings_nns(const FN_string &name,
    FN_status_csvc& cstat)
{
	FN_ref *ref = c_lookup_nns(name, 0, cstat);

	if (cstat.is_success())
		cstat.set(FN_E_SPI_CONTINUE, ref, 0, 0);

	if (ref)
		delete ref;
	return (0);
}


int
FNSP_FlatContext::c_bind_nns(const FN_string &name, const FN_ref &ref,
    unsigned bind_flags, FN_status_csvc &cstat)
{
	return (c_bind(name, ref, bind_flags, cstat));
}

int
FNSP_FlatContext::c_unbind_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	return (c_unbind(name, cstat));
}

int
FNSP_FlatContext::c_rename_nns(const FN_string &name,
    const FN_composite_name &newname,
    unsigned flags, FN_status_csvc &cstat)
{
	return (c_rename(name, newname, flags, cstat));
}

FN_ref *
FNSP_FlatContext::c_create_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference, name);
	return (0);
}

int
FNSP_FlatContext::c_destroy_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	return (c_destroy_subcontext(name, cstat));
}

FN_attrset*
FNSP_FlatContext::c_get_syntax_attrs_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_ref *ref = c_lookup_nns(name, 0, cstat);
	if (cstat.is_success())
		cstat.set(FN_E_SPI_CONTINUE, ref, 0, 0);

	if (ref)
		delete ref;
	return (0);
}

FN_attribute*
FNSP_FlatContext::c_attr_get_nns(const FN_string &name,
    const FN_identifier &id,
    FN_status_csvc &cs)
{
	return (c_attr_get(name, id, cs));
}

int
FNSP_FlatContext::c_attr_modify_nns(const FN_string &name,
    unsigned int flags,
    const FN_attribute &id,
    FN_status_csvc &cs)
{
	return (c_attr_modify(name, flags, id, cs));
}

FN_valuelist*
FNSP_FlatContext::c_attr_get_values_nns(const FN_string &name,
    const FN_identifier &id,
    FN_status_csvc &cs)
{
	return (c_attr_get_values(name, id, cs));
}

FN_attrset*
FNSP_FlatContext::c_attr_get_ids_nns(const FN_string &name,
    FN_status_csvc &cs)
{
	return (c_attr_get_ids(name, cs));
}

FN_multigetlist*
FNSP_FlatContext::c_attr_multi_get_nns(const FN_string &name,
    const FN_attrset *attrset,
    FN_status_csvc &cs)
{
	return (c_attr_multi_get(name, attrset, cs));
}

int
FNSP_FlatContext::c_attr_multi_modify_nns(const FN_string &name,
    const FN_attrmodlist &modlist,
    FN_attrmodlist **mod,
    FN_status_csvc &cs)
{
	return (c_attr_multi_modify(name, modlist, mod, cs));
}

FN_ref *
FNSP_FlatContext::n_create_subcontext(const FN_string &name,
    unsigned context_type,
    unsigned repr_type,
    const FN_identifier *ref_type,
    FN_status &stat)
{
	if (name.is_empty()) {
		// cannot create self
		FN_composite_name cname(name);
		stat.set(FN_E_OPERATION_NOT_SUPPORTED, my_reference,
		    0, &cname);
		return (0);
	} else {
		return (n_create_subcontext_nns(name, context_type,
		    repr_type, ref_type, stat));
	}
}

FN_ref *
FNSP_FlatContext::n_create_subcontext_nns(const FN_string &name,
    unsigned context_type,
    unsigned representation_type,
    const FN_identifier *ref_type,
    FN_status &stat)
{
	unsigned status;
	FN_composite_name cname(name);

	if (name.is_empty()) {
		// cannot create explicit nns
		stat.set(FN_E_OPERATION_NOT_SUPPORTED, my_reference,
		    0, &cname);
		return (0);
	}

	FN_ref *newref =
		FNSP_create_and_bind(*my_address,
		    name,
		    context_type,
		    representation_type,
		    status,
		    1, /* find legal table name */
		    ref_type);
	if (status == FN_SUCCESS)
		stat.set_success();
	else
		stat.set(status, my_reference, 0, &cname);
	return (newref);
}
