/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_user_entries.cc	1.11 94/10/25 SMI"


#include <xfn/fn_p.hh>

#include "FNSP_entries.hh"
#include "FNSP_enterprise.hh"

// These are definitions of the subclass specific constructors and
// resolution methods for each type of user-related entry in
// the initial context.

FNSP_InitialContext::UserEntry::UserEntry(uid_t uid) : Entry()
{
	my_uid = uid;
}

FNSP_InitialContext_UserOrgUnitEntry::
FNSP_InitialContext_UserOrgUnitEntry(uid_t uid) :
FNSP_InitialContext::UserEntry(uid)
{
	num_names = 2;
	stored_names = new FN_string* [num_names];

	stored_names[0] = new FN_string((unsigned char *)"_myorgunit");
	stored_names[1] = new FN_string((unsigned char *)"myorgunit");
}


void
FNSP_InitialContext_UserOrgUnitEntry::resolve()
{
	FN_string *UserOrgUnit_name =
	    FNSP_get_enterprise()->get_user_orgunit_name(my_uid);

	if (UserOrgUnit_name) {
		FN_ref *org_ref = FNSP_reference(
		    *FNSP_get_enterprise()->get_addr_type(),
		    *UserOrgUnit_name,
		    FNSP_organization_context);
		if (org_ref) {
			FN_status status;
			FN_ctx* ctx = FN_ctx::from_ref(*org_ref, status);
			if (ctx) {
				stored_ref = ctx->lookup((unsigned char *)
				    "/", status);
				stored_status_code = status.code();
				delete ctx;
			}
			delete org_ref;
		}
		delete UserOrgUnit_name;
	} else {
		stored_status_code = FN_E_NAME_NOT_FOUND;
	}
}


FNSP_InitialContext_ThisUserEntry::
FNSP_InitialContext_ThisUserEntry(uid_t uid)
: FNSP_InitialContext::UserEntry(uid)
{
	num_names = 3;
	stored_names = new FN_string* [num_names];
	stored_names[0] = new FN_string((unsigned char *)"_myself");
	stored_names[1] = new FN_string((unsigned char *)"myself");
	stored_names[2] = new FN_string((unsigned char *)"thisuser");
}


void
FNSP_InitialContext_ThisUserEntry::resolve()
{
	FN_string *username = FNSP_get_enterprise()->get_user_name(my_uid);

	if (username) {
		// build the name to lookup
		// n = myorgunit/user/<username>/
		FN_composite_name n((unsigned char *)"myorgunit/_user");
		n.append_comp(*username);
		n.append_comp((unsigned char *)"");
		// look it up
		FN_status status;
		FN_ctx_svc* ctx = FNSP_InitialContext_from_initial(
		    FNSP_USER_IC, my_uid, status);
		if (ctx) {
			stored_ref = ctx->lookup(n, status);
			stored_status_code = status.code();
			delete ctx;
		}
		delete username;
	} else {
		stored_status_code = FN_E_NAME_NOT_FOUND;
	}
}

FNSP_InitialContext_UserSiteEntry::FNSP_InitialContext_UserSiteEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];

	stored_names[0] = new FN_string((unsigned char *)"_mysite");
	stored_names[1] = new FN_string((unsigned char *)"mysite");
}

void
FNSP_InitialContext_UserSiteEntry::resolve()
{
	// %%% do not know to figure out affliation yet
	stored_status_code = FN_E_NAME_NOT_FOUND;
}

FNSP_InitialContext_UserENSEntry::FNSP_InitialContext_UserENSEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];

	stored_names[0] = new FN_string((unsigned char *)"_myens");
	stored_names[1] = new FN_string((unsigned char *)"myens");
}

void
FNSP_InitialContext_UserENSEntry::resolve()
{
	// %%% do not know how to figure out affliation yet
	// %%% for now, use machine's root
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

#ifdef FN_IC_EXTENSIONS

FNSP_InitialContext_UserOrgEntry::FNSP_InitialContext_UserOrgEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];

	stored_names[0] = new FN_string((unsigned char *)"_myorg");
	stored_names[1] = new FN_string((unsigned char *)"myorg");
}

void
FNSP_InitialContext_UserOrgEntry::resolve()
{
	// %%% make that same as hostorg for now;
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

FNSP_InitialContext_UserUserEntry::FNSP_InitialContext_UserUserEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];
	stored_names[0] = new FN_string((unsigned char *)"_myuser");
	stored_names[1] = new FN_string((unsigned char *)"myuser");
}


void
FNSP_InitialContext_UserUserEntry::resolve()
{
	// No other clean way to do this with the current design.
	// At this level the Entry doesn't "know" that it's a part of any
	// Table or context.
	FN_status status;
	FN_ctx_svc* ctx = FNSP_InitialContext_from_initial(status);
	if (ctx) {
		stored_ref = ctx->lookup((unsigned char *)
		    "_myorgunit/_user/", status);
		stored_status_code = status.code();
		delete ctx;
	} else {
		stored_status_code = status.code();
	}
}



FNSP_InitialContext_UserHostEntry::FNSP_InitialContext_UserHostEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];

	stored_names[0] = new FN_string((unsigned char *)"_myhost");
	stored_names[1] = new FN_string((unsigned char *)"myhost");
}



void
FNSP_InitialContext_UserHostEntry::resolve()
{
	FN_status status;
	FN_ctx_svc *ctx = FNSP_InitialContext_from_initial(status);
	if (ctx) {
		stored_ref = ctx->lookup((unsigned char *)
		    "_myorgunit/_host/", status);
		stored_status_code = status.code();
		delete ctx;
	} else {
		stored_status_code = status.code();
	}
}

FNSP_InitialContext_UserSiteRootEntry::
FNSP_InitialContext_UserSiteRootEntry()
{
	num_names = 2;
	stored_names = new FN_string* [num_names];

	stored_names[0] = new FN_string((unsigned char *)"_mysiteroot");
	stored_names[1] = new FN_string((unsigned char *)"mysiteroot");
}


void
FNSP_InitialContext_UserSiteRootEntry::resolve()
{
	FN_status status;
	FN_ctx_svc *ctx = FNSP_InitialContext_from_initial(status);
	if (ctx) {
		stored_ref = ctx->lookup((unsigned char *)
		    "_myorg//_site/", status);
		stored_status_code = status.code();
		delete ctx;
	} else {
		stored_status_code = status.code();
	}
}

#endif /* FN_IC_EXTENSIONS */
