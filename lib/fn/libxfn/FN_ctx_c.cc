/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FN_ctx_c.cc	1.6 94/11/20 SMI"

#include <xfn/FN_ctx.hh>


extern "C"
void
fn_ctx_handle_destroy(FN_ctx_t *p)
{
	delete (FN_ctx *)p;
}

extern "C"
FN_ctx_t *
fn_ctx_handle_from_initial(FN_status_t *s)
{
	return ((FN_ctx_t *)FN_ctx::from_initial(
					*((FN_status *)s)));
}

extern "C"
FN_ctx_t *
fn_ctx_handle_from_ref(const FN_ref_t *ref, FN_status_t *s)
{
	return ((FN_ctx_t *)FN_ctx::from_ref(
		*((const FN_ref *)ref),
		*((FN_status *)s)));
}

extern "C"
FN_ref_t *
fn_ctx_get_ref(const FN_ctx_t *p, FN_status_t *s)
{
	return ((FN_ref_t *)
		((const FN_ctx *)p)->get_ref(*((FN_status *)s)));
}

extern "C"
FN_ref_t *
fn_ctx_lookup(FN_ctx_t *p, const FN_composite_name_t *name,
    FN_status_t *s)
{
	return ((FN_ref_t *)
		((FN_ctx *)p)->lookup(*((const FN_composite_name *)name),
			*((FN_status *)s)));
}

extern "C"
FN_namelist_t *
fn_ctx_list_names(
	FN_ctx_t *p,
	const FN_composite_name_t *name,
	FN_status_t *s)
{
	return ((FN_namelist_t*)((FN_ctx *)p)->list_names(
	    *((const FN_composite_name *)name), *((FN_status *)s)));
}


extern "C"
FN_string_t *
fn_namelist_next(FN_namelist_t *nl, FN_status_t *s)
{
	return ((FN_string_t*)(((FN_namelist *)nl)->next(*((FN_status *)s))));
}

extern "C"
void
fn_namelist_destroy(FN_namelist_t *nl, FN_status_t *)
{
	// %%% ignore s
	delete (FN_namelist *)nl;
}

extern "C"
FN_bindinglist_t*
fn_ctx_list_bindings(
	FN_ctx_t *p,
	const FN_composite_name_t *name,
	FN_status_t *s)
{
	return ((FN_bindinglist_t*)((FN_ctx *)p)->list_bindings(
	    *((const FN_composite_name *)name), *((FN_status *)s)));
}


extern "C"
FN_string_t *
fn_bindinglist_next(FN_bindinglist_t *nl, FN_ref_t** ref, FN_status_t *s)
{
	FN_ref* retref = 0;
	FN_string_t *answer = ((FN_string_t *)(((FN_bindinglist *)nl)->next(
	    &retref, *((FN_status *)s))));
	if (ref) {
		*ref = (FN_ref_t*)retref;
	}
	return (answer);
}

extern "C"
void
fn_bindinglist_destroy(FN_bindinglist_t *nl, FN_status_t *)
{
	// ignore s
	delete (FN_bindinglist *)nl;
}


extern "C"
int
fn_ctx_bind(FN_ctx_t *ctx,
	    const FN_composite_name_t *name,
	    const FN_ref_t *ref,
	    unsigned bind_flags,
	    FN_status_t *s)
{
	return (((FN_ctx *)ctx)->bind(*((const FN_composite_name *)name),
	    *((const FN_ref *)ref), bind_flags, *((FN_status *)s)));
}

extern "C"
int
fn_ctx_unbind(FN_ctx_t *ctx, const FN_composite_name_t *name, FN_status_t *s)
{
	return (((FN_ctx *)ctx)->unbind(*((const FN_composite_name *)name),
	    *((FN_status *)s)));
}

extern "C"
int
fn_ctx_rename(
	FN_ctx_t *ctx,
	const FN_composite_name_t *oldname,
	const FN_composite_name_t *newname,
	unsigned int exclusive,
	FN_status_t *s)
{
	return (((FN_ctx *)ctx)->rename(*((const FN_composite_name *)oldname),
	    *((const FN_composite_name *)newname), exclusive,
	    *((FN_status *)s)));
}

extern "C"
FN_ref_t *
fn_ctx_lookup_link(FN_ctx_t *p, const FN_composite_name_t *name, FN_status_t *s)
{
	return ((FN_ref_t *)((FN_ctx *)p)->lookup_link(
	    *((const FN_composite_name *)name), *((FN_status *)s)));
}

extern "C"
FN_ref_t *
fn_ctx_create_subcontext(FN_ctx_t *ctx, const FN_composite_name_t *name,
    FN_status_t *s)
{
	return ((FN_ref_t *)((FN_ctx *)ctx)->create_subcontext(
	    *((const FN_composite_name *)name), *((FN_status *)s)));
}

extern "C"
int
fn_ctx_destroy_subcontext(FN_ctx_t *p, const FN_composite_name_t *name,
    FN_status_t *s)
{
	return (((FN_ctx *)p)->destroy_subcontext(
	    *((const FN_composite_name *)name), *((FN_status *)s)));
}

extern "C"
FN_attrset_t *
fn_ctx_get_syntax_attrs(FN_ctx_t *p, const FN_composite_name_t *name,
			FN_status_t *s)
{
	return ((FN_attrset_t *)((FN_ctx *)p)->get_syntax_attrs(
	    *((const FN_composite_name *)name), *((FN_status *)s)));
}


/* Attribute: Functions to access and modify attributes */

extern "C"
FN_attribute_t *
fn_attr_get(FN_ctx_t *ctx, const FN_composite_name_t *name,
	    const FN_identifier_t *attr_id,
	    FN_status_t *status)
{
	return ((FN_attribute_t *)((FN_ctx *) ctx)->attr_get(
	    *((const FN_composite_name *) name),
	    *((const FN_identifier_t *) attr_id),
	    *((FN_status *) status)));
}

extern "C"
int fn_attr_modify(
	FN_ctx_t *ctx,
	const FN_composite_name_t *name,
	unsigned int mod_op,
	const FN_attribute_t *attr,
	FN_status_t *status)
{
	return ((int) ((FN_ctx *) ctx)->attr_modify(
	    *((const FN_composite_name *) name), mod_op,
	    *((const FN_attribute *) attr),
	    *((FN_status *) status)));
}

extern "C"
FN_valuelist_t *
fn_attr_get_values(
	FN_ctx_t *ctx,
	const FN_composite_name_t *name,
	const FN_identifier_t *attr_id,
	FN_status_t *status)
{
	return ((FN_valuelist_t *) ((FN_ctx *) ctx)->attr_get_values(
	    *((const FN_composite_name *) name),
	    *((const FN_identifier *) attr_id),
	    *((FN_status *) status)));
}

extern "C"
FN_attrvalue_t *
fn_valuelist_next(FN_valuelist_t *vl, FN_identifier_t **attr_syntax,
    FN_status_t *status)
{
	FN_identifier *id = 0;
	FN_attrvalue *answer = ((FN_valuelist *) vl)->next(&id,
	    *((FN_status *) status));
	if (id) {
		if (*attr_syntax)
			*attr_syntax = (FN_identifier_t *) id;
	}
	return ((FN_attrvalue_t *) answer);
}

extern "C"
void
fn_valuelist_destroy(FN_valuelist_t *vl, FN_status_t *)
{
	// ignore status
	delete ((FN_valuelist *) vl);
}
	

extern "C"
FN_attrset_t *
fn_attr_get_ids(FN_ctx_t *ctx, const FN_composite_name_t *name,
    FN_status_t *status)
{
	return ((FN_attrset_t *) ((FN_ctx *) ctx)->attr_get_ids(
	    *((const FN_composite_name *) name), *((FN_status *)status)));
}

extern "C"
FN_multigetlist_t *
fn_attr_multi_get(FN_ctx_t *ctx, const FN_composite_name_t *name,
    const FN_attrset_t *attr_ids, FN_status_t *status)
{
	FN_multigetlist_t *answer;

	answer = ((FN_multigetlist_t *) ((FN_ctx *) ctx)->attr_multi_get(
	    *((const FN_composite_name *) name),
	    (const FN_attrset *) attr_ids, *((FN_status *) status)));

	return (answer);
}

extern "C"
FN_attribute_t *
fn_multigetlist_next(FN_multigetlist_t *ml, FN_status_t *s)
{
	return ((FN_attribute_t *) (((FN_multigetlist *) ml)->next(*((FN_status *)s))));
}

extern "C"
void
fn_multigetlist_destroy(FN_multigetlist_t *ml, FN_status_t *)
{
	// ignore status
	delete ((FN_multigetlist *) ml);
}

extern "C"
int fn_attr_multi_modify(
	FN_ctx_t *ctx,
	const FN_composite_name_t *name,
	const FN_attrmodlist_t *mods,
	FN_attrmodlist_t **unexecuted_mods,
	FN_status_t *status)
{
	return ((int)((FN_ctx *) ctx)->attr_multi_modify(
	    *((const FN_composite_name *) name),
	    *((const FN_attrmodlist *) mods),
	    ((FN_attrmodlist **) unexecuted_mods),
	    *((FN_status *) status)));
}
