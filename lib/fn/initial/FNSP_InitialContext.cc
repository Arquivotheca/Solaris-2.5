/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)FNSP_InitialContext.cc	1.7	94/11/20 SMI"

#include "FNSP_InitialContext.hh"
#include <xfn/fn_p.hh>

// This file contains the code implementing the context service operations
// of FNSP_InitialContext.  That is, those functions that are defined as
// virtual member functions in FN_ctx_single_component_static_svc and
// re-defined non-virtual in FNSP_InitialContext.

static const FN_string empty_string((unsigned char *)"");

static inline int
not_supported(const FN_string &name, FN_status_csvc &status)
{
	status.set(FN_E_OPERATION_NOT_SUPPORTED);
	status.set_remaining_name(&name);
	return (0);
}

#if 0
static inline int
continue_in_nns(const FN_string &name, FN_status_csvc &status)
{
	FN_ref *ref = c_lookup(name, 0, status);
	if (status.is_success()) {
		status.set_error(FN_E_CONTINUE, *ref, empty_string);
		if (ref) delete ref;
	}
	// Note: if the status was not Success, we just pass up the status
	// returned from the lookup.
	return (0);
}
#endif

// If first two components are "/..." (i.e. { "", "..."}), treat that
// equivalent as "...".  Otherwise, do same as for strong separation
// (i.e. returns copy of first component).
// 'rn' is set to remaining components.
FN_composite_name *
FNSP_InitialContext::p_component_parser(const FN_composite_name &n,
    FN_composite_name **rn,
    FN_status_psvc &ps)
{
	void *p;
	const FN_string *first_comp = n.first(p);
	ps.set_success();

	if (first_comp) {
		if (first_comp->is_empty()) {
			// check second component to see if it is "..."
			const FN_string *second_comp = n.next(p);
			if (second_comp &&
			    second_comp->compare(
			    (unsigned char *)"...") == 0)
				// skip empty component
				first_comp = second_comp;
			else {
				// encountered something other than '...'
				if (rn)
					// need to do this to reset 'p'
					first_comp = n.first(p);
			}
		}

		if (rn)
			*rn = n.suffix(p);
		// separate construction of answer (instead of making
		// one call to
		// FN_composite_name(const FN_string &)constructor)
		// so that the constructor try to parse it and eat up any
		// quotes/escapes inside the component.
		FN_composite_name *answer = new FN_composite_name();
		if (answer == 0)
			ps.set_code(FN_E_INSUFFICIENT_RESOURCES);
		else
			answer->append_comp(*first_comp);
		return (answer);
	} else {
		ps.set_code(FN_E_ILLEGAL_NAME);
		return (0);
	}
}

FN_ref *
FNSP_InitialContext::c_lookup_nns(const FN_string &name,
    unsigned int /* lookup_flags */, FN_status_csvc &status)
{
	FN_ref *ref = 0;
	unsigned status_code;
	Table* table = 0;
	Entry *e = 0;
	int i;

	if (name.is_empty()) {
		// There can be no nns associated with IC
		status.set(FN_E_NAME_NOT_FOUND);
		status.set_remaining_name(&name);
		return (0);
	}

	for (i = 0; e == 0 && i < FNSP_NUMBER_TABLES; i++) {
		if ((table = tables[i]) == 0)
			continue;
		e = table->find(name);
	}

	if (e) {
		ref = e->reference(status_code);
		status.set(status_code);
		status.set_remaining_name(&name);
	} else {
		status.set(FN_E_NAME_NOT_FOUND);
		status.set_remaining_name(&name);
	}

// %%% Must think about this;
// %%% What about thisuser == user/rosanna/ ?
	// Names in the initial context should never be links
	//	if (!(lookup_flags&FN_SPI_LEAVE_TERMINAL_LINK) &&
	//	    ref->is_link()) {
	//		status.set_continue_context(*ref, *this);
	//		delete ref;
	//		ref = 0;
	//	}

	return (ref);
}


FN_ref *
FNSP_InitialContext::c_lookup(const FN_string &name,
    unsigned int lookup_flags, FN_status_csvc &status)
{
	if (name.is_empty()) {
		// lookup of the empty name would normally return
		// a reference to this context.
		// The initial context has no reference.
		// lookup of the empty name is not supported.
		// note however that lookup_nns of the empty name
		// just returns
		// "not found", because that is effectively looking up "/".
		return ((FN_ref *)not_supported(name, status));
	}

	return (c_lookup_nns(name, lookup_flags, status));
}

FN_namelist*
FNSP_InitialContext::c_list_names(const FN_string &name,
    FN_status_csvc &status)
{
	if (!name.is_empty()) {
		// if the name is not empty, we must look it up and pass
		// FN_E_SPI_CONTINUE up with the reference

		FN_ref *ref = c_lookup(name, 0, status);
		if (status.is_success()) {
			status.set_error(FN_E_SPI_CONTINUE, *ref,
					    empty_string);
			delete ref;
		}

		// Note: if the status was not Success, we just pass
		// up the status returned from the lookup.
		return (0);
	}

	// Name was empty, we are  listing names in the IC

	FN_nameset *bound_names = new FN_nameset;
	if (bound_names == 0) {
		status.set(FN_E_INSUFFICIENT_RESOURCES);
		return (0);
	}
	IterationPosition iter_pos;
	Table* table = 0;
	Entry *e = 0;
	int i;

	for (i = 0; i < FNSP_NUMBER_TABLES; i++) {
		if ((table = tables[i]) == 0)
			continue;
		for (e = table->first(iter_pos);
		    e != NULL;
		    e = table->next(iter_pos)) {
			FN_ref *ref;
			unsigned status_code;
			// if the name in the entry is bound
			if (ref = e->reference(status_code)) {

				// add it to the set of bound names
				void *iter_pos;
				const FN_string *nn;

				for (nn = e->first_name(iter_pos);
				    nn != NULL;
				    nn = e->next_name(iter_pos)) {
					if (!(bound_names->add(*nn))) {
						status.set(
						  FN_E_INSUFFICIENT_RESOURCES);
						delete bound_names;
						delete ref;
						return (0);
					}
				}
				delete ref;
			}
		}
	}

	status.set_success();
	return (new FN_namelist_svc(bound_names));
}

FN_bindinglist *
FNSP_InitialContext::c_list_bindings(const FN_string &name,
    FN_status_csvc &status)
{
	if (!name.is_empty()) {
		// if the name is not empty, we must look it up and
		// pass FN_E_SPI_CONTINUE up with the reference

		FN_ref *ref = c_lookup(name, 0, status);
		if (status.is_success()) {
			status.set_error(FN_E_SPI_CONTINUE,
					 *ref, empty_string);
			delete ref;
		}
		return (0);
	}

	// Name was empty, listing bindings of IC
	FN_bindingset *bindings = new FN_bindingset;
	if (bindings == 0) {
		status.set(FN_E_INSUFFICIENT_RESOURCES);
		return (0);
	}

	IterationPosition iter_pos;
	FN_ref *ref;
	unsigned status_code;
	Table* table = 0;
	Entry *e = 0;
	int i;

	for (i = 0; i < FNSP_NUMBER_TABLES; i++) {
		if ((table = tables[i]) == 0)
			continue;

		for (e = table->first(iter_pos);
		    e != NULL;
		    e = table->next(iter_pos)) {
			// if the name in the entry is bound
			if (ref = e->reference(status_code)) {
				void *iter_pos;
				unsigned int couldnotadd = 0;
				const FN_string *nn;

				for (nn = e->first_name(iter_pos);
				    nn != NULL;
				    nn = e->next_name(iter_pos)) {
					// add all alias names
					if (!(bindings->add(*nn, *ref))) {
						status.set(
						  FN_E_INSUFFICIENT_RESOURCES);
						delete ref;
						delete bindings;
						break;
					}
				}
				delete ref;
			}
		}
	}

	if (!status.is_success()) {
		delete bindings;
		return (0);
	}

	status.set_success();
	return (new FN_bindinglist_svc(bindings));
}


// Flat, case-insensitive.
static const FN_syntax_standard
FNSP_InitialContext_syntax(FN_SYNTAX_STANDARD_DIRECTION_FLAT,
    FN_STRING_CASE_INSENSITIVE,
    0, 0, 0, 0);

// No syntax (doesn't really matter, default: Flat, case-sensitive.)
static const FN_syntax_standard
FNSP_no_syntax(FN_SYNTAX_STANDARD_DIRECTION_FLAT,
    FN_STRING_CASE_SENSITIVE,
    0, 0, 0, 0);

// not yet supported, but should be
FN_attrset*
FNSP_InitialContext::c_get_syntax_attrs(const FN_string &name,
    FN_status_csvc &status)
{
	FN_attrset* answer = 0;
	if (name.is_empty()) {
		// Asking for syntax of initial context itself
		answer = FNSP_InitialContext_syntax.get_syntax_attrs();
		status.set_success();
	} else {
		// Asking for syntax of contexts of names bound in IC
		FN_ref *ref = c_lookup_nns(name, 0, status);
		if (status.is_success()) {
			status.set_error(FN_E_SPI_CONTINUE, *ref,
					    empty_string);
			delete ref;
		}
	}

	if (answer == 0 && status.is_success())
		status.set(FN_E_INSUFFICIENT_RESOURCES);

	return (answer);
}

FN_attribute *
FNSP_InitialContext::c_attr_get(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	return ((FN_attribute *)not_supported(name, cs));
}

int
FNSP_InitialContext::c_attr_modify(const FN_string &name,
    unsigned int,
    const FN_attribute &,
    FN_status_csvc &cs)
{
	return (not_supported(name, cs));
}

FN_valuelist*
FNSP_InitialContext::c_attr_get_values(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	return ((FN_valuelist *)not_supported(name, cs));
}

FN_attrset*
FNSP_InitialContext::c_attr_get_ids(const FN_string &name,
    FN_status_csvc &cs)
{
	return ((FN_attrset *)not_supported(name, cs));
}

FN_multigetlist*
FNSP_InitialContext::c_attr_multi_get(const FN_string &name,
    const FN_attrset *,
    FN_status_csvc &cs)
{
	return ((FN_multigetlist *)not_supported(name, cs));
}

int
FNSP_InitialContext::c_attr_multi_modify(const FN_string &name,
    const FN_attrmodlist&,
    FN_attrmodlist**,
    FN_status_csvc &cs)
{
	return (not_supported(name, cs));
}


FN_namelist*
FNSP_InitialContext::c_list_names_nns(const FN_string &name,
    FN_status_csvc &status)
{
	FN_ref *ref = c_lookup_nns(name, 0, status);
	if (status.is_success()) {
		status.set(FN_E_SPI_CONTINUE, ref);
		if (ref) delete ref;
	}
	// Note: if the status was not Success, we just pass up the status
	// returned from the lookup.
	return (0);
}


FN_bindinglist*
FNSP_InitialContext::c_list_bindings_nns(const FN_string &name,
    FN_status_csvc &status)
{
	FN_ref *ref = c_lookup_nns(name, 0, status);
	if (status.is_success()) {
		status.set(FN_E_SPI_CONTINUE, ref);
		if (ref) delete ref;
	}
	// Note: if the status was not Success, we just pass up the status
	// returned from the lookup.
	return (0);
}

// %%% should bind_nns, unbind_nns, and rename_nns be unsupported???



int
FNSP_InitialContext::c_bind_nns(const FN_string &name, const FN_ref &,
    unsigned /* BindFlags */, FN_status_csvc &status)
{
	return (not_supported(name, status));
}

int
FNSP_InitialContext::c_unbind_nns(const FN_string &name, FN_status_csvc &status)
{
	return (not_supported(name, status));
}


int
FNSP_InitialContext::c_rename_nns(const FN_string &name,
    const FN_composite_name &,
    unsigned /* BindFlags */, FN_status_csvc &status)
{
	return (not_supported(name, status));
}


FN_ref *
FNSP_InitialContext::c_create_subcontext_nns(const FN_string &name,
    FN_status_csvc &status)
{
	return ((FN_ref *)not_supported(name, status));
}


int
FNSP_InitialContext::c_destroy_subcontext_nns(const FN_string &name,
    FN_status_csvc &status)
{
	return (not_supported(name, status));
}

FN_attrset*
FNSP_InitialContext::c_get_syntax_attrs_nns(const FN_string &name,
    FN_status_csvc &status)
{
	FN_ref *ref = c_lookup_nns(name, 0, status);
	if (status.is_success()) {
		status.set(FN_E_SPI_CONTINUE, ref);
		if (ref) delete ref;
	}
	// Note: if the status was not Success, we just pass up the status
	// returned from the lookup.
	return (0);
}



FN_attribute *
FNSP_InitialContext::c_attr_get_nns(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	return ((FN_attribute *)not_supported(name, cs));
}

int
FNSP_InitialContext::c_attr_modify_nns(const FN_string &name,
    unsigned int,
    const FN_attribute &,
    FN_status_csvc &cs)
{
	return (not_supported(name, cs));
}

FN_valuelist*
FNSP_InitialContext::c_attr_get_values_nns(const FN_string &name,
    const FN_identifier &,
    FN_status_csvc &cs)
{
	return ((FN_valuelist *)not_supported(name, cs));
}

FN_attrset*
FNSP_InitialContext::c_attr_get_ids_nns(const FN_string &name,
    FN_status_csvc &cs)
{
	return ((FN_attrset *)not_supported(name, cs));
}

FN_multigetlist*
FNSP_InitialContext::c_attr_multi_get_nns(const FN_string &name,
    const FN_attrset *,
    FN_status_csvc &cs)
{
	return ((FN_multigetlist *)not_supported(name, cs));
}

int
FNSP_InitialContext::c_attr_multi_modify_nns(const FN_string &name,
    const FN_attrmodlist&,
    FN_attrmodlist**,
    FN_status_csvc &cs)
{
	return (not_supported(name, cs));
}

int
FNSP_InitialContext::c_bind(const FN_string &name, const FN_ref &,
    unsigned /* BindFlags */, FN_status_csvc &status)
{
	return (not_supported(name, status));
}


int
FNSP_InitialContext::c_unbind(const FN_string &name, FN_status_csvc &status)
{
	return (not_supported(name, status));
}

int
FNSP_InitialContext::c_rename(const FN_string &name,
    const FN_composite_name &,
    unsigned /* BindFlags */, FN_status_csvc &status)
{
	return (not_supported(name, status));
}


FN_ref *
FNSP_InitialContext::c_create_subcontext(const FN_string &name,
    FN_status_csvc &status)
{
	return ((FN_ref *)not_supported(name, status));
}

int
FNSP_InitialContext::c_destroy_subcontext(const FN_string &name,
    FN_status_csvc &status)
{
	return (not_supported(name, status));
}


FN_ref *
FNSP_InitialContext::get_ref(FN_status &status) const
{
	status.set(FN_E_OPERATION_NOT_SUPPORTED);
	return (0);
}
