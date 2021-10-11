/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_HUContext.cc	1.9 95/01/09 SMI"

#include <sys/time.h>

#include "FNSP_HUContext.hh"
#include "fnsp_internal.hh"
#include "../FNSP_Syntax.hh"

#include <rpcsvc/nis.h>
#include <stdlib.h>
#include <string.h>

// %%% eventually, might want to merge this with FNSP_FlatContext
// %%% but for now, this is difficult because of attribute implementation
// %%% need to reexamine that

FNSP_HUContext::~FNSP_HUContext()
{
	if (my_orgname) delete my_orgname;
}

FNSP_HUContext::FNSP_HUContext(const FN_string &dirname,
    unsigned context_type, unsigned child_context_type) :
    FNSP_FlatContext(dirname, context_type)
{
	unsigned status;
	my_orgname = FNSP_orgname_of(my_address->get_internal_name(), status);
	my_child_context_type = child_context_type;

	// check for null pointers and status
}

FNSP_HUContext::FNSP_HUContext(const FN_ref &from_ref,
    unsigned child_context_type) :
    FNSP_FlatContext(from_ref)
{
	unsigned status;
	my_orgname = FNSP_orgname_of(my_address->get_internal_name(), status);
	my_child_context_type = child_context_type;

	// check for null pointers and status
}

FNSP_HUContext::FNSP_HUContext(const FN_ref_addr &from_addr,
    const FN_ref &from_ref, unsigned child_context_type) :
    FNSP_FlatContext(from_addr, from_ref)
{
	unsigned status;

	my_orgname = FNSP_orgname_of(my_address->get_internal_name(), status);
	my_child_context_type = child_context_type;

	// check for null pointers
}


// Enumeration Operations

class FN_namelist_hu : public FN_namelist {
	nis_name table_name;
	char *next_in_line;
	unsigned int next_status;
	netobj iter_pos;
public:
	FN_namelist_hu(const char *table_name, char *name, netobj);
	~FN_namelist_hu();
	FN_string* next(FN_status &);
};

FN_namelist_hu::FN_namelist_hu(const char *tabname, char *first_name,
    netobj ip)
{
	table_name = strdup(tabname);
	next_in_line = first_name;
	next_status = FN_SUCCESS;
	iter_pos = ip;
}

FN_namelist_hu::~FN_namelist_hu()
{
	free(table_name);
	free(next_in_line);
	free(iter_pos.n_bytes);
}

FN_string*
FN_namelist_hu::next(FN_status &status)
{
	FN_string *answer;

	if (next_status != FN_SUCCESS) {
		status.set(next_status);
		return (0);
	}

	if (next_in_line == 0) {
		next_status = FN_E_INVALID_ENUM_HANDLE;
		status.set_success();
		return (0);
	}

	answer = new FN_string((const unsigned char *)next_in_line);
	free(next_in_line);

	// This will
	// 1. read the next entry
	// 2. free iter_pos
	// 3. reassign iter_pos if any
	next_in_line = FNSP_read_next(table_name, iter_pos, next_status);

	return (answer);
}


FN_namelist* FNSP_HUContext::c_list_names(const FN_string &name,
    FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		// listing names bound in current context
		netobj iter_pos;
		unsigned int status;
		const FN_string tab = my_address->get_table_name();
		const char *table_name = (const char *)tab.str();
		char *first = FNSP_read_first(table_name, iter_pos, status);

		if (status != FN_SUCCESS) {
			cstat.set_error(status, *my_reference, name);
			return (0);
		}
		cstat.set_success();
		return (new FN_namelist_hu(table_name, first, iter_pos));
	} else {
		// resolve name and have list be performed there
		FN_ref *ref = resolve(name, 0, cstat);
		FN_composite_name empty_name((const unsigned char *)"");

		if (cstat.is_success()) {
			cstat.set(FN_E_SPI_CONTINUE, ref, 0, &empty_name);
			delete ref;
		}
	}

	return (0);
}

class FN_bindinglist_hu : public FN_bindinglist {
	nis_name table_name;
	char *next_in_line;
	FN_ref *next_ref;
	unsigned int next_status;
	netobj iter_pos;
public:
	FN_bindinglist_hu(const char *table_name, char *name, FN_ref *ref,
			    netobj);
	~FN_bindinglist_hu();
	FN_string* next(FN_ref **ref, FN_status &);
};

FN_bindinglist_hu::FN_bindinglist_hu(const char *tabname, char *first_name,
    FN_ref *first_ref, netobj ip)
{
	table_name = strdup(tabname);
	next_in_line = first_name;
	next_ref = first_ref;
	next_status = FN_SUCCESS;
	iter_pos = ip;
}

FN_bindinglist_hu::~FN_bindinglist_hu()
{
	free(table_name);
	free(next_in_line);
	delete next_ref;
	free(iter_pos.n_bytes);
}

FN_string*
FN_bindinglist_hu::next(FN_ref **ref, FN_status &status)
{
	FN_string *answer;

	if (next_status != FN_SUCCESS) {
		status.set(next_status);
		if (ref)
			*ref = 0;
		return (0);
	}

	if (next_in_line == 0) {
		next_status = FN_E_INVALID_ENUM_HANDLE;
		status.set_success();
		if (ref)
			*ref = 0;
		return (0);
	}

	answer = new FN_string((const unsigned char *)next_in_line);
	free(next_in_line);
	if (ref)
		*ref = next_ref;
	else
		delete next_ref;
	next_ref = 0;

	// This will
	// 1. read the next entry (name and reference)
	// 2. free iter_pos
	// 3. reassign iter_pos if any
	next_in_line = FNSP_read_next(table_name, iter_pos, next_status,
				    &next_ref);

	return (answer);
}



FN_bindinglist* FNSP_HUContext::c_list_bindings(const FN_string &name,
    FN_status_csvc &cstat)
{
	if (name.is_empty()) {
		// listing bindings in current context
		netobj iter_pos;
		unsigned int status;
		FN_ref *first_ref;
		const FN_string tab = my_address->get_table_name();
		const char *table_name = (const char *)tab.str();
		char *first = FNSP_read_first(table_name, iter_pos, status,
					    &first_ref);

		if (status != FN_SUCCESS) {
			cstat.set_error(status, *my_reference, name);
			return (0);
		}
		cstat.set_success();
		return (new FN_bindinglist_hu(table_name, first, first_ref,
					    iter_pos));
	} else {
		// resolve name and have list be performed there
		FN_ref *ref = resolve(name, 0, cstat);
		FN_composite_name empty_name((const unsigned char *)"");

		if (cstat.is_success()) {
			cstat.set(FN_E_SPI_CONTINUE, ref, 0, &empty_name);
			delete ref;
		}
	}

	return (0);
}


// Attribute operations

FN_attribute*
FNSP_HUContext::c_attr_get(const FN_string &name,
    const FN_identifier &id,
    FN_status_csvc& cs)
{
	FN_attribute *answer = 0;
	unsigned status;

	FNSP_Address *target_ctx = get_attribute_context(name, status);

	if (status == FN_SUCCESS) {
		answer = FNSP_get_attribute((*target_ctx), name, id, status);
		if (status == FN_SUCCESS)
			cs.set_success();
		else
			cs.set_error(status, *my_reference, name);
	} else
		cs.set_error(status, *my_reference, name);

	delete target_ctx;
	return (answer);
}

int
FNSP_HUContext::c_attr_modify(const FN_string &name,
    unsigned int flag,
    const FN_attribute &attribute,
    FN_status_csvc& cs)
{
	unsigned status;

	FNSP_Address *target_ctx = get_attribute_context(name, status);

	if (status == FN_SUCCESS) {
		switch (flag) {
		case FN_ATTR_OP_ADD:
			FNSP_remove_attribute((*target_ctx), name, &attribute);
			status = FNSP_set_attribute((*target_ctx), name,
			    attribute);
			break;

		case FN_ATTR_OP_ADD_EXCLUSIVE: {
			const FN_identifier *id = attribute.identifier();
			if (!(FNSP_get_attribute((*target_ctx), name,
			    (*id), status)))
				status = FNSP_set_attribute((*target_ctx),
				    name, attribute);
			else status = FN_E_NAME_IN_USE;
			break;
		}

		case FN_ATTR_OP_ADD_VALUES:
			status = FNSP_set_attribute((*target_ctx), name,
			    attribute);
			break;

		case FN_ATTR_OP_REMOVE:
			status = FNSP_remove_attribute(*target_ctx, name,
			    &attribute);
			break;

		case FN_ATTR_OP_REMOVE_VALUES:
			status = FNSP_remove_attribute_values(*target_ctx,
			    name, attribute);
			break;

		default:
			status = FN_E_OPERATION_NOT_SUPPORTED;
			break;
		}
		delete target_ctx;
	}

	// Checking the status after the modify operation
	if (status == FN_SUCCESS) {
		cs.set_success();
		return (1);
	} else {
		cs.set_error(status, *my_reference, name);
		return (0);
	}
}

FN_valuelist*
FNSP_HUContext::c_attr_get_values(const FN_string &name,
    const FN_identifier &id,
    FN_status_csvc &cs)
{
	FN_attribute *attribute = c_attr_get(name, id, cs);
	FN_valuelist_svc *answer = new FN_valuelist_svc(attribute);
	return (answer);
}

FN_attrset*
FNSP_HUContext::c_attr_get_ids(const FN_string &name,
    FN_status_csvc& cs)
{
	FN_attrset *answer = 0;
	unsigned status;

	FNSP_Address *target_ctx = get_attribute_context(name, status);

	if (status == FN_SUCCESS) {
		answer = FNSP_get_attrset((*target_ctx), name, status);
		if (status == FN_SUCCESS)
			cs.set_success();
		else
			cs.set_error(status, *my_reference, name);
		delete target_ctx;
		if (answer == 0)
			answer = new FN_attrset;
	} else
		cs.set_error(status, *my_reference, name);
	return (answer);
}

FN_multigetlist*
FNSP_HUContext::c_attr_multi_get(const FN_string &name,
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
FNSP_HUContext::c_attr_multi_modify(const FN_string &name,
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

// Attribute operations on nns
// (these are identical to the non-nns ones because names bound in HU are
// junctions).


FN_attribute*
FNSP_HUContext::c_attr_get_nns(const FN_string &name,
    const FN_identifier &id,
    FN_status_csvc &cs)
{
	return (c_attr_get(name, id, cs));
}

int
FNSP_HUContext::c_attr_modify_nns(const FN_string &name,
    unsigned int flags,
    const FN_attribute &id,
    FN_status_csvc &cs)
{
	return (c_attr_modify(name, flags, id, cs));
}

FN_valuelist*
FNSP_HUContext::c_attr_get_values_nns(const FN_string &name,
    const FN_identifier &id,
    FN_status_csvc &cs)
{
	return (c_attr_get_values(name, id, cs));
}

FN_attrset*
FNSP_HUContext::c_attr_get_ids_nns(const FN_string &name,
    FN_status_csvc &cs)
{
	return (c_attr_get_ids(name, cs));
}

FN_multigetlist*
FNSP_HUContext::c_attr_multi_get_nns(const FN_string &name,
    const FN_attrset *attrset,
    FN_status_csvc &cs)
{
	return (c_attr_multi_get(name, attrset, cs));
}

int
FNSP_HUContext::c_attr_multi_modify_nns(const FN_string &name,
    const FN_attrmodlist& modl,
    FN_attrmodlist** mod,
    FN_status_csvc& cs)
{
	return (c_attr_multi_modify(name, modl, mod, cs));
}

// Definition of 'resolve' that takes into account of possible configuration
// error.
//
// For example, context does not exist for a host, but host entry is in
// 'hosts' table.

FN_ref *
FNSP_HUContext::resolve(const FN_string &name,
    unsigned int lookup_flags, FN_status_csvc &cs)
{
	FN_ref *ref = FNSP_FlatContext::resolve(name, lookup_flags, cs);

	if (cs.code() == FN_E_NAME_NOT_FOUND)
		check_for_config_error(name, cs);

	return (ref);
}

FN_ref *
FNSP_HUContext::c_create_subcontext(const FN_string &name,
    FN_status_csvc& cstat)
{
	return (c_create_subcontext_nns(name, cstat));
}

FN_ref *
FNSP_HUContext::c_create_subcontext_nns(const FN_string &name,
    FN_status_csvc& cstat)
{
	unsigned status;
	FN_ref *newref = 0;

	if (name.is_empty()) {
		// there is no nns pointer associated with a HU context
		cstat.set_error(FN_E_OPERATION_NOT_SUPPORTED, *my_reference,
		    name);
		return (0);
	}

	newref = FNSP_create_and_bind(*my_address, name,
	    my_child_context_type, FNSP_normal_repr, status, 1);

	if (status == FN_SUCCESS)
		cstat.set_success();
	else
		cstat.set_error(status, *my_reference, name);

	return (newref);
}


FN_ref *
FNSP_HUContext::n_create_subcontext(const FN_string &name,
    unsigned context_type,
    unsigned representation_type,
    const FN_identifier *ref_type,
    FN_status &stat)
{
	return (FNSP_HUContext::n_create_subcontext_nns(name, context_type,
	    representation_type, ref_type, stat));
}


FN_ref *
FNSP_HUContext::n_create_subcontext_nns(const FN_string &name,
    unsigned context_type,
    unsigned representation_type,
    const FN_identifier *ref_type,
    FN_status &stat)
{
	if (name.is_empty() ||
	    context_type != my_child_context_type ||
	    representation_type != FNSP_normal_repr ||
	    ref_type != 0) {
		FN_composite_name cname(name);
		stat.set(FN_E_OPERATION_NOT_SUPPORTED, my_reference, 0,
		    &cname);
		return (0);
	}

	FN_status_csvc cstat;
	FN_ref *answer = c_create_subcontext_nns(name, cstat);
	if (cstat.is_success())
		stat.set_success();
	else
		stat.set(cstat.code(), cstat.resolved_ref(),
		    cstat.resolved_name(), cstat.remaining_name());
	return (answer);
}
