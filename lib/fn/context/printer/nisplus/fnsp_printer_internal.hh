/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _FNSP_PRINTER_INTERNAL_HH
#define	_FNSP_PRINTER_INTERNAL_HH

#pragma ident	"@(#)fnsp_printer_internal.hh	1.4	94/11/29 SMI"

#include "../FNSP_printer_Address.hh"
#include <xfn/FN_nameset.hh>
#include <xfn/FN_bindingset.hh>

// Interfaces to address routines
extern const FN_identifier &
FNSP_printer_nisplus_address_type_name();

extern int
FNSP_printer_nisplus_address_p(const FN_ref_addr &);

extern const FN_identifier *
FNSP_printer_reftype_from_ctxtype(unsigned context_type);

extern const FN_identifier &
FNSP_printername_reftype_name();

extern const FN_identifier &
FNSP_printer_reftype_name();

// interface to NIS+ routines

extern unsigned
FNSP_printer_add_binding(const FNSP_printer_Address& parent,
    const FN_string &atomic_name,
    const FN_ref &ref,
    unsigned flags);

extern unsigned
FNSP_printer_remove_binding(const FNSP_printer_Address& parent,
    const FN_string &atomic_name);

extern unsigned
FNSP_printer_rename_binding(const FNSP_printer_Address& parent,
    const FN_string &atomic_name,
    const FN_string &new_name,
    unsigned flags);

extern FN_ref *
FNSP_printer_lookup_binding(const FNSP_printer_Address& parent,
    const FN_string &atomic_name,
    unsigned &status);

extern unsigned
FNSP_printer_context_exists(const FN_ref &);

extern unsigned
FNSP_printer_context_exists(const FNSP_printer_Address&);

extern FN_ref *
FNSP_printer_create_context(const FNSP_printer_Address& parent,
    unsigned &status,
    const FN_string *dirname = 0,
    const FN_identifier *reftype = 0);

extern unsigned
FNSP_printer_destroy_context(const FNSP_printer_Address& parent,
    const FN_string *dirname = 0);

extern FN_ref *
FNSP_printer_create_and_bind(const FNSP_printer_Address& parent,
    const FN_string &child_name,
    unsigned context_type,
    unsigned repr_type,
    unsigned &status,
    int find_legal_name = 0,
    const FN_identifier *ref_type = 0);

extern unsigned
FNSP_printer_destroy_and_unbind(const FNSP_printer_Address& parent,
    const FN_string &child_name);

extern FN_nameset*
FNSP_printer_list_names(const FNSP_printer_Address& parent,
    unsigned & status, int children_only = 0);

extern FN_bindingset*
FNSP_printer_list_bindings(const FNSP_printer_Address& parent,
    unsigned &status);

// functions that deal with internal names
extern int
FNSP_printer_decompose_index_name(const FN_string &src,
    FN_string &tabname, FN_string &indexname);

// change ownership associated with binding associated with given name
extern int
FNSP_printer_change_binding_ownership(const FN_ref &ref,
    const FN_string &atomic_name,
    const FN_string &owner);

// funtions to support attribute opertations
// To obtains the attribute set
extern FN_attrset *
FNSP_printer_get_attrset(const FNSP_printer_Address &context,
    const FN_string &atomic_name,
    unsigned &status);

// To modify the attribute set
extern int
FNSP_printer_modify_attribute(const FNSP_printer_Address &context,
    const FN_string &stomic_name,
    const FN_attribute &attr, unsigned flags);

extern int
FNSP_printer_set_attrset(const FNSP_printer_Address &context,
    const FN_string &stomic_name,
    FN_attrset &attrset);

#endif /* _FNSP_PRINTER_INTERNAL_HH */
