/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)m_mbstow.c 1.1	94/10/12 SMI"

/*
 *       wchar_t *m_mbstowcsdup(char *s)
 * (per strdup, only converting at the same time.)
 * Takes a multibyte string, figures out how long it will be in wide chars,
 * allocates that wide char string, copies to that wide char string.
 * returns (wchar_t *)0 on
 *       - out of memory
 *       - invalid multibyte character
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
static char rcsID[] = "$Header: /u/rd/src/libc/wide/RCS/m_mbstow.c,v 1.5 1993/08/23 11:36:50 mark Exp $";
#endif /*lint*/
#endif /*M_RCSID*/

#include "mks.h"
#include <stdlib.h>
#include <string.h>

wchar_t *
m_mbstowcsdup(const char *s)
{
	int n;
	wchar_t *w;

	n = strlen(s) + 1;
	if ((w = (wchar_t *)m_malloc(n * sizeof(wchar_t))) == NULL) {
		/* XXX	
		m_error(m_textmsg(3581, "!memory allocation failure", "E"));
		*/
		return(NULL);
	}

	if (mbstowcs(w, s, n) == -1) {
		/* XXX	
		m_error(m_textmsg(3642, "!multibyte string", "E"));
		*/
		return(NULL);
	}
	return w;
}
