/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)putpwent.c	1.12	94/05/13 SMI"	/* SVr4.0 1.10	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
/*
 * format a password file entry
 */
#ifdef __STDC__
#pragma weak putpwent = _putpwent
#endif
#include "synonyms.h"
#include <stdio.h>
#include <pwd.h>

int
putpwent(p, f)
#ifdef __STDC__
register const struct passwd *p;
#else
register const struct passwd *p;
#endif
register FILE *f;
{
	int black_magic = 0;

	(void) fprintf(f, "%s:%s", p->pw_name,
	    p->pw_passwd ? p->pw_passwd : "");
	if (((p->pw_age) != (char *)0) && ((*p->pw_age) != '\0'))
		(void) fprintf(f, ",%s", p->pw_age); /* fatal "," */
	black_magic = (*p->pw_name == '+' || *p->pw_name == '-');
	/* leading "+/-"  taken from getpwnam_r.c */
	if (black_magic) {
		(void) fprintf(f, ":::%s:%s:%s",
			p->pw_gecos ? p->pw_gecos : "",
			p->pw_dir ? p->pw_dir : "",
			p->pw_shell ? p->pw_shell : "");
	} else { /* "normal case" */
		(void) fprintf(f, ":%ld:%ld:%s:%s:%s",
			p->pw_uid,
			p->pw_gid,
			p->pw_gecos,
			p->pw_dir,
			p->pw_shell);
	}
	(void) putc('\n', f);
	(void) fflush(f);
	return (ferror(f));
}
