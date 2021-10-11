/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _FNSP_ENTERPRISE_HH
#define	_FNSP_ENTERPRISE_HH

#pragma ident	"@(#)FNSP_enterprise.hh	1.4	94/10/19 SMI"

#include <xfn/xfn.hh>
#include <sys/types.h>  /* for uid_t */

class FNSP_enterprise {
public:
	virtual const FN_string *get_root_orgunit_name() = 0;
	virtual FN_string *get_user_orgunit_name(uid_t) = 0;
	virtual FN_string *get_user_name(uid_t) = 0;
	virtual FN_string *get_host_orgunit_name() = 0;
	virtual FN_string *get_host_name() = 0;
	virtual const FN_identifier *get_addr_type() = 0;
};

extern FNSP_enterprise *FNSP_get_enterprise();

#endif /* _FNSP_ENTERPRISE_HH */
