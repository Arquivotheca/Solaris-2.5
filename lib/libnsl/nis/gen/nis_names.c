/*
 *	nis_names.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_names.c	1.9	93/03/03 SMI"

/*
 *	nis_names.c
 *
 * This module provides the relative name search policies.
 */
#include <string.h>
#include <rpcsvc/nis.h>
#include <ctype.h>
#include "nis_local.h"

/*
 * Do a sprintf(rslt, "%s.%s", name, dir); but count the dots
 * in the resulting name. If dir == NULL, effectively just tacks
 * a dot onto the name. Note we have to be tricky and look out for
 * quoted dots.
 */
static int
dots_in_name(name, dir, rslt)
	register char	*name;
	register char	*dir;
	register char	*rslt;
{
	int	num = 1;
	int	in_quotes = 0, quote_quote = 0;

	while (*name) {
		*rslt = *name;
		if (quote_quote && in_quotes && (*rslt != '"')) {
			quote_quote = FALSE;
			in_quotes = FALSE;
			if (*rslt == '.')
				num++;
		} else if (quote_quote && in_quotes && (*rslt == '"')) {
			quote_quote = FALSE;
		} else if (quote_quote && (*rslt != '"')) {
			quote_quote = FALSE;
			in_quotes = TRUE;
		} else if (quote_quote && (*rslt == '"')) {
			quote_quote = FALSE;
		} else if (in_quotes && (*rslt == '"')) {
			quote_quote = TRUE;
		} else if (!in_quotes && (*rslt == '"')) {
			quote_quote = TRUE;
		} else if (*rslt == '.')
			num++;
		rslt++;
		name++;
	}
	*rslt = '.'; /* This is why num = 1, we always add at least one */
	rslt++;
	if (dir) {
		while (*dir) {
			*rslt = *dir;
			if (quote_quote && in_quotes && (*rslt != '"')) {
				quote_quote = FALSE;
				in_quotes = FALSE;
				if (*rslt == '.')
					num++;
			} else if (quote_quote && in_quotes && (*rslt == '"')) {
				quote_quote = FALSE;
			} else if (quote_quote && (*rslt != '"')) {
				quote_quote = FALSE;
				in_quotes = TRUE;
			} else if (quote_quote && (*rslt == '"')) {
				quote_quote = FALSE;
			} else if (in_quotes && (*rslt == '"')) {
				quote_quote = TRUE;
			} else if (!in_quotes && (*rslt == '"')) {
				quote_quote = TRUE;
			} else if (*rslt == '.')
				num++;
			rslt++;
			dir++;
		}
	}
	*rslt = NUL;
	return (num);
}

/*
 * parse_default()
 *
 * This function takes the default domain and creates a possible set of
 * candidate names from it. (similar to the DNS server)
 */
static int
parse_default(name, result, rmax)
	nis_name	name;
	nis_name	*result;	/* array of pointers 	*/
	int		rmax;		/* max array value	*/
{
	int		dots = 0;		/* number of dots in name  */
	char		buf[NIS_MAXSRCHLEN];	/* Working buffer for dots */
	nis_name	d = nis_local_directory();
	int		comps;

	if ((d == 0) || (d[0] == 0))
		return (0);	/* dir name not yet set */
	for (comps = 0; *d && (comps < rmax); comps++) {
		dots = dots_in_name(name, d, buf);
		if (dots < 3)
			break;
		result[comps] = strdup(buf);
		if (! result[comps])
			break;
		d = (nis_name) __nis_nextsep_of(d);
		d++;
	}
	if (dots > 2) {
		/* Try just terminating it with a dot. */
		dots = dots_in_name(name, NULL, buf);
		result[comps] = strdup(buf);
		if (result[comps])
			comps++;
	}
	return (comps);
}

/*
 * __nis_parse_path()
 *
 * This function consumes "path" and parses it into a list of
 * names. Pointers to those names are stored in the array of nis_names
 * passed as 'list'. 'max' is the length of the array 'list'.
 *
 * It malloc's no memory, it only uses the array passed and the string
 * in path.
 */
int
__nis_parse_path(path, list, max)
	char		*path;
	nis_name	*list;
	int		max;
{
	register char	*s;
	int		cur;

	/* parse a colon separated list into individual table names */
	for (s = path, cur = 0; (*s != '\0') && (cur < max); cur++) {
		list[cur] = s;
		/* walk through s until EOS or ':' */
		while ((*s != ':') && (*s != '\0')) {
			if (*s == '"') {
				if (*(s+1) == '"') { /* escaped quote */
					s += 2;
				} else {
					/* skip quoted string */
					s++;
					while (1) {
						if (*s == '\0')
							break;
						/* embedded quote quote */
						if ((*s == '"') &&
						    (*(s+1) == '"')) {
							s = s+2;
							continue;
						}
						if (*s == '"')
							break;
						s++;
					}
					if (*s == '"')
						s++;
				}
			} else
				s++;
		}
		if (*s == ':') {
			*s = '\0';
			s++;
		}
	}
	return (cur);
}

/*
 * parse_path()
 *
 * This function returns the number of names it parsed out
 * of the string.
 */
static int
parse_path(name, path, local, result, rmax)
	const nis_name	name;
	const char	*path;
	const nis_name	local;
	nis_name	*result;
	int		rmax;
{
	int		i, len, comps, cur, added;
	nis_name	list[NIS_MAXPATHDEPTH];
	char		buf[NIS_MAXSRCHLEN], pbuf[NIS_MAXPATHLEN];

	/* parse a colon separated list into individual table names */
	strncpy(pbuf, path, NIS_MAXPATHLEN); /* local copy of path */
	comps = __nis_parse_path(pbuf, list, NIS_MAXPATHDEPTH);

	/* expand "special" names in the path based on $ and + */
	for (i = 0, cur = 0; (i < comps) && (cur < rmax); i++) {
		if ((*(list[i]) == '+') && (*(list[i]+1) == NUL)) {
			if (local) {
				sprintf(buf, "%s.%s", name, local);
				result[cur++] = (nis_name) strdup(buf);
				if (! result[cur-1])
					break; /* finish early */
			}
		} else if ((*(list[i]) == '$') && (*(list[i]+1) == NUL)) {
			cur += parse_default(name, &result[cur], rmax - cur);
			if (cur > 0 && (!result[cur-1]))
				break; /* finish early */
		} else {
			len = strlen((char *)(list[i]));
			/* is last character a $? */
			if (*(list[i] + (len - 1)) == '$') {
				*(list[i] + (len - 1)) = NUL;
				sprintf(buf, "%s.%s%s", name, list[i],
							nis_local_directory());
			} else
				sprintf(buf, "%s.%s", name, list[i]);
			/* force ending dot */
			if (buf[strlen(buf)-1] != '.')
				strcat(buf, ".");
			result[cur++] = (nis_name) strdup(buf);
			if (! result[cur-1])
				break; /* finish early */
		}
	}
	return (cur);
}

/*
 * nis_getnames(name)
 *
 * This function returns a list of candidate NIS+ names given an
 * non fully qualified NIS name. Note it is HOST RFC compliant
 * in that it stops generating names when the resulting name would
 * have 2 or fewer dots in it. This helps avoid banging on the root
 * name servers.
 */
nis_name *
nis_getnames(name)
	    nis_name	name;
{
	int			i;
	register nis_name	*result;
	register char		*d;
	char			*local = NULL,   /* The local directory */
				*path = NULL;	 /* The search path */
	char			buf[NIS_MAXSRCHLEN];

	if (! name)
		return (NULL);

	if (name[strlen(name)-1] != '.') {
		result = (nis_name *) malloc(128 * sizeof (nis_name));
		if (result == NULL)
			return (NULL);

		i = 0;
		path = (char *) getenv("NIS_PATH");
		if (! path)
			path = "+:$"; /* default path */
		local = (char *) getenv("NIS_DIRECTORY");
		i = parse_path(name, path, local, result, 128);
		/* check case were name is "near" the root. */
		if (i == 0) {
			char *d = nis_local_directory();

			if (d && d[0]) {
				sprintf(buf, "%s.%s", name, d);
				result[i++] = (nis_name) strdup(buf);
			}
		}
		result[i] = NULL;
	} else {
		result = (nis_name *) malloc(2 * sizeof (nis_name));
		if (! result)
			return (NULL);
		result[0] = (nis_name) strdup((char *)(name));
		result[1] = NULL;
	}
	return (result);
}

void
nis_freenames(namelist)
	nis_name	*namelist;
{
	int		i;

	i = 0;
	while (namelist[i])
		free(namelist[i++]);
	free(namelist);
	return;
}
