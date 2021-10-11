/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef	_SYSLOG_H
#define	_SYSLOG_H

#pragma ident	"@(#)syslog.h	1.8	93/11/01 SMI"	/* SVr4.0 1.1	*/

#include <sys/syslog.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	__STDC__
/* Allow old varargs style iff varargs.h was ***previously*** included.  */
#ifndef	_VARARGS_H
#include <stdarg.h>
#endif
void openlog(const char *, int, int);
void syslog(int, const char *, ...);
void vsyslog(int, const char *, va_list);
void closelog(void);
int setlogmask(int);
#else	/* __STDC__ */
void openlog();
void syslog();
void vsyslog();
void closelog();
int setlogmask();
#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYSLOG_H */
