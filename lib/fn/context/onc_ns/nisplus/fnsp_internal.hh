/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _FNSP_INTERNAL_HH
#define	_FNSP_INTERNAL_HH

#pragma ident	"@(#)fnsp_internal.hh	1.10	95/01/29 SMI"

#include <netdb.h>
// for hostent definition
#include <rpc/rpc.h>
// for netobj

#include "../FNSP_Address.hh"
#include <xfn/FN_nameset.hh>
#include <xfn/FN_bindingset.hh>

extern const FN_string FNSP_nns_name;

// interface to NIS+ routines
extern int
FNSP_nisplus_address_p(const FN_ref_addr &addr);

extern const FN_identifier &
FNSP_nisplus_address_type_name(void);

// interface to directory routines
unsigned FNSP_create_directory(const FN_string &, unsigned int access_flags);

extern unsigned
FNSP_add_binding(const FNSP_Address& parent,
    const FN_string &atomic_name,
    const FN_ref &ref,
    unsigned flags);

extern unsigned
FNSP_remove_binding(const FNSP_Address& parent,
    const FN_string &atomic_name);

extern unsigned
FNSP_rename_binding(const FNSP_Address& parent,
    const FN_string &atomic_name,
    const FN_string &new_name,
    unsigned flags);

extern FN_ref *
FNSP_lookup_binding(const FNSP_Address& parent,
    const FN_string &atomic_name,
    unsigned &status);

extern unsigned
FNSP_context_exists(const FNSP_Address&);

extern FN_ref *
FNSP_create_context(const FNSP_Address& parent,
    unsigned &status,
    const FN_string *dirname = 0,
    const FN_identifier *reftype = 0);

extern unsigned
FNSP_destroy_context(const FNSP_Address& parent,
    const FN_string *dirname = 0);

extern FN_ref *
FNSP_create_and_bind(const FNSP_Address& parent,
    const FN_string &child_name,
    unsigned context_type,
    unsigned repr_type,
    unsigned &status,
    int find_legal_name = 0,
    const FN_identifier *ref_type = 0);

extern unsigned
FNSP_destroy_and_unbind(const FNSP_Address& parent,
    const FN_string &child_name);

extern FN_nameset*
FNSP_list_names(const FNSP_Address& parent,
    unsigned & status, int children_only = 0);

extern FN_bindingset*
FNSP_list_bindings(const FNSP_Address& parent,
    unsigned &status);

extern FN_ref *
FNSP_lookup_org(const FN_string &org_name, unsigned int access_flags,
		unsigned &status);

extern FN_nameset *
FNSP_list_orgnames(const FN_string &org_name, unsigned int access_flags,
		    unsigned &status);

extern FN_bindingset *
FNSP_list_orgbindings(const FN_string &org_name, unsigned int access_flags,
		    unsigned &status);

extern FN_ref *
FNSP_resolve_orgname(const FN_string &directory,
    const FN_string &target,
    unsigned int access_flags,
    unsigned &status,
    FN_status &stat,
    int &stat_set);

// user and host name routines
extern FN_string *
FNSP_orgname_of(const FN_string &internal_name, unsigned &status,
    int org = 0);

extern FN_string *
FNSP_find_host_entry(const FN_string &directory,
    const FN_string &hostname,
    unsigned int access_flags,
    unsigned &status,
    struct hostent **he = 0);

extern void
free_hostent(struct hostent *);

extern FN_string *
FNSP_find_user_entry(const FN_string &directory,
    const FN_string &username,
    unsigned int access_flags,
    unsigned &status);

// functions that deal with internal names
extern int
FNSP_decompose_index_name(const FN_string &src,
    FN_string &tabname, FN_string &indexname);

extern FN_string *
FNSP_compose_ctx_tablename(const FN_string &short_tabname,
    const FN_string &domain_name);

// change ownership of context (and all inclusive bindings)
// If 'ref' is 'host' or 'user' nns type, change all subcontexts too
extern int
FNSP_change_context_ownership(const FN_ref &ref,
    const FN_string &owner);

// change ownership associated with binding associated with given name
extern int
FNSP_change_binding_ownership(const FN_ref &ref,
    const FN_string &atomic_name,
    const FN_string &owner);

// funtions to support attribute opertations
// To obtains the attribute set
extern FN_attrset *
FNSP_get_attrset(const FNSP_Address &context, const FN_string &atomic_name,
    unsigned &status);

// To modify the attribute set
extern int
FNSP_modify_attribute(const FNSP_Address &context,
    const FN_string &stomic_name,
    const FN_attribute &attr, unsigned flags);

extern int
FNSP_set_attrset(const FNSP_Address &context, const FN_string &stomic_name,
    FN_attrset &attrset);

// Attribute support for single table implementation
// That is for username and hostname implementation
extern FN_attribute*
FNSP_get_attribute(const FNSP_Address &context, const FN_string &aname,
    const FN_identifier &id, unsigned &status);

extern int
FNSP_set_attribute(const FNSP_Address &context, const FN_string &aname,
    const FN_attribute &attr);

extern int
FNSP_remove_attribute(const FNSP_Address &context, const FN_string &aname,
    const FN_attribute *attribute = 0);

extern int
FNSP_remove_attribute_values(const FNSP_Address &context,
    const FN_string &aname,
    const FN_attribute &attribute);

extern char *
FNSP_read_first(const char *tab_name, netobj &iter_pos,
    unsigned int &status, FN_ref **ref = 0);

extern char *
FNSP_read_next(const char *tab_name, netobj &iter_pos,
    unsigned int &status, FN_ref **ref = 0);

#endif	/* _FNSP_INTERNAL_HH */
