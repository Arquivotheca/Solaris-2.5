/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_Address.cc	1.6 95/01/29 SMI"

#include "../FNSP_Address.hh"

// from fnsp_internal.hh
extern int
FNSP_decompose_index_name(const FN_string& src,
    FN_string& tabname, FN_string& indexname);

extern unsigned int
FNSP_set_access_flags(const FN_string &name, unsigned int &flags);

void
FNSP_Address::init(const FN_string &contents)
{
	internal_name = contents;

	switch (ctx_type) {
	case FNSP_organization_context:
		impl_type = FNSP_directory_impl;
		table_name = internal_name;
		break;
	case FNSP_hostname_context:
	case FNSP_username_context:
	case FNSP_enterprise_context:
		impl_type = FNSP_single_table_impl;
		table_name = internal_name;
		break;
	case FNSP_generic_context:
	case FNSP_service_context:
	case FNSP_nsid_context:
	case FNSP_user_context:
	case FNSP_host_context:
	case FNSP_site_context:
		if (FNSP_decompose_index_name(internal_name,
		    table_name, index_name))
			impl_type = FNSP_entries_impl;
		else
			impl_type = FNSP_shared_table_impl;
		break;
	default:
		impl_type = FNSP_null_impl;  // should be error;
		return;
	}

	access_flags = 0;
	FNSP_set_access_flags(table_name, access_flags);
}

FNSP_Address::FNSP_Address(const FN_string &contents,
    unsigned context_type,
    unsigned r_type)
{
	ctx_type = context_type;
	repr_type = r_type;

	init(contents);
}


FNSP_Address::FNSP_Address(const FN_ref_addr &addr)
{
	FN_string* iname = FNSP_address_to_internal_name(addr,
	    &ctx_type,
	    &repr_type);
	if (iname) {
		init(*iname);
		delete iname;
	} else {
		ctx_type = repr_type = 0;
		impl_type = FNSP_single_table_impl;
	}
}

FNSP_Address::FNSP_Address(const FN_ref &ref)
{
	FN_string *iname = FNSP_reference_to_internal_name(ref,
	    &ctx_type,
	    &repr_type);
	if (iname) {
		init(*iname);
		delete iname;
	} else {
		ctx_type = repr_type = 0;
		impl_type = FNSP_single_table_impl;
	}
}
