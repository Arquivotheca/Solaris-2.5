/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)x500dua2.cc	1.4	95/03/13 SMI"

#include <string.h>
#include <stdlib.h>	// bsearch()
#include <ctype.h>	// isalnum()
#include "x500dua.hh"

const int	max_oid_length = 128;	// object identifier
					// (dotted decimal or ASN.1 BER form)
const int	max_dn_length = 512;	// string distinguished name
					// (X/Open DCE Directory form)
const int	max_paddr_length = 1024; // string presentation address
					// (RFC-1278 encoding)
const int	max_naddr_length = 128;	// string network-address
					// (RFC-1278 encoding)
const int	max_post_length = 256;	// string postal address
					// (RFC-1488 encoding)
const int	max_ref_length = 1024;	// reference string


/*
 * Fill an OM_descriptor structure (no argument validation)
 */
void
CX500DUA::fill_om_desc(
	OM_descriptor	&desc
) const
{
	desc.type = OM_NO_MORE_TYPES;
	desc.syntax = OM_S_NO_MORE_SYNTAXES;
	desc.value.string.length = 0;
	desc.value.string.elements = OM_ELEMENTS_UNSPECIFIED;
}


/*
 * Fill an OM_descriptor structure (no argument validation)
 */
void
CX500DUA::fill_om_desc(
	OM_descriptor	&desc,
	const OM_type	type,
	OM_descriptor	*object
) const
{
	desc.type = type;
	desc.syntax = OM_S_OBJECT;
	desc.value.object.padding = 0;
	desc.value.object.object = object;
}


/*
 * Fill an OM_descriptor structure (no argument validation)
 */
void
CX500DUA::fill_om_desc(
	OM_descriptor	&desc,
	const OM_type	type,
	const OM_string	oid
) const
{
	desc.type = type;
	desc.syntax = OM_S_OBJECT_IDENTIFIER_STRING;
	desc.value.string.length = oid.length;
	desc.value.string.elements = oid.elements;
}


/*
 * Fill an OM_descriptor structure (no argument validation)
 */
void
CX500DUA::fill_om_desc(
	OM_descriptor		&desc,
	const OM_type		type,
	const OM_syntax		syntax,
	void			*string,
	const OM_string_length	length
) const
{
	desc.type = type;
	desc.syntax = syntax;
	desc.value.string.length = length;
	desc.value.string.elements = string;

	// explicitly set the length (XOM has problems)
	if (length == OM_LENGTH_UNSPECIFIED)
		desc.value.string.length = strlen((const char *)string);
}


/*
 * Fill an OM_descriptor structure (no argument validation)
 */
void
CX500DUA::fill_om_desc(
	OM_descriptor	&desc,
	const OM_type	type,
	const OM_sint32	number,
	const OM_syntax	syntax
) const
{
	desc.type = type;
	if ((desc.syntax = syntax) == OM_S_ENUMERATION)
		desc.value.enumeration = number;
	else
		desc.value.integer = number;
}


/*
 * Fill an OM_descriptor structure (no argument validation)
 */
void
CX500DUA::fill_om_desc(
	OM_descriptor		&desc,
	const OM_type		type,
	const OM_boolean	boolean
) const
{
	desc.type = type;
	desc.syntax = OM_S_BOOLEAN;
	desc.value.boolean = boolean;
}


/*
 * Compare 2 object identifiers (in ASN.1 BER format)
 * (returns equal or not-equal)
 */
int
CX500DUA::compare_om_oids(
	OM_object_identifier	&oid1,
	OM_object_identifier	&oid2
) const
{
	int	i;

	if ((i = (int)oid1.length) == oid2.length) {
		// test final octets first
		while ((--i >= 0) && (((unsigned char *)oid1.elements)[i] ==
		    ((unsigned char *)oid2.elements)[i]))
			;
		if (i < 0)
			return (1);
	}

	return (0);
}


/*
 * Map an object identifier in ASN.1 BER format into its dotted string format
 * (e.g. ASN.1 BER decoding of "\x55\x04\x06" produces "2.5.4.6")
 */
unsigned char *
CX500DUA::om_oid_to_string_oid(
	OM_object_identifier	*oid
) const
{
	unsigned char	buf[max_oid_length];
	unsigned char	idbuf[5];
	unsigned char	*bp = buf;
	unsigned char	*cp;
	unsigned long	id;
	int		i = 0;
	int		j;
	int		k;

	cp = (unsigned char *)oid->elements;

	// ASN.1 BER: first x 40 + second
	bp += sprintf((char *)bp, "%d", *cp / 40);
	*bp++ = '.';
	bp += sprintf((char *)bp, "%d", *cp % 40);
	cp++;

	for (i = 1; i < oid->length; i++, cp++) {
		if (*cp <= 0x7F)
			bp += sprintf((char *)bp, ".%d", *cp);
		else {
			// ASN.1 BER: skip high bit and extract block of 7 bits
			id = idbuf[0] = 0;
			for (j = 1; *cp > 0x7F; j++, i++, cp++)  {
				idbuf[j] = *cp & 0x7F;
			}
			idbuf[j] = *cp;
			for (k = 0; j; k++, j--) {
				id |= (idbuf[j] >> k |
				    (idbuf[j-1] << (7 - k) & 0xFF)) << 8 * k;
			}
			bp += sprintf((char *)bp, ".%ld", id);
		}
	}
	*bp++ = '\0';
	int	len = bp - buf;

	return ((unsigned char *)memcpy(new unsigned char [len], buf,
	    (size_t)len));
}


/*
 * Map an object identifier in dotted string format into its ASN.1 BER format
 * (e.g. ASN.1 BER encoding of "2.5.4.6" produces "\x55\x04\x06")
 */
OM_object_identifier *
CX500DUA::string_oid_to_om_oid(
	const char	*oid
) const
{
	unsigned char	oid_buf[max_oid_length];	// BER encoded object id
	unsigned char	sub_buf[5];	// BER encoded sub-identifier
	unsigned char	*oid_copy;	// string object identifier (copy)
	unsigned char	*cp1;
	unsigned char	*cp2;
	unsigned long	id;		// decimal identifier
	int		i = 0;
	int		j;

	// make a copy of the supplied oid string (the copy will be modified)
	int	len = strlen(oid);

	cp1 = oid_copy = (unsigned char *)memcpy(new unsigned char [len + 1],
	    oid, (size_t)len + 1);

	while (cp1) {
		// locate the dot that separates identifiers
		if (cp2 = (unsigned char *)strchr((const char *)cp1, '.'))
			*cp2 = '\0';	// mark end of string
		id = atol((const char *)cp1);
		if (id <= 0x7F) {
			oid_buf[i++] = (unsigned char)id;
		} else {
			// ASN.1 BER: set high bit and fill block of 7 bits
			for (j = 0; id; id >>= 7, j++) {
				sub_buf[j] = (unsigned char)id & 0x7F |
				    (j ? 0x80 : 0);
			}
			while (j)
				oid_buf[i++] = sub_buf[--j];
		}
		if (cp2)
			cp1 = ++cp2;
		else
			cp1 = cp2;
	}
	delete [] oid_copy;

	// ASN.1 BER: first x 40 + second
	oid_buf[1] = (oid_buf[0] * 40) + oid_buf[1];

	OM_string	*om_oid = new OM_string;

	om_oid->length = i - 1;	// the first entry will be skipped
	om_oid->elements = memcpy(new unsigned char [i - 1], &oid_buf[1],
	    (size_t)i - 1);
	return (om_oid);
}


/*
 * Map an object identifier's string abbreviation to its ASN.1 BER format.
 * Also accepts dotted decimal object identifiers (e.g. 2.5.4.0).
 * Set the appropriate syntax.
 *
 * e.g.  'C' or 'c'  -> DS_A_COUNTRY_NAME
 *      'CN' or 'cn' -> DS_A_COMMON_NAME
 *       'O' or 'o'  -> DS_A_ORG_NAME
 *	'OU' or 'ou' -> DS_A_ORG_UNIT_NAME
 *       'L' or 'l'  -> DS_A_LOCALITY_NAME
 *      'ST' or 'st' -> DS_A_STATE_OR_PROV_NAME
 *      'SN' or 'sn' -> DS_A_SURNAME
 *
 * TBD: use XDS init files to locate valid abbreviations
 */
OM_object_identifier *
CX500DUA::abbrev_to_om_oid(
	const char	*oid,
	OM_syntax	&syntax
) const
{
	OM_object_identifier	*om_oid;

	syntax = OM_S_TELETEX_STRING;	// default

	if (strcasecmp(oid, "C") == 0) {
		syntax = OM_S_PRINTABLE_STRING;
		om_oid = &DS_A_COUNTRY_NAME;

	} else if (strcasecmp(oid, "CN") == 0) {
		om_oid = &DS_A_COMMON_NAME;

	} else if (strcasecmp(oid, "O") == 0) {
		om_oid = &DS_A_ORG_NAME;

	} else if (strcasecmp(oid, "OU") == 0) {
		om_oid = &DS_A_ORG_UNIT_NAME;

	} else if (strcasecmp(oid, "L") == 0) {
		om_oid = &DS_A_LOCALITY_NAME;

	} else if (strcasecmp(oid, "ST") == 0) {
		om_oid = &DS_A_STATE_OR_PROV_NAME;

	} else if (strcasecmp(oid, "SN") == 0) {
		om_oid = &DS_A_SURNAME;

	} else if (strchr(oid, '.')) {
		return (string_oid_to_om_oid(oid));

	} else
		return (0);

	OM_object_identifier	*om_oid2 = new OM_object_identifier;
	int			len = (int) om_oid->length;

	om_oid2->length = len;
	om_oid2->elements = memcpy(new unsigned char [len], om_oid->elements,
	    (size_t)len);
	return (om_oid2);
}


/*
 * Extract the error code from an XDS error object
 */
int
CX500DUA::xds_error_to_int(
	OM_private_object	&status
) const
{
	OM_private_object	err = status;
	OM_boolean		instance;
	OM_type			tl[] = {DS_PROBLEM, 0};
	OM_public_object	problem;
	OM_value_position	total;

	if (om_instance(status, DS_C_ATTRIBUTE_ERROR, &instance) != OM_SUCCESS)
		return (DS_E_MISCELLANEOUS);

	if (instance == OM_TRUE) {	// only examine the first attr problem

		x500_debug("CX500DUA::xds_error_to_int: %s\n",
		    "DS_C_ATTRIBUTE_ERROR encountered");

		OM_type	tl[] = {DS_PROBLEMS, 0};

		if ((om_get(status,
		    OM_EXCLUDE_ALL_BUT_THESE_TYPES + OM_EXCLUDE_SUBOBJECTS, tl,
		    OM_FALSE, 0, 0, &err, &total) != OM_SUCCESS) && (! total))
			return (DS_E_MISCELLANEOUS);

		err = err->value.object.object;
	}

	if (om_get(err, OM_EXCLUDE_ALL_BUT_THESE_TYPES, tl, OM_FALSE, 0, 0,
	    &problem, &total) != OM_SUCCESS) {

		if (instance == OM_TRUE)
			om_delete(err);
		return (DS_E_MISCELLANEOUS);
	}

	x500_debug("CX500DUA::xds_error_to_int: %d (%s)\n",
	    problem->value.enumeration,
	    xds_problems[problem->value.enumeration]);

	if (instance == OM_TRUE)
		om_delete(err);
	om_delete(problem);

	return (total ? problem->value.enumeration : DS_E_MISCELLANEOUS);
}


/*
 * Map an attribute object identifier in ASN.1 BER format to its OM syntax
 * (and OM class).
 */
int
CX500DUA::om_oid_to_syntax(
	OM_object_identifier	*oid,
	OM_syntax		*syntax,
	OM_object_identifier	**class_oid
) const
{
	oid_to_string_t	key;
	oid_to_string_t	*entry;

	key.oid = oid;
	entry = (oid_to_string_t *)bsearch(&key, oid_to_string_table,
	    oid_to_string_table_size, sizeof (oid_to_string_t),
	    compare_om_oids2);

	if (entry) {
		*syntax = entry->syntax;
		if (class_oid)
			*class_oid = entry->class_oid;
		return (1);
	}
	else
		return (0);
}


/*
 * Map an object identifier in ASN.1 BER format to a string.
 */
unsigned char *
CX500DUA::om_oid_to_string(
	OM_object_identifier	*oid
) const
{
	oid_to_string_t	key;
	oid_to_string_t	*entry;

	key.oid = oid;
	entry = (oid_to_string_t *)bsearch(&key, oid_to_string_table,
	    oid_to_string_table_size, sizeof (oid_to_string_t),
	    compare_om_oids2);

	if (entry) {
		char	*cp = string_to_oid_table[entry->index].string;
		int	len = strlen(cp) + 1;	// include terminator

		return ((unsigned char *)memcpy(new unsigned char [len], cp,
		    (size_t)len));
	}
	else
		return (0);
}


/*
 * Map an object identifier string to ASN.1 BER format.
 */
OM_object_identifier *
CX500DUA::string_to_om_oid(
	unsigned char	*oid
) const
{
	string_to_oid_t	key;
	string_to_oid_t	*entry;

	key.string = (char *)oid;
	entry = (string_to_oid_t *)bsearch(&key, string_to_oid_table,
	    string_to_oid_table_size, sizeof (string_to_oid_t),
	    compare_strings);

	if (entry) {
		OM_string	*om_oid = new OM_string;
		int		len = (int)entry->oid->length;

		om_oid->length = len;
		om_oid->elements = memcpy(new unsigned char [len],
		    entry->oid->elements, len);
		return (om_oid);
	}
	else
		return (0);
}


/*
 * Map XDS error code to XFN error code
 */
int
CX500DUA::xds_error_to_xfn(
	OM_private_object	&status
) const
{
	switch (xds_error_to_int(status)) {
	case DS_E_NO_SUCH_OBJECT:
		return (FN_E_NAME_NOT_FOUND);

	case DS_E_ENTRY_ALREADY_EXISTS:
		return (FN_E_NAME_IN_USE);

	case DS_E_NO_SUCH_ATTRIBUTE_OR_VALUE:
		return (FN_E_NO_SUCH_ATTRIBUTE);

	case DS_E_INVALID_ATTRIBUTE_SYNTAX:
		return (FN_E_INVALID_ATTR_VALUE);

	case DS_E_UNDEFINED_ATTRIBUTE_TYPE:
		return (FN_E_INVALID_ATTR_IDENTIFIER);

	case DS_E_INSUFFICIENT_ACCESS_RIGHTS:
		return (FN_E_CTX_NO_PERMISSION);

	case DS_E_INVALID_CREDENTIALS:
	case DS_E_NO_INFORMATION:
		return (FN_E_AUTHENTICATION_FAILURE);

	case DS_E_BAD_NAME:
	case DS_E_NAMING_VIOLATION:
		return (FN_E_ILLEGAL_NAME);

	case DS_E_UNAVAILABLE:
	case DS_E_BUSY:
		return (FN_E_CTX_UNAVAILABLE);

	case DS_E_COMMUNICATIONS_PROBLEM:
		return (FN_E_COMMUNICATION_FAILURE);

	case DS_E_UNWILLING_TO_PERFORM:
		return (FN_E_OPERATION_NOT_SUPPORTED);

	// TBD: others

	default:
		return (FN_E_UNSPECIFIED_ERROR);
	}
}


/*
 * Map an object identifier's ASN.1 BER format to its string abbreviation.
 * Also produces dotted decimal object identifiers (e.g. 2.5.4.0).
 *
 * e.g. DS_A_COUNTRY_NAME       -> 'c'
 *      DS_A_COMMON_NAME        -> 'cn'
 *      DS_A_ORG_NAME           -> 'o'
 *      DS_A_ORG_UNIT_NAME      -> 'ou'
 *      DS_A_LOCALITY_NAME      -> 'l'
 *      DS_A_STATE_OR_PROV_NAME -> 'st'
 *      DS_A_SURNAME            -> 'sn'
 *
 * TBD: use XDS init files to locate valid abbreviations
 */
unsigned char *
CX500DUA::om_oid_to_abbrev(
	OM_object_identifier	&om_oid,
	unsigned char		*oid
) const
{
	if (compare_om_oids(DS_A_COUNTRY_NAME, om_oid))
		return ((unsigned char *)strcat((char *)oid, "c=") + 2);

	if (compare_om_oids(DS_A_COMMON_NAME, om_oid))
		return ((unsigned char *)strcat((char *)oid, "cn=") + 3);

	if (compare_om_oids(DS_A_ORG_NAME, om_oid))
		return ((unsigned char *)strcat((char *)oid, "o=") + 2);

	if (compare_om_oids(DS_A_ORG_UNIT_NAME, om_oid))
		return ((unsigned char *)strcat((char *)oid, "ou=") + 3);

	if (compare_om_oids(DS_A_LOCALITY_NAME, om_oid))
		return ((unsigned char *)strcat((char *)oid, "l=") + 2);

	if (compare_om_oids(DS_A_STATE_OR_PROV_NAME, om_oid))
		return ((unsigned char *)strcat((char *)oid, "st=") + 3);

	if (compare_om_oids(DS_A_SURNAME, om_oid))
		return ((unsigned char *)strcat((char *)oid, "sn=") + 3);

	// other object identifiers: convert to dotted decimal
	unsigned char	*cp = om_oid_to_string_oid(&om_oid);

	oid = (unsigned char *)strcat((char *)oid, (char *)cp) +
	    strlen((char *)cp);
	*oid++ = '=';
	delete cp;

	return (oid);
}


/*
 * convert an XFN identifier to an object identifier in ASN.1 BER format.
 */
int
CX500DUA::id_to_om_oid(
	const FN_identifier	&id,
	OM_object_identifier	&oid,
	OM_syntax		*syntax,	// if non-zero, return syntax
	OM_object_identifier	**class_oid	// if non-zero, return class
) const
{
	OM_object_identifier	*oidp;

	switch (id.format()) {
	case FN_ID_ISO_OID_STRING:

		x500_debug("CX500DUA::id_to_om_oid: %s\n",
		    "FN_ID_ISO_OID_STRING identifier encountered");

		if (oidp = string_oid_to_om_oid((const char *)id.str())) {
			oid = *oidp;
			delete oidp;	// delete contents later
		} else
			return (0);
		break;

	case FN_ID_ISO_OID_BER:

		x500_debug("CX500DUA::id_to_om_oid: %s\n",
		    "FN_ID_ISO_OID_BER identifier encountered");

		oid.length = id.length();
		oid.elements = memcpy(new unsigned char [oid.length],
		    (void *)id.contents(), (size_t)oid.length);

		break;

	case FN_ID_STRING:

		x500_debug("CX500DUA::id_to_om_oid: %s\n",
		    "FN_ID_STRING identifier encountered");

		if (oidp = string_to_om_oid((unsigned char *)id.str())) {
			oid = *oidp;
			delete oidp;	// delete contents later
		} else {

			x500_debug("CX500DUA::id_to_om_oid: %s\n",
			    "cannot map string to an object identifier");

			return (0);
		}
		break;

	case FN_ID_DCE_UUID:

		x500_debug("CX500DUA::id_to_om_oid: %s\n",
		    "FN_ID_DCE_UUID identifier encountered");

		return (0);

	default:

		x500_debug("CX500DUA::id_to_om_oid: %s\n",
		    "unknown identifier format encountered");

		return (0);
	}

	if (syntax) {
		if (! om_oid_to_syntax(&oid, syntax, class_oid)) {
			*syntax = OM_S_TELETEX_STRING;	// default string syntax

			x500_debug("CX500DUA::id_to_om_oid: %s - %s\n",
			    "unrecognised attribute",
			    "using teletex-string syntax");
		}
	}

	return (1);
}


/*
 * insert an attribute identifier into XDS selection object
 */
OM_private_object
CX500DUA::id_to_xds_selection(
	const FN_identifier	&id
) const
{
	OM_private_object	pri_sel = 0;
	OM_string		oid;
	OM_descriptor		one_attr_type[2];
	static OM_descriptor	select_one_attr[] = {
		{DS_ALL_ATTRIBUTES, OM_S_BOOLEAN, {{OM_FALSE, NULL}}},
		{DS_INFO_TYPE, OM_S_ENUMERATION, {{DS_TYPES_AND_VALUES, NULL}}},
		OM_NULL_DESCRIPTOR
	};


	if (! id_to_om_oid(id, oid, 0, 0))
		return (0);

	fill_om_desc(one_attr_type[0], DS_ATTRIBUTES_SELECTED, oid);

	// add a NULL descriptor
	fill_om_desc(one_attr_type[1]);

	// convert to private object
	if (om_create(DS_C_ENTRY_INFO_SELECTION, OM_FALSE, workspace,
	    &pri_sel) == OM_SUCCESS) {
		om_put(pri_sel, OM_REPLACE_ALL, select_one_attr, 0, 0, 0);
		// TBD: om_put() is broken (must use OM_INSERT_AT_END)
		om_put(pri_sel, OM_INSERT_AT_END, one_attr_type, 0, 0, 0);
	}

	delete [] oid.elements;
	return (pri_sel);
}


/*
 * insert one or more attribute identifiers into XDS selection object
 */
OM_private_object
CX500DUA::ids_to_xds_selection(
	const FN_attrset	*ids
) const
{
	// TBD: remove when XDS supports DS_SELECT_ALL_TYPES_AND_VALUES
	static OM_descriptor	select_all[] = {
		OM_OID_DESC(OM_CLASS, DS_C_ENTRY_INFO_SELECTION),
		{DS_ALL_ATTRIBUTES, OM_S_BOOLEAN, {{OM_TRUE, NULL}}},
		{DS_INFO_TYPE, OM_S_ENUMERATION, {{DS_TYPES_AND_VALUES, NULL}}},
		OM_NULL_DESCRIPTOR
	};
	// TBD: remove when XDS supports DS_SELECT_NO_ATTRIBUTES
	static OM_descriptor	select_none[] = {
		OM_OID_DESC(OM_CLASS, DS_C_ENTRY_INFO_SELECTION),
		{DS_ALL_ATTRIBUTES, OM_S_BOOLEAN, {{OM_FALSE, NULL}}},
		{DS_INFO_TYPE, OM_S_ENUMERATION, {{DS_TYPES_AND_VALUES, NULL}}},
		OM_NULL_DESCRIPTOR
	};

	if (! ids)
		return (select_all);

	int	id_num = ids->count();

	if (! id_num)
		return (select_none);

	OM_private_object	pri_sel = 0;
	OM_descriptor		*attr_types;
	OM_descriptor		*attr_type;
	OM_string		oid;
	static OM_descriptor	select_attrs[] = {
		OM_OID_DESC(OM_CLASS, DS_C_ENTRY_INFO_SELECTION),
		{DS_ALL_ATTRIBUTES, OM_S_BOOLEAN, {{OM_FALSE, NULL}}},
		{DS_INFO_TYPE, OM_S_ENUMERATION, {{DS_TYPES_AND_VALUES, NULL}}},
		OM_NULL_DESCRIPTOR
	};
	const FN_identifier	*id;
	void			*iter;
	int			i;

	attr_types = new OM_descriptor [id_num + 1];
	attr_type = attr_types;

	for (id = ids->first(iter)->identifier();
	    id_num > 0;
	    id = ids->next(iter)->identifier(), attr_type++, id_num--) {

		if (! id_to_om_oid(*id, oid, 0, 0)) {

			// delete oid(s)
			for (i = 0; i < id_num; i++) {
				delete [] attr_types[i].value.string.elements;
			}
			delete [] attr_types;
			return (0);
		}

		fill_om_desc(*attr_type, DS_ATTRIBUTES_SELECTED, oid);
	}
	// add a NULL descriptor
	fill_om_desc(*attr_type);

	// convert to private object
	if (om_create(DS_C_ENTRY_INFO_SELECTION, OM_FALSE, workspace,
	    &pri_sel) == OM_SUCCESS) {
		om_put(pri_sel, OM_REPLACE_ALL, select_attrs, 0, 0, 0);
		// TBD: om_put() is broken (must use OM_INSERT_AT_END)
		om_put(pri_sel, OM_INSERT_AT_END, attr_types, 0, 0, 0);
	}

	// delete oid(s)
	for (i = 0; i < id_num; i++) {
		delete [] attr_types[i].value.string.elements;
	}
	delete [] attr_types;
	return (pri_sel);
}


/*
 * Convert an XFN reference into an octet string (DS_A_NNS_REF_STRING).
 * Fill the syntax and value components of the supplied OM_descriptor.
 *
 * <id-tag> '$' <ref-id> { '$' <id-tag> '$' <addr-id> '$' <addr> }+
 *
 * where: { x }+ denotes one or more occurrences of x
 *        <id-tag> is one of 'id', 'oid', 'uuid' or 'ber'
 *        <ref-id> is a reference type
 *        <addr-id> is a reference address type
 *        <addr> is an address (hexadecimal string encoding)
 *
 * e.g.   "id$xxxx$id$yyyy$7a7a7a7a"
 */
int
CX500DUA::ref_to_xds_attr_value(
	const FN_ref	*ref,
	OM_object	attr_value
) const
{
	if (! ref)
		return (0);

	if (xfn_pkg) {

		// TBD: build DS_C_REF from ref
		attr_value->syntax = OM_S_OBJECT;

		x500_debug("CX500DUA::ref_to_xds_attr_value: %s\n",
		    "DS_C_REF not supported");

		return (0);

	} else {	// workaround

		x500_debug("CX500DUA::ref_to_xds_attr_value: %s\n",
		    "DS_A_NNS_REF_STRING supported");

		// build octet string from ref

		attr_value->syntax = OM_S_OCTET_STRING;

		const FN_identifier	*id = 0;
		unsigned char		*ref_string =
					    new unsigned char [max_ref_length];
		unsigned char		*cp = ref_string;
		const unsigned char	*cp2;
		void			*iter_pos;
		int			len;

		// copy reference type
		if (! (id = ref->type())) {
			delete [] ref_string;
			return (0);
		}
		if (! (cp = id_format_to_string(id->format(), cp))) {
			delete [] ref_string;
			return (0);
		}
		cp2 = id->str();
		strcpy((char *)cp, (char *)ref->type()->str());
		cp += strlen((char *)cp2);

		int			addr_num = ref->addrcount();
		const FN_ref_addr	*ref_addr = ref->first(iter_pos);

		x500_debug("CX500DUA::ref_to_xds_attr_value: %s %s %s %d\n",
		    "ref-type", ref->type()->str(), "ref-addresses:", addr_num);

		while (addr_num--) {

			x500_debug("CX500DUA::ref_to_xds_attr_value: %s %s\n",
			    "ref-addr-type:", ref_addr->type()->str());

			*cp++ = '$';
			id = 0;

			// copy address type
			if (! (id = ref_addr->type())) {
				delete [] ref_string;
				return (0);
			}
			if (! (cp = id_format_to_string(id->format(), cp))) {
				delete [] ref_string;
				return (0);
			}
			cp2 = id->str();
			strcpy((char *)cp, (char *)cp2);
			cp += strlen((char *)cp2);

			*cp++ = '$';

			// copy address data (encode hex string)

			int 		i;
			unsigned char	*addr =
					    (unsigned char *)ref_addr->data();

			len = ref_addr->length();
			for (i = 0; i < len; i++) {
				cp += sprintf((char *)cp, "%.2x", *addr++);
			}
			ref_addr = ref->next(iter_pos);
		}
		attr_value->value.string.length = cp - ref_string;
		attr_value->value.string.elements = ref_string;

		x500_debug("CX500DUA::ref_to_xds_attr_value: ref (%d): %s\n",
		    cp - ref_string, ref_string);
	}

	return (1);
}


/*
 * Convert an octet string (DS_A_NNS_REF_STRING) into an XFN reference.
 *
 * <id-tag> '$' <ref-id> { '$' <id-tag> '$' <addr-id> '$' <addr> }+
 *
 * where: { x }+ denotes one or more occurrences of x
 *        <id-tag> is one of 'id', 'oid', 'uuid' or 'ber'
 *        <ref-id> is a reference type
 *        <addr-id> is a reference address type
 *        <addr> is an address (hexadecimal string encoding)
 *
 * e.g.   "id$xxxx$id$yyyy$7a7a7a7a"
 */
FN_ref *
CX500DUA::xds_attr_value_to_ref(
	OM_object	attr_value
) const
{
	FN_ref	*ref;

	if (xfn_pkg) {

		// TBD: build reference from DS_C_REF
		attr_value->syntax = OM_S_OBJECT;
		if (attr_value->syntax != OM_S_OBJECT)
			return (0);

		x500_debug("CX500DUA::xds_attr_value_to_ref: %s\n",
		    "DS_C_REF not supported");

		return (0);

	} else {	// workaround

		x500_debug("CX500DUA::xds_attr_value_to_ref: %s\n",
		    "DS_A_NNS_REF_STRING supported");

		// build reference from octet string
		if (attr_value->syntax != OM_S_OCTET_STRING)
			return (0);

		int		len = (int)attr_value->value.string.length;
		unsigned char	*ref_string = (unsigned char *)
				    memcpy(new unsigned char [len + 1],
				    attr_value->value.string.elements,
				    (size_t)len);
		unsigned char	*cp = ref_string;
		unsigned char	*cp2;
		unsigned int	format;

		ref_string[len] = '\0';	// add terminator

		// build ref-type

		if (! (cp = string_to_id_format(cp, format))) {
			delete [] ref_string;
			return (0);
		}
		cp2 = cp;
		while (*cp && (*cp != '$')) {
			cp++;
		}
		FN_identifier	ref_id(format, cp - cp2, cp2);
		ref = new FN_ref(ref_id);

		// build ref-addr-type

		while (*cp) {
			if (*cp == '$')
				cp++;
			if (! (cp = string_to_id_format(cp, format))) {
				delete [] ref_string;
				delete ref;
				return (0);
			}
			cp2 = cp;
			while (*cp && (*cp != '$')) {
				cp++;
			}
			FN_identifier	addr_id(format, cp - cp2, cp2);
			if (*cp == '$')
				cp++;
			cp2 = cp;
			while (*cp && (*cp != '$')) {
				cp++;
			}

			int		buflen = (cp - cp2) / 2;
			unsigned char	*buf = new unsigned char [buflen];
			unsigned char	*bp = buf;

			// copy address data (decode hex string)

			unsigned char	hex_pair[3];

			hex_pair[2] = '\0';
			cp = cp2;
			while (*cp && (*cp != '$')) {

				hex_pair[0] = *cp++;
				if (*cp) {
					hex_pair[1] = *cp++;
				} else {
					delete [] buf;
					delete [] ref_string;
					delete ref;
					return (0);
				}
				*bp++ = (unsigned char)strtol((char *)hex_pair,
				    (char **)NULL, 16);
			}

			FN_ref_addr	ref_addr(addr_id, buflen, buf);
			ref->append_addr(ref_addr);
			delete [] buf;
		}
		delete [] ref_string;
	}
	return (ref);
}


/*
 * Convert a string to the format specifier in an FN_identifier.
 */
unsigned char *
CX500DUA::string_to_id_format(
	unsigned char	*cp,
	unsigned int	&format
) const
{
	if (strncmp((char *)cp, "id$", 3) == 0) {
		format = FN_ID_STRING;
		return (cp += 3);

	} else if (strncmp((char *)cp, "oid$", 4) == 0) {
		format = FN_ID_ISO_OID_STRING;
		return (cp += 4);

	} else if (strncmp((char *)cp, "uuid$", 5) == 0) {
		format = FN_ID_DCE_UUID;
		return (cp += 5);

	} else if (strncmp((char *)cp, "ber$", 4) == 0) {
		format = FN_ID_ISO_OID_BER;
		return (cp += 4);

	} else
		return (0);
}


/*
 * Convert the format specifier in an FN_identifier to a string.
 */
unsigned char *
CX500DUA::id_format_to_string(
	unsigned int	format,
	unsigned char	*cp
) const
{
	switch (format) {
	case FN_ID_STRING:
		strcpy((char *)cp, "id$");
		return (cp += 3);

	case FN_ID_ISO_OID_STRING:
		strcpy((char *)cp, "oid$");
		return (cp += 4);

	case FN_ID_DCE_UUID:
		strcpy((char *)cp, "uuid$");
		return (cp += 5);

	case FN_ID_ISO_OID_BER:
		strcpy((char *)cp, "ber$");
		return (cp += 4);

	default:
		return (0);
	}
}


/*
 * Convert a string into an XDS attribute value
 */
int
CX500DUA::string_to_xds_attr_value(
	FN_string		*val_str,
	const OM_syntax		syntax,
	OM_object_identifier	*class_oid,
	OM_descriptor		*attr_value
) const
{
	if (! val_str)
		return (0);

	switch (syntax) {

	case OM_S_PRINTABLE_STRING:
	case OM_S_TELETEX_STRING:
	case OM_S_IA5_STRING:
	case OM_S_NUMERIC_STRING:
	case OM_S_VISIBLE_STRING:
		attr_value->value.string.length = val_str->bytecount();
		attr_value->value.string.elements = (void *)val_str->contents();
		return (1);

	case OM_S_OCTET_STRING:
		attr_value->value.string.length = val_str->bytecount();
		attr_value->value.string.elements = (void *)val_str->contents();
		return (1);

	case OM_S_OBJECT_IDENTIFIER_STRING: {
		OM_object_identifier	*oid;

		if (oid = string_to_om_oid((unsigned char *)val_str->str())) {
			attr_value->value.string.length = oid->length;
			attr_value->value.string.elements = oid->elements;
			delete oid;	// delete contents later
			return (1);
		} else if (oid = string_oid_to_om_oid(
		    (const char *)val_str->str())) {
			attr_value->value.string.length = oid->length;
			attr_value->value.string.elements = oid->elements;
			delete oid;	// delete contents later
			return (1);
		} else
			return (0);
	}

	case OM_S_ENUMERATION:
	case OM_S_INTEGER:
	case OM_S_BOOLEAN:
		attr_value->value.enumeration = atoi((char *)val_str->str());
		return (1);

	case OM_S_OBJECT:

		x500_debug("CX500DUA::string_to_xds_attr_value: %s\n",
		    "non-string syntax encountered");

		if (compare_om_oids(DS_C_DS_DN, *class_oid)) {

			if (! (attr_value->value.object.object =
				string_to_xds_dn(*val_str))) {
				return (0);
			}

		} else if (compare_om_oids(DS_C_PRESENTATION_ADDRESS,
		    *class_oid)) {

			if (! (attr_value->value.object.object =
				string_to_xds_paddr(*val_str))) {
				return (0);
			}

		} else if (compare_om_oids(DS_C_POSTAL_ADDRESS, *class_oid)) {

			if (! (attr_value->value.object.object =
				string_to_xds_post_addr(*val_str))) {
				return (0);
			}
		}
		// TBD: handle other non-string syntaxes

		return (1);

	default:
		return (0);
	}
}


/*
 * Release resources associated with an attribute value
 */
void
CX500DUA::delete_xds_attr_value(
	OM_descriptor	*attr_value
) const
{
	switch (attr_value->syntax & OM_S_SYNTAX) {

	case OM_S_PRINTABLE_STRING:
	case OM_S_TELETEX_STRING:
	case OM_S_OCTET_STRING:
	case OM_S_IA5_STRING:
	case OM_S_NUMERIC_STRING:
	case OM_S_VISIBLE_STRING:
		break;	// do nothing - supplied string was not copied

	case OM_S_OBJECT_IDENTIFIER_STRING:
		delete [] attr_value->value.string.elements;
		break;


	case OM_S_ENUMERATION:
	case OM_S_INTEGER:
	case OM_S_BOOLEAN:
		break;

	case OM_S_OBJECT: {
		OM_object		obj = attr_value->value.object.object;

		if (attr_value->syntax & OM_S_PRIVATE) {
			om_delete(obj);
			break;
		}
		OM_object_identifier	*class_oid = &obj->value.string;
		OM_descriptor		*dp = obj;

		if (compare_om_oids(DS_C_DS_DN, *class_oid)) {

			break;	// always private

		} else if (compare_om_oids(DS_C_PRESENTATION_ADDRESS,
		    *class_oid)) {

			dp++; 	// skip OM_CLASS
			while (dp->type != OM_NO_MORE_TYPES) {
				delete [] dp->value.string.elements;
				dp++;
			}

		} else if (compare_om_oids(DS_C_POSTAL_ADDRESS, *class_oid)) {

			dp++; 	// skip OM_CLASS
			while (dp->type != OM_NO_MORE_TYPES) {
				delete [] dp->value.string.elements;
				dp++;
			}
		}
		// TBD: handle other non-string syntaxes

		break;
	}

	default:
		break;
	}
}


/*
 * Convert an XDS attribute value into OM string format
 */
unsigned char *
CX500DUA::xds_attr_value_to_string(
	OM_descriptor	*attr_value,
	unsigned int	&format,
	unsigned int	&length
) const
{
	OM_string	*val = &attr_value->value.string;
	unsigned char	*str;

	switch (attr_value->syntax & OM_S_SYNTAX) {

	case OM_S_PRINTABLE_STRING:
	case OM_S_TELETEX_STRING:
	case OM_S_IA5_STRING:
	case OM_S_NUMERIC_STRING:
	case OM_S_VISIBLE_STRING:
		str = (unsigned char *)
		    memcpy(new unsigned char [val->length + 1],
		    val->elements, (size_t)val->length);

		str[val->length] = '\0';
		length = (unsigned int) val->length + 1;
		format = FN_ID_STRING;
		return (str);

	case OM_S_OBJECT_IDENTIFIER_STRING:
		if (str = om_oid_to_string(val)) {
			format = FN_ID_STRING;
		} else {
			str = om_oid_to_string_oid(val);
			format = FN_ID_ISO_OID_STRING;
		}
		length = strlen((char *)str);
		return (str);

	case OM_S_OBJECT: {
		x500_debug("CX500DUA::xds_attr_value_to_string: %s\n",
		    "non-string syntax encountered");

		OM_object		value = attr_value->value.object.object;
		OM_object_identifier	*class_oid = &value->value.string;
		int			len;

		if (compare_om_oids(DS_C_DS_DN, *class_oid)) {

			if (! (str = xds_dn_to_string(value, 1))) {
				return (0);
			}

		} else if (compare_om_oids(DS_C_PRESENTATION_ADDRESS,
		    *class_oid)) {

			if (! (str = xds_paddr_to_string(value, len))) {
				return (0);
			}

		} else if (compare_om_oids(DS_C_POSTAL_ADDRESS, *class_oid)) {

			if (! (str = xds_post_addr_to_string(value, len))) {
				return (0);
			}

		} else {

			// TBD: handle other non-string syntaxes

			return (0);
		}
		length = strlen((char *)str);
		format = FN_ID_STRING;
		return (str);
	}

	case OM_S_OCTET_STRING:

		x500_debug("CX500DUA::xds_attr_value_to_string: %s\n",
		    "octet-string syntax encountered");

		str = (unsigned char *)
		    memcpy(new unsigned char [val->length + 1],
		    val->elements, (size_t)val->length);

		length = (unsigned int) val->length;
		format = FN_ID_STRING + 99;	// must not be FN_ID_STRING
		return (str);

	case OM_S_ENUMERATION:
	case OM_S_INTEGER:
	case OM_S_BOOLEAN: {

		unsigned char	*num = new unsigned char[32];

		sprintf((char *)num, "%d", attr_value->value.enumeration);
		length = strlen((char *)num);
		format = FN_ID_STRING;
		return (num);
	}

	default:
		return (0);
	}
}


/*
 * Convert a distinguished name (in the string format specified in
 * X/Open DCE Directory) into XDS format (class DS_C_DS_DN)
 */
OM_private_object
CX500DUA::string_to_xds_dn(
	const FN_string	&dn
) const
{
	unsigned char	*dn_string;
	unsigned char	*cp;
	int		rdn_num = 0;
	int		ava_num = 0;
	int		mul_ava_num = 0;
	OM_descriptor	*name;
	int		idn;
	int		irdn;
	int		iava;
	int		multiple_ava;
	int		is_root = 0;


	// make a copy of the supplied name string
	int	len = dn.bytecount();

	cp = dn_string = (unsigned char *)memcpy(new unsigned char [len + 1],
	    dn.str(), (size_t)len + 1);

	// count number of AVAs and number of multiple AVAs
	while (*cp) {
		if (*cp == '=')		// equal-sign separates RDNs
			ava_num++;
		if (*cp == ',')		// comma separates multiple AVAs
			mul_ava_num++;
		cp++;
	}

	if (ava_num < mul_ava_num) {

		x500_debug("%s: %d AVAs, (incl. %d multiple AVAs)\n",
		    "CX500DUA::string_to_xds_dn", ava_num, mul_ava_num);

		delete [] dn_string;
		return (0);
	}


	rdn_num = ava_num - mul_ava_num;
	idn = 0;					// start of DN object
	irdn = rdn_num + 2;				// start of RDN objects
	iava = rdn_num * 3 + mul_ava_num + irdn;	// start of AVA objects
	name = new OM_descriptor [ava_num * 4 + iava];
	int	total = ava_num *4 + iava;

	x500_debug("CX500DUA::string_to_xds_dn: %d RDNs, %d AVAs\n", rdn_num,
	    ava_num);

	cp = dn_string;	// reset to start of name

	while (*cp == '.')	// skip over '...', if present
		cp++;
	if (*cp == '/') {	// skip over leading slash, if present
		cp++;
		if (! *cp)
			is_root = 1;	// ROOT is denoted by a single slash
	}

	while (*cp || is_root) {

		// build a DS_C_DS_DN object

		fill_om_desc(name[idn++], OM_CLASS, DS_C_DS_DN);

		while (*cp) {

			fill_om_desc(name[idn++], DS_RDNS, &name[irdn]);

			// build a DS_C_DS_RDN object

			fill_om_desc(name[irdn++], OM_CLASS, DS_C_DS_RDN);

			multiple_ava = 1;
			while (*cp && multiple_ava) {

				fill_om_desc(name[irdn++], DS_AVAS,
				    &name[iava]);

				// build a DS_C_AVA object

				fill_om_desc(name[iava++], OM_CLASS, DS_C_AVA);

				unsigned char	*cp2 = (unsigned char *)strchr
						    ((const char *)cp, '=');

				if (! cp2) {
					delete [] dn_string;
					delete [] name;
					return (0);
				}
				*cp2 = '\0';

				OM_syntax		s;
				OM_object_identifier	*oid = abbrev_to_om_oid(
							    (const char *)cp,
							    s);

				if (! oid) {
					delete [] dn_string;
					delete [] name;
					return (0);
				}
				fill_om_desc(name[iava++], DS_ATTRIBUTE_TYPE,
				    *oid);

				delete oid;	// delete contents later

				cp = ++cp2;
				while (*cp2 && (*cp2 != ',') && (*cp2 != '/'))
					cp2++;

				if (*cp2 != ',')
					multiple_ava = 0;
				if (*cp2)
					*cp2 = '\0';
				else
					cp2--;

				fill_om_desc(name[iava++], DS_ATTRIBUTE_VALUES,
				    s, (void *)cp, OM_LENGTH_UNSPECIFIED);

				cp = cp2 + 1;

				fill_om_desc(name[iava++]);
			}
			fill_om_desc(name[irdn++]);
		}
		fill_om_desc(name[idn]);
		is_root = 0;
	}

	OM_private_object	pri_dn = 0;

	// convert to private object
	if (om_create(DS_C_DS_DN, OM_FALSE, workspace, &pri_dn) == OM_SUCCESS) {

		if (om_put(pri_dn, OM_REPLACE_ALL, name, 0, 0, 0) !=
		    OM_SUCCESS) {
			om_delete(pri_dn);
		}
	}
	// cleanup attribute types
	int	i;
	for (i = 0; i < total; i++)
		if (name[i].type == DS_ATTRIBUTE_TYPE)
			delete [] name[i].value.string.elements;

	delete [] dn_string;
	delete [] name;

	return (pri_dn);
}


/*
 * Convert a (relative) distinguished name in XDS format (class DS_C_DS_RDN
 * or DS_C_DS_DN) into the string format specified in X/Open DCE Directory
 */
unsigned char *
CX500DUA::xds_dn_to_string(
	OM_object	dn,
	int		is_dn	// dn or rdn
) const
{
	unsigned char		*dn_string = new unsigned char [max_dn_length];
	unsigned char		*cp = dn_string;
	OM_public_object	pub_dn = 0;
	OM_value_position	total;
	OM_descriptor		*dnd;
	OM_descriptor		*rdnd;
	OM_descriptor		*avad;
	OM_descriptor		*avad2;
	int			multiple_ava;

	// convert to public
	if (dn->type == OM_PRIVATE_OBJECT) {
		if (om_get(dn, OM_NO_EXCLUSIONS, 0, OM_FALSE, 0, 0, &pub_dn,
		    &total) != OM_SUCCESS) {
			delete [] dn_string;
			return (0);
		}
		dn = pub_dn;
	}
	rdnd = dnd = dn;

	dnd++;	// skip OM_CLASS
	while (dnd->type != OM_NO_MORE_TYPES) {

		if (dnd->type == DS_RDNS)
			rdnd = dnd->value.object.object;

		rdnd++;	// skip OM_CLASS
		multiple_ava = 0;
		while (rdnd->type != OM_NO_MORE_TYPES) {

			if (multiple_ava)
				*cp++ = ',';	// comma separates multiple AVAs
			else
				if (is_dn)
					*cp++ = '/';	// slash separates RDNs

			if (rdnd->type == DS_AVAS)
				avad = rdnd->value.object.object;

			avad++;	// skip OM_CLASS
			if (avad->type != OM_NO_MORE_TYPES) {

				avad2 = avad;
				while ((avad->type != DS_ATTRIBUTE_TYPE) &&
				    (avad->type != OM_NO_MORE_TYPES))
					avad++;

				if (avad->type == DS_ATTRIBUTE_TYPE) {
					cp = om_oid_to_abbrev(
					    avad->value.string, cp);
				}

				avad = avad2;
				while ((avad->type != DS_ATTRIBUTE_VALUES) &&
					(avad->type != OM_NO_MORE_TYPES))
					avad++;

				if (avad->type == DS_ATTRIBUTE_VALUES) {

					switch (avad->syntax & OM_S_SYNTAX) {
					case OM_S_PRINTABLE_STRING:
					case OM_S_TELETEX_STRING:
					case OM_S_IA5_STRING:
					case OM_S_NUMERIC_STRING:
					case OM_S_VISIBLE_STRING:

						memcpy(cp,
						    avad->value.string.elements,
						    (size_t)
						    avad->value.string.length);
						cp = cp +
						avad->value.string.length;
						break;

					default:
						*cp++ = '?';
						break;
					}
				}
			}
			rdnd++;
			multiple_ava = 1;
		}
		dnd++;
	}
	*cp++ = '\0';

	if (pub_dn)
		om_delete(pub_dn);

	int	len = cp - dn_string;

	cp = dn_string;
	cp = (unsigned char *)memcpy(new unsigned char [len], dn_string,
	    (size_t)len);

	x500_debug("CX500DUA::xds_dn_to_string: %s\n", cp);

	delete [] dn_string;
	return (cp);
}


/*
 * Convert a presentation address (in the string format specified in
 * RFC-1278) into XDS format (class DS_C_PRESENTATION_ADDRESS).
 *
 * [[[ <p-sel> '/' ] <s-sel> '/' ] <t-sel> '/' ] { <n-addr> }+
 *
 * where: [ x ] denotes optional x
 *        { x }+ denotes one or more occurrences of x
 *        <p-sel> is a presentation selector in hex format
 *        <s-sel> is a session selector in hex format
 *        <t-sel> is a transport selector in hex format
 *        <n-addr> is a network address in hex format. Each address has a
 *                 prefix of 'NS+' and multiple addresses are is linked by
 *                 an '_'.
 *
 * e.g. "ses"/NS+b1b2b3  or  'a1a2a3'H/NS+b1b2b3_NS+c1c2c3"
 */
OM_public_object
CX500DUA::string_to_xds_paddr(
	const FN_string	&paddr
) const
{
	unsigned char	*paddr_string = (unsigned char *)paddr.str();
	unsigned char	*cp = paddr_string;
	int		sel_num = 0;
	int		na_num = 0;

	while (*cp) {
		if (*cp == '/')
			sel_num++;
		else if (*cp == '+')
			na_num++;
		cp++;
	}

	x500_debug("CX500DUA::string_to_xds_paddr: %d sel's, %d n-addr's\n",
	    sel_num, na_num);

	if ((na_num == 0) || (sel_num > 3)) {
		return (0);
	}

	OM_descriptor	*pa = new OM_descriptor [sel_num + na_num + 2];
	int		ipa = 0;	// index
	unsigned char	*buf = new unsigned char [max_naddr_length];
					// tmp buffer for selector or n-address
	OM_string	*sel;

	cp = paddr_string;	// reset

	while (*cp) {

		// build a DS_C_PRESENTATION_ADDRESS object

		fill_om_desc(pa[ipa++], OM_CLASS, DS_C_PRESENTATION_ADDRESS);

		switch (sel_num) {

		case 3:
			fill_om_desc(pa[ipa], DS_P_SELECTOR,
			    OM_S_OCTET_STRING, (void *)0, 0);
			sel = &pa[ipa++].value.string;
			sel->elements = buf;

			if (! (cp = string_to_xds_paddr_selector(cp, sel))) {
				delete [] pa;
				delete [] buf;
				return (0);
			}
			sel->elements = memcpy(new unsigned char [sel->length],
			    sel->elements, (size_t)sel->length);

			// FALL-THROUGH

		case 2:
			fill_om_desc(pa[ipa], DS_S_SELECTOR,
			    OM_S_OCTET_STRING, (void *)0, 0);
			sel = &pa[ipa++].value.string;
			sel->elements = buf;

			if (! (cp = string_to_xds_paddr_selector(cp, sel))) {
				delete [] pa;
				delete [] buf;
				return (0);
			}
			sel->elements = memcpy(new unsigned char [sel->length],
			    sel->elements, (size_t)sel->length);

			// FALL-THROUGH

		case 1:
			fill_om_desc(pa[ipa], DS_T_SELECTOR,
			    OM_S_OCTET_STRING, (void *)0, 0);
			sel = &pa[ipa++].value.string;
			sel->elements = buf;

			if (! (cp = string_to_xds_paddr_selector(cp, sel))) {
				delete [] pa;
				delete [] buf;
				return (0);
			}
			sel->elements = memcpy(new unsigned char [sel->length],
			    sel->elements, (size_t)sel->length);

			break;

		default:
			delete [] pa;
			delete [] buf;
			return (0);
		}

		int		i;
		unsigned char	*bp;

		for (i = 0; i < na_num; i++) {

			bp = buf;	// reset

			fill_om_desc(pa[ipa], DS_N_ADDRESSES,
			    OM_S_OCTET_STRING, (void *)0, 0);
			sel = &pa[ipa++].value.string;
			sel->elements = buf;

			if ((cp[0] == 'N') && (cp[1] == 'S') && (cp[2] == '+'))
				cp += 3;	// skip "NS+" prefix
			else {
				delete [] pa;
				delete [] buf;
				return (0);
			}

			unsigned char	hex_string[3];

			hex_string[2] = '\0';
			while (*cp && (*cp != '_')) {

				hex_string[0] = *cp++;
				if (*cp) {
					hex_string[1] = *cp++;
				} else {
					delete [] pa;
					delete [] buf;
					return (0);
				}
				*bp++ = (unsigned char)strtol(
				    (char *)hex_string, (char **)0, 16);
			}
			if (i > 0) {
				if (*cp++ != '_') {
					delete [] pa;
					delete [] buf;
					return (0);	// no separator
				}
			}
			sel->length = bp - buf;
			sel->elements = memcpy(new unsigned char [sel->length],
			    sel->elements, (size_t)sel->length);
		}
	}
	fill_om_desc(pa[ipa]);
	delete [] buf;

	return (pa);
}


/*
 * Convert a presentation address selector into its XDS encoding
 *
 * (e.g. encoding 'a1a2a3'H produces "\xa1\xa2\xa3"
 *       encoding "DSA"     produces "\x44\x53\x41")
 */
unsigned char *
CX500DUA::string_to_xds_paddr_selector(
	unsigned char	*string,
	OM_string	*selector	// supplied buffer
) const
{
	unsigned char	*cp = string;
	unsigned char	*bp = (unsigned char *)selector->elements;

	if (*cp++ == '"') {	// IA5

		while (*cp && (*cp != '"'))
			*bp++ = *cp++;

		if (*cp == '"')
			cp++;
		else
			return (0);	// no closing quote

		if (*cp++ != '/')
			return (0);	// no terminating slash

	} else if (*cp++ == '\'') {

		unsigned char	hex_string[3];

		hex_string[2] = '\0';
		while (*cp && (*cp != '\'')) {

			hex_string[0] = *cp++;
			if (*cp) {
				hex_string[1] = *cp++;
			} else {
				return (0);
			}
			*bp++ = (unsigned char)strtol((char *)hex_string,
			    (char **)NULL, 16);
		}

		if (*cp++ != '\'')
			return (0);	// no closing quote

		if (*cp++ != 'H')
			return (0);

		if (*cp++ != '/')
			return (0);	// no terminating slash

	} else
		return (0);

	selector->length = bp - (unsigned char *)selector->elements;
	return (cp);
}


/*
 * Convert a presentation address (in XDS format) into the string format
 * specified in RFC-1278 and set its length.
 *
 * [[[ <p-sel> '/' ] <s-sel> '/' ] <t-sel> '/' ] { <n-addr> }+
 *
 * where: [ x ] denotes optional x
 *        { x }+ denotes one or more occurrences of x
 *        <p-sel> is a presentation selector in hex format
 *        <s-sel> is a session selector in hex format
 *        <t-sel> is a transport selector in hex format
 *        <n-addr> is a network address in hex format. Each address has a
 *                 prefix of 'NS+' and multiple addresses are is linked by
 *                 an '_'.
 *
 * e.g. "ses"/NS+b1b2b3  or  'a1a2a3'H/NS+b1b2b3_NS+c1c2c3"
 */
unsigned char *
CX500DUA::xds_paddr_to_string(
	OM_object	paddr,
	int		&len
) const
{
	unsigned char		paddr_string[max_paddr_length];
	unsigned char		*cp = paddr_string;
	OM_public_object	pub_paddr = 0;
	OM_value_position	total;
	OM_object		pa = 0;

	if (! paddr)
		return (0);

	// convert to public
	if (paddr->type == OM_PRIVATE_OBJECT) {
		if (om_get(paddr, OM_NO_EXCLUSIONS, 0, OM_FALSE, 0, 0,
		    &pub_paddr, &total) != OM_SUCCESS) {
			return (0);
		}
		pa = pub_paddr;
	} else
		pa = paddr;

	OM_object	pa2 = pa;

	// locate presentation-selector (if present)
	while ((pa->type != OM_NO_MORE_TYPES) && (pa->type != DS_P_SELECTOR))
		pa++;

	if (pa->type == DS_P_SELECTOR) {
		cp = xds_paddr_selector_to_string(&pa->value.string, cp);
		*cp++ = '/';
	}
	pa = pa2;	// reset

	// locate session-selector (if present)
	while ((pa->type != OM_NO_MORE_TYPES) && (pa->type != DS_S_SELECTOR))
		pa++;

	if (pa->type == DS_S_SELECTOR) {
		cp = xds_paddr_selector_to_string(&pa->value.string, cp);
		*cp++ = '/';
	}
	pa = pa2;	// reset

	// locate transport-selector (if present)
	while ((pa->type != OM_NO_MORE_TYPES) && (pa->type != DS_T_SELECTOR))
		pa++;

	if (pa->type == DS_T_SELECTOR) {
		cp = xds_paddr_selector_to_string(&pa->value.string, cp);
		*cp++ = '/';
	}
	pa = pa2;	// reset

	// locate network-address(es)
	while ((pa->type != OM_NO_MORE_TYPES) && (pa->type != DS_N_ADDRESSES))
		pa++;

	while (pa->type == DS_N_ADDRESSES) {
		int	i;
		int	j = (int)pa->value.string.length;
		unsigned char	*addr =
				    (unsigned char *)pa->value.string.elements;

		*cp++ = 'N';
		*cp++ = 'S';
		*cp++ = '+';

		for (i = 0; i < j; i++) {
			cp += sprintf((char *)cp, "%.2x", *addr++);
		}
		*cp++ = '_';
		pa++;
	}
	*--cp = '\0';	// remove trailing underscore

	len = cp - paddr_string + 1;

	x500_debug("CX500DUA::xds_paddr_to_string: length=%d\n", len);

	return ((unsigned char *)memcpy(new unsigned char [len], paddr_string,
	    (size_t)len));
}


/*
 * Convert a presentation address selector into its RFC-1278 string encoding
 *
 * (e.g. decoding "\xa1\xa2\xa3" produces 'a1a2a3'H
 *       decoding "\x44\x53\x41" produces "DSA")
 */
unsigned char *
CX500DUA::xds_paddr_selector_to_string(
	OM_string	*selector,
	unsigned char	*string		// supplied buffer
) const
{
	int		len = (int)selector->length;
	unsigned char	*sel = (unsigned char *)selector->elements;
	unsigned char	*cp = string;
	int	i;

	// assume IA5
	*cp++ = '"';
	for (i = 0; i < len; i++) {

		if ((isalnum(*sel)) || (*sel == '+') || (*sel == '-') ||
		    (*sel == '.')) {

			*cp++ = *sel++;
		} else
			break;	// not IA5
	}
	*cp++ = '"';
	if (i != len) {
		sel = (unsigned char *)selector->elements;	// reset
		cp = string;	// reset

		*cp++ = '\'';
		for (i = 0; i < len; i++) {
			cp += sprintf((char *)cp, "%.2x", *sel++);
		}
		*cp++ = '\'';
		*cp++ = 'H';
	}
	return (cp);
}

/*
 * Convert a postal address (in the string format specified in RFC-1488)
 * into XDS format (DS_C_POSTAL_ADDRESS).
 *
 * <line> [ '$' <line> ]
 *
 * where: [ x ] denotes optional x
 *        <line> is a line of a postal address (may have upto 6 lines)
 *
 * e.g. "2550 Garcia Ave $ Mountain View $ CA 94043-1100"
 */
OM_public_object
CX500DUA::string_to_xds_post_addr(
	const FN_string	&post
) const
{
	unsigned char	*post_string = (unsigned char *)post.str();
	unsigned char	*cp = post_string;
	int		line_num = 1;

	while (*cp) {
		if (*cp == '$')
			line_num++;
		cp++;
	}

	x500_debug("CX500DUA::string_to_xds_post_addr: %d lines\n", line_num);

	if ((line_num > 6) ||
	    (cp - post_string > ((line_num * 30) + line_num))) {
		return (0);
	}

	OM_descriptor	*po = new OM_descriptor [line_num + 2];
	int		ipo = 0;	// index
	int		len = 0;
	OM_string	*line;

	cp = post_string;	// reset

	while (*cp) {

		// build a DS_C_POSTAL_ADDRESS object

		fill_om_desc(po[ipo++], OM_CLASS, DS_C_POSTAL_ADDRESS);

		while (line_num--) {

			fill_om_desc(po[ipo], DS_POSTAL_ADDRESS,
			    OM_S_TELETEX_STRING, (void *)0, 0);
			line = &po[ipo++].value.string;

			unsigned char	*buf = new unsigned char [32];
			unsigned char	*bp = buf;

			while (*cp && (*cp != '$'))
				*bp++ = *cp++;

			if (*cp == '$')
				cp++;

			line->elements = buf;
			line->length = bp - buf;
		}
		fill_om_desc(po[ipo]);
	}
	return (po);
}


/*
 * Convert a postal address (in XDS format) into the string format
 * specified in RFC-1488 and set its length.
 *
 * <line> [ '$' <line> ]
 *
 * where: [ x ] denotes optional x
 *        <line> is a line of a postal address (may have upto 6 lines)
 *
 * e.g. "2550 Garcia Ave $ Mountain View $ CA 94043-1100"
 */
unsigned char *
CX500DUA::xds_post_addr_to_string(
	OM_object	post,
	int		&len
) const
{
	unsigned char		post_string[max_post_length];
	unsigned char		*cp = post_string;
	OM_public_object	pub_post = 0;
	OM_value_position	total;
	OM_object		po = 0;

	if (! post)
		return (0);

	// convert to public
	if (post->type == OM_PRIVATE_OBJECT) {
		if (om_get(post, OM_NO_EXCLUSIONS, 0, OM_FALSE, 0, 0,
		    &pub_post, &total) != OM_SUCCESS) {
			return (0);
		}
		po = pub_post;
	} else
		po = post;

	po++;	// skip OM_CLASS
	while (po->type == DS_POSTAL_ADDRESS) {
		cp = (unsigned char *)memcpy(cp, po->value.string.elements,
		    (size_t)po->value.string.length);
		cp += po->value.string.length;
		*cp++ = '$';
		po++;
	}
	*--cp = '\0';	// remove trailing dollar

	len = cp - post_string + 1;

	x500_debug("CX500DUA::xds_post_addr_to_string: length=%d\n", len);

	if (pub_post)
		om_delete(pub_post);

	return ((unsigned char *)memcpy(new unsigned char [len], post_string,
	    (size_t)len));
}


/*
 * Extract the innermost component(s) from a multi-level XOM object.
 * The path through the object is specified by the 'route' argument.
 */
OM_return_code
CX500DUA::deep_om_get(
	OM_type_list		route,
	const OM_private_object	original,
	const OM_exclusions	exclusions,
	const OM_type_list	included_types,
	const OM_boolean	local_strings,
	const OM_value_position	initial_value,
	const OM_value_position	limiting_value,
	OM_public_object	*copy,
	OM_value_position	*total_number
)
{
	OM_type			tl[] = {0, 0};
	OM_return_code		rc;
	OM_private_object	obj_in = original;
	OM_exclusions		excl = OM_EXCLUDE_ALL_BUT_THESE_TYPES +
				    OM_EXCLUDE_SUBOBJECTS;
	OM_public_object	obj_out;
	OM_value_position	num;

	while (tl[0] = *route++) {

		rc = om_get(obj_in, excl, tl, OM_FALSE, 0, 0, &obj_out, &num);

		if (rc != OM_SUCCESS) {
			om_delete(obj_out);
			return (rc);
		}

		if (num)
			obj_in = obj_out->value.object.object;

		om_delete(obj_out);
	}
	return (om_get(obj_in, exclusions, included_types, local_strings,
	    initial_value, limiting_value, copy, total_number));
}
