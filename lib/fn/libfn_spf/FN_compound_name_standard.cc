/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FN_compound_name_standard.cc	1.10 94/10/28 SMI"

#include <string.h>
#include <xfn/FN_compound_name_standard.hh>

#include "../libxfn/NameList.hh"

class FN_compound_name_standard_rep {
public:
	FN_syntax_standard syntax;
	List comps;

	FN_compound_name_standard_rep(const FN_syntax_standard &);
	FN_compound_name_standard_rep(const FN_compound_name_standard_rep &);
	~FN_compound_name_standard_rep();
};

FN_compound_name_standard_rep::FN_compound_name_standard_rep(
    const FN_syntax_standard &s)
: syntax(s)
{
}

FN_compound_name_standard_rep::FN_compound_name_standard_rep(
    const FN_compound_name_standard_rep &n)
: syntax(n.syntax), comps(n.comps)
{
}

FN_compound_name_standard_rep::~FN_compound_name_standard_rep()
{
}


FN_compound_name_standard::FN_compound_name_standard(
    FN_compound_name_standard_rep *r)
: rep(r)
{
}

FN_compound_name_standard_rep *
FN_compound_name_standard::get_rep(const FN_compound_name_standard &n)
{
	return (n.rep);
}

FN_compound_name_standard::FN_compound_name_standard(
    const FN_syntax_standard &s)
{
	rep = new FN_compound_name_standard_rep(s);
}

static int syntax_legal_comp(const FN_syntax_standard &s,
    const FN_string &c)
{
	// anything goes with flat names
	if (s.direction() == FN_SYNTAX_STANDARD_DIRECTION_FLAT)
		return (1);
	// can't have quotes if no way to escape them
	if (s.escape() == 0 && s.begin_quote() &&
	    (c.next_substring(*s.begin_quote()) != FN_STRING_INDEX_NONE ||
	    c.next_substring(*s.end_quote()) != FN_STRING_INDEX_NONE))
		return (0);
	return (1);
}

static int
syntax_extract_comp(const FN_syntax_standard &s,
    unsigned char *comp,
    const unsigned char *&str)
{
	while (*str) {
		if (s.begin_quote() &&
		    strncmp((char *)str, (char *)(s.begin_quote()->str()),
			    s.begin_quote()->charcount()) == 0) {
			for (str += s.begin_quote()->charcount();
			    *str && strncmp((char *)str,
			    (char *)(s.end_quote()->str()),
			    s.end_quote()->charcount()) != 0;
			    str++) {
				if (s.escape() &&
				    strncmp((char *)str,
					(char *)(s.escape()->str()),
					s.escape()->charcount()) == 0)
					str += s.escape()->charcount();
				*comp++ = *str;
			}
			if (*str == 0)
				return (0);
			str += s.end_quote()->charcount();
		} else {
			if (s.escape() &&
			    strncmp((char *)str, (char *)(s.escape()->str()),
			    s.escape()->charcount()) == 0) {
				str += s.escape()->charcount();
				if (*str == 0)
					return (0);
				*comp++ = *str++;
			} else {
				if (s.direction() !=
				    FN_SYNTAX_STANDARD_DIRECTION_FLAT &&
				    strncmp((char *)str,
				    (char *) (s.component_separator()->str()),
				    s.component_separator()->charcount())
				    == 0) {
					str +=
					    s.component_separator()->
					    charcount();
					*comp = 0;
					return (1);
				} else {
					*comp++ = *str++;
				}
			}
		}
	}
	str = 0;
	*comp = 0;
	return (1);
}

static void
syntax_stringify_comp(const FN_syntax_standard &s,
    unsigned char *&str, const unsigned char *comp)
{
	int quot = 0;
	int esc_sep = 0;

	const unsigned char *p;
	for (p = comp; *p; p++)
		if (s.direction() != FN_SYNTAX_STANDARD_DIRECTION_FLAT &&
		    strncmp((char *)p,
		    (char *)(s.component_separator()->str()),
		    s.component_separator()->charcount()) == 0) {
			if (s.begin_quote())
				quot = 1;
			else
				esc_sep = 1;
			break;
		}

	if (quot) {
		strcpy((char *)str, (char *)(s.begin_quote()->str()));
		str += s.begin_quote()->charcount();
	}
	while (*comp) {
		if (s.escape() && strncmp((char *)comp,
		    (char *)(s.escape()->str()),
		    s.escape()->charcount()) == 0) {
			strcpy((char *)str, (char *)(s.escape()->str()));
			str += s.escape()->charcount();
			strcpy((char *)str, (char *)(s.escape()->str()));
			str += s.escape()->charcount();
			comp += s.escape()->charcount();
		} else {
			if (quot && strncmp((char *)comp,
			    (char *)(s.end_quote()->str()),
			    s.end_quote()->charcount()) == 0) {
				strcpy((char *)str,
				    (char *)(s.escape()->str()));
				str += s.escape()->charcount();
				strcpy((char *)str,
				    (char *)(s.end_quote()->str()));
				str += s.end_quote()->charcount();
				comp += s.end_quote()->charcount();
			} else
				*str++ = *comp++;
		}
	}
	if (quot) {
		strcpy((char *)str, (char *)(s.end_quote()->str()));
		str += s.end_quote()->charcount();
	}
}

static void
syntax_extract_name(FN_compound_name_standard_rep &d, const unsigned char *str)
{
	unsigned char *buf;

	d.comps.delete_all();
	buf = new unsigned char[strlen((char *)str)+1];
	do {
		if (!syntax_extract_comp(d.syntax, buf, str)) {
			// illegal name
			d.comps.delete_all();
			break;
		}
		switch (d.syntax.direction()) {
		case FN_SYNTAX_STANDARD_DIRECTION_FLAT:
		case FN_SYNTAX_STANDARD_DIRECTION_LTR:
			d.comps.append_item(new NameListItem(buf));
			break;
		case FN_SYNTAX_STANDARD_DIRECTION_RTL:
			d.comps.prepend_item(new NameListItem(buf));
			break;
		}
	} while (str);
	delete[] buf;
}

static unsigned char *
syntax_stringify_name(FN_compound_name_standard_rep &d)
{
	const NameListItem *i;
	void *ip;
	int size;
	unsigned char *buf;
	unsigned char *p;

	for (size = 0, i = (const NameListItem *)(d.comps.first(ip)); i;
	    i = (const NameListItem *)(d.comps.next(ip)))
		size += 2+2*strlen((char *)(i->name.str()));
	if (size == 0)
		p = buf = new unsigned char[1];
	else {
		p = buf = new unsigned char[size];
		switch (d.syntax.direction()) {
		case FN_SYNTAX_STANDARD_DIRECTION_FLAT:
		case FN_SYNTAX_STANDARD_DIRECTION_LTR:
			i = (const NameListItem *)(d.comps.first(ip));
			break;
		case FN_SYNTAX_STANDARD_DIRECTION_RTL:
			i = (const NameListItem *)(d.comps.last(ip));
			break;
		default:
			i = 0;
		}
		while (i) {
			syntax_stringify_comp(d.syntax, p, i->name.str());
			switch (d.syntax.direction()) {
			case FN_SYNTAX_STANDARD_DIRECTION_FLAT:
			case FN_SYNTAX_STANDARD_DIRECTION_LTR:
				i = (const NameListItem *)(d.comps.next(ip));
				break;
			case FN_SYNTAX_STANDARD_DIRECTION_RTL:
				i = (const NameListItem *)(d.comps.prev(ip));
				break;
			}
			if (i) {
				strcpy((char *)p, (char *)
				    (d.syntax.component_separator()->str()));
				p += d.syntax.component_separator()->
				    charcount();
			}
		}
	}
	*p = 0;
	return (buf);
}

FN_compound_name_standard::FN_compound_name_standard(
    const FN_syntax_standard &s, const FN_string &n)
{
	unsigned int status;
	rep = new FN_compound_name_standard_rep(s);
	syntax_extract_name(*rep, n.str(&status));
}

FN_compound_name_standard::FN_compound_name_standard(
    const FN_compound_name_standard &n)
{
	rep = new FN_compound_name_standard_rep(*get_rep(n));
}

FN_compound_name &
FN_compound_name_standard::operator=(const FN_compound_name &n)
{
	// assume 'n' is FN_compound_name_standard
	if (&n != this) {
		if (rep)
			delete rep;
		rep = new FN_compound_name_standard_rep(
		    *get_rep((const FN_compound_name_standard &)n));
	}
	return (*this);
}

FN_compound_name *
FN_compound_name_standard::dup() const
{
	FN_compound_name_standard	*ns;

	if (rep) {
		FN_compound_name_standard_rep	*r;

		r = new FN_compound_name_standard_rep(*rep);
		ns = new FN_compound_name_standard(r);
		if (ns == 0)
			delete r;
	} else
		ns = 0;
	return (ns);
}

FN_compound_name_standard::~FN_compound_name_standard()
{
	if (rep)
		delete rep;
}

FN_syntax_standard *
FN_compound_name_standard::get_syntax() const
{
	return (new FN_syntax_standard(rep->syntax));
}

FN_compound_name_standard *
FN_compound_name_standard::from_syntax_attrs(const FN_attrset &a,
    const FN_string &n,
    FN_status &s)
{
	FN_syntax_standard *stx = FN_syntax_standard::from_syntax_attrs(a, s);
	if (stx == 0)
		return (0);

	FN_compound_name_standard *ret =
	    new FN_compound_name_standard(*stx, n);
	if (ret == 0) {
		s.set_code(FN_E_INSUFFICIENT_RESOURCES);
	} else if (ret->count() == 0) {
		s.set_code(FN_E_ILLEGAL_NAME);
		delete ret;
		ret = 0;
	} else {
		s.set_success();
	}

	delete stx;
	return (ret);
}

FN_attrset*
FN_compound_name_standard::get_syntax_attrs() const
{
	return (rep->syntax.get_syntax_attrs());
}

// convert to string representation
FN_string *
FN_compound_name_standard::string() const
{
	unsigned char *buf = syntax_stringify_name(*rep);
	FN_string *ret = new FN_string(buf);
	delete[] buf;
	return (ret);
}

#if 0
FN_compound_name&
FN_compound_name_standard::operator=(const FN_string &s)
{
	unsigned int status;
	syntax_extract_name(*rep, s.str(&status));
	return (*this);
}
#endif


// syntactic comparison
FN_compound_name_standard::operator==(const FN_compound_name &n) const
{
	return (is_equal(n));
}

FN_compound_name_standard::operator!=(const FN_compound_name &n) const
{
	void *ip1, *ip2;
	const FN_string *c1, *c2;

	for (c1 = first(ip1), c2 = n.first(ip2);
	    c1 && c2;
	    c1 = next(ip1), c2 = n.next(ip2))
		if (c1->compare(*c2, (rep)->syntax.string_case()))
			return (1);
	return (c1 != c2);
}

FN_compound_name_standard::operator<(const FN_compound_name &n) const
{
	void *ip1, *ip2;
	const FN_string *c1, *c2;

	for (c1 = first(ip1), c2 = n.first(ip2);
	    c1 && c2 && c1->compare(*c2, (rep)->syntax.string_case()) == 0;
	    c1 = next(ip1), c2 = n.next(ip2));
	if (c2) {
		if (c1)
			return (c1->compare(*c2,
			    (rep)->syntax.string_case()) < 0);
		return (1);
	}
	return (0);
}

FN_compound_name_standard::operator>(const FN_compound_name &n) const
{
	void *ip1, *ip2;
	const FN_string *c1, *c2;

	for (c1 = first(ip1), c2 = n.first(ip2);
	    c1 && c2 && c1->compare(*c2, (rep)->syntax.string_case()) == 0;
	    c1 = next(ip1), c2 = n.next(ip2));
	if (c1) {
		if (c2)
			return (c1->compare(*c2, (rep)->syntax.string_case()) >
			    0);
		return (1);
	}
	return (0);
}

FN_compound_name_standard::operator<=(const FN_compound_name &n) const
{
	void *ip1, *ip2;
	const FN_string *c1, *c2;

	for (c1 = first(ip1), c2 = n.first(ip2);
	    c1 && c2 && c1->compare(*c2, (rep)->syntax.string_case()) == 0;
	    c1 = next(ip1), c2 = n.next(ip2));
	if (c2) {
		if (c1)
			return (c1->compare(*c2, (rep)->syntax.string_case()) <
			    0);
		return (1);
	}
	if (c1)
		return (0);
	return (1);
}

FN_compound_name_standard::operator>=(const FN_compound_name &n) const
{
	void *ip1, *ip2;
	const FN_string *c1, *c2;

	for (c1 = first(ip1), c2 = n.first(ip2);
	    c1 && c2 && c1->compare(*c2, (rep)->syntax.string_case()) == 0;
	    c1 = next(ip1), c2 = n.next(ip2));
	if (c1) {
		if (c2)
			return (c1->compare(*c2, (rep)->syntax.string_case()) >
			    0);
		return (1);
	}
	if (c2)
		return (0);
	return (1);
}

// get count of components in name
unsigned
FN_compound_name_standard::count() const
{
	return (rep->comps.count());
}

// test for empty name (single empty string component)
int
FN_compound_name_standard::is_empty() const
{
	void *ip;
	return (count() == 1 && first(ip)->is_empty());
}

// get first component (points iter_pos after name)
const FN_string *
FN_compound_name_standard::first(void *&iter_pos) const
{
	const NameListItem *i;

	if (i = (const NameListItem *)((rep)->comps.first(iter_pos)))
		return (&(i->name));
	else
		return (0);
}

// test for prefix (points iter_pos after prefix)
int
FN_compound_name_standard::is_prefix(const FN_compound_name &n,
    void *&iter_pos, unsigned int * /*status*/) const
{
	void *ip;
	const FN_string *c1, *c2;

	for (c1 = n.first(ip), c2 = first(iter_pos);
	    c1 && c2;
	    c1 = n.next(ip), c2 = next(iter_pos))
		if (c1->compare(*c2, (rep)->syntax.string_case()))
			return (0);
	if (c2)
		prev(iter_pos);
	return (c1 == 0);
}

// test for equality
int
FN_compound_name_standard::is_equal(const FN_compound_name &n,
    unsigned int * /*status*/) const
{
	void *ip1, *ip2;
	const FN_string *c1, *c2;

	for (c1 = first(ip1), c2 = n.first(ip2);
	    c1 && c2;
	    c1 = next(ip1), c2 = n.next(ip2))
		if (c1->compare(*c2, (rep)->syntax.string_case()))
			// check status
			return (0);
	return (c1 == c2);
}

// Get last component (points iter_pos before name)
const FN_string *
FN_compound_name_standard::last(void *&iter_pos) const
{
	const NameListItem *i;

	if (i = (const NameListItem *)((rep)->comps.last(iter_pos)))
		return (&(i->name));
	else
		return (0);
}

// test for suffix (points iter_pos before suffix)
int
FN_compound_name_standard::is_suffix(const FN_compound_name &n,
    void *&iter_pos, unsigned int * /*status*/) const
{
	void *ip;
	const FN_string *c1, *c2;

	for (c1 = n.last(ip), c2 = last(iter_pos);
	    c1 && c2;
	    c1 = n.prev(ip), c2 = prev(iter_pos))
		if (c1->compare(*c2, (rep)->syntax.string_case()))
			return (0);
	if (c2)
		next(iter_pos);
	return (c1 == 0);
}

// get component following iter_pos (points iter_pos after component)
const FN_string *FN_compound_name_standard::next(void *&iter_pos) const
{
	const NameListItem *i;

	if (i = (const NameListItem *)((rep)->comps.next(iter_pos)))
		return (&(i->name));
	else
		return (0);
}

// get component before iter_pos (points iter_pos before component)
const FN_string *FN_compound_name_standard::prev(void *&iter_pos) const
{
	const NameListItem *i;

	if (i = (const NameListItem *)((rep)->comps.prev(iter_pos)))
		return (&(i->name));
	else
		return (0);
}

// get copy of name from first component through iter_pos
FN_compound_name* FN_compound_name_standard::prefix(const void *iter_pos) const
{
	void *ip = (void *)iter_pos;
	FN_compound_name *n;
	const FN_string *c;
	unsigned int status;

	if ((c = prev(ip)) == 0)
		return (0);
	if ((n = new FN_compound_name_standard((rep)->syntax)) == 0)
		return (0);
	do {
		n->prepend_comp(*c, &status);
	} while (c = prev(ip));
	return (n);
}

// get copy of name from iter_pos through last component
FN_compound_name* FN_compound_name_standard::suffix(const void *iter_pos) const
{
	void *ip = (void *)iter_pos;
	FN_compound_name *n;
	const FN_string *c;
	unsigned int status;

	if ((c = next(ip)) == 0)
		return (0);
	if ((n = new FN_compound_name_standard((rep)->syntax)) == 0)
		return (0);
	do {
		n->append_comp(*c, &status);
	} while (c = next(ip));
	return (n);
}

// prepend component to name
int
FN_compound_name_standard::prepend_comp(const FN_string &c,
    unsigned int *status)
{
	// syntax allows name?
	if (!syntax_legal_comp((rep)->syntax, c)) {
		*status = FN_E_SYNTAX_NOT_SUPPORTED;
		return (0);
	}

	// flat names can only have one component
	if ((rep)->syntax.direction() == FN_SYNTAX_STANDARD_DIRECTION_FLAT &&
	    count()+1 > 1) {
		*status = FN_E_ILLEGAL_NAME;
		return (0);
	}

	int r = rep->comps.prepend_item(new NameListItem(c));
	*status = r? FN_SUCCESS: FN_E_INSUFFICIENT_RESOURCES;
	return (r);
}

// append component to name
int
FN_compound_name_standard::append_comp(const FN_string &c,
    unsigned int *status)
{
	*status = FN_SUCCESS;
	// syntax allows name?
	if (!syntax_legal_comp(rep->syntax, c)) {
		*status = FN_E_SYNTAX_NOT_SUPPORTED;
		return (0);
	}

	// special case for appending null names
	// what should we return in this case?
	if (c.is_empty())
		return (1);

	// check to see if first component is NULL
	// it is NULL, then replace the component with the new one
	if (count() == 1) {
		void *iterpos;
		const NameListItem *item =
			(const NameListItem *)(rep)->comps.first(iterpos);
		if (!item) {
			*status = FN_E_UNSPECIFIED_ERROR;
			return (0);
		}
		if ((item->name).is_empty()) {
			// first component is null, replace that
			// with new component
			(rep)->comps.delete_item(iterpos);
			int r = (rep->comps.append_item(new NameListItem(c)));
			if (!r)
				*status = FN_E_UNSPECIFIED_ERROR;
			return (r);
		}
	}
	// flat names can only have one component
	if ((rep)->syntax.direction() == FN_SYNTAX_STANDARD_DIRECTION_FLAT &&
	    (count()+1 > 1)) {
		*status = FN_E_ILLEGAL_NAME;
		return (0);
	}

	int r = (rep->comps.append_item(new NameListItem(c)));
	if (!r)
		*status = FN_E_INSUFFICIENT_RESOURCES;
	return (r);
}

// insert component before iter_pos
int
FN_compound_name_standard::insert_comp(void *&iter_pos,
    const FN_string &c, unsigned int *status)
{
	*status = FN_SUCCESS;
	// syntax allows name?
	if (!syntax_legal_comp((rep)->syntax, c)) {
		*status = FN_E_SYNTAX_NOT_SUPPORTED;
		return (0);
	}

	// special case for appending null names
	// what should we return in this case?
	if (c.is_empty())
		return (1);

	// check to see if first component is NULL
	// it is NULL, then replace the component with the new one
	if (count() == 1) {
		void *pos;
		const NameListItem *item =
			(const NameListItem *)(rep)->comps.first(pos);
		if (!item) {
			*status = FN_E_ILLEGAL_NAME;
			return (0);
		}
		if ((item->name).is_empty()) {
			// first component is null, replace that
			// with new component
			(rep)->comps.delete_item(pos);
			int r = (rep)->comps.append_item(new NameListItem(c));
			if (r) {
				// set iterpos to correct postion
				item = (const NameListItem *)
				    (rep)->comps.first(iter_pos);
			}
			*status = FN_E_INSUFFICIENT_RESOURCES;
			return (r);
		}
	}

	// flat names can only have one component
	if ((rep)->syntax.direction() == FN_SYNTAX_STANDARD_DIRECTION_FLAT &&
	    (count()+1 > 1)) {
		*status = FN_E_ILLEGAL_NAME;
		return (0);
	}

	int r = (rep->comps.insert_item(iter_pos, new NameListItem(c)));
	if (!r)
		*status = FN_E_INSUFFICIENT_RESOURCES;
	return (r);
}

// delete component before iter_pos
int
FN_compound_name_standard::delete_comp(void *&iter_pos)
{
	return (rep->comps.delete_item(iter_pos));
}

// delete all components
int
FN_compound_name_standard::delete_all()
{
	return (rep->comps.delete_all());
}


// external C interface constructor
extern "C"
FN_compound_name_t*
standard__fn_compound_name_from_syntax_attrs(const FN_attrset_t *attrs,
	const FN_string_t *name,
	FN_status_t *status)
{
	const FN_attrset *ccattrs = (const FN_attrset *)attrs;
	const FN_string *ccname = (const FN_string *)name;
	FN_status *ccstat = (FN_status *)(status);
	FN_compound_name *std =
		FN_compound_name_standard::from_syntax_attrs(*ccattrs,
			*ccname, *ccstat);

	return ((FN_compound_name_t*)std);
}
