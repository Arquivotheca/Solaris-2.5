/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_FNSP_PRINTERNAME_CONTEXT_HH
#define	_FNSP_PRINTERNAME_CONTEXT_HH

#pragma ident	"@(#)FNSP_PrinternameContext.hh	1.4	94/11/29 SMI"

#include <xfn/fn_spi.hh>

FN_ref* get_service_ref_from_value(const FN_string& intname, char *value);

class FNSP_PrinternameContext : public FN_ctx_csvc_strong {
public:
	~FNSP_PrinternameContext();

	static FNSP_PrinternameContext* from_address(const FN_ref_addr &,
	    const FN_ref &, FN_status &stat);
	FN_ref* get_ref(FN_status &) const;

#ifdef DEBUG
	// used only for testing
	FNSP_PrinternameContext(const FN_string&);
	FNSP_PrinternameContext(const FN_ref&);
#endif

protected:
	FN_ref *my_reference;
	FN_attrset *my_syntax;

	// internal construction function
	FNSP_PrinternameContext(const FN_ref_addr&, const FN_ref&);

	virtual FN_ref* resolve(const FN_string&, FN_status_csvc&);
	virtual FN_nameset* list(FN_status_csvc&);
	virtual FN_bindingset* list_bs(FN_status_csvc&);

	// implementations for FNS Printername Context
	FN_ref* c_lookup(const FN_string &name, unsigned int f,
	    FN_status_csvc&);
	FN_namelist* c_list_names(const FN_string &name, FN_status_csvc&);
	FN_bindinglist* c_list_bindings(const FN_string &name, FN_status_csvc&);
	int c_bind(const FN_string &name, const FN_ref&,
	    unsigned BindFlags, FN_status_csvc&);
	int c_unbind(const FN_string &name, FN_status_csvc&);
	int c_rename(const FN_string &name, const FN_composite_name &,
	    unsigned BindFlags, FN_status_csvc &);
	FN_ref* c_create_subcontext(const FN_string &name, FN_status_csvc &);
	int c_destroy_subcontext(const FN_string &name, FN_status_csvc &);
	FN_attrset* c_get_syntax_attrs(const FN_string &name, FN_status_csvc &);

	// Attribute Operations
	FN_attribute *c_attr_get(const FN_string&,
	    const FN_identifier&, FN_status_csvc&);
	int c_attr_modify(const FN_string&,
	    unsigned int, const FN_attribute&, FN_status_csvc&);
	FN_valuelist *c_attr_get_values(const FN_string&,
	    const FN_identifier&, FN_status_csvc&);
	FN_attrset *c_attr_get_ids(const FN_string&, FN_status_csvc&);
	FN_multigetlist *c_attr_multi_get(const FN_string&,
	    const FN_attrset *, FN_status_csvc&);
	int c_attr_multi_modify(const FN_string&,
	    const FN_attrmodlist&, FN_attrmodlist **, FN_status_csvc&);

	FN_ref* c_lookup_nns(const FN_string& name,
	    unsigned int f, FN_status_csvc&);
	FN_namelist* c_list_names_nns(const FN_string& name, FN_status_csvc&);
	FN_bindinglist* c_list_bindings_nns(const FN_string &name,
	    FN_status_csvc&);
	int c_bind_nns(const FN_string& name, const FN_ref&,
	    unsigned BindFlags, FN_status_csvc&);
	int c_unbind_nns(const FN_string& name, FN_status_csvc&);
	int c_rename_nns(const FN_string& name, const FN_composite_name&,
	    unsigned BindFlags, FN_status_csvc&);
	FN_ref* c_create_subcontext_nns(const FN_string& name, FN_status_csvc&);
	int c_destroy_subcontext_nns(const FN_string& name, FN_status_csvc&);
	FN_attrset* c_get_syntax_attrs_nns(const FN_string& name,
	    FN_status_csvc&);

	// Attribute Operations
	FN_attribute *c_attr_get_nns(const FN_string&,
	    const FN_identifier&, FN_status_csvc&);
	int c_attr_modify_nns(const FN_string&,
	    unsigned int, const FN_attribute&, FN_status_csvc&);
	FN_valuelist *c_attr_get_values_nns(const FN_string&,
	    const FN_identifier&, FN_status_csvc&);
	FN_attrset *c_attr_get_ids_nns(const FN_string&, FN_status_csvc&);
	FN_multigetlist *c_attr_multi_get_nns(const FN_string&,
	    const FN_attrset *, FN_status_csvc&);
	int c_attr_multi_modify_nns(const FN_string&,
	    const FN_attrmodlist&, FN_attrmodlist **, FN_status_csvc&);
};
#endif	/* _FNSP_PRINTERNAMECONTEXT_HH */
