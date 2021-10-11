/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#ident	"@(#)wcsftime.c	1.2	94/02/14 SMI"

#include <wchar.h>

size_t wcsftime(
	wchar_t		*wcs,
	size_t		maxsize,
	const char	*format,
	const struct tm	*timeptr)
{
	size_t	ret;
	char	*strftime_out;

	if ((strftime_out = (char*)malloc(maxsize * sizeof(char))) == NULL)
		return(0);

	if ((ret = strftime(strftime_out, maxsize, format, timeptr)) == NULL)
		return(0);

	if ((ret = mbstowcs(wcs, strftime_out, maxsize)) == -1)
		ret = 0;

	return(ret);
}
