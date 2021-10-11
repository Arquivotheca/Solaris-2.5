/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_set_tab.c	1.14	95/07/11 SMI"	/* SVr4.0 1.7	*/

/*
* _set_tab - set _ctype[], _numeric[] to requested locale-based info.
*/

#include <locale.h>
#include "_locale.h"
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/*
 * wide character support
 */
extern struct _wctype *_wcptr[];
extern int _lflag;
extern int _cswidth[];
extern char *_loc_filename[];	/* defined in setlocale.c */

int
_set_tab(loc, cat)
const char *loc;
int cat;
{
	unsigned char my_ctype[SZ_TOTAL];
	unsigned char *space;
	int size;
	register int fd;
	register int ret = -1;
	int i;
        char *loc_path;

	/*
	 * Reset multibyte character table pointer
	 */
	if (cat == LC_CTYPE) 
	{
		_lflag = 0;
		for (i = 0; i < 3; i++)
			_wcptr[i] = 0;
		/* When even LC_CTYPE is changed, _cswidth structure */
		/* needs to be reset.				     */
		*_cswidth = 0;
	}

	size = (cat ? SZ_NUMERIC : SZ_TOTAL);
        loc_path = _fullocale(loc, _loc_filename[cat]);
	if (access(loc_path, R_OK) != 0 ||
            (fd = open(loc_path, O_RDONLY)) == -1) 
                return ret;
	else if (read(fd, (char *)my_ctype, size) == size) 
	{
		space = (cat ? _numeric : _ctype);
		(void)memcpy(space, my_ctype, size);
		ret = 0;
	}
	(void)close(fd);
	return ret;
}
