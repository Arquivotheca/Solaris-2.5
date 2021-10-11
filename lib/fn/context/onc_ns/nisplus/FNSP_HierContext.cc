/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_HierContext.cc	1.9 95/01/30 SMI"

#include "FNSP_HierContext.hh"
#include "fnsp_internal.hh"
#include <xfn/FN_valuelist_svc.hh>
#include <xfn/FN_multigetlist_svc.hh>

//  A FNSP_HierContext is derived from NS_ServiceContextAtomic.
//  A naming system composed of FNSP_HierContext supports a hierarchical
//  name space.  By the time processing gets to a FNSP_HierContext,
//  it only needs to deal  with 'atomic names'.  'nns pointers' may be
//  associated with each atomic  name.
//
//  The FNSP_HierContext itself may have an associatd nns pointer;
//  in this case, its binding could be found under the reserved name "FNS_nns"
//  in the context.  The bindings of names associated with the atomic names
//  are stored in the binding table of the atomic context.
//

static const FN_string empty_name((const unsigned char *)"");

FNSP_HierContext::~FNSP_HierContext()
{
	if (my_reference) delete my_reference;
	if (my_address) delete my_address;
}


FNSP_HierContext::FNSP_HierContext(const FN_ref_addr &from_addr,
    const FN_ref &from_ref)
{
	my_reference = new FN_ref(from_ref);

	my_address = new FNSP_Address(from_addr);

	// check for null pointers
}


FN_ref *
FNSP_HierContext::get_ref(FN_status &stat) const
{
	FN_ref *answer = new FN_ref(*my_reference);

	if (answer)
		stat.set_success();
	else
		stat.set(FN_E_INSUFFICIENT_RESOURCES);

	return (answer);
}

FN_ref *
FNSP_HierContext::resolve(const FN_string &name, FN_status_asvc &astat)
{
	FN_ref *answer = 0;
	unsigned status;
	answer = FNSP_lookup_binding(*my_address, name, status);
	if (status == FN_SUCCESS)
		astat.set_success();
	else if (name.compare(FNSP_nns_name) == 0)
		astat.set_error(status, my_reference);
	else
		astat.set_error(status, my_reference, &name);
	return (answer);
}

FN_string *
FNSP_HierContext::c_component_parser(const FN_string &n,
    FN_string **rest,
    FN_status_csvc& stat)
{
	FN_compound_name* parsed_name =  new
	    FN_compound_name_standard(*my_syntax, n);
	FN_string *answer = 0;

	if (parsed_name) {
		void *iter_pos;
		const FN_string *fst = parsed_name->first(iter_pos);
		if (rest) {
			FN_compound_name* rc = parsed_name->suffix(iter_pos);
			if (rc) {
				*rest = rc->string();
				delete rc;
			} else
				*rest = 0;
		}
		answer = new FN_string(*fst);
		delete parsed_name;
		if (answer == 0)
			stat.set(FN_E_INSUFFICIENT_RESOURCES);
		else
			stat.set_success();
	} else {
		stat.set(FN_E_ILLEGAL_NAME);
	}

	return (answer);
}

FN_ref *
FNSP_HierContext::a_lookup(const FN_string &name,
    unsigned int /* lookup_flags */,
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
		answer = resolve(name, astat);

	return (answer);
}

FN_namelist*
FNSP_HierContext::a_list_names(FN_status_asvc &astat)
{
	FN_nameset *answer = 0;
	unsigned status;
	// listing all names bound in current context
	answer = FNSP_list_names(*my_address, status);
	if (status == FN_SUCCESS) {
		if (answer)
			answer->remove(FNSP_nns_name);
		astat.set_success();
	} else
		astat.set_error(status, my_reference);

	if (answer)
		return (new FN_namelist_svc(answer));
	else
		return (0);
}

FN_bindinglist*
FNSP_HierContext::a_list_bindings(FN_status_asvc &astat)
{
	FN_bindingset *answer = 0;
	unsigned status;
	// Get all bindings
	answer = FNSP_list_bindings(*my_address, status);

	if (status == FN_SUCCESS) {
		if (answer)
			answer->remove(FNSP_nns_name);
		astat.set_success();
	} else
		astat.set_error(status, my_reference);

	if (answer)
		return (new FN_bindinglist_svc(answer));
	else
		return (0);
}

int
FNSP_HierContext::a_bind(const FN_string &name, const FN_ref &ref,
    unsigned BindFlags, FN_status_asvc &astat)
{
	// should do some checks against FNSP_nns_name ???
	unsigned status;
	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED; 	// cannot bind to self
	else
		status = FNSP_add_binding(*my_address, name, ref, BindFlags);

	if (status == FN_SUCCESS)
		astat.set_success();
	else
		astat.set_error(status, my_reference, &name);
	return (status == FN_SUCCESS);
}

int
FNSP_HierContext::a_unbind(const FN_string &name, FN_status_asvc &astat)
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
FNSP_HierContext::a_rename(const FN_string &name,
    const FN_composite_name &newname,
    unsigned BindFlags, FN_status_asvc &astat)
{
	// should do some checks against FNSP_nns_name ???
	unsigned status;
	FN_string *newtarget = 0;

	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED; 	// cannot rename self
	else {
		// get string form of new name and check for legal name
		if (newname.count() == 1) {
			void *ip;
			const FN_string *newstr = newname.first(ip);
			FN_status_csvc cstat;
			FN_string**rest = 0;

			newtarget = (newstr ? c_component_parser(*newstr,
			    rest, cstat) : 0);

			if (newtarget && rest == 0) {
				/* do nothing */
				;
			} else {
				// do not support renaming to a compound name
				// or an empty name,
				// or an otherwise illegal name
				if (newtarget) {
					delete newtarget;
					newtarget = 0;
				}

				if (rest) {
					delete rest;
					rest = 0;
				}
				FN_string *newname_str = newname.string();
				if (newname_str) {
					astat.set_error(FN_E_ILLEGAL_NAME,
							my_reference,
							newname_str);
					delete newname_str;
				} else
					astat.set_error(FN_E_ILLEGAL_NAME,
							my_reference);
				return (0);
			}
		} else {
			// do not support renaming to a composite name
			FN_string *newname_str = newname.string();
			if (newname_str) {
				astat.set_error(FN_E_ILLEGAL_NAME,
						my_reference,
						newname_str);
				delete newname_str;
			} else
				astat.set_error(FN_E_ILLEGAL_NAME,
						my_reference);
			return (0);
		}

		status = FNSP_rename_binding(*my_address, name,
		    *newtarget, BindFlags);
	}
	if (status == FN_SUCCESS)
		astat.set_success();
	else
		astat.set_error(status, my_reference, &name);
	if (newtarget)
		delete newtarget;
	return (status == FN_SUCCESS);
}

FN_ref *
FNSP_HierContext::a_create_subcontext(const FN_string &name,
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
		    status, 1);
	}
	if (status == FN_SUCCESS)
		astat.set_success();
	else
		astat.set_error(status, my_reference, &name);
	return (newref);
}

int
FNSP_HierContext::a_destroy_subcontext(const FN_string &name,
    FN_status_asvc &astat)
{
	unsigned status;
	if (name.is_empty())
		status = FN_E_OPERATION_NOT_SUPPORTED;
	else
		status = FNSP_destroy_and_unbind(*my_address, name);

	if (status == FN_SUCCESS)
		astat.set_success();
	else
		astat.set_error(status, my_reference, &name);
	return (status == FN_SUCCESS);
}

FN_attrset*
FNSP_HierContext::a_get_syntax_attrs(FN_status_asvc& astat)
{
	FN_attrset* answer = my_syntax->get_syntax_attrs();

	if (answer)
		astat.set_success();
	else
		astat.set_error(FN_E_INSUFFICIENT_RESOURCES);
	return (answer);
}

FN_attribute*
FNSP_HierContext::a_attr_get(const FN_string &aname,
    const FN_identifier &id,
    FN_status_asvc &as)
{
	unsigned status;
	FN_attribute *answer = 0;

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
		answer = new FN_attribute(*old_attr);
		as.set_success();
	} else
		as.set_error(FN_E_NO_SUCH_ATTRIBUTE, my_reference, &aname);

	delete attrset;
	return (answer);
}

int
FNSP_HierContext::a_attr_modify(const FN_string &aname,
    unsigned int flags,
    const FN_attribute &attr,
    FN_status_asvc& as)
{
	unsigned status = FNSP_modify_attribute(*my_address, aname,
	    attr, flags);
	if (status == FN_SUCCESS) {
		as.set_success();
		return (1);
	}
	as.set_error(status, my_reference, &aname);
	return (0);
}

FN_valuelist*
FNSP_HierContext::a_attr_get_values(const FN_string &name,
    const FN_identifier &id,
    FN_status_asvc &as)
{
	FN_valuelist_svc *answer = 0;
	FN_attribute *attribute = a_attr_get(name, id, as);
	if (as.is_success())
		answer = new FN_valuelist_svc(attribute);

	return (answer);
}

FN_attrset*
FNSP_HierContext::a_attr_get_ids(const FN_string &aname, FN_status_asvc &as)
{
	unsigned status;
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
FNSP_HierContext::a_attr_multi_get(const FN_string &name,
    const FN_attrset *attrset,
    FN_status_asvc &as)
{
	FN_multigetlist_svc *answer;

	FN_attrset *result = a_attr_get_ids(name, as);
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
FNSP_HierContext::a_attr_multi_modify(const FN_string &name,
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
FNSP_HierContext::a_lookup_nns(const FN_string &name,
    unsigned int /* lookup_flags */, FN_status_asvc &astat)
{
	if (name.is_empty()) {
		FN_ref *answer = resolve(FNSP_nns_name, astat);
		return (answer);
	} else {
		// resolve name first and then operate on its nns
		FN_ref *name_ref = resolve(name, astat);
		if (astat.is_success()) {
			astat.set_continue(*name_ref, *my_reference,
			    &empty_name);
			delete name_ref;
		}
		return (0);
	}
}

int
FNSP_HierContext::a_bind_nns(const FN_string &name,
    const FN_ref &ref, unsigned bind_flags, FN_status_asvc &astat)
{
	if (name.is_empty()) {
		unsigned status;
		status = FNSP_add_binding(*my_address, FNSP_nns_name,
					    ref, bind_flags);

		if (status == FN_SUCCESS)
			astat.set_success();
		else
			astat.set_error(status, my_reference);
		return (status == FN_SUCCESS);
	} else {
		// resolve name first and then operate on nns there
		FN_ref *name_ref = resolve(name, astat);
		if (astat.is_success()) {
			astat.set_continue(*name_ref, *my_reference,
			    &empty_name);
			delete name_ref;
		}
		return (0);
	}
}

int
FNSP_HierContext::a_unbind_nns(const FN_string &name, FN_status_asvc &astat)
{
	if (name.is_empty()) {
		unsigned status;
		status = FNSP_remove_binding (*my_address, FNSP_nns_name);

		if (status == FN_SUCCESS)
			astat.set_success();
		else
			astat.set_error(status, my_reference);
		return (status == FN_SUCCESS);
	} else {
		// resolve name first and then operate on its nns
		FN_ref *name_ref = resolve(name, astat);
		if (astat.is_success()) {
			astat.set_continue(*name_ref, *my_reference,
			    &empty_name);
			delete name_ref;
		}
		return (0);
	}
}

int
FNSP_HierContext::a_rename_nns(const FN_string &name,
    const FN_composite_name &, unsigned, FN_status_asvc& astat)
{
	if (name.is_empty()) {
		astat.set_error(FN_E_OPERATION_NOT_SUPPORTED);
		return (0);
	} else {
		// resolve name first and then operate on its nns
		// even though rename of nns is not supported, this
		// resolution step could point out other errors
		FN_ref *name_ref = resolve(name, astat);
		if (astat.is_success()) {
			astat.set_continue(*name_ref, *my_reference,
			    &empty_name);
			delete name_ref;
		}
		return (0);
	}
}

FN_ref *
FNSP_HierContext::a_create_subcontext_nns(const FN_string &name,
    FN_status_asvc &astat)
{
	if (name.is_empty()) {
		unsigned status;
		FN_ref *newref =
			FNSP_create_and_bind(*my_address, FNSP_nns_name,
			    FNSP_nsid_context, FNSP_normal_repr, status, 0);
		if (status == FN_SUCCESS)
			astat.set_success();
		else
			astat.set_error(status, my_reference);
		return (newref);
	} else {
		// resolve name first and then operate on its nns
		FN_ref *name_ref = resolve(name, astat);
		if (astat.is_success()) {
			astat.set_continue(*name_ref, *my_reference,
			    &empty_name);
			delete name_ref;
		}
		return (0);
	}
}

int
FNSP_HierContext::a_destroy_subcontext_nns(const FN_string &name,
    FN_status_asvc &astat)
{

	if (name.is_empty()) {
		unsigned status;
		status = FNSP_destroy_and_unbind(*my_address, FNSP_nns_name);

		if (status == FN_SUCCESS)
			astat.set_success();
		else
			astat.set_error(status, my_reference);
		return (status == FN_SUCCESS);
	} else {
		// resolve 'name'
		FN_ref *name_ref = resolve(name, astat);
		if (astat.is_success())
			astat.set_continue(*name_ref, *my_reference,
			    &empty_name);
		return (0);
	}
}

FN_attribute*
FNSP_HierContext::a_attr_get_nns(const FN_string &name,
    const FN_identifier &id,
    FN_status_asvc &as)
{
	return (a_attr_get(name, id, as));
}

int
FNSP_HierContext::a_attr_modify_nns(const FN_string &name,
    unsigned int flags,
    const FN_attribute &id,
    FN_status_asvc& as)
{
	return (a_attr_modify(name, flags, id, as));
}

FN_valuelist*
FNSP_HierContext::a_attr_get_values_nns(const FN_string &name,
    const FN_identifier &id,
    FN_status_asvc &as)
{
	return (a_attr_get_values(name, id, as));
}

FN_attrset*
FNSP_HierContext::a_attr_get_ids_nns(const FN_string &name,
    FN_status_asvc &as)
{
	return (a_attr_get_ids(name, as));
}

FN_multigetlist*
FNSP_HierContext::a_attr_multi_get_nns(const FN_string &name,
    const FN_attrset *attrset,
    FN_status_asvc &as)
{
	return (a_attr_multi_get(name, attrset, as));
}

int
FNSP_HierContext::a_attr_multi_modify_nns(const FN_string &name,
    const FN_attrmodlist &modl,
    FN_attrmodlist **mod,
    FN_status_asvc &as)
{
	return (a_attr_multi_modify(name, modl, mod, as));
}

FN_ref *
FNSP_HierContext::n_create_subcontext(const FN_string &name,
    unsigned context_type,
    unsigned representation_type,
    const FN_identifier *ref_type,
    FN_status &stat)
{
	if (name.is_empty() ||
	    context_type != my_address->get_context_type() ||
	    representation_type != FNSP_normal_repr ||
	    ref_type != 0) {
		FN_composite_name cname(name);
		stat.set(FN_E_OPERATION_NOT_SUPPORTED, my_reference, 0,
		    &cname);
		return (0);
	}

	// Name need not be atomic
	FN_ref *answer = FN_ctx_svc::create_subcontext(name, stat);
	return (answer);
}

FN_ref *
FNSP_HierContext::n_create_subcontext_nns(const FN_string &name,
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

	FN_ref *answer = 0;
	FN_status_asvc astat;
	if (name.is_empty()) {
		answer = a_create_subcontext_nns(name, astat);
		if (astat.is_success())
			stat.set_success();
		else
			stat.set(astat.code(), my_reference, 0, &cname);
	} else {
		// Name need not be atomic
		FN_composite_name nnsname(name);
		FN_composite_name empty_cname((unsigned char *)"");
		nnsname.append_name(empty_cname);  // put back nns
		FN_status nstat;

		answer = FN_ctx_svc::create_subcontext(nnsname, nstat);
		// take out nns from remaining name if any
		const FN_composite_name *rname = nstat.remaining_name();
		if (rname) {
			void *iter;
			const FN_string *lastpart = rname->last(iter);
			FN_composite_name *real_last = rname->prefix(iter);
			stat.set(nstat.code(), nstat.resolved_ref(), 0,
			    real_last);
		} else
			stat.set(nstat.code(), nstat.resolved_ref(), 0, 0);
	}
	return (answer);
}
