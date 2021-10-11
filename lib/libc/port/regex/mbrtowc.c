/*
 * mbrtowc - Not correct!
 * This interface is provided for the MKS regex port
 */

#ident	"@(#)mbrtowc.c 1.1	94/10/12 SMI"

#include <stdlib.h>
#include <wchar.h>
#include <mks.h>


size_t
mbrtowc(pwc, s, n, ps)
	wchar_t *pwc; char *s; size_t n; mbstate_t *ps;
{
	int ret;

	if (ps != NULL)
		*ps = 0;
	if (pwc == NULL)
		return (-1);

	ret = mbtowc(pwc, s, n);

	return (ret);

}
