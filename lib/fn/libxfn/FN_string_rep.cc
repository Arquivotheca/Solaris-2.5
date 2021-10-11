/*
 * Copyright (c) 1993 - 1994 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FN_string_rep.cc	1.4 94/08/03 SMI"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <synch.h>

#include <xfn/FN_status.hh>

#include "FN_string_rep.hh"


int	FN_string_rep::nnodes = 0;


FN_string_rep::FN_string_rep(unsigned long code_set)
{
	mutex_t r = DEFAULTMUTEX;

	++nnodes;

	p_code_set = code_set;
	p_native = 0;
	p_native_bytes = 0;
#if HAVE_LOCALE
	p_locale = 0;
	p_locale_bytes = 0;
#endif
	ref = r;
	refcnt = 1;
	// sts = FN_SUCCESS;
}

FN_string_rep::~FN_string_rep()
{
	zap_native();
#if HAVE_LOCALE
	if (p_locale_bytes > 0)
		delete[] p_locale;
#endif
	--nnodes;
}

#if HAVE_LOCALE
int
FN_string_rep::set_locale(const void *locale_info, size_t locale_bytes)
{
	if (locale_info && locale_bytes > 0) {
		p_locale = new char[locale_bytes];
		if (p_locale == 0)
			return (0);		// error
		p_locale_bytes = locale_bytes;
		memcpy(p_locale, locale_info, locale_bytes);
	}
	return (1);
}
#endif

const unsigned char *
FN_string_rep::as_str(size_t *native_bytes)
{
	if (p_native) {
		if (native_bytes)
			*native_bytes = p_native_bytes;
		return (p_native);
	}

	/*
	 * This is a no-op for regular chars.
	 */

	if (string_char::narrow(this)) {
		const unsigned char	*cp;

		cp = (const unsigned char *)contents();
		if (native_bytes)
			*native_bytes = charcount();
		return (cp);
	}

	/*
	 * Must reconstruct native representation, probably wiped out
	 * as a result of strcat, et. al..
	 */

	string_wchar	*wp;
	unsigned char	*native;
	size_t		max_bytes, mb_bytes;

	if ((wp = string_wchar::narrow(this)) == 0)
		return (0);

	// At most 4 bytes per character in EUC, plus '\0' terminator.
	max_bytes = wp->charcount() * 4 + 1;
	native = new unsigned char[max_bytes];
	if (native == 0)
		return (0);
	if ((mb_bytes = wcstombs((char *)native,
			(const wchar_t *)wp->contents(),
			wp->charcount())) == (size_t)-1) {
		delete[] native;
		return (0);
	}
	native[mb_bytes] = '\0';
	p_native = native;
	p_native_bytes = mb_bytes;
	if (native_bytes)
		*native_bytes = p_native_bytes;
	return (p_native);
}

void
FN_string_rep::zap_native()
{
	delete[] p_native;
	p_native = 0;
	p_native_bytes = 0;
}

unsigned long
#if HAVE_LOCALE
FN_string_rep::code_set(const void **linfo, size_t *lbytes) const
#else
FN_string_rep::code_set(const void **, size_t *) const
#endif
{
#if HAVE_LOCALE
	if (linfo)
		*linfo = p_locale;
	if (lbytes)
		*lbytes = p_locale_bytes;
#endif
	return (p_code_set);
}

FN_string_rep *
FN_string_rep::share()
{
	mutex_lock(&ref);
	refcnt++;
	mutex_unlock(&ref);
	return (this);
}

int
FN_string_rep::release()
{
	mutex_lock(&ref);
	int r = --refcnt;
	mutex_unlock(&ref);
	return (r);
}
