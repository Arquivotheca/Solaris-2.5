/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_nisplusPrinternameContext.cc	1.6 95/02/06 SMI"

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#include <xfn/fn_p.hh>
#include <xfn/fn_printer_p.hh>
#include "fnsp_printer_internal.hh"
#include "../FNSP_printer_Syntax.hh"
#include "FNSP_nisplusPrinternameContext.hh"

static const FN_string empty_string((const unsigned char *) "");

FNSP_nisplusPrinternameContext::~FNSP_nisplusPrinternameContext()
{
}

FNSP_nisplusPrinternameContext::FNSP_nisplusPrinternameContext(
    const FN_ref_addr &from_addr,
    const FN_ref &from_ref)
: FNSP_PrinternameContext(from_addr, from_ref)
{
	my_address = new FNSP_printer_Address(from_addr);
}

FNSP_nisplusPrinternameContext*
FNSP_nisplusPrinternameContext::from_address(const FN_ref_addr &from_addr,
    const FN_ref &from_ref,
    FN_status &stat)
{
	FNSP_nisplusPrinternameContext *answer =
	    new FNSP_nisplusPrinternameContext(from_addr, from_ref);

	if ((answer) && (answer->my_reference))
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
FNSP_nisplusPrinternameContext::resolve(const FN_string &aname,
    FN_status_csvc& cstat)
{
	FN_ref *ref = 0;
	unsigned status;

	ref = FNSP_printer_lookup_binding(*my_address, aname, status);
	if (status == FN_SUCCESS)
		cstat.set_success();
	else
		cstat.set_error(status, *my_reference, empty_string);
	return (ref);
}

FN_nameset*
FNSP_nisplusPrinternameContext::list(FN_status_csvc &cstat)
{
	FN_nameset* ns = 0;

	// Obtain the binding from the files
	ns = FNSP_PrinternameContext::list(cstat);
	return (ns);
}

FN_bindingset*
FNSP_nisplusPrinternameContext::list_bs(FN_status_csvc &cstat)
{
	FN_bindingset* bs = 0;

	// Obtain the binding from the files
	bs = FNSP_PrinternameContext::list_bs(cstat);
	return (bs);
}

FN_ref*
FNSP_nisplusPrinternameContext::c_lookup(const FN_string &name, unsigned int,
    FN_status_csvc &cstat)
{
	FN_ref *answer = 0;
	if (name.is_empty()) {
		// No name was given; resolves to current reference of context
		answer = new FN_ref(*my_reference);
		if (answer)
			cstat.set_success();
		else
			cstat.set_error(FN_E_INSUFFICIENT_RESOURCES,
			    *my_reference, empty_string);
		return (answer);
	} else {
		// Check first in NIS+ tables
		answer = resolve(name, cstat);

		// If reference is not found
		// check if the name is present in files
		if (!answer)
			answer = FNSP_PrinternameContext::resolve(name, cstat);
	}
	return (answer);
}

FN_namelist*
FNSP_nisplusPrinternameContext::c_list_names(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_nameset *ns_files = 0;
	FN_nameset *ns_nisplus = 0;
	if (name.is_empty()) {
		cstat.set_success();

		// Obtain listing from files
		ns_files = list(cstat);
		if (!ns_files)
			return (0);

		// Obtain listing from nisplus
		unsigned status;
		ns_nisplus = FNSP_printer_list_names(*my_address, status);

		// Combine the name lists
		if ((status == FN_SUCCESS) && ns_nisplus) {
			// Added the file entries to the nisplus entries
			void *ip;
			const FN_string *name;
			for (name = ns_files->first(ip); name;
			    name = ns_files->next(ip))
				ns_nisplus->add(*name);
			delete ns_files;
			cstat.set_success();
			return (new FN_namelist_svc(ns_nisplus));
		}

		if (ns_files)
			return (new FN_namelist_svc(ns_files));
	} else {
		// If name is not empty, get the reference and
		// set continue context
		FN_ref *next_ref = resolve(name, cstat);
		if (cstat.is_success())
			cstat.set_continue(*next_ref, *my_reference,
			    &empty_string);
		delete next_ref;
	}
	return (0);
}

FN_bindinglist*
FNSP_nisplusPrinternameContext::c_list_bindings(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_bindingset *bs_files = 0;
	FN_bindingset *bs_nisplus = 0;
	if (name.is_empty()) {
		cstat.set_success();

		// Obtain listing from files
		bs_files = list_bs(cstat);
		if (!bs_files)
			return (0);

		// Obtain listing from nisplus
		unsigned status;
		bs_nisplus = FNSP_printer_list_bindings(*my_address, status);

		// Combine the name lists
		if ((status == FN_SUCCESS) && bs_nisplus) {
			// Added the file entries to the nisplus entries
			void *ip;
			const FN_string *name;
			const FN_ref *ref;
			for (name = bs_files->first(ip, ref); name;
			    name = bs_files->next(ip, ref))
				bs_nisplus->add(*name, *ref);
			delete bs_files;
			cstat.set_success();
			return (new FN_bindinglist_svc(bs_nisplus));
		}

		if (bs_files)
			return (new FN_bindinglist_svc(bs_files));
	} else {
		// If name is not empty, get the reference and
		// set continue context
		FN_ref *next_ref = resolve(name, cstat);
		if (cstat.is_success())
			cstat.set_continue(*next_ref, *my_reference,
			    &empty_string);
		delete next_ref;
	}
	return (0);
}

int
FNSP_nisplusPrinternameContext::c_bind(const FN_string &name,
    const FN_ref &ref, unsigned BindFlags, FN_status_csvc &cstat)
{
	unsigned status;
	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED;
	else
		status =
		    FNSP_printer_add_binding(*my_address, name, ref, BindFlags);

	if (status == FN_SUCCESS)
		cstat.set_success();
	else
		cstat.set_error(status, *my_reference, name);
	return (status == FN_SUCCESS);
}

int
FNSP_nisplusPrinternameContext::c_unbind(const FN_string &name,
    FN_status_csvc& cstat)
{
	unsigned status;
	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED;
	else
		status = FNSP_printer_remove_binding(*my_address, name);

	if (status == FN_SUCCESS)
		cstat.set_success();
	else
		cstat.set_error(status, *my_reference, name);
	return (status == FN_SUCCESS);
}

int
FNSP_nisplusPrinternameContext::c_rename(const FN_string &name,
    const FN_composite_name &newname, unsigned rflags,
    FN_status_csvc &cstat)
{
	unsigned status;

	if (name.is_empty()) {
		cstat.set_error(FN_E_NAME_IN_USE, *my_reference,
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

	status = FNSP_printer_rename_binding(*my_address, name, *fn, rflags);
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

FN_ref*
FNSP_nisplusPrinternameContext::c_create_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_ref *newref = 0;
	unsigned status;
	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED;
	else {
		newref = FNSP_printer_create_and_bind(*my_address, name,
		    FNSP_printer_object,
		    FNSP_normal_repr, status, 1,
		    FNSP_printer_reftype_from_ctxtype(FNSP_printer_object));
	}

	if (status == FN_SUCCESS)
		cstat.set_success();
	else
		cstat.set_error(status, *my_reference, name);

	return (newref);
}

int
FNSP_nisplusPrinternameContext::c_destroy_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	unsigned status;
	if (name.is_empty())
		status = FNSP_printer_destroy_context(*my_address);
	else
		status = FNSP_printer_destroy_and_unbind(*my_address, name);

	if (status == FN_SUCCESS)
		cstat.set_success();
	else
		cstat.set_error(status, *my_reference, name);

	return (status == FN_SUCCESS);
}

FN_attrset*
FNSP_nisplusPrinternameContext::c_get_syntax_attrs(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_attrset* answer;
	if (name.is_empty()) {
		answer = FNSP_printer_Syntax(
		    FNSP_printername_context)->get_syntax_attrs();

		if (answer) {
			cstat.set_success();
			return (answer);
		} else
			cstat.set_error(FN_E_INSUFFICIENT_RESOURCES,
			    *my_reference, name);
	} else {
		FN_ref *ref = resolve(name, cstat);
		if (((ref) && (cstat.is_success())) || (name.is_empty())) {
			answer = FNSP_printer_Syntax(
			    FNSP_printer_object)->get_syntax_attrs();
			delete ref;

			if (answer)
				return (answer);
			else
				cstat.set_error(FN_E_INSUFFICIENT_RESOURCES,
						*my_reference, name);
		}
	}
	return (0);
}


FN_attribute*
FNSP_nisplusPrinternameContext::c_attr_get(const FN_string &name,
    const FN_identifier &id, FN_status_csvc &cs)
{
	unsigned status;
	FN_attrset *attrset = FNSP_printer_get_attrset(*my_address, name, status);
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
FNSP_nisplusPrinternameContext::c_attr_modify(const FN_string &aname,
    unsigned int flags, const FN_attribute &attr, FN_status_csvc& cs)
{
	unsigned status = FNSP_printer_modify_attribute(*my_address,
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
FNSP_nisplusPrinternameContext::c_attr_get_values(const FN_string &name,
    const FN_identifier &id, FN_status_csvc &cs)
{
	FN_valuelist_svc *answer = 0;
	FN_attribute *attribute = c_attr_get(name, id, cs);
	if (cs.is_success())
		answer = new FN_valuelist_svc(attribute);

	return (answer);
}

FN_attrset*
FNSP_nisplusPrinternameContext::c_attr_get_ids(const FN_string &name,
    FN_status_csvc &cs)
{
	unsigned status;
	FN_attrset *attrset = FNSP_printer_get_attrset(*my_address, name, status);
	if (status != FN_SUCCESS) {
		cs.set_error(status, *my_reference, name);
		return (0);
	}
	cs.set_success();
	return (attrset);
}

FN_multigetlist*
FNSP_nisplusPrinternameContext::c_attr_multi_get(const FN_string &name,
    const FN_attrset *attrset, FN_status_csvc &cs)
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
FNSP_nisplusPrinternameContext::c_attr_multi_modify(const FN_string &name,
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

// == Lookup (name:)
FN_ref*
FNSP_nisplusPrinternameContext::c_lookup_nns(const FN_string &name,
    unsigned int stat, FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, name);
		return (0);
	} else
		return (c_lookup(name, stat, cstat));
}

FN_namelist*
FNSP_nisplusPrinternameContext::c_list_names_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	return (c_list_names(name, cstat));
}

FN_bindinglist *
FNSP_nisplusPrinternameContext::c_list_bindings_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	return (c_list_bindings(name, cstat));
}

int
FNSP_nisplusPrinternameContext::c_bind_nns(const FN_string &name,
    const FN_ref &ref, unsigned bind, FN_status_csvc &cstat)
{
	return (c_bind(name, ref, bind, cstat));
}

int
FNSP_nisplusPrinternameContext::c_unbind_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	return (c_unbind(name, cstat));
}

int
FNSP_nisplusPrinternameContext::c_rename_nns(const FN_string &name,
    const FN_composite_name &aname, unsigned bind, FN_status_csvc &cstat)
{
	return (c_rename(name, aname, bind, cstat));
}

FN_ref*
FNSP_nisplusPrinternameContext::c_create_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)

{
	return (c_create_subcontext(name, cstat));
}

int
FNSP_nisplusPrinternameContext::c_destroy_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	return (c_destroy_subcontext(name, cstat));
}

FN_attrset*
FNSP_nisplusPrinternameContext::c_get_syntax_attrs_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_ref* nns_ref = c_lookup_nns(name, 0, cstat);

	if ((nns_ref) && (cstat.is_success())) {
		FN_attrset* answer =
		    FNSP_printer_Syntax(
		    FNSP_printername_context)->get_syntax_attrs();
		delete nns_ref;
		if (answer)
			return (answer);
		cstat.set_error(FN_E_INSUFFICIENT_RESOURCES, *my_reference,
		    name);
	}
	return (0);
}
FN_attribute*
FNSP_nisplusPrinternameContext::c_attr_get_nns(const FN_string &name,
    const FN_identifier &id, FN_status_csvc &cs)
{
	return (c_attr_get(name, id, cs));
}

int
FNSP_nisplusPrinternameContext::c_attr_modify_nns(const FN_string &name,
    unsigned int mod, const FN_attribute &attr, FN_status_csvc &cs)
{
	return (c_attr_modify(name, mod, attr, cs));
}

FN_valuelist*
FNSP_nisplusPrinternameContext::c_attr_get_values_nns(const FN_string &name,
    const FN_identifier &id, FN_status_csvc &cs)
{
	return (c_attr_get_values(name, id, cs));
}

FN_attrset*
FNSP_nisplusPrinternameContext::c_attr_get_ids_nns(const FN_string &name,
    FN_status_csvc &cs)
{
	return (c_attr_get_ids(name, cs));
}

FN_multigetlist*
FNSP_nisplusPrinternameContext::c_attr_multi_get_nns(const FN_string &name,
    const FN_attrset *attr, FN_status_csvc &cs)
{
	return (c_attr_multi_get(name, attr, cs));
}

int
FNSP_nisplusPrinternameContext::c_attr_multi_modify_nns(const FN_string &name,
    const FN_attrmodlist &modl, FN_attrmodlist** unmod, FN_status_csvc &cs)
{
	return (c_attr_multi_modify(name, modl, unmod, cs));
}
