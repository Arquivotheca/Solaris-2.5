/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)putenv.c	1.9	93/10/30 SMI"	/* SVr4.0 1.11	*/

/*	LINTLIBRARY	*/
/*	putenv - change environment variables

	input - const char *change = a pointer to a string of the form
				"name=value"

	output - 0, if successful
		 1, otherwise
*/
#ifdef __STDC__
	#pragma weak putenv = _putenv
#endif
#include "synonyms.h"
#include "shlib.h"
#include <string.h>
#include <stdlib.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#define	NULL 0
#define	CHAR (const char **)

extern const char **environ;		/* pointer to enviroment */
static reall = 0;		/* flag to reallocate space, if putenv */
				/* is called more than once */

#ifdef _REENTRANT
extern mutex_t __environ_lock;
#endif _REENTRANT

static int find(), match();

int
putenv(change)
const char *change;
{
	char **newenv;		    /* points to new environment */
	register int which;	    /* index of variable to replace */

	_mutex_lock(&__environ_lock);
	if ((which = find(change)) < 0)  {
		/* if a new variable */
		/* which is negative of table size, so invert and
		   count new element */
		which = (-which) + 1;
		if (reall)  {
			/* we have expanded environ before */
			newenv = (char **)realloc(environ,
				  which*sizeof(char *));
			if (newenv == NULL)  {
			        _mutex_unlock(&__environ_lock);
				return -1;
			}
			/* now that we have space, change environ */
			environ =  CHAR newenv;
		} else {
			/* environ points to the original space */
			reall++;
			newenv = (char **)malloc(which*sizeof(char *));
			if (newenv == NULL)  {
			        _mutex_unlock(&__environ_lock);
				return -1;
			}
			(void)memcpy((char *)newenv, (char *)environ,
 				(int)(which*sizeof(char *)));
			environ = CHAR newenv;
		}
		environ[which-2] = change;
		environ[which-1] = NULL;
	}  else  {
		/* we are replacing an old variable */
		environ[which] = change;
	}
	_mutex_unlock(&__environ_lock);
	return 0;
}

/*	find - find where s2 is in environ
 *
 *	input - str = string of form name=value
 *
 *	output - index of name in environ that matches "name"
 *		 -size of table, if none exists
*/
static
find(str)
register char *str;
{
	register int ct = 0;	/* index into environ */

	while(environ[ct] != NULL)   {
		if (match(environ[ct], str)  != 0)
			return ct;
		ct++;
	}
	return -(++ct);
}
/*
 *	s1 is either name, or name=value
 *	s2 is name=value
 *	if names match, return value of 1,
 *	else return 0
 */

static
match(s1, s2)
register char *s1, *s2;
{
	while(*s1 == *s2++)  {
		if (*s1 == '=')
			return 1;
		s1++;
	}
	return 0;
}
