/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#ifndef _FNSP_WEAKSLASHCONTEXT_HH
#define	_FNSP_WEAKSLASHCONTEXT_HH

#pragma ident	"@(#)FNSP_WeakSlashContext.hh	1.8	94/11/28 SMI"

#include <xfn/fn_spi.hh>
#include <xfn/fn_p.hh>
#include "../FNSP_Address.hh"
#include "FNSP_Context.hh"

/* For:  service context */
class FNSP_WeakSlashContext :
public FN_ctx_asvc_weak_dynamic,
public FNSP_Context {
public:
	~FNSP_WeakSlashContext();
	FN_ref *get_ref(FN_status &)const;

	static FNSP_WeakSlashContext* from_address(const FN_ref_addr &,
	    const FN_ref &,
	    FN_status &stat);
private:
	FN_ref *resolve(const FN_string &name, unsigned int lookup_flags,
			FN_status_asvc&);

protected:
	FN_ref *my_reference;  /* encoded */
	FNSP_Address *my_address;  /* decoded */

	FNSP_WeakSlashContext(const FN_ref_addr &, const FN_ref &);

	// atomic name service interface
	FN_ref *a_lookup(const FN_string &name, unsigned int f,
	    FN_status_asvc&);
	FN_namelist* a_list_names(FN_status_asvc&);
	FN_bindinglist* a_list_bindings(FN_status_asvc&);
	int a_rename(const FN_string &name, const FN_composite_name &,
	    unsigned BindFlags, FN_status_asvc&);
	int a_bind(const FN_string &name, const FN_ref &,
	    unsigned BindFlags, FN_status_asvc&);
	int a_unbind(const FN_string &name, FN_status_asvc&);
	FN_ref *a_create_subcontext(const FN_string &name,
	    FN_status_asvc&);
	int a_destroy_subcontext(const FN_string &name, FN_status_asvc&);
	FN_attrset* a_get_syntax_attrs(FN_status_asvc&);
	// Attribute operations
	FN_attribute *a_attr_get(const FN_string &,
	    const FN_identifier &,
	    FN_status_asvc&);
	int a_attr_modify(const FN_string &,
	    unsigned int,
	    const FN_attribute&,
	    FN_status_asvc&);
	FN_valuelist *a_attr_get_values(const FN_string &,
	    const FN_identifier &,
	    FN_status_asvc&);
	FN_attrset *a_attr_get_ids(const FN_string &, FN_status_asvc&);
	FN_multigetlist *a_attr_multi_get(const FN_string &, const FN_attrset *,
	    FN_status_asvc&);
	int a_attr_multi_modify(const FN_string &, const FN_attrmodlist&,
	    FN_attrmodlist **,
	    FN_status_asvc&);

	FN_ref *a_lookup_nns(const FN_string&, unsigned int f,
	    FN_status_asvc&);
	int a_bind_nns(const FN_string&,
	    const FN_ref &, unsigned BindFlags, FN_status_asvc&);
	int a_unbind_nns(const FN_string&, FN_status_asvc&);
	int a_rename_nns(const FN_string&,
	    const FN_composite_name &newn, unsigned f, FN_status_asvc&);
	FN_ref *a_create_subcontext_nns(const FN_string&, FN_status_asvc&);
	int a_destroy_subcontext_nns(const FN_string&, FN_status_asvc&);

	// Attribute operations
	FN_attribute *a_attr_get_nns(const FN_string &,
	    const FN_identifier &,
	    FN_status_asvc&);
	int a_attr_modify_nns(const FN_string &,
	    unsigned int,
	    const FN_attribute&,
	    FN_status_asvc&);
	FN_valuelist *a_attr_get_values_nns(const FN_string &,
	    const FN_identifier &,
	    FN_status_asvc&);
	FN_attrset *a_attr_get_ids_nns(const FN_string &,
	    FN_status_asvc&);
	FN_multigetlist *a_attr_multi_get_nns(const FN_string &,
	    const FN_attrset *,
	    FN_status_asvc&);
	int a_attr_multi_modify_nns(const FN_string &,
	    const FN_attrmodlist&,
	    FN_attrmodlist **,
	    FN_status_asvc&);

	// implementation for FNSP_Context
	FN_ref *n_create_subcontext(const FN_string &atomic_name,
	    unsigned context_type,
	    unsigned representation_type,
	    const FN_identifier *ref_type,
	    FN_status &);
	FN_ref *n_create_subcontext_nns(const FN_string &atomic_name,
	    unsigned context_type,
	    unsigned representation_type,
	    const FN_identifier *ref_type,
	    FN_status &);
};

#endif	/* _FNSP_WEAKSLASHCONTEXT_HH */
