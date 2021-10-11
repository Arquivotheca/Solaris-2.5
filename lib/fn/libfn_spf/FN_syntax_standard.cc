/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FN_syntax_standard.cc	1.5 94/09/21 SMI"

#include "FN_syntax_standard.hh"

static FN_identifier
    FN_ASCII_SYNTAX_ID((unsigned char *) "fn_attr_syntax_ascii");


FN_syntax_standard::~FN_syntax_standard()
{
	if (info.component_separator)
		delete ((FN_string *)info.component_separator);
	if (info.begin_quote)
		delete ((FN_string *)info.begin_quote);
	if (info.end_quote)
		delete ((FN_string *)info.end_quote);
	if (info.escape)
		delete ((FN_string *)info.escape);
	if (info.type_separator)
		delete ((FN_string *)info.type_separator);
	if (info.ava_separator)
		delete ((FN_string *)info.ava_separator);

	info.component_separator = info.escape = 0;
	info.end_quote = info.begin_quote = 0;
	info.type_separator = info.ava_separator = 0;
}

void
FN_syntax_standard::common_init(unsigned int dir,
    unsigned int scase,
    const FN_string *sep,
    const FN_string *begin_q,
    const FN_string *end_q,
    const FN_string *esc,
    const FN_string *type_sep,
    const FN_string *ava_sep)
{
	info.direction = dir;
	info.string_case = scase;
	if (sep)
		info.component_separator = (FN_string_t *)new FN_string(*sep);
	else
		info.component_separator = (FN_string_t *)0;
	if (begin_q)
		info.begin_quote =  (FN_string_t *)new FN_string(*begin_q);
	else
		info.begin_quote = (FN_string_t *)0;
	if (end_q)
		info.end_quote =  (FN_string_t *)new FN_string(*end_q);
	else
		info.end_quote = (FN_string_t *)0;
	if (esc)
		info.escape =  (FN_string_t *)new FN_string(*esc);
	else
		info.escape = (FN_string_t *)0;
	if (type_sep)
		info.type_separator =  (FN_string_t *)new FN_string(*type_sep);
	else
		info.type_separator = (FN_string_t *)0;
	if (ava_sep)
		info.ava_separator =  (FN_string_t *)new FN_string(*ava_sep);
	else
		info.ava_separator = (FN_string_t *)0;
}

FN_syntax_standard::FN_syntax_standard(const FN_syntax_standard &s)
{
	common_init(s.info.direction,
		    s.info.string_case,
		    (const FN_string *)s.info.component_separator,
		    (const FN_string *)s.info.begin_quote,
		    (const FN_string *)s.info.end_quote,
		    (const FN_string *)s.info.escape,
		    (const FN_string *)s.info.type_separator,
		    (const FN_string *)s.info.ava_separator);
}

FN_syntax_standard::FN_syntax_standard(const FN_syntax_standard_t &si)
{
	common_init(si.direction,
		    si.string_case,
		    (const FN_string *)si.component_separator,
		    (const FN_string *)si.begin_quote,
		    (const FN_string *)si.end_quote,
		    (const FN_string *)si.escape,
		    (const FN_string *)si.type_separator,
		    (const FN_string *)si.ava_separator);
}


FN_syntax_standard::FN_syntax_standard(unsigned int dir,
    unsigned int c,
    const FN_string *sep,
    const FN_string *begin_q,
    const FN_string *end_q,
    const FN_string *esc)
{
	common_init(dir, c, sep, begin_q, end_q, esc, 0, 0);
}

FN_syntax_standard::FN_syntax_standard(unsigned int dir,
    unsigned int c,
    const FN_string *sep,
    const FN_string *begin_q,
    const FN_string *end_q,
    const FN_string *esc,
    const FN_string *tsep,
    const FN_string *asep)
{
	common_init(dir, c, sep, begin_q, end_q, esc, tsep, asep);
}


FN_syntax_standard*
FN_syntax_standard::from_syntax_attrs(const FN_attrset &a,
    FN_status &s)
{
	const FN_attribute *type_attr =
	    a.get((unsigned char *)"fn_syntax_type");
	if (type_attr == 0) {
		s.set_code(FN_E_INVALID_SYNTAX_ATTRS);
		return (0);
	}

	void *vp;
	const FN_attrvalue *type_val = type_attr->first(vp);
	FN_attrvalue standard_val((unsigned char *)"standard");
	unsigned int done = 0;
	while (!done && type_val) {
		if (*type_val == standard_val) {
			done = 1;
			break;
		}
		type_val = type_attr->next(vp);
	}
	if (!done) {
		s.set_code(FN_E_SYNTAX_NOT_SUPPORTED);
		return (0);
	}

	const FN_attribute *dir_attr =
	    a.get((unsigned char *)"fn_std_syntax_direction");
	void *ip;
	const FN_attrvalue *dir_val;
	if (dir_attr == 0 || (dir_val = dir_attr->first(ip)) == 0) {
		s.set_code(FN_E_INVALID_SYNTAX_ATTRS);
		return (0);
	}
	FN_string *dir_str = dir_val->string();
	unsigned int dir;
	if (dir_str->compare((unsigned char *)"flat") == 0) {
		dir = FN_SYNTAX_STANDARD_DIRECTION_FLAT;
	} else if (dir_str->compare((unsigned char *)"left_to_right") == 0) {
		dir = FN_SYNTAX_STANDARD_DIRECTION_LTR;
	} else if (dir_str->compare((unsigned char *)"right_to_left") == 0) {
		dir = FN_SYNTAX_STANDARD_DIRECTION_RTL;
	} else {
		delete dir_str;
		s.set_code(FN_E_INVALID_SYNTAX_ATTRS);
		return (0);
	}
	delete dir_str;

	// determine separator (if there should be one)

	const FN_attribute	*sep_attr;
	const FN_attrvalue	*sep_val;
	FN_string		*sep;

	sep_attr = a.get((unsigned char *)"fn_std_syntax_separator");
	sep_val = sep_attr? sep_attr->first(ip): 0;
	sep = sep_val? sep_val->string(): 0;

	if (dir == FN_SYNTAX_STANDARD_DIRECTION_FLAT) {
		if (sep) {
			delete sep;
			s.set_code(FN_E_INVALID_SYNTAX_ATTRS);
			return (0);
		}
	} else {
		if (!sep) {
			s.set_code(FN_E_INVALID_SYNTAX_ATTRS);
			return (0);
		}
	}

	const FN_attribute *begin_q_at =
		a.get((unsigned char *)"fn_std_syntax_begin_quote");
	const FN_attribute *end_q_at =
	    a.get((unsigned char *)"fn_std_syntax_end_quote");
	if (begin_q_at && !end_q_at) {
		s.set_code(FN_E_INVALID_SYNTAX_ATTRS);
		return (0);
	}

	const FN_attrvalue *begin_qval = begin_q_at? begin_q_at->first(ip): 0;
	FN_string *begin_q = (begin_qval? begin_qval->string() : 0);
	const FN_attrvalue *end_qval = end_q_at? end_q_at->first(ip): 0;
	FN_string *end_q = (end_qval? end_qval->string() : 0);

	const FN_attribute *esc_at = a.get(
	    (const unsigned char *)"fn_std_syntax_escape");
	const FN_attrvalue *esc_atval;
	FN_string *esc = 0;
	if (esc_at && (esc_atval = esc_at->first(ip)))
		esc = esc_atval->string();

	const FN_attribute *case_str_at =
		a.get((unsigned char *)"fn_std_syntax_case_insensitive");
	unsigned int c;
	if (case_str_at) {
		c = FN_STRING_CASE_INSENSITIVE;
	} else {
		c = FN_STRING_CASE_SENSITIVE;
	}

	FN_syntax_standard *ret =
	    new FN_syntax_standard(dir, c, sep, begin_q, end_q, esc);
	delete sep;
	delete begin_q;
	delete end_q;
	delete esc;
	if (ret == 0) {
		s.set_code(FN_E_INSUFFICIENT_RESOURCES);
	} else {
		s.set_success();
	}
	return (ret);
}


FN_attrset*
FN_syntax_standard::get_syntax_attrs() const
{
	FN_attrset *ret = new FN_attrset();
	if (ret == 0)
		return (0);

	FN_attribute syn_type((unsigned char *)"fn_syntax_type",
	    FN_ASCII_SYNTAX_ID);
	FN_attrvalue standard((unsigned char *)"standard");
	syn_type.add(standard);
	ret->add(syn_type);
	FN_attribute syn_dir((unsigned char *)"fn_std_syntax_direction",
	    FN_ASCII_SYNTAX_ID);
	FN_attrvalue dir;
	switch (direction()) {
	case FN_SYNTAX_STANDARD_DIRECTION_FLAT:
		dir = ((unsigned char *)"flat");
		break;
	case FN_SYNTAX_STANDARD_DIRECTION_LTR:
		dir = ((unsigned char *)"left_to_right");
		break;
	case FN_SYNTAX_STANDARD_DIRECTION_RTL:
		dir = ((unsigned char *)"right_to_left");
		break;
	}
	syn_dir.add(dir);
	ret->add(syn_dir);

	if (component_separator()) {
		FN_attribute syn_sep(
		    (unsigned char *)"fn_std_syntax_separator",
		    FN_ASCII_SYNTAX_ID);
		FN_attrvalue sep(*component_separator());
		syn_sep.add(sep);
		ret->add(syn_sep);
	}

	if (begin_quote()) {
		FN_attribute syn_bquote(
		    (unsigned char *)"fn_std_syntax_begin_quote",
		    FN_ASCII_SYNTAX_ID);
		FN_attrvalue bquote(*begin_quote());
		syn_bquote.add(bquote);
		ret->add(syn_bquote);
	}
	if (end_quote()) {
		FN_attribute syn_equote(
		    (unsigned char *)"fn_std_syntax_end_quote",
		    FN_ASCII_SYNTAX_ID);
		FN_attrvalue equote(*end_quote());
		syn_equote.add(equote);
		ret->add(syn_equote);
	}

	if (escape()) {
		FN_attribute syn_esc(
		    (unsigned char *)"fn_std_syntax_escape",
		    FN_ASCII_SYNTAX_ID);
		FN_attrvalue esc(*escape());
		syn_esc.add(esc);
		ret->add(syn_esc);
	}

	if (string_case() == FN_STRING_CASE_INSENSITIVE) {
		FN_attribute syn_case(
		    (unsigned char *)"fn_std_syntax_case_insensitive",
		    FN_ASCII_SYNTAX_ID);
		ret->add(syn_case);
	}

	if (ava_separator()) {
		FN_attribute syn_sep(
		    (unsigned char *)"fn_std_syntax_separator",
		    FN_ASCII_SYNTAX_ID);
		FN_attrvalue sep(*ava_separator());
		syn_sep.add(sep);
		ret->add(syn_sep);
	}

	if (type_separator()) {
		FN_attribute syn_sep(
		    (unsigned char *)"fn_std_syntax_typeval_separator",
		    FN_ASCII_SYNTAX_ID);
		FN_attrvalue sep(*type_separator());
		syn_sep.add(sep);
		ret->add(syn_sep);
	}

	return (ret);
}

unsigned int
FN_syntax_standard::direction(void) const
{
	return (info.direction);
}
unsigned int
FN_syntax_standard:: string_case(void) const
{
	return (info.string_case);
}
const FN_string *
FN_syntax_standard::component_separator(void) const
{
	return ((const FN_string *)info.component_separator);
}
const FN_string *
FN_syntax_standard::begin_quote(void) const
{
	return ((const FN_string *)info.begin_quote);
}
const FN_string *
FN_syntax_standard::end_quote(void) const
{
	return ((const FN_string *)info.end_quote);
}
const FN_string *
FN_syntax_standard::escape(void) const
{
	return ((const FN_string *)info.escape);
}
const FN_string *
FN_syntax_standard::type_separator(void) const
{
	return ((const FN_string *)info.type_separator);
}
const FN_string *
FN_syntax_standard::ava_separator(void) const
{
	return ((const FN_string *)info.ava_separator);
}
