/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_FNSP_NISPRINTERNAMECONTEXT_HH
#define	_FNSP_NISPRINTERNAMECONTEXT_HH

#pragma ident	"@(#)FNSP_nisPrinternameContext.hh	1.4	94/11/29 SMI"

#include "../FNSP_PrinternameContext.hh"

extern FN_ref* get_service_ref_from_value(const FN_string&, char *);

class FNSP_nisPrinternameContext : public FNSP_PrinternameContext {
public:
	FNSP_nisPrinternameContext(const FN_ref_addr &, const FN_ref &);

	static FNSP_nisPrinternameContext* from_address(const FN_ref_addr&,
	    const FN_ref&, FN_status& stat);

protected:
	// Domain name
	FN_string *domain_name;

	FN_ref* resolve(const FN_string &, FN_status_csvc &);
	FN_nameset* list(FN_status_csvc&);
	FN_bindingset* list_bs(FN_status_csvc&);
};

#endif	/* _FNSP_NISPRINTERNAMECONTEXT_HH */
