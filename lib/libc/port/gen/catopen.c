/*
 * catopen.c
 *
 * Copyright (c) 1990, 1991, Sun Microsystems, Inc.
 */

#ident	"@(#)catopen.c 1.14	94/02/02 SMI"

#ifdef __STDC__
#pragma weak catopen = _catopen
#pragma weak catclose = _catclose
#endif

#include "synonyms.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <nl_types.h>
#include <locale.h>

#define	PATH_MAX 1023

#ifdef __STDC__
static char *
replace_nls_option(char *, char *, char *, char *, char *, char *, char *);
static nl_catd file_open(const char *);
static nl_catd process_nls_path(char *, int);
#else
static char *replace_nls_option();
static nl_catd file_open();
static nl_catd process_nls_path();
#endif


nl_catd
_catopen(const char *name, int oflag)
{
	nl_catd p;

	if (!name) {				/* Null pointer */
		return ((nl_catd)-1);
	} else if (name && (strchr(name, '/') != NULL)) {
		/* If name contains '/', then it is complete file name */
		p = file_open(name);
	} else {				/* Normal case */
		p = process_nls_path((char *)name, oflag);
	}

	if (p == NULL) {  /* Opening catalog file failed */
		return ((nl_catd)-1);
	} else {
		return (p);
	}
}


/*
 * This routine will process NLSPATH environment variable.
 * It will return catd id whenever it finds valid catalog.
 */
static nl_catd
process_nls_path(name, oflag)
	char	*name;
	int 	oflag;
{
	char	*s, *s1, *s2, *t, *u;
	char	*nlspath, *lang, *territory, *codeset, *locale;
	char	pathname[PATH_MAX + 1];
	int	fd;
	nl_catd	p;

	/*
	 * locale=language_territory.codeset
	 * XPG4 uses LC_MESSAGES.
	 * XPG3 uses LANG.
	 * From the following two lines, choose one depending on XPG3 or 4.
	 *
	 * Chose XPG4. If oflag == NL_CAT_LOCALE, use LC_MESSAGES.
	 */
	if (oflag == NL_CAT_LOCALE)
		locale = _setlocale(LC_MESSAGES, NULL); 
	else
		locale = getenv("LANG"); 	

	nlspath = getenv("NLSPATH");
	lang = NULL;
	if (nlspath) {
		territory = NULL;
		codeset = NULL;
		/*
		 * extract lang, territory and codeset from locale name
		 */
		if (locale) {
			lang = s = strdup(locale);
			s1 = s2 = NULL;
			while (s && *s) {
				if (*s == '_') {
					s1 = s;
					*s1++ = NULL;
				} else if (*s == '.') {
					s2 = s;
					*s2++ = NULL;
				}
				s++;
			}
			territory = s1;
			codeset   = s2;
		} /* if (locale) */

		/*
		 * March through NLSPATH until finds valid cat file
		 */
		s = nlspath;
		while (*s) {
			if (*s == ':') {
				p = file_open(name);
				if (p != NULL) {
					if (lang)
						free(lang);
					return (p);
				}
				++s;
				continue;
			}

			/* replace Substitution field */
			s = replace_nls_option(s, name, pathname, locale,
						lang, territory, codeset);

			p = file_open(pathname);
			if (p != NULL) {
				if (lang)
					free(lang);
				return (p);
			}
			if (*s)
				++s;
		} /* while */
	} /* if (nlspath) */

	/* lang is not used any more, free it */
	if (lang)
		free(lang);

	/*
	 * Implementation dependent default location of XPG3.
	 * We use /usr/lib/locale/<locale>/LC_MESSAGES/%N.
	 * If C locale, do not translate message.
	 */
	if (locale == NULL) {
		return (NULL);
	} else if (locale[0] == 'C' && locale[1] == '\0') {
		p = (nl_catd) malloc (sizeof (struct _nl_catd_struct));
		p->__content = NULL;
		p->__size = 0;
		return (p);
	}

	s = "/usr/lib/locale/";
	t = pathname;
	while (*t++ = *s++)
		continue;
	t--;
	s = locale;
	while (*s && t < pathname + PATH_MAX)
		*t++ = *s++;
	s = "/LC_MESSAGES/";
	while (*s && t < pathname + PATH_MAX)
		*t++ = *s++;
	s = name;
	while (*s && t < pathname + PATH_MAX)
		*t++ = *s++;
	*t = NULL;
	return (file_open(pathname));
}


/*
 * This routine will replace substitution parameters in NLSPATH
 * with appropiate values.
 */
static char *
replace_nls_option(s, name, pathname, locale, lang, territory, codeset)
	char	*s;		/* nlspath */
	char	*name;		/* name of catalog file. */
	char	*pathname;	/* To be returned. Expanded path name */
	char	*locale;	/* locale name */
	char	*lang;		/* language element  */
	char	*territory;	/* territory element */
	char	*codeset;	/* codeset element   */
{
	char	*t, *u;

	t = pathname;
	while (*s && *s != ':') {
		if (t < pathname + PATH_MAX) {
			/*
			 * %% is considered a single % character (XPG).
			 * %L : LC_MESSAGES (XPG4) LANG(XPG3)
			 * %l : The language element from the current locale.
			 *	(XPG3, XPG4)
			 */
			if (*s != '%')
				*t++ = *s;
			else if (*++s == 'N') {
				if (name) {
					u = name;
					while (*u && t < pathname + PATH_MAX)
						*t++ = *u++;
				}
			} else if (*s == 'L') {
				if (locale) {
					u = locale;
					while (*u && t < pathname + PATH_MAX)
						*t++ = *u++;
				}
			} else if (*s == 'l') {
				if (lang) {
					u = lang;
					while (*u && *u != '_' &&
						t < pathname + PATH_MAX)
						*t++ = *u++;
				}
			} else if (*s == 't') {
				if (territory) {
					u = territory;
					while (*u && *u != '.' &&
						t < pathname + PATH_MAX)
						*t++ = *u++;
				}
			} else if (*s == 'c') {
				if (codeset) {
					u = codeset;
					while (*u && t < pathname + PATH_MAX)
						*t++ = *u++;
				}
			} else {
				if (t < pathname + PATH_MAX)
					*t++ = *s;
			}
		}
		++s;
	}
	*t = NULL;
	return (s);
}

/*
 * This routine will open file, mmap it, and return catd id.
 */
#ifdef __STDC__
static nl_catd
file_open(name)
	const char	*name;
#else
static nl_catd
file_open(name)
	char	*name;
#endif
{
	int		fd;
	struct stat	statbuf;
	caddr_t		addr;
	struct _cat_hdr	*tmp;
	nl_catd		tmp_catd;

	fd = open(name, O_RDONLY, 0);
	if (fd == -1) {
		return (NULL);
	}
	fstat(fd, &statbuf);
	addr = mmap(0, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
	close(fd);

	if (addr == (caddr_t) -1) {
		return (NULL);
	}

	/* check MAGIC number of catalogue file */
	tmp = (struct _cat_hdr *)addr;
	if (tmp->__hdr_magic != _CAT_MAGIC) {
		munmap(addr, statbuf.st_size);
		return (NULL);
	}

	tmp_catd = (nl_catd) malloc (sizeof (struct _nl_catd_struct));
	tmp_catd->__content = (void *)addr;
	tmp_catd->__size = statbuf.st_size;

	return (tmp_catd);
}

int
_catclose(catd)
	nl_catd	catd;
{
	if (catd &&
	    catd != (nl_catd)-1) {
		if (catd->__content) {
			munmap(catd->__content, catd->__size);
			catd->__content = NULL;
		}
		catd->__size = 0;
		free(catd);
	}
	return (0);
}
