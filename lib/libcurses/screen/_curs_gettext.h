/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#ident        "@(#)_curs_gettext.h 1.1     93/05/05 SMI"

/* Header file for _curs_gettext() macro. */
#if !defined(TEXT_DOMAIN)	/* Should be defined thru -D flag. */
#	define	TEXT_DOMAIN	"SYS-TEST"
#endif

char * _dgettext(const char *, const char *);
#define _curs_gettext(msg_id)	_dgettext(TEXT_DOMAIN, msg_id)
