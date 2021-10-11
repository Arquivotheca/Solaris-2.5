/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)child.c 1.17 94/08/18 SMI"

/*
 * Includes
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "prbutlp.h"
#include "dbg.h"


/*
 * Defines
 */

#define	PRELOAD		"LD_PRELOAD"
#define	LIBPROBE	"libtnfprobe.so.1"


/*
 * Local declarations
 */

static prb_status_t find_executable(char *name, char *ret_path);
static const char *exec_cat(const char *s1, const char *s2, char *si);

/*
 * prb_child_create()  - this routine instantiates and rendevous with the
 * target child process.  This routine returns an open file descriptor on the
 * childs /proc entry.
 */

prb_status_t
prb_child_create(char *cmdname, char **cmdargs, char *loption, pid_t * pid_p)
{
	prb_status_t	prbstat;
	pid_t		   childpid;
	char			executable_name[PATH_MAX + 2];
	extern char   **environ;
	size_t		  loptlen;


	/* reset the shared memory rendevous buffer */
	prbstat = prb_shmem_set();
	if (prbstat)
		return (prbstat);

	/* fork to create the child process */
	childpid = fork();
	if (childpid == (pid_t) - 1) {
		DBG(perror("prb_child_create: fork failed"));
		return (prb_status_map(errno));
	}
	if (childpid == 0) {
		char		   *oldenv;
		char		   *newenv;

		/* ---- CHILD PROCESS ---- */

#ifdef DEBUG
		if (__prb_verbose)
			(void) fprintf(stderr, "child process %d created\n",
				(int) getpid());
#endif

		/* append libtnfprobe.so to the LD_PRELOAD environment */
		loptlen = (loption) ? strlen(loption) : 0;
		oldenv = getenv(PRELOAD);
		if (oldenv) {
			newenv = (char *) malloc(strlen(PRELOAD) +
				1 +	/* "=" */
				strlen(oldenv) +
				1 +	/* " " */
				strlen(LIBPROBE) +
				1 +	/* " " */
				loptlen +
				1);	/* NULL */

			if (!newenv)
				goto EnvFailed;
			(void) strcpy(newenv, PRELOAD);
			(void) strcat(newenv, "=");
			(void) strcat(newenv, oldenv);
			(void) strcat(newenv, " ");
			(void) strcat(newenv, LIBPROBE);
			if (loptlen) {
				(void) strcat(newenv, " ");
				(void) strcat(newenv, loption);
			}
		} else {
			newenv = (char *) malloc(strlen(PRELOAD) +
				1 +	/* "=" */
				strlen(LIBPROBE) +
				1);	/* NULL */
			if (!newenv)
				goto EnvFailed;
			(void) strcpy(newenv, PRELOAD);
			(void) strcat(newenv, "=");
			(void) strcat(newenv, LIBPROBE);
			if (loptlen) {
				(void) strcat(newenv, " ");
				(void) strcat(newenv, loption);
			}
		}
		(void) putenv((char *) newenv);
		/*
		 * * We don't check the return value of putenv because the
		 * desired * libraries might already be in the target, even
		 * if our effort to * change the environment fails.  We
		 * should continue either way ...
		 */
EnvFailed:
		;

		/* wait until the parent releases us */
		(void) prb_shmem_wait();

#ifdef DEBUG
		if (__prb_verbose >= 2)
			(void) fprintf(stderr,
				"prb_child_create: child process"
				" about to exec \"%s\"\n",
				cmdname);
#endif

		/* make the child it's own process group */
		(void) setpgrp();
		prbstat = find_executable(cmdname, executable_name);
		if (prbstat) {
			DBG((void) fprintf(stderr, "prb_child_create: %s\n",
					prb_status_str(prbstat)));
			/* parent waits for exit */
			exit(1);
		}
		if (execve(executable_name, cmdargs, environ) == -1) {
			DBG(perror("prb_child_create: exec failed"));

			/*
			 * probably the following statement is not such a
			 * good idea.  Where are we returning to?  In the
			 * child?
			 */
			/* return (prb_status_map(errno)); */
			_exit(1);
		}
		_exit(0);
	}
	/* ---- PARENT PROCESS ---- */
	/* child is waiting for us */

	*pid_p = childpid;
	return (PRB_STATUS_OK);

}				/* end prb_child_create */


/* Copyright (c) 1988 AT&T */
/* All Rights Reserved   */

/* THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	 */
/* The copyright notice above does not evidence any		 */
/* actual or intended publication of such source code.	 */


static		  prb_status_t
find_executable(char *name, char *ret_path)
{
	const char	 *pathstr;
	char			fname[PATH_MAX + 2];
	const char	 *cp;
	struct stat	 stat_buf;

	if (*name == '\0') {
		return (prb_status_map(ENOENT));
	}
	if ((pathstr = getenv("PATH")) == NULL) {
		if (geteuid() == 0 || getuid() == 0)
			pathstr = "/usr/sbin:/usr/bin";
		else
			pathstr = "/usr/bin:";
	}
	cp = strchr(name, '/') ? (const char *) "" : pathstr;

	do {
		cp = exec_cat(cp, name, fname);
		if (stat(fname, &stat_buf) != -1) {
			/* successful find of the file */
			(void) strncpy(ret_path, fname, PATH_MAX + 2);
			return (PRB_STATUS_OK);
		}
	} while (cp);

	return (prb_status_map(ENOENT));
}



static const char *
exec_cat(const char *s1, const char *s2, char *si)
{
	char		   *s;
	/* number of characters in s2 */
	int			 cnt = PATH_MAX + 1;

	s = si;
	while (*s1 && *s1 != ':') {
		if (cnt > 0) {
			*s++ = *s1++;
			cnt--;
		} else
			s1++;
	}
	if (si != s && cnt > 0) {
		*s++ = '/';
		cnt--;
	}
	while (*s2 && cnt > 0) {
		*s++ = *s2++;
		cnt--;
	}
	*s = '\0';
	return (*s1 ? ++s1 : 0);
}
