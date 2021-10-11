/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */

#ident  "@(#)m_cclass.c 1.7     95/10/05 SMI"

/*
 *
 * Copyright 1985, 1992 by Mortice Kern Systems Inc.  All rights reserved.
 *
 * This Software is unpublished, valuable, confidential property of
 * Mortice Kern Systems Inc.  Use is authorized only in accordance
 * with the terms and conditions of the source licence agreement
 * protecting this Software.  Any unauthorized use or disclosure of
 * this Software is strictly prohibited and will result in the
 * termination of the licence agreement.
 *
 * If you have any questions, please consult your supervisor.
 *
 */
#ifdef M_RCSID
#ifndef lint
static char rcsID[] = "$Header: /u/rd/src/libc/mks/RCS/m_cclass.c,v 1.8 1992/06/19 13:28:17 gord Exp $";
#endif
#endif

#include "mks.h"
#include <ctype.h>
#include <string.h>
#include <locale.h>
#include "_libcollate.h"

#ifdef isprint
#undef isprint
#endif /* isprint */

#ifdef isblank
#undef isblank
#endif /* isblank */

#define isprint(c)	(((__ctype + 1)[c] & (_P | _U | _L | _N | _B)) && \
			!((__ctype + 1)[c] & _C ))
extern _coll_info *_loaded_coll_;
 
#define FIX_WHEN_ISBLANKS_DONE
#ifdef FIX_WHEN_ISBLANKS_DONE /* WARNING! See the comments for isblank() */
static int	isblank(m_collel_t c);
static int	___iswblank(wint_t c);
#endif /* FIX_WHEN_ISBLANKS_DONE */

/*f
 * Convert a character class name into a string of its member characters.
 * Return is count of # of entries in the character class; -1 indicates
 * invalid input (unknown class).
 * Other return is final arg, which is a pointer to the list of the
 * characters (in collating-element format) in the class.
 */
LDEFN int
m_cclass(name, list)
char *name;
m_collel_t **list;
{
	int index, member;
	static m_collel_t *buf = NULL;
	m_collel_t *cp;
	m_collel_t c;
	int csetsize;
	wchar_t wc;
	wctype_t iswhat;
	int is_a_blank = 0;

	if ((csetsize = m_maxcoll()) <= 0)
		return (-1);

	if (buf != NULL)
		free(buf);

	if ((buf =
	    (m_collel_t *) malloc(csetsize * sizeof (m_collel_t))) == NULL)
		return (-1);

	cp = buf;

	/*
	 * Check special case for [:blank:]
	 * Note, this should be fixed when we get a real isblank()
	 */
	if (strcmp(name, "blank") == 0)
		is_a_blank = 1;
	else if ((iswhat = wctype(name)) <= 0)
		return (-1);		/* unknown class */

	for (c = 0; c <= csetsize; c++) {
		/*
		 * Multi character collating elements are undefined in
		 * character class expressions, per posix 1003.2
		 */
		if (_m_ismccollel(c))
			continue;

		if ((wc = _cetowc(_loaded_coll_, c)) < 0)
			continue;

		/*
		 * handle special case for blank first
		 */
		if (is_a_blank == 1)
			member = ___iswblank(wc);
		else
			member = iswctype(wc, iswhat);

		if (member)
			*cp++ = c;
	}
	*list = buf;
	return (cp-buf);
}


#ifdef FIX_WHEN_ISBLANKS_DONE
/*
 * isblank():	see if a character is a blank.
 *	input:	character to be examined.
 *	output:	non-zero is character is a blank; zero otherwise.
 *	description:
 *		the purpose of this routine is to check a character to
 *	see if it's a space or a tab, in an international fashion.
 *
 * N.B.! at the time that this routine was written for spec1170 compliance,
 *	we didn't have an isblank() routine. isspace() comes close, but
 *	checks for more characters than space or tab. so, in order to get
 *	this operational in an international fashion, i've had to resort
 *	to hand crafting this routine. it's a total and temporary hack,
 *	and a bugid will be raised to correct it for the future
 */

static int
isblank(c)
m_collel_t c;
{
	return ((c == (char) 0x09 || c == (char) 0x20) ? 1 : 0);
}

static int
___iswblank(wint_t c)
{
	if (c > 255)
		return (iswctype((wchar_t) c, _B));
	else
		return (isblank(c));
}
#endif /* FIX_WHEN_ISBLANKS_DONE */
