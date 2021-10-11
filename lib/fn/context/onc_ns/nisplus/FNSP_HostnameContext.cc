/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_HostnameContext.cc	1.6 95/01/29 SMI"

#include "FNSP_HostnameContext.hh"
#include "fnsp_internal.hh"
#include "../FNSP_Syntax.hh"

/* maximum size for diagnostic message */
#define	MAXMSGLEN 256

static const FN_string
FNSP_hostname_attribute((unsigned char *) "_host_attribute");

static inline FN_string *
FNSP_hostname_attribute_internal_name(FN_string &domain)
{
	return (FNSP_compose_ctx_tablename(FNSP_hostname_attribute, domain));
}

FNSP_HostnameContext::FNSP_HostnameContext(const FN_ref_addr &from_addr,
    const FN_ref &from_ref)
: FNSP_HUContext(from_addr, from_ref, FNSP_host_context)
{
}

FNSP_HostnameContext*
FNSP_HostnameContext::from_address(const FN_ref_addr &from_addr,
    const FN_ref &from_ref,
    FN_status &stat)
{
	FNSP_HostnameContext *answer = new FNSP_HostnameContext(from_addr,
	    from_ref);

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

int
FNSP_HostnameContext::check_for_config_error(const FN_string &name,
    FN_status_csvc& cs)
{
	unsigned status;
	char diagmsg[MAXMSGLEN];
	FN_string *home_org = FNSP_find_host_entry(*my_orgname, name,
	    my_address->get_access_flags(), status);

	if (home_org) {
		if (home_org->compare(*my_orgname,
		    FN_STRING_CASE_INSENSITIVE) == 0)
			sprintf(diagmsg,
"\nEntry for %s exists in hosts table but does not have associated context.",
			    name.str());
		else
			sprintf(diagmsg,
"\nHost entry for %s is in domain %s but looking for context in domain %s",
			    name.str(), home_org->str(), my_orgname->str());

		FN_string dmsg((const unsigned char *)diagmsg);
		cs.set_code(FN_E_CONFIGURATION_ERROR);
		cs.set_diagnostic_message(&dmsg);

		delete home_org;
	} else {
		// cannot find host entry either.  No problem here.
		return (0);
	}
	return (1);
}


FNSP_Address*
FNSP_HostnameContext::get_attribute_context(const FN_string & /* name */,
    unsigned &status)
{
	FNSP_Address *target_ctx = 0;
	FN_string *target_name;

	if (target_name = FNSP_hostname_attribute_internal_name(*my_orgname)) {
		target_ctx = new FNSP_Address(*target_name,
		    FNSP_hostname_context);
		if (target_ctx == 0)
			status = FN_E_INSUFFICIENT_RESOURCES;
		delete target_name;
		status = FN_SUCCESS;
	} else
		status = FN_E_NAME_NOT_FOUND;

	return (target_ctx);
}
