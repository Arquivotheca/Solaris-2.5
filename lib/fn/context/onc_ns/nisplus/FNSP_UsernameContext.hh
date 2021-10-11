/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#ifndef _FNSP_USERNAMECONTEXT_HH
#define	_FNSP_USERNAMECONTEXT_HH

#pragma ident "@(#)FNSP_UsernameContext.hh	1.3 94/09/21 SMI"


#include "FNSP_HUContext.hh"

class FNSP_UsernameContext : public FNSP_HUContext {
public:
	static FNSP_UsernameContext* from_address(const FN_ref_addr &,
	    const FN_ref &,
	    FN_status &stat);

private:
	FNSP_UsernameContext(const FN_ref_addr&, const FN_ref&);

protected:
	// implementation for FNSP_HUContext virtual functions
	FNSP_Address *get_attribute_context(const FN_string &,
	    unsigned &status);

	int check_for_config_error(const FN_string &, FN_status_csvc &);
};

#endif _FNSP_USERNAMECONTEXT_HH
