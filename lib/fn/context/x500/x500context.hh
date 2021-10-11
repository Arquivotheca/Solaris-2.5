/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_X500CONTEXT_HH
#define	_X500CONTEXT_HH

#pragma ident	"@(#)x500context.hh	1.1	94/12/05 SMI"


#include "x500dua.hh"


/*
 * The X500 context
 */

class CX500Context : public FN_ctx_csvc_weak_static
{

	FN_string	*context_prefix;
	FN_ref		*self_reference;
	CX500DUA	x500_dua;

	// to determine NS boundary
	FN_composite_name	*p_component_parser(const FN_composite_name &,
					FN_composite_name **rest,
					FN_status_psvc &s);

	// define virtual funcs for FN_ctx:
	FN_ref		*get_ref(FN_status &) const;

	// define virtual funcs
	FN_ref		*c_lookup(const FN_string &name, unsigned int,
				FN_status_csvc &);
	FN_namelist	*c_list_names(const FN_string &name, FN_status_csvc &);
	FN_bindinglist	*c_list_bindings(const FN_string &name,
				FN_status_csvc &);
	int		c_bind(const FN_string &name, const FN_ref &,
				unsigned bind_flags, FN_status_csvc &);
	int		c_unbind(const FN_string &name, FN_status_csvc &);
	FN_ref		*c_create_subcontext(const FN_string &name,
				FN_status_csvc &);
	int		c_destroy_subcontext(const FN_string &name,
				FN_status_csvc &);
	FN_ref		*c_lookup_nns(const FN_string &name, unsigned int f,
				FN_status_csvc &);
	FN_namelist	*c_list_names_nns(const FN_string &name,
				FN_status_csvc &);
	FN_bindinglist	*c_list_bindings_nns(const FN_string &name,
				FN_status_csvc &);
	int		c_bind_nns(const FN_string &name, const FN_ref &,
				unsigned bind_flags, FN_status_csvc &);
	int		c_unbind_nns(const FN_string &name, FN_status_csvc &);
	FN_ref		*c_create_subcontext_nns(const FN_string &name,
				FN_status_csvc &);
	int		c_destroy_subcontext_nns(const FN_string &name,
				FN_status_csvc &);
	int		c_attr_multi_modify_nns(const FN_string &,
				const FN_attrmodlist &, FN_attrmodlist **,
				FN_status_csvc &);
	FN_multigetlist	*c_attr_multi_get_nns(const FN_string &,
				const FN_attrset *, FN_status_csvc &);
	FN_attrset	*c_attr_get_ids_nns(const FN_string &,
				FN_status_csvc &);
	FN_valuelist	*c_attr_get_values_nns(const FN_string &,
				const FN_identifier &, FN_status_csvc &);
	int		c_attr_modify_nns(const FN_string &, unsigned int,
				const FN_attribute &, FN_status_csvc &);
	FN_attribute	*c_attr_get_nns(const FN_string &,
				const FN_identifier &, FN_status_csvc &);
	FN_attrset	*c_get_syntax_attrs_nns(const FN_string &,
				FN_status_csvc &);
	int		c_rename_nns(const FN_string &oldname,
				const FN_composite_name &newname,
				unsigned int exclusive, FN_status_csvc &);
	int		c_attr_multi_modify(const FN_string &,
				const FN_attrmodlist &, FN_attrmodlist **,
				FN_status_csvc &);
	FN_multigetlist	*c_attr_multi_get(const FN_string &, const FN_attrset *,
				FN_status_csvc &);
	FN_attrset	*c_attr_get_ids(const FN_string &, FN_status_csvc &);
	FN_valuelist	*c_attr_get_values(const FN_string &,
				const FN_identifier &, FN_status_csvc &);
	int		c_attr_modify(const FN_string &, unsigned int,
				const FN_attribute &, FN_status_csvc &);
	FN_attribute	*c_attr_get(const FN_string &, const FN_identifier &,
				FN_status_csvc &);
	FN_attrset	*c_get_syntax_attrs(const FN_string &name,
				FN_status_csvc &);
	int		c_rename(const FN_string &oldname,
				const FN_composite_name &newname,
				unsigned int exclusive, FN_status_csvc &);

public:
	CX500Context(const FN_ref_addr &addr, const FN_ref &ref, int &err);
	~CX500Context();
};


#endif	/* _X500CONTEXT_HH */
