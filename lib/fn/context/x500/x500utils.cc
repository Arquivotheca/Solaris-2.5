/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)x500utils.cc	1.6	95/03/12 SMI"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>	// vfprintf()
#include "x500utils.hh"

extern "C" {

#include "xds.h"
#include "xdsbdcp.h"
#include "xdsmdup.h"
#include "xdssap.h"
#include "xdsxfnp.h"

}

/* Directory Attributes */

/* xdsbdcp.h */
OM_EXPORT(DS_A_OBJECT_CLASS)		/* 2.5.4.0 */
OM_EXPORT(DS_A_ALIASED_OBJECT_NAME)	/* 2.5.4.1 */
OM_EXPORT(DS_A_KNOWLEDGE_INFO)		/* 2.5.4.2 */
OM_EXPORT(DS_A_COMMON_NAME)		/* 2.5.4.3 */
OM_EXPORT(DS_A_SURNAME)			/* 2.5.4.4 */
OM_EXPORT(DS_A_SERIAL_NBR)		/* 2.5.4.5 */
OM_EXPORT(DS_A_COUNTRY_NAME)		/* 2.5.4.6 */
OM_EXPORT(DS_A_LOCALITY_NAME)		/* 2.5.4.7 */
OM_EXPORT(DS_A_STATE_OR_PROV_NAME)	/* 2.5.4.8 */
OM_EXPORT(DS_A_STREET_ADDRESS)		/* 2.5.4.9 */
OM_EXPORT(DS_A_ORG_NAME)		/* 2.5.4.10 */
OM_EXPORT(DS_A_ORG_UNIT_NAME)		/* 2.5.4.11 */
OM_EXPORT(DS_A_TITLE)			/* 2.5.4.12 */
OM_EXPORT(DS_A_DESCRIPTION)		/* 2.5.4.13 */
OM_EXPORT(DS_A_SEARCH_GUIDE)		/* 2.5.4.14 */
OM_EXPORT(DS_A_BUSINESS_CATEGORY)	/* 2.5.4.15 */
OM_EXPORT(DS_A_POSTAL_ADDRESS)		/* 2.5.4.16 */
OM_EXPORT(DS_A_POSTAL_CODE)		/* 2.5.4.17 */
OM_EXPORT(DS_A_POST_OFFICE_BOX)		/* 2.5.4.18 */
OM_EXPORT(DS_A_PHYS_DELIV_OFF_NAME)	/* 2.5.4.19 */
OM_EXPORT(DS_A_PHONE_NBR)		/* 2.5.4.20 */
OM_EXPORT(DS_A_TELEX_NBR)		/* 2.5.4.21 */
OM_EXPORT(DS_A_TELETEX_TERM_IDENT)	/* 2.5.4.22 */
OM_EXPORT(DS_A_FACSIMILE_PHONE_NBR)	/* 2.5.4.23 */
OM_EXPORT(DS_A_X121_ADDRESS)		/* 2.5.4.24 */
OM_EXPORT(DS_A_INTERNAT_ISDN_NBR)	/* 2.5.4.25 */
OM_EXPORT(DS_A_REGISTERED_ADDRESS)	/* 2.5.4.26 */
OM_EXPORT(DS_A_DEST_INDICATOR)		/* 2.5.4.27 */
OM_EXPORT(DS_A_PREF_DELIV_METHOD)	/* 2.5.4.28 */
OM_EXPORT(DS_A_PRESENTATION_ADDRESS)	/* 2.5.4.29 */
OM_EXPORT(DS_A_SUPPORT_APPLIC_CONTEXT)	/* 2.5.4.30 */
OM_EXPORT(DS_A_MEMBER)			/* 2.5.4.31 */
OM_EXPORT(DS_A_OWNER)			/* 2.5.4.32 */
OM_EXPORT(DS_A_ROLE_OCCUPANT)		/* 2.5.4.33 */
OM_EXPORT(DS_A_SEE_ALSO)		/* 2.5.4.34 */
OM_EXPORT(DS_A_USER_PASSWORD)		/* 2.5.4.35 */

/* xdssap.h */
OM_EXPORT(DS_A_USER_CERT)		/* 2.5.4.36 */
OM_EXPORT(DS_A_CA_CERT)			/* 2.5.4.37 */
OM_EXPORT(DS_A_AUTHORITY_REVOC_LIST)	/* 2.5.4.38 */
OM_EXPORT(DS_A_CERT_REVOC_LIST)		/* 2.5.4.39 */
OM_EXPORT(DS_A_CROSS_CERT_PAIR)		/* 2.5.4.40 */

/* xdsmdup.h */
OM_EXPORT(DS_A_DELIV_CONTENT_LENGTH)	/* 2.6.5.2.0 */
OM_EXPORT(DS_A_DELIV_CONTENT_TYPES)	/* 2.6.5.2.1 */
OM_EXPORT(DS_A_DELIV_EITS)		/* 2.6.5.2.2 */
OM_EXPORT(DS_A_DL_MEMBERS)		/* 2.6.5.2.3 */
OM_EXPORT(DS_A_DL_SUBMIT_PERMS)		/* 2.6.5.2.4 */
OM_EXPORT(DS_A_MESSAGE_STORE)		/* 2.6.5.2.5 */
OM_EXPORT(DS_A_OR_ADDRESSES)		/* 2.6.5.2.6 */
OM_EXPORT(DS_A_PREF_DELIV_METHODS)	/* 2.6.5.2.7 */
OM_EXPORT(DS_A_SUPP_AUTO_ACTIONS)	/* 2.6.5.2.8 */
OM_EXPORT(DS_A_SUPP_CONTENT_TYPES)	/* 2.6.5.2.9 */
OM_EXPORT(DS_A_SUPP_OPT_ATTRIBUTES)	/* 2.6.5.2.10 */

/* xdsxfnp.h */
OM_EXPORT(DS_A_OBJECT_REF_IDENT)	/* 1.2.840.113536.26 */
OM_EXPORT(DS_A_OBJECT_REF_ADDRESSES)	/* 1.2.840.113536.27 */
OM_EXPORT(DS_A_NNS_REF_IDENT)		/* 1.2.840.113536.28 */
OM_EXPORT(DS_A_NNS_REF_ADDRESSES)	/* 1.2.840.113536.29 */
OM_EXPORT(DS_A_OBJECT_REF_STRING)	/* 1.2.840.113536.30 */
OM_EXPORT(DS_A_NNS_REF_STRING)		/* 1.2.840.113536.31 */


/* Directory Object Classes */

/* xdsbdcp.h */
OM_EXPORT(DS_O_TOP)			/* 2.5.6.0 */
OM_EXPORT(DS_O_ALIAS)			/* 2.5.6.1 */
OM_EXPORT(DS_O_COUNTRY)			/* 2.5.6.2 */
OM_EXPORT(DS_O_LOCALITY)		/* 2.5.6.3 */
OM_EXPORT(DS_O_ORG)			/* 2.5.6.4 */
OM_EXPORT(DS_O_ORG_UNIT)		/* 2.5.6.5 */
OM_EXPORT(DS_O_PERSON)			/* 2.5.6.6 */
OM_EXPORT(DS_O_ORG_PERSON)		/* 2.5.6.7 */
OM_EXPORT(DS_O_ORG_ROLE)		/* 2.5.6.8 */
OM_EXPORT(DS_O_GROUP_OF_NAMES)		/* 2.5.6.9 */
OM_EXPORT(DS_O_RESIDENTIAL_PERSON)	/* 2.5.6.10 */
OM_EXPORT(DS_O_APPLIC_PROCESS)		/* 2.5.6.11 */
OM_EXPORT(DS_O_APPLIC_ENTITY)		/* 2.5.6.12 */
OM_EXPORT(DS_O_DSA)			/* 2.5.6.13 */
OM_EXPORT(DS_O_DEVICE)			/* 2.5.6.14 */

/* xdssap.h */
OM_EXPORT(DS_O_STRONG_AUTHENT_USER)	/* 2.5.6.15 */
OM_EXPORT(DS_O_CERT_AUTHORITY)		/* 2.5.6.16 */

/* xdsmdup.h */
OM_EXPORT(DS_O_MHS_DISTRIBUTION_LIST)	/* 2.6.5.1.0 */
OM_EXPORT(DS_O_MHS_MESSAGE_STORE)	/* 2.6.5.1.1 */
OM_EXPORT(DS_O_MHS_MESSAGE_TRANS_AG)	/* 2.6.5.1.2 */
OM_EXPORT(DS_O_MHS_USER)		/* 2.6.5.1.3 */
OM_EXPORT(DS_O_MHS_USER_AG)		/* 2.6.5.1.4 */

/* xdsxfnp.h */
OM_EXPORT(DS_O_XFN)			/* 1.2.840.113536.24 */
OM_EXPORT(DS_O_XFN_SUPPLEMENT)		/* 1.2.840.113536.25 */


/* XOM object classes */

/* xds.h */
OM_EXPORT(DS_C_ABANDON_FAILED)		/* 1.3.12.2.1011.28.0.701 */
OM_EXPORT(DS_C_ACCESS_POINT)		/* 1.3.12.2.1011.28.0.702 */
OM_EXPORT(DS_C_ADDRESS)			/* 1.3.12.2.1011.28.0.703 */
OM_EXPORT(DS_C_ATTRIBUTE)		/* 1.3.12.2.1011.28.0.704 */
OM_EXPORT(DS_C_ATTRIBUTE_ERROR)		/* 1.3.12.2.1011.28.0.705 */
OM_EXPORT(DS_C_ATTRIBUTE_LIST)		/* 1.3.12.2.1011.28.0.706 */
OM_EXPORT(DS_C_ATTRIBUTE_PROBLEM)	/* 1.3.12.2.1011.28.0.707 */
OM_EXPORT(DS_C_AVA)			/* 1.3.12.2.1011.28.0.708 */
OM_EXPORT(DS_C_COMMON_RESULTS)		/* 1.3.12.2.1011.28.0.709 */
OM_EXPORT(DS_C_COMMUNICATIONS_ERROR)	/* 1.3.12.2.1011.28.0.710 */
OM_EXPORT(DS_C_COMPARE_RESULT)		/* 1.3.12.2.1011.28.0.711 */
OM_EXPORT(DS_C_CONTEXT)			/* 1.3.12.2.1011.28.0.712 */
OM_EXPORT(DS_C_CONTINUATION_REF)	/* 1.3.12.2.1011.28.0.713 */
OM_EXPORT(DS_C_DS_DN)			/* 1.3.12.2.1011.28.0.714 */
OM_EXPORT(DS_C_DS_RDN)			/* 1.3.12.2.1011.28.0.715 */
OM_EXPORT(DS_C_ENTRY_INFO)		/* 1.3.12.2.1011.28.0.716 */
OM_EXPORT(DS_C_ENTRY_INFO_SELECTION)	/* 1.3.12.2.1011.28.0.717 */
OM_EXPORT(DS_C_ENTRY_MOD)		/* 1.3.12.2.1011.28.0.718 */
OM_EXPORT(DS_C_ENTRY_MOD_LIST)		/* 1.3.12.2.1011.28.0.719 */
OM_EXPORT(DS_C_ERROR)			/* 1.3.12.2.1011.28.0.720 */
OM_EXPORT(DS_C_EXT)			/* 1.3.12.2.1011.28.0.721 */
OM_EXPORT(DS_C_FILTER)			/* 1.3.12.2.1011.28.0.722 */
OM_EXPORT(DS_C_FILTER_ITEM)		/* 1.3.12.2.1011.28.0.723 */
OM_EXPORT(DS_C_LIBRARY_ERROR)		/* 1.3.12.2.1011.28.0.724 */
OM_EXPORT(DS_C_LIST_INFO)		/* 1.3.12.2.1011.28.0.725 */
OM_EXPORT(DS_C_LIST_INFO_ITEM)		/* 1.3.12.2.1011.28.0.726 */
OM_EXPORT(DS_C_LIST_RESULT)		/* 1.3.12.2.1011.28.0.727 */
OM_EXPORT(DS_C_NAME)			/* 1.3.12.2.1011.28.0.728 */
OM_EXPORT(DS_C_NAME_ERROR)		/* 1.3.12.2.1011.28.0.729 */
OM_EXPORT(DS_C_OPERATION_PROGRESS)	/* 1.3.12.2.1011.28.0.730 */
OM_EXPORT(DS_C_PARTIAL_OUTCOME_QUAL)	/* 1.3.12.2.1011.28.0.731 */
OM_EXPORT(DS_C_PRESENTATION_ADDRESS)	/* 1.3.12.2.1011.28.0.732 */
OM_EXPORT(DS_C_READ_RESULT)		/* 1.3.12.2.1011.28.0.733 */
OM_EXPORT(DS_C_REFERRAL)		/* 1.3.12.2.1011.28.0.734 */
OM_EXPORT(DS_C_RELATIVE_NAME)		/* 1.3.12.2.1011.28.0.735 */
OM_EXPORT(DS_C_SEARCH_INFO)		/* 1.3.12.2.1011.28.0.736 */
OM_EXPORT(DS_C_SEARCH_RESULT)		/* 1.3.12.2.1011.28.0.737 */
OM_EXPORT(DS_C_SECURITY_ERROR)		/* 1.3.12.2.1011.28.0.738 */
OM_EXPORT(DS_C_SERVICE_ERROR)		/* 1.3.12.2.1011.28.0.739 */
OM_EXPORT(DS_C_SESSION)			/* 1.3.12.2.1011.28.0.740 */
OM_EXPORT(DS_C_SYSTEM_ERROR)		/* 1.3.12.2.1011.28.0.741 */
OM_EXPORT(DS_C_UPDATE_ERROR)		/* 1.3.12.2.1011.28.0.742 */

/* xdsbdcp.h */
OM_EXPORT(DS_C_FACSIMILE_PHONE_NBR)	/* 1.3.12.2.1011.28.1.801 */
OM_EXPORT(DS_C_POSTAL_ADDRESS)		/* 1.3.12.2.1011.28.1.802 */
OM_EXPORT(DS_C_SEARCH_CRITERION)	/* 1.3.12.2.1011.28.1.803 */
OM_EXPORT(DS_C_SEARCH_GUIDE)		/* 1.3.12.2.1011.28.1.804 */
OM_EXPORT(DS_C_TELETEX_TERM_IDENT)	/* 1.3.12.2.1011.28.1.805 */
OM_EXPORT(DS_C_TELEX_NBR)		/* 1.3.12.2.1011.28.1.806 */

/* xdssap.h */
OM_EXPORT(DS_C_ALGORITHM_IDENT)		/* 1.3.12.2.1011.28.2.821 */
OM_EXPORT(DS_C_CERT)			/* 1.3.12.2.1011.28.2.822 */
OM_EXPORT(DS_C_CERT_LIST)		/* 1.3.12.2.1011.28.2.823 */
OM_EXPORT(DS_C_CERT_PAIR)		/* 1.3.12.2.1011.28.2.824 */
OM_EXPORT(DS_C_CERT_SUBLIST)		/* 1.3.12.2.1011.28.2.825 */
OM_EXPORT(DS_C_SIGNATURE)		/* 1.3.12.2.1011.28.2.826 */

/* xdsmdup.h */
OM_EXPORT(DS_C_DL_SUBMIT_PERMS)		/* 1.3.12.2.1011.28.3.901 */


char	*xds_problems[] = {
	"",
	"DS_E_ADMIN_LIMIT_EXCEEDED",
	"DS_E_AFFECTS_MULTIPLE_DSAS",
	"DS_E_ALIAS_DEREFERENCING_PROBLEM",
	"DS_E_ALIAS_PROBLEM",
	"DS_E_ATTRIBUTE_OR_VALUE_ALREADY_EXISTS",
	"DS_E_BAD_ARGUMENT",
	"DS_E_BAD_CLASS",
	"DS_E_BAD_CONTEXT",
	"DS_E_BAD_NAME",
	"DS_E_BAD_SESSION",
	"DS_E_BAD_WORKSPACE",
	"DS_E_BUSY",
	"DS_E_CANNOT_ABANDON",
	"DS_E_CHAINING_REQUIRED",
	"DS_E_COMMUNICATIONS_PROBLEM",
	"DS_E_CONSTRAINT_VIOLATION",
	"DS_E_DIT_ERROR",
	"DS_E_ENTRY_ALREADY_EXISTS",
	"DS_E_INAPPROP_AUTHENTICATION",
	"DS_E_INAPPROP_MATCHING",
	"DS_E_INSUFFICIENT_ACCESS_RIGHTS",
	"DS_E_INVALID_ATTRIBUTE_SYNTAX",
	"DS_E_INVALID_ATTRIBUTE_VALUE",
	"DS_E_INVALID_CREDENTIALS",
	"DS_E_INVALID_REF",
	"DS_E_INVALID_SIGNATURE",
	"DS_E_LOOP_DETECTED",
	"DS_E_MISCELLANEOUS",
	"DS_E_MISSING_TYPE",
	"DS_E_MIXED_SYNCHRONOUS",
	"DS_E_NAMING_VIOLATION",
	"DS_E_NO_INFORMATION",
	"DS_E_NO_SUCH_ATTRIBUTE_OR_VALUE",
	"DS_E_NO_SUCH_OBJECT",
	"DS_E_NO_SUCH_OPERATION",
	"DS_E_NOT_ALLOWED_ON_NON_LEAF",
	"DS_E_NOT_ALLOWED_ON_RDN",
	"DS_E_NOT_SUPPORTED",
	"DS_E_OBJECT_CLASS_MOD_PROHIB",
	"DS_E_OBJECT_CLASS_VIOLATION",
	"DS_E_OUT_OF_SCOPE",
	"DS_E_PROTECTION_REQUIRED",
	"DS_E_TIME_LIMIT_EXCEEDED",
	"DS_E_TOO_LATE",
	"DS_E_TOO_MANY_OPERATIONS",
	"DS_E_TOO_MANY_SESSIONS",
	"DS_E_UNABLE_TO_PROCEED",
	"DS_E_UNAVAILABLE",
	"DS_E_UNAVAILABLE_CRIT_EXT",
	"DS_E_UNDEFINED_ATTRIBUTE_TYPE",
	"DS_E_UNWILLING_TO_PERFORM"
};


#define	OM_S_OBJECT_ID_STRING	OM_S_OBJECT_IDENTIFIER_STRING

struct oid_to_string_t	oid_to_string_table[] = {

// this table is ordered on its first field

{&DS_O_XFN,			51, 0,				0},
{&DS_O_XFN_SUPPLEMENT,		52, 0,				0},
{&DS_A_OBJECT_CLASS,		19, OM_S_OBJECT_ID_STRING,	0},
{&DS_A_ALIASED_OBJECT_NAME,	 1, OM_S_OBJECT, &DS_C_DS_DN},
{&DS_A_KNOWLEDGE_INFO,		15, OM_S_TELETEX_STRING,	0},
{&DS_A_COMMON_NAME,		 5, OM_S_TELETEX_STRING,	0},
{&DS_A_SURNAME,			43, OM_S_TELETEX_STRING,	0},
{&DS_A_SERIAL_NBR,		39, OM_S_PRINTABLE_STRING,	0},
{&DS_A_COUNTRY_NAME,		 7, OM_S_PRINTABLE_STRING,	0},
{&DS_A_LOCALITY_NAME,		17, OM_S_TELETEX_STRING,	0},
{&DS_A_STATE_OR_PROV_NAME,	40, OM_S_TELETEX_STRING,	0},
{&DS_A_STREET_ADDRESS,		41, OM_S_TELETEX_STRING,	0},
{&DS_A_ORG_NAME,		21, OM_S_TELETEX_STRING,	0},
{&DS_A_ORG_UNIT_NAME,		25, OM_S_TELETEX_STRING,	0},
{&DS_A_TITLE,			47, OM_S_TELETEX_STRING,	0},
{&DS_A_DESCRIPTION,		 8, OM_S_TELETEX_STRING,	0},
{&DS_A_SEARCH_GUIDE,		37, OM_S_OBJECT, &DS_C_SEARCH_GUIDE},
{&DS_A_BUSINESS_CATEGORY,	 4, OM_S_TELETEX_STRING,	0},
{&DS_A_POSTAL_ADDRESS,		30, OM_S_OBJECT, &DS_C_POSTAL_ADDRESS},
{&DS_A_POSTAL_CODE,		31, OM_S_TELETEX_STRING,	0},
{&DS_A_POST_OFFICE_BOX,		29, OM_S_TELETEX_STRING,	0},
{&DS_A_PHYS_DELIV_OFF_NAME,	28, OM_S_TELETEX_STRING,	0},
{&DS_A_PHONE_NBR,		44, OM_S_PRINTABLE_STRING,	0},
{&DS_A_TELEX_NBR,		46, OM_S_OBJECT, &DS_C_TELEX_NBR},
{&DS_A_TELETEX_TERM_IDENT,	45, OM_S_OBJECT, &DS_C_TELETEX_TERM_IDENT},
{&DS_A_FACSIMILE_PHONE_NBR,	12, OM_S_OBJECT, &DS_C_FACSIMILE_PHONE_NBR},
{&DS_A_X121_ADDRESS,		50, OM_S_NUMERIC_STRING,	0},
{&DS_A_INTERNAT_ISDN_NBR,	14, OM_S_NUMERIC_STRING,	0},
{&DS_A_REGISTERED_ADDRESS,	34, OM_S_OBJECT, &DS_C_POSTAL_ADDRESS},
{&DS_A_DEST_INDICATOR,		 9, OM_S_PRINTABLE_STRING,	0},
{&DS_A_PREF_DELIV_METHOD,	32, OM_S_ENUMERATION,		0},
{&DS_A_PRESENTATION_ADDRESS,	33, OM_S_OBJECT, &DS_C_PRESENTATION_ADDRESS},
{&DS_A_SUPPORT_APPLIC_CONTEXT,	42, OM_S_OBJECT_ID_STRING,	0},
{&DS_A_MEMBER,			18, OM_S_OBJECT, &DS_C_DS_DN},
{&DS_A_OWNER,			26, OM_S_OBJECT, &DS_C_DS_DN},
{&DS_A_ROLE_OCCUPANT,		36, OM_S_OBJECT, &DS_C_DS_DN},
{&DS_A_SEE_ALSO,		38, OM_S_OBJECT, &DS_C_DS_DN},
{&DS_A_USER_PASSWORD,		49, OM_S_OCTET_STRING,		0},
{&DS_O_TOP,			48, 0,				0},
{&DS_O_ALIAS,			 0, 0,				0},
{&DS_O_COUNTRY,			 6, 0,				0},
{&DS_O_LOCALITY,		16, 0,				0},
{&DS_O_ORG,			20, 0,				0},
{&DS_O_ORG_UNIT,		24, 0,				0},
{&DS_O_PERSON,			27, 0,				0},
{&DS_O_ORG_PERSON,		22, 0,				0},
{&DS_O_ORG_ROLE,		23, 0,				0},
{&DS_O_GROUP_OF_NAMES,		13, 0,				0},
{&DS_O_RESIDENTIAL_PERSON,	35, 0,				0},
{&DS_O_APPLIC_PROCESS,		 3, 0,				0},
{&DS_O_APPLIC_ENTITY,		 2, 0,				0},
{&DS_O_DSA,			11, 0,				0},
{&DS_O_DEVICE,			10, 0,				0}

};
#undef OM_S_OBJECT_ID_STRING

int	 oid_to_string_table_size =
		sizeof (oid_to_string_table) / sizeof (oid_to_string_t);


struct string_to_oid_t	string_to_oid_table[] = {

// this table is ordered on its first field

/* 00 */ { "alias",			0,	&DS_O_ALIAS },
/* 01 */ { "aliased-object-name",	0,	&DS_A_ALIASED_OBJECT_NAME },
/* 02 */ { "application-entity",	0, 	&DS_O_APPLIC_ENTITY },
/* 03 */ { "application-process",	0,	&DS_O_APPLIC_PROCESS },
/* 04 */ { "business-category",		0,	&DS_A_BUSINESS_CATEGORY },
/* 05 */ { "common-name",		"cn",	&DS_A_COMMON_NAME },
/* 06 */ { "country",			0,	&DS_O_COUNTRY },
/* 07 */ { "country-name",		"c",	&DS_A_COUNTRY_NAME },
/* 08 */ { "description",		0,	&DS_A_DESCRIPTION },
/* 09 */ { "destination-indicator",	0,	&DS_A_DEST_INDICATOR },
/* 10 */ { "device",			0,	&DS_O_DEVICE },
/* 11 */ { "DSA",			0,	&DS_O_DSA },
/* 12 */ { "facsimile-telephone-number", 0,	&DS_A_FACSIMILE_PHONE_NBR },
/* 13 */ { "group-of-names",		0,	&DS_O_GROUP_OF_NAMES },
/* 14 */ { "international-ISDN-number",	0,	&DS_A_INTERNAT_ISDN_NBR },
/* 15 */ { "knowledge-information",	0,	&DS_A_KNOWLEDGE_INFO },
/* 16 */ { "locality",			0,	&DS_O_LOCALITY },
/* 17 */ { "locality-name",		"l",	&DS_A_LOCALITY_NAME },
/* 18 */ { "member",			0,	&DS_A_MEMBER },
/* 19 */ { "object-class",		0,	&DS_A_OBJECT_CLASS },
/* 20 */ { "organization",		0,	&DS_O_ORG },
/* 21 */ { "organization-name",		"o",	&DS_A_ORG_NAME },
/* 22 */ { "organizational-person",	0,	&DS_O_ORG_PERSON },
/* 23 */ { "organizational-role",	0,	&DS_O_ORG_ROLE },
/* 24 */ { "organizational-unit",	0,	&DS_O_ORG_UNIT },
/* 25 */ { "organizational-unit-name",	"ou",	&DS_A_ORG_UNIT_NAME },
/* 26 */ { "owner",			0,	&DS_A_OWNER },
/* 27 */ { "person",			0,	&DS_O_PERSON },
/* 28 */ { "physical-delivery-office-name", 0,	&DS_A_PHYS_DELIV_OFF_NAME },
/* 29 */ { "post-office-box",		0,	&DS_A_POST_OFFICE_BOX },
/* 30 */ { "postal-address",		0,	&DS_A_POSTAL_ADDRESS },
/* 31 */ { "postal-code",		0,	&DS_A_POSTAL_CODE },
/* 32 */ { "preferred-delivery-method",	0,	&DS_A_PREF_DELIV_METHOD },
/* 33 */ { "presentation-address",	0,	&DS_A_PRESENTATION_ADDRESS },
/* 34 */ { "registered-address",	0,	&DS_A_REGISTERED_ADDRESS },
/* 35 */ { "residential-person",	0,	&DS_O_RESIDENTIAL_PERSON },
/* 36 */ { "role-occupant",		0,	&DS_A_ROLE_OCCUPANT },
/* 37 */ { "search-guide",		0,	&DS_A_SEARCH_GUIDE },
/* 38 */ { "see-also",			0,	&DS_A_SEE_ALSO },
/* 39 */ { "serial-number",		0,	&DS_A_SERIAL_NBR },
/* 40 */ { "state-or-province-name",	"st",	&DS_A_STATE_OR_PROV_NAME },
/* 41 */ { "street-address",		0,	&DS_A_STREET_ADDRESS },
/* 42 */ { "supported-application-context", 0,	&DS_A_SUPPORT_APPLIC_CONTEXT },
/* 43 */ { "surname",			"sn",	&DS_A_SURNAME },
/* 44 */ { "telephone-number",		0,	&DS_A_PHONE_NBR },
/* 45 */ { "teletex-terminal-identifier", 0,	&DS_A_TELETEX_TERM_IDENT },
/* 46 */ { "telex-number",		0,	&DS_A_TELEX_NBR },
/* 47 */ { "title",			0,	&DS_A_TITLE },
/* 48 */ { "top",			0,	&DS_O_TOP },
/* 49 */ { "user-password",		0,	&DS_A_USER_PASSWORD },
/* 50 */ { "x121-address",		0,	&DS_A_X121_ADDRESS },
/* 51 */ { "XFN",			0,	&DS_O_XFN },
/* 52 */ { "XFN-supplement",		0,	&DS_O_XFN_SUPPLEMENT }

};
int	 string_to_oid_table_size =
		sizeof (string_to_oid_table) / sizeof (string_to_oid_t);

// XDS API calls are not MT-Safe
mutex_t	x500_dua_mutex = DEFAULTMUTEX;

// XDS/XOM environment
OM_boolean		initialized = 0;
OM_workspace		workspace = 0;
OM_private_object	session = 0;
OM_boolean		xfn_pkg = 0;


/*
 * Compare 2 object identifiers (in ASN.1 BER format)
 * (returns less-than, greater-than or equal)
 */
int
compare_om_oids2(
	const void	*oid1,
	const void	*oid2
)
{
	return (memcmp(((oid_to_string_t *)oid1)->oid->elements,
	    ((oid_to_string_t *)oid2)->oid->elements,
	    (size_t)((oid_to_string_t *)oid2)->oid->length));
}


/*
 * Compare 2 strings
 * (returns less-than, greater-than or equal)
 */
int
compare_strings(
	const void	*str1,
	const void	*str2
)
{
	return (strcasecmp(((string_to_oid_t *)str1)->string,
	    ((string_to_oid_t *)str2)->string));
}


/*
 * Display the supplied message on stderr
 */
void
x500_debug(
#ifdef DEBUG
	char	*format,
	...
#else
	char *,
	...
#endif
)
{
#ifdef DEBUG
	va_list	ap;
	va_start(ap, /* */);

	char		time_string[32];
	const time_t	time_value = time((time_t *)0);

	cftime(time_string, "%c", &time_value);
	(void) fprintf(stderr, "%s (fns x500) ", time_string);
	(void) vfprintf(stderr, format, ap);
	va_end(ap);
#endif
}


/*
 * Dump the supplied OM object on stderr
 */
void
x500_dump(
#ifdef DEBUG
	OM_object	object
#else
	OM_object
#endif
)
{
#ifdef DEBUG
	OMPexamin(object, 0, 2);
#endif
}
