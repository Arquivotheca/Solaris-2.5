/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)_locale.c	1.16	95/07/11 SMI"	/* SVr4.0 1.5	*/

#include "synonyms.h"
#include <locale.h>
#include "_locale.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

/* return value for category with "" locale */
char *
_nativeloc(cat)
int cat;
{
	static  char lang[] = "LANG";
	static  char *_loc_envs[LC_ALL][4] = /* env. vars for "" */
	{
		{"LC_CTYPE",	lang, 	"CHRCLASS",	0},
		{"LC_NUMERIC",	lang,	0},
		{"LC_TIME",	lang,	"LANGUAGE",	0},
		{"LC_COLLATE",	lang,	0},
		{"LC_MONETARY",	lang,	0},
		{"LC_MESSAGES",	lang,	0},
	};
	static char ans[LC_NAMELEN];
	register char *s;
	register char  **p;
#define SAFEVALUE(s) (strstr((s), "..") == 0)
 
	if ((s = getenv("LC_ALL")) != 0 && s[0] != '\0' && SAFEVALUE(s))
		goto found;
	for (p = _loc_envs[cat]; *p != 0; p++)
		if ((s = getenv(*p)) != 0 && s[0] != '\0' && SAFEVALUE(s))
			goto found;
	(void) strcpy (ans, "C");
	return ans;
found:
	(void)strncpy(ans, s, LC_NAMELEN - 1);
	ans[LC_NAMELEN - 1] = '\0';
	return ans;
}

char *
_fullocale(loc, file)	/* "/usr/lib/locale/<loc>/<file>" */
const char *loc, *file;
{
	static char ans[18 + 2 * LC_NAMELEN] = "/usr/lib/locale/";
	register char *p = ans + 16;

	(void)strcpy(p, loc);
	p += strlen(loc);
	p[0] = '/';
	(void)strcpy(p + 1, file);
	return ans;
}
