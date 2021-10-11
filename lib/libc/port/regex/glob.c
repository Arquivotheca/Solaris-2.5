/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)glob.c 1.2	95/06/30 SMI"

/*
 * glob, globfree -- POSIX.2 compatible file name expansion routines.
 *
 * Copyright 1985, 1991 by Mortice Kern Systems Inc.  All rights reserved.
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
 * Written by Eric Gisin.
 */
#ifdef M_RCSID
#ifndef lint
static char rcsID[] = "$Id: glob.c 1.31 1994/04/07 22:50:43 mark Exp $";
#endif
#endif

#include <mks.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <glob.h>
#include <errno.h>
#include <fnmatch.h>

#define	GLOB__CHECK	0x80	/* stat generated paths */

#define	INITIAL	8		/* initial pathv allocation */
#define	NULLCPP	((char **)0)	/* Null char ** */

static	char *path;

STATREF	int globit ANSI((int dend, const char *sp, glob_t *gp, int flag,
			 int (*errfn) ANSI((const char *path, int err)) ));
STATREF	int pstrcmp ANSI((const void *, const void *));
STATREF	int append ANSI((glob_t *gp, const char *str));

/*f
 * Free all space consumed by glob.
 */
void
globfree(gp)
register glob_t *gp;
{
	register int i;

	if (gp->gl_pathv == 0)
		return;

	for (i = gp->gl_offs; i < gp->gl_offs + gp->gl_pathc; ++i)
		free(gp->gl_pathv[i]);
	free((_VOID *)gp->gl_pathv);

	gp->gl_pathc = 0;
	gp->gl_pathv = NULLCPP;
}

/*f
 * Do filename expansion.
 */
int
glob(pattern, flags, errfn, gp)
const char *pattern;
int flags;
int (*errfn) ANSI((const char *path, int err));
register glob_t *gp;
{
	register int i;
	int ipathc, rv = 0;

	if ((flags & GLOB_DOOFFS) == 0)
		gp->gl_offs = 0;

	if (!(flags&GLOB_APPEND)) {
		gp->gl_pathc = 0;
		gp->gl_pathn = gp->gl_offs + INITIAL;
		gp->gl_pathv = (char **)malloc(sizeof(char *) * gp->gl_pathn);

		if (gp->gl_pathv == NULLCPP)
			return (GLOB_NOSPACE);
		gp->gl_pathp = gp->gl_pathv + gp->gl_offs;

		for (i = 0; i < gp->gl_offs; ++i)
			gp->gl_pathv[i] = NULL;
	}

	if ((path = malloc(strlen(pattern)+1)) == NULL)
		return (GLOB_NOSPACE);

	M_INVARIANTINIT();
	ipathc = gp->gl_pathc;
	rv = globit(0, pattern, gp, flags, errfn);

	if (rv == GLOB_ABORTED) {
		/*
		 * User's error function returned non-zero, or GLOB_ERR was
		 * set, and we encountered a directory we couldn't search.
		 */
		free(path);
		return GLOB_ABORTED;
	}

	i = gp->gl_pathc - ipathc;
	if (i>0 && !(flags&GLOB_NOSORT)) {
		qsort((char *)(gp->gl_pathp+ipathc), i, sizeof(char *),
		      pstrcmp);
	}
	if (i == 0) {
		if (flags & GLOB_NOCHECK)
			append(gp, pattern);
		else
			rv = GLOB_NOMATCH;
	}
	gp->gl_pathp[gp->gl_pathc] = NULL;
	free(path);

	return rv;
}

/*f
 * Recursive routine to match glob pattern, and walk directories.
 */
STATIC int
globit(dend, sp, gp, flags, errfn)
int dend;			/* offset to end of "path" */
const char *sp;			/* source pattern */
glob_t *gp;
int flags;
int (*errfn) ANSI((const char *path, int err));
{
	size_t n, m;
	int end = 0;	/* end of expanded directory */
	char *pat = (char*) sp;	/* pattern component */
	char *dp = path + dend;
	int expand = 0;		/* path has pattern */
	register char *cp;
	struct stat sb;
	DIR *dirp;
	struct dirent *d;
	int namemax;
	int err;
	int c;

	while (1)
		switch (c = (*dp++ = *(unsigned char *)sp++), M_INVARIANT(c)) {
		  case '\0':	/* end of source path */
			if (expand)
				goto Expand;
			else {
				if (!(flags&GLOB_NOCHECK)
				    || flags&(GLOB__CHECK|GLOB_MARK))
					if (stat(path, &sb) < 0)
						return 0;
				if (flags&GLOB_MARK && S_ISDIR(sb.st_mode)) {
					*dp = '\0';
					*--dp = '/';
				}
				if (append(gp, path) < 0)
					return GLOB_NOSPACE;
				return 0;
			}
			/*NOTREACHED*/
			break;

		  case '*':
		  case '?':
		  case '[':
#if (!defined(DOS) && !defined(NT) && !defined(OS2))
		  case '\\':
#endif
			++expand;
			break;

#if defined(DOS) || defined(NT) || defined(OS2)
		  case ':':
			/*l
			 * if (dp - path != 1+1) then
			 *   we are not considering c = dp[-1] = path[1] = ':'
			 *   which means that ':' should not
			 *   be considered a delimiter
			 * else
			 *   ':' is a delimiter
			 * fi
			 */
			if (dp - path != 1+1)
				break;

		  case '\\':
#endif
		  case '/':
			if (expand)
				goto Expand;
			end = dp - path;
			pat = (char*) sp;
			break;

		  Expand:
			/* determine directory and open it */
			path[end] = '\0';
#ifdef NAME_MAX
                        namemax = NAME_MAX;
#else
			namemax = 1024;  /* something large */
#endif
			dirp = opendir(*path == '\0' ? "." : path);
			if (dirp == (DIR *)0 || namemax == -1) {
				if (errfn != 0 && errfn(path, errno) != 0 
				    || flags&GLOB_ERR)
					return GLOB_ABORTED;
				return 0;
			}

			/* extract pattern component */
			n = sp - pat;
			if ((cp = malloc(n)) == NULL) {
				closedir(dirp);
				return GLOB_NOSPACE;
			}
			pat = memcpy(cp, pat, n);
			pat[n-1] = '\0';
			if (*--sp != '\0')
				flags |= GLOB__CHECK;

			/* expand path to max. expansion */
			n = dp - path;
			path = realloc(path, strlen(path)+namemax+strlen(sp)+1);
			if (path == NULL) {
				closedir(dirp);
				free(pat);
				return GLOB_NOSPACE;
			}
			dp = path + n;

			/* read directory and match entries */
			err = 0;
			while ((d = readdir(dirp)) != NULL) {
				cp = d->d_name;
				if ((flags&GLOB_NOESCAPE) 
				    ? fnmatch(pat, cp, FNM_PERIOD|FNM_NOESCAPE)
				    : fnmatch(pat, cp, FNM_PERIOD))
					continue;

				n = strlen(cp);
				memcpy(path+end, cp, n);
				m = dp - path;
				err = globit(end+n, sp, gp, flags, errfn);
				dp = path + m;   /* globit can move path */
				if (err != 0)
					break;
			}

			closedir(dirp);
			free(pat);
			return err;
		}
}

/*f
 * Comparison routine for two name arguments, called by qsort.
 */
STATIC int
pstrcmp(npp1, npp2)
const void *npp1, *npp2;
{
	return (strcoll(*(char**)npp1, *(char **)npp2));
}

/*f
 * Add a new matched filename to the glob_t structure, increasing the
 * size of that array, as required.
 */
STATIC int
append(gp, str)
register glob_t *gp;
const char *str;
{
	char *cp;

	if ((cp = malloc(strlen(str)+1)) == NULL)
		return GLOB_NOSPACE;
	gp->gl_pathp[gp->gl_pathc++] = strcpy(cp, str);

	if ((gp->gl_pathc + gp->gl_offs) >= gp->gl_pathn) {
		gp->gl_pathn *= 2;
		gp->gl_pathv = (char**) realloc((_VOID*)gp->gl_pathv, 
						gp->gl_pathn * sizeof(char*));
		if (gp->gl_pathv == NULLCPP)
			return GLOB_NOSPACE;
		gp->gl_pathp = gp->gl_pathv + gp->gl_offs;
	}
	return 0;
}
