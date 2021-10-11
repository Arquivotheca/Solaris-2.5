#ident	"@(#)yp_stubs.c	1.8	92/07/14 SMI"

/*
 *+++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 *  		PROPRIETARY NOTICE (Combined)
 *  
 *  This source code is unpublished proprietary information
 *  constituting, or derived under license from AT&T's UNIX(r) System V.
 *  In addition, portions of such source code were derived from Berkeley
 *  4.3 BSD under license from the Regents of the University of
 *  California.
 *  
 *  
 *  
 *  		Copyright Notice 
 *  
 *  Notice of copyright on this source code product does not indicate 
 *  publication.
 *  
 *  	(c) 1986,1987,1988,1989,1990  Sun Microsystems, Inc
 *  	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 *  	          All rights reserved.
 */

/*
 * yp_stubs.c
 *
 * This is the interface to NIS library calls in libnsl, that
 * are made through dlopen() and dlsym() to avoid linking
 * libnsl. The primary reason for this file is to offer access
 * to yp_*() calls from within various libc routines that
 * use NIS password and shadow databases. Preferably, a cleaner
 * way should be found to accomplish inter-library dependency.
 */

#include "synonyms.h"
#include "_libc_gettext.h"
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <sys/file.h>
#include <rpcsvc/ypclnt.h>
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>

#define YPSTUB_OK 0
#define YPSTUB_NOMEM 1
#define YPSTUB_NOSYM 2
#define YPSTUB_OPEN 3
#define YPSTUB_ACCESS 4
#define YPSTUB_SYSTEM 5

static char lnsl[]= "/usr/lib/libnsl.so";
static int ypstub_err = YPSTUB_OK;
static char *yperrbuf;

typedef struct translator {
	int		(*ypdom)();	/* yp_get_default_domain */
	int		(*ypfirst)();	/* yp_first */
 	int		(*ypnext)();	/* yp_next	*/
	int		(*ypmatch)();	/* yp_match */
	void	*tr_fd;	/* library descriptor */
	char	tr_name[512];	/* Full path	*/
} translator_t;

static translator_t *t = NULL;
static translator_t *load_xlate();
static char *_buf();
static char *ypstub_sperror();
static ypstub_perror();
extern char *malloc();

int libc_yp_get_default_domain();
int libc_yp_first();
int libc_yp_next();
int libc_yp_match();

int
libc_yp_get_default_domain(domain)
char **domain;
{
	int retval;
	if (t == NULL) {
		if ((t = load_xlate(lnsl)) == NULL) {
		/*
		 * We don't print this to avoid annoyance to the user,
		 * other libc_yp routines will anyway do that job.
		 */
#ifdef DEBUG
			ypstub_perror("NIS access from libc routines");
#endif
			return(YPERR_YPERR);
		}
	}
	retval =(*(t->ypdom))(domain);
	return(retval);
}

int
libc_yp_first(domain, map, key, keylen, val, vallen)
	char *domain;
	char *map;
	char **key;		/* return: key array */
	int  *keylen;		/* return: bytes in key */
	char **val;		/* return: value array */
	int  *vallen;		/* return: bytes in val */
{
	int retval;
	if (t == NULL) {
		if ((t = load_xlate(lnsl)) == NULL) {
			ypstub_perror("NIS access from libc routines");
			return(YPERR_YPERR);
		}
	}
	retval =(*(t->ypfirst))(domain, map, key, keylen, val, vallen);
	return(retval);
}

int
libc_yp_next(domain, map, inkey, inkeylen, outkey, outkeylen, val, vallen)
	char *domain;
	char *map;
	char *inkey;
	int  inkeylen;
	char **outkey;		/* return: key array associated with val */
	int  *outkeylen;	/* return: bytes in key */
	char **val;		/* return: value array associated with outkey */
	int  *vallen;		/* return: bytes in val */
{
	int retval;
	if (t == NULL) {
		if ((t = load_xlate(lnsl)) == NULL) {
			ypstub_perror("NIS access from libc routines");
			return(YPERR_YPERR);
		}
	}
	retval =(*(t->ypnext))(domain, map, inkey, inkeylen,
			       outkey, outkeylen, val, vallen);
	return(retval);
}

int
libc_yp_match(domain, map, key, keylen, val, vallen)
	char *domain;
	char *map;
	char *key;
	register int  keylen;
	char **val;		/* returns value array */
	int  *vallen;		/* returns bytes in val */
{
	int retval;
	if (t == NULL) {
		if ((t = load_xlate(lnsl)) == NULL) {
			ypstub_perror("NIS access from libc routines");
			return(YPERR_YPERR);
		}
	}
	retval =(*(t->ypmatch))(domain, map, key, keylen, val, vallen);
	return(retval);
}

/*
 * load_xlate is a routine that will attempt to dynamically link in
 * libnsl for network access from within libc. 
 */
static translator_t *
load_xlate(name)
	char	*name;		/* file name to load */
{
	/* do a sanity check on the file ... */
	if (access(name, F_OK) == 0) {
		t = (translator_t *) malloc(sizeof(translator_t));
		if (!t) {
			ypstub_err = YPSTUB_NOMEM;
			return (0);
		}

		strcpy(t->tr_name, name);

		/* open for linking */
		t->tr_fd = dlopen(name, RTLD_LAZY);
		if (t->tr_fd == NULL) {
			ypstub_err = YPSTUB_OPEN;
			free((char *)t);
			return (0);
		}

		/* resolve the yp_get_default_domain symbol */
		t->ypdom = (int (*)())dlsym(t->tr_fd, "yp_get_default_domain");
		if (!(t->ypdom)) {
			ypstub_err = YPSTUB_NOSYM;
			free((char *)t);
			return (0);
		}

		/* resolve the yp_first symbol */
		t->ypfirst = (int (*)())dlsym(t->tr_fd, "yp_first");
		if (!(t->ypfirst)) {
			ypstub_err = YPSTUB_NOSYM;
			free((char *)t);
			return (0);
		}
	
		/* resolve the yp_next symbol */
		t->ypnext = (int (*)())dlsym(t->tr_fd, "yp_next");
		if (!(t->ypnext)) {
			ypstub_err = YPSTUB_NOSYM;
			free((char *)t);
			return (0);
		}

		/* resolve the yp_match symbol */
		t->ypmatch = (int (*)())dlsym(t->tr_fd, "yp_match");
		if (!(t->ypmatch)) {
			ypstub_err = YPSTUB_NOSYM;
			free((char *)t);
			return (0);
		}
		return (t);
	}
	ypstub_err = YPSTUB_ACCESS;
	return (0);
}

static char *
_buf()
{
	if (yperrbuf == NULL)
		yperrbuf = (char *)malloc(128);
	return (yperrbuf);
}

/*
 * This is a routine that returns a string related to the current
 * error in ypstub_err.
 */
static char *
ypstub_sperror()
{
	char	*str = _buf();

	if (str == NULL)
		return (NULL);
	switch (ypstub_err) {
	case YPSTUB_OK :
		(void) sprintf(str, _libc_gettext(
					"%s: successful completion"),
			       lnsl);
		break;
	case YPSTUB_NOMEM :
		(void) sprintf(str, _libc_gettext(
					"%s: memory allocation failed"),
			       lnsl);
		break;
	case YPSTUB_NOSYM :
		(void) sprintf(str, _libc_gettext(
					"%s: symbol missing in shared object"),
			       lnsl);
		break;
	case YPSTUB_OPEN :
		(void) sprintf(str, _libc_gettext(
					"%s: couldn't open shared object"),
			       lnsl);
		break;
	case YPSTUB_ACCESS :
		(void) sprintf(str, _libc_gettext(
					"%s: shared object does not exist"),
			       lnsl);
		break;
 	case YPSTUB_SYSTEM:
 		(void) sprintf(str, _libc_gettext(
					"%s: system error: %s"),
			       lnsl, strerror(errno));
	default : 
		(void) sprintf(str, _libc_gettext(
					"%s: unknown error #%d"),
			       lnsl, ypstub_err);
		break;
	}
	return (str);
}

/*
 * This is a routine that prints out strings related to the current
 * error in ypstub_err. Like perror() it takes a string to print with a 
 * colon first.
 */
static
ypstub_perror(s)
	char	*s;
{
	char	*err;

	err = ypstub_sperror();
	fprintf(stderr, _libc_gettext("%s: %s\n"), 
		s, err ? err: _libc_gettext("error"));
	return;
} 
