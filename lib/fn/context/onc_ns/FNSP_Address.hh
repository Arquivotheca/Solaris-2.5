/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#ifndef _FNSP_ADDRESS_HH
#define	_FNSP_ADDRESS_HH

#pragma ident "@(#)FNSP_Address.hh	1.3 95/01/29 SMI"


#include <xfn/fn_p.hh>

enum FNSP_implementation_type {
	FNSP_directory_impl,
	FNSP_single_table_impl,
	FNSP_shared_table_impl,	// shares table with entries
	FNSP_entries_impl,	// ctx implemented using only entries
	FNSP_null_impl
};

class FNSP_Address {
	unsigned ctx_type;	// org | user | host ...
	unsigned repr_type;	// normal | merged
	FNSP_implementation_type impl_type;
	unsigned int access_flags;	// flags required for access
	FN_string internal_name;	// whole name
	FN_string index_name;		// index part, if any
	FN_string table_name;		// table part
					// internal_name if no ind

private:
    void init(const FN_string& contents);

public:
	const FN_string& get_internal_name() const { return internal_name; }
	const FN_string& get_index_name() const { return index_name; }
	const FN_string& get_table_name() const { return table_name; }
	unsigned get_context_type() const { return ctx_type; }
	unsigned get_repr_type() const { return repr_type; }
	unsigned get_access_flags() const { return access_flags; }
	FNSP_implementation_type get_impl_type() const { return impl_type; }

	// used
	FNSP_Address(const FN_ref_addr&);
	FNSP_Address(const FN_string&, unsigned ctx_type,
	    unsigned = FNSP_normal_repr);

	// used by some constructors in Org and Flat
	// probably only for testing
	FNSP_Address(const FN_ref&);
};

#endif // _FNSP_ADDRESS_HH
