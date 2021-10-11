/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident "@(#)FNSP_printer_Syntax.cc	1.3 94/11/24 SMI"

#include <xfn/fn_printer_p.hh>
#include "../FNSP_printer_Syntax.hh"

// Flat, case-sensitive.

static const FN_syntax_standard
    FNSP_flat_syntax(FN_SYNTAX_STANDARD_DIRECTION_FLAT,
    FN_STRING_CASE_SENSITIVE,
    0, 0, 0, 0);


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
FNSP_printer_Syntax(unsigned context_type)
{
	const FN_syntax_standard *answer = 0;

	switch (context_type) {
	case FNSP_printername_context:
	case FNSP_printer_object:
		// sensitive, left-to-right slash
		answer = &FNSP_slash_syntax;
		break;
	case FNSP_printername_context_nis:
	case FNSP_printername_context_files:
		answer = &FNSP_flat_syntax;
		break;
	default:
		answer = 0;
	}
	return (answer);
}
