/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)fnsp_printer_internal.cc	1.8 95/01/29 SMI"

#include <sys/time.h>
#include <sys/socket.h>
#include <rpcsvc/nis.h>
#include <string.h>
#include <stdlib.h> // for strtol
#include <malloc.h>

#include <xfn/fn_xdr.hh>
#include <xfn/fn_p.hh>
#include <xfn/fn_printer_p.hh>
#include "fnsp_printer_internal.hh"
#include "../FNSP_printer_Syntax.hh"


// Routines to understand various printer addresses

enum FNSP_binding_type {
	FNSP_bound_reference = 0,
	FNSP_child_context = 1
};

static const unsigned long FNSP_nisflags = USE_DGRAM;

static const FN_identifier
FNSP_printername_reftype((unsigned char *) "onc_fn_printername");

static const FN_identifier
FNSP_printer_reftype((unsigned char *) "onc_printers");

static const FN_identifier
FNSP_printer_nisplus_address_type((unsigned char *) "onc_fn_printer_nisplus");

/*
 *
 * Internal routines used by FNSP context implementations.
 *
 * Contains routines that make NIS+ library calls.
 */

static const char nis_default_separator = ' ';
static const char *nis_default_table_type = "H";
static const char *FNSP_name_col_label = "atomicname";
static const char *FNSP_ctx_col_label = "contextname";

static const char FNSP_internal_name_separator = '_';
static const char FNSP_default_char = '#';

static const FN_string FNSP_context_directory((unsigned char *) "ctx_dir.");
static const int FNSP_cd_size = FNSP_context_directory.charcount();

static const FN_string FNSP_org_directory((unsigned char *) "org_dir.");
static const int FNSP_od_size = FNSP_org_directory.charcount();

static const FN_string FNSP_self_name((unsigned char *) "_FNS_self_");
static const FN_string FNSP_prefix((unsigned char *) "fns");

#define	FNSP_CTX_COL 4
#define	FNSP_NAME_COL 0
#define	FNSP_REF_COL 1
#define	FNSP_BIND_COL 2
#define	FNSP_ATTR_COL 3
#define	FNSP_WIDE_TABLE_WIDTH 5
#define	FNSP_NARROW_TABLE_WIDTH 3

#define	FNSP_DEFAULT_TTL 43200

#define	NOBODY_RIGHTS ((NIS_READ_ACC|NIS_CREATE_ACC) << 24)
#define	WORLD_RIGHTS (NIS_READ_ACC|NIS_CREATE_ACC)
#define	GROUP_RIGHTS ((NIS_READ_ACC |\
	NIS_MODIFY_ACC |\
	NIS_CREATE_ACC |\
	NIS_DESTROY_ACC) << 8)
#define	FNSP_DEFAULT_RIGHTS (NOBODY_RIGHTS | WORLD_RIGHTS | OWNER_DEFAULT | \
	GROUP_RIGHTS)

#define	ENTRY_FLAGS(obj, col) \
	(obj)->EN_data.en_cols.en_cols_val[col].ec_flags

extern "C" {
nis_result * __nis_list_localcb(nis_name, u_long,
    int (*)(nis_name, nis_object *, void *), void *);
bool_t xdr_nis_server(XDR*, nis_server*);
};

static inline fnsp_meta_char(char c)
{
	return (c == FNSP_internal_name_separator || c == FNSP_default_char);
}

static inline nis_bad_value_char(char c)
{
	switch (c) {
	case '.':
	case '[':
	case ']':
	case ',':
	case '=':
	case '"': // not illegal but causes problems
		return (1);
	default:
		return (0);
	}
}

// Characters that are used by NIS+ to terminate a name
static inline nis_terminal_char(char c)
{
	switch (c) {
	case '.':
	case '[':
	case ']':
	case ',':
	case '=':
	case '"': // not illegal but causes problems
	case '/':  // not NIS+ reserved but NIS+ will reject it
		return (1);
	default:
		return (0);
	}
}

// Characters that cannot be leading characters in a NIS+ name
static inline nis_bad_lead_char(char c)
{
	switch (c) {
	case '@':
	case '+':
	case '-':
	case '"': // not illegal but causes problems
		return (1);
	default:
		return (0);
	}
}

// ********************** nisplus interface routines *******************

static unsigned
#ifdef DEBUG
FNSP_map_status(nis_error nisstatus, char *msg)
#else
FNSP_map_status(nis_error nisstatus, char *)
#endif /* DEBUG */
{
#ifdef DEBUG
	if (nisstatus != NIS_SUCCESS && msg != 0) {
		nis_perror(nisstatus, msg);
	}
#endif DEBUG

	switch (nisstatus) {
	case NIS_SUCCESS:
	case NIS_S_SUCCESS:
		return (FN_SUCCESS);
	case NIS_NOTFOUND:
	case NIS_PARTIAL:
		return (FN_E_NAME_NOT_FOUND);
	case NIS_BADNAME:
	case NIS_BADATTRIBUTE:
		return (FN_E_ILLEGAL_NAME);
	case NIS_NOSUCHNAME:
	case NIS_NOSUCHTABLE:
		return (FN_E_NOT_A_CONTEXT);
		// %%% was: context_not_found
	case NIS_NOMEMORY:
	case NIS_NOFILESPACE:
	case NIS_NOPROC:
	case NIS_RES2BIG:
		return (FN_E_INSUFFICIENT_RESOURCES);
	case NIS_S_NOTFOUND:
	case NIS_TRYAGAIN:
	case NIS_UNAVAIL:
		return (FN_E_CTX_UNAVAILABLE);
	case NIS_RPCERROR:
	case NIS_NAMEUNREACHABLE:
	case NIS_CBERROR:
		return (FN_E_COMMUNICATION_FAILURE);
	case NIS_PERMISSION:
	case NIS_CLNTAUTH:
	case NIS_SRVAUTH:
		return (FN_E_CTX_NO_PERMISSION);
	case NIS_NAMEEXISTS:
		return (FN_E_NAME_IN_USE);
	case NIS_FOREIGNNS:
		/* should try to continue with diff ns?  */
		/* return FN_E_continue */
	default:
		return (FN_E_UNSPECIFIED_ERROR); /* generic error */
	}
}


// Maps NIS+ result in nis_result structure to FN_ctx status code
static unsigned
FNSP_map_result(nis_result *res, char *msg)
{
	nis_error nisstatus;

	if (res) {
		nisstatus = res -> status;
	} else {
		nisstatus = NIS_NOMEMORY;
	}

	return (FNSP_map_status(nisstatus, msg));
}

static inline void
free_nis_result(nis_result *res)
{
	if (res)
		nis_freeresult(res);
}

int
FNSP_printer_nisplus_address_p(const FN_ref_addr &addr)
{
	return ((*addr.type()) ==
		FNSP_printer_nisplus_address_type);
}

const FN_identifier &
FNSP_printer_nisplus_address_type_name()
{
	return (FNSP_printer_nisplus_address_type);
}

const FN_identifier &
FNSP_printername_reftype_name()
{
	return (FNSP_printername_reftype);
}

const FN_identifier &
FNSP_printer_reftype_name()
{
	return (FNSP_printer_reftype);
}

const FN_identifier *
FNSP_printer_reftype_from_ctxtype(unsigned context_type)
{
	const FN_identifier *answer = 0;

	switch (context_type) {
	case FNSP_printername_context:
		answer = &FNSP_printername_reftype;
		break;
	case FNSP_printer_object:
		answer = &FNSP_printer_reftype;
		break;
	}
	return (answer);
}

static FN_ref *
FNSP_extract_lookup_result(nis_result* res, unsigned &status,
    FNSP_binding_type *btype)
{
	FN_ref *ref;

	/* extract reference */
	ref = FN_ref_xdr_deserialize(ENTRY_VAL(res->objects.objects_val,
	    FNSP_REF_COL),
	    ENTRY_LEN(res->objects.objects_val,
	    FNSP_REF_COL),
	    status);
	/* extract binding type */
	if (btype) {
		int blen = ENTRY_LEN(res->objects.objects_val, FNSP_BIND_COL);
		char *bstr = ENTRY_VAL(res->objects.objects_val,
		    FNSP_BIND_COL);
		unsigned bnum = 3;
		// neither FNSP_(child_context,bound_reference)

		if (blen == 1)
			bnum = bstr[0];

		switch (bnum) {
		case FNSP_child_context:
		case FNSP_bound_reference:
			*btype = (FNSP_binding_type) bnum;
			break;
		default:
#ifdef DEBUG
			fprintf(stderr,
			    "FNSP_lookup_binding aux: bad btype %d\n",
			    bstr[0]);
#endif
			*btype = FNSP_bound_reference;
			// default appropriate ???
		}
	}

	return (ref);
}


static FN_ref *
FNSP_lookup_binding_shared(const FN_string &tabname,
    const FN_string &cname,
    const FN_string &aname,
    unsigned &status,
    FNSP_binding_type *btype,
    unsigned nisflags)
{
	char sname[NIS_MAXNAMELEN+1];
	FN_ref *ref = 0;
	nis_result *res;
	nis_name tablename = (nis_name)tabname.str(&status);

	if (status != FN_SUCCESS)
		return (0);

	sprintf(sname, "[%s=\"%s\",%s=\"%s\"],%s",
		FNSP_name_col_label, aname.str(&status),
		FNSP_ctx_col_label, cname.str(&status),
		tablename);
	if (sname[strlen(sname)-1] != '.')
		strcat(sname, ".");

	if (status != FN_SUCCESS)
		return (0);

	res = nis_list(sname, FNSP_nisflags|nisflags, NULL, NULL);
	status = FNSP_map_result(res, 0);

	if (status == FN_SUCCESS) {
		ref = FNSP_extract_lookup_result(res, status, btype);
	}
	free_nis_result(res);
	return (ref);
}


static FN_ref *
FNSP_lookup_binding_aux(const FNSP_printer_Address& parent,
    const FN_string &aname,
    unsigned &status,
    FNSP_binding_type* btype,
    unsigned nisflags)
{
	nisflags |= parent.get_access_flags();

	switch (parent.get_impl_type()) {
	case FNSP_printer_entries_impl:
		return FNSP_lookup_binding_shared(parent.get_table_name(),
		    parent.get_index_name(),
		    aname, status, btype, nisflags);
	default:
		return (0);
	}
}

// Lookup atomic name 'aname' in context associated with 'parent'.
// Return status of operation in 'status', and reference, if found as ret val.

FN_ref *
FNSP_printer_lookup_binding(const FNSP_printer_Address& parent,
    const FN_string &aname,
    unsigned &status)
{
	return (FNSP_lookup_binding_aux(parent, aname, status, 0, 0));
}


// narrow:
// Add entry consisting of (aname, ref, bind_type) to the bindings table
// associated with 'tabname'.
// wide:
// Add entry consisting of (catt, aname, ref, bind_type)
// 'flags', if set appropriately, allows the existing entry, if any, to
// be overwritten.

static unsigned
FNSP_add_binding_entry(const FN_string &tabname,
    const FN_string &aname,
    const FN_ref &ref,
    unsigned flags,
    FNSP_binding_type bind_type,
    const FN_string *cname = 0)
{
	char sname[NIS_MAXNAMELEN+1];
	nis_object obj;
	entry_col	*cols;
	entry_obj	*eo;
	char *refbuf;
	nis_result *res;
	int len;
	unsigned status;
	char bt_buf[1];
	nis_name tablename = (nis_name)tabname.str(&status);
	char atomic_tablename[NIS_MAXNAMELEN+1];
	int num_cols;
	int wide = (cname? 1 : 0);

	if (status != FN_SUCCESS)
		return (FN_E_UNSPECIFIED_ERROR);

	if (wide)
		num_cols = FNSP_WIDE_TABLE_WIDTH;
	else
		num_cols = FNSP_NARROW_TABLE_WIDTH;

	cols = (entry_col*) calloc(num_cols, sizeof (entry_col));
	if (cols == 0)
		return (FN_E_INSUFFICIENT_RESOURCES);

	memset((char *)(&obj), 0, sizeof (obj));

	nis_leaf_of_r(tablename, atomic_tablename, NIS_MAXNAMELEN);
	obj.zo_name = atomic_tablename;
	obj.zo_group = nis_local_group();
	obj.zo_owner = nis_local_principal();
	obj.zo_access = FNSP_DEFAULT_RIGHTS;
	obj.zo_ttl = FNSP_DEFAULT_TTL;
	obj.zo_data.zo_type = ENTRY_OBJ;
	eo = &(obj.EN_data);
	eo->en_type = (char *) nis_default_table_type;  // %%% OK?
	eo->en_cols.en_cols_val = cols;
	eo->en_cols.en_cols_len = num_cols;

	ENTRY_VAL(&obj, FNSP_NAME_COL) = (nis_name) (aname.str(&status));
	ENTRY_LEN(&obj, FNSP_NAME_COL) = aname.bytecount() + 1;

	refbuf = FN_ref_xdr_serialize(ref, len);
	if (refbuf == NULL) {
		free(cols);
		return (FN_E_INSUFFICIENT_RESOURCES);
	}

	ENTRY_VAL(&obj, FNSP_REF_COL) = refbuf;
	ENTRY_LEN(&obj, FNSP_REF_COL) = len;
	ENTRY_FLAGS(&obj, FNSP_REF_COL) = EN_BINARY;

	bt_buf[0] = bind_type;
	ENTRY_VAL(&obj, FNSP_BIND_COL) = &bt_buf[0];
	ENTRY_LEN(&obj, FNSP_BIND_COL) = 1;
	ENTRY_FLAGS(&obj, FNSP_BIND_COL) = EN_BINARY;

	if (!wide) {
		sprintf(sname, "[%s=\"%s\"],%s", FNSP_name_col_label,
			aname.str(&status), tablename);
	} else {
		/* wide table format has 'context' column */
		ENTRY_VAL(&obj, FNSP_CTX_COL) = (nis_name)
		    (cname->str(&status));
		ENTRY_LEN(&obj, FNSP_CTX_COL) = cname->bytecount() + 1;

		sprintf(sname, "[%s=\"%s\",%s=\"%s\"],%s",
			FNSP_name_col_label, aname.str(&status),
			FNSP_ctx_col_label, cname->str(&status),
			tablename);

		/* initialize attribute column */
		ENTRY_VAL(&obj, FNSP_ATTR_COL) = 0;
		ENTRY_LEN(&obj, FNSP_ATTR_COL) = 0;
		ENTRY_FLAGS(&obj, FNSP_ATTR_COL) = EN_BINARY;
	}
	if (sname[strlen(sname)-1] != '.')
		strcat(sname, ".");

	res = nis_add_entry(sname, &obj,
	    (flags&FN_OP_EXCLUSIVE) ? 0 : ADD_OVERWRITE);
	status = FNSP_map_result(res, "could not add entry");

	// not found must mean table does not exist.
	if (status == FN_E_NAME_NOT_FOUND)
		status = FN_E_NOT_A_CONTEXT; // %%% CONTEXT_NOT_FOUND;

	if (refbuf)
		free(refbuf);
	free_nis_result(res);
	free(cols);
	return (status);
}

static unsigned
FNSP_add_binding_aux(const FNSP_printer_Address& parent,
    const FN_string &aname,
    const FN_ref &ref,
    unsigned flags,
    FNSP_binding_type bind_type)
{
	switch (parent.get_impl_type()) {
	case FNSP_printer_entries_impl:
		return FNSP_add_binding_entry(parent.get_table_name(),
		    aname, ref, flags, bind_type,
		    &(parent.get_index_name()));
	default:
#ifdef DEBUG
		fprintf(stderr, "bad printer 1 implemantation type = %d\n",
		    parent.get_impl_type());
#endif /* DEBUG */
		return (FN_E_MALFORMED_REFERENCE);
	}
}


// Checks whether 'newref' contains a FNSP address that's in 'oref'.
//
// Returns 0 if the FNSP address in 'oref' is not present in 'newref'.
// Otherwise returns 1.
//
// POLICY: Allow only ONE FNSP address in a reference

static int
check_if_old_addr_present(const FN_ref &oref, const FN_ref &newref)
{
	void	*iter;
	const FN_ref_addr *a1, *a2;

	for (a2 = newref.first(iter); a2; a2 = newref.next(iter)) {
		if (FNSP_printer_nisplus_address_p(*a2)) {
			// found a FNSP address in newref,
			// now look for one in the
			// old reference
			a1 = oref.first(iter);
			for (a1 = oref.first(iter); a1; a1 = oref.next(iter))
				if (FNSP_printer_nisplus_address_p(*a1))
					break;
			if (!a1)
				return (0);
			// no FNSP Address in oref? should not happen
			break;
		}
	}
	if (!a2)
		return (0);
	// no FNSP address in New Reference
	// now compare the two addresses to make sure they are identical
	if (a1->length() == a2->length() &&
	    (memcmp(a1->data(), a2->data(), a1->length()) == 0) &&
	    (*(a1->type()) == *(a2->type())))
		return (1);
	else
		return (0);
}

// Add new binding (aname, ref) to bindings table associated with 'parent'.
//
// Policy:  Cannot overwrite a child reference if the new reference does not
//	    atleast have an address of the original FNSP context

unsigned
FNSP_printer_add_binding(const FNSP_printer_Address& parent,
    const FN_string &aname,
    const FN_ref &ref,
    unsigned flags)
{
	unsigned status;

	if (flags&FN_OP_EXCLUSIVE)
		return FNSP_add_binding_aux(parent, aname, ref, flags,
		    FNSP_bound_reference);

	// bind_supercede:  check that we are not overwriting a child reference
	FNSP_binding_type btype;
	unsigned lstatus;
	FN_ref *oref = FNSP_lookup_binding_aux(parent, aname, lstatus, &btype,
	    MASTER_ONLY);

	if (lstatus == FN_E_NAME_NOT_FOUND ||
	    (lstatus == FN_SUCCESS && btype != FNSP_child_context)) {
		// binding does not exist or not a child context
		status = FNSP_add_binding_aux(parent, aname, ref, flags,
		    FNSP_bound_reference);
	} else if (lstatus == FN_SUCCESS && btype == FNSP_child_context) {
		// binding to child
		// check if new address list contains the old address
		if (check_if_old_addr_present(*oref, ref) == 0)
		    status = FN_E_NAME_IN_USE;
		else
			status = FNSP_add_binding_aux(parent, aname, ref,
			    flags, FNSP_child_context);
	} else {
		// some other kind of error
		status = lstatus;
	}
	delete oref;
	return (status);
}


// Remove entry with atomic name 'aname' from bindings table associated with
// 'tabname'.
// Return status of operation.
// aname is the atomic name
// cname is the context name
//
// If aname == 0, that means remove all names identified by cname
// If cname == 0, that means table does not have further context name
// qualification (i.e. entire table is the context)

static unsigned
FNSP_remove_binding_entry(const FN_string &tabname,
    const FN_string *aname,
    const FN_string *cname = 0)
{
	char sname[NIS_MAXNAMELEN+1];
	nis_result *res;
	unsigned status;
	nis_name tablename = (nis_name)tabname.str(&status);

	if (aname) {
		if (cname == 0)
			sprintf(sname, "[%s=\"%s\"],%s",
				FNSP_name_col_label, aname->str(&status),
				tablename);
		else
			sprintf(sname, "[%s=\"%s\",%s=\"%s\"],%s",
				FNSP_name_col_label, aname->str(&status),
				FNSP_ctx_col_label, cname->str(&status),
				tablename);
	} else {
		// no atomic name specified;
		// remove all entries qualified by context information
		if (cname == 0)
			sprintf(sname, "[],%s",	tablename);
		else
			sprintf(sname, "[%s=\"%s\"],%s",
				FNSP_ctx_col_label, cname->str(&status),
				tablename);
	}

	if (sname[strlen(sname)-1] != '.')
	    strcat(sname, ".");

	res = nis_remove_entry(sname, 0, 0);
	status = FNSP_map_result(res, "could not remove entry");
	free_nis_result(res);
	return (status);
}

static unsigned
FNSP_remove_binding_aux(const FNSP_printer_Address& parent,
    const FN_string &aname)
{
	switch (parent.get_impl_type()) {
	case FNSP_printer_entries_impl:
		return (FNSP_remove_binding_entry(parent.get_table_name(),
		    &aname, &(parent.get_index_name())));
	default:
#ifdef DEBUG
		fprintf(stderr, "bad printer 2 implemantation type = %d\n",
		    parent.get_impl_type());
#endif /* DEBUG */
		return (FN_E_MALFORMED_REFERENCE);
	}
}


// Remove entry with atomic name 'aname' from bindings table of 'parent'.
// Policies:
// 1. Cannot remove binding of a child context if child context still exists
//    (must explicitly destroy)
// 2. return 'success' if binding was not there to begin with.

unsigned
FNSP_printer_remove_binding(const FNSP_printer_Address& parent,
    const FN_string &aname)
{
	FNSP_binding_type btype;
	unsigned status;
	FN_ref *ref = FNSP_lookup_binding_aux(parent, aname, status, &btype,
	    MASTER_ONLY);
	switch (status) {
	case FN_SUCCESS:
		if (btype == FNSP_child_context) {
			FNSP_printer_Address child(*ref);
			if (child.get_context_type() == 0)
				status = FN_E_MALFORMED_REFERENCE;
			// appropriate?
			else {
				switch (FNSP_printer_context_exists(child)) {
				case FN_E_NOT_A_CONTEXT:
					// %%% was context_not_found
					// context no longer exists;
					// go ahead and unbind
					status = FNSP_remove_binding_aux(
					    parent, aname);
					break;
				case FN_SUCCESS:
					// must explicitly destroy to
					// avoid orphan
					status = FN_E_NAME_IN_USE;
					break;
				default:
					// cannot determine state of
					// child context
					status = FN_E_OPERATION_NOT_SUPPORTED;
				}
			}
		} else
			status = FNSP_remove_binding_aux(parent, aname);
		break;

	case FN_E_NAME_NOT_FOUND:
		status = FN_SUCCESS;
	}
	delete ref;
	return (status);
}




// narrow:
// Entry consisting of (aname, ref, bind_type) to the bindings table
// Change aname to newname.
// wide:
// Entry consisting of (contextname, aname, ref, bind_type)
// Change aname to newname.
//
// 'flags', if set appropriately, allows the existing entry, if any, to
// be overwritten.

static unsigned
FNSP_rename_binding_entry(const FN_string &tabname,
    const FN_string &aname,
    const FN_string &newname,
    unsigned flags,
    const FN_string *cname = 0)
{
	char sname[NIS_MAXNAMELEN+1];
	nis_object obj;
	entry_col	*cols;
	entry_obj	*eo;
	nis_result *res;
	unsigned status;
	char bt_buf[1];
	nis_name tablename = (nis_name)tabname.str(&status);
	char atomic_tablename[NIS_MAXNAMELEN+1];
	int num_cols;
	int wide = (cname? 1 : 0);

	if (status != FN_SUCCESS)
		return (0);

	if (wide)
		num_cols = FNSP_WIDE_TABLE_WIDTH;
	else
		num_cols = FNSP_NARROW_TABLE_WIDTH;

	cols = (entry_col*) calloc(num_cols, sizeof (entry_col));
	if (cols == 0)
		return (FN_E_INSUFFICIENT_RESOURCES);

	memset((char *)(&obj), 0, sizeof (obj));

	nis_leaf_of_r(tablename, atomic_tablename, NIS_MAXNAMELEN);
	obj.zo_name = atomic_tablename;
	obj.zo_group = nis_local_group();
	obj.zo_access = FNSP_DEFAULT_RIGHTS;
	obj.zo_ttl = FNSP_DEFAULT_TTL;
	obj.zo_data.zo_type = ENTRY_OBJ;
	eo = &(obj.EN_data);
	eo->en_type = (char *) nis_default_table_type; // %%% OK?
	eo->en_cols.en_cols_val = cols;
	eo->en_cols.en_cols_len = num_cols;

	ENTRY_VAL(&obj, FNSP_NAME_COL) = (nis_name) (newname.str(&status));
	ENTRY_LEN(&obj, FNSP_NAME_COL) = newname.bytecount() + 1;
	ENTRY_FLAGS(&obj, FNSP_NAME_COL) = EN_MODIFIED;

	/* clear the rest of the columns */
	ENTRY_VAL(&obj, FNSP_REF_COL) = 0;
	ENTRY_LEN(&obj, FNSP_REF_COL) = 0;
	ENTRY_FLAGS(&obj, FNSP_REF_COL) = EN_BINARY;

	ENTRY_VAL(&obj, FNSP_BIND_COL) = 0;
	ENTRY_LEN(&obj, FNSP_BIND_COL) = 0;
	ENTRY_FLAGS(&obj, FNSP_BIND_COL) = EN_BINARY;

	if (!wide) {
		sprintf(sname, "[%s=\"%s\"],%s", FNSP_name_col_label,
			aname.str(&status), tablename);
	} else {
		/* clear wide columns */
		ENTRY_VAL(&obj, FNSP_CTX_COL) = 0;
		ENTRY_LEN(&obj, FNSP_CTX_COL) = 0;

		/* wide table format has 'context' column */
		sprintf(sname, "[%s=\"%s\",%s=\"%s\"],%s",
			FNSP_name_col_label, aname.str(&status),
			FNSP_ctx_col_label, cname->str(&status),
			tablename);

		ENTRY_VAL(&obj, FNSP_ATTR_COL) = 0;
		ENTRY_LEN(&obj, FNSP_ATTR_COL) = 0;
		ENTRY_FLAGS(&obj, FNSP_ATTR_COL) = EN_BINARY;
	}
	if (sname[strlen(sname)-1] != '.')
		strcat(sname, ".");

	res = nis_modify_entry(sname, &obj,
	    (flags&FN_OP_EXCLUSIVE) ? 0 : ADD_OVERWRITE);
	status = FNSP_map_result(res, "could not modify entry");

	// not found must mean table does not exist.
	if (status == FN_E_NAME_NOT_FOUND)
		status = FN_E_NOT_A_CONTEXT;
	// %%% was CONTEXT_NOT_FOUND;

	free_nis_result(res);
	free(cols);
	return (status);
}

static unsigned
FNSP_rename_binding_aux(const FNSP_printer_Address& parent,
    const FN_string &aname,
    const FN_string &newname,
    unsigned flags)
{
	switch (parent.get_impl_type()) {
	case FNSP_printer_entries_impl:
		return FNSP_rename_binding_entry(parent.get_table_name(),
		    aname, newname, flags, &(parent.get_index_name()));
	default:
#ifdef DEBUG
		fprintf(stderr, "bad printer 3 implemantation type = %d\n",
		    parent.get_impl_type());
#endif /* DEBUG */
		return (FN_E_MALFORMED_REFERENCE);
	}
}

// Rename binding (aname, newname) in bindings table associated with 'parent'.
//
// Policy:  Cannot overwrite if newname is bound to a child reference

unsigned
FNSP_printer_rename_binding(const FNSP_printer_Address& parent,
    const FN_string &aname,
    const FN_string &newname,
    unsigned flags)
{
	unsigned status;

	if (flags&FN_OP_EXCLUSIVE)
		return (FNSP_rename_binding_aux(parent, aname, newname, flags));

	// bind_supercede:  check that we are not overwriting a child reference
	FNSP_binding_type btype;
	unsigned lstatus;
	FN_ref *oref = FNSP_lookup_binding_aux(parent, aname, lstatus, &btype,
	    MASTER_ONLY);

	if (lstatus == FN_E_NAME_NOT_FOUND ||
	    (lstatus == FN_SUCCESS && btype != FNSP_child_context)) {
		// binding does not exist or not a child context
		status = FNSP_rename_binding_aux(parent, aname, newname, flags);
	} else if (lstatus == FN_SUCCESS && btype == FNSP_child_context) {
		// newname is bound to child reference; cannot do that
		    status = FN_E_NAME_IN_USE;
	} else {
		// some other kind of error
		status = lstatus;
	}
	delete oref;
	return (status);
}


// Callback function used for constructing FN_nameset of bindiings
// found in a bindings table.
// Assumes 'udata' points to FN_nameset to add to and updates its contents.
typedef struct {
	FN_nameset *nameset;
	int children_only;
} FNSP_listdata_t;


static int
add_obj_to_nameset(char *, nis_object *ent, void *udata)
{
	FNSP_listdata_t *ld = (FNSP_listdata_t *) udata;
	FN_nameset *ns;

	ns = ld->nameset;

	if (ld->children_only) {
		int blen = ENTRY_LEN(ent, FNSP_BIND_COL);
		char *bstr = ENTRY_VAL(ent, FNSP_BIND_COL);
		unsigned bnum = 3; /* bogus default */

		if (blen == 1)
			bnum = bstr[0];

		switch (bnum) {
		case FNSP_child_context:
			break;

		default: /* otherewise, ignore this entry */
			return (0);
		}
	}

	ns->add((unsigned char *)(ENTRY_VAL(ent, FNSP_NAME_COL)));
	return (0);
}


// Callback function used for constructing FN_bindingset of bindiings
// found in a bindings table.
// Assumes 'udata' points to FN_bindingset to add to and updates its contents.

static int
add_obj_to_bindingset(char *, nis_object* ent, void *udata)
{
	FN_bindingset *bs;
	unsigned status;

	bs = (FN_bindingset*) udata;
	FN_ref *ref =
	    FN_ref_xdr_deserialize(ENTRY_VAL(ent, FNSP_REF_COL),
	    ENTRY_LEN(ent, FNSP_REF_COL),
	    status);
	if (status == FN_SUCCESS && ref)
		bs->add((unsigned char *)(ENTRY_VAL(ent, FNSP_NAME_COL)), *ref,
		    FN_OP_EXCLUSIVE);
	delete ref;
	return (0);
}



// Base function used by FNSP_list_names and FNSP_list_bindings to construct
// FN_nameset and FN_bindingset of bindings associated with 'tabname'.

static unsigned
FNSP_get_binding_entries(const FN_string &tabname,
    unsigned int access_flags,
    int (*add_func)(char *, nis_object*, void *),
    void *add_params,
    const FN_string *cname = 0)
{
	char sname[NIS_MAXNAMELEN+1];
	nis_result *res;
	unsigned status;
	nis_name tablename = (nis_name)tabname.str(&status);

#ifdef CAREFUL_BUT_SLOW
	/* lookup bindings table object */
	res = nis_lookup(tablename, FNSP_nisflags|access_flags);
	status = FNSP_map_result(tres, 0);

	if (status == FN_E_NAME_NOT_FOUND)
		status = FN_E_NOT_A_CONTEXT;
	// %%% was CONTEXT_NOT_FOUND;

	if (status != FN_SUCCESS) {
		free_nis_result(res);
		return (status);
	}

	/* Make sure it is a table object. */
	if (res->objects.objects_val[0].zo_data.zo_type != TABLE_OBJ) {
#ifdef DEBUG
		fprintf(stderr, "%s is not a table!\n", tablename);
#endif /* DEBUG */
		free_nis_result(res);
		return (FN_E_NOT_A_CONTEXT);
	}
	free_nis_result(res);
#endif /* CAREFUL_BUT_SLOW */

	/* Construct query that identifies entries in context */
	if (cname == 0)
		sprintf(sname, tablename);
	else
		sprintf(sname, "[%s=\"%s\"],%s",
			FNSP_ctx_col_label, cname->str(), tablename);

	/* Get table contents using callback function */
	res = __nis_list_localcb(sname, FNSP_nisflags|access_flags,
	    add_func, add_params);

	if (res->status == NIS_RPCERROR) {
		// may have failed because too big
		free_nis_result(res);
		unsigned long new_flags =
		    access_flags|(FNSP_nisflags&(~USE_DGRAM));
		res = nis_list(sname, new_flags, add_func, add_params);
	}

	if ((res->status == NIS_CBRESULTS) || (res->status == NIS_NOTFOUND))
		status = FN_SUCCESS;
	else
		status = FNSP_map_result(res, 0);

	free_nis_result(res);
	return (status);
}


// Return names of bindings associated with 'tabname'.
// Set 'status' appropriately upon return.

FN_nameset *
FNSP_printer_list_names(const FNSP_printer_Address& parent,
    unsigned &status, int children_only)
{
	FN_nameset *ns = new FN_nameset;

	if (ns == 0) {
		status = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}
	FNSP_listdata_t ld;
	ld.nameset = ns;
	ld.children_only = children_only;

	switch (parent.get_impl_type()) {
	case FNSP_printer_entries_impl:
		status = FNSP_get_binding_entries(parent.get_table_name(),
		    parent.get_access_flags(),
		    add_obj_to_nameset, (void *) &ld,
		    &(parent.get_index_name()));
		// get rid of context identifier ('self')
		if (status == FN_SUCCESS)
			ns->remove(FNSP_self_name);
		break;
	default:
#ifdef DEBUG
		fprintf(stderr, "bad printer 4 implemantation type = %d\n",
		    parent.get_impl_type());
#endif /* DEBUG */
		status = FN_E_MALFORMED_REFERENCE;
	}

	if (status != FN_SUCCESS) {
		delete ns;
		ns = 0;
	}
	return (ns);
}


// Return bindings associated with 'parent'.
// Set 'status' appropriately upon return.

FN_bindingset *
FNSP_printer_list_bindings(const FNSP_printer_Address& parent, unsigned &status)
{
	FN_bindingset *bs = new FN_bindingset;

	if (bs == 0) {
		status = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}
	switch (parent.get_impl_type()) {
	case FNSP_printer_entries_impl:
		status = FNSP_get_binding_entries(parent.get_table_name(),
		    parent.get_access_flags(),
		    add_obj_to_bindingset, (void *) bs,
		    &(parent.get_index_name()));
		// get rid of context identifier ('self')
		if (status == FN_SUCCESS)
			bs->remove(FNSP_self_name);
		break;
	default:
#ifdef DEBUG
		fprintf(stderr, "bad printer 5 implemantation type = %d\n",
		    parent.get_impl_type());
#endif
		status = FN_E_MALFORMED_REFERENCE;
	}

	if (status != FN_SUCCESS) {
		delete bs;
		bs = 0;
	}
	return (bs);
}

/* ******************* creating and destroying contexts ***************** */

// Returns whether given name is name of context directory
static inline unsigned
FNSP_context_directory_p(const FN_string &name)
{
	return (name.compare_substring(0, FNSP_cd_size-1,
	    FNSP_context_directory) == 0);
}

static unsigned
FNSP_table_empty_p(nis_name tablename, unsigned &status)
{
	nis_result *res = nis_first_entry(tablename);
	unsigned answer = 0;

	if (res->status == NIS_NOTFOUND || res->status == NIS_NOSUCHTABLE)
		answer = 1;
	else
		status = FNSP_map_result(res, 0);

	free_nis_result(res);
	return (answer);
}

static unsigned
FNSP_remove_context(const FNSP_printer_Address &parent)
{
	unsigned status;
	int done = 0;
	FN_nameset* ns;
	nis_name tablename = (nis_name)parent.get_table_name().str();

	switch (parent.get_impl_type()) {
	case FNSP_printer_entries_impl:
		// check if context is empty
		ns = FNSP_printer_list_names(parent, status, 1);
		/* children_only */
		if (status == FN_E_NAME_NOT_FOUND ||
		    status == FN_E_NOT_A_CONTEXT) {
			// %%% was context_not_found
			status = FN_SUCCESS;
			done = 1;
		} else if (ns) {
			if (ns->count() > 0)
				status = FN_E_CTX_NOT_EMPTY;
			delete ns;
		}
		if (done || status != FN_SUCCESS)
			break;

		/* remove all entries associated with this context */
		status = FNSP_remove_binding_entry(
		    parent.get_table_name(), 0,
		    &(parent.get_index_name()));
		break;
	default:
		status = FN_E_MALFORMED_REFERENCE;
		break;
	}

	return (status);
}


// For contexts that involve directories (only ctx_dir for now)
//  Conditions for removal:
//  1.  Bindings table should be empty
//  2.  Directory should have no subdirectories
//
// For contexts that involve only a bindings table,
// check that it is empty.
//
// If context structure no longer exists, return success.
//
unsigned
FNSP_printer_destroy_context(const FNSP_printer_Address &parent,
    const FN_string *)
{
	unsigned status = FNSP_remove_context(parent);

	if (status == FN_E_NAME_NOT_FOUND ||
	    status == FN_E_NOT_A_CONTEXT)
		// %%% was context_not_found
		status = FN_SUCCESS;

	return (status);
}

// Creates a 'normal' (non-merged) FNSP context and return its reference.
//
// Create a FNSP reference using (tabname, reftype, context_type)
// and construct the structures (bindings table and sometimes directory)
// associated with it.

static FN_ref *
FNSP_create_normal_context(const FNSP_printer_Address& new_addr,
    const FN_identifier &reftype,
    unsigned /* string_case */,
    unsigned &status)
{
	FNSP_printer_implementation_type impl;
	FN_ref *ref = FNSP_reference(
	    FNSP_printer_nisplus_address_type_name(),
	    reftype,
	    new_addr.get_internal_name(),
	    new_addr.get_context_type(),
	    FNSP_normal_repr);

	if (ref == 0) {
		status = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}

	switch (impl = new_addr.get_impl_type()) {
	case FNSP_printer_entries_impl:
		// add entry with atomicname = 'self' to signify new context
		// 'ref' is actually ref of self
		status = FNSP_add_binding_entry(new_addr.get_table_name(),
		    FNSP_self_name, *ref, 0,
		    FNSP_bound_reference,
		    &(new_addr.get_index_name()));
		break;
	default:
		status = FN_E_MALFORMED_REFERENCE;
	}

	if (status != FN_SUCCESS) {
		delete ref;
		return (0);
	}

	return (ref);
}

// Creates a FNSP context and return its reference.

FN_ref *
FNSP_printer_create_context(const FNSP_printer_Address &new_addr,
    unsigned int &status,
    const FN_string * /* dirname */,
    const FN_identifier *reftype)
{
	unsigned context_type = new_addr.get_context_type();

	// Weed out context types that cannot be created

	// Obtain reference type of context to be created if not supplied
	if (reftype == 0) {
		printf("Reference type not provided in fnsp_internal\n");
		status = FN_E_UNSPECIFIED_ERROR;
		// ??? bad_reference?
		return (0);
	}

	unsigned string_case = FNSP_printer_Syntax(
	    context_type)->string_case();

	// Use representation information for creation
	switch (new_addr.get_repr_type()) {
	case FNSP_normal_repr:
		return FNSP_create_normal_context(new_addr, *reftype,
		    string_case, status);
	case FNSP_merged_repr:
		status = FN_E_OPERATION_NOT_SUPPORTED;
		return (0);

	default:
		status = FN_E_UNSPECIFIED_ERROR;
		// ??? bad_reference?
		return (0);
	}
}

// Return legalized value of given name
//

static FN_string
legalize_value(const FN_string &name)
{
	const unsigned char *name_str = name.str();
	int len  = name.bytecount(), i, ri = 0;
	unsigned char *result = 0;

	if (name_str != 0)
		result = (unsigned char *) malloc(len+len+1);
	// double for escapes
	if (result == 0)
		return (FN_string((unsigned char *) ""));

	for (i = 0; i < len; i++) {
		if (fnsp_meta_char(name_str[i])) {
			result[ri++] = FNSP_default_char;
			result[ri++] = name_str[i];
		} else if (nis_bad_value_char(name_str[i]))
			result[ri++] = FNSP_default_char;
		else
			result[ri++] = name_str[i];
	}
	result[ri] = '\0';

	FN_string res(result);
	delete result;
	return (res);
}


// Construct a legal nis+ name.
// Terminals (. [ ] , =) to '_'.
// Leading '@', '+', '-' to '_'.

static FN_string
legalize_tabname(const FN_string &name)
{
	const unsigned char *name_str = name.str();
	int len  = name.bytecount(), i, ri = 0;
	unsigned char *result = 0;

	if (name_str != 0)
		result = (unsigned char *) malloc(len+len+1);
	if (result == 0)
		return (FN_string((unsigned char *) ""));

	if (len >= 0) {
		if (nis_bad_lead_char(name_str[0]) ||
		    nis_terminal_char(name_str[0]))
			result[ri++] = FNSP_default_char;
		else if (fnsp_meta_char(name_str[0])) {
			result[ri++] = FNSP_default_char;
			result[ri++] = name_str[0];
		} else
			result[ri++] = name_str[0];
	}

	for (i = 1; i < len; i++) {
		if (fnsp_meta_char(name_str[i])) {
			result[ri++] = FNSP_default_char;
			result[ri++] = name_str[i];
		} else if (nis_terminal_char(name_str[i]))
			result[ri++] = FNSP_default_char;
		else
			result[ri++] = name_str[i];
	}
	result[ri] = '\0';

	FN_string res(result);
	delete result;
	return (res);
}

static FNSP_printer_Address*
compose_child_addr(const FNSP_printer_Address &parent,
    const FN_string &childname,
    unsigned context_type,
    unsigned repr_type,
    unsigned &status,
    int find_legal_childname)
{
	FN_string *iname = 0;
	FNSP_printer_Address* answer = 0;
	char sname[NIS_MAXNAMELEN+1];

	// ignore parent, these are always single table implementations

	FN_string realchild = (find_legal_childname?
	    legalize_value(childname) : childname);
	switch (parent.get_impl_type()) {
	case FNSP_printer_entries_impl:
		// ignore context type, children can only be
		// FNSP_printer_entries_impl
		// cname = parent''s cname + childname
		// iname = [contextname=cname],tablename
		sprintf(sname, "[%s=\"%s%c%s\"],%s",
			FNSP_ctx_col_label,
			parent.get_index_name().str(),
			FNSP_internal_name_separator,
			realchild.str(),
			parent.get_table_name().str());
		iname = new FN_string((unsigned char *)sname);
		status = FN_SUCCESS;
		break;

	default:
		status = FN_E_OPERATION_NOT_SUPPORTED;
	}

	if (iname) {
		answer = new FNSP_printer_Address(*iname, context_type,
		    repr_type);
		delete iname;
	}

	return (answer);
}


// Create structures for new context and bind it in the given parent context.
// Policies:
//  1.  Fails if flags indicate bind_exclusive and binding already exists.
//  2.  Fails if binding already exists and is that of a child context.
//  3.  Add reference of new context to binding table, indicating it is a child

FN_ref *
FNSP_printer_create_and_bind(const FNSP_printer_Address &parent,
    const FN_string &childname,
    unsigned context_type,
    unsigned repr_type,
    unsigned &status,
    int find_legal_tabname,
    const FN_identifier *ref_type)
{
	FNSP_binding_type btype;
	unsigned lstatus;
	FN_ref *ref = FNSP_lookup_binding_aux(parent, childname,
	    lstatus, &btype,
	    MASTER_ONLY);
	status = FN_SUCCESS;  // initialize

	// check for existing binding
	if (ref) {
		status = FN_E_NAME_IN_USE;  // must destroy explicitly
		delete ref;
		return (0);
	}

	// compose internal name of new context
	FNSP_printer_Address *child_addr = compose_child_addr(parent, childname,
	    context_type, repr_type,
	    status, find_legal_tabname);

	// create context
	if (child_addr) {
		ref = FNSP_printer_create_context(*child_addr,
		    status, 0, ref_type);
	}

	// add binding to parent context
	// at this point, always do an 'add-overwrite' to update or add binding
	if (status == FN_SUCCESS) {
		status = FNSP_add_binding_aux(parent, childname, *ref, 0,
		    FNSP_child_context);
		// try to recover
		if (status != FN_SUCCESS) {
			(void) FNSP_printer_destroy_context(*child_addr);
			delete ref;
			ref = 0;
		}
	}
	delete child_addr;
	return (ref);
}

// Destroy structures associated with context and unbind it from parent.
// Policies:
// 1.  Can delete a context, even one that we have not created.
// 2.  Can only delete empty contexts.
// 3.  If context no longer exists but binding does, remove binding.

unsigned
FNSP_printer_destroy_and_unbind(const FNSP_printer_Address &parent,
    const FN_string &childname)
{
	unsigned status;
	FNSP_binding_type btype;
	const FN_ref *ref = FNSP_lookup_binding_aux(parent, childname,
	    status, &btype,
	    MASTER_ONLY);
	if (status == FN_E_NAME_NOT_FOUND)
		return (FN_SUCCESS);
	else if (status != FN_SUCCESS)
		return (status);

	FNSP_printer_Address child(*ref);
	delete ref;

	if (child.get_context_type() == 0)
		// reference is not one that we can delete
		return (FN_E_OPERATION_NOT_SUPPORTED);
	// ??? appropriate error?

	status = FNSP_printer_destroy_context(child);

	switch (status) {
	case FN_SUCCESS:
		status = FNSP_remove_binding_aux(parent, childname);
		break;
	case FN_E_ILLEGAL_NAME:
		// set to different error to avoid confusion
		status = FN_E_MALFORMED_REFERENCE;  // ??? appropriate error?
		break;
	}
	return (status);
}


// Does (normal) context associated with given internal name exists?
// (i.e. does the given internal name has an associated bindings table?)

unsigned
FNSP_printer_context_exists(const FN_ref &ref)
{
	const FN_ref_addr *addr;
	void *ip;
	unsigned status;

	addr = ref.first(ip);
	while ((addr) && (!FNSP_printer_nisplus_address_p(*addr)))
		addr = ref.next(ip);

	if (!addr)
		return (FN_E_NOT_A_CONTEXT);

	const FNSP_printer_Address *address = new FNSP_printer_Address(*addr);
	status = FNSP_printer_context_exists(*address);
	delete address;
	return (status);
}

unsigned
FNSP_printer_context_exists(const FNSP_printer_Address &ctx)
{
	unsigned status;
	FN_ref *ref;

	switch (ctx.get_impl_type()) {
	case FNSP_printer_entries_impl:
		/* look for 'self' entry that denotes context exists */
		ref = FNSP_lookup_binding_shared(ctx.get_table_name(),
		    ctx.get_index_name(),
		    FNSP_self_name,
		    status, 0, 0);
		delete ref;
		break;
	default:
		status = FN_E_MALFORMED_REFERENCE;
	}

	if (status == FN_E_NAME_NOT_FOUND)
		status = FN_E_NOT_A_CONTEXT;
	// %%% was context_not_found;

	return (status);
}

static const FN_string open_bracket((unsigned char *)"[");
static const FN_string close_bracket((unsigned char *)"]");
static const FN_string equal_sign((unsigned char *)"=");
static const FN_string quote_string((unsigned char *)"\"");
static const FN_string comma_string((unsigned char *)",");

static inline int
FNSP_table_name_p(const FN_string &name)
{
	return (name.next_substring(open_bracket) == 0);
}

// If name is of form "[contextname=<index_part>]<table_part>",
// set tab to table_part and ind to index_part.
// Otherwise, set 'tab' to entire 'name'.
int
FNSP_printer_decompose_index_name(const FN_string &name,
    FN_string &tab, FN_string &ind)
{
	if (FNSP_table_name_p(name)) {
		int pos = name.next_substring(close_bracket);
		if (pos > 0) {
			int tabstart = pos + 1;
			int tabend = name.charcount()-1;

			FN_string wholeindpart(name, 1, pos-1);
			pos = wholeindpart.next_substring(equal_sign);
			if (pos > 0) {
				int indstart = pos + 1;
				int indend = wholeindpart.charcount() - 1;
				// get rid of quotes
				if (wholeindpart.compare_substring(indstart,
				    indstart, quote_string) == 0) {
					++indstart;
					--indend;
				}
				ind = FN_string(wholeindpart, indstart, indend);

				// get rid of comma
				if (name.compare_substring(tabstart, tabstart,
				    comma_string) == 0)
					++tabstart;
				tab = FN_string(name, tabstart, tabend);
				return (1);
			}
		}
	}
	// default
	tab = name;
	return (0);
}


/* functions that deal with changing ownership */


// change ownership of entry named by 'name', with object 'ent'
// to be owned by 'udata' (owner principal string name)
// Because this is used as a callback function for nis_list
// (and __nis_list_localcb):
// Returns 0 (success) : means may continue with next callback if any
// Returns 1 (failure) : means callback will terminate
static int
change_entry_ownership(nis_name name, nis_object *ent, void *udata)
{
	nis_object newobj;
	nis_result *res;
	int status = 0;
	nis_name saveowner;

	newobj = *ent;
	saveowner = newobj.zo_owner; // save
	newobj.zo_owner = (nis_name)udata;
	res = nis_modify_entry(name, &newobj, MOD_SAMEOBJ);
	newobj.zo_owner = saveowner; // restore
	if (res == 0) {
#ifdef DEBUG
		fprintf(stderr,
		    "Lookup of entry '%s' failed: (no status returned)\n",
		    name);
#endif /* DEBUG */
		return (1);
	}

	if (res->status != NIS_SUCCESS) {
#ifdef DEBUG
		fprintf(stderr,
		    "Could not modify entry ownership of '%s': %s\n",
		    name,
		    nis_sperrno(res->status));
#endif /* DEBUG */
		status = 1;
	}
	free_nis_result(res);
	return (status);
}

// Change ownership of binding of 'name' in context of reference to 'owner'
// Returns 1 if successful; 0 if fail
int
FNSP_printer_change_binding_ownership(const FN_ref &parent,
    const FN_string &name,
    const FN_string &owner)
{
	// get parent context
	nis_name tablename;
	unsigned status = 0;
	nis_result *res;
	char sname[NIS_MAXNAMELEN+1];

	FNSP_printer_Address parent_addr(parent);

	if (parent_addr.get_context_type() == 0)
		// could not construct address
		return (0);

	tablename = (nis_name)parent_addr.get_table_name().str();

	switch (parent_addr.get_impl_type()) {
	case FNSP_printer_entries_impl:
		sprintf(sname, "[%s=\"%s\",%s=\"%s\"],%s",
		    FNSP_name_col_label, name.str(),
		    FNSP_ctx_col_label, parent_addr.get_index_name().str(),
		    tablename);
		break;
	default:
		return (0);
	}

	// retrieve entry
	res = nis_list(sname, MASTER_ONLY|FNSP_nisflags, 0, 0);

	// change ownership
	if (res && res->status == NIS_SUCCESS) {
		nis_object *obj = &(res->objects.objects_val[0]);
		status = !(change_entry_ownership(sname, obj,
		    (void *) owner.str()));
	}

	free_nis_result(res);
	return (status);
}

static FN_attrset*
FNSP_extract_attrset_result(nis_result *res, unsigned &status)
{
	FN_attrset *attrset;

	/* extract attribute set */
	attrset = FN_attr_xdr_deserialize(ENTRY_VAL(res->objects.objects_val,
	    FNSP_ATTR_COL), ENTRY_LEN(res->objects.objects_val,
	    FNSP_ATTR_COL), status);
	return (attrset);
}

static FN_attrset *
FNSP_get_attrset_shared(const FN_string &tabname, const FN_string &cname,
    const FN_string &aname,
    unsigned &status, unsigned nisflags)
{
	char sname[NIS_MAXNAMELEN+1];
	FN_attrset *attrset;
	nis_result *res;
	nis_name tablename = (nis_name) tabname.str(&status);

	if (status != FN_SUCCESS)
		return (0);

	sprintf(sname, "[%s=\"%s\",%s=\"%s\"],%s",
		FNSP_name_col_label, aname.str(&status),
		FNSP_ctx_col_label, cname.str(&status), tablename);
	if (status != FN_SUCCESS)
		return (0);

	if (sname[strlen(sname)-1] != '.')
		strcat(sname, ".");

	res = nis_list(sname, FNSP_nisflags|nisflags, NULL, NULL);
	if (res->status == NIS_NOTFOUND)
		status = FN_E_NO_SUCH_ATTRIBUTE;
	else
		status = FNSP_map_result(res, 0);

	if (status == FN_SUCCESS)
		attrset = FNSP_extract_attrset_result(res, status);
	else
		attrset = 0;

	free_nis_result(res);
	return (attrset);
}

static FN_attrset *
FNSP_get_attrset_aux(const FNSP_printer_Address &context,
    const FN_string &atomic_name,
    unsigned &status, unsigned nisflags)
{
	nisflags |= context.get_access_flags();

	switch (context.get_impl_type()) {
	case FNSP_printer_entries_impl:
		return FNSP_get_attrset_shared(context.get_table_name(),
		    context.get_index_name(),
		    atomic_name, status,
		    nisflags);
	case FNSP_printer_null_impl:
		status = FN_E_OPERATION_NOT_SUPPORTED;
		return (0);

	default:
		status = FN_E_UNSPECIFIED_ERROR;
		return (0);
	}
}

FN_attrset *
FNSP_printer_get_attrset(const FNSP_printer_Address &context,
    const FN_string &atomic_name,
    unsigned &status)
{
	return (FNSP_get_attrset_aux(context, atomic_name, status, 0));
}


static int
FNSP_modify_attribute_entry(const FN_string &tabname,
    const FN_string &aname,
    const FN_attribute &attr,
    unsigned flags,
    unsigned int access_flags,
    unsigned &status,
    const FN_string *cname)
{
	char sname[NIS_MAXNAMELEN+1];
	nis_object *obj;
	char *attrbuf = 0, *savebuf;
	nis_result *res;
	nis_result *res1;
	int attrlen = 0, savelen;
	int howmany, i;
	void *ip;

	nis_name tablename = (nis_name) tabname.str(&status);
	if (status != FN_SUCCESS)
		return (0);

	// Get the attribute set associated with this entry
	FN_attrset *aset;

	// Get the nis_object associted with this entry and
	// obtain the attribute set
	sprintf(sname, "[%s=\"%s\",%s=\"%s\"],%s",
	    FNSP_name_col_label, aname.str(&status),
	    FNSP_ctx_col_label, cname->str(&status),
	    tablename);

	res = nis_list(sname, MASTER_ONLY|FNSP_nisflags|access_flags,
	    NULL, NULL);
	status = FNSP_map_result(res, "Unable to perform nis_list");
	if (status != FN_SUCCESS) {
		free_nis_result(res);
		return (status);
	}

	aset = FNSP_extract_attrset_result(res, status);
	// Check the status, in case of an error in XDR decode
	if (status != FN_SUCCESS) {
		free_nis_result(res);
		return (status);
	}
	if (aset == 0) {
		if (flags == FN_ATTR_OP_REMOVE ||
		    flags == FN_ATTR_OP_REMOVE_VALUES) {
			free_nis_result(res);
			return (FN_SUCCESS);
		}
		// otherwise create attribute set to work with
		aset = new FN_attrset;
	}

	// Perform the required operation on aset
	switch (flags) {
	case FN_ATTR_OP_ADD:
		if (!aset->add(attr, FN_OP_SUPERCEDE)) {
			free_nis_result(res);
			delete aset;
			return (FN_E_INSUFFICIENT_RESOURCES);
		}
		break;

	case FN_ATTR_OP_ADD_EXCLUSIVE:
		if (!aset->add(attr, FN_OP_EXCLUSIVE)) {
			free_nis_result(res);
			delete aset;
			// %%% should be FN_E_ATTR_IN_USE
			return (FN_E_UNSPECIFIED_ERROR);
		}
		break;

	case FN_ATTR_OP_ADD_VALUES: {
		const FN_identifier *ident = attr.identifier();
		const FN_attribute *old_attr = aset->get(*ident);

		if (old_attr == NULL) {
			if (!aset->add(attr)) {
				free_nis_result(res);
				delete aset;
				return (FN_E_INSUFFICIENT_RESOURCES);
			}
		} else {
			// merge attr with old_attr
			FN_attribute merged_attr(*old_attr);

			howmany = attr.valuecount();
			const FN_attrvalue *new_attrval;
			new_attrval = attr.first(ip);
			for (i = 0; new_attrval && i < howmany; i++) {
				merged_attr.add(*new_attrval);
				new_attrval = attr.next(ip);
			}
			// overwrite old_attr with merged_attr
			if (!aset->add(merged_attr, FN_OP_SUPERCEDE)) {
				free_nis_result(res);
				delete aset;
				return (FN_E_INSUFFICIENT_RESOURCES);
			}
		}
		break;
	}

	case FN_ATTR_OP_REMOVE:
	case FN_ATTR_OP_REMOVE_VALUES: {
		const FN_identifier *attr_id = attr.identifier();
		const FN_attribute *old_attr = aset->get(*attr_id);

		if (old_attr == NULL) {
			// do not need to update table
			free_nis_result(res);
			delete aset;
			return (FN_SUCCESS);
		}

		if (flags == FN_ATTR_OP_REMOVE)
			aset->remove(*attr_id);
		else {
			// take intersection of attr and old_attr
			FN_attribute inter_attr(*old_attr);

			howmany = attr.valuecount();
			const FN_attrvalue *attr_value = attr.first(ip);
			for (i = 0; attr_value && i < howmany; i++) {
				inter_attr.remove(*attr_value);
				attr_value = attr.next(ip);
			}
			if (inter_attr.valuecount() <= 0)
				aset->remove(*attr_id);
			else if (!aset->add(inter_attr, FN_OP_SUPERCEDE)) {
				// overwrite
				free_nis_result(res);
				delete aset;
				return (FN_E_INSUFFICIENT_RESOURCES);
			}
		}
		break;
	}

	default:
		free_nis_result(res);
		delete aset;
		return (FN_E_OPERATION_NOT_SUPPORTED);
	}

	if (aset->count() > 0) {
		attrbuf = FN_attr_xdr_serialize((*aset), attrlen);
		if (attrbuf == NULL) {
			free_nis_result(res);
			delete aset;
			return (FN_E_INSUFFICIENT_RESOURCES);
		}
	}

	obj = res->objects.objects_val;
	savebuf = ENTRY_VAL(obj, FNSP_ATTR_COL);  // save for cleanup
	savelen = ENTRY_LEN(obj, FNSP_ATTR_COL);
	ENTRY_VAL(obj, FNSP_ATTR_COL) = attrbuf;
	ENTRY_LEN(obj, FNSP_ATTR_COL) = attrlen;
	ENTRY_FLAGS(obj, FNSP_ATTR_COL) = EN_MODIFIED;

	res1 = nis_modify_entry(sname, obj, 0);
	status = FNSP_map_result(res, "Could not modify attributes");
	free(attrbuf);
	free_nis_result(res1);
	ENTRY_VAL(obj, FNSP_ATTR_COL) = savebuf;  // restore
	ENTRY_LEN(obj, FNSP_ATTR_COL) = savelen;
	free_nis_result(res);
	delete aset;
	return (status);
}

static int
FNSP_modify_attribute_aux(const FNSP_printer_Address &context,
    const FN_string &aname,
    const FN_attribute &attr, unsigned flags,
    unsigned &status)
{
	switch (context.get_impl_type()) {
	case FNSP_printer_entries_impl:
		return (FNSP_modify_attribute_entry(context.get_table_name(),
		    aname, attr, flags,
		    context.get_access_flags(), status,
		    &(context.get_index_name())));

	case FNSP_printer_null_impl:
		status = FN_E_OPERATION_NOT_SUPPORTED;
		return (0);

	default:
		status = FN_E_UNSPECIFIED_ERROR;
		return (0);
	}
}

int
FNSP_printer_modify_attribute(const FNSP_printer_Address &context,
    const FN_string &atomic_name,
    const FN_attribute &attr, unsigned flags)
{
	unsigned status = FN_E_UNSPECIFIED_ERROR;

	FNSP_modify_attribute_aux(context, atomic_name, attr, flags, status);

	return (status);
}


static int
FNSP_set_attrset_entry(const FN_string &tabname,
    const FN_string &aname,
    const FN_attrset &attrset,
    unsigned int access_flags,
    unsigned &status, const FN_string *cname)
{
	char sname[NIS_MAXNAMELEN+1];
	nis_object *obj;
	char *asetbuf, *savebuf;
	nis_result *res, *res2;
	int aslen, savelen;

	nis_name tablename = (nis_name) tabname.str(&status);
	if (status != FN_SUCCESS)
		return (0);

	// Get the nis_object associted with this entry and
	// obtain the attribute set
	sprintf(sname, "[%s=\"%s\",%s=\"%s\"],%s",
	    FNSP_name_col_label, aname.str(&status),
	    FNSP_ctx_col_label, cname->str(&status),
	    tablename);

	res = nis_list(sname, MASTER_ONLY|FNSP_nisflags|access_flags,
	    NULL, NULL);
	status = FNSP_map_result(res, "Unable to list in NIS+ table");
	if (status != FN_SUCCESS) {
		free_nis_result(res);
		return (status);
	}

	asetbuf = FN_attr_xdr_serialize(attrset, aslen);
	if (asetbuf == NULL) {
		free_nis_result(res);
		return (FN_E_INSUFFICIENT_RESOURCES);
	}

	obj = res->objects.objects_val;
	savebuf = ENTRY_VAL(obj, FNSP_ATTR_COL);  // save for cleanup
	savelen = ENTRY_LEN(obj, FNSP_ATTR_COL);
	ENTRY_VAL(obj, FNSP_ATTR_COL) = asetbuf;
	ENTRY_LEN(obj, FNSP_ATTR_COL) = aslen;
	ENTRY_FLAGS(obj, FNSP_ATTR_COL) = EN_MODIFIED;

	res2 = nis_modify_entry(sname, obj, 0);
	status = FNSP_map_result(res, "Could not modify attribute set");

	free(asetbuf);
	free_nis_result(res2);
	ENTRY_VAL(obj, FNSP_ATTR_COL) = savebuf;  // restore
	ENTRY_LEN(obj, FNSP_ATTR_COL) = savelen;
	free_nis_result(res);
	return (status);
}

static int
FNSP_set_attrset_aux(const FNSP_printer_Address &context,
    const FN_string &aname,
    const FN_attrset &attrset, unsigned &status)
{
	switch (context.get_impl_type()) {
	case FNSP_printer_entries_impl:
		return (FNSP_set_attrset_entry(context.get_table_name(),
		    aname, attrset,
		    context.get_access_flags(),
		    status, &(context.get_index_name())));

	case FNSP_printer_null_impl:
		status = FN_E_OPERATION_NOT_SUPPORTED;
		return (0);

	default:
		status = FN_E_UNSPECIFIED_ERROR;
		return (0);
	}
}

int
FNSP_printer_set_attrset(const FNSP_printer_Address &context,
    const FN_string &atomic_name,
    FN_attrset &attrset)
{
	unsigned status = FN_E_UNSPECIFIED_ERROR;

	FNSP_set_attrset_aux(context, atomic_name, attrset, status);

	return (status);
}
