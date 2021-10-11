/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _FNSP_ENTERPRISE_FILES_HH
#define	_FNSP_ENTERPRISE_FILES_HH

#pragma ident "@(#)FNSP_enterprise_files.hh	1.2 94/11/08 SMI"

#include <xfn/xfn.hh>
#include "FNSP_enterprise.hh"

class FNSP_enterprise_files: public FNSP_enterprise
{
private:
	FN_string* root_directory;

public:
	FNSP_enterprise_files(const FN_string& domain);
	virtual ~FNSP_enterprise_files();

	const FN_string* get_root_orgunit_name();
	FN_string* get_user_orgunit_name(uid_t);
	FN_string* get_user_name(uid_t);
	FN_string* get_host_orgunit_name();
	FN_string* get_host_name();

	virtual const FN_identifier* get_addr_type();
};

#endif /* _FNSP_ENTERPRISE_FILES_HH */
