/*
 *	auto_subr.c
 *
 *	Copyright (c) 1988-1993 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)auto_subr.c	1.21	95/01/12 SMI"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <locale.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <thread.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/signal.h>
#include <sys/utsname.h>
#include <sys/tiuser.h>
#include <sys/fs/autofs.h>
#include <sys/utsname.h>
#include <rpc/rpc.h>
#include <rpcsvc/nfs_prot.h>
#include "automount.h"

#ifndef _REENTRANT
extern char *strtok_r(char *, const char *, char **);
#endif

extern int loaddirect_map(char *, char *, char *);
extern void pr_msg(const char *, ...);

static char *check_hier(char *);

struct mntlist *current_mounts;

void
dirinit(mntpnt, map, opts, direct)
	char *mntpnt, *map, *opts;
	int direct;
{
	extern struct autodir *dir_head;
	extern struct autodir *dir_tail;
	struct autodir *dir;
	char *p;

	if (strcmp(map, "-null") == 0)
		goto enter;

	p = mntpnt + (strlen(mntpnt) - 1);
	if (*p == '/')
		*p = '\0';	/* trim trailing / */
	if (*mntpnt != '/') {
		pr_msg("dir %s must start with '/'", mntpnt);
		return;
	}
	if (p = check_hier(mntpnt)) {
		pr_msg("hierarchical mountpoint: %s and %s",
			p, mntpnt);
		return;
	}

	/*
	 * If it's a direct map then call dirinit
	 * for every map entry.
	 */
	if (strcmp(mntpnt, "/-") == 0) {
		(void) loaddirect_map(map, map, opts);
		return;
	}

enter:
	dir = (struct autodir *) malloc(sizeof (*dir));
	if (dir == NULL)
		goto alloc_failed;
	dir->dir_name = strdup(mntpnt);
	if (dir->dir_name == NULL)
		goto alloc_failed;
	dir->dir_map = strdup(map);
	if (dir->dir_map == NULL)
		goto alloc_failed;
	dir->dir_opts = strdup(opts);
	if (dir->dir_opts == NULL)
		goto alloc_failed;
	dir->dir_direct = direct;
	dir->dir_remount = 0;
	dir->dir_next = NULL;

	/*
	 * Append to dir chain
	 */
	if (dir_head == NULL)
		dir_head = dir;
	else
		dir_tail->dir_next = dir;

	dir->dir_prev = dir_tail;
	dir_tail = dir;

	return;

alloc_failed:
	pr_msg("dirinit: memory allocation failed");
}

/*
 *  Check whether the mount point is a
 *  subdirectory or a parent directory
 *  of any previously mounted automount
 *  mount point.
 */
static char *
check_hier(mntpnt)
	char *mntpnt;
{
	extern struct autodir *dir_head;
	register struct autodir *dir;
	register char *p, *q;

	for (dir = dir_head; dir; dir = dir->dir_next) {
		p = dir->dir_name;
		q = mntpnt;
		for (; *p == *q; p++, q++)
			if (*p == '\0')
				break;
		if (*p == '/' && *q == '\0')
			return (dir->dir_name);
		if (*p == '\0' && *q == '/')
			return (dir->dir_name);
		if (*p == '\0' && *q == '\0')
			return (NULL);
	}
	return (NULL);	/* it's not a subdir or parent */
}

mkdir_r(dir)
	char *dir;
{
	int err;
	char *slash;

	if (mkdir(dir, 0555) == 0 || errno == EEXIST)
		return (0);
	if (errno != ENOENT)
		return (-1);
	slash = strrchr(dir, '/');
	if (slash == NULL)
		return (-1);
	*slash = '\0';
	err = mkdir_r(dir);
	*slash++ = '/';
	if (err || !*slash)
		return (err);
	return (mkdir(dir, 0555));
}

rmdir_r(dir, depth)
	char *dir;
	int depth;
{
	int is_spec = 0;
	int err;
	char mydir[MAXPATHLEN], *slash;

	if (dir[strlen(dir) -1] == ' ')
		is_spec = 1;

	/* make the common case fast */
	err = rmdir(dir);
	if (depth == 1)
		return (err);

	/* uncommon case */
	depth--;
	(void) strcpy(mydir, dir);
	err = 0;
	do {
		depth--;
		slash = strrchr(mydir, '/');
		if (slash == NULL) {
			err = -1;
			break;
		}
		if (is_spec) {
			*slash = ' ';
			*(slash+1) = '\0';
		} else
			*slash = '\0';
		err = rmdir(mydir);
		if ((err < 0) && (errno == ENOENT))
			err = 0;	/* someone removed mydir for us */
	} while (depth && !err);
	return (err);
}

int
autofs_mkdir_r(dir, depth)
	char *dir;
	int *depth;
{
	int err;
	int scount = 0;	/* keeps track of the number of slashes removed */
	char *slash;
	char spec_dir[MAXPATHLEN]; /* XXX not a good idea in recursive finc */

	(void) sprintf(spec_dir, "%s%s", dir, " ");

	err = mkdir(spec_dir, 0555);
	if (err == 0) {
		(*depth)++;
		return (0);
	} else if (errno == EEXIST)
		return (0);
	else if (errno != ENOENT)
		return (-1);

	slash = strrchr(dir, '/');
	if (slash == NULL)
		return (-1);

	while (*slash == '/') {
		*slash-- = '\0';
		scount++;
	}
	slash++;

	err = autofs_mkdir_r(dir, depth);
	while (scount > 0) {
		*slash++ = '/';
		scount--;
	}
	if (err || !*slash)
		return (err);
	(*depth)++;
	return (mkdir(spec_dir, 0555));
}

/*
 * Gets the next token from the string "p" and copies
 * it into "w".  Both "wq" and "w" are quote vectors
 * for "w" and "p".  Delim is the character to be used
 * as a delimiter for the scan.  A space means "whitespace".
 * Wordsz is the maximum string length that "w" can accept.
 */
void
getword(w, wq, p, pq, delim, wordsz)
	char *w;
	char *wq;
	char **p;
	char **pq;
	char delim;
	int wordsz;
{
	char *tmp = w;

	if (wordsz <= 0)
		return;
	while ((delim == ' ' ? isspace(**p) : **p == delim) && **pq == ' ')
		(*p)++, (*pq)++;

	while (**p &&
		!((delim == ' ' ? isspace(**p) : **p == delim) &&
			**pq == ' ')) {
		if (--wordsz <= 0) {
			*w = '\0';
			syslog(LOG_ERR,
			"maximum word length (%d) exceeded in map - %s",
			wordsz, tmp);
			break;
		}
		*w++  = *(*p)++;
		*wq++ = *(*pq)++;
	}
	*w  = '\0';
	*wq = '\0';
}

char *
get_line(fp, map, line, linesz)
	FILE *fp;
	char *map;
	char *line;
	int linesz;
{
	register char *p = line;
	register int len;
	int excess = 0;

	*p = '\0';

	for (;;) {
		if (fgets(p, linesz - (p-line), fp) == NULL) {
			return (*line ? line : NULL);	/* EOF */
		}

		len = strlen(line);
		if (len <= 0) {
			p = line;
			continue;
		}
		p = &line[len - 1];

		/*
		 * Is input line too long?
		 */
		if (*p != '\n') {
			excess = 1;
			/*
			 * Perhaps last char read was '\'. Reinsert it
			 * into the stream to ease the parsing when we
			 * read the rest of the line to discard.
			 */
			(void) ungetc(*p, fp);
			break;
		}
trim:
		/* trim trailing white space */
		while (p >= line && isspace(*(u_char *)p))
			*p-- = '\0';
		if (p < line) {			/* empty line */
			p = line;
			continue;
		}

		if (*p == '\\') {		/* continuation */
			*p = '\0';
			continue;
		}

		/*
		 * Ignore comments. Comments start with '#'
		 * which must be preceded by a whitespace, unless
		 * if '#' is the first character in the line.
		 */
		p = line;
		while (p = strchr(p, '#')) {
			if (p == line || isspace(*(p-1))) {
				*p-- = '\0';
				goto trim;
			}
			p++;
		}
		break;
	}
	if (excess) {
		int c;

		/*
		 * Discard rest of the line.
		 */
		while ((c = getc(fp)) != EOF) {
			*p = c;
			if (*p == '\n')		/* end of the long line */
				break;
			else if (*p == '\\') {		/* continuation */
				if (getc(fp) == EOF)	/* ignore next char */
					break;
			}
		}
		syslog(LOG_ERR,
			"map %s: line too long (max %d chars)",
			map, linesz-1);
		*line = '\0';
	}

	return (line);
}

/*
 * Gets the retry=n entry from opts.
 * Returns 0 if retry=n is not present in option string,
 * retry=n is invalid, or when option string is NULL.
 */
int
get_retry(char *opts)
{
	int retry = 0;
	char buf[1024];
	char *p, *pb, *lasts;

	if (opts == NULL)
		return (retry);

	(void) strcpy(buf, opts);
	pb = buf;
	while (p = (char *)strtok_r(pb, ",", &lasts)) {
		pb = NULL;
		if (strncmp(p, "retry=", 6) == 0)
			retry = atoi(p+6);
	}
	return (retry > 0 ? retry : 0);
}

/*
 * Returns zero if "opt" is found in mnt->mnt_opts, setting
 * *sval to whatever follows the equal sign after "opt".
 * str_opt allocates a string long enough to store the value of
 * "opt" plus a terminating null character and returns it as *sval.
 * It is the responsability of the caller to deallocate *sval.
 * *sval will be equal to NULL upon return if either "opt=" is not found,
 * or "opt=" has no value associated with it.
 *
 * stropt will return -1 on error.
 */
int
str_opt(struct mnttab *mnt, char *opt, char **sval)
{
	char *str, *comma;

	/*
	 * is "opt" in the options field?
	 */
	if (str = hasmntopt(mnt, opt)) {
		str += strlen(opt);
		if (*str++ != '=' ||
		    (*str == ',' || *str == '\0')) {
			syslog(LOG_ERR, "Bad option field");
			return (-1);
		}
		comma = strchr(str, ',');
		if (comma != NULL)
			*comma = '\0';
		*sval = strdup(str);
		if (comma != NULL)
			*comma = ',';
		if (*sval == NULL)
			return (-1);
	} else
		*sval = NULL;

	return (0);
}

/*
 * Performs text expansions in the string "pline".
 * "plineq" is the quote vector for "pline".
 * An identifier prefixed by "$" is replaced by the
 * corresponding environment variable string.  A "&"
 * is replaced by the key string for the map entry.
 *
 * This routine will return an error (non-zero) if *size* would be
 * exceeded after expansion, indicating that the macro_expand failed.
 * This is to prevent writing past the end of pline and plineq.
 * Both pline and plineq are left untouched in such error case.
 */
int
macro_expand(key, pline, plineq, size)
	char *key, *pline, *plineq;
	int size;
{
	register char *p,  *q;
	register char *bp, *bq;
	register char *s;
	char buffp[LINESZ], buffq[LINESZ];
	char namebuf[64], *pn;
	int expand = 0;
	struct utsname name;

	p = pline;  q = plineq;
	bp = buffp; bq = buffq;

	while (*p) {
		if (*p == '&' && *q == ' ') {	/* insert key */
			/*
			 * make sure we don't overflow buffer
			 */
			if ((int)((bp - buffp) + strlen(key)) < size) {
				for (s = key; *s; s++) {
					*bp++ = *s;
					*bq++ = ' ';
				}
				expand++;
				p++; q++;
				continue;
			} else {
				/*
				 * line too long...
				 */
				return (1);
			}
		}

		if (*p == '$' && *q == ' ') {	/* insert env var */
			p++; q++;
			pn = namebuf;
			if (*p == '{') {
				p++; q++;
				while (*p && *p != '}') {
					*pn++ = *p++;
					q++;
				}
				if (*p) {
					p++; q++;
				}
			} else {
				while (*p && isalnum(*p)) {
					*pn++ = *p++;
					q++;
				}
			}
			*pn = '\0';

			s = getenv(namebuf);
			if (!s) {
				/* not found in env */
				if (strcmp(namebuf, "HOST") == 0) {
					(void) uname(&name);
					s = name.nodename;
				} else if (strcmp(namebuf, "OSREL") == 0) {
					(void) uname(&name);
					s = name.release;
				} else if (strcmp(namebuf, "OSNAME") == 0) {
					(void) uname(&name);
					s = name.sysname;
				} else if (strcmp(namebuf, "OSVERS") == 0) {
					(void) uname(&name);
					s = name.version;
				}
			}

			if (s) {
				if ((int)((bp - buffp) + strlen(s)) < size) {
					while (*s) {
						*bp++ = *s++;
						*bq++ = ' ';
					}
				} else {
					/*
					 * line too long...
					 */
					return (1);
				}
			}
			expand++;
			continue;
		}
		/*
		 * Since buffp needs to be null terminated, we need to
		 * check that there's still room in the buffer to
		 * place at least two more characters, *p and the
		 * terminating null.
		 */
		if (bp - buffp == size - 1) {
			/*
			 * There was not enough room for at least two more
			 * characters, return with an error.
			 */
			return (1);
		}
		/*
		 * The total number of characters so far better be less
		 * than the size of buffer passed in.
		 */
		*bp++ = *p++;
		*bq++ = *q++;

	}
	if (!expand)
		return (0);
	*bp = '\0';
	*bq = '\0';
	/*
	 * We know buffp/buffq will fit in pline/plineq since we
	 * processed at most size characters.
	 */
	(void) strcpy(pline, buffp);
	(void) strcpy(plineq, buffq);

	return (0);
}

/*
 * Removes quotes from the string "str" and returns
 * the quoting information in "qbuf". e.g.
 * original str: 'the "quick brown" f\ox'
 * unquoted str: 'the quick brown fox'
 * and the qbuf: '    ^^^^^^^^^^^  ^ '
 */
void
unquote(str, qbuf)
	char *str, *qbuf;
{
	register int escaped, inquote, quoted;
	register char *ip, *bp, *qp;
	char buf[LINESZ];

	escaped = inquote = quoted = 0;

	for (ip = str, bp = buf, qp = qbuf; *ip; ip++) {
		if (!escaped) {
			if (*ip == '\\') {
				escaped = 1;
				quoted++;
				continue;
			} else
			if (*ip == '"') {
				inquote = !inquote;
				quoted++;
				continue;
			}
		}

		*bp++ = *ip;
		*qp++ = (inquote || escaped) ? '^' : ' ';
		escaped = 0;
	}
	*bp = '\0';
	*qp = '\0';
	if (quoted)
		(void) strcpy(str, buf);
}

/*
 * Removes trailing spaces from string "s".
 */
void
trim(s)
	char *s;
{
	char *p = &s[strlen(s) - 1];

	while (p >= s && isspace(*(u_char *)p))
		*p-- = '\0';
}

/*
 * Print trace output.
 * Like fprintf(stderr, fmt, ...) except that if "id" is nonzero, the output
 * is preceeded by the ID of the calling thread.
 */
#define	FMT_BUFSIZ 1024

void
trace_prt(int id, char *fmt, ...)
{
	va_list args;

#ifdef _REENTRANT
	char buf[FMT_BUFSIZ];

	if (id) {
		(void) sprintf(buf, "t%u\t%s", thr_self(), fmt);
		fmt = buf;
	}
#endif
	va_start(args, fmt);
	(void) vfprintf(stderr, fmt, args);
	va_end(args);
}
