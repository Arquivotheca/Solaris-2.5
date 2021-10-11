/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)x500dua.cc	1.3	95/03/12 SMI"


#include <string.h>
#include "x500dua.hh"

/*
 * definitions of useful OM objects (read-only)
 */

static DS_feature	packages[] = {
	{OM_STRING(OMP_O_DS_BASIC_DIR_CONTENTS_PKG), OM_TRUE},
	{OM_STRING(OMP_O_DS_XFN_PKG), OM_TRUE},
	{{0, 0}, OM_FALSE}
};

static OM_descriptor	select_all_types[] = {
	OM_OID_DESC(OM_CLASS, DS_C_ENTRY_INFO_SELECTION),
	{DS_ALL_ATTRIBUTES, OM_S_BOOLEAN, {{OM_TRUE, NULL}}},
	{DS_INFO_TYPE, OM_S_ENUMERATION, {{DS_TYPES_ONLY, NULL}}},
	OM_NULL_DESCRIPTOR
};

static OM_descriptor	select_ref_attrs[] = {	// TBD: others
	OM_OID_DESC(OM_CLASS, DS_C_ENTRY_INFO_SELECTION),
	{DS_ALL_ATTRIBUTES, OM_S_BOOLEAN, {{OM_FALSE, NULL}}},
	OM_OID_DESC(DS_ATTRIBUTES_SELECTED, DS_A_PRESENTATION_ADDRESS),
	{DS_INFO_TYPE, OM_S_ENUMERATION, {{DS_TYPES_AND_VALUES, NULL}}},
	OM_NULL_DESCRIPTOR
};

static OM_descriptor	select_ref_next[] = {
	OM_OID_DESC(OM_CLASS, DS_C_ENTRY_INFO_SELECTION),
	{DS_ALL_ATTRIBUTES, OM_S_BOOLEAN, {{OM_FALSE, NULL}}},
	OM_OID_DESC(DS_ATTRIBUTES_SELECTED, DS_A_NNS_REF_STRING),
	{DS_INFO_TYPE, OM_S_ENUMERATION, {{DS_TYPES_AND_VALUES, NULL}}},
	OM_NULL_DESCRIPTOR
};

static OM_descriptor	remove_ref[] = {
	OM_OID_DESC(OM_CLASS, DS_C_ENTRY_MOD),
	{DS_MOD_TYPE, OM_S_ENUMERATION, {{DS_REMOVE_ATTRIBUTE, NULL}}},
	OM_OID_DESC(DS_ATTRIBUTE_TYPE, DS_A_NNS_REF_STRING),
	OM_NULL_DESCRIPTOR
};

static OM_descriptor	mod_list_remove[] = {
	OM_OID_DESC(OM_CLASS, DS_C_ENTRY_MOD_LIST),
	{DS_CHANGES, OM_S_OBJECT, {{0, remove_ref}}},
	OM_NULL_DESCRIPTOR
};

static FN_identifier	x500((const unsigned char *)"x500");
static FN_identifier	osi_paddr((const unsigned char *)"osi_paddr");
static FN_identifier	ascii((const unsigned char *)"fn_attr_syntax_ascii");
static FN_identifier	octet((const unsigned char *)"fn_attr_syntax_octet");


CX500DUA::CX500DUA(
	int	&err
)
{
	DS_status	status = DS_SUCCESS;

	mutex_lock(&x500_dua_mutex);

	if (! initialized) {
		if ((workspace = ds_initialize()) != NULL) {

			if ((status = ds_version(packages, workspace)) ==
			    DS_SUCCESS) {

				if (packages[1].activated == OM_TRUE)
					xfn_pkg = OM_TRUE;
				else
					xfn_pkg = OM_FALSE;

				status = ds_bind(DS_DEFAULT_SESSION, workspace,
				    &session);
			}

		}
		if (workspace && (status == DS_SUCCESS)) {
			initialized++;
			err = 0;

			x500_debug("[CX500DUA] XDS/XOM initialized ok\n");

		} else {

			x500_debug("[CX500DUA] XDS/XOM initialization error\n");

			err = 1;
			if (status)
				om_delete(status);
		}
	} else {
		err = 0;

		x500_debug("[CX500DUA]\n");
	}

	mutex_unlock(&x500_dua_mutex);
}


CX500DUA::~CX500DUA(
)
{
	DS_status	status = DS_SUCCESS;
	DS_status	status2 = DS_SUCCESS;

	mutex_lock(&x500_dua_mutex);

	if (initialized == 1) {

		if (session) {
			if ((status = ds_unbind(session)) == DS_SUCCESS)
				session = 0;
		}
		if (workspace) {
			if ((status2 = ds_shutdown(workspace)) == DS_SUCCESS)
				workspace = 0;
		}
		if ((status != DS_SUCCESS) || (status2 != DS_SUCCESS)) {

			x500_debug("[~CX500DUA] XDS/XOM shutdown error\n");

			if (status)
				om_delete(status);
			if (status2)
				om_delete(status2);
		} else

			x500_debug("[~CX500DUA] XDS/XOM shutdown ok\n");

	} else {

		x500_debug("[~CX500DUA]\n");
	}
	initialized--;

	mutex_unlock(&x500_dua_mutex);
}


/*
 * If the specified entry exists then return a reference to it.
 */
FN_ref *
CX500DUA::lookup(
	const FN_string	&name,
	int		&err
)
{
	DS_status		status;
	OM_private_object	dn;
	OM_private_object	result;
	OM_sint			invoke_id;

	x500_debug("CX500DUA::lookup(\"%s\")\n", name.str());

	// convert string name to XDS distinguished name format
	dn = string_to_xds_dn(name);

	x500_debug("CX500DUA::lookup: ds_search dn: ...\n"),
	x500_dump(dn);

	// test for reference attributes
	if ((status = ds_search(session, DS_DEFAULT_CONTEXT, dn, DS_BASE_OBJECT,
	    DS_NO_FILTER, OM_TRUE, select_ref_attrs, &result, &invoke_id)) !=
	    DS_SUCCESS) {

		x500_debug("CX500DUA::lookup: ds_search status: ...\n"),
		x500_dump(status);

		err = xds_error_to_xfn(status);
		om_delete(dn);
		om_delete(status);
		return (0);
	}
	x500_debug("CX500DUA::lookup: ds_search result: ...\n"),
	x500_dump(result);

	om_delete(dn);

	// extract reference attributes
	OM_type			route[] = {DS_SEARCH_INFO, DS_ENTRIES, 0};
	OM_type			type[] = {DS_ATTRIBUTES, 0};
	OM_public_object	pub_result;
	OM_value_position	total;

	if (deep_om_get(route, result, OM_EXCLUDE_ALL_BUT_THESE_TYPES, type,
	    OM_FALSE, 0, 0, &pub_result, &total) != OM_SUCCESS) {

		om_delete(result);
		err = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}
	om_delete(result);

	// build reference
	FN_ref		*ref = new FN_ref(x500);
	FN_ref_addr	x500_ref_addr(x500, name.bytecount(), name.str());

	x500_debug("CX500DUA::lookup: created X.500 reference to %s\n",
	    name.str());

	if (ref)
		ref->append_addr(x500_ref_addr);
	else {
		om_delete(pub_result);
		err = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}

	if (total == 0) {
		err = FN_SUCCESS;
		return (ref);
	}

	OM_public_object	attr = pub_result->value.object.object;
	OM_public_object	attr2 = ++attr;	// skip OM_CLASS

	while (attr->type != DS_ATTRIBUTE_TYPE &&
	    attr->type != OM_NO_MORE_TYPES)
		attr++;

	// expects a single attribute
	if (attr->type == DS_ATTRIBUTE_TYPE) {

		if (compare_om_oids(DS_A_PRESENTATION_ADDRESS,
		    attr->value.string)) {

			x500_debug("CX500DUA::lookup: %s\n",
			    "presentation-address attribute encountered");

			while (attr2->type != DS_ATTRIBUTE_VALUES &&
			    attr2->type != OM_NO_MORE_TYPES)
				attr2++;

			unsigned char	*paddr;
			int		len;

			if ((attr2->type != DS_ATTRIBUTE_VALUES) ||
			    (! (paddr = xds_paddr_to_string(
			    attr2->value.object.object, len)))) {

				om_delete(pub_result);
				err = FN_E_INSUFFICIENT_RESOURCES;
				return (0);
			}

			FN_ref_addr	ref_addr(osi_paddr, len, paddr);

			delete [] paddr;

			if (ref)
				ref->append_addr(ref_addr);
			else {
				om_delete(pub_result);
				err = FN_E_INSUFFICIENT_RESOURCES;
				return (0);
			}
		}
	}
	om_delete(pub_result);

	err = FN_SUCCESS;
	return (ref);
}


/*
 * If the specified entry holds a reference then return that reference.
 */
FN_ref *
CX500DUA::lookup_next(
	const FN_string	&name,
	int		&err
)
{
	DS_status		status;
	OM_private_object	dn;
	OM_private_object	result;
	OM_sint			invoke_id;

	x500_debug("CX500DUA::lookup_next(\"%s\")\n", name.str());

	// convert string name to XDS distinguished name format
	dn = string_to_xds_dn(name);

	x500_debug("CX500DUA::lookup_next: ds_search dn: ...\n"),
	x500_dump(dn);

	// test for external reference attribute
	if ((status = ds_search(session, DS_DEFAULT_CONTEXT, dn, DS_BASE_OBJECT,
	    DS_NO_FILTER, OM_TRUE, select_ref_next, &result, &invoke_id)) !=
	    DS_SUCCESS) {

		x500_debug("CX500DUA::lookup_next: ds_search status: ...\n"),
		x500_dump(status);

		err = xds_error_to_xfn(status);
		om_delete(dn);
		om_delete(status);
		return (0);
	}
	x500_debug("CX500DUA::lookup_next: ds_search result: ...\n"),
	x500_dump(result);

	om_delete(dn);

	// extract reference attributes
	OM_type			route[] = {DS_SEARCH_INFO, DS_ENTRIES, 0};
	OM_type			type[] = {DS_ATTRIBUTES, 0};
	OM_public_object	pub_result;
	OM_value_position	total;

	if (deep_om_get(route, result, OM_EXCLUDE_ALL_BUT_THESE_TYPES, type,
	    OM_FALSE, 0, 0, &pub_result, &total) != OM_SUCCESS) {

		om_delete(result);
		err = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}
	om_delete(result);

	if (total == 0) {
		err = FN_E_NAME_NOT_FOUND;
		return (0);
	}

	// build reference
	FN_ref			*ref = 0;
	OM_public_object	attr = pub_result->value.object.object;
	OM_public_object	attr2 = ++attr;	// skip OM_CLASS

	while (attr->type != DS_ATTRIBUTE_TYPE &&
	    attr->type != OM_NO_MORE_TYPES)
		attr++;

	// expects a single attribute
	if (attr->type == DS_ATTRIBUTE_TYPE) {

		if (compare_om_oids(DS_A_NNS_REF_STRING,
		    attr->value.string)) {

			x500_debug("CX500DUA::lookup_next: %s\n",
			    "string-reference attribute encountered");

			while (attr2->type != DS_ATTRIBUTE_VALUES &&
			    attr2->type != OM_NO_MORE_TYPES)
				attr2++;

			if ((attr2->type != DS_ATTRIBUTE_VALUES) ||
			    (! (ref = xds_attr_value_to_ref(attr2)))) {

				om_delete(pub_result);
				err = FN_E_INSUFFICIENT_RESOURCES;
				return (0);
			}
		} else {

			x500_debug("CX500DUA::lookup_next: %s\n",
			    "unexpected attribute encountered");

			om_delete(pub_result);
			err = FN_E_INSUFFICIENT_RESOURCES;
			return (0);
		}
	}
	om_delete(pub_result);

	err = FN_SUCCESS;
	return (ref);
}

/*
 * Query the specified entry for its subordinates using ds_list()
 */
FN_namelist *
CX500DUA::list_names(
	const FN_string	&name,
	int		&err
)
{
	DS_status		status;
	OM_private_object	dn;
	OM_private_object	result;
	OM_sint			invoke_id;

	x500_debug("CX500DUA::list_names(\"%s\")\n", name.str());

	// convert name to XDS distinguished name format
	dn = string_to_xds_dn(name);

	x500_debug("CX500DUA::list_names: ds_list dn: ...\n"),
	x500_dump(dn);

	// locate the children of the specified entry
	if ((status = ds_list(session, DS_DEFAULT_CONTEXT, dn, &result,
	    &invoke_id)) != DS_SUCCESS) {

		x500_debug("CX500DUA::list_names: ds_list status: ...\n"),
		x500_dump(status);

		err = xds_error_to_xfn(status);
		om_delete(dn);
		om_delete(status);
		return (0);
	}

	x500_debug("CX500DUA::list_names: ds_list result: ...\n"),
	x500_dump(result);

	om_delete(dn);

	// extract subordinates
	OM_type			route[] = {DS_LIST_INFO, 0};
	OM_type			type[] = {DS_SUBORDINATES, 0};
	OM_public_object	pub_result = 0;
	OM_value_position	total;
	OM_public_object	rdn;

	if (deep_om_get(route, result, OM_EXCLUDE_ALL_BUT_THESE_TYPES, type,
	    OM_FALSE, 0, 0, &pub_result, &total) != OM_SUCCESS) {

		om_delete(result);
		err = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}
	om_delete(result);

	// build set of names
	FN_nameset	*name_set;

	x500_debug("CX500DUA::list_names: found %d entr%s\n", total,
	    (total == 1) ? "y" : "ies");

	if ((name_set = new FN_nameset) == 0) {
		om_delete(pub_result);
		err = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}

	if (total == 0) {
		err = FN_SUCCESS;
		return (new FN_namelist_svc(name_set));
	}

	while (total--) {
		unsigned char	*cp;

		rdn = pub_result[total].value.object.object;
		while (rdn->type != DS_RDN)
			rdn++;

		if (cp = xds_dn_to_string(rdn->value.object.object)) {
			name_set->add(cp);
			delete [] cp;
		} else {
			err = FN_E_INSUFFICIENT_RESOURCES;
			om_delete(pub_result);
			return (0);
		}

	}

	om_delete(pub_result);
	err = FN_SUCCESS;
	return (new FN_namelist_svc(name_set));
}


/*
 * Query the specified entry for its subordinates (and their reference
 * attributes) using ds_search()
 */
FN_bindinglist *
CX500DUA::list_bindings(
	const FN_string	&name,
	int		&err
)
{
	DS_status		status;
	OM_private_object	dn;
	OM_private_object	result;
	OM_sint			invoke_id;

	x500_debug("CX500DUA::list_bindings(\"%s\")\n", name.str());

	// convert name to XDS distinguished name format
	dn = string_to_xds_dn(name);

	x500_debug("CX500DUA::list_bindings: ds_search dn: ...\n"),
	x500_dump(dn);

	// locate the childern (and their attributes) of the specified entry
	if ((status = ds_search(session, DS_DEFAULT_CONTEXT, dn, DS_ONE_LEVEL,
	    DS_NO_FILTER, OM_TRUE, select_ref_attrs, &result, &invoke_id)) !=
	    DS_SUCCESS) {

		x500_debug("CX500DUA::list_bindings: ds_search status: ...\n"),
		x500_dump(status);

		err = xds_error_to_xfn(status);
		om_delete(dn);
		om_delete(status);
		return (0);
	}
	om_delete(dn);

	x500_debug("CX500DUA::list_bindings: ds_search result: ...\n"),
	x500_dump(result);

	// extract reference attributes
	OM_type			route[] = {DS_SEARCH_INFO, 0};
	OM_type			type[] = {DS_ENTRIES, 0};
	OM_public_object	pub_result;
	OM_value_position	total;

	if (deep_om_get(route, result, OM_EXCLUDE_ALL_BUT_THESE_TYPES, type,
	    OM_FALSE, 0, 0, &pub_result, &total) != OM_SUCCESS) {

		om_delete(dn);
		om_delete(result);
		err = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}
	om_delete(result);

	// build set of bindings
	FN_bindingset	*binding_set;

	x500_debug("CX500DUA::list_bindings: found %d entr%s\n", total,
	    (total == 1) ? "y" : "ies");

	if ((binding_set = new FN_bindingset) == 0) {
		err = FN_E_INSUFFICIENT_RESOURCES;
		om_delete(pub_result);
		return (0);
	}

	if (total == 0) {
		err = FN_SUCCESS;
		return (new FN_bindinglist_svc(binding_set));
	}

	OM_public_object	entry;
	OM_public_object	entry2;
	OM_public_object	attr;
	OM_public_object	attr2;
	OM_public_object	rdn;
	FN_string		*rdn_string;
	FN_string		slash((unsigned char *)"/");

	while (total--) {
		FN_ref	ref(x500);

		entry2 = entry = pub_result[total].value.object.object;

		while (entry->type != DS_OBJECT_NAME)
			entry++;

		// locate and store the final RDN
		rdn = entry->value.object.object;
		rdn++;
		// skip to the final RDN
		while (rdn->type == DS_RDNS)
			rdn++;
		rdn = (--rdn)->value.object.object;

		unsigned char	*cp;

		if (! (cp = xds_dn_to_string(rdn))) {
			err = FN_E_INSUFFICIENT_RESOURCES;
			om_delete(pub_result);
			return (0);
		}
		if (! (rdn_string = new FN_string(cp))) {
			err = FN_E_INSUFFICIENT_RESOURCES;
			om_delete(pub_result);
			delete [] cp;
			return (0);
		}
		delete [] cp;

		// create X.500 reference
		unsigned int	status;
		FN_string	subordinate(&status, &name, &slash, rdn_string,
				    0);
		FN_ref_addr	ref_addr(x500, subordinate.bytecount(),
				    subordinate.str());

		ref.append_addr(ref_addr);

		x500_debug("CX500DUA::list_bindings: %s to %s\n",
		    "created X.500 reference", subordinate.str());

		entry = entry2;	// reset to start
		while (entry->type != DS_ATTRIBUTES &&
		    entry->type != OM_NO_MORE_TYPES)
			entry++;

		if (entry->type == DS_ATTRIBUTES) {
			attr = entry->value.object.object;
			attr2 = ++attr;	// skip OM_CLASS

			while (attr->type != DS_ATTRIBUTE_TYPE &&
			    attr->type != OM_NO_MORE_TYPES)
				attr++;

			if (compare_om_oids(DS_A_PRESENTATION_ADDRESS,
			    attr->value.string)) {

				x500_debug("CX500DUA::list_bindings: %s\n",
				"presentation-address attribute encountered");

				while (attr2->type != DS_ATTRIBUTE_VALUES &&
				    attr2->type != OM_NO_MORE_TYPES)
					attr2++;

				// convert presentation address and append to
				//  reference
				while (attr2->type == DS_ATTRIBUTE_VALUES) {
					unsigned char	*paddr;
					int		len;

					paddr = xds_paddr_to_string(
					    attr2->value.object.object, len);

					FN_ref_addr	ref_addr(osi_paddr, len,
							    paddr);

					ref.append_addr(ref_addr);

					delete [] paddr;

					x500_debug("%s %s\n",
					    "CX500DUA::list_bindings: added ",
					    "presentation-address attribute");

					attr2++;
				}
			} else {
				// TBD: other reference attributes

				x500_debug("CX500DUA::list_bindings: %s\n",
					"unknown attribute encountered");

				continue;
			}
		}
		binding_set->add(*rdn_string, ref);
		delete rdn_string;
	}
	om_delete(pub_result);
	err = FN_SUCCESS;
	return (new FN_bindinglist_svc(binding_set));
}


/*
 * Add the supplied reference to the specified entry using ds_modify_entry()
 */
int
CX500DUA::bind_next(
	const FN_string	&name,
	const FN_ref	&ref,
	unsigned int	exclusive
)
{
	x500_debug("CX500DUA::bind_next(\"%s\")\n", name.str());

	DS_status		status;
	OM_private_object	dn;
	OM_sint			invoke_id;

	// convert name to XDS distinguished name format
	dn = string_to_xds_dn(name);

	x500_debug("CX500DUA::bind_next: ds_modify_entry dn: ...\n"),
	x500_dump(dn);

	// build DS_C_ENTRY_MOD object
	OM_descriptor	add_ref[5];
	OM_descriptor	*xref = &add_ref[3];	// reference

	fill_om_desc(add_ref[0], OM_CLASS, DS_C_ENTRY_MOD);
	fill_om_desc(add_ref[1], DS_MOD_TYPE, DS_ADD_ATTRIBUTE,
		OM_S_ENUMERATION);
	fill_om_desc(add_ref[2], DS_ATTRIBUTE_TYPE, DS_A_NNS_REF_STRING);
	fill_om_desc(add_ref[3], DS_ATTRIBUTE_VALUES, (OM_descriptor *)0);
	fill_om_desc(add_ref[4]);

	if (! ref_to_xds_attr_value(&ref, &add_ref[3])) {
		om_delete(dn);
		return (FN_E_MALFORMED_REFERENCE);
	}

	// build DS_C_ENTRY_MOD_LIST object
	OM_descriptor	mod_list[4];

	fill_om_desc(mod_list[0], OM_CLASS, DS_C_ENTRY_MOD_LIST);
	if (exclusive) {
		// fail if ref attribute already present
		fill_om_desc(mod_list[1], DS_CHANGES, add_ref);
		fill_om_desc(mod_list[2]);
	} else {
		// overwrite if ref attribute already present
		fill_om_desc(mod_list[1], DS_CHANGES, remove_ref);
		fill_om_desc(mod_list[2], DS_CHANGES, add_ref);
		fill_om_desc(mod_list[3]);
	}

	x500_debug("CX500DUA::bind_next: ds_modify_entry changes: ...\n"),
	x500_dump(mod_list);

	// add the reference attribute
	if ((status = ds_modify_entry(session, DS_DEFAULT_CONTEXT, dn,
	    mod_list, &invoke_id)) != DS_SUCCESS) {

		x500_debug("CX500DUA::bind_next: %s\n",
		    "ds_modify_entry status: ..."),
		x500_dump(status);

		int	xerr = xds_error_to_int(status);

		if ((xerr == DS_E_NO_SUCH_ATTRIBUTE_OR_VALUE) &&
		    (! exclusive)) {
			// ref attribute was not present so add it

			fill_om_desc(mod_list[1], DS_CHANGES, add_ref);
			fill_om_desc(mod_list[2]);

			x500_debug("CX500DUA::bind_next: %s\n",
			    "ds_modify_entry changes: ..."),
			x500_dump(mod_list);

			om_delete(status);
			if ((status = ds_modify_entry(session,
			    DS_DEFAULT_CONTEXT, dn, mod_list, &invoke_id)) !=
				DS_SUCCESS) {

				x500_debug("CX500DUA::bind_next: %s\n",
				    "ds_modify_entry status: ..."),
				x500_dump(status);

				int err = xds_error_to_xfn(status);

				if (xref->syntax == OM_S_OCTET_STRING)
					delete [] xref->value.string.elements;
				om_delete(dn);
				om_delete(status);
				return (err);
			}
		} else {
			int	err = xds_error_to_xfn(status);

			if (xref->syntax == OM_S_OCTET_STRING)
				delete [] xref->value.string.elements;
			om_delete(dn);
			om_delete(status);
			return (err);
		}
	}

	if (xref->syntax == OM_S_OCTET_STRING)
		delete [] xref->value.string.elements;
	om_delete(dn);
	return (FN_SUCCESS);
}


/*
 * Remove the specified entry using ds_remove_entry()
 */
int
CX500DUA::unbind(
	const FN_string	&name
)
{
	x500_debug("CX500DUA::unbind(\"%s\")\n", name.str());

	DS_status		status;
	OM_private_object	dn;
	OM_sint			invoke_id;

	// convert name to XDS distinguished name format
	dn = string_to_xds_dn(name);

	x500_debug("CX500DUA::unbind: ds_remove_entry dn: ...\n"),
	x500_dump(dn);

	// remove the entry
	if ((status = ds_remove_entry(session, DS_DEFAULT_CONTEXT, dn,
	    &invoke_id)) != DS_SUCCESS) {

		x500_debug("CX500DUA::unbind: ds_remove_entry status: ...\n"),
		x500_dump(status);

		int	xerr = xds_error_to_int(status);

		if (xerr == DS_E_NO_SUCH_OBJECT) {

			// if parent entry exists, return success

			OM_public_object	match;
			OM_type			tl[] = {DS_MATCHED, 0};
			OM_value_position	total;
			unsigned char		*match_dn;

			if ((om_get(status, OM_EXCLUDE_ALL_BUT_THESE_TYPES, tl,
			    OM_FALSE, 0, 0, &match, &total) == OM_SUCCESS) &&
			    total) {

				const unsigned char	*leaf_dn = name.str();
				const unsigned char	*cp = leaf_dn;
				int			match_len;

				// exclude final RDN

				if (cp) {
					while (*cp++)
						;

					while (*cp != '/')
						cp--;	// backup to final slash

					match_len = cp - leaf_dn;

					if (match_dn = xds_dn_to_string(
					    match->value.object.object, 1)) {

						if ((match_len == 0) ||
						    (strncasecmp(
						    (char *)leaf_dn,
						    (char *)match_dn,
						    match_len) == 0)) {

							x500_debug("%s: %s\n",
							    "CX500DUA::unbind",
							    "parent exists");

							om_delete(dn);
							om_delete(status);
							om_delete(match);
							delete [] match_dn;
							return (FN_SUCCESS);
						}
						delete [] match_dn;
					}
				}
				om_delete(match);
			}
		}

		int	err = xds_error_to_xfn(status);

		om_delete(dn);
		om_delete(status);
		return (err);
	}

	om_delete(dn);
	return (FN_SUCCESS);
}


/*
 * Remove reference from the specified entry using ds_modify_entry()
 */
int
CX500DUA::unbind_next(
	const FN_string	&name
)
{
	x500_debug("CX500DUA::unbind_next(\"%s\")\n", name.str());

	DS_status		status;
	OM_private_object	dn;
	OM_private_object	changes;
	OM_sint			invoke_id;

	// convert name to XDS distinguished name format
	dn = string_to_xds_dn(name);

	x500_debug("CX500DUA::unbind_next: ds_modify_entry dn: ...\n"),
	x500_dump(dn);

	// use a DS_C_ENTRY_MOD_LIST object to remove the reference attribute
	changes = mod_list_remove;

	x500_debug("CX500DUA::unbind_next: ds_modify_entry changes: ...\n"),
	x500_dump(changes);

	// remove the reference attribute
	if ((status = ds_modify_entry(session, DS_DEFAULT_CONTEXT, dn,
	    changes, &invoke_id)) != DS_SUCCESS) {

		x500_debug("CX500DUA::unbind_next: ds_modify_entry %s: ...\n",
		    "status"),
		x500_dump(status);

		int	xerr = xds_error_to_int(status);

		// if no binding present return success
		if (xerr == DS_E_NO_SUCH_ATTRIBUTE_OR_VALUE) {
			om_delete(dn);
			om_delete(status);
			return (FN_SUCCESS);
		}

		int	err = xds_error_to_xfn(status);

		om_delete(dn);
		om_delete(status);
		return (err);
	}

	om_delete(dn);
	return (FN_SUCCESS);
}


/*
 * Rename the specified leaf entry to the supplied name using ds_modify_rdn()
 */
int
CX500DUA::rename(
	const FN_string	&name,
	const FN_string	*newname,
	unsigned int
)
{
	x500_debug("CX500DUA::rename(\"%s\",\"%s\")\n", name.str(),
		newname->str());

	DS_status		status;
	OM_private_object	dn_old;
	OM_private_object	dn_new;
	OM_object		rdn;
	OM_sint			invoke_id;

	// convert name to XDS distinguished name format
	dn_old = string_to_xds_dn(name);

	x500_debug("CX500DUA::rename: ds_modify_rdn dn_old: ...\n"),
	x500_dump(dn_old);

	// convert name to XDS distinguished name format
	dn_new = string_to_xds_dn(*newname);

	// locate rdn within the dn
	{
		OM_type			tl[] = {DS_RDNS, 0};
		OM_value_position	total;

		if ((om_get(dn_new,
		    OM_EXCLUDE_ALL_BUT_THESE_TYPES + OM_EXCLUDE_SUBOBJECTS, tl,
		    OM_FALSE, 0, 0, &rdn, &total) != OM_SUCCESS) || (! total)) {

			om_delete(dn_old);
			om_delete(dn_new);
			return (0);
		}
	}
	x500_debug("CX500DUA::rename: ds_modify_rdn rdn: ...\n"),
	x500_dump(rdn->value.object.object);

	// rename the specified entry
	if ((status = ds_modify_rdn(session, DS_DEFAULT_CONTEXT, dn_old,
	    rdn->value.object.object, OM_TRUE, &invoke_id)) != DS_SUCCESS) {

		x500_debug("CX500DUA::rename: ds_modify_rdn status: ...\n"),
		x500_dump(status);

		int	err = xds_error_to_xfn(status);

		om_delete(dn_old);
		om_delete(dn_new);
		om_delete(rdn);
		om_delete(status);
		return (err);
	}

	om_delete(dn_old);
	om_delete(dn_new);
	om_delete(rdn);
	return (FN_SUCCESS);
}


/*
 * Query the specified entry for the requested attribute using ds_read().
 */
FN_attribute *
CX500DUA::get_attr(
	const FN_string		&name,
	const FN_identifier	&id,
	int			&err
)
{
	DS_status		status;
	OM_private_object	dn;
	OM_private_object	selection;
	OM_private_object	result;
	OM_sint			invoke_id;

	x500_debug("CX500DUA::get_attr(\"%s\")\n", name.str());

	// convert string name to XDS distinguished name format
	dn = string_to_xds_dn(name);

	x500_debug("CX500DUA::get_attr: ds_read dn: ...\n"),
	x500_dump(dn);

	if (! (selection = id_to_xds_selection(id))) {
		err = FN_E_INVALID_ATTR_IDENTIFIER;
		om_delete(dn);
		return (0);
	}

	x500_debug("CX500DUA::get_attr: ds_read selection: ...\n"),
	x500_dump(selection);

	// retrieve the requested attribute
	if ((status = ds_read(session, DS_DEFAULT_CONTEXT, dn, selection,
	    &result, &invoke_id)) != DS_SUCCESS) {

		x500_debug("CX500DUA::get_attr: ds_read status: ...\n"),
		x500_dump(status);

		err = xds_error_to_xfn(status);
		om_delete(dn);
		om_delete(selection);
		om_delete(status);
		return (0);
	}
	om_delete(dn);
	om_delete(selection);

	x500_debug("CX500DUA::get_attr: ds_read result: ...\n"),
	x500_dump(result);

	// extract requested attribute value(s)
	OM_type			route[] = {DS_ENTRY, DS_ATTRIBUTES, 0};
	OM_type			type[] = {DS_ATTRIBUTE_VALUES, 0};
	OM_public_object	pub_result;
	OM_value_position	total;

	if (deep_om_get(route, result, OM_EXCLUDE_ALL_BUT_THESE_TYPES, type,
	    OM_FALSE, 0, 0, &pub_result, &total) != OM_SUCCESS) {

		om_delete(dn);
		om_delete(selection);
		om_delete(result);
		err = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}
	om_delete(result);

	// build attribute
	FN_attribute	*attr = 0;

	x500_debug("CX500DUA::get_attr: retrieved %d attribute value%s\n",
	    total, (total == 1) ? "" : "s");

	if (total == 0) {
		err = FN_SUCCESS;
		return (0);
	}

	OM_string 	*val;
	OM_string	str;
	unsigned int	format;
	unsigned int	length;

	while (total--) {
		val = &pub_result[total].value.string;

		if (! (str.elements = xds_attr_value_to_string(
		    &pub_result[total], format, length))) {

			x500_debug("CX500DUA::get_attr: %s\n",
			    "cannot convert attribute value to string");

			continue;	// ignore it
		}

		const FN_attrvalue	av(str.elements, length);

		delete [] str.elements;	// xds_attr_value_to_string() allocates

		if (! attr)
			attr = new FN_attribute(id,
			    (format == FN_ID_STRING) ? ascii : octet);
			// X.500 attribute value is either ascii or octet string

		attr->add(av);
	}
	om_delete(pub_result);
	err = FN_SUCCESS;
	return (attr);
}


/*
 * Query the specified entry for all its attribute types using ds_read().
 */
FN_attrset *
CX500DUA::get_attr_ids(
	const FN_string		&name,
	int			&err
)
{
	DS_status		status;
	OM_private_object	dn;
	OM_private_object	result;
	OM_sint			invoke_id;

	x500_debug("CX500DUA::get_attr_ids(\"%s\")\n", name.str());

	// convert string name to XDS distinguished name format
	dn = string_to_xds_dn(name);

	x500_debug("CX500DUA::get_attr_ids: ds_read dn: ...\n"),
	x500_dump(dn);

	// retrieve requested the attribute
	if ((status = ds_read(session, DS_DEFAULT_CONTEXT, dn, select_all_types,
	    &result, &invoke_id)) != DS_SUCCESS) {

		x500_debug("CX500DUA::get_attr_ids: ds_read status: ...\n"),
		x500_dump(status);

		err = xds_error_to_xfn(status);
		om_delete(dn);
		om_delete(status);
		return (0);
	}
	om_delete(dn);

	x500_debug("CX500DUA::get_attr_ids: ds_read result: ...\n"),
	x500_dump(result);

	// extract attribute(s)
	OM_type			route[] = {DS_ENTRY, 0};
	OM_type			type[] = {DS_ATTRIBUTES, 0};
	OM_public_object	pub_result;
	OM_value_position	total;

	if (deep_om_get(route, result, OM_EXCLUDE_ALL_BUT_THESE_TYPES, type,
	    OM_FALSE, 0, 0, &pub_result, &total) != OM_SUCCESS) {

		om_delete(result);
		err = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}
	om_delete(result);

	// build set of attribute ids
	FN_attribute	*attr = 0;
	FN_attrset	*attrs = 0;

	x500_debug("CX500DUA::get_attr_ids: retrieved %d attribute id%s\n",
	    total, (total == 1) ? "" : "s");

	if (total == 0) {
		err = FN_SUCCESS;
		return (0);
	}

	OM_descriptor	*at;
	OM_string 	*val;
	OM_string	str;
	unsigned int	format;
	unsigned int	length;
	OM_syntax	syntax;

	while (total--) {
		at = pub_result[total].value.object.object + 1;
		val = &at->value.string;

		if (! (str.elements = xds_attr_value_to_string(at, format,
		    length))) {

			x500_debug("CX500DUA::get_attr_ids: %s\n",
			    "cannot convert attribute type to string");

			continue;	// ignore it
		}
		const FN_identifier	id(format, length, str.elements);

		delete [] str.elements;	// xds_attr_value_to_string() allocates

		if (! attrs)
			attrs = new FN_attrset;

		if (! om_oid_to_syntax(val, &syntax, 0)) {

			x500_debug("CX500DUA::get_attr_ids: %s\n",
			    "cannot locate attribute's syntax");

			continue;	// ignore it
		}
		attrs->add(FN_attribute(id,
		    (syntax != OM_S_OCTET_STRING) ? ascii : octet));
			// X.500 attribute value is either ascii or octet string
	}
	om_delete(pub_result);
	err = FN_SUCCESS;
	return (attrs);
}


/*
 * Query the specified entry for the requested attributes using ds_read().
 */
FN_multigetlist *
CX500DUA::get_attrs(
	const FN_string		&name,
	const FN_attrset	*ids,
	int			&err
)
{
	DS_status		status;
	OM_private_object	dn;
	OM_private_object	selection;
	OM_private_object	result;
	OM_sint			invoke_id;

	x500_debug("CX500DUA::get_attrs(\"%s\")\n", name.str());

	// convert string name to XDS distinguished name format
	dn = string_to_xds_dn(name);

	x500_debug("CX500DUA::get_attrs: ds_read dn: ...\n"),
	x500_dump(dn);

	selection = ids_to_xds_selection(ids);

	if ((selection != DS_SELECT_ALL_TYPES_AND_VALUES) &&
	    (selection != DS_SELECT_ALL_TYPES) &&
	    (selection != DS_SELECT_NO_ATTRIBUTES)) {

		x500_debug("CX500DUA::get_attrs: ds_read selection: ...\n"),
		x500_dump(selection);
	}

	// retrieve requested attributes
	if ((status = ds_read(session, DS_DEFAULT_CONTEXT, dn, selection,
	    &result, &invoke_id)) != DS_SUCCESS) {

		x500_debug("CX500DUA::get_attrs: ds_read status: ...\n"),
		x500_dump(status);

		err = xds_error_to_xfn(status);
		om_delete(dn);
		om_delete(selection);
		om_delete(status);
		return (0);
	}
	om_delete(dn);
	om_delete(selection);

	x500_debug("CX500DUA::get_attrs: ds_read result: ...\n"),
	x500_dump(result);

	// extract requested attribute value(s)
	OM_type			route[] = {DS_ENTRY, 0};
	OM_type			type[] = {DS_ATTRIBUTES, 0};
	OM_public_object	pub_result;
	OM_value_position	total;

	if (deep_om_get(route, result, OM_EXCLUDE_ALL_BUT_THESE_TYPES, type,
	    OM_FALSE, 0, 0, &pub_result, &total) != OM_SUCCESS) {

		om_delete(result);
		err = FN_E_INSUFFICIENT_RESOURCES;
		return (0);
	}
	om_delete(result);

	// build set of attributes
	FN_attrset	*attrs = 0;

	x500_debug("CX500DUA::get_attrs: retrieved %d attribute%s\n",
	    total, (total == 1) ? "" : "s");

	if (total == 0) {
		err = FN_SUCCESS;
		return (0);
	}

	OM_descriptor	*at;
	OM_descriptor	*at2;
	OM_string 	*val;
	OM_string	str;
	FN_identifier	*id;
	FN_attribute	*attr;
	unsigned int	format;
	unsigned int	length;

	while (total--) {
		at2 = at = pub_result[total].value.object.object + 1;
		attr = 0;

		while (at->type != OM_NO_MORE_TYPES) {
			val = &at->value.string;

			if (at->type == DS_ATTRIBUTE_TYPE) {

				if (! (str.elements =
				    xds_attr_value_to_string(at, format,
				    length))) {

					x500_debug("CX500DUA::get_attrs: %s %s",
					    "cannot convert attribute",
					    "type to string\n");

					continue;	// ignore it
				}
				id = new FN_identifier(format, length,
				    str.elements);

				delete [] str.elements;
					// xds_attr_value_to_string() allocates
				break;
			}
			at++;
		}

		at = at2;	// reset
		while (at->type != OM_NO_MORE_TYPES) {
			val = &at->value.string;

			if (at->type == DS_ATTRIBUTE_VALUES) {

				if (! (str.elements =
				    xds_attr_value_to_string(at, format,
				    length))) {

					x500_debug("CX500DUA::get_attrs: %s %s",
					    "cannot convert attribute",
					    "value to string\n");

					continue;	// ignore it
				}
				if (! attr)
					attr = new FN_attribute(*id,
					    ((format == FN_ID_STRING) ||
					    (format == FN_ID_ISO_OID_STRING) ||
					    (format == FN_ID_DCE_UUID)) ?
					    ascii : octet);

				attr->add(FN_attrvalue(str.elements, length));

				delete [] str.elements;
					// xds_attr_value_to_string() allocates
			}
			at++;
		}
		if (! attrs)
			attrs = new FN_attrset;

		attrs->add(*attr);

		delete attr;
		delete id;
	}

	om_delete(pub_result);
	err = FN_SUCCESS;
	return (new FN_multigetlist_svc(attrs));
}


/*
 * Modify the specified entry using ds_modify_entry().
 */
int
CX500DUA::modify_attr(
	const FN_string		&name,
	unsigned int		mod_op,
	const FN_attribute	&attr
)
{
	DS_status		status;
	OM_private_object	dn;
	OM_sint			invoke_id;
	FN_attrset		*attrs = 0;
	int			op;

	x500_debug("CX500DUA::modify_attr(\"%s\")\n", name.str());

	// convert string name to XDS distinguished name format
	dn = string_to_xds_dn(name);

	x500_debug("CX500DUA::modify_attr: ds_modify_entry dn: ...\n"),
	x500_dump(dn);

	switch (mod_op) {
	case FN_ATTR_OP_ADD:		// replace
	case FN_ATTR_OP_ADD_EXCLUSIVE:	// add
		op = DS_ADD_ATTRIBUTE;
		break;

	case FN_ATTR_OP_REMOVE:
		op = DS_REMOVE_ATTRIBUTE;
		break;

	case FN_ATTR_OP_ADD_VALUES:
		op = DS_ADD_VALUES;
		break;

	case FN_ATTR_OP_REMOVE_VALUES:
		op = DS_REMOVE_VALUES;
		break;

	default:
		om_delete(dn);
		return (FN_E_OPERATION_NOT_SUPPORTED);
	}

	// extract object identifier
	const FN_identifier	*id = attr.identifier();
	OM_object_identifier	oid;
	OM_syntax		syntax;
	OM_object_identifier	*class_oid;

	if ((! id) || (! id_to_om_oid(*id, oid, &syntax, &class_oid))) {
		om_delete(dn);
		return (0);
	}

	// build DS_C_ENTRY_MOD object
	void			*iter_pos;
	const FN_attrvalue	*val = attr.first(iter_pos);
	FN_string		*val_str;
	unsigned int		val_num = attr.valuecount();
	OM_descriptor		*mod_attr = new OM_descriptor[val_num + 4];
	int			i = 0;

	fill_om_desc(mod_attr[0], OM_CLASS, DS_C_ENTRY_MOD);
	fill_om_desc(mod_attr[1], DS_MOD_TYPE, op, OM_S_ENUMERATION);
	fill_om_desc(mod_attr[2], DS_ATTRIBUTE_TYPE, oid);
	fill_om_desc(mod_attr[3 + val_num]);

	while (i < val_num) {

		mod_attr[i + 3].type = DS_ATTRIBUTE_VALUES;
		mod_attr[i + 3].syntax = syntax;

		val_str = val->string();
		if (! string_to_xds_attr_value(val_str, syntax, class_oid,
		    &mod_attr[i + 3])) {

			while (i--)
				delete_xds_attr_value(&mod_attr[i + 3]);

			delete val_str;
			delete [] mod_attr;
			om_delete(dn);
			return (0);
		}
		val = attr.next(iter_pos);
		i++;
	}

	// build DS_C_ENTRY_MOD object
	OM_descriptor	remove_attr[4];

	// build DS_C_ENTRY_MOD_LIST object
	OM_descriptor	mod_list[4];

	fill_om_desc(mod_list[0], OM_CLASS, DS_C_ENTRY_MOD_LIST);
	if (mod_op == FN_ATTR_OP_ADD) {

		fill_om_desc(remove_attr[0], OM_CLASS, DS_C_ENTRY_MOD);
		fill_om_desc(remove_attr[1], DS_MOD_TYPE, DS_REMOVE_ATTRIBUTE,
		    OM_S_ENUMERATION);
		fill_om_desc(remove_attr[2], DS_ATTRIBUTE_TYPE, oid);
		fill_om_desc(remove_attr[3]);

		fill_om_desc(mod_list[1], DS_CHANGES, remove_attr);
		fill_om_desc(mod_list[2], DS_CHANGES, mod_attr);
		fill_om_desc(mod_list[3]);
	} else {
		fill_om_desc(mod_list[1], DS_CHANGES, mod_attr);
		fill_om_desc(mod_list[2]);
	}

	x500_debug("CX500DUA::modify_attr: ds_modify_entry changes: ...\n"),
	x500_dump(mod_list);

	// make the specified modifications
	if ((status = ds_modify_entry(session, DS_DEFAULT_CONTEXT, dn, mod_list,
	    &invoke_id)) != DS_SUCCESS) {

		int	xerr = xds_error_to_int(status);

		if ((xerr == DS_E_NO_SUCH_OBJECT) &&
		    ((op == DS_ADD_ATTRIBUTE) || (op == DS_ADD_VALUES))) {

			// create the entry since it doesn't exist
			OM_descriptor	entry[3];

			fill_om_desc(entry[0], OM_CLASS, DS_C_ATTRIBUTE_LIST);
			fill_om_desc(entry[1], DS_ATTRIBUTES, mod_attr);
			fill_om_desc(entry[2]);

			x500_debug("CX500DUA::modify_attr: %s\n",
			    "ds_add_entry entry: ..."),
			x500_dump(entry);

			om_delete(status);
			if ((status = ds_add_entry(session, DS_DEFAULT_CONTEXT,
			    dn, entry, &invoke_id)) != DS_SUCCESS) {

				x500_debug("CX500DUA::modify_attr: %s\n",
				    "ds_add_entry status: ..."),
				x500_dump(status);

				int err = xds_error_to_xfn(status);

				while (val_num--)
					delete_xds_attr_value(
					    &mod_attr[val_num + 3]);

				om_delete(dn);
				om_delete(status);
				delete [] mod_attr;
				return (err);
			}
		} else if ((xerr == DS_E_NO_SUCH_ATTRIBUTE_OR_VALUE) &&
		    (mod_op == FN_ATTR_OP_ADD_VALUES)) {

			// create the attribute since it doesn't exist
			mod_attr[1].value.enumeration = DS_ADD_ATTRIBUTE;

			x500_debug("CX500DUA::modify_attr: %s\n",
			    "ds_modify_entry changes: ..."),
			x500_dump(mod_list);

			om_delete(status);
			if ((status = ds_modify_entry(session,
			    DS_DEFAULT_CONTEXT, dn, mod_list, &invoke_id)) !=
			    DS_SUCCESS) {

				x500_debug("CX500DUA::modify_attr:%s\n",
				    " ds_modify_entry status: ..."),
				x500_dump(status);

				int err = xds_error_to_xfn(status);

				while (val_num--)
					delete_xds_attr_value(
					    &mod_attr[val_num + 3]);

				om_delete(dn);
				om_delete(status);
				delete [] mod_attr;
				return (err);
			}
		} else {

			x500_debug("CX500DUA::modify_attr: %s\n",
			    "ds_modify_entry status: ..."),
			x500_dump(status);

			int err = xds_error_to_xfn(status);

			while (val_num--)
				delete_xds_attr_value(&mod_attr[val_num + 3]);

			om_delete(dn);
			om_delete(status);
			delete [] mod_attr;
			return (err);
		}
	}

	while (val_num--)
		delete_xds_attr_value(&mod_attr[val_num + 3]);

	delete [] mod_attr;
	om_delete(dn);
	return (FN_SUCCESS);
}
