/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#ifndef _XFN__FN_SYNTAX_STANDARD_HH
#define	_XFN__FN_SYNTAX_STANDARD_HH

#pragma ident	"@(#)FN_syntax_standard.hh	1.5	94/08/13 SMI"

#include <xfn/xfn.hh>
#include <xfn/FN_syntax_standard.h>

class FN_syntax_standard {
	void common_init(unsigned int direction,
	    unsigned int string_case,
	    const FN_string *sep,
	    const FN_string *begin_q,
	    const FN_string *end_q,
	    const FN_string *esc,
	    const FN_string *type_sep,
	    const FN_string *ava_sep);
public:
	FN_syntax_standard_t info;

	~FN_syntax_standard();
	FN_syntax_standard(const FN_syntax_standard &);
	FN_syntax_standard(const FN_syntax_standard_t &);

	FN_syntax_standard &operator=(const FN_syntax_standard &);

	FN_syntax_standard(unsigned int direction,
	    unsigned int string_case,
	    const FN_string *sep,
	    const FN_string *begin_q,
	    const FN_string *end_q,
	    const FN_string *esc);

	FN_syntax_standard(unsigned int direction,
	    unsigned int string_case,
	    const FN_string *sep,
	    const FN_string *begin_q,
	    const FN_string *end_q,
	    const FN_string *esc,
	    const FN_string *type_sep,
	    const FN_string *ava_sep);

	/* accessors */
	unsigned int direction(void) const;
	unsigned int string_case(void) const;
	const FN_string* component_separator(void) const;
	const FN_string* begin_quote(void) const;
	const FN_string* end_quote(void) const;
	const FN_string* escape(void) const;
	const FN_string* type_separator(void) const;
	const FN_string* ava_separator(void) const;

	static FN_syntax_standard *from_syntax_attrs(const FN_attrset &,
	    FN_status &);
	FN_attrset *get_syntax_attrs(void) const;
};

#endif // _XFN__FN_SYNTAX_STANDARD_HH
