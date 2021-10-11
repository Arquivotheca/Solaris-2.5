/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)FN_ctx.cc	1.6	94/08/13 SMI"

#include <xfn/xfn.hh>

FN_namelist::~FN_namelist()
{
}

FN_bindinglist::~FN_bindinglist()
{
}

FN_valuelist::~FN_valuelist()
{
}

FN_multigetlist::~FN_multigetlist()
{
}

FN_ctx::~FN_ctx()
{
}

#include "fns_symbol.hh"

typedef FN_ctx_t* (*from_initial_func)(FN_status_t *);

// get initial context
FN_ctx *
FN_ctx::from_initial(FN_status &s)
{
	void *fh;

	FN_status_t *stat = (FN_status_t *)(&s);
	FN_ctx_t *c_ctx = 0;

	// look in executable (and linked libraries)
	if (fh = fns_link_symbol("initial__fn_ctx_handle_from_initial"))
	    c_ctx = ((*((from_initial_func)fh))(stat));

	// look in loadable module
	else if (fh = fns_link_symbol("initial__fn_ctx_handle_from_initial",
	    "fn_ctx_initial.so"))
		c_ctx = ((*((from_initial_func)fh))(stat));

	// configuration error
	if (c_ctx == (FN_ctx_t*)0)
	    s.set(FN_E_CONFIGURATION_ERROR, 0, 0, 0);

	return ((FN_ctx*)c_ctx);
}

#include <stdio.h>
#include <sys/param.h>

typedef FN_ctx_t *(*from_address_func)(const FN_ref_addr_t *,
    const FN_ref_t *, FN_status_t *);

typedef FN_ctx_t* (*from_ref_func)(const FN_ref_t*, FN_status_t *);

// construct a context from a reference
FN_ctx *
FN_ctx::from_ref(const FN_ref &r, FN_status &s)
{
	FN_ctx_t *cp = 0;
	const FN_ref_addr *ap;
	const unsigned char *addr_type_str;
	void *ip, *fh;
	char mname[MAXPATHLEN], fname[MAXPATHLEN];
	const FN_identifier *addr_type;

	// prime status for case of no supported addresses
	s.set(FN_E_NO_SUPPORTED_ADDRESS, 0, 0, 0);

	// look for supported addresses (and try them)
	for (ap = r.first(ip); ap; ap = r.next(ip)) {
		addr_type = ap->type();
		addr_type_str = (addr_type ? addr_type->str() : 0);
		if (addr_type_str == 0)
			continue;
		sprintf(fname,
			"%s__fn_ctx_handle_from_ref_addr",
			(char *)addr_type_str);

		// look in executable (and linked libraries)
		if (fh = fns_link_symbol(fname)) {
			if (cp = (*((from_address_func)fh))(
			    (const FN_ref_addr_t*)ap, (const FN_ref_t*)&r,
			    (FN_status_t *)&s))
				return ((FN_ctx*)cp);
			continue;
		}

		// look in loadable module
		sprintf(mname, "fn_ctx_%s.so", (char *)addr_type_str);
		if (fh = fns_link_symbol(fname, mname)) {
			if (cp = (*((from_address_func)fh))(
			    (const FN_ref_addr_t*)ap,
			    (const FN_ref_t*)&r, (FN_status_t *)&s))
				return ((FN_ctx*)cp);
			continue;
		}
	}

	return (0);
}
