/*
 * Copyright (c) 1992 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FN_ref_addr.cc	1.8 94/10/31 SMI"

#include <memory.h>
#include <synch.h>

#include <xfn/FN_string.hh>
#include <xfn/FN_identifier.hh>
#include <xfn/FN_ref_addr.hh>


class FN_ref_addr_rep {
public:
	FN_identifier type;
	int length;
	void *data;

	FN_ref_addr_rep(const FN_identifier &type, int l, const void *d);
	~FN_ref_addr_rep();

	// manage ref count
	FN_ref_addr_rep *share();
	int release();

#if 0
	friend const FN_string *fns_default_address_description(
	    const FN_ref_addr &addr,
	    unsigned d,
	    unsigned *md,
	    const FN_string *msg);
#endif

private:
	mutex_t ref;
	int refcnt;
};

FN_ref_addr_rep::FN_ref_addr_rep(const FN_identifier &t, int l, const void *d)
	: type(t), length(l)
{
	mutex_t r = DEFAULTMUTEX;

	if (l) {
		data = new char[l];
		memcpy(data, d, l);
	} else
	    data = 0;
	ref = r;
	refcnt = 1;
}

FN_ref_addr_rep::~FN_ref_addr_rep()
{
	delete[] data;
}

FN_ref_addr_rep *
FN_ref_addr_rep::share()
{
	mutex_lock(&ref);
	refcnt++;
	mutex_unlock(&ref);
	return (this);
}

int
FN_ref_addr_rep::release()
{
	mutex_lock(&ref);
	int r = --refcnt;
	mutex_unlock(&ref);
	return (r);
}


FN_ref_addr::FN_ref_addr(FN_ref_addr_rep *r)
	: rep(r)
{
}

FN_ref_addr_rep *
FN_ref_addr::get_rep(const FN_ref_addr &a)
{
	return (a.rep);
}

// default constructor disallowed
FN_ref_addr::FN_ref_addr()
{
}

// constructor
FN_ref_addr::FN_ref_addr(const FN_identifier &t, unsigned l, const void *d)
{
	rep = new FN_ref_addr_rep(t, l, d);
}

FN_ref_addr::~FN_ref_addr()
{
	if (rep && rep->release() == 0)
		delete rep;
}

// copy and assignment
FN_ref_addr::FN_ref_addr(const FN_ref_addr &a)
{
	rep = get_rep(a)->share();
}

FN_ref_addr &
FN_ref_addr::operator=(const FN_ref_addr &a)
{
	if (&a != this) {
		if (rep->release() == 0)
			delete rep;
		rep = get_rep(a)->share();
	}
	return (*this);
}

// address type
const FN_identifier *
FN_ref_addr::type() const
{

	return (&(rep->type));
}

// length of address data in octets
unsigned
FN_ref_addr::length() const
{
	return (rep->length);
}

// address data
const void *
FN_ref_addr::data() const
{
	return (rep->data);
}

#include <stdio.h>
#include <sys/param.h>
#include <string.h>
#include <ctype.h>

#include "fns_symbol.hh"


// %%% not ready for this yet
// %%% not MT safe

// get default description of address
// 	d>=0 --> address type
// 	d==1 --> + length + 1st 100 bytes of data
// 	d>=2 --> + length + all data

FN_string *
fns_default_address_description(const FN_ref_addr &addr,
				unsigned d,
				unsigned *md,
				const FN_string *msg)
{
	const FN_identifier *tp = addr.type();
	unsigned int status;
	int l = addr.length();
	int size = 72; // overhead
	const unsigned char *addr_type_str = 0;
	if (msg)
	    size += msg->charcount();
	if (tp) {
	    size += tp->length(); // type string
	    addr_type_str = tp->str(&status);
	}
	size += ((l / 10) + 1) * 70; // formatted data
	char *buf = new char[size];

	sprintf(buf, "Address type: %s",
		addr_type_str ? (char *)addr_type_str : "");
	char *bp = buf + strlen(buf);
	if (msg) {
		sprintf(bp, "\n  Warning: %s", msg->str(&status));
		bp = bp + strlen(bp);
	}

	if (d > 0) {
		sprintf(bp, "\n  length: %d\n  data: ", l);
		bp = bp + strlen(bp);

		int dl;
		if (d == 1 && l > 100) {
			dl = 100;
			if (md)
			    *md = 2;
		} else {
			dl = l;
			if (md)
			    *md = d;
		}

		int i, j;
		const unsigned char *p;
		for (p = (const unsigned char *)(addr.data()), i = 0; i < dl;) {
			for (j = 0; (j < 10) && ((i + j) < dl); j++) {
				sprintf(bp, "0x%.2x ", p[i + j]);
				bp += 5;
			};
			for (; j < 10; j++) {
				strcpy(bp, "     ");
				bp += 5;
			};
			*bp++ = ' ';
			for (j = 0; j < 10 && i < dl; j++, i++)
			    *bp++ = (isprint(p[i]))?p[i]:'.';
			if (i < dl) {
				strcpy(bp, "\n        ");
				bp += 9;
			};
		}

		if (dl < l) {
			strcpy(bp, "\n        ...");
			bp += 12;
		};
	} else {
		if (md)
		    *md = 1;
	}

	*bp++ = '\n';
	*bp = 0;

	FN_string *desc = new FN_string((const unsigned char *)buf);
	delete[] buf;
	return (desc);
}

typedef FN_string_t *(*description_func)(const FN_ref_addr_t*,
    unsigned detail, unsigned *more_detail);

// get description of address
FN_string *
FN_ref_addr::description(unsigned int d, unsigned int *md) const
{
	const FN_identifier	*tp;
	void			*fh;
	char			mname[MAXPATHLEN], fname[MAXPATHLEN];

	const FN_ref_addr_t	* myaddr = (FN_ref_addr_t*)this;
	const unsigned char	*addr_type_str;

	if ((tp = type()) && (addr_type_str = tp->str())) {
		sprintf(fname, "%s__fn_ref_addr_description",
			addr_type_str);
		// look in executable (and linked libraries)
		if (fh = fns_link_symbol(fname))
		    return ((FN_string *)
			    (*((description_func)fh))(myaddr, d, md));

		// look in loadable module
		sprintf(mname, "fn_ref_addr_%s.so", addr_type_str);
		if (fh = fns_link_symbol(fname, mname))
		    return ((FN_string *)
			    (*((description_func)fh))(myaddr, d, md));
	}

	return ((FN_string *)fns_default_address_description(*this, d, md, 0));
}
