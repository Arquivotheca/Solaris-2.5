/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FN_compound_name.cc	1.5 94/08/07 SMI"

#include <stdio.h>
#include <sys/param.h>

#include <xfn/FN_compound_name.hh>

#include "fns_symbol.hh"


FN_compound_name::~FN_compound_name()
{
}

typedef FN_compound_name_t* (*from_attrset_func)(const FN_attrset_t*,
    const FN_string_t*, FN_status_t*);

// construct and initialize an FN_compound_name
FN_compound_name *
FN_compound_name::from_syntax_attrs(
	const FN_attrset &a,
	const FN_string &n,
	FN_status &s)
{
	void *fh;
	char mname[MAXPATHLEN], fname[MAXPATHLEN];
	const FN_attribute *syn_attr;
	const FN_attrvalue *tv;

	const FN_attrset_t *at = (const FN_attrset_t*)&a;
	const FN_string_t *nt = (const FN_string_t*)&n;
	FN_status_t *st = (FN_status_t*)&s;
	void * iter_pos;

	if ((syn_attr = a.get((const unsigned char *)"fn_syntax_type")) == 0 ||
	    (tv = syn_attr->first(iter_pos)) == 0) {
		s.set(FN_E_INVALID_SYNTAX_ATTRS, 0, 0, 0);
		return (0);
	}

	const char *syntax_type = (const char *)(tv->contents());

	// prime status for case of syntax not supported
	s.set(FN_E_SYNTAX_NOT_SUPPORTED, 0, 0, 0);


	sprintf(fname, "%s__fn_compound_name_from_syntax_attrs",
		syntax_type);
	// look in executable (and linked libraries)
	if (fh = fns_link_symbol(fname)) {
		return ((FN_compound_name*)(*((from_attrset_func)fh))(at,
		    nt, st));
	}

	// look in loadable module
	sprintf(mname, "fn_compound_name_%s.so", syntax_type);
	if (fh = fns_link_symbol(fname, mname)) {
		return ((FN_compound_name*)(*((from_attrset_func)fh))(at,
		    nt, st));
	}

	// give up
	return (0);
}
