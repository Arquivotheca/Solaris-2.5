/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)setlocale.c	1.23	95/09/25 SMI"	/* SVr4.0 1.9	*/

/*
* setlocale - set and query function for all or parts of a program's locale.
*/

#pragma weak setlocale = _setlocale

#include "synonyms.h"
#include "shlib.h"
#include <locale.h>
#include "_locale.h"	/* internal to libc locale data structures */
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _REENTRANT
mutex_t _locale_lock = DEFAULTMUTEX;
#endif

static char *set_cat();

const char *_loc_filename[LC_ALL] =
		{ "LC_CTYPE/ctype",
		    "LC_NUMERIC/numeric",
		    "LC_TIME/time",
		    "LC_COLLATE/coll.so",
		    "LC_MONETARY/monetary"
		};


static char *_real_setlocale();

char *
setlocale(cat, loc)
int cat;
const char *loc;
{
	static char stat_ans[LC_ANS];
	static thread_key_t ans_key = 0;
	char *ans = (_thr_main() ? stat_ans :
			(char *)_tsdalloc(&ans_key, LC_ANS));

	_mutex_lock(&_locale_lock);
	ans = _real_setlocale(cat, loc, ans);
	_mutex_unlock(&_locale_lock);
	return (ans);
}


static char *
_real_setlocale(cat, loc, ans)
int cat;
const char *loc;
char *ans;
{
	char part[LC_NAMELEN];

	if (loc == 0) {	/* query */
		if (cat != LC_ALL)
			(void) strcpy(ans, _cur_locale[cat]);
		else {
			register char *p, *q;
			register int flag = 0;
			register int i;

			/*
			* Generate composite locale description.
			*/
			p = ans;
			for (i = LC_CTYPE; i < LC_ALL; i++) {
				*p++ = '/';
				q = _cur_locale[i];
				(void) strcpy(p, q);
				p += strlen(q);
				if (!flag && i > LC_CTYPE)
					flag = strcmp(q, _cur_locale[i - 1]);
			}
			if (!flag)
				return (q);
		}
		return (ans);
	}
	/*
	* Handle LC_ALL setting specially.
	*/
	if (cat == LC_ALL) {
		static int reset = 0;
		register const char *p;
		register int i;
		static char *sv_loc;

		if (!reset)
			sv_loc = _real_setlocale(LC_ALL, NULL, ans);
		cat = LC_CTYPE;
		if ((p = loc)[0] != '/') {	/* simple locale */
			loc = strncpy(part, p, LC_NAMELEN - 1);
			part[LC_NAMELEN - 1] = '\0';
		}
		do {	/* for each category other than LC_ALL */
			if (p[0] == '/') {	/* piece of composite locale */
				i = strcspn(++p, "/");
				(void) strncpy(part, p, i);
				part[i] = '\0';
				p += i;
			}
			if (set_cat(cat++, part) == 0) {
				/*
				 * we cannot attempt a reset if the
				 * "so called" saved locale has no
				 * backing disk files, otherwise
				 * we will recurse for ever or core
				 * dump.
				 */
				if (reset)
					return (0);
				reset = 1;
				_real_setlocale(LC_ALL, sv_loc, ans);
				reset = 0;
				return (0);
			}
		} while (cat < LC_ALL);
		return (_real_setlocale(LC_ALL, NULL, ans));
	}
	return (set_cat(cat, loc));
}

static char *
set_cat(cat, loc)
int cat;
const char *loc;
{
	char part[LC_NAMELEN];
	struct stat stat_buf;

	/*
	* Set single category's locale.  By default,
	* just note the new name and handle it later.
	* For LC_CTYPE and LC_NUMERIC, fill in their
	* tables now.
	*/
	if (loc[0] == '\0')
		loc = _nativeloc(cat);
	else {
		loc = strncpy(part, loc, LC_NAMELEN - 1);
		part[LC_NAMELEN - 1] = '\0';
	}
	if (cat <= LC_NUMERIC) {
		if (strcmp(loc, _cur_locale[cat]) != 0 &&
		    _set_tab(loc, cat) != 0)
			return (0);
	} else if (cat == LC_COLLATE) {
#if defined(PIC)
		if (strcmp(loc, _cur_locale[cat]) != 0 &&
			setup_collate(loc) == -1)
			return (0);
#else
		/* strcoll() of libc.a only works for C locale. */
		return (0);
#endif /* PIC */
	} else {
		int fd;

		if (strcmp(loc, "C") == 0) /* "C" is always valid. */
		    goto valid_locale;

		if (cat == LC_MESSAGES) /* only check for directory */
			if (stat(_fullocale(loc, ""), &stat_buf) == 0)
				goto valid_locale;
			else
				return (0);

		if (strcmp(loc, _cur_locale[cat]) != 0) {
			if ((fd = open(_fullocale(loc,
				_loc_filename[cat]),
				O_RDONLY)) == -1)
				return (0);
			(void) close(fd);
		}
	}
valid_locale:
	return (strcpy(_cur_locale[cat], loc));
}
