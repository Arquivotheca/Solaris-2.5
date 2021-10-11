/*
 * Copyright (c) 1992 - 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)fncreate.cc	1.13	95/01/29 SMI"


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <xfn/xfn.hh>
#include <xfn/fn_p.hh>
#include <rpcsvc/nis.h>
#include "FNSP_Context.hh"
#include "fnsp_internal.hh" // For FNSP_orgname_of

static int create_subcontexts_p = 1;   // default is to create subcontexts
static int verbose = 0;
static char *target_name_str = 0;
static unsigned global_bind_flags = FN_OP_EXCLUSIVE;
static unsigned context_type;
static char *input_file = 0;
static char *reference_type = 0;

static char *program_name  = 0;

static int
__fns_xdr_encode_string(const char *str, char *result, size_t &len);

#define	FNSP_fs_context 101

#define	USER_SOURCE "passwd.org_dir"
#define	HOST_SOURCE "hosts.org_dir"

// ---------------------------------------------------------------------
// Code for dealing with NIS+ tables

#include <rpcsvc/nis.h>

static struct traverse_data {
	FNSP_Context *parent;
	const FN_ref *parent_ref;
	const FN_composite_name *parent_name;
	const FN_string *domain_name;
	int subcontext_p;
	int count;
};

int traverse_user_list(
	const FN_string &domainname,
	FNSP_Context *parent,
	const FN_ref &parent_ref,
	const FN_composite_name &parent_name,
	int sub);

int traverse_host_list(
	const FN_string &domainname,
	FNSP_Context *parent,
	const FN_ref &parent_ref,
	const FN_composite_name &parent_name,
	int sub);

int process_host_entry(char *, nis_object *ent, void *udata);

/* ******************************************************************* */

// for host and user functions

static const FN_string FNSP_service_string((unsigned char *)"service");
static const FN_string FNSP_service_string_sc((unsigned char *)"_service");
static const FN_string FNSP_host_string((unsigned char *)"host");
static const FN_string FNSP_host_string_sc((unsigned char *)"_host");
static const FN_string FNSP_user_string((unsigned char *)"user");
static const FN_string FNSP_user_string_sc((unsigned char *)"_user");
static const FN_string FNSP_site_string_sc((unsigned char *)"_site");
static const FN_string FNSP_site_string((unsigned char *)"site");
static const FN_string FNSP_fs_string_sc((unsigned char *)"_fs");
static const FN_string FNSP_fs_string((unsigned char *)"fs");

static const FN_string FNSP_empty_component((unsigned char *)"");
static const FN_string NISPLUS_separator((unsigned char *)".");

void
usage(char *cmd, char *msg = 0)
{
	if (msg)
		fprintf(stderr, "%s: %s\n", cmd, msg);
	fprintf(stderr,
"Usage:\t%s -t org|hostname|username|host|user|service|site|generic|nsid|fs\n",
	    cmd);
	fprintf(stderr,
"\t\t[-osv] [-f <input_filename>] [-r <reference_type>]composite_name\n");
	exit(1);
}

// Note that we're only using the FNSP constants here instead of
// inventing a new set.  The type of context to be created do not
// always correspond exactly to these types.
unsigned
get_context_type_from_string(char *ctx_type_str)
{
	if (strcmp(ctx_type_str, "org") == 0)
		return (FNSP_organization_context);
	else if (strcmp(ctx_type_str, "hostname") == 0)
		return (FNSP_hostname_context);
	else if (strcmp(ctx_type_str, "username") == 0)
		return (FNSP_username_context);
	else if (strcmp(ctx_type_str, "host") == 0)
		return (FNSP_host_context);
	else if (strcmp(ctx_type_str, "user") == 0)
		return (FNSP_user_context);
	else if (strcmp(ctx_type_str, "service") == 0)
		return (FNSP_service_context);
	else if (strcmp(ctx_type_str, "site") == 0)
		return (FNSP_site_context);
	else if (strcmp(ctx_type_str, "generic") == 0)
		return (FNSP_generic_context);
	else if (strcmp(ctx_type_str, "nsid") == 0)
		return (FNSP_nsid_context);
	else if (strcmp(ctx_type_str, "fs") == 0)
		return (FNSP_fs_context);

	return (0);
}


void
process_cmd_line(int argc, char **argv)
{
	int c;
	char *ctx_type_str = 0;
	while ((c = getopt(argc, argv, "ost:vf:r:")) != -1) {
		switch (c) {
		case 'o' :
			create_subcontexts_p = 0;
			break;
		case 's' :
			global_bind_flags = 0;
			break;
		case 'v' :
			verbose = 1;
			break;
		case 't':
			ctx_type_str = optarg;
			break;
		case 'f':
			input_file = (optarg? strdup(optarg) : 0);
			break;
		case 'r':
			reference_type =  (optarg? strdup(optarg) : 0);
			break;
		case '?':
			default :
			usage(argv[0], "invalid option");
		}
	}

	if (optind < argc)
		target_name_str = argv[optind++];
	else
		usage(argv[0], "missing composite name of context to create");

	if (optind < argc)
		usage(argv[0], "too many arguments");

	if (ctx_type_str == 0)
		usage(argv[0], "missing type of context to create");
	else
		context_type = get_context_type_from_string(ctx_type_str);

	if (context_type == 0)
		usage(argv[0], "invalid context type");

	if (input_file && (context_type != FNSP_hostname_context &&
	    context_type != FNSP_username_context))
		usage(argv[0],
		"-f option can only be used with hostname or username types");

	if (reference_type != NULL && context_type != FNSP_generic_context)
		usage(argv[0],
		    "-r option can only be used with generic context type");

	program_name = strdup(argv[0]);
}

// Basic FNSP creation routine for creating context of appropriate
// FNSP subtype.
// No support for arbitrary reference type yet.

FN_ref *
fnscreate(
	FNSP_Context *ctx,
	const FN_composite_name &fullname,	// name relative to initial ctx
	const FN_composite_name &name,		// name relative to 'ctx'
	unsigned context_type,
	int &created,
	const FN_identifier *ref_type = 0)
{
	FN_status status;
	FN_ref *ref;
	FN_string *fstr = fullname.string();

	ref = ctx->create_fnsp_subcontext(name, context_type, status,
	    FNSP_normal_repr, ref_type);
	if (status.is_success()) {
		++created;
		if (verbose)
			printf("%s created\n",
			    (fstr ? (char *)fstr->str() : ""));
	} else if (status.code() == FN_E_NAME_IN_USE) {
		fprintf(stderr, "Binding for '%s' already exists.\n",
			(fstr ? (char *)fstr->str() : ""));
		const FN_ref *rref = status.resolved_ref();
		const FN_composite_name *rname = status.remaining_name();
		FN_status s1;
		if (rref && rname) {
			FN_ctx *rctx = FN_ctx::from_ref(*rref, s1);
			if (rctx) {
				ref = rctx->lookup(*rname, s1);
				delete rctx;
			} else {
				FN_string * desc = s1.description();
				fprintf(stderr,
"DEBUG fnscreate: Could not generate context from resolved reference: %s\n",
				    desc? (char *)(desc->str()) : "");
				delete desc;
				ref = ctx->lookup(name, s1);
			}
		} else {
			fprintf(stderr,
"DEBUG fnscreate: Could not use resolved reference.\n");
			ref = ctx->lookup(name, s1);
		}
	} else {
	    FN_string *desc = status.description();
	    fprintf(stderr, "Create of '%s' failed: %s\n",
		    (fstr ? (char *)fstr->str() : ""),
		    (desc? (char *)(desc->str()): ""));
	    delete desc;
	}
	delete fstr;
	return (ref);
}

int
fnsbind(FN_ctx *ctx,
	const FN_composite_name &alias_fullname,
	const FN_composite_name &alias_name,
	const FN_composite_name &orig_fullname,
	const FN_ref &ref)
{
	FN_status status;

	if (ctx->bind(alias_name, ref, global_bind_flags, status) ||
	    status.code() == FN_E_NAME_IN_USE) {
		if (status.code() == FN_E_NAME_IN_USE) {
			FN_string *pstr = alias_fullname.string();
			fprintf(stderr,
			    "Binding for '%s' already exists.\n",
			    (pstr ? (char *)pstr->str() : ""));
			delete pstr;
		} else if (verbose) {
			FN_string *astr = alias_fullname.string();
			FN_string *ostr = orig_fullname.string();
			printf("'%s' bound to context reference of '%s'\n",
			    (astr ? (char *)astr->str() : ""),
			    (ostr ? (char *)ostr->str() : ""));
			delete astr;
			delete ostr;
		}
		return (1);
	} else {
		FN_string *desc = status.description();
		FN_string *astr = alias_fullname.string();
		FN_string *ostr = orig_fullname.string();
		fprintf(stderr,
"%s: '%s' could not be bound to context reference of '%s': %s\n",
		    program_name,
			(astr ? (char *)astr->str() : ""),
			(ostr ? (char *)ostr->str() : ""),
			(desc ? (char *)(desc->str()) : ""));
		delete desc;
		delete astr;
		delete ostr;
		return (0);
	}
}

// Make sure that NIS_GROUP has been set.
static int
check_nis_group()
{
	if (getenv("NIS_GROUP") == 0) {
		fprintf(stderr,
"The environment variable NIS_GROUP has not been set.  This has\n\
administrative implications for contexts that will be created.\n\
See fncreate(1M) and nis+(1). Please try again after setting NIS_GROUP.\n");
		return (0);
	} else
		return (1);
}

// make sure that name has two components, last of which is null
// returns 1 if condition is true, 0 otherwise
static int
check_null_trailer(const FN_composite_name &fullname,
    const FN_composite_name &name)
{
	void *iter_pos;
	if (name.count() == 2 && name.last(iter_pos)->is_empty())
		return (1);
	else {
		FN_string *fstr = fullname.string();
		fprintf(stderr, "%s: Cannot create context for '%s'.\n\
the supplied name should have a trailing slash '/'.\n",
			program_name, (fstr ? (char *)fstr->str(): ""));
		delete fstr;
		return (0);
	}
}

static const
    FN_composite_name canonical_service_name((unsigned char *)"_service/");
static const
    FN_composite_name canonical_username_name((unsigned char *)"_user/");
static const
    FN_composite_name canonical_hostname_name((unsigned char *)"_host/");
static const FN_composite_name canonical_site_name((unsigned char *)"_site/");
static const FN_composite_name canonical_fs_name((unsigned char *)"_fs/");
static const FN_composite_name custom_service_name((unsigned char *)"service/");
static const FN_composite_name custom_username_name((unsigned char *)"user/");
static const FN_composite_name custom_hostname_name((unsigned char *)"host/");
static const FN_composite_name custom_site_name((unsigned char *)"site/");
static const FN_composite_name custom_fs_name((unsigned char *)"fs/");

static int
name_is_special_token(const FN_composite_name &shortname,
    unsigned int ctx_type)
{
	void *iter_pos;
	const FN_string *atomic_name = shortname.first(iter_pos);

	if (atomic_name == 0)
		return (0);

	switch (ctx_type) {
	case FNSP_service_context:
		return (atomic_name->compare(FNSP_service_string,
		    FN_STRING_CASE_INSENSITIVE) == 0 ||
		    atomic_name->compare(FNSP_service_string_sc,
		    FN_STRING_CASE_INSENSITIVE) == 0);
	case FNSP_username_context:
		return (atomic_name->compare(FNSP_user_string,
		    FN_STRING_CASE_INSENSITIVE) == 0 ||
		    atomic_name->compare(FNSP_user_string_sc,
		    FN_STRING_CASE_INSENSITIVE) == 0);
	case FNSP_hostname_context:
		return (atomic_name->compare(FNSP_host_string,
		    FN_STRING_CASE_INSENSITIVE) == 0 ||
		    atomic_name->compare(FNSP_host_string_sc,
		    FN_STRING_CASE_INSENSITIVE) == 0);
	case FNSP_site_context:
		return (atomic_name->compare(FNSP_site_string,
		    FN_STRING_CASE_INSENSITIVE) == 0 ||
		    atomic_name->compare(FNSP_site_string_sc,
		    FN_STRING_CASE_INSENSITIVE) == 0);
	case FNSP_fs_context:
		return (atomic_name->compare(FNSP_fs_string,
		    FN_STRING_CASE_INSENSITIVE) == 0 ||
		    atomic_name->compare(FNSP_fs_string_sc,
		    FN_STRING_CASE_INSENSITIVE) == 0);
	}
	return (0);
}

static int
assign_aliases(
	const FN_composite_name &parent_name,
	unsigned int ctx_type,
	FN_composite_name **fn,
	const FN_composite_name **rn,
	FN_composite_name **afn,
	const FN_composite_name **arn)
{
	// get short forms
	switch (ctx_type) {
	case FNSP_service_context:
		*rn = &custom_service_name;
		*arn = &canonical_service_name;
		break;
	case FNSP_site_context:
		*rn = &custom_site_name;
		*arn = &canonical_site_name;
		break;
	case FNSP_username_context:
		*rn = &custom_username_name;
		*arn = &canonical_username_name;
		break;
	case FNSP_hostname_context:
		*rn = &custom_hostname_name;
		*arn = &canonical_hostname_name;
		break;
	}

	// construct long forms using parent name

	void *iter_pos;
	// parent has trailing FNSP_empty_component;
	parent_name.last(iter_pos);
	FN_composite_name *new_fn = parent_name.prefix(iter_pos);
	FN_composite_name *new_afn = parent_name.prefix(iter_pos);

	if (new_fn == 0 || new_afn == 0)
		return (0);

	// %%% minimal error checking here
	new_fn->append_name(**rn);
	new_afn->append_name(**arn);
	*fn = new_fn;
	*afn = new_afn;
	return (1);
}


// Create a 'nsid' context and return its reference.
//
// This is a flat context in which naming system names (with associated
// nns pointers) could be bound.  Examples of such names  are: "_service/",
// "_host/", "_user/".
//
// An "nsid" context should only be bound to a name with a trailing '/'.
// Should we make the effort to check for that here?

FN_ref *
create_nsid_context(FNSP_Context *ctx,
		    const FN_composite_name &fullname,
		    const FN_composite_name &name,
		    int &created,
		    unsigned context_type = FNSP_nsid_context)
{
	return (fnscreate(ctx, fullname, name, context_type, created));
}


// Create a service context in which slash-separated, left-to-right names
// could be bound and return its reference.
FN_ref *
create_generic_context(
	FNSP_Context *ctx,
	const FN_composite_name &fullname,
	const FN_composite_name &name,
	int &created,
	const char *ref_type_str)
{
	FN_identifier *ref_type = 0;
	FN_ref * answer;

	if (ref_type_str)
		ref_type = new FN_identifier(
		    (const unsigned char *)ref_type_str);

	answer = fnscreate(ctx, fullname, name, FNSP_generic_context,
			    created, ref_type);
	if (ref_type)
		delete ref_type;
	return (answer);
}

// Create a service context in which slash-separated, left-to-right names
// could be bound and return its reference.
FN_ref *
create_service_context(
	FNSP_Context *ctx,
	const FN_composite_name &fullname,
	const FN_composite_name &name,
	int &created)
{
	return (fnscreate(ctx, fullname, name, FNSP_service_context, created));
}

// Create an nsid context with a service subcontext within it and
// return the reference of the nsid context.
// If 'service_ref_holder' is non-zero, set it to reference of
// service context created.

FN_ref *
create_nsid_and_service_context(FNSP_Context *ctx,
				const FN_composite_name &fullname,
				const FN_composite_name &name,
				int &how_many,
				unsigned int context_type = FNSP_nsid_context,
				FN_ref **service_ref_holder = 0,
				FN_ctx **nsid_ctx_holder = 0)
{
	// name should have a trailing '/'

	if (nsid_ctx_holder)
		*nsid_ctx_holder = 0;  // initialize

	// Create nsid context associated.
	FN_ref *nsid_ref = fnscreate(ctx, fullname, name, context_type,
	    how_many);

	if (nsid_ref == 0)
		return (0);

	// Create service subcontext.
	FN_status status;
	FNSP_Context *nsid_ctx = FNSP_Context::from_ref(*nsid_ref, status);
	if (nsid_ctx == 0) {
		FN_string *desc = status.description();
		fprintf(stderr,
		    "%s:Could not generate context from nsid reference: %s\n",
		    program_name,
		    desc ? (char *)(desc->str()) : "");
		if (desc)
			delete desc;
		delete nsid_ref;
		return (0);
	}

	void *iter_pos;
	FN_composite_name service_fullname(fullname);

	if (service_fullname.last(iter_pos)->is_empty())
		service_fullname.insert_comp(iter_pos, FNSP_service_string);
	else
		service_fullname.append_name(custom_service_name);

	FN_ref *service_ref =
	    create_service_context(nsid_ctx, service_fullname,
				    custom_service_name, how_many);
	if (service_ref) {
		// add alias
		FN_composite_name alias_fullname(fullname);

		if (alias_fullname.last(iter_pos)->is_empty())
			alias_fullname.insert_comp(iter_pos,
			    FNSP_service_string_sc);
		else
			alias_fullname.append_name(canonical_service_name);

		fnsbind(nsid_ctx, alias_fullname, canonical_service_name,
			service_fullname, *service_ref);

		if (service_ref_holder)
			*service_ref_holder = service_ref;
		else
			delete service_ref;
	}
	if (nsid_ctx_holder)
		*nsid_ctx_holder = nsid_ctx;
	else
		delete nsid_ctx;

	return (nsid_ref);
}

static FN_identifier
FNSP_fs_reftype((const unsigned char *)"onc_fn_fs");
static FN_identifier
FNSP_user_fs_addrtype((const unsigned char *)"onc_fn_fs_user_nisplus");
static FN_identifier
FNSP_host_fs_addrtype((const unsigned char *)"onc_fn_fs_host");

/* returns 1 for success; 0 for failure */
static int
fnsbind_ref(FN_ctx *ctx, const FN_composite_name &parent_fullname,
	    const FN_composite_name &target_name, const FN_ref &ref)
{
	FN_status status;
	FN_string *pstr;
	FN_string *nstr;
	FN_string *desc;
	if (ctx->bind(target_name, ref, global_bind_flags, status) ||
	    status.code() == FN_E_NAME_IN_USE) {
		if (status.code() == FN_E_NAME_IN_USE) {
			pstr = parent_fullname.string();
			nstr = target_name.string();
			fprintf(stderr,
		"Binding for '%s' in '%s' context already exists.\n",
			    (nstr ? (char *)nstr->str() : ""),
			    (pstr ? (char *)pstr->str() : ""));
			delete pstr;
			delete nstr;
		} else if (verbose) {
			pstr = parent_fullname.string();
			nstr = target_name.string();
			printf("Created binding for '%s' in '%s' context.\n",
			    (nstr ? (char *)nstr->str() : ""),
			    (pstr ? (char *)pstr->str() : ""));
			delete nstr;
		}
		return (1);
	} else {
		// could not create canonical binding
		pstr = parent_fullname.string();
		nstr = target_name.string();
		desc = status.description();
		fprintf(stderr,
	"%s: could not create binding for '%s' in '%s' context: %s\n",
			program_name,
			(nstr ? (char *)nstr->str() : ""),
			(pstr ? (char *)pstr->str() : ""),
			(desc ? (char *)desc->str() : "No status description"));
		delete pstr;
		delete nstr;
		delete desc;
		return (0);
	}
}

// create and return reference for user fs binding
static FN_ref *
create_user_fs_ref(const FN_string &user_name, const FN_string &domain_name)
{
	FN_ref *ref = new FN_ref(FNSP_fs_reftype);
	char fs_addr[NIS_MAXNAMELEN];
	char encoded_addr[NIS_MAXNAMELEN];
	size_t len = NIS_MAXNAMELEN;

	if (ref == 0)
		return (0);

	sprintf(fs_addr, "[name=%s]passwd.org_dir.%s",
		user_name.str(), domain_name.str());
	if (__fns_xdr_encode_string(fs_addr, encoded_addr, len) == 0) {
		delete ref;
		return (0);
	}

	FN_ref_addr addr(FNSP_user_fs_addrtype, len, encoded_addr);
	ref->append_addr(addr);

	return (ref);
}

static int
create_user_fs(FN_ctx *user_ctx,
    const FN_composite_name &user_fullname,
    const FN_string &user_name,
    const FN_string &domain_name,
    const FN_composite_name *target_name = 0)
{
	FN_ref *ref = create_user_fs_ref(user_name, domain_name);
	int status = 0;

	if (target_name)
		status = fnsbind_ref(user_ctx, user_fullname, *target_name,
		    *ref);
	else {
		status = (fnsbind_ref(user_ctx, user_fullname,
		    canonical_fs_name, *ref) &&
		    fnsbind_ref(user_ctx, user_fullname, custom_fs_name, *ref));
	}
	delete ref;
	return (status);
}

// create and return reference for host fs binding
static FN_ref *
create_host_fs_ref(const FN_string &host_name, const FN_string &domain_name)
{
	FN_ref *ref = new FN_ref(FNSP_fs_reftype);
	char fs_addr[NIS_MAXNAMELEN];
	char encoded_addr[NIS_MAXNAMELEN];
	size_t len = NIS_MAXNAMELEN;

	if (ref == 0)
		return (0);

	sprintf(fs_addr, "%s.%s", host_name.str(), domain_name.str());
	if (__fns_xdr_encode_string(fs_addr, encoded_addr, len) == 0) {
		delete ref;
		return (0);
	}

	FN_ref_addr addr(FNSP_host_fs_addrtype, len, encoded_addr);
	ref->append_addr(addr);

	return (ref);
}

/* returns 1 for success; 0 for failure */
static int
create_host_fs(FN_ctx *host_ctx,
    const FN_composite_name &host_fullname,
    const FN_string &host_name,
    const FN_string &domain_name,
    const FN_composite_name *target_name = 0)
{
	FN_ref *ref = create_host_fs_ref(host_name, domain_name);
	int status = 0;

	if (target_name)
		status = fnsbind_ref(host_ctx, host_fullname, *target_name,
		    *ref);
	else {
		status = (fnsbind_ref(host_ctx, host_fullname,
		    canonical_fs_name, *ref) &&
		    fnsbind_ref(host_ctx, host_fullname, custom_fs_name, *ref));
	}
	delete ref;
	return (status);
}


// Create a site context in which dot-separated, right-to-left site names
// could be bound.
// If 'subcontext_p' is set, a nsid context for the site, and a service
// context in the nsid context are created.
FN_ref *
create_site_context(FNSP_Context *ctx,
		    const FN_composite_name &fullname,
		    const FN_composite_name &name,
		    int subcontext_p,
		    int &how_many)
{
	FN_ref *site_ref = fnscreate(ctx,
	    fullname, name, FNSP_site_context, how_many);

	if (site_ref == 0 || subcontext_p == 0)
		return (site_ref);

	FN_status status;
	FNSP_Context *site_ctx = FNSP_Context::from_ref(*site_ref, status);
	if (site_ctx == 0) {
		FN_string *desc = status.description();
		fprintf(stderr,
		    "%s: Could not generate context from site reference: %s\n",
		    desc ? (char *)(desc->str()) : "");
		if (desc)
			delete desc;
		delete site_ref;
		return (0);
	}

	FN_composite_name site_nsid_name(name);
	site_nsid_name.append_comp(FNSP_empty_component);  // tag on "/"

	FN_composite_name site_full_nsid_name(fullname); // tag on "/"
	site_full_nsid_name.append_comp(FNSP_empty_component);

	FN_ref *nsid_ref =
		create_nsid_and_service_context(ctx,
						site_full_nsid_name,
						site_nsid_name,
						how_many);
	if (nsid_ref)
		delete nsid_ref;

	return (site_ref);
}

// Create nsid context associated with host, and create a 'service'
// context in its nsid context and create 'fs' bindings
FN_ref *
create_host_context(FNSP_Context *ctx,
		    const FN_ref &parent_ref,
		    const FN_composite_name &fullname,
		    const FN_composite_name &name,
		    const FN_string &domain_name,
		    const FN_string &host_name,
		    int subcontexts_p,
		    int &how_many)
{
	// name should have a trailing '/'

	FN_ref *host_ref;
	FN_ctx *host_ctx = 0;
	int how_many_before = how_many;  // remember count before attempt

	if (subcontexts_p) {
		host_ref = create_nsid_and_service_context(ctx, fullname, name,
		    how_many, FNSP_host_context, 0, &host_ctx);
		if (host_ctx && create_host_fs(host_ctx, fullname, host_name,
		    domain_name))
			how_many += 2;
		delete host_ctx;
	} else
		host_ref = create_nsid_context(ctx, fullname, name, how_many,
		    FNSP_host_context);

	// if created new contexts, change their ownership to be owned by host

	if (host_ref && (how_many_before < how_many)) {
		FN_string owner(0, &host_name, &NISPLUS_separator, &domain_name,
		    0);

		// note: service subcontext need not be changed explicitly
		// update as side effect of updating nsid context

		// do nsid context
		FNSP_change_context_ownership(*host_ref, owner);
		FNSP_change_binding_ownership(parent_ref, host_name, owner);
	}

	return (host_ref);
}

// create 'host' contexts for the canonical host name
// (can_nsid_name and alias_nsid_name are host names with trailing '/')
// in parent context 'ctx' with name 'parent_name'
FN_ref *
create_host_context_can(FNSP_Context *ctx,
			const FN_ref &parent_ref,
			const FN_composite_name &parent_name,
			const FN_string &can_hostname,
			const FN_string &alias_hostname,
			const FN_string &domain_name,
			int subcontext_p,
			int &how_many)
{
	FN_ref *ref = 0;
	FN_status status;
	void *iter_pos;

	// Construct relative and full names of "<hostname>/" for canonical name
	FN_composite_name can_hostname_nsid(can_hostname);
	can_hostname_nsid.append_comp(FNSP_empty_component);

	FN_composite_name can_fullname(parent_name);
	(void) can_fullname.last(iter_pos);
	can_fullname.insert_comp(iter_pos, can_hostname);

	// Construct relative and full names of "<hostname>/" for alias name
	FN_composite_name alias_hostname_nsid(alias_hostname);
	alias_hostname_nsid.append_comp(FNSP_empty_component);

	FN_composite_name alias_fullname(parent_name);
	(void) alias_fullname.last(iter_pos);
	alias_fullname.insert_comp(iter_pos, alias_hostname);

	if (can_hostname.compare(alias_hostname,
	    FN_STRING_CASE_INSENSITIVE) == 0) {
		// canonical name is same as alias name
		ref = create_host_context(ctx, parent_ref,
		    can_fullname, can_hostname_nsid,
		    domain_name, can_hostname, subcontext_p, how_many);
	} else {
		//
		// canonical name is different from alias name
		// do lookup first, since canonical name is likely to already
		// have context (assumes canonical entry is first in list)
		//
		ref = ctx->lookup(can_hostname_nsid, status);
		if (status.is_success() == 0) {
			// canonical name context does not exist
			ref = create_host_context(ctx, parent_ref,
			    can_fullname, can_hostname_nsid,
			    domain_name, can_hostname, subcontext_p, how_many);
			if (ref == 0) {
				FN_string *desc = status.description();
				FN_string *cstr = can_fullname.string();
				fprintf(stderr,
				"%s: Could not create context for '%s': %s\n",
				    (cstr? (char *)cstr->str() : ""),
				    (desc? (char *)(desc->str()): ""));
				delete desc;
				delete cstr;
				return (0);
			}
		}

		// make binding for alias name (to point to ref of
		// canonical name)
		if (status.is_success()) {
			if (fnsbind(ctx, alias_fullname, alias_hostname_nsid,
			    can_fullname, *ref)) {

				// change ownership of binding to that of
				// canonical host
				FN_string owner(0, &can_hostname,
				    &NISPLUS_separator, &domain_name, 0);
				FNSP_change_binding_ownership(parent_ref,
				    alias_hostname, owner);
			}
		}
	}
	return (ref);
}


FN_ref *
create_user_context(
	FNSP_Context *ctx,
	const FN_ref &parent_ref,
	const FN_composite_name &fullname,
	const FN_composite_name &name,
	const FN_string &domain_name,
	const FN_string &user_name,
	int subcontexts_p,
	int &how_many)
{
	// name should have a trailing '/'

	FN_ref *user_ref;
	FN_ctx *user_ctx = 0;

	int how_many_before = how_many;  // remember count before attempt

	if (subcontexts_p) {
		user_ref = create_nsid_and_service_context(ctx, fullname,
		    name, how_many, FNSP_user_context, 0, &user_ctx);
		if (user_ctx && create_user_fs(user_ctx, fullname, user_name,
		    domain_name))
			how_many += 2;
		delete user_ctx;
	} else
		user_ref = create_nsid_context(ctx, fullname, name, how_many,
		    FNSP_user_context);

	// if created new contexts, change their ownership to be owned by user
	if (user_ref && (how_many_before < how_many)) {
		FN_string owner(0, &user_name, &NISPLUS_separator, &domain_name,
		    0);

		// note: service subcontext need not be changed explicitly
		// update as side effect of updating nsid context

		// do nsid context
		FNSP_change_context_ownership(*user_ref, owner);
		FNSP_change_binding_ownership(parent_ref, user_name, owner);
	}

	return (user_ref);
}

FN_ref *
create_username_context(FNSP_Context *ctx,
			const FN_composite_name &fullname,
			const FN_composite_name &name,
			int subcontexts_p,
			int &how_many)
{
	// A "username" context should only be bound to a name of "user/".
	// Should we make the effort to check for that here?

	FN_ref *username_ref = fnscreate(ctx, fullname, name,
	    FNSP_username_context, how_many);

	if (subcontexts_p == 0 || username_ref == 0)
		return (username_ref);

	// Get username context
	FN_status stat;
	FNSP_Context *username_ctx = FNSP_Context::from_ref(*username_ref,
							    stat);
	if (username_ctx == 0) {
		FN_string *desc = stat.description();
		fprintf(stderr,
		"%s: Could not generate context from username reference: %s\n",
		    program_name, (desc ? (char *)(desc->str()): ""));
		if (desc)
			delete desc;
		fprintf(stderr, "%s: No user contexts were created.\n");
		return (username_ref);
	}

	// Get organization directory name of username context
	unsigned status;
	FN_string *username_dir =
	    FNSP_reference_to_internal_name(*username_ref);
	if (username_dir == 0) {
		fprintf(stderr,
		    "%s: Could not obtain object name of username context.\n",
		    program_name);
		return (username_ref);
	}
	FN_string *org_dir = FNSP_orgname_of(*username_dir, status);
	// use stat for printing purposes
	stat.set(status, 0, 0);
	delete username_dir;
	if (status != FN_SUCCESS) {
		FN_string *desc = stat.description();
		fprintf(stderr,
"%s: Could not obtain directory name of username context's organization: %s\n",
		    program_name,
		    desc? ((char *)(desc->str())) : "");
		if (desc)
			delete desc;
		return (username_ref);
	}

	// For each user, create a user nsid context
	void *iter_pos;
	int users;
	if (fullname.last(iter_pos)->is_empty())
		// trailing slash supplied
		users = traverse_user_list(*org_dir, username_ctx,
		    *username_ref, fullname, 1);
	else {
		FN_composite_name new_fullname(fullname);
		new_fullname.append_comp(FNSP_empty_component);
		users = traverse_user_list(*org_dir, username_ctx,
		    *username_ref, new_fullname, 1);
	}

	if (org_dir)
		delete org_dir;

	how_many += users;

	return (username_ref);
}

FN_ref *
create_hostname_context(FNSP_Context *ctx,
			const FN_composite_name &fullname,
			const FN_composite_name &name,
			int subcontexts_p,
			int &how_many)
{
	// An "hostname" context should only be bound to a name of "host/".
	// Should we make the effort to check for that here?

	FN_ref *hostname_ref = fnscreate(ctx,
	    fullname,
	    name,
	    FNSP_hostname_context,
	    how_many);

	if (subcontexts_p == 0 || hostname_ref == 0)
		return (hostname_ref);

	// Get handle to hostname context
	FN_status stat;
	FNSP_Context *hostname_ctx = FNSP_Context::from_ref(*hostname_ref,
							    stat);
	if (hostname_ctx == 0) {
		FN_string *desc = stat.description();
		fprintf(stderr,
		"%s: Could not generate context from hostname reference: %s\n",
		    program_name, (desc ? (char *)(desc->str()) : ""));
		fprintf(stderr, "%s: No host contexts were created.\n",
		    program_name);
		if (desc)
			delete desc;
		return (hostname_ref);
	}

	// Get organization directory name of hostname context
	unsigned status;
	FN_string *hostname_dir =
	    FNSP_reference_to_internal_name(*hostname_ref);
	if (hostname_dir == 0) {
		fprintf(stderr,
		    "%s: Could not obtain object name of hostname context.\n",
		    program_name);
		return (hostname_ref);
	}
	FN_string *org_dir = FNSP_orgname_of(*hostname_dir, status);
	// use stat for printing purposes
	stat.set(status, 0, 0);
	delete hostname_dir;
	if (status != FN_SUCCESS) {
		FN_string *desc = stat.description();
		fprintf(stderr,
"%s:Could not obtain directory name of hostname context's organization: %s\n",
		    desc ? (char *)(desc->str()) : "");
		if (desc)
			delete desc;
		return (hostname_ref);
	}

	// For each host, create a host nsid context
	int hosts;
	void *iter_pos;
	if (fullname.last(iter_pos)->is_empty())
		// trailing slash supplied
		hosts = traverse_host_list(*org_dir, hostname_ctx,
		    *hostname_ref, fullname, 1);
	else {
		FN_composite_name new_fullname(fullname);
		new_fullname.append_comp(FNSP_empty_component);
		hosts = traverse_host_list(*org_dir, hostname_ctx,
		    *hostname_ref, new_fullname, 1);
	}

	if (org_dir)
		delete org_dir;

	how_many += hosts;
	return (hostname_ref);
}

// For printer address and reference types
static FN_identifier
FNSP_printer_address((const unsigned char *) "onc_fn_printer_nisplus");

static FN_identifier
FNSP_printer_reftype((const unsigned char *) "onc_fn_printername");

// From usr/src/lib/printer/include/xfn/fn_p.hh
static const int FNSP_printername_context = 1;

// To Create printer binding in the service context
static int
create_printer_bindings(FN_ctx *service_ctx)
{
	FN_status status;
	const FN_string internal_string((const unsigned char *) "printers");
	FN_ref *printer_ref = FNSP_reference(FNSP_printer_address,
	    FNSP_printer_reftype, internal_string, FNSP_printername_context,
	    FNSP_normal_repr);
	if (printer_ref == 0) {
		fprintf(stderr,
		    "%s:Could not generate printer reference\n",
		    program_name);
		return (0);
	}

	FN_string printer_name_string((const unsigned char *) "printer");
	if (service_ctx->bind(printer_name_string, *printer_ref,
	    global_bind_flags, status) == 0 &&
	    status.code() != FN_E_NAME_IN_USE) {
		FN_string *desc = status.description();
		fprintf(stderr, "Printer binding failed: %s\n",
		    (char *)desc->str());
		delete desc;
		delete printer_ref;
		return (0);
	}
	delete printer_ref;
	return (1);
	// End of printer binding implementaion
}


// Creation of org context, create the bindings for service and printer.
// Also creates contexts for hostname and username contexts.
// subcontext_p determines whether these are populated.
FN_ref *
create_org_context(
	FNSP_Context *ctx,
	const FN_composite_name &fullname,
	const FN_composite_name &name,
	int subcontexts_p)
{
	// create nsid context for organization
	FN_ref *org_ref;
	FN_status status;
	int count = 0;
	FN_ref *service_ref;

	org_ref = create_nsid_and_service_context(ctx, fullname, name,
	    count, FNSP_nsid_context, &service_ref);

	if (org_ref == 0)
		return (0);

	FNSP_Context *service_ctx =
	    FNSP_Context::from_ref(*service_ref, status);
	if (service_ctx == 0) {
		FN_string *desc = status.description();
		fprintf(stderr,
	"%s:Could not generate context from service reference: %s\n",
		    program_name,
		    desc ? (char *)(desc->str()) : "");
		delete desc;
		return (0);
	}

	// Create the printer bindings here
	if (!create_printer_bindings(service_ctx)) {
		delete service_ref;
		delete service_ctx;
		return (0);
	}
	delete service_ctx;
	delete service_ref;

	if (org_ref == 0)
		return (0);

	FNSP_Context *org_ctx = FNSP_Context::from_ref(*org_ref, status);
	if (org_ctx == 0) {
		FN_string *desc = status.description();
		fprintf(stderr,
		    "%s: Could not generate context from nsid reference: %s\n",
		    program_name,
		    desc ?(char *)(desc->str()): "No status description");
		delete org_ref;
		delete desc;
		return (0);
	}

	void *iter_pos;
	FN_ref *ref;

	// construct full and relative names for "_host/"
	FN_composite_name full_hname(fullname);
	(void) full_hname.last(iter_pos);
	full_hname.insert_name(iter_pos, FNSP_host_string);
	if (ref = create_hostname_context(org_ctx,
	    full_hname, custom_hostname_name, subcontexts_p, count)) {
		FN_composite_name alias_fullname(fullname);
		(void) alias_fullname.last(iter_pos);
		alias_fullname.insert_name(iter_pos, FNSP_host_string_sc);
		fnsbind(org_ctx, alias_fullname, canonical_hostname_name,
			full_hname, *ref);
		delete ref;
	}

	// construct full and relative names for "user/"
	FN_composite_name full_uname(fullname);
	(void) full_uname.last(iter_pos);
	full_uname.insert_name(iter_pos, FNSP_user_string);
	if (ref = create_username_context(org_ctx,
	    full_uname, custom_username_name, subcontexts_p, count)) {
		FN_composite_name alias_fullname(fullname);
		(void) alias_fullname.last(iter_pos);
		alias_fullname.insert_name(iter_pos, FNSP_user_string_sc);
		fnsbind(org_ctx, alias_fullname, canonical_username_name,
			full_uname, *ref);
		delete ref;
	}

	return (org_ref);
}

/*
 * Lookup the 'parent' context in which the context is to be created.
 * If the name ends in a '/', the parent name consists of
 * fullname up to the second last '/' encountered.
 * e.g. '_org//_service/', parent = '_org//', rest = '_service/'
 *      '_org//', parent = 'org/', rest = '/'
 * If the name does not end in a '/', the parent name consists of
 * fullname up to the last '/'.
 * e.g.  '_org//_service/abc', parent = '_org//service/', rest = 'abc'
 *      '_org/abc', parent = '_org/', rest = 'abc'
 */

FNSP_Context *
lookup_parent_context(
	const FN_composite_name &fullname,
	FN_composite_name &rest,
	FN_status &status,
	FN_composite_name **save_parent_name = 0)
{
	if (fullname.count() < 2) {
		status.set(FN_E_ILLEGAL_NAME, 0, 0, &fullname);
		return (0);
	}

	FN_ctx *initial_context = FN_ctx::from_initial(status);

	if (initial_context == 0) {
		FN_string *desc = status.description();
		fprintf(stderr, "%s: Unable to get initial context: %s\n",
			program_name, desc? (char *)(desc->str()): "");
		if (desc)
			delete desc;
		exit(1);
	}

	void *lp;
	const FN_string *lastcomp = fullname.last(lp);
	const FN_string *second_lastcomp = 0;
	if (lastcomp == 0 || lastcomp->is_empty()) {
		// last component is null (i.e. names ends in '/')
		// rest is second last component plus '/'.
		// parent is first component to '/' before rest
		second_lastcomp = fullname.prev(lp);
		rest.append_comp(*second_lastcomp);
		rest.append_comp(FNSP_empty_component);
	} else {
		// last component is a component name
		rest.append_comp(*lastcomp);
	}
	FN_composite_name *parent_name = fullname.prefix(lp);

	if (parent_name == 0) {
		// we were given a name like "xxx/",
		fprintf(stderr,
		    "%s: Cannot create new bindings in the initial context.\n",
		    program_name);
		exit(1);
	}

	parent_name->append_comp(FNSP_empty_component); // append null component

	FNSP_Context *target_ctx = 0;

	FN_ref *parent_ref = initial_context->lookup(*parent_name, status);
	delete initial_context;
	if (status.is_success()) {
		target_ctx = FNSP_Context::from_ref(*parent_ref, status);
		delete parent_ref;
	} else {
		const FN_composite_name *cp = status.remaining_name();

		if (cp) {
			void *iter_pos;
			// 'srname' = remaining_name without trailing null
			// component
			// parent has trailing FNSP_empty_component;
			cp->last(iter_pos);
			FN_composite_name *srname = cp->prefix(iter_pos);

			// tag on remaining name to make status sensible
			srname->append_name(rest);
			status.set_remaining_name(srname);
			delete srname;
		} else {
			status.set_remaining_name(&rest);
		}
	}

	if (save_parent_name)
		*save_parent_name = parent_name;
	else
		delete parent_name;

	return (target_ctx);
}

int
process_org_request(const FN_composite_name &fullname, FN_status &status)
{
	FN_string *desc;
	if (fullname.count() < 2) {
		status.set(FN_E_ILLEGAL_NAME, 0, 0, &fullname);
		return (0);
	}

	// Check for the presense of ctx_dir.
	// This should be done before the intial context is obtained
	char domain[NIS_MAXNAMELEN];
	sprintf(domain, "ctx_dir.%s", nis_local_directory());
	FN_string domain_string((unsigned char *) domain);
	unsigned ret_status = FNSP_create_directory(domain_string, 0);
	if (ret_status != FN_SUCCESS) {
		status.set(ret_status, 0, 0, &fullname);
		FN_string *desc = status.description();
		fprintf(stderr, "%s: create of %s directory failed: %s\n",
		    program_name, domain,
		    desc ? ((char *)(desc->str())) : "");
		delete desc;
		return (0);
	}
	FN_composite_name rest;
	FNSP_Context *target_ctx =
	    lookup_parent_context(fullname, rest, status);
		if (target_ctx == 0) {
			desc = status.description();
			FN_string *fstr = fullname.string();
			fprintf(stderr, "%s: create of '%s' failed: %s\n",
			    program_name, (fstr ? (char *)fstr->str() : ""),
			    desc ? ((char *)(desc->str())) : "");
			delete desc;
			delete fstr;
			return (0);
	}

	FN_ref *orgnsid_ref = create_org_context(target_ctx, fullname, rest,
	    create_subcontexts_p);
	if (orgnsid_ref)
		delete orgnsid_ref;
	return (orgnsid_ref != 0);
}

int
process_hostname_request(
	FNSP_Context *target_ctx,
	const FN_composite_name &fullname,
	const FN_composite_name &name,
	const FN_composite_name &parent_name)
{
	int count = 0;
	FN_ref *hostname_ref = 0;
	if (name_is_special_token(name, FNSP_hostname_context)) {
		FN_composite_name *fn, *rn, *afn, *arn;
		if (assign_aliases(parent_name, FNSP_hostname_context, &fn, &rn,
		    &afn, &arn)) {
			hostname_ref = create_hostname_context(target_ctx,
			    *fn,
			    *rn,
			    create_subcontexts_p,
			    count);
			if (hostname_ref)
				fnsbind(target_ctx, *afn, *arn, *fn,
				    *hostname_ref);
		}
		if (fn) delete fn;
		if (afn) delete afn;
	} else {
		hostname_ref = create_hostname_context(target_ctx,
		    fullname,
		    name,
		    create_subcontexts_p,
		    count);
	}
	if (hostname_ref)
		delete hostname_ref;
	return (hostname_ref != 0);
}

int
process_username_request(
	FNSP_Context *target_ctx,
	const FN_composite_name &fullname,
	const FN_composite_name &name,
	const FN_composite_name &parent_name)
{
	int count = 0;
	FN_ref *username_ref;
	if (name_is_special_token(name, FNSP_username_context)) {
		FN_composite_name *fn, *rn, *afn, *arn;
		if (assign_aliases(parent_name, FNSP_username_context, &fn, &rn,
		    &afn, &arn)) {
			username_ref = create_username_context(target_ctx,
			    *fn,
			    *rn,
			    create_subcontexts_p,
			    count);
			if (username_ref)
				fnsbind(target_ctx, *afn, *arn, *fn,
				    *username_ref);
		}
		if (fn) delete fn;
		if (afn) delete afn;
	} else {
		username_ref = create_username_context(target_ctx,
		    fullname,
		    name,
		    create_subcontexts_p,
		    count);
	}
	if (username_ref)
		delete username_ref;
	return (username_ref != 0);
}

// Most of the work is trying to determine whether user has a password
// entry, and if so, print out a warning before creation.

int
process_user_request(
	FNSP_Context *target_ctx,
	const FN_composite_name &fullname,
	const FN_composite_name &name)
{
	unsigned nisflags = 0;  // FOLLOW_LINKS? FOLLOW_PATHS?
	nis_result* res = 0;
	int count = 0;
	FN_ref *usernsid_ref = 0;
	char tname[NIS_MAXNAMELEN+1];
	FN_composite_name fcname(fullname);
	void *iter;
	const FN_string *user_name = name.first(iter);

	// 'target_ctx' points to username context;
	// get domain name of username context
	unsigned status;
	FN_status stat;
	FN_ref *username_ref = target_ctx->get_ref(stat);
	FN_string *username_dir =
	    FNSP_reference_to_internal_name(*username_ref);
	if (username_dir == 0) {
		fprintf(stderr,
		    "%s: Could not obtain object name of username context.\n",
		    program_name);
		return (0);
	}
	FN_string *domainname = FNSP_orgname_of(*username_dir, status);
	delete username_dir;

	sprintf(tname, "[name=\"%s\"],%s.%s",
		user_name->str(), USER_SOURCE, domainname->str());
	if (tname[strlen(tname)-1] != '.')
		strcat(tname, ".");

	res = nis_list(tname, nisflags, 0, 0);

	if (res && (res->status != NIS_SUCCESS && res->status != NIS_S_SUCCESS))
		fprintf(stderr, "WARNING: user '%s' not in passwd table.\n",
		    user_name->str());

	usernsid_ref = create_user_context(target_ctx, *username_ref,
	    fullname, name, *domainname, *user_name,
	    create_subcontexts_p, count);
	delete username_ref;
	delete usernsid_ref;
	delete domainname;
	return (usernsid_ref != 0);
}

/* Create host context with name 'name' in context 'target_ctx'. */
int
process_host_request(
	FNSP_Context *target_ctx,
	const FN_composite_name &fullname,
	const FN_composite_name &name)
{
	unsigned nisflags = 0;   // FOLLOW_LINKS? FOLLOW_PATHS
	nis_result* res = 0;
	struct traverse_data td;
	char tname[NIS_MAXNAMELEN+1];
	int i = 1;  // failure by default
	int count = 0;

	// 'target_ctx' points to hostname context;
	// Get domain name of hostname context

	unsigned status;
	FN_status stat;
	FN_ref *hostname_ref = target_ctx->get_ref(stat);
	FN_string *hostname_dir =
	    FNSP_reference_to_internal_name(*hostname_ref);
	if (hostname_dir == 0) {
		fprintf(stderr,
		    "%s: Could not obtain object name of hostname context.\n",
		    program_name);
		return (0);
	}
	FN_string *domainname = FNSP_orgname_of(*hostname_dir, status);
	delete hostname_dir;

	// Get name of hostname context and hostname from fullname
	void *iter;
	const FN_string *host_name;
	const FN_string *trailer = fullname.last(iter);

	if (trailer->is_empty())			// Trailing slash
		host_name = fullname.prev(iter);  	//  next last is host
	else
		host_name = trailer;
	FN_composite_name *parent_name = fullname.prefix(iter);

	if (parent_name == 0) {
		// we were given a name like "xxx/"
		fprintf(stderr,
			"%s: Cannot create bindings in the initial context.\n",
			program_name);
		delete hostname_ref;
		return (0);
	}

	parent_name->append_comp(FNSP_empty_component);

	// Construct arguments for process_host_entry call
	td.parent = target_ctx;
	td.parent_ref = hostname_ref;
	td.parent_name = parent_name;
	td.subcontext_p = create_subcontexts_p;
	td.domain_name = domainname;
	td.count = 0;

	// Construct NIS+ entry name for host
	sprintf(tname, "[name=\"%s\"],%s.%s",
		host_name->str(), HOST_SOURCE, domainname->str());
	if (tname[strlen(tname)-1] != '.')
		strcat(tname, ".");

	res = nis_list(tname, nisflags, 0, 0);  // retrieve entry for host

	if (res && (res->status == NIS_SUCCESS ||
	    res->status == NIS_S_SUCCESS)) {
		// process_host_entry returns 0 if OK, 1 for error
		i = process_host_entry(0, &(res->objects.objects_val[0]),
		    (void *)&td);
	} else {
		fprintf(stderr, "WARNING: host '%s' not in hosts table.\n",
		    host_name->str());
		FN_ref *ref = create_host_context(target_ctx, *hostname_ref,
		    fullname, name,
		    *domainname,
		    *host_name,
		    create_subcontexts_p, count);
		i = (ref != 0);
		delete ref;
	}
	delete hostname_ref;
	delete domainname;
	delete parent_name;
	if (res)
		nis_freeresult(res);
	return (i == 0);
}

int
process_site_request(
	FNSP_Context *target_ctx,
	const FN_composite_name &fullname,
	const FN_composite_name &name,
	const FN_composite_name &parent_name)
{
	int count = 0;
	FN_ref *site_ref = 0;

	if (name_is_special_token(name, FNSP_site_context)) {
		FN_composite_name *fn, *afn;
		const FN_composite_name *rn, *arn;
		if (assign_aliases(parent_name, FNSP_site_context, &fn, &rn,
		    &afn, &arn)) {
			site_ref = create_site_context(target_ctx,
			    *fn,
			    *rn,
			    create_subcontexts_p,
			    count);
			if (site_ref)
				fnsbind(target_ctx, *afn, *arn, *fn, *site_ref);
		}
		if (fn) delete fn;
		if (afn) delete afn;
	} else {
		site_ref = create_site_context(target_ctx, fullname, name,
		    create_subcontexts_p, count);
	}
	if (site_ref)
		delete site_ref;
	return (site_ref != 0);
}


int
process_service_request(FNSP_Context *target_ctx,
			const FN_composite_name &fullname,
			const FN_composite_name &name,
			const FN_composite_name &parent_name)
{
	int count = 0;
	FN_ref *service_ref;
	if (name_is_special_token(name, FNSP_service_context)) {
		FN_composite_name *fn, *afn;
		const FN_composite_name *rn, *arn;
		if (assign_aliases(parent_name, FNSP_service_context, &fn, &rn,
		    &afn, &arn)) {
			service_ref = create_service_context(target_ctx,
			    *fn,
			    *rn,
			    count);
			if (service_ref)
				fnsbind(target_ctx, *afn, *arn, *fn,
				    *service_ref);
		}
		if (fn) delete fn;
		if (afn) delete afn;
	} else {
		service_ref = create_service_context(target_ctx, fullname,
		    name, count);
	}
	if (service_ref)
		delete service_ref;
	return (service_ref != 0);
}

int
process_generic_request(FNSP_Context *target_ctx,
			const FN_composite_name &fullname,
			const FN_composite_name &name,
			const char * ref_type)
{
	int count = 0;
	FN_ref *ref = create_generic_context(target_ctx, fullname,
	    name, count, ref_type);

	if (ref)
		delete ref;
	return (ref != 0);
}

int
process_nsid_request(
	FNSP_Context *target_ctx,
	const FN_composite_name &fullname,
	const FN_composite_name &name)
{
	int count = 0;
	FN_ref *nsid_ref = create_nsid_context(target_ctx, fullname, name,
	    count);
	delete nsid_ref;
	return (nsid_ref != 0);
}

int
process_fs_request(
	FNSP_Context *target_ctx,
	const FN_composite_name & /* fullname */,
	const FN_composite_name &name,
	const FN_composite_name &parent_name)
{
	int count = 0;
	FN_status status;
	FN_ref *parent_ref = target_ctx->get_ref(status);
	const FN_identifier *reftype;

	if (parent_ref == 0 || (reftype = parent_ref->type()) == 0)
		return (0);

	// determine whether parent is a user or a host
	unsigned int parent_type;
	if (*reftype == *FNSP_reftype_from_ctxtype(FNSP_host_context))
		parent_type = FNSP_host_context;
	else if (*reftype == *FNSP_reftype_from_ctxtype(FNSP_user_context))
		parent_type = FNSP_user_context;
	else {
		fprintf(stderr,
"%s: can only create fs bindings for a host or a user\n",
			program_name);
		delete parent_ref;
		return (0);
	}

	// determine domain name
	FN_string *internal_name =
	    FNSP_reference_to_internal_name(*parent_ref);
	if (internal_name == 0) {
		fprintf(stderr,
		    "%s: Could not obtain internal name of target context.\n",
		    program_name);
		delete parent_ref;
		return (0);
	}
	unsigned int s;
	FN_string *domain_name = FNSP_orgname_of(*internal_name, s);
	delete internal_name;
	if (domain_name == 0) {
		fprintf(stderr,
		    "%s: Could not obtain directory name of target context.\n",
		    program_name);
		delete parent_ref;
		return (0);
	}

	// determine object name (i.e. host or user name)
	void *iter;
	const FN_string *obj_name = parent_name.last(iter);
	if (obj_name && obj_name->is_empty())
		obj_name = parent_name.prev(iter);
	if (obj_name == 0) {
		fprintf(stderr,
		    "%s: Could not obtain object name of target context.\n",
		    program_name);
		delete domain_name;
		delete parent_ref;
		return (0);
	}

	int bind_status;
	if (name_is_special_token(name, FNSP_fs_context)) {
		switch (parent_type) {
		case FNSP_host_context:
			bind_status = create_host_fs(target_ctx, parent_name,
						*obj_name, *domain_name);
			break;
		case FNSP_user_context:
			bind_status = create_user_fs(target_ctx, parent_name,
						*obj_name, *domain_name);
			break;
		}
	} else {
		switch (parent_type) {
		case FNSP_host_context:
			bind_status = create_host_fs(target_ctx, parent_name,
			    *obj_name, *domain_name, &name);
			break;
		case FNSP_user_context:
			bind_status = create_user_fs(target_ctx, parent_name,
			    *obj_name, *domain_name, &name);
			break;
		}
	}

	delete domain_name;
	delete parent_ref;
	return (bind_status);
}


// Returns 1 if error encountered; 0 if OK
static int
process_user_entry_aux(char *user_str, int len, struct traverse_data *td)
{
	FN_ref *ref;
	FN_string user_name((unsigned char *)user_str, len);
	void *iter_pos;

	// Generate full and relative names of "<user>/"
	FN_composite_name user_nsid(user_name);
	user_nsid.append_comp(FNSP_empty_component);
	FN_composite_name user_full_nsid(*(td->parent_name));
	(void) user_full_nsid.last(iter_pos);
	user_full_nsid.insert_comp(iter_pos, user_name);

	// Create context using user
	ref = create_user_context(td->parent,
	    *(td->parent_ref),
	    user_full_nsid,
	    user_nsid,
	    *(td->domain_name),
	    user_name,
	    td->subcontext_p,
	    td->count);
	if (ref) {
		delete ref;
		return (0);
	}
	return (1);
}

#define	MAXINPUTLEN 256

extern FILE *get_user_file(const char *, const char *);
extern FILE *get_host_file(const char *, const char *);
extern void free_user_file(FILE *);
extern void free_host_file(FILE *);

// Returns 1 if error encountered; 0 if OK
static int
process_user_entry(char *, nis_object *ent, void *udata)
{
	struct traverse_data *td = (struct traverse_data *) udata;
	long entry_type;

	// extract user name from entry
	entry_type = *(long *)
	    (ent->EN_data.en_cols.en_cols_val[0].ec_value.ec_value_val);

	if (entry_type == ENTRY_OBJ) {
		fprintf(stderr, "Encountered object that is not an entry\n");
		return (1);
	}

	return (process_user_entry_aux(ENTRY_VAL(ent, 0),
	    ENTRY_LEN(ent, 0), td));
}


int
traverse_user_list(
	const FN_string &domainname,
	FNSP_Context *parent,
	const FN_ref &parent_ref,
	const FN_composite_name &parent_name,
	int subcontext_p)
{
	struct traverse_data td;
	char *user;
	FILE *userfile;
	char line[MAXINPUTLEN];

	td.parent = parent;
	td.parent_ref = &parent_ref;
	td.parent_name = &parent_name;
	td.domain_name = &domainname;
	td.subcontext_p = subcontext_p;
	td.count = 0;

	if (input_file) {
		userfile = fopen(input_file, "r");
		if (userfile == 0) {
			fprintf(stderr, "%s: Could not open input file '%s'.",
				program_name, input_file);
		}
	} else
		userfile = get_user_file(program_name,
		    (const char *)domainname.str());
	if (userfile == NULL)
		return (0);

	while (fgets(line, MAXINPUTLEN, userfile) != NULL) {
		user = strtok(line, "\n\t ");
		if (user == 0 ||
		    (process_user_entry_aux(user, strlen(user), &td) != 0))
			break;
	}
	if (input_file)
		fclose(userfile);
	else
		free_user_file(userfile);
	return (td.count);
}


// Return 1 if error; 0 if OK
int
process_host_entry_aux(
	char *chost,
	int clen,
	char *host,
	int len,
	struct traverse_data *td)
{
	FN_ref *ref;
	FN_string can_host((unsigned char *)chost, clen);
	FN_string alias_host((unsigned char *)host, len);

	ref = create_host_context_can(td->parent,
	    *(td->parent_ref),
	    *(td->parent_name),
	    can_host,
	    alias_host,
	    *(td->domain_name),
	    td->subcontext_p,
	    td->count);
	if (ref) {
		delete ref;
		return (0);
	}
	return (1);
}


// Return 1 if error encountered; 0 if OK
int
process_host_entry(char *, nis_object *ent, void *udata)
{
	struct traverse_data *td = (struct traverse_data *) udata;
	long entry_type;

	// extract host name from entry
	entry_type = *(long *)
	    (ent->EN_data.en_cols.en_cols_val[0].ec_value.ec_value_val);

	if (entry_type == ENTRY_OBJ) {
		fprintf(stderr, "Encountered object that is not an entry\n");
		return (1);
	}

	return (process_host_entry_aux(ENTRY_VAL(ent, 0), ENTRY_LEN(ent, 0),
	    ENTRY_VAL(ent, 1), ENTRY_LEN(ent, 1),
	    td));
}

int
traverse_host_list(
	const FN_string &domainname,
	FNSP_Context *parent,
	const FN_ref &parent_ref,
	const FN_composite_name &parent_name,
	int subcontext_p)
{
	struct traverse_data td;
	char line[MAXINPUTLEN];
	char *can_host, *host;
	FILE *hostfile;

	td.parent = parent;
	td.parent_ref = &parent_ref;
	td.parent_name = &parent_name;
	td.domain_name = &domainname;
	td.subcontext_p = subcontext_p;
	td.count = 0;

	if (input_file) {
		hostfile = fopen(input_file, "r");
		if (hostfile == 0) {
			fprintf(stderr, "%s: Could not open input file '%s'.",
				program_name, input_file);
		}
	} else
		hostfile = get_host_file(program_name,
		    (const char *)domainname.str());
	if (hostfile == NULL)
		return (0);

	while (fgets(line, MAXINPUTLEN, hostfile) != NULL) {
		can_host = strtok(line, "\t \n");
		host = strtok(NULL, " \t\n");
		if (can_host == 0)
			break;
		if (host == 0)
			host = can_host;
		if (process_host_entry_aux(can_host, strlen(can_host),
		    host, strlen(host), &td) != 0)
			break;
	}
	if (input_file)
		fclose(hostfile);
	else
		free_host_file(hostfile);
	return (td.count);
}

main(int argc, char **argv)
{
	process_cmd_line(argc, argv);

	FN_status status;
	int exit_status = 1;
	FN_composite_name relative_name;
	FN_composite_name fullname((unsigned char *)target_name_str);
	FN_composite_name *parent_name;
	FN_string *desc;

	// If request to create is for organization
	// then check for ctx_dir, so that from_initial will work fine
	if (context_type == FNSP_organization_context) {
		if ((create_subcontexts_p == 0 || check_nis_group()) &&
		    process_org_request(fullname, status))
			exit_status = 0;
		exit(exit_status);
	}

	FNSP_Context *target_ctx = lookup_parent_context(fullname,
	    relative_name,
	    status,
	    &parent_name);

	if (target_ctx == 0) {
		desc = status.description();
		fprintf(stderr, "%s: create of '%s' failed: %s\n",
			program_name,
			target_name_str,
			desc ? ((char *)(desc->str())) : "");
		if (desc)
			delete desc;
		exit(1);
	}

	switch (context_type) {
	case FNSP_hostname_context:
		if ((create_subcontexts_p == 0 || check_nis_group()) &&
		    process_hostname_request(target_ctx, fullname,
		    relative_name, *parent_name))
			exit_status = 0;
		break;

	case FNSP_username_context:
		if ((create_subcontexts_p == 0 || check_nis_group()) &&
		    process_username_request(target_ctx, fullname,
		    relative_name, *parent_name))
			exit_status = 0;
		break;

	case FNSP_user_context:
		if (check_nis_group() &&
		    process_user_request(target_ctx, fullname, relative_name))
			exit_status = 0;
		break;

	case FNSP_host_context:
		if (check_nis_group() &&
		    process_host_request(target_ctx, fullname, relative_name))
			exit_status = 0;
		break;

	case FNSP_site_context:
		if (process_site_request(target_ctx, fullname, relative_name,
					 *parent_name))
			exit_status = 0;
		break;

	case FNSP_service_context:
		if (process_service_request(target_ctx, fullname, relative_name,
		    *parent_name))
			exit_status = 0;
		break;

	case FNSP_nsid_context:
		if (check_null_trailer(fullname, relative_name) &&
		    process_nsid_request(target_ctx, fullname, relative_name))
			exit_status = 0;
		break;

	case FNSP_generic_context:
		if (process_generic_request(target_ctx, fullname, relative_name,
		    reference_type))
			exit_status = 0;
		break;

	case FNSP_fs_context:
		if (process_fs_request(target_ctx, fullname, relative_name,
		    *parent_name))
			exit_status = 0;
		break;
	default:
		fprintf(stderr, "%s: unknown context type: %d\n",
			argv[0], context_type);
		break;
	}

	if (parent_name)
		delete parent_name;
	exit(exit_status);
}


#include <rpc/rpc.h>  /* for XDR */

static int
__fns_xdr_encode_string(const char *str, char *buffer, size_t &len)
{
	XDR	xdr;

	xdrmem_create(&xdr, (caddr_t)buffer, len, XDR_ENCODE);
	if (xdr_string(&xdr, (char **)&str, ~0) == FALSE) {
		return (0);
	}

	len = xdr_getpos(&xdr);
	xdr_destroy(&xdr);
	return (1);
}
