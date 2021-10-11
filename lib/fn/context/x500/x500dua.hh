/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_X500DUA_HH
#define	_X500DUA_HH

#pragma ident	"@(#)x500dua.hh	1.1	94/12/05 SMI"


#include <xfn/xfn.h>
#include <xfn/fn_spi.hh>
#include "x500utils.hh"

extern "C" {

#include "xom.h"
#include "xds.h"
#include "xdsbdcp.h"
#include "xdsxfnp.h"

}


/*
 * X500 Directory User Agent over the XDS API
 */

class CX500DUA
{
	void			fill_om_desc(OM_descriptor &desc) const;

	void			fill_om_desc(OM_descriptor &desc,
					const OM_type type,
					OM_descriptor *object) const;

	void			fill_om_desc(OM_descriptor &desc,
					const OM_type type,
					const OM_string	oid) const;

	void			fill_om_desc(OM_descriptor &desc,
					const OM_type type,
					const OM_syntax	syntax, void *string,
					const OM_string_length length
					= OM_LENGTH_UNSPECIFIED) const;

	void			fill_om_desc(OM_descriptor &desc,
					const OM_type type,
					const OM_sint32 number,
					const OM_syntax	syntax
					= OM_S_ENUMERATION) const;

	void			fill_om_desc(OM_descriptor &desc,
					const OM_type type,
					const OM_boolean boolean) const;

	int			compare_om_oids(OM_object_identifier &oid1,
					OM_object_identifier &oid2) const;

	unsigned char		*om_oid_to_string_oid(OM_object_identifier *oid)
					const;

	OM_string		*string_oid_to_om_oid(const char *oid) const;

	OM_object_identifier	*abbrev_to_om_oid(const char *oid,
					OM_syntax &syntax) const;

	FN_ref			*xds_attr_value_to_ref(OM_object attr_value)
					const;

	int			ref_to_xds_attr_value(const FN_ref *ref,
					OM_object attr_value) const;

	unsigned char		*id_format_to_string(unsigned int format,
					unsigned char *cp) const;

	unsigned char		*string_to_id_format(unsigned char *cp,
					unsigned int &format) const;

	int			id_to_om_oid(const FN_identifier &id,
					OM_object_identifier &oid,
					OM_syntax *syntax,
					OM_object_identifier **class_oid) const;

	OM_private_object	id_to_xds_selection(const FN_identifier	&id)
					const;

	OM_private_object	ids_to_xds_selection(const FN_attrset *ids)
					const;

	int			om_oid_to_syntax(OM_object_identifier *oid,
					OM_syntax *syntax,
					OM_object_identifier **class_oid) const;

	unsigned char		*om_oid_to_string(OM_object_identifier *oid)
					const;

	OM_object_identifier	*string_to_om_oid(unsigned char *oid)
					const;

	int			string_to_xds_attr_value(FN_string *val_str,
					const OM_syntax syntax,
					OM_object_identifier *class_oid,
					OM_descriptor *attr_value) const;

	void			delete_xds_attr_value(OM_descriptor *attr_value)
					const;

	unsigned char		*xds_attr_value_to_string(
					OM_descriptor *attr_value,
					unsigned int &format,
					unsigned int &length) const;

	OM_private_object	string_to_xds_dn(const FN_string &dn) const;

	int			xds_error_to_int(OM_private_object &status)
					const;

	int			xds_error_to_xfn(OM_private_object &status)
					const;

	unsigned char		*om_oid_to_abbrev(OM_object_identifier &om_oid,
					unsigned char *oid) const;

	unsigned char		*xds_dn_to_string(OM_private_object dn,
					int is_dn = 0) const;

	OM_public_object	string_to_xds_paddr(const FN_string &paddr)
					const;

	unsigned char		*string_to_xds_paddr_selector(
					unsigned char *string,
					OM_string *selector) const;

	unsigned char 		*xds_paddr_to_string(OM_private_object paddr,
					int &len) const;

	unsigned char 		*xds_paddr_selector_to_string(
					OM_string *selector,
					unsigned char *string) const;

	OM_public_object	string_to_xds_post_addr(const FN_string &post)
					const;

	unsigned char 		*xds_post_addr_to_string(
					OM_private_object addr,
					int &len) const;

	OM_return_code		deep_om_get(OM_type_list route,
					const OM_private_object original,
					const OM_exclusions exclusions,
					const OM_type_list included_types,
					const OM_boolean local_strings,
					const OM_value_position initial_value,
					const OM_value_position limiting_value,
					OM_public_object *copy,
					OM_value_position *total_number);

public:
	FN_ref			*lookup(const FN_string &name, int &err);

	FN_ref			*lookup_next(const FN_string &name, int &err);

	FN_namelist		*list_names(const FN_string &name, int &err);

	FN_bindinglist		*list_bindings(const FN_string &name, int &err);

	int			bind_next(const FN_string &name,
					const FN_ref &ref,
					unsigned int exclusive);

	int			unbind(const FN_string &name);

	int			unbind_next(const FN_string &name);

	int			rename(const FN_string &name,
					const FN_string *newname,
					unsigned int exclusive);

	FN_attribute		*get_attr(const FN_string &name,
					const FN_identifier &id, int &err);

	FN_attrset		*get_attr_ids(const FN_string &name, int &err);

	FN_multigetlist		*get_attrs(const FN_string &name,
					const FN_attrset *ids, int &err);

	int			modify_attr(const FN_string &name,
					unsigned int mod_op,
					const FN_attribute &attr);

	CX500DUA(int &err);
	~CX500DUA();
};


#endif	/* _X500DUA_HH */
