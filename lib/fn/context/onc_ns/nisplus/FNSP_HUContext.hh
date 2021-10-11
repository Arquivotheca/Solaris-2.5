/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#ifndef _FNSP_HUCONTEXT_HH
#define	_FNSP_HUCONTEXT_HH

#pragma ident	"@(#)FNSP_HUContext.hh	1.7	94/11/28 SMI"

#include <xfn/fn_spi.hh>
#include <xfn/fn_p.hh>
#include "../FNSP_Address.hh"
#include "FNSP_Context.hh"
#include "FNSP_FlatContext.hh"

// A HU naming system is one in which host or user names are bound.
// All names in the naming system are bound in a single context.
// The only thing that can be bound under a name is a nns pointer,
// and are implemented as junctions.
//
//  This is similar to a Flat naming system
//  1.  except for how names are resolved (some error checking for
//  config errors)
//  2.  attribute operations have their own implementations
//
//  The bindings of junctions
//  are stored in the binding table of the HU context.
//

/* For supporting HU naming systems */
class FNSP_HUContext : public FNSP_FlatContext
{
public:
	virtual ~FNSP_HUContext();

	FNSP_HUContext(const FN_string &,
	    unsigned context_type, unsigned child_context_type);

	FNSP_HUContext(const FN_ref &, unsigned child_context_type);

protected:
	FN_string *my_orgname;
	unsigned my_child_context_type;

	FNSP_HUContext(const FN_ref_addr &, const FN_ref &,
	    unsigned child_context_type);
	FN_ref *resolve(const FN_string &, unsigned int flags,
	    FN_status_csvc &status);

	// subclass must provide implementation for these
	virtual FNSP_Address *get_attribute_context(const FN_string &,
	    unsigned &status) = 0;
	virtual int check_for_config_error(const FN_string &,
					    FN_status_csvc &) = 0;

	FN_namelist* c_list_names(const FN_string &name, FN_status_csvc&);
	FN_bindinglist* c_list_bindings(const FN_string &name,
	    FN_status_csvc&);

	// Attribute Operations
	FN_attribute *c_attr_get(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	int c_attr_modify(const FN_string &,
	    unsigned int,
	    const FN_attribute&,
	    FN_status_csvc&);
	FN_valuelist *c_attr_get_values(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	FN_attrset *c_attr_get_ids(const FN_string &,
	    FN_status_csvc&);
	FN_multigetlist *c_attr_multi_get(const FN_string &,
	    const FN_attrset *,
	    FN_status_csvc&);
	int c_attr_multi_modify(const FN_string &,
	    const FN_attrmodlist&,
	    FN_attrmodlist **,
	    FN_status_csvc&);

	// Attribute NNS Operations
	FN_attribute *c_attr_get_nns(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	int c_attr_modify_nns(const FN_string &,
	    unsigned int,
	    const FN_attribute&,
	    FN_status_csvc&);
	FN_valuelist *c_attr_get_values_nns(const FN_string &,
	    const FN_identifier &,
	    FN_status_csvc&);
	FN_attrset *c_attr_get_ids_nns(const FN_string &,
	    FN_status_csvc&);
	FN_multigetlist *c_attr_multi_get_nns(const FN_string &,
	    const FN_attrset *,
	    FN_status_csvc&);
	int c_attr_multi_modify_nns(const FN_string &,
	    const FN_attrmodlist&,
	    FN_attrmodlist **,
	    FN_status_csvc&);


	// Creation routines

	FN_ref *c_create_subcontext_nns(const FN_string &name,
	    FN_status_csvc&);

	FN_ref *c_create_subcontext(const FN_string &name,
	    FN_status_csvc&);


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

#endif	/* _FNSP_HUCONTEXT_HH */
