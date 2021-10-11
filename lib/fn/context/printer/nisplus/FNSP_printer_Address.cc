/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_printer_Address.cc	1.4 95/01/29 SMI"

#include <xfn/fn_p.hh>
#include "../FNSP_printer_Address.hh"

extern int
FNSP_printer_decompose_index_name(const FN_string &,
    FN_string &, FN_string &);

extern unsigned int
FNSP_set_access_flags(const FN_string &name, unsigned int &flags);

void
FNSP_printer_Address::init(const FN_string &contents)
{
	internal_name = contents;

	switch (ctx_type) {
	case FNSP_printername_context:
	case FNSP_printer_object:
		FNSP_printer_decompose_index_name(internal_name,
		    table_name, index_name);
		impl_type = FNSP_printer_entries_impl;
		break;
	default:
		fprintf(stderr, "Unknown context type in FNSP_Address\n");
		impl_type = FNSP_printer_null_impl;  // should be error;
		return;
	}
	access_flags = 0;
	FNSP_set_access_flags(table_name, access_flags);
}

FNSP_printer_Address::FNSP_printer_Address(const FN_string &contents,
    unsigned context_type,
    unsigned r_type)
{
	ctx_type = context_type;
	repr_type = r_type;

	init(contents);
}


FNSP_printer_Address::FNSP_printer_Address(const FN_ref_addr &addr)
{
	FN_string* iname = FNSP_address_to_internal_name(addr,
	    &ctx_type, &repr_type);
	if (iname) {
		init(*iname);
		delete iname;
	} else {
		fprintf(stderr, "Error in obtaining internal name\n");
	}
}

FNSP_printer_Address::FNSP_printer_Address(const FN_ref &ref)
{
	FN_string *iname = FNSP_reference_to_internal_name(ref,
	    &ctx_type, &repr_type);
	if (iname) {
		init(*iname);
		delete iname;
	} else {
		fprintf(stderr, "Error in obtaining internal name\n");
	}
}
