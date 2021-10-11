/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma ident "@(#)FN_string_c.cc	1.3 94/07/31 SMI"

#include <stdarg.h>

#include <xfn/FN_string.hh>
#include <xfn/FN_status.h>

/*
 * This is an implementation of the draft XFN FN_string data type.
 *
 * Note:
 *	All of the real work gets done in C++.
 *
 * Warning:
 *	This is incomplete.
 *	wchar_t and codeset unimplemented.
 */

extern "C"
FN_string_t *
fn_string_create(void)
{
	return ((FN_string_t *)new FN_string);
}

extern "C"
void
fn_string_destroy(FN_string_t *s)
{
	delete (FN_string *)s;
}

extern "C"
FN_string_t *
fn_string_from_str(const unsigned char *str)
{
	return ((FN_string_t *)new FN_string(str));
}

extern "C"
FN_string_t *
fn_string_from_str_n(const unsigned char *str, size_t len)
{
	return ((FN_string_t *)new FN_string(str, len));
}

extern "C"
FN_string_t *
fn_string_from_contents(unsigned long code_set,
			const void *locale_info,
			size_t locale_info_len,
			size_t charcount,
			size_t bytecount,
			const void *contents,
			unsigned int *status)
{
	FN_string_t *answer = (FN_string_t *)new FN_string(code_set,
	    locale_info, locale_info_len, charcount, bytecount,
	    contents, status);

	return (answer);
}

extern "C"
unsigned long
fn_string_code_set(
	const FN_string_t *p,
	const void **locale_info,
	size_t *locale_info_len)
{
	return (((const FN_string *)p)->code_set(locale_info, locale_info_len));
}


extern "C"
size_t
fn_string_bytecount(const FN_string_t *s)
{
	return (((const FN_string *)s)->bytecount());
}

extern "C"
size_t
fn_string_charcount(const FN_string_t *s)
{
	return (((const FN_string *)s)->bytecount());
}

extern "C"
const void *
fn_string_contents(const FN_string_t *s)
{
	return (((const FN_string *)s)->contents());
}

extern "C"
const unsigned char *
fn_string_str(const FN_string_t *str, unsigned int *status)
{
	const unsigned char *answer = (((const FN_string *)str)->str(status));
	return (answer);
}

extern "C"
FN_string_t *
fn_string_copy(const FN_string_t *s)
{
	return ((FN_string_t *)new FN_string(*(const FN_string *)s));
}

extern "C"
FN_string_t *
fn_string_assign(FN_string_t *dst, const FN_string_t *src)
{
	*(FN_string *)dst = *(const FN_string *)src;
	return (dst);
}

extern "C"
FN_string_t *
fn_string_from_strings(
		unsigned int *status,
		const FN_string_t *s1,
		const FN_string_t *s2,
		...)
{
	*status = FN_SUCCESS;
	unsigned int s;

	if (s1 == 0) {
		return ((FN_string_t *)new FN_string);
	}
	if (s2 == 0) {
		return ((FN_string_t *)new FN_string(*((FN_string *)s1)));
	}

	// the slow and painful (yet portable) approach
	FN_string *ret = new FN_string(*((FN_string *)s1));
	FN_string *n = new FN_string(&s, ret, (FN_string *)s2, (FN_string *)0);
	delete ret;
	ret = n;

	if (s != FN_SUCCESS) {
		*status = s;
		delete ret;
		return (0);
	}

	va_list ap;
	void *sn;
	va_start(ap, s2);
	while (ret && (sn = va_arg(ap, void *))) {
		FN_string *tn = new FN_string(&s, ret, (FN_string *)sn,
								(FN_string *)0);
		delete ret;
		ret = tn;
		if (s != FN_SUCCESS)
			break;
	};
	va_end(ap);

	if (s != FN_SUCCESS) {
		*status = s;
		delete ret;
		return (0);
	}

	return ((FN_string_t *)ret);
}

extern "C"
FN_string_t *
fn_string_from_substring(const FN_string_t *s, int first, int last)
{
	return ((FN_string_t *)new
		FN_string(*(const FN_string *)s, first, last));
}

extern "C"
int
fn_string_is_empty(const FN_string_t *s)
{
	return (((const FN_string *)s)->is_empty());
}


extern "C"
int
fn_string_compare(
	const FN_string_t *s1,
	const FN_string_t *s2,
	unsigned int string_case,
	unsigned int *status)
{
	int answer = ((const FN_string *)s1)->compare(*(const FN_string *)s2,
							string_case, status);
	return (answer);
}

extern "C"
int
fn_string_compare_substring(
	const FN_string_t *s1,
	int first,
	int last,
	const FN_string_t *s2,
	unsigned int string_case,
	unsigned int *status)
{
	int answer = ((const FN_string *)s1)->compare_substring(first, last,
				*(const FN_string *)s2, string_case, status);

	return (answer);
}

extern "C"
int
fn_string_next_substring(
	const FN_string_t *str,
	const FN_string_t *sub,
	int index,
	unsigned int string_case,
	unsigned int *status)
{
	int answer = ((const FN_string *)str)->next_substring(
			*(const FN_string *)sub, index, string_case, status);
	return (answer);
}

extern "C"
int
fn_string_prev_substring(
	const FN_string_t *str,
	const FN_string_t *sub,
	int index,
	unsigned int string_case,
	unsigned int *status)
{
	int answer = ((const FN_string *)str)->prev_substring(
			*(const FN_string *)sub, index, string_case, status);
	return (answer);
}
