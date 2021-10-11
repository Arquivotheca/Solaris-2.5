/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_enterprise_nisplus.hh	1.19 94/04/16 SMI"

#ifndef _FNSP_ENTERPRISE_NISPLUS_HH
#define	_FNSP_ENTERPRISE_NISPLUS_HH

#pragma ident "@(#)FNSP_enterprise_nisplus.hh	1.6 94/10/19 SMI"

#include <xfn/xfn.hh>
#include "FNSP_enterprise.hh"

#define NISPLUS_ADDRESS_STR "onc_fn_nisplus"

class FNSP_enterprise_nisplus: public FNSP_enterprise
{
private:
	FN_string *root_directory;
	mutex_t root_directory_lock;
	const FN_identifier *my_address_type;
	root_orgunit_p(const FN_string& name);
	const FN_string* set_root_orgunit_name(const FN_string& name);

public:
	FNSP_enterprise_nisplus();

	const FN_string *get_root_orgunit_name();

	FN_string *get_user_orgunit_name(uid_t);

	FN_string* get_user_name(uid_t);

	FN_string* get_host_orgunit_name();

	FN_string* get_host_name();

	const FN_identifier *get_addr_type();
};

#endif /* _FNSP_ENTERPRISE_NISPLUS_HH */
