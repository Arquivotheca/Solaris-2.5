#ident	"@(#)dumpsetup.c 1.8 93/06/23"

/*
 * Copyright (c) 1990-1992 by Sun Microsystems, Inc.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <grp.h>
#include <config.h>
#include "structs.h"

static void do_directories(void);
static void do_directory(char *, int);

static char *database;			/* database host */
static char *operd;			/* operd host */
static char *dumpdates;			/* dumpdates file -> create it */
static char *progname;			/* my name (last component) */
static char host[MAXHOSTNAMELEN+1];	/* my host name */
static struct group *grp;		/* For group operator/sys */

/* these items are not static on purpose; smart people can patch them */
mode_t dirmode = 02775;			/* default mode for directories */
mode_t filemode = 0664;			/* default mode for files */

/* support for the /etc/init.d/hsmdump startup script */
static char *hsmdump_rc = "/etc/init.d/hsmdump";
static char *hsmdump_links[] = {
	"/etc/rc0.d/K25hsmdump",
	"/etc/rc1.d/K25hsmdump",
	"/etc/rc2.d/K25hsmdump",
	"/etc/rc2.d/S90hsmdump",
	(char *)NULL
};
static char *hsmdump = "#!/bin/sh\n\
#\n\
# SUNWhsm hsmdump start-up and shutdown script\n\
#\n\
# Used to send any left-over hsmdump database temporary files to the\n\
# hsmdump database daemon (rpc.dumpdbd).\n\
#\n\
# Online: Backup 2.0\n\
#\n\
if [ ! -d %s ]; then	# %s not mounted\n\
	exit\n\
fi\n\
\n\
case \"$1\" in\n\
start)\n\
	# Recover any remaining database update files\n\
	%s/hsmdump R &\n\
	;;\n\
stop)\n\
	# Nothing to do\n\
	;;\n\
*)\n\
	echo \"Usage: /etc/init.d/hsmdump { start | stop }\"\n\
	;;\n\
esac\n\
exit 0\n";

static void
usage(void)
{
	(void) fprintf(stderr, gettext(
	"Usage: %s [-U database_host] [-O operd_host] [-I dumpdates_file]\n"),
		progname);
	exit(1);
}

int
main(argc, argv)
	int argc;
	char **argv;
{
	struct stat stbuf;
	FILE *fp;
	int c;

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	progname = strrchr(argv[0], '/');
	if (progname == (char *)0)
		progname = argv[0];
	else
		progname++;

	if (sysinfo(SI_HOSTNAME, host, MAXHOSTNAMELEN) == -1) {
		(void) fprintf(stderr, gettext(
			"%s: Cannot determine host name: %s\n"),
			progname, strerror(errno));
		exit(1);
	}

	while ((c = getopt(argc, argv, "U:O:I:")) != -1) {
		switch (c) {
		case 'U':
			database = optarg;
			break;
		case 'O':
			operd = optarg;
			break;
		case 'I':
			dumpdates = optarg;
			break;
		default:
			usage();
		}
	}

	/* look for a matching groupid before we get started */
	grp = getgrnam(OPERATOR);

	/* first, create the path to the $HSMROOT/etc directory */
	do_directories();

	/* now, if we have a dumpdates file that doesn't exist, make one */
	if (dumpdates && stat(dumpdates, &stbuf) == -1) {
		int fd;

		fd = open(dumpdates, O_RDWR|O_CREAT, 0666);
		if (fd == -1) {
			(void) fprintf(stderr, gettext(
				"%s: Cannot create `%s:%s': %s\n"),
				progname, host, dumpdates,
				strerror(errno));
		} else {
			(void) close(fd);
			if (grp)
				(void) chown(dumpdates, (uid_t)-1, grp->gr_gid);
			if (filemode)
				(void) chmod(dumpdates, filemode);
		}
	}

	/* update the dump.conf file if it doesn't already exist */
	if (database || operd) {
		struct stat stbuf;
		char dumpconf[PATH_MAX+1];

		(void) sprintf(dumpconf, "%s/dump.conf", gethsmpath(etcdir));
		if (stat(dumpconf, &stbuf) == -1 && errno == ENOENT) {
			fp = fopen(dumpconf, "w+");
			if (fp == (FILE *) NULL) {
				(void) fprintf(stderr, gettext(
					"%s: Cannot create `%s:%s': %s\n"),
					progname, host, dumpconf,
					strerror(errno));
			} else {
				char *hdr = "# hsmdump configuration file\n\n";

				if (fputs(hdr, fp) == EOF ||
				    (operd && fprintf(fp, "operd\t\t%s\n",
					operd) == EOF) ||
				    (database && fprintf(fp, "database\t%s\n",
					database) == EOF) ||
				    fclose(fp) == EOF) {
					(void) unlink(dumpconf);
					(void) fprintf(stderr, gettext(
					    "%s: Cannot write `%s:%s': %s\n"),
					    progname, host, dumpconf,
					    strerror(errno));
				}

				/* we wrote it; fix owner/group/modes */
				if (grp)
					(void) chown(dumpconf,
					    (uid_t)-1, grp->gr_gid);
				if (filemode)
					(void) chmod(dumpconf, filemode);
			}
		}
	}

	/* create the /etc/init.d/hsmdump startup script if necessary */
	if (stat(hsmdump_rc, &stbuf) == -1 && errno == ENOENT) {
		/* not there -- try to make it, always being silent */
		char **cpp;

		fp = fopen(hsmdump_rc, "w+");
		if (fp != NULL) {
			(void) fprintf(fp, hsmdump, gethsmpath(libdir),
			    gethsmpath(libdir), gethsmpath(sbindir));
			(void) fclose(fp);
			if (grp)
				(void) chown(hsmdump_rc, (uid_t)-1,
				    grp->gr_gid);
			(void) chmod(hsmdump_rc, 0744);

			/* now, make the proper links, silently */
			for (cpp = hsmdump_links; *cpp; cpp++) {
				(void) unlink(*cpp);
				(void) link(hsmdump_rc, *cpp);
			}
		}
	}

	exit(0);
#ifdef lint
	return (0);
#endif
}

/*
 * Create all the magic directories that are needed to support auto-client
 * configuration.
 */
static void
do_directories(void)
{
	char dirpath[PATH_MAX+1];

	/* we must be able to make the etc and etc/dumps directories... */
	do_directory(gethsmpath(etcdir), 1);	/* fatal */

	/* The "etc" directory is now there.  Let's make the "dumps" dir too */
	(void) sprintf(dirpath, "%s/dumps", gethsmpath(etcdir));
	do_directory(dirpath, 1);		/* fatal */

	do_directory(gethsmpath(admdir), 0);	/* not fatal */

	/* for grins, try to create the "adm/dumplog" directory */
	(void) sprintf(dirpath, "%s/dumplog", gethsmpath(admdir));
	do_directory(dirpath, 0);		/* not fatal */
}

static void
do_directory(char *xdir, int fatal)
{
	char linkpath[PATH_MAX+1];
	char *s, *cp;
	struct stat stbuf;
	int cnt;

	if (stat(xdir, &stbuf) != -1) {
		if (S_ISDIR(stbuf.st_mode)) {
			return;
		} else if (fatal) {
			(void) fprintf(stderr, gettext(
			    "%s: path `%s:%s' must be a directory\n"),
			    progname, host, xdir);
			exit(1);
		}
		return;
	}

	/* if it's not a link or still can't stat it, just make it */
	if (lstat(xdir, &stbuf) == -1) {
		if (mkdir(xdir, 0777) == -1) {
			if (fatal) {
				(void) fprintf(stderr, gettext(
				    "%s: Cannot make directory `%s:%s': %s\n"),
				    progname, host, xdir,
				    strerror(errno));
				exit(1);
			}
			return;
		} else {
			if (grp)
				(void) chown(xdir, (uid_t)-1, grp->gr_gid);
			if (dirmode)
				(void) chmod(xdir, dirmode);
		}
	}

	/*
	 * If we can read the link, build each directory that
	 * the link points to, one at a time.
	 *
	 * We check for EINVAL bacause the test above told us that it is a
	 * directory (or points to a directory) if it exists.
	 */
	cnt = readlink(xdir, linkpath, sizeof (linkpath) - 1);
	if (cnt == -1) {
		if (errno != EINVAL) {
			if (fatal) {
				(void) fprintf(stderr, gettext(
			"%s: Cannot read symbolic link `%s:%s': %s\n"),
					progname, host, xdir, strerror(errno));
				exit(1);
			} else
				return;
		}
	}
	linkpath[cnt] = '\0';		/* NULL-terminate */

	for (s = linkpath; (cp = strchr(s, '/')) != NULL; s = cp + 1) {
		*cp = '\0';
		if (!*linkpath) {
			*cp = '/';
			continue;
		}
		if (mkdir(linkpath, 0777) == -1) {
			if (errno != EROFS && errno != EEXIST) {
				if (fatal) {
					(void) fprintf(stderr, gettext(
				    "%s: Cannot make directory `%s:%s': %s\n"),
						progname, host, linkpath,
						strerror(errno));
					exit(1);
				}
				return;
			}
		} else {
			if (grp)
				(void) chown(linkpath, (uid_t)-1, grp->gr_gid);
			if (dirmode)
				(void) chmod(linkpath, dirmode);
		}
		*cp = '/';
	}
	if (mkdir(linkpath, 0777) == -1) {
		if (errno != EROFS && errno != EEXIST) {
			if (fatal) {
				(void) fprintf(stderr, gettext(
				    "%s: Cannot make directory `%s:%s': %s\n"),
				    progname, host, linkpath,
				    strerror(errno));
				exit(1);
			}
			return;
		}
	} else {
		if (grp)
			(void) chown(linkpath, (uid_t)-1, grp->gr_gid);
		if (dirmode)
			(void) chmod(linkpath, dirmode);
	}

}
