/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_enterprise_nis.hh	1.2 94/11/08 SMI"

#ifndef _FNSP_ENTERPRISE_NIS_HH
#define	_FNSP_ENTERPRISE_NIS_HH

#pragma ident "@(#)FNSP_enterprise_nis.hh	1.2 94/08/05 SMI"

#include <xfn/xfn.hh>
#include "FNSP_enterprise_files.hh"

class FNSP_enterprise_nis: public FNSP_enterprise_files
{
public:
	FNSP_enterprise_nis(const FN_string& domain);

	const FN_identifier* get_addr_type();
};

#endif /* _FNSP_ENTERPRISE_NIS_HH */
