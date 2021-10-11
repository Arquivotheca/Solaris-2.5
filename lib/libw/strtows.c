/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)strtows.c	1.1	93/11/12 SMI"

#include <limits.h>
#include <widec.h>
#include <errno.h>

wchar_t *
strtows(wchar_t *s1, char *s2)
{
	int ret;
	extern int errno;

	ret = mbstowcs(s1, s2, TMP_MAX);
	if (ret == -1) {
		errno = EILSEQ;
		return (NULL);
	}
	return (s1);
}

char *
wstostr(char *s1, wchar_t *s2)
{
	int ret;
	extern int errno;

	ret = wcstombs(s1, s2, TMP_MAX);
	if (ret == -1) {
		errno = EILSEQ;
		return (NULL);
	}
	return (s1);
}
