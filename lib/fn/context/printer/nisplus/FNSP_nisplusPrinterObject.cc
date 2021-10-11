/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_nisplusPrinterObject.cc	1.6 95/02/06 SMI"

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#include <xfn/fn_p.hh>
#include <xfn/fn_printer_p.hh>
#include "../FNSP_printer_Syntax.hh"
#include "fnsp_printer_internal.hh"
#include "FNSP_nisplusPrinterObject.hh"

static const FN_string empty_string((const unsigned char *) "");

FNSP_nisplusPrinterObject::~FNSP_nisplusPrinterObject()
{
	if (my_reference)
		delete my_reference;
	if (my_address)
		delete my_address;
}

#ifdef DEBUG
FNSP_nisplusPrinterObject::FNSP_nisplusPrinterObject(const FN_ref &from_ref)
{
	my_address = new FNSP_printer_Address(from_ref);
	my_reference = new FN_ref(from_ref);
	my_syntax = FNSP_printer_Syntax(
	    FNSP_printer_object)->get_syntax_attrs();
}
#endif

FNSP_nisplusPrinterObject::FNSP_nisplusPrinterObject(
    const FN_ref_addr &from_addr,
    const FN_ref &from_ref)
{
	my_reference = new FN_ref(from_ref);
	my_address = new FNSP_printer_Address(from_addr);
	my_syntax = FNSP_printer_Syntax(
	    FNSP_printer_object)->get_syntax_attrs();

}

FNSP_nisplusPrinterObject*
FNSP_nisplusPrinterObject::from_address(const FN_ref_addr &from_addr,
    const FN_ref &from_ref,
    FN_status &stat)
{
	FNSP_nisplusPrinterObject *answer =
	    new FNSP_nisplusPrinterObject(from_addr, from_ref);

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

FN_ref*
FNSP_nisplusPrinterObject::get_ref(FN_status &stat) const
{
	stat.set_success();

	return (new FN_ref(*my_reference));
}

FN_ref*
FNSP_nisplusPrinterObject::resolve(const FN_string &aname,
    FN_status_csvc &cstat)
{
	FN_ref *ref = 0;
	unsigned status;

	ref = FNSP_printer_lookup_binding(*my_address, aname, status);
	if (status == FN_SUCCESS)
		cstat.set_success();
	else
		cstat.set_error(status, *my_reference, aname);
	return (ref);
}

FN_ref*
FNSP_nisplusPrinterObject::c_lookup(const FN_string& name, unsigned int,
			FN_status_csvc& cstat)
{
	if (name.is_empty()) {
		FN_ref *answer = 0;

		// No name was given; resolves to current reference of context
		answer = new FN_ref(*my_reference);
		if (answer)
			cstat.set_success();
		else
			cstat.set_error(FN_E_INSUFFICIENT_RESOURCES,
			    *my_reference, name);
		return (answer);
	} else
		return (resolve(name, cstat));
}

FN_namelist*
FNSP_nisplusPrinterObject::c_list_names(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_nameset *ns = 0;
	if (name.is_empty()) {
		// Obtain listing from nisplus
		unsigned status;
		ns = FNSP_printer_list_names(*my_address, status);
		if (status == FN_SUCCESS) {
			cstat.set_success();
			return (new FN_namelist_svc(ns));
		} else {
			cstat.set_error(status, *my_reference, name);
			return (0);
		}
	} else {
		// If name is not empty, get the reference
		// and set continue context
		FN_ref *next_ref = resolve(name, cstat);
		if (cstat.is_success())
			cstat.set_continue(*next_ref, *my_reference,
			    &empty_string);
		delete next_ref;
		return (0);
	}
}

FN_bindinglist*
FNSP_nisplusPrinterObject::c_list_bindings(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_bindingset *bs = 0;
	if (name.is_empty()) {
		// Obtain listing from nisplus
		unsigned status;
		bs = FNSP_printer_list_bindings(*my_address, status);
		if (status == FN_SUCCESS) {
			cstat.set_success();
			return (new FN_bindinglist_svc(bs));
		} else {
			cstat.set_error(status, *my_reference, name);
			return (0);
		}
	} else {
		// If name is not empty, get the reference
		// and set continue context
		FN_ref *next_ref = resolve(name, cstat);
		if (cstat.is_success())
			cstat.set_continue(*next_ref, *my_reference,
			    &empty_string);
		delete next_ref;
		return (0);
	}
}

int
FNSP_nisplusPrinterObject::c_bind(const FN_string &name, const FN_ref &ref,
    unsigned BindFlags, FN_status_csvc &cstat)
{
	unsigned status;
	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED;
	else
		status = FNSP_printer_add_binding(*my_address, name,
		    ref, BindFlags);

	if (status == FN_SUCCESS)
		cstat.set_success();
	else
		cstat.set_error(status, *my_reference, name);
	return (status == FN_SUCCESS);
}

int
FNSP_nisplusPrinterObject::c_unbind(const FN_string &name,
    FN_status_csvc &cstat)
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
FNSP_nisplusPrinterObject::c_rename(const FN_string &name,
    const FN_composite_name &newname, unsigned rflags, FN_status_csvc &cstat)
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
FNSP_nisplusPrinterObject::c_create_subcontext(const FN_string &name,
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
FNSP_nisplusPrinterObject::c_destroy_subcontext(const FN_string &name,
    FN_status_csvc &cstat)
{
	unsigned status;
	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED;
	else
		status = FNSP_printer_destroy_and_unbind(*my_address, name);

	if (status == FN_SUCCESS)
		cstat.set_success();
	else
		cstat.set_error(status, *my_reference, name);
	return (status == FN_SUCCESS);
}

FN_attrset*
FNSP_nisplusPrinterObject::c_get_syntax_attrs(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_attrset* answer;
	if (name.is_empty()) {
		answer = FNSP_printer_Syntax(
		    FNSP_printer_object)->get_syntax_attrs();
		if (answer) {
			cstat.set_success();
			return (answer);
		} else
			cstat.set_error(FN_E_INSUFFICIENT_RESOURCES,
			    *my_reference, name);
	} else {
		FN_ref *ref = resolve(name, cstat);
		if (cstat.is_success()) {
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
FNSP_nisplusPrinterObject::c_attr_get(const FN_string &name,
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
FNSP_nisplusPrinterObject::c_attr_modify(const FN_string &aname,
    unsigned int flags, const FN_attribute &attr, FN_status_csvc &cs)
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
FNSP_nisplusPrinterObject::c_attr_get_values(const FN_string &name,
    const FN_identifier &id, FN_status_csvc &cs)
{
	FN_valuelist_svc *answer = 0;
	FN_attribute *attribute = c_attr_get(name, id, cs);
	if (cs.is_success())
		answer = new FN_valuelist_svc(attribute);

	return (answer);
}

FN_attrset*
FNSP_nisplusPrinterObject::c_attr_get_ids(const FN_string &name,
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
FNSP_nisplusPrinterObject::c_attr_multi_get(const FN_string &name,
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
FNSP_nisplusPrinterObject::c_attr_multi_modify(const FN_string &name,
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
FNSP_nisplusPrinterObject::c_lookup_nns(const FN_string &name,
    unsigned int stat, FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, name);
		return (0);
	} else
		return (c_lookup(name, stat, cstat));
}

FN_namelist*
FNSP_nisplusPrinterObject::c_list_names_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	return (c_list_names(name, cstat));
}

FN_bindinglist *
FNSP_nisplusPrinterObject::c_list_bindings_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	return (c_list_bindings(name, cstat));
}


int
FNSP_nisplusPrinterObject::c_bind_nns(const FN_string &name,
    const FN_ref &ref, unsigned bind, FN_status_csvc &cstat)
{
	return (c_bind(name, ref, bind, cstat));
}

int
FNSP_nisplusPrinterObject::c_unbind_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	return (c_unbind(name, cstat));
}

int
FNSP_nisplusPrinterObject::c_rename_nns(const FN_string &name,
    const FN_composite_name &aname, unsigned bind, FN_status_csvc &cstat)
{
	return (c_rename(name, aname, bind, cstat));
}

FN_ref*
FNSP_nisplusPrinterObject::c_create_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)

{
	return (c_create_subcontext(name, cstat));
}


int
FNSP_nisplusPrinterObject::c_destroy_subcontext_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	return (c_destroy_subcontext(name, cstat));
}

FN_attrset*
FNSP_nisplusPrinterObject::c_get_syntax_attrs_nns(const FN_string &name,
    FN_status_csvc &cstat)
{
	FN_ref *nns_ref = c_lookup_nns(name, 0, cstat);

	if (cstat.is_success()) {
		FN_attrset *answer = FNSP_printer_Syntax(
		    FNSP_printer_object)->get_syntax_attrs();
		delete nns_ref;
		if (answer)
			return (answer);
		cstat.set_error(FN_E_INSUFFICIENT_RESOURCES, *my_reference,
		    name);
	}
	return (0);
}

FN_attribute*
FNSP_nisplusPrinterObject::c_attr_get_nns(const FN_string &name,
    const FN_identifier &id, FN_status_csvc &cs)
{
	return (c_attr_get(name, id, cs));
}

int
FNSP_nisplusPrinterObject::c_attr_modify_nns(const FN_string &name,
    unsigned int mod, const FN_attribute &attr, FN_status_csvc &cs)
{
	return (c_attr_modify(name, mod, attr, cs));
}

FN_valuelist*
FNSP_nisplusPrinterObject::c_attr_get_values_nns(const FN_string &name,
    const FN_identifier &id, FN_status_csvc &cs)
{
	return (c_attr_get_values(name, id, cs));
}

FN_attrset*
FNSP_nisplusPrinterObject::c_attr_get_ids_nns(const FN_string& name,
    FN_status_csvc &cs)
{
	return (c_attr_get_ids(name, cs));
}

FN_multigetlist*
FNSP_nisplusPrinterObject::c_attr_multi_get_nns(const FN_string &name,
    const FN_attrset *attr, FN_status_csvc &cs)
{
	return (c_attr_multi_get(name, attr, cs));
}

int
FNSP_nisplusPrinterObject::c_attr_multi_modify_nns(const FN_string &name,
    const FN_attrmodlist &modl, FN_attrmodlist ** unmod,
    FN_status_csvc &cs)
{
	return (c_attr_multi_modify(name, modl, unmod, cs));
}
