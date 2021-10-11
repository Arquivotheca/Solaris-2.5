/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)m_wcstom.c 1.1	94/10/12 SMI"

/*
 *       char *m_wcstombsdup(wchar_t *s)
 * (per strdup, only converting at the same time.)
 * Takes a wide string, figures out how long it will be in mb chars,
 * allocates that mb char string, copies to that mb char string.
 * Returns NULL on
 *       - out of memory
 *       - invalid multibyte character
 * On NULL return, m_error message has been issued.
 * Caller must free returned memory by calling free.
 *
 * Copyright 1992 by Mortice Kern Systems Inc.  All rights reserved.
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
static char rcsID[] = "$Header: /u/rd/src/libc/wide/RCS/m_wcstom.c,v 1.5 1993/08/23 11:36:52 mark Exp $";
#endif /*lint*/
#endif /*M_RCSID*/

#include "mks.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

char *
m_wcstombsdup(const wchar_t *w)
{
	int n;
	char *mb;
	extern char *_cmdname;

	/* Fetch memory for worst case string length */
	n = wcslen(w) + 1;
	n *= MB_CUR_MAX;
	if ((mb = (char *)m_malloc(n)) == NULL) {
		/* XXX
		m_error(m_textmsg(3581, "!memory allocation failure", "E"));
		*/
		return(NULL);
	}

	/* Convert the string */
	if ((n = wcstombs(mb, w, n)) == -1) {
		int saverr = errno;

		/* XXX
		m_error(m_textmsg(3642, "!multibyte string", "E"));
		*/
		free(mb);
		errno = saverr;
		return(0);
	}

	/* Shrink the string down */
	if ((mb = (char *)m_realloc(mb, strlen(mb)+1)) == NULL)  {
		/* XXX
		m_error(m_textmsg(3581, "!memory allocation failure", "E"));
		*/
		return(NULL);
	}
	return mb;
}
