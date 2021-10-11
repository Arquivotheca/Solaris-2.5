/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_DotContext.cc	1.2 94/08/08 SMI"

#include "FNSP_DotContext.hh"
#include "fnsp_internal.hh"
#include "../FNSP_Syntax.hh"

FNSP_DotContext::FNSP_DotContext(const FN_ref_addr &from_addr,
    const FN_ref &from_ref)
: FNSP_HierContext(from_addr, from_ref)
{
	my_syntax = FNSP_Syntax(FNSP_site_context);
}


FNSP_DotContext*
FNSP_DotContext::from_address(const FN_ref_addr &from_addr,
    const FN_ref &from_ref,
    FN_status &stat)
{
	FNSP_DotContext *answer = new FNSP_DotContext(from_addr, from_ref);

	if (answer && answer->my_reference && answer->my_address)
		stat.set_success();
	else {
		if (answer) {
			delete answer;
			answer = 0;
		}
		stat.set(FN_E_INSUFFICIENT_RESOURCES);
	}
	return (answer);
}
