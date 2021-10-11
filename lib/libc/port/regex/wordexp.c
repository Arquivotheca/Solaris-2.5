/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 *
 * This code is MKS code ported to Solaris with minimum modifications so that
 * upgrades from MKS will readily integrate. Avoid modification if possible!
 */
#ident	"@(#)wordexp.c 1.2	95/03/29 SMI"

/*
 * wordexp, wordfree -- POSIX.2 D11.2 word expansion routines.
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
 */

#ifdef M_RCSID
#ifndef lint
static char rcsID[] = "$Id: wordexp.c 1.22 1994/11/21 18:24:50 miked Exp $";
#endif
#endif

#include <mks.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wordexp.h>

#define	INITIAL	8		/* initial pathv allocation */
#define	NULLCPP	((char **)0)	/* Null char ** */

static char *path = "/bin/ksh";	/* Hardwired for now */

STATREF	int append ANSI((wordexp_t *wp, char *str));

/*f
 * Do word expansion.
 * We just pass our arguments to shell with -E option.  Note that the 
 * underlying shell must recognize the -E option, and do the right thing
 * with it.
 */
LDEFN int
wordexp(word, wp, flags)
const char *word;
register wordexp_t *wp;
int flags;
{
	wordexp_t wptmp;
	register int i;
	pid_t pid;
	char line[LINE_MAX];		/* word from shell */
	char *cp;
	int rv = 0;
	int status;
	int fd1, fd2;			/* dup'd fd 1 and 2 */
	int pv[2];			/* pipe from shell stdout */
	FILE *fp;			/* pipe read stream */
	static char options[9] = "-";
	char *optendp = options+1;
	static char *args[] = {"ksh", options, NULL, NULL}; /* sh -^E word */
	extern char** environ;


	/*
	 * Do absolute minimum neccessary for the REUSE flag. Eventually
	 * want to be able to actually avoid excessive malloc calls.
	 */
	if (flags&WRDE_REUSE)
		wordfree(wp);

	/*
	 * Initialize wordexp_t
	 *
	 * XPG requires that the struct pointed to by wp not be modified 
	 * unless wordexp() either succeeds, or fails on WRDE_NOSPACE.
	 * So we work with wptmp, and only copy wptmp to wp if one of the
	 * previously mentioned conditions is satisfied.
	 */
	wptmp = *wp;
#ifdef WRDE_DOOFFS
	if ((flags & WRDE_DOOFFS) == 0)
		wptmp.we_offs = 0;
#endif
	if (!(flags&WRDE_APPEND)) {
		wptmp.we_wordc = 0;
		wptmp.we_wordn = wptmp.we_offs + INITIAL;
		wptmp.we_wordv = (char **)malloc(
					sizeof(char *) * wptmp.we_wordn);
		if (wptmp.we_wordv == NULLCPP)
			return (WRDE_NOSPACE);
#ifdef WRDE_DOOFFS
		wptmp.we_wordp = wptmp.we_wordv + wptmp.we_offs;
		for (i = 0; i < wptmp.we_offs; i++)
			wptmp.we_wordv[i] = NULL;
#endif
	}

	/*
	 * Turn flags into shell options
	 */
	*optendp++ = (char) 0x05;		/* ksh -^E */
	if (flags&WRDE_UNDEF)
		*optendp++ = 'u';
	if (flags&WRDE_NOCMD)
		*optendp++ = 'N';
	*optendp = '\0';

	/*
	 * Set up pipe from shell stdout to "fp" for us
	 */
	if (pipe(pv) < 0 || (fp = fdopen(pv[0], "rb")) == NULL)
		return WRDE_ERRNO;
	fd1 = dup(1);
	dup2(pv[1], 1);			/* set stdout to pipe write */
	close(pv[1]);
	fd2 = dup(2);
	if (!(flags&WRDE_SHOWERR)) {
		close(2);
		open(M_NULLNAME, O_WRONLY);
	}

	/*
	 * Fork/exec shell with -E word
	 */
	args[2] = (char *)word;

	if ((pid = fexecve(path, args, environ)) == -1) {
		dup2(fd1, 1);
		dup2(fd2, 2);
		close(pv[0]);
		return WRDE_ERRNO;
	}
	close(1);

	/*
	 * Read words from shell, separated with '\0'.
	 * Since there is no way to disable IFS splitting,
	 * it would be possible to separate the output with '\n'.
	 */
	cp = line;
	while ((i = getc(fp)) != EOF) {
		*cp++ = i;
		if (i == '\0') {
			cp = malloc(strlen(line)+1);
			if (cp == NULL) {
				rv = WRDE_NOSPACE;
			} else {
				strcpy(cp, line);
				rv = append(&wptmp, cp);
			}
			cp = line;
		}
	}

	wptmp.we_wordp[wptmp.we_wordc] = NULL;
		
	/* Restore our fd 1 and 2 */
	dup2(fd1, 1);	close(fd1);
	dup2(fd2, 2);	close(fd2);
	close(pv[0]);			/* kill shell if still writing */

	if (waitpid(pid, &status, 0) == -1)
		rv = WRDE_ERRNO;
	else if (rv == 0) 
		rv = WEXITSTATUS(status); /* shell WRDE_* status */
	if (rv == 0 )
		*wp = wptmp;
	else
		wordfree(&wptmp);
	/*
	 * Map ksh errors to wordexp() errors
	 */
	if (rv == 4)
		rv = WRDE_CMDSUB;
	else if (rv == 5)
		rv = WRDE_BADVAL;
	else if (rv == 6)
		rv = WRDE_SYNTAX;
	return rv;
}

/*f
 * Append a word to the wordexp_t structure, growing it as neccessary.
 */
STATIC int
append(wp, str)
register wordexp_t *wp;
char *str;
{
	char *cp;

	cp = malloc(strlen(str)+1);
	if (cp == NULL)
		return WRDE_NOSPACE;
	wp->we_wordp[wp->we_wordc++] = strcpy(cp, str);
	if (wp->we_wordc >= wp->we_wordn) {
		wp->we_wordn *= 2;
		wp->we_wordv = (char**)
		    realloc((_VOID*)wp->we_wordv, wp->we_wordn * sizeof(char*));
		if (wp->we_wordv == NULLCPP)
			return WRDE_NOSPACE;
		wp->we_wordp = wp->we_wordv + wp->we_offs;
	}
	return 0;
}

/*f
 * Free all space owned by wordexp_t.
 */
LDEFN void
wordfree(wp)
register wordexp_t *wp;
{
	register int i;

	if (wp->we_wordv == NULL)
		return;
	for (i = wp->we_offs; i < wp->we_offs + wp->we_wordc; i++)
		free(wp->we_wordv[i]);
	free((_VOID *)wp->we_wordv);
	wp->we_wordc = 0;
	wp->we_wordv = NULLCPP;
}

