/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_FNSP_PRINTER_SYNTAX_HH
#define	_FNSP_PRINTER_SYNTAX_HH

#pragma ident	"@(#)FNSP_printer_Syntax.hh	1.3	94/11/29 SMI"

#include <xfn/fn_spi.hh>  /* for FN_syntax_standard */

// function to return syntax information of FNSP contexts
extern const FN_syntax_standard* FNSP_printer_Syntax(unsigned context_type);


#endif /* _FNSP_PRINTER_SYNTAX_HH */
