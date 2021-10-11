/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#ifndef	_FNSP_FLATCONTEXT_HH
#define	_FNSP_FLATCONTEXT_HH

#pragma ident	"@(#)FNSP_FlatContext.hh	1.7	94/11/28 SMI"

#include <xfn/fn_spi.hh>
#include <xfn/fn_p.hh>
#include "FNSP_Context.hh"
#include "../FNSP_Address.hh"

/* For supporting flat naming systems; used for NamingSystemNames */

class FNSP_FlatContext : public FN_ctx_csvc_strong,
public FNSP_Context {
public:
	virtual ~FNSP_FlatContext();
	static FNSP_FlatContext* from_address(const FN_ref_addr &,
	    const FN_ref &,
	    FN_status &stat);
	FN_ref *get_ref(FN_status &)const;

	// probably only used for testing
	FNSP_FlatContext(const FN_string &,
			    unsigned context_type = FNSP_nsid_context);
	FNSP_FlatContext(const FN_ref &);

protected:
	FN_ref *my_reference;  /* encoded */
	FNSP_Address * my_address;  /* decoded */

	// internal functions
	FNSP_FlatContext(const FN_ref_addr &, const FN_ref &);
	virtual FN_ref *resolve(const FN_string &,
	    unsigned int flags, FN_status_csvc&);

	// implementations for NS_ContextServiceCompound
	FN_ref *c_lookup(const FN_string &name, unsigned int f,
	    FN_status_csvc&);
	virtual FN_namelist* c_list_names(const FN_string &n, FN_status_csvc&);
	virtual FN_bindinglist* c_list_bindings(const FN_string &name,
	    FN_status_csvc&);
	int c_bind(const FN_string &name, const FN_ref &,
	    unsigned BindFlags, FN_status_csvc&);
	int c_unbind(const FN_string &name, FN_status_csvc&);
	int c_rename(const FN_string &name, const FN_composite_name &,
	    unsigned BindFlags, FN_status_csvc&);
	virtual FN_ref *c_create_subcontext(const FN_string &name,
	    FN_status_csvc&);
	int c_destroy_subcontext(const FN_string &name, FN_status_csvc&);
	FN_attrset* c_get_syntax_attrs(const FN_string &name, FN_status_csvc&);

	// Attribute Operations
	virtual FN_attribute *c_attr_get(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	virtual int c_attr_modify(const FN_string &,
	    unsigned int,
	    const FN_attribute&,
	    FN_status_csvc&);
	virtual FN_valuelist *c_attr_get_values(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	virtual FN_attrset *c_attr_get_ids(const FN_string &,
	    FN_status_csvc&);
	virtual FN_multigetlist *c_attr_multi_get(const FN_string &,
	    const FN_attrset *,
	    FN_status_csvc&);
	virtual int c_attr_multi_modify(const FN_string &,
	    const FN_attrmodlist&,
	    FN_attrmodlist **,
	    FN_status_csvc&);

	FN_ref *c_lookup_nns(const FN_string &name, unsigned int f,
	    FN_status_csvc&);
	FN_namelist* c_list_names_nns(const FN_string &name, FN_status_csvc&);
	FN_bindinglist* c_list_bindings_nns(const FN_string &name,
	    FN_status_csvc&);
	int c_bind_nns(const FN_string &name, const FN_ref &,
	    unsigned BindFlags, FN_status_csvc&);
	int c_unbind_nns(const FN_string &name, FN_status_csvc&);
	int c_rename_nns(const FN_string &name, const FN_composite_name &,
	    unsigned BindFlags, FN_status_csvc&);
	virtual FN_ref *c_create_subcontext_nns(const FN_string &name,
	    FN_status_csvc&);
	int c_destroy_subcontext_nns(const FN_string &name, FN_status_csvc&);
	FN_attrset* c_get_syntax_attrs_nns(const FN_string &name,
	    FN_status_csvc&);

	// Attribute Operations
	virtual FN_attribute *c_attr_get_nns(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	virtual int c_attr_modify_nns(const FN_string &,
	    unsigned int,
	    const FN_attribute&,
	    FN_status_csvc&);
	virtual FN_valuelist *c_attr_get_values_nns(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	virtual FN_attrset *c_attr_get_ids_nns(const FN_string &,
	    FN_status_csvc&);
	virtual FN_multigetlist *c_attr_multi_get_nns(const FN_string &,
	    const FN_attrset *,
	    FN_status_csvc&);
	virtual int c_attr_multi_modify_nns(const FN_string &,
	    const FN_attrmodlist&,
	    FN_attrmodlist **,
	    FN_status_csvc&);

	// implementation for FNSP_Context
	virtual FN_ref *n_create_subcontext(const FN_string &atomic_name,
	    unsigned context_type,
	    unsigned representation_type,
	    const FN_identifier *ref_type,
	    FN_status &);
	virtual FN_ref *n_create_subcontext_nns(const FN_string &atomic_name,
	    unsigned context_type,
	    unsigned representation_type,
	    const FN_identifier *ref_type,
	    FN_status &);
};

#endif	/* _FNSP_FLATCONTEXT_HH */
