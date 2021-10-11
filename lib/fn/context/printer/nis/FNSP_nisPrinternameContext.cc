/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_nisPrinternameContext.cc	1.2 94/11/08 SMI"

#include <sys/types.h>
#include <rpcsvc/ypclnt.h>
#include <stdlib.h>
#include <string.h>
#include <xfn/fn_printer_p.hh>
#include <xfn/fn_p.hh>
#include "FNSP_nisPrinternameContext.hh"

#define	CSIZE 1024
#define	NISSUFFIX ".conf.byname"
#define	NISSUFLEN (12)

static FN_string null_name((unsigned char *) "");
static FN_string internal_name((unsigned char *) "printers");

FNSP_nisPrinternameContext::FNSP_nisPrinternameContext(
    const FN_ref_addr &from_addr, const FN_ref &from_ref)
	: FNSP_PrinternameContext(from_addr, from_ref)
{
	domain_name = FNSP_address_to_internal_name(from_addr);
}

FNSP_nisPrinternameContext*
FNSP_nisPrinternameContext::from_address(const FN_ref_addr &from_addr,
    const FN_ref &from_ref, FN_status &stat)
{
	FNSP_nisPrinternameContext *answer =
	    new FNSP_nisPrinternameContext(from_addr, from_ref);

	if ((answer) && (answer->my_reference))
		stat.set_success();
	else {
		if (answer) {
			delete answer;
			answer = 0;
		}
		FN_composite_name empty_name(null_name);
		stat.set(FN_E_INSUFFICIENT_RESOURCES);
	}
	return (answer);
}

FN_ref*
FNSP_nisPrinternameContext::resolve(const FN_string &aname,
    FN_status_csvc& cstat)
{
	FN_ref *ref = 0;
	char mapname[CSIZE], *value;
	int len;

	ref = FNSP_PrinternameContext::resolve(aname, cstat);
	if (ref)
		return (ref);

	if ((internal_name.charcount() + NISSUFLEN) >= CSIZE)
		return (ref);

	sprintf(mapname, "%s%s", internal_name.str(), NISSUFFIX);

	char *domain = (char *) domain_name->str();
	if (yp_match(domain, mapname, (char *)aname.str(), aname.charcount(),
	    &value, &len) == 0) {
		value[len] = '\0'; // replace \n
		ref = get_service_ref_from_value(internal_name, value);
		free(value);
	}

	if (ref == 0)
		cstat.set_error(FN_E_NAME_NOT_FOUND, *my_reference, null_name);
	else
		cstat.set_success();
	return (ref);
}

FN_nameset*
FNSP_nisPrinternameContext::list(FN_status_csvc &cstat)
{
	FN_nameset *ns;
	char mapname[CSIZE], *inkey, *outkey, *val;
	int inkeylen, outkeylen, vallen, ret;

	ns = FNSP_PrinternameContext::list(cstat);
	if (ns == 0)
		ns = new FN_nameset;

	if ((internal_name.charcount() + NISSUFLEN) >= CSIZE) {
		cstat.set_error(FN_E_PARTIAL_RESULT, *my_reference, null_name);
		return (ns);
	}

	sprintf(mapname, "%s%s", internal_name.str(), NISSUFFIX);

	char *domain = (char *) domain_name->str();
	ret = yp_first(domain, mapname, &inkey, &inkeylen, &val, &vallen);
	if (ret) {
		cstat.set_error(FN_E_PARTIAL_RESULT, *my_reference, null_name);
		return (ns);
	}
	inkey[inkeylen] = '\0'; // replace \n
	ns->add((unsigned char *)inkey);
	free(val);

	while (yp_next(domain, mapname, inkey, inkeylen,
	    &outkey, &outkeylen, &val, &vallen) == 0) {
		outkey[outkeylen] = '\0'; // replace \n
		ns->add((unsigned char *)outkey);

		free(inkey);
		free(val);
		inkey = outkey;
		inkeylen = outkeylen;
	}
	free(inkey);
	cstat.set_success();
	return (ns);
}

FN_bindingset*
FNSP_nisPrinternameContext::list_bs(FN_status_csvc &cstat)
{
	FN_bindingset *bs;
	FN_ref *ref;
	char mapname[CSIZE], *inkey, *outkey, *val;
	int inkeylen, outkeylen, vallen, ret;

	bs = FNSP_PrinternameContext::list_bs(cstat);
	if (bs == 0)
		bs = new FN_bindingset;

	if ((internal_name.charcount() + NISSUFLEN) >= CSIZE) {
		cstat.set_error(FN_E_PARTIAL_RESULT, *my_reference, null_name);
		return (bs);
	}

	sprintf(mapname, "%s%s", internal_name.str(), NISSUFFIX);

	char *domain = (char *) domain_name->str();
	ret = yp_first(domain, mapname, &inkey, &inkeylen, &val, &vallen);
	if (ret) {
		cstat.set_error(FN_E_PARTIAL_RESULT, *my_reference, null_name);
		return (bs);
	}
	val[vallen] = '\0'; // replace \n
	ref = get_service_ref_from_value(internal_name, val);
	free(val);
	if (ref) {
		inkey[inkeylen] = '\0'; // replace \n
		bs->add((unsigned char *)inkey, *ref);
		free(ref);
	}
	while (yp_next(domain, mapname, inkey, inkeylen,
	    &outkey, &outkeylen, &val, &vallen) == 0) {
		val[vallen] = '\0'; // replace \n
		ref = get_service_ref_from_value(internal_name, val);
		free(inkey);
		free(val);
		if (ref) {
			outkey[outkeylen] = '\0'; // replace \n
			bs->add((unsigned char *)outkey, *ref);
			free(ref);
		}
		inkey = outkey;
		inkeylen = outkeylen;
	}
	free(inkey);
	cstat.set_success();
	return (bs);
}
