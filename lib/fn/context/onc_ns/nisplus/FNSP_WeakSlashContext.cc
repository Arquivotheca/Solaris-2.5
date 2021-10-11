/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_WeakSlashContext.cc	1.11 95/01/09 SMI"

#include "FNSP_WeakSlashContext.hh"
#include "fnsp_internal.hh"
#include "../FNSP_Syntax.hh"
#include <xfn/FN_valuelist_svc.hh>
#include <xfn/FN_multigetlist_svc.hh>

//  A FNSP_WeakSlashContext is derived from FN_ctx_asvc_weak_dynamic class.
//  A naming system composed of FNSP_WeakSlashContext supports a hierarchical
//  namespace with a slash-separated left-to-right syntax.
//  By the time processing gets to a FNSP_WeakSlashContext,
//  it only needs to deal  with 'atomic names'.  'nns pointers' may be
//  associated with each atomic  name.
//
//  The FNSP_WeakSlashContext itself may have an associatd nns pointer;
//  in this case, its binding could be found under the reserved name "FNS_nns"
//  in the context.  The bindings of names associated with the atomic names
//  are stored in the binding table of the atomic context.
//

static
const FN_syntax_standard *my_syntax = FNSP_Syntax(FNSP_service_context);

FNSP_WeakSlashContext::~FNSP_WeakSlashContext()
{
	if (my_reference) delete my_reference;
	if (my_address) delete my_address;
}


FNSP_WeakSlashContext::FNSP_WeakSlashContext(const FN_ref_addr &from_addr,
    const FN_ref &from_ref)
{
	my_reference = new FN_ref(from_ref);
	my_address = new FNSP_Address(from_addr);
	// check for null pointers
}

FNSP_WeakSlashContext*
FNSP_WeakSlashContext::from_address(const FN_ref_addr &from_addr,
    const FN_ref &from_ref,
    FN_status &stat)
{
	FNSP_WeakSlashContext *answer = new FNSP_WeakSlashContext(from_addr,
	    from_ref);

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
FNSP_WeakSlashContext::get_ref(FN_status &stat) const
{
	FN_ref *answer = new FN_ref(*my_reference);

	if (answer)
		stat.set_success();
	else
		stat.set(FN_E_INSUFFICIENT_RESOURCES);

	return (answer);
}

FN_ref *
FNSP_WeakSlashContext::resolve(const FN_string &name,
    unsigned int /* lookup_flags */, FN_status_asvc &astat)
{
	FN_ref *answer = 0;
	unsigned status;
	answer = FNSP_lookup_binding(*my_address, name, status);
	if (status == FN_SUCCESS)
		astat.set_success();
	else
		astat.set_error(status, my_reference, &name);
	return (answer);
}

FN_ref *
FNSP_WeakSlashContext::a_lookup(const FN_string &name,
    unsigned int lookup_flags,
    FN_status_asvc &astat)
{
	FN_ref *answer = 0;
	if (name.is_empty()) {
		// Return reference of current context
		answer = new FN_ref(*my_reference);
		if (answer)
			astat.set_success();
		else
			astat.set_error(FN_E_INSUFFICIENT_RESOURCES);
	} else
		answer = resolve(name, lookup_flags, astat);

	return (answer);
}

FN_namelist*
FNSP_WeakSlashContext::a_list_names(FN_status_asvc &astat)
{
	FN_nameset *answer = 0;
	unsigned status;
	// listing all names bound in current context
	answer = FNSP_list_names(*my_address, status);
	if (status == FN_SUCCESS) {
		astat.set_success();
	} else
		astat.set_error(status, my_reference);

	if (answer)
		return (new FN_namelist_svc(answer));
	else
		return (0);
}

FN_bindinglist*
FNSP_WeakSlashContext::a_list_bindings(FN_status_asvc &astat)
{
	FN_bindingset *answer = 0;
	unsigned status;
	// Get all bindings
	answer = FNSP_list_bindings(*my_address, status);

	if (status == FN_SUCCESS) {
		astat.set_success();
	} else
		astat.set_error(status, my_reference);

	if (answer)
		return (new FN_bindinglist_svc(answer));
	else
		return (0);
}

int
FNSP_WeakSlashContext::a_bind(const FN_string &name, const FN_ref &ref,
    unsigned BindFlags, FN_status_asvc &astat)
{
	unsigned status;
	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED;
	// cannot bind to self
	else
		status = FNSP_add_binding(*my_address, name, ref, BindFlags);
	if (status == FN_SUCCESS)
		astat.set_success();
	else
		astat.set_error(status, my_reference, &name);
	return (status == FN_SUCCESS);
}

int
FNSP_WeakSlashContext::a_unbind(const FN_string &name, FN_status_asvc &astat)
{
	unsigned status;
	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED; 	// cannot unbind self
	else
		status = FNSP_remove_binding(*my_address, name);

	if (status == FN_SUCCESS)
		astat.set_success();
	else
		astat.set_error(status, my_reference, &name);
	return (status == FN_SUCCESS);
}

int
FNSP_WeakSlashContext::a_rename(const FN_string &name,
    const FN_composite_name &newname,
    unsigned BindFlags, FN_status_asvc &astat)
{
	unsigned status;
	const FN_string *newtarget = 0;

	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED;
	// cannot rename self
	else {
		// get string form of new name and check for legal name
		if (newname.count() == 1) {
			void *ip;
			newtarget = newname.first(ip);
			if (newtarget == 0) {
				astat.set_error(FN_E_ILLEGAL_NAME,
						my_reference,
						newtarget);
				return (0);
			}
		} else {
			// do not support renaming to a composite name
			FN_string *newstr = newname.string();
			if (newstr) {
				astat.set_error(FN_E_ILLEGAL_NAME,
						my_reference,
						newstr);
				delete newstr;
			} else
				astat.set_error(FN_E_ILLEGAL_NAME,
				    my_reference);
			return (0);
		}

		status = FNSP_rename_binding(*my_address, name, *newtarget,
		    BindFlags);
	}
	if (status != FN_SUCCESS)
		astat.set_error(status, my_reference, &name);
	else
		astat.set_success();
	return (status == FN_SUCCESS);
}

FN_ref *
FNSP_WeakSlashContext::a_create_subcontext(const FN_string &name,
    FN_status_asvc &astat)
{
	FN_ref *newref = 0;
	unsigned status;
	if (name.is_empty()) {
		// cannot create self
		status = FN_E_OPERATION_NOT_SUPPORTED;
	} else {
		newref =
			FNSP_create_and_bind(*my_address,
			    name,
			    my_address->get_context_type(),
			    FNSP_normal_repr,
			    status,
			    1,
			    // Child context inherits parent's reference type.
			    my_reference->type());
	}
	if (status != FN_SUCCESS)
		astat.set_error(status, my_reference, &name);
	else
		astat.set_success();
	return (newref);
}

int
FNSP_WeakSlashContext::a_destroy_subcontext(const FN_string &name,
    FN_status_asvc &astat)
{
	unsigned status;
	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED;
	else
		status = FNSP_destroy_and_unbind(*my_address, name);

	if (status == FN_SUCCESS) {
		astat.set_success();
		return (1);
	} else if (status == FN_E_MALFORMED_REFERENCE) {
		// Try to destroy the child context with empty name
		FN_ref *child_ref = a_lookup(name, 0, astat);
		if (astat.code() == FN_E_NAME_NOT_FOUND) {
			astat.set_success();
			return (1);
		} else if ((!child_ref) || (!astat.is_success()))
			return (0);

		FN_ctx_svc *child_ctx = FN_ctx_svc::from_ref(*child_ref, astat);
		if ((!child_ctx) || (!astat.is_success())) {
			astat.set_error(FN_E_NOT_A_CONTEXT, my_reference,
			    &name);
			delete child_ref;
			return (0);
		}

		delete child_ref;
		child_ctx->destroy_subcontext((unsigned char *) "", astat);
		delete child_ctx;
		if (astat.is_success()) {
			a_unbind(name, astat);
			if (astat.is_success())
				return (1);
		}
		return (0);
	} else
		astat.set_error(status, my_reference, &name);
	return (status == FN_SUCCESS);
}

FN_attrset*
FNSP_WeakSlashContext::a_get_syntax_attrs(FN_status_asvc &astat)
{
	FN_attrset* answer = my_syntax->get_syntax_attrs();

	if (answer)
		astat.set_success();
	else
		astat.set_error(FN_E_INSUFFICIENT_RESOURCES);
	return (answer);
}

FN_attribute*
FNSP_WeakSlashContext::a_attr_get(const FN_string &aname,
    const FN_identifier &id,
    FN_status_asvc &as)
{
	unsigned status;
	FN_attribute *attr = 0;

	FN_attrset *attrset = FNSP_get_attrset(*my_address, aname, status);

	if (attrset == 0)
		status = FN_E_NO_SUCH_ATTRIBUTE;

	if (status != FN_SUCCESS) {
		as.set_error(status, my_reference, &aname);
		return (0);
	}

	// Get the required attribute
	const FN_attribute *old_attr = attrset->get(id);
	if (old_attr) {
		attr = new FN_attribute(*old_attr);
		as.set_success();
	} else
		as.set_error(FN_E_NO_SUCH_ATTRIBUTE, my_reference, &aname);

	delete attrset;
	return (attr);
}

int
FNSP_WeakSlashContext::a_attr_modify(const FN_string &aname,
    unsigned int flags,
    const FN_attribute &attr,
    FN_status_asvc &as)
{
	unsigned status = FNSP_modify_attribute(*my_address, aname,
	    attr, flags);
	if (status == FN_SUCCESS) {
		as.set_success();
		return (1);
	} else
		as.set_error(status, my_reference, &aname);
	return (0);
}

FN_valuelist*
FNSP_WeakSlashContext::a_attr_get_values(const FN_string &name,
    const FN_identifier &id,
    FN_status_asvc &as)
{
	FN_valuelist_svc *answer = 0;
	FN_attribute *attribute = a_attr_get(name, id, as);
	if (as.is_success())
		answer = new FN_valuelist_svc(attribute);

	// Varaible "attribute" should not be deleted
	return (answer);
}

FN_attrset*
FNSP_WeakSlashContext::a_attr_get_ids(const FN_string &aname,
    FN_status_asvc &as)
{
	unsigned status;

	// FNSP_get_attrset returns the values too, and the
	// multi_get function is dependent on this feature (or bug?)
	FN_attrset *attrset = FNSP_get_attrset(*my_address, aname, status);

	if (status != FN_SUCCESS) {
		as.set_error(status, my_reference, &aname);
		return (0);
	}

	if (attrset == 0)
		attrset = new FN_attrset;

	as.set_success();
	return (attrset);
}

FN_multigetlist*
FNSP_WeakSlashContext::a_attr_multi_get(const FN_string &name,
    const FN_attrset *attrset,
    FN_status_asvc &as)
{
	FN_multigetlist_svc *answer;

	// The assumption is that attr_get_ids returns the values too
	FN_attrset *result = a_attr_get_ids(name, as);
	if (!result)
		return (0);

	// If the request is to return all the attributues
	if (attrset == 0) {
		answer = new FN_multigetlist_svc(result);
		// The variable "result" should not be deleted
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
	// The variable "new_attrset" should not be deleted
	return (answer);
}

int
FNSP_WeakSlashContext::a_attr_multi_modify(const FN_string &name,
    const FN_attrmodlist &modlist,
    FN_attrmodlist **un_modlist,
    FN_status_asvc &as)
{
	void *ip;
	unsigned int mod_op, status;
	const FN_attribute *attribute;

	attribute = modlist.first(ip, mod_op);
	while (attribute) {
		status = a_attr_modify(name, mod_op, *attribute, as);
		if (!as.is_success())
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

FN_ref *
FNSP_WeakSlashContext::a_lookup_nns(const FN_string& name,
    unsigned int lookup_flags, FN_status_asvc &astat)
{
	if (name.is_empty()) {
		astat.set_error(FN_E_NAME_NOT_FOUND, my_reference, &name);
		return (0);
	} else
		return (a_lookup(name, lookup_flags, astat));
}

int
FNSP_WeakSlashContext::a_bind_nns(const FN_string& name, const FN_ref &ref,
    unsigned bind_flags, FN_status_asvc &astat)
{
	return (a_bind(name, ref, bind_flags, astat));
}

int
FNSP_WeakSlashContext::a_unbind_nns(const FN_string& name,
    FN_status_asvc &astat)
{
	return (a_unbind(name, astat));
}

int
FNSP_WeakSlashContext::a_rename_nns(const FN_string &name,
    const FN_composite_name &newname, unsigned rflags, FN_status_asvc &astat)
{
	return (a_rename(name, newname, rflags, astat));
}

FN_ref *
FNSP_WeakSlashContext::a_create_subcontext_nns(const FN_string& name,
    FN_status_asvc &astat)
{
	return (a_create_subcontext(name, astat));
}


int
FNSP_WeakSlashContext::a_destroy_subcontext_nns(const FN_string& name,
    FN_status_asvc &astat)
{
	return (a_destroy_subcontext(name, astat));
}

FN_attribute*
FNSP_WeakSlashContext::a_attr_get_nns(const FN_string &name,
    const FN_identifier &id,
    FN_status_asvc &as)
{
	return (a_attr_get(name, id, as));
}

int
FNSP_WeakSlashContext::a_attr_modify_nns(const FN_string &name,
    unsigned int flags,
    const FN_attribute &id,
    FN_status_asvc &as)
{
	return (a_attr_modify(name, flags, id, as));
}

FN_valuelist*
FNSP_WeakSlashContext::a_attr_get_values_nns(const FN_string &name,
    const FN_identifier &id,
    FN_status_asvc &as)
{
	return (a_attr_get_values(name, id, as));
}

FN_attrset*
FNSP_WeakSlashContext::a_attr_get_ids_nns(const FN_string &name,
    FN_status_asvc &as)
{
	return (a_attr_get_ids(name, as));
}

FN_multigetlist*
FNSP_WeakSlashContext::a_attr_multi_get_nns(const FN_string &name,
    const FN_attrset *attrset,
    FN_status_asvc &as)
{
	return (a_attr_multi_get(name, attrset, as));
}

int
FNSP_WeakSlashContext::a_attr_multi_modify_nns(const FN_string &name,
    const FN_attrmodlist &modl,
    FN_attrmodlist **mod,
    FN_status_asvc &as)
{
	return (a_attr_multi_modify(name, modl, mod, as));
}



/* ***************** FNSP_Context interfaces ************************* */

FN_ref *
FNSP_WeakSlashContext::n_create_subcontext(const FN_string &name,
    unsigned context_type,
    unsigned representation_type,
    const FN_identifier *ref_type,
    FN_status &stat)
{
	FN_composite_name cname(name);
	FN_ref *newref = 0;
	unsigned status;

	if (name.is_empty() || (representation_type != FNSP_normal_repr)) {
		status = FN_E_OPERATION_NOT_SUPPORTED;
	} else {
		// Child context inherits parent's reference type by default.
		if (ref_type == 0 &&
		    context_type == my_address->get_context_type()) {
			ref_type = my_reference->type();
		}
		newref = FNSP_create_and_bind(*my_address, name, context_type,
				representation_type, status, 1, ref_type);
	}
	if (status == FN_SUCCESS) {
		stat.set_success();
	} else {
		stat.set(status, my_reference, 0, &cname);
	}
	return (newref);
}

FN_ref *
FNSP_WeakSlashContext::n_create_subcontext_nns(const FN_string &name,
    unsigned context_type,
    unsigned representation_type,
    const FN_identifier *ref_type,
    FN_status &stat)
{
	return (n_create_subcontext(name, context_type,
	    representation_type, ref_type, stat));
}
