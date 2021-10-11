
#ident "@(#)rebuildtape.c 1.11 93/05/12"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include "defs.h"
#define	_POSIX_SOURCE	/* hack to avoid redef of MAXNAMLEN */
#define	_POSIX_C_SOURCE
#include <dirent.h>
#undef	_POSIX_C_SOURCE
#undef	_POSIX_SOURCE
#include <sys/stat.h>
#include <sys/mman.h>

#ifdef __STDC__
static void dohost(const char *);
#else
static void dohost();
#endif

/*
 * rebuild an `activetapes' file by reading the headers of all
 * dumps in this database.
 */
void
rebuildtape(dbroot)
	char *dbroot;
{
	char root[256];
	char thishost[BCHOSTNAMELEN+1];
	DIR *dp;
	struct dirent *de;

	if (gethostname(thishost, BCHOSTNAMELEN)) {
		perror("gethostname");
		exit(1);
	}

	if (dbroot == NULL) {
		(void) fprintf(stderr, gettext(
			"Enter root of database file hierarchy: "));
		if (gets(root) == NULL)
			exit(1);
		dbroot = root;
	}
	if (chdir(dbroot) == -1) {
		perror("chdir");
		(void) fprintf(stderr,
			gettext("cannot cd to database root %s\n"), dbroot);
		exit(1);
	}
	maint_lock();
	pokeserver(QUIESCE_OPERATION, thishost);

	(void) unlink(TAPEFILE);

	(void) tape_open(".");
	if ((dp = opendir(".")) == NULL) {
		(void) fprintf(stderr, gettext(
			"cannot open database root directory `%s'\n"), dbroot);
		goto done;
	}

	while (de = readdir(dp)) {
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		if (strcmp(INSTANCECONFIG, de->d_name) == 0)
			continue;
		if (strcmp(TAPEFILE, de->d_name) == 0)
			continue;
		if (strcmp(UTIL_LOCKFILE, de->d_name) == 0)
			continue;
		if (strcmp(DBSERV_LOCKFILE, de->d_name) == 0)
			continue;
		if (strchr(de->d_name, '.') != NULL &&
		    isdigit(de->d_name[strlen(de->d_name) - 1]))
			dohost(de->d_name);
	}
	(void) closedir(dp);

done:
	tape_close(".");
	tape_trans(".");
	pokeserver(RESUME_OPERATION, thishost);
	maint_unlock();
}

static void
#ifdef __STDC__
dohost(const char *name)
#else
dohost(name)
	char *name;
#endif
{
	DIR *dp;
	struct dirent *de;
	char headername[MAXPATHLEN];
	static struct dheader *dh;
	static int dhsize;
	int fd;
	struct stat stbuf;
	register int i;
	char *p;
	u_long hostnum, dumpid, recnum;
	extern u_long inet_addr();

	if ((dp = opendir(name)) == NULL) {
		return;
	}

	if ((p = strchr(name, '.')) == NULL ||
	    (hostnum = inet_addr(++p)) == (u_long) -1) {
		(void) fprintf(stderr, gettext(
		    "%s: Warning: Cannot get internet address from `%s'\n"),
		    "dohost", name);
		(void) closedir(dp);
		return;
	}

	if (dh == (struct dheader *)0) {
		/*
		 * large enough for a dump which spans 50 tapes...
		 */
		dhsize = sizeof (struct dheader) + 50*LBLSIZE;
		dh = (struct dheader *)malloc((unsigned)dhsize);
		if (dh == NULL) {
			(void) fprintf(stderr,
				gettext("%s: out of memory\n"), "dohost");
			exit(1);
		}
	}
	while (de = readdir(dp)) {
		if (strncmp(de->d_name, HEADERFILE, strlen(HEADERFILE)))
			continue;
		(void) sprintf(headername, "%s/%s", name, de->d_name);
		if ((fd = open(headername, O_RDONLY)) == -1) {
			perror(headername);
			continue;
		}
		if (fstat(fd, &stbuf) == -1) {
			perror("stat");
			(void) close(fd);
			continue;
		}
		if (stbuf.st_size > dhsize) {
			(void) fprintf(stderr,
				gettext("%s too big!\n"), headername);
			(void) close(fd);
			continue;
		}
		if (read(fd, (char *)dh,
				(int)stbuf.st_size) != (int)stbuf.st_size) {
			perror("read");
			(void) close(fd);
			continue;
		}
		(void) close(fd);
		if ((p = strchr(de->d_name, '.')) == NULL) {
			(void) fprintf(stderr, gettext(
				"cannot get dumpid from `%s'\n"), de->d_name);
			continue;
		}
		if (sscanf(++p, "%lu", &dumpid) != 1) {
			(void) fprintf(stderr, gettext(
				"cannot get dumpid from `%s'\n"), de->d_name);
			continue;
		}
		for (i = 0; i < dh->dh_ntapes; i++) {
			u_long filenum;

			if (i == 0)
				filenum = dh->dh_position;
			else
				filenum = 1;
			if (tape_lookup(dh->dh_label[i],
					&recnum) == NULL_TREC) {
				recnum = tape_newrec(dh->dh_label[i],
						NONEXISTENT_BLOCK);
			}
			(void) tape_addent(recnum, hostnum, dumpid, filenum);
		}
	}
	(void) closedir(dp);
}
