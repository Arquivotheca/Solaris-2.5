/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)regerror.c 1.3	95/02/06 SMI"


/*
 * regerror: map error number to text string
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
static char rcsID[] = "$Header: /u/rd/src/libc/regex/rcs/regerror.c 1.28 1994/11/07 14:40:06 jeffhe Exp $";
#endif
#endif

#include "mks.h"
#include <regex.h>
#include <string.h>

/*
 * Solaris porting mods
 */
#if !defined(TEXT_DOMAIN)	/* Should be defined thru -D flag. */
#define  TEXT_DOMAIN	"SYS-TEST"
#endif

char * _dgettext(const char *, const char *);
#define _libc_gettext(msg_id)   _dgettext(TEXT_DOMAIN, msg_id)
#undef	m_textstr
#define	m_textstr(a,b,c)	(b)
#define	m_textmsg(id, str, cls)	_libc_gettext(str)
/*
 * Error messages for regerror
 */
static char *regerrors[] = {
m_textstr(646, "success", "I"),				/* REG_OK */
m_textstr(647, "failed to match", "E"),			/* REG_NOMATCH */
m_textstr(648, "invalid collation element", "E"),	/* REG_ECOLLATE */
m_textstr(649, "trailing \\ in pattern", "E"),		/* REG_EESCAPE */
m_textstr(650, "newline found before end of pattern", "E"),/* REG_ENEWLINE */
"",							/* REG_ENSUB (OBS) */
m_textstr(652, "number in \\[0-9] invalid", "E"),	/* REG_ESUBREG */
m_textstr(653, "[ ] imbalance or syntax error", "E"),	/* REG_EBRACK */
m_textstr(654, "( ) or \\( \\) imbalance", "E"),		/* REG_EPAREN */
m_textstr(655, "{ } or \\{ \\} imbalance", "E"),		/* REG_EBRACE */
m_textstr(656, "invalid endpoint in range", "E"),	/* REG_ERANGE */
m_textstr(133, "out of memory", "E"),			/* REG_ESPACE */
m_textstr(5031, "?, *, +, or { } not preceded by valid regular expression", "E"),
							/* REG_BADRPT */
m_textstr(658, "invalid character class type", "E"),	/* REG_ECTYPE */
m_textstr(659, "syntax error", "E"),			/* REG_BADPAT */
m_textstr(660, "contents of { } or \\{ \\} invalid", "E"),/* REG_BADBR */
m_textstr(661, "internal error", "E"),			/* REG_EFATAL */
m_textstr(3366, "invalid multibyte character", "E"),	/* REG_ECHAR */
m_textstr(3641,
"backtrack stack overflow: expression generates too many alternatives", "E"),
							/* REG_STACK */
};

/*f
 * Map error number to text message.
 * preg is supplied to allow an error message with perhaps pieces of
 * the offending regular expression embeded in it.
 * preg is permitted to be zero, results still have to be returned.
 * In this implementation, preg is currently unused.
 * The string is returned into errbuf, which is errbuf_size bytes
 * long, and is possibly truncated.  If errbuf_size is zero, the string
 * is not returned.  The length of the error message is returned.
 */
size_t
regerror(errcode, preg, errbuf, errbuf_size)
int errcode;
const regex_t *preg;
char *errbuf;
size_t errbuf_size;
{
	char *s;

	if (errcode < REG_OK || errcode >= REG__LAST)
		s = m_textmsg(662, "unknown regex error", "E");
	else
		s = m_strmsg(regerrors[errcode]);
	if (errbuf_size != 0) {
		strncpy(errbuf, s, errbuf_size);
		errbuf[errbuf_size-1] = '\0';
	}
	return strlen(s) + 1;
}
