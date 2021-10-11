/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_host_entries.cc	1.11 94/10/25 SMI"


#include <xfn/fn_p.hh>

#include "FNSP_entries.hh"
#include "FNSP_enterprise.hh"

// These are definitions of the subclass specific constructors and
// resolution methods for each type of host-related entry in
// the initial context.


FNSP_InitialContext_HostOrgUnitEntry::FNSP_InitialContext_HostOrgUnitEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];

	stored_names[0] = new FN_string((unsigned char *)"_thisorgunit");
	stored_names[1] = new FN_string((unsigned char *)"thisorgunit");
}

void
FNSP_InitialContext_HostOrgUnitEntry::resolve()
{
	FN_string *hostorgunit_name =
	    FNSP_get_enterprise()->get_host_orgunit_name();
	if (hostorgunit_name) {
		FN_ref *org_ref = FNSP_reference(
		    *FNSP_get_enterprise()->get_addr_type(),
		    *hostorgunit_name,
		    FNSP_organization_context);
		if (org_ref) {
			FN_status status;
			FN_ctx* ctx = FN_ctx::from_ref(*org_ref, status);
			if (ctx) {
				stored_ref =
				    ctx->lookup((unsigned char *)"/",
				    status);
				stored_status_code = status.code();
				delete ctx;
			}
			delete org_ref;
		}
		delete hostorgunit_name;
	} else {
		stored_status_code = FN_E_NAME_NOT_FOUND;
	}
}

FNSP_InitialContext_ThisHostEntry::FNSP_InitialContext_ThisHostEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];
	stored_names[0] = new FN_string((unsigned char *)"_thishost");
	stored_names[1] = new FN_string((unsigned char *)"thishost");
}

void
FNSP_InitialContext_ThisHostEntry::resolve()
{
	FN_string *hostname = FNSP_get_enterprise()->get_host_name();

	// build the name to lookup
	// n = thisorgunit/host/<hostname>

	if (hostname) {
		FN_composite_name n((unsigned char *)"_thisorgunit/_host");
		n.append_comp(*hostname);
		n.append_comp((unsigned char *)"");

		// look it up
		FN_status status;
		FN_ctx_svc* ctx = FNSP_InitialContext_from_initial(
		    FNSP_HOST_IC, 0, status);
		if (ctx) {
			stored_ref = ctx->lookup(n, status);
			stored_status_code = status.code();
			delete ctx;
		}
		delete hostname;
	} else {
		stored_status_code = FN_E_NAME_NOT_FOUND;
	}
}

FNSP_InitialContext_HostSiteEntry::FNSP_InitialContext_HostSiteEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];
	stored_names[0] = new FN_string((unsigned char *)"_thissite");
	stored_names[1] = new FN_string((unsigned char *)"thissite");
}


void
FNSP_InitialContext_HostSiteEntry::resolve()
{
	// %%% do not know how to figure out affliation yet
	stored_status_code = FN_E_NAME_NOT_FOUND;
}

FNSP_InitialContext_HostENSEntry::FNSP_InitialContext_HostENSEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];
	stored_names[0] = new FN_string((unsigned char *)"_thisens");
	stored_names[1] = new FN_string((unsigned char *)"thisens");
}


void
FNSP_InitialContext_HostENSEntry::resolve()
{
	const FN_string *root_dir =
	    FNSP_get_enterprise()->get_root_orgunit_name();

	if (root_dir) {
		stored_ref = FNSP_reference(
		    *FNSP_get_enterprise()->get_addr_type(),		    
		    *root_dir,
		    FNSP_enterprise_context);
		if (stored_ref) {
			stored_status_code = FN_SUCCESS;
		} else {
			stored_status_code = FN_E_INSUFFICIENT_RESOURCES;
		}
	} else {
		stored_status_code = FN_E_NAME_NOT_FOUND;
	}
}

// *************************************************************
// the following chooses host-centric, org-centric relationships

FNSP_InitialContext_HostOrgEntry::FNSP_InitialContext_HostOrgEntry()
{
	num_names = 3;
	stored_names = new FN_string* [num_names];

	stored_names[0] = new FN_string((unsigned char *)"_orgunit");
	stored_names[1] = new FN_string((unsigned char *)"orgunit");
	stored_names[2] = new FN_string((unsigned char *)"org");
}


void
FNSP_InitialContext_HostOrgEntry::resolve()
{
	const FN_string *root_dir =
	    FNSP_get_enterprise()->get_root_orgunit_name();

	if (root_dir) {
		stored_ref = FNSP_reference(
		    *FNSP_get_enterprise()->get_addr_type(),
		    *root_dir,
		    FNSP_organization_context);
		if (stored_ref) {
			stored_status_code = FN_SUCCESS;
		} else {
			stored_status_code = FN_E_INSUFFICIENT_RESOURCES;
		}
	} else {
		stored_status_code = FN_E_NAME_NOT_FOUND;
	}
}


FNSP_InitialContext_HostSiteRootEntry::
FNSP_InitialContext_HostSiteRootEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];
	stored_names[0] = new FN_string((unsigned char *)"_site");
	stored_names[1] = new FN_string((unsigned char *)"site");
}


void
FNSP_InitialContext_HostSiteRootEntry::resolve()
{
	FN_status status;
	FN_ctx_svc *ctx = FNSP_InitialContext_from_initial(
	    FNSP_HOST_IC, 0, status);
	// %%% should be _thisens/_site/
	if (ctx) {
		stored_ref = ctx->lookup((unsigned char *)"_orgunit//_site/",
		    status);
		stored_status_code = status.code();
		delete ctx;
	} else {
		stored_status_code = status.code();
	}
}

FNSP_InitialContext_HostUserEntry::FNSP_InitialContext_HostUserEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];

	stored_names[0] = new FN_string((unsigned char *)"_user");
	stored_names[1] = new FN_string((unsigned char *)"user");
}

void
FNSP_InitialContext_HostUserEntry::resolve()
{
	// No other clean way to do this with the current design.
	// At this level the Entry doesn't "know" that it's a part of any
	// Table or context.
	FN_status status;
	FN_ctx_svc* ctx = FNSP_InitialContext_from_initial(
	    FNSP_HOST_IC, 0, status);
	if (ctx) {
		stored_ref = ctx->lookup((unsigned char *)
		    "_thisorgunit/_user/", status);
		stored_status_code = status.code();
		delete ctx;
	} else {
		stored_status_code = status.code();
	}
}

FNSP_InitialContext_HostHostEntry::FNSP_InitialContext_HostHostEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];

	stored_names[0] = new FN_string((unsigned char *)"_host");
	stored_names[1] = new FN_string((unsigned char *)"host");
}

void
FNSP_InitialContext_HostHostEntry::resolve()
{
	FN_status status;
	FN_ctx_svc *ctx = FNSP_InitialContext_from_initial(
	    FNSP_HOST_IC, 0, status);
	if (ctx) {
		stored_ref = ctx->lookup((unsigned char *)
		    "_thisorgunit/_host/", status);
		stored_status_code = status.code();
		delete ctx;
	} else {
		stored_status_code = status.code();
	}
}
