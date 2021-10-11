/*
 * Copyright (c) 1992 - 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FNSP_Syntax.cc	1.5 94/11/24 SMI"

#include "../FNSP_Syntax.hh"
#include <xfn/fn_p.hh>

/* ******************* FNSP Syntaxes ********************************** */

// Flat, case-sensitive.

static const FN_syntax_standard
    FNSP_flat_syntax(FN_SYNTAX_STANDARD_DIRECTION_FLAT,
    FN_STRING_CASE_SENSITIVE,
    0, 0, 0, 0);


// Flat, case-insensitive.

static const FN_syntax_standard
FNSP_iflat_syntax(FN_SYNTAX_STANDARD_DIRECTION_FLAT,
    FN_STRING_CASE_INSENSITIVE,
    0, 0, 0, 0);


// Dotted right-to-left (case insensitive).

static const FN_string dot_sep((unsigned char *)".");
static const FN_string dot_bq((unsigned char *)"\""),
    dot_eq((unsigned char *)"\"");
static const FN_string dot_esc((unsigned char *)"\\");
static const FN_syntax_standard
    FNSP_dot_syntax(FN_SYNTAX_STANDARD_DIRECTION_RTL,
    FN_STRING_CASE_INSENSITIVE, &dot_sep, &dot_bq, &dot_eq, &dot_esc);


// Slash-separated, left-to-right (case sensitive).

static const FN_string slash_sep((unsigned char *)"/");
static const FN_string slash_bq((unsigned char *)"\""),
    slash_eq((unsigned char *)"\"");
static const FN_string slash_esc((unsigned char *)"\\");
static const FN_syntax_standard
    FNSP_slash_syntax(FN_SYNTAX_STANDARD_DIRECTION_LTR,
    FN_STRING_CASE_SENSITIVE, &slash_sep, &slash_bq, &slash_eq, &slash_esc);


// Returns syntax associated with given FNSP context type

const FN_syntax_standard *
FNSP_Syntax(unsigned context_type)
{
	const FN_syntax_standard *answer = 0;

	switch (context_type) {
	case FNSP_organization_context:
	case FNSP_site_context:
		answer = &FNSP_dot_syntax;    // insensitive, right-to-left dot
		break;
	case FNSP_enterprise_context:
	case FNSP_hostname_context:
	case FNSP_user_context:
	case FNSP_host_context:
	case FNSP_nsid_context:
		answer = &FNSP_iflat_syntax;  // insensitive flat
		break;
	case FNSP_null_context:		// no syntax
	case FNSP_username_context:
		answer = &FNSP_flat_syntax;   // sensitive flat
		break;
	case FNSP_service_context:
	case FNSP_generic_context:
		answer = &FNSP_slash_syntax;  // sensitive, left-to-right slash
		break;
	default:
		answer = 0;
	}
	return (answer);
}
