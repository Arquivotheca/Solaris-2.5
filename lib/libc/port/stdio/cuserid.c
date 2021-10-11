/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)cuserid.c	1.10	94/08/08 SMI"	/* SVr4.0 1.14	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
#ifdef __STDC__
#pragma weak cuserid = _cuserid
#endif
#include "synonyms.h"
#include <stdio.h>
#include <pwd.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static char res[L_cuserid];

char *
cuserid(s)
	char	*s;
{
	struct passwd pwd;
	register struct passwd *pw;
	char utname[L_cuserid];
	register char *p;

	if (s == 0)
		s = res;
	p = getlogin_r(utname, L_cuserid);
	s[L_cuserid - 1] = 0;
	if (p != 0)
		return (strncpy(s, p, L_cuserid - 1));
	pw = getpwuid(getuid());
	if (pw != 0)
		return (strncpy(s, pw->pw_name, L_cuserid - 1));
	*s = '\0';
	return (0);
}
