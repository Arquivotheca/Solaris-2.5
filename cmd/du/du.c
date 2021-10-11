/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)du.c	1.17	94/12/15 SMI"	/* SVr4.0 1.22	*/

/*
 * du -- summarize disk usage
 *	/bin/du [-a][-d][-k][-r][-o|-s] [file ...]
 *	/usr/xpg4/bin/du [-a][-k][-r][-s][-x] [file ...]
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>

static int	aflg = 0, rflg = 0, sflg = 0, kflg = 0, oflg = 0, dflg = 0;
static char	*dot = ".";
static int	level = 0;	/* Level of recursion */

struct links {
	dev_t	dev;
	ino_t	ino;
};
#define	ML	1000	/* Number of struct links's to allocate at a time */

/*
 * convert DEV_BSIZE blocks to K blocks
 */
#define	DEV_KSHIFT	1
#define	kb(n)		(((u_long) (n)) >> DEV_KSHIFT)

long	wait();
static long descend();

void
main(argc, argv)
	int argc;
	char **argv;
{
	char		path[PATH_MAX+1], name[PATH_MAX+1];
	long		blocks = 0;
	register	c;
	extern int	optind;
	register char	*np;
	register pid_t	pid, wpid;
	int		status, retcode = 0;

	setbuf(stderr, NULL);
	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN	"SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

#ifdef XPG4
	rflg++;		/* "-r" is not an option but ON always */
#endif

#ifdef XPG4
	while ((c = getopt(argc, argv, "arskx")) != EOF)
#else
	while ((c = getopt(argc, argv, "arskod")) != EOF)
#endif
		switch (c) {

		case 'a':
			aflg++;
			continue;

		case 'r':
			rflg++;
			continue;

		case 's':
			sflg++;
			continue;

		case 'k':
			kflg++;
			continue;

		case 'o':
			oflg++;
			continue;

		case 'd':
			dflg++;
			continue;

		case 'x':
			dflg++;
			continue;

#ifdef XPG4
		case '?':
			(void) fprintf(stderr, "usage: du %s\n",
			gettext("[-a][-k][-r][-s][-x] [file ...]"));
			exit(2);
#else
		case '?':
			(void) fprintf(stderr, "usage: du %s\n",
			gettext("[-a][-d][-k][-r][-o|-s] [file ...]"));
			exit(2);
#endif
		}
	if (optind == argc) {
		argv = &dot;
		argc = 1;
		optind = 0;
	}

	/* "-o" and "-s" don't make any sense together. */
	if (oflg && sflg)
		oflg = 0;

	do {
		if (optind < argc - 1) {
			pid = fork();
			if (pid == (pid_t)-1) {
				perror(gettext("du: No more processes"));
				exit(1);
			}
			if (pid != 0) {
				while ((wpid = wait(&status)) != pid &&
				    wpid != (pid_t)-1)
					;
				if (pid != (pid_t)-1 && status != 0)
					retcode = 1;
			}
		}
		if (optind == argc - 1 || pid == 0) {
			(void) strcpy(path, argv[optind]);
			(void) strcpy(name, argv[optind]);
			if (np = strrchr(name, '/')) {
				*np++ = '\0';
				if (chdir(*name ? name : "/") < 0) {
					if (rflg) {
						(void) fprintf(stderr, "du: ");
						perror(*name ? name : "/");
						exit(1);
					}
					exit(0);
				}
			} else
				np = path;
			blocks = descend(path, *np ? np : ".", &retcode,
				(dev_t) 0);
			if (sflg && level == 0) {
				if (!kflg)
#ifdef XPG4
					(void) printf("%ld %s\n",
						blocks, path);
#else
					(void) printf("%ld\t%s\n",
						blocks, path);
#endif
				else
#ifdef XPG4
					(void) printf("%ld %s\n",
						(long) kb(blocks), path);
#else
					(void) printf("%ld\t%s\n",
						(long) kb(blocks), path);
#endif
			}
			if (optind < argc - 1)
				exit(retcode);
		}
		optind++;
	} while (optind < argc);
	exit(retcode);
}

static long
descend(base, name, retcode, device)
	char *base, *name;
	int  *retcode;
	dev_t device;
{
	static DIR		*dirp = NULL;
	char			*ebase0, *ebase;
	struct stat		stb;
	int			i;
	long			blocks = 0;
	long			curoff = 0;
	register struct dirent	*dp;
	struct links		*temp_ml;
	static struct links	*ml = NULL;
	static int		mlx = 0, mlsize = 0;

	ebase0 = ebase = strchr(base, 0);
	if (ebase > base && ebase[-1] == '/')
		ebase--;
	if (lstat(name, &stb) < 0) {
		if (rflg) {
			(void) fprintf(stderr, "du: ");
			perror(base);
		}

		/*
		 * POSIX states that non-zero status codes are only set
		 * when an error message is printed out on stderr
		 */
		*retcode = (rflg ? 1 : 0);
		*ebase0 = 0;
		return (0);
	}
	if (device) {
		if (dflg && stb.st_dev != device) {
			*ebase0 = 0;
			return (0);
		}
	}
	else
		device = stb.st_dev;

	if (stb.st_nlink > 1 && (stb.st_mode & S_IFMT) != S_IFDIR) {
		for (i = 0; i < mlx; i++)
			if (ml[i].ino == stb.st_ino && ml[i].dev == stb.st_dev)
				return (0);
		if (mlx == mlsize)	/* Need to allocate more nodes! */
		{
			temp_ml = (struct links *) realloc(ml, (mlx + ML) *
				sizeof (struct links));
			if (temp_ml == NULL)
				perror(gettext(
					"du: can't keep track of more links"));
			else
			{
				ml = temp_ml;
				mlsize += ML;
			}
		}
		if (mlx < mlsize) {
			ml[mlx].dev = stb.st_dev;
			ml[mlx].ino = stb.st_ino;
			mlx++;
		}
	}
	blocks = stb.st_blocks;
	if ((stb.st_mode & S_IFMT) != S_IFDIR) {
		if ((sflg == 0) && dflg) {
#ifdef XPG4
			if (!kflg)
				(void) printf("%ld %s\n", blocks, base);
			else
				(void) printf("%ld %s\n", (long) kb(blocks),
					base);
#endif
		} else if (aflg) {
			if (!kflg)
#ifdef XPG4
				(void) printf("%ld %s\n", blocks, base);
#else
				(void) printf("%ld\t%s\n", blocks, base);
#endif
			else
#ifdef XPG4
				(void) printf("%ld %s\n", (long) kb(blocks),
					base);
#else
				(void) printf("%ld\t%s\n",
					(long) kb(blocks), base);
#endif
		} else if ((sflg == 0) && (level == 0)) {
#ifdef XPG4
			if (!kflg)
				(void) printf("%ld %s\n", blocks, base);
			else
				(void) printf("%ld %s\n", (long) kb(blocks),
					base);
#endif
		}
		return (blocks);
	}
	if (dirp != NULL)
		(void) closedir(dirp);
	if ((dirp = opendir(name)) == NULL) {
		if (rflg) {
			(void) fprintf(stderr, "du: ");
			perror(base);
		}
		*retcode = 1;
		*ebase0 = 0;
		return (0);
	}
	level++;
	if (chdir(name) < 0) {
		if (rflg) {
			(void) fprintf(stderr, "du: ");
			perror(base);
		}
		*retcode = 1;
		*ebase0 = 0;
		(void) closedir(dirp);
		dirp = NULL;
		return (0);
	}
	while (dp = readdir(dirp)) {
		if ((strcmp(dp->d_name, ".") == 0) ||
			(strcmp(dp->d_name, "..") == 0))
			continue;
		(void) sprintf(ebase, "/%s", dp->d_name);
		curoff = telldir(dirp);
		blocks += descend(base, ebase+1, retcode, device);
		*ebase = 0;
		if (dirp == NULL) {
			if ((dirp = opendir(".")) == NULL) {
				if (rflg) {
					(void) fprintf(stderr,
					    gettext("du: Can't reopen in "));
					perror(base);
				}
				*retcode = 1;
				return (0);
			}
			seekdir(dirp, curoff);
		}
	}
	(void) closedir(dirp);
	level--;
	dirp = NULL;
	if (sflg == 0) {
		if (!kflg)
#ifdef XPG4
			(void) printf("%ld %s\n", blocks, base);
#else
			(void) printf("%ld\t%s\n", blocks, base);
#endif
		else
#ifdef XPG4
			(void) printf("%ld %s\n", (long) kb(blocks), base);
#else
			(void) printf("%ld\t%s\n", (long) kb(blocks), base);
#endif
	}
	if (chdir("..") < 0) {
		if (rflg) {
			(void) sprintf(strchr(base, '\0'), "/..");
			(void) fprintf(stderr,
			    gettext("du: Can't change dir to '..' in "));
			perror(base);
		}
		exit(1);
	}
	*ebase0 = 0;
	if (oflg)
		return (0);
	else
		return (blocks);
}
