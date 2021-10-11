/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*	Portions Copyright (c) 1988, Sun Microsystems, Inc.	*/
/*	All Rights Reserved. 					*/

#ident	"@(#)rm.c	1.18	95/07/22 SMI"	/* SVr4.0 1.19	*/
/*
 * rm [-fiRr] file ...
 */

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>
#include <locale.h>
#include <langinfo.h>
#include <unistd.h>
#include <stdlib.h>

#define	ARGCNT		5		/* Number of arguments */
#define	CHILD		0
#define	DIRECTORY	((buffer.st_mode&S_IFMT) == S_IFDIR)
#define	SYMLINK		((buffer.st_mode&S_IFMT) == S_IFLNK)
#define	FAIL		-1
#define	MAXFORK		100		/* Maximum number of forking attempts */
#define	MAXFILES	OPEN_MAX  - 2	/* Maximum number of open files */
#define	MAXLEN		DIRBUF-24  	/* Name length (1024) is limited */
			/* stat(2).  DIRBUF (1048) is defined */
			/* in dirent.h as the max path length */
#define	NAMESIZE	MAXNAMLEN + 1	/* "/" + (file name size) */
#define	TRUE		1
#define	FALSE		0
#define	WRITE		02
#define	SEARCH		07

static	int	errcode;
static	int interactive, recursive, silent; /* flags for command line options */

static	void	rm(char *, int);
static	void	undir(char *, int, dev_t, ino_t);
static	int	yes(void);
static	int	mypath(dev_t, ino_t);

static	char	yeschr;
static	char	nochr;

static char *fullpath;
static int homedirfd;
static ino_t homeino;
static dev_t homedev;

static void push_name(char *name, int first);
static void pop_name(int first);
static char *get_filename(char *name);

int
main(int argc, char *argv[])
{
	extern int	optind;
	int	errflg = 0;
	int	c;
	struct stat buffer;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	yeschr = *nl_langinfo(YESSTR);
	nochr = *nl_langinfo(NOSTR);

	while ((c = getopt(argc, argv, "frRi")) != EOF)
		switch (c) {
		case 'f':
			silent = TRUE;
#ifdef XPG4
			interactive = FALSE;
#endif
			break;
		case 'i':
			interactive = TRUE;
#ifdef XPG4
			silent = FALSE;
#endif
			break;
		case 'r':
		case 'R':
			recursive = TRUE;
			break;
		case '?':
			errflg = 1;
			break;
		}

	/*
	 * For BSD compatibility allow '-' to delimit the end
	 * of options.  However, if options were already explicitly
	 * terminated with '--', then treat '-' literally: otherwise,
	 * "rm -- -" won't remove '-'.
	 */
	if (optind < argc &&
	    strcmp(argv[optind], "-") == 0 &&
	    strcmp(argv[optind - 1], "--") != 0)
		optind++;

	argc -= optind;
	argv = &argv[optind];

	if ((argc < 1 && !silent) || errflg) {
		(void) fprintf(stderr,
			gettext("usage: rm [-fiRr] file ...\n"));
		exit(2);
	}

	homedirfd = open(".", O_RDONLY);
	if (homedirfd == -1) {
		(void) fprintf(stderr,
		    gettext(
			"rm: cannot open starting directory: "));
		perror("");
		exit(2);
	}
	if (fstat(homedirfd, &buffer) == -1) {
		(void) fprintf(stderr,
		    gettext(
			"rm: cannot stat current directory: "));
		perror("");
		exit(2);
	}
	homedev = buffer.st_dev;
	homeino = buffer.st_ino;

	while (argc-- > 0) {
		rm(*argv, 1);
		argv++;
		if (fchdir(homedirfd) == -1) {
			(void) fprintf(stderr,
			    gettext(
				"rm: cannot change to current directory: "));
			perror("");
			exit(2);
		}
	}

	return (errcode ? 2 : 0);
	/* NOTREACHED */
}

static void
rm(char *path, int first)
{
	struct stat buffer;
	char	*filepath;
	char	*p;

	/*
	 * Check file to see if it exists.
	 */
	if (lstat(path, &buffer) == FAIL) {
		if (!silent) {
			perror(path);
			++errcode;
		}
		return;
	}

	/* prevent removal of . or .. (directly) */
	if (p = strrchr(path, '/'))
		p++;
	else
		p = path;
	if (strcmp(".", p) == 0 || strcmp("..", p) == 0) {
		if (!silent) {
			(void) fprintf(stderr,
			    gettext("rm of %s is not allowed\n"), path);
			errcode++;
		}
		return;
	}
	/*
	 * If it's a directory, remove its contents.
	 */
	if (DIRECTORY) {
		/*
		 * If "-r" wasn't specified, trying to remove directories
		 * is an error.
		 */
		if (!recursive) {
			(void) fprintf(stderr,
			    gettext("rm: %s is a directory\n"), path);
			++errcode;
			return;
		}

		undir(path, first, buffer.st_dev, buffer.st_ino);
		return;
	}

	filepath = get_filename(path);

	/*
	 * If interactive, ask for acknowledgement.
	 *
	 * TRANSLATION_NOTE - The following message will contain the
	 * first character of the strings for "yes" and "no" defined
	 * in the file "nl_langinfo.po".  After substitution, the
	 * message will appear as follows:
	 *	rm: remove <filename> (y/n)?
	 * For example, in German, this will appear as
	 *	rm: löschen <filename> (j/n)?
	 * where j=ja, n=nein, <filename>=the file to be removed
	 *
	 */


	if (interactive) {
		(void) fprintf(stderr, gettext("rm: remove %s (%c/%c)? "),
			filepath, yeschr, nochr);
		if (!yes()) {
			free(filepath);
			return;
		}
	} else if (!silent) {
		/*
		 * If not silent, and stdin is a terminal, and there's
		 * no write access, and the file isn't a symbolic link,
		 * ask for permission.
		 *
		 * TRANSLATION_NOTE - The following message will contain the
		 * first character of the strings for "yes" and "no" defined
		 * in the file "nl_langinfo.po".  After substitution, the
		 * message will appear as follows:
		 * 	rm: <filename>: override protection XXX (y/n)?
		 * where XXX is the permission mode bits of the file in octal
		 * and <filename> is the file to be removed
		 *
		 */
		if (!SYMLINK && access(path, W_OK) == FAIL &&
		    isatty(fileno(stdin))) {
			(void) printf(
			    gettext("rm: %s: override protection %o (%c/%c)? "),
			    filepath, buffer.st_mode & 0777, yeschr, nochr);
			/*
			 * If permission isn't given, skip the file.
			 */
			if (!yes()) {
				free(filepath);
				return;
			}
		}
	}

	/*
	 * If the unlink fails, inform the user if interactive or not silent.
	 */

#ifdef XPG4
	if (unlink(path) == FAIL) {
#else
	if (unlink(path) == FAIL && (!silent || interactive)) {
#endif
		(void) fprintf(stderr,
			    gettext("rm: %s not removed: "), filepath);
			perror("");
			++errcode;
	}

	free(filepath);
}

static void
undir(char *path, int first, dev_t dev, ino_t ino)
{
	char	*newpath;
	DIR	*name;
	struct dirent *direct;
	int	ismypath;
	int	chdir_failed = 0;
	int	len;

	push_name(path, first);

	/*
	 * If interactive and this file isn't in the path of the
	 * current working directory, ask for acknowledgement.
	 *
	 * TRANSLATION_NOTE - The following message will contain the
	 * first character of the strings for "yes" and "no" defined
	 * in the file "nl_langinfo.po".  After substitution, the
	 * message will appear as follows:
	 *	rm: examine files in directory <directoryname> (y/n)?
	 * where <directoryname> is the directory to be removed
	 *
	 */
	ismypath = mypath(dev, ino);
	if (interactive) {
		(void) fprintf(stderr,
		    gettext("rm: examine files in directory %s (%c/%c)? "),
			fullpath, yeschr, nochr);
		/*
		 * If the answer is no, skip the directory.
		 */
		if (!yes()) {
			pop_name(first);
			return;
		}
	}

#ifdef XPG4
	/*
	 * XCU4 and POSIX.2: If not interactive and file is not in the
	 * path of the current working directory, check to see whether
	 * or not directory is readable or writable and if not,
	 * prompt user for response.
	 */
	if (!interactive && !ismypath &&
	    (access(path, W_OK|X_OK) == FAIL) && isatty(fileno(stdin))) {
		if (!silent) {
			(void) fprintf(stderr,
			    gettext(
				"rm: examine files in directory %s (%c/%c)? "),
			    fullpath, yeschr, nochr);
			/*
			 * If the answer is no, skip the directory.
			 */
			if (!yes()) {
				pop_name(first);
				return;
			}
		}
	}
#endif

	/*
	 * Open the directory for reading.
	 */
	if ((name = opendir(path)) == NULL) {
		(void) fprintf(stderr,
		    gettext("rm: cannot read directory %s: "), fullpath);
		perror("");
		/* Continue to next file/directory rather than exit */
		++errcode;
		pop_name(first);
		return;
	}

	/*
	 * XCU4 requires that rm -r descend the directory
	 * hierarchy without regard to PATH_MAX.  If we can't
	 * chdir() do not increment error counter and do not
	 * print message.
	 *
	 * However, if we cannot chdir because someone has taken away
	 * execute access we may still be able to delete the directory
	 * if it's empty. The old rm could do this.
	 */

	if (chdir(path) == -1) {
		chdir_failed = 1;
	}

	/*
	 * Read every directory entry.
	 */
	while ((direct = readdir(name)) != NULL) {
		/*
		 * Ignore "." and ".." entries.
		 */
		if (strcmp(direct->d_name, ".") == 0 ||
		    strcmp(direct->d_name, "..") == 0)
			continue;
		/*
		 * Try to remove the file.
		 */
		len = strlen(direct->d_name) + 1;
		if (chdir_failed) {
			len += strlen(path) + 2;
		}

		newpath = malloc(len);
		if (newpath == NULL) {
			(void) fprintf(stderr,
			    gettext("rm: Insufficient memory.\n"));
			exit(1);
		}

		if (!chdir_failed) {
			(void) strcpy(newpath, direct->d_name);
		} else {
			(void) sprintf(newpath, "%s/%s", path, direct->d_name);
		}


		/*
		 * If a spare file descriptor is available, just call the
		 * "rm" function with the file name; otherwise close the
		 * directory and reopen it when the child is removed.
		 */
		if (name->dd_fd >= MAXFILES) {
			(void) closedir(name);
			rm(newpath, 0);
			if (!chdir_failed)
				name = opendir(".");
			else
				name = opendir(path);
			if (name == NULL) {
				(void) fprintf(stderr,
				    gettext("rm: cannot read directory %s: "),
				    fullpath);
				perror("");
				exit(2);
			}
		} else
			rm(newpath, 0);

		free(newpath);
	}

	/*
	 * Close the directory we just finished reading.
	 */
	(void) closedir(name);

	/*
	 * The contents of the directory have been removed.  If the
	 * directory itself is in the path of the current working
	 * directory, don't try to remove it.
	 * When the directory itself is the current working directory, mypath()
	 * has a return code == 2.
	 *
	 * XCU4: Because we've descended the directory heirarchy in order
	 * to avoid PATH_MAX limitation, we must now start ascending
	 * one level at a time and remove files/directories.
	 */

	if (!chdir_failed) {
		if (first) {
			if (fchdir(homedirfd) == -1) {
				(void) fprintf(stderr,
				    gettext("rm: cannot change to starting "
					    "directory: "));
				perror("");
				exit(2);
			}
		} else if (chdir("..") == -1) {
			(void) fprintf(stderr,
			    gettext("rm: cannot change to parent of "
				    "directory %s: "),
			    fullpath);
			perror("");
			exit(2);
		}
	}

	switch (ismypath) {
	case 2:
		(void) fprintf(stderr,
		    gettext("rm: Cannot remove any directory in the path "
			"of the current working directory\n%s\n"), fullpath);
		++errcode;
		pop_name(first);
		return;
	case 1:
		pop_name(first);
		return;
	case 0:
		break;
	}

	/*
	 * If interactive, ask for acknowledgement.
	 */
	if (interactive) {
		(void) fprintf(stderr, gettext("rm: remove %s: (%c/%c)? "),
			fullpath, yeschr, nochr);
		if (!yes()) {
			pop_name(first);
			return;
		}
	}
	if (rmdir(path) == FAIL) {
		(void) fprintf(stderr,
			gettext("rm: Unable to remove directory %s: "),
			fullpath);
		perror("");
		++errcode;
	}
	pop_name(first);
}


static int
yes(void)
{
	int	i, b;

	i = b = getchar();
	while (b != '\n' && b != '\0' && b != EOF)
		b = getchar();
	return (i == yeschr);
}


static int
mypath(dev_t dev, ino_t ino)
{
	struct stat buffer;
	dev_t	lastdev;
	ino_t	lastino;
	int	cwdfd;
	int	retval;

	/* save current location */
	cwdfd = open(".", O_RDONLY);
	if (cwdfd == -1) {
		(void) fprintf(stderr,
		    gettext(
			"rm: cannot open current directory: "));
		perror("");
		exit(2);
	}
	/*
	 * Starting from current working directory, walk toward the
	 * root, looking at each directory along the way.
	 */
	/* Start from real current working directory. */
	if (fchdir(homedirfd) == -1) {
		(void) fprintf(stderr,
		    gettext("rm: cannot change to current directory: "));
		perror("");
		exit(2);
	}
	/*
	 * Check to see if this is our current directory
	 * Indicated by return 2;
	 */
	if (dev == homedev && ino == homeino) {
		retval = 2;
		goto out;
	}
	lastdev = homedev;
	lastino = homeino;

	for (;;) {
		if (chdir("..") == -1 || lstat(".", &buffer) == -1) {
			(void) fprintf(stderr,
			    gettext("rm: cannot determine if this is an "
				"ancestor of the current working "
				"directory\n%s\n"),
			    fullpath);
			/* assume it is. least dangerous */
			retval = 1;
			goto out;
		}

		/*
		 * If we find a match, the directory (dev, ino) passed to
		 * mypath() is an ancestor of ours. Indicated by return 1;
		 */
		if (buffer.st_dev == dev && buffer.st_ino == ino) {
			retval = 1;
			goto out;
		}

		/*
		 * If we reach the root without a match, the given
		 * directory is not in our path.
		 */
		if (buffer.st_dev == lastdev && buffer.st_ino == lastino) {
			retval = 0;
			goto out;
		}

		/*
		 * Save the current dev and ino, and loop again to go
		 * back another level.
		 */
		lastdev = buffer.st_dev;
		lastino = buffer.st_ino;
	}
out:
	if (fchdir(cwdfd) == -1) {
		(void) fprintf(stderr,
		    gettext(
			"rm: cannot change to current directory: "));
		perror("");
		exit(2);
	}
	(void) close(cwdfd);
	return (retval);
}

static int maxlen;
static int curlen;

static char *
get_filename(char *name)
{
	char *path;

	if (fullpath == NULL || *fullpath == '\0') {
		path = strdup(name);
		if (path == NULL) {
			(void) fprintf(stderr,
			    gettext("rm: Insufficient memory.\n"));
			exit(1);
		}
	} else {
		path = malloc(strlen(fullpath) + strlen(name) + 2);
		if (path == NULL) {
			(void) fprintf(stderr,
			    gettext("rm: Insufficient memory.\n"));
			exit(1);
		}
		(void) sprintf(path, "%s/%s", fullpath, name);
	}
	return (path);
}

static void
push_name(char *name, int first)
{
	int	namelen;

	namelen = strlen(name) + 1; /* 1 for "/" */
	if ((curlen + namelen) >= maxlen) {
		maxlen += MAXLEN;
		fullpath = realloc(fullpath, maxlen + 1);
	}
	if (first) {
		(void) strcpy(fullpath, name);
	} else {
		(void) strcat(fullpath, "/");
		(void) strcat(fullpath, name);
	}
	curlen = strlen(fullpath);
}

static void
pop_name(int first)
{
	char *slash;

	if (first) {
		*fullpath = '\0';
		return;
	}
	slash = strrchr(fullpath, '/');
	if (slash)
		*slash = '\0';
	else
		*fullpath = '\0';
	curlen = strlen(fullpath);
}
