/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#ifndef _FNSP_DOTCONTEXT_HH
#define	_FNSP_DOTCONTEXT_HH

#pragma ident "@(#)FNSP_DotContext.hh	1.2 94/08/08 SMI"


#include "FNSP_HierContext.hh"


class FNSP_DotContext : public FNSP_HierContext {
public:
	static FNSP_DotContext* from_address(const FN_ref_addr&,
	    const FN_ref&,
	    FN_status& stat);

private:
	FNSP_DotContext(const FN_ref_addr& from_addr, const FN_ref& from_ref);
};


#endif _FNSP_DOTCONTEXT_HH
