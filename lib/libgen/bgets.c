/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)bgets.c	1.12	93/11/10 SMI"	/* SVr4.0 1.1.3.2	*/

/*	read no more than <count> characters into <buf> from stream <fp>, */
/*	stoping at any character slisted in <stopstr>.			  */
/*	NOTE: This function will not work for multi-byte characters.	  */

#pragma weak bgets = _bgets

#include "synonyms.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include "mtlib.h"

#define	CHARS	256

#ifdef _REENTRANT
#define	getc(f) getc_unlocked(f)
#else /* _REENTRANT */
static char	*stop = NULL;
#endif /* _REENTRANT */

#ifdef _REENTRANT
static char *
_get_stop(thread_key_t *key)
{
	char *str = NULL;

	if (_thr_getspecific(*key, &str) != 0) {
		if (_thr_keycreate(key, free) != 0) {
			return (NULL);
		}
	}
	if (!str) {
		if (_thr_setspecific(*key, (void *)(str = calloc(CHARS,
			sizeof (char)))) != 0) {
			if (str)
				(void) free(str);
			return (NULL);
		}
	}
	return (str);
}
#endif /* _REENTRANT */

char *
bgets(buf, count, fp, stopstr)
char	*buf;
register
size_t	count;
FILE	*fp;
char	*stopstr;
{
	register char	*cp;
	register int	c;
	register size_t i;
#ifdef _REENTRANT
	static thread_key_t key = 0;
	char  *stop  = _get_stop(&key);
#else /* _REENTRANT */
	if (! stop)
	    stop = (char *)calloc(CHARS, sizeof (char));
	else
#endif /* _REENTRANT */
	if (stopstr) 	/* reset stopstr array */
		memset(stop, 0, CHARS);
	if (stopstr)
		for (cp = stopstr; *cp; cp++)
			stop[(unsigned char)*cp] = 1;
	i = 0;
	FLOCKFILE(fp);
	cp = buf;
	for (;;) {
		if (i++ == count) {
			*cp = '\0';
			break;
		}
		if ((c = getc(fp)) == EOF) {
			*cp = '\0';
			if (cp == buf)
				cp = (char *) 0;
			break;
		}
		*cp++ = c;
		if (stop[c]) {
			*cp = '\0';
			break;
		}
	}
	FUNLOCKFILE(fp);
	return (cp);
}
