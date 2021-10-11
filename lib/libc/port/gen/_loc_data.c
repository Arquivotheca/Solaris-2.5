/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_loc_data.c	1.12	92/07/14 SMI"	/* SVr4.0 1.10	*/

#include <stdlib.h>
#include "synonyms.h"
#include <locale.h>
#include "_locale.h"
#include <wctype.h>
char _cur_locale[LC_ALL][LC_NAMELEN] = /* current locale names */
{
	"C", "C", "C", "C", "C", "C"		/* need a repeat count feature */
};

unsigned char _numeric[SZ_NUMERIC] =
{
	'.',	'\0',
};

/* _lflag=1 if multi-byte character table is updated */
int _lflag = 0;

struct _wctype _wcptr[3] = {
	{0,     0,      0,      0,      0,      0,      0},
	{0,     0,      0,      0,      0,      0,      0},
	{0,     0,      0,      0,      0,      0,      0}
};
