/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_enterprise.cc	1.7 94/11/08 SMI"

#include <xfn/xfn.hh>
#include <synch.h>

#include <xfn/xfn.hh>
#include <synch.h>
#include <sys/systeminfo.h>
#include <rpcsvc/ypclnt.h>
#include <rpcsvc/nis.h>
#include "FNSP_enterprise_nisplus.hh"
#include "FNSP_enterprise_nis.hh"
#include "FNSP_enterprise_files.hh"

static FNSP_enterprise *FNSP_real_enterprise = 0;
static mutex_t real_enterprise_lock = DEFAULTMUTEX;

static FNSP_enterprise*
FNSP_set_real_enterprise()
{
	nis_result	*res;
	char	domain[NIS_MAXNAMELEN+1];

	sprintf(domain, "ctx_dir.%s", nis_local_directory());
	res = nis_lookup(domain, NO_AUTHINFO | USE_DGRAM);
	if ((res->status == NIS_SUCCESS)) {
		FNSP_real_enterprise = new FNSP_enterprise_nisplus();
	} else if ((sysinfo(SI_SRPC_DOMAIN, domain, NIS_MAXNAMELEN) > 0) &&
	    (yp_bind(domain) == 0)) {
		FNSP_real_enterprise = new
		    FNSP_enterprise_nis((unsigned char *)domain);
		yp_unbind(domain);
	} else
		FNSP_real_enterprise = new
		    FNSP_enterprise_files((unsigned char *)domain);

	nis_freeresult(res);
	return (FNSP_real_enterprise);
}

FNSP_enterprise*
FNSP_get_enterprise()
{
	mutex_lock(&real_enterprise_lock);
	if (FNSP_real_enterprise == 0) {
		FNSP_enterprise* r = FNSP_set_real_enterprise();
		mutex_unlock(&real_enterprise_lock);
		return (r);
	} else {
		mutex_unlock(&real_enterprise_lock);
		return (FNSP_real_enterprise);
	}
}
