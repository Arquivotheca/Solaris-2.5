
#ident	"@(#)rebuilddir.c 1.13 93/04/28"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include "defs.h"
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <database/backupdb.h>
#include <database/dbserv.h>
#include <database/dir.h>
#include <database/instance.h>
#include <database/dnode.h>

#ifndef USG
extern void exit();
#endif

/*
 * rebuild 'dir' and 'instances' files for a given host.
 * Existing `dir' and `instance' files (if any) are removed, and
 * then the header/dnode pairs for each dump are read and processed.
 * We assume that dnode files are ordered
 * such that sequential processing gives a breadth-first traversal
 * of the file system hierarchy.
 */

struct parent {
	struct parent *nxt, *prv;
	u_long dnode;
	u_long chld_dirblk;
	char *name;
	u_long this_dirblk;
};

#define	NULL_PARENT	(struct parent *)0

#ifdef __STDC__
static int is_tempfile(const char *name);
static void process_dump(const char *, u_long);
static void getname(FILE *, char *, u_long, int *);
static void freeparent(struct parent *);
static void newparents(void);
static struct parent *getparent(u_long);
static void newparent(const char *name, u_long, u_long, u_long);
#else
static int is_tempfile();
static void process_dump();
static void getname();
static void freeparent();
static void newparents();
static struct parent *getparent();
static void newparent();
#endif

void
rebuilddir(dbroot, dbhost, all)
	char *dbroot;
	char *dbhost;
	int all;
{
	char root[256];
	DIR *dp = NULL;
	char thishost[BCHOSTNAMELEN+1];

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

	if (gethostname(thishost, BCHOSTNAMELEN)) {
		perror("gethostname");
		exit(1);
	}

	if (all) {
		struct dirent *de;
		struct stat s[1];
		DIR *dot;

		if (dbhost) {
			fprintf(stderr,
			    gettext(
	"warning: `%s' flag ignored with `%s' flag; processing all hosts\n"),
			    "-h", "-a");
		}
		if ((dot = opendir(".")) == NULL) {
			perror(dbroot);
			exit(1);
		}

		maint_lock();
		pokeserver(QUIESCE_OPERATION, thishost);

		while (de = readdir(dot)) {
			if ((de->d_name[0] == '.') &&
			    ((de->d_name[1] == '\0') ||
				((de->d_name[1] == '.') &&
				    (de->d_name[2] == '\0'))))
				continue;

			if (stat(de->d_name, s))
				/* empty link? */
				continue;

			if (! S_ISDIR(s->st_mode))
				continue;

			if ((dp = opendir(de->d_name)) == NULL) {
				(void) fprintf(stderr, gettext(
				    "Cannot open directory `%s'\n"),
				    de->d_name);
				perror(de->d_name);
				continue;
			}

			rebuildone(de->d_name, dp);
		}
		(void) closedir(dot);
	} else {
		dbhost = getdbhost(dbhost);
		while (dbhost == NULL ||
			(dp == NULL && ((dp = opendir(dbhost)) == NULL))) {
			if (dbhost)
				(void) fprintf(stderr, gettext(
				    "Cannot open directory `%s'\n"), dbhost);
				dbhost = getdbhost((char *)NULL);
		}

		maint_lock();
		pokeserver(QUIESCE_OPERATION, thishost);

		rebuildone(dbhost, dp);
	}

	pokeserver(RESUME_OPERATION, thishost);
	maint_unlock();
}

void
rebuildone(dbhost, dp)
	char *dbhost;
	DIR *dp;
{
	char fullpath[256], goodhost[256], *c;
	struct dirent *de;
	int pid, ndumps;

	(void) sprintf(fullpath, "%s/%s", dbhost, INSTANCEFILE);
	(void) unlink(fullpath);	/* XXX: don't care about status */
	(void) sprintf(fullpath, "%s/%s", dbhost, DIRFILE);
	(void) unlink(fullpath);	/* XXX: don't care about status */

	strcpy(goodhost, dbhost);
	for (c = goodhost; c = strchr(c, '.'); c++)
		if (isdigit(c[1])) {
			*c = '\0';
			break;
		}

	ndumps = 0;
	while (de = readdir(dp)) {
		if (strncmp(de->d_name, DNODEFILE, strlen(DNODEFILE)) == 0) {
			u_long dumpid;
			char fmt[256];

			(void) strcpy(fmt, DNODEFILE);
			(void) strcat(fmt, ".");
			(void) strcat(fmt, "%d");
			if (sscanf(de->d_name, fmt, &dumpid) != 1) {
				(void) fprintf(stderr, gettext(
					"cannot get dumpid for file %s\n"),
					de->d_name);
				continue;
			}
			if ((pid = fork()) == -1) {
				perror("fork");
				exit(1);
			} else if (pid) {
				(void) printf(gettext(
				    "%s: dump #%d, dumpid: %lu\n"),
					goodhost, ++ndumps, dumpid);
				if (waitpid(pid, (int *)NULL, 0) == -1)
					perror("waitpid");
			} else {
				newparents();
				process_dump(dbhost, dumpid);
				exit(1);
			}
		} else if (is_tempfile(de->d_name)) {
			(void) sprintf(fullpath, "%s/%s", dbhost, de->d_name);
			(void) unlink(fullpath);
		}
	}
	(void) closedir(dp);
}

static int
#ifdef __STDC__
is_tempfile(const char *name)
#else
is_tempfile(name)
	char *name;
#endif
{
	register char *p;

	if (strncmp(name, TEMP_PREFIX, strlen(TEMP_PREFIX)) == 0 ||
	    strncmp(name, UPDATE_INPROGRESS, strlen(UPDATE_INPROGRESS)) == 0 ||
	    strncmp(name, UPDATE_DONE, strlen(UPDATE_DONE)) == 0) {
		return (1);
	} else {
		p = strstr(name, MAP_SUFFIX);
		if (p && strcmp(p, MAP_SUFFIX) == 0)
			return (1);
		p = strstr(name, TRANS_SUFFIX);
		if (p && strcmp(p, TRANS_SUFFIX) == 0)
			return (1);
	}
	return (0);
}

static int pathoffset;

static void
#ifdef __STDC__
process_dump(const char *host,
	u_long dumpid)
#else
process_dump(host, dumpid)
	char *host;
	u_long dumpid;
#endif
{
	FILE *dfp, *pfp;
	char fullpath[1024];
	char thisname[256];
	int namelen;
	struct dnode dnode;
	u_long dnode_num, pdn;
	struct parent *pp;

	struct dir_block *dbp;
	struct dir_entry *dep;
	u_long dirblk, instancerec;
	u_long mydirblk;

	(void) sprintf(fullpath, "%s/%s.%lu", host, DNODEFILE, dumpid);
	if ((dfp = fopen(fullpath, "r")) == NULL) {
		(void) fprintf(stderr, gettext(
			"cannot open dnode file '%s'\n"), fullpath);
		exit(1);
	}
	(void) sprintf(fullpath, "%s/%s.%lu", host, PATHFILE, dumpid);
	if ((pfp = fopen(fullpath, "r")) == NULL) {
		(void) fprintf(stderr,
		    gettext("cannot open pathcomponent file '%s'\n"), fullpath);
		exit(1);
	}
	pathoffset = 0;

	if (instance_open(host)) {
		(void) fprintf(stderr, gettext(
			"cannot open instance for host %s\n"), host);
		exit(1);
	}
	if (dir_open(host)) {
		(void) fprintf(stderr,
			gettext("cannot open dir for host %s\n"), host);
		exit(1);
	}

	pp = NULL_PARENT;
	pdn = NONEXISTENT_BLOCK;
	dnode_num = 0;
	mydirblk = DIR_ROOTBLK;
	while (fread((char *)&dnode, sizeof (struct dnode), 1, dfp) == 1) {
		getname(pfp, thisname, dnode.dn_filename, &namelen);
		instancerec = NONEXISTENT_BLOCK;
		if (dnode.dn_parent != pdn) {
			if (pp)
				freeparent(pp);
			pp = getparent(dnode.dn_parent);
			if (pp == NULL_PARENT) {
				(void) fprintf(stderr,
					gettext("%s error\n"), "getparent");
				exit(1);
			}
			pdn = dnode.dn_parent;
			if (pp->chld_dirblk == NONEXISTENT_BLOCK) {
#if 0
				(void) dir_newsubdir(pp->this_dirblk, pp->ep);
				pp->chld_dirblk = pp->ep->de_directory;
#else
				(void) fprintf(stderr,
					gettext("cannot get mydirblk!\n"));
#endif
			}
			mydirblk = pp->chld_dirblk;
		}
		dirblk = mydirblk;
		if ((dep = dir_name_getblock(&dirblk,
				&dbp, thisname, namelen)) == NULL_DIRENTRY) {
			if (dnode.dn_mtime)
				instancerec =
					instance_newrec(NONEXISTENT_BLOCK);
			dirblk = mydirblk;
			dep = dir_addent(&dirblk, &dbp, thisname,
					namelen, instancerec);
		} else {
			instancerec = dep->de_instances;
			if (dep->de_instances == NONEXISTENT_BLOCK &&
							dnode.dn_mtime) {
				instancerec =
					instance_newrec(NONEXISTENT_BLOCK);
				(void) dir_add_instance(dirblk,
					dep, instancerec);
			}
		}
		if (instancerec != NONEXISTENT_BLOCK && dnode.dn_mtime) {
			(void) instance_addent(instancerec, dumpid, dnode_num);
		}
		if (S_ISDIR(dnode.dn_mode) || dnode.dn_mtime == 0) {
			if (S_ISDIR(dnode.dn_mode) &&
				dep->de_directory == NONEXISTENT_BLOCK) {
				(void) dir_newsubdir(dirblk, dep);
			}
			newparent(thisname, dirblk,
				dep->de_directory, dnode_num);
		}

		dnode_num++;
	}
	(void) fclose(dfp);
	(void) fclose(pfp);

	instance_close(host);
	dir_close(host);
	dir_trans(host);
	instance_trans(host);
}

static void
getname(fp, p, offset, namelen)
	FILE *fp;
	char *p;
	u_long offset;
	int *namelen;
{
	char c;
	register int len = 0;

	if (offset != pathoffset) {
		if (fseek(fp, (off_t)offset, 0) != 0) {
			(void) fprintf(stderr, gettext(
				"%s: %s error\n"), "getname", "fseek");
			exit(1);
		}
	}
	while (c = getc(fp)) {
		*p++ = c;
		len++;
	}
	*p = '\0';
	*namelen = len;
	pathoffset = offset + len + 1;
}

static struct {
	struct parent *nxt, *prv;
} plist = {
	(struct parent *)&plist,
	(struct parent *)&plist
};

static void
freeparent(pp)
	struct parent *pp;
{
	free(pp->name);
	pp->prv->nxt = pp->nxt;
	pp->nxt->prv = pp->prv;
	free((char *)pp);
}

static void
#ifdef __STDC__
newparents(void)
#else
newparents()
#endif
{
	register struct parent *p, *np;

	p = plist.nxt;
	if (p == (struct parent *)&plist)
		return;

	do {
		np = p->nxt;
		freeparent(p);
		p = np;
	} while (p != (struct parent *)&plist);
}

static struct parent *
getparent(dnode)
	u_long dnode;
{
	struct parent *p = plist.nxt;
	u_long blk;
	struct dir_block *bp;
	struct dir_entry *ep;

	if (p == (struct parent *)&plist)
		return ((struct parent *)0);
	do {
		if (p->dnode == dnode) {
			if (p->chld_dirblk == NONEXISTENT_BLOCK) {
				blk = p->this_dirblk;
				ep = dir_name_getblock(&blk, &bp,
						p->name, strlen(p->name));
				if (ep == NULL_DIRENTRY)
					return ((struct parent *)0);
				if (ep->de_instances == NONEXISTENT_BLOCK) {
					u_long irec;

					/*
					 * we make sure there is an instance
					 * record for any dir entry that
					 * is a `parent'.  This handles the
					 * case where we dump a long mount
					 * point (e.g., '/usr/export/home/rmtc'
					 * before dumping the fs that contains
					 * the mounted-on directory.
					 */
					irec = instance_newrec(
							NONEXISTENT_BLOCK);
					if (dir_add_instance(blk, ep, irec)) {
						(void) fprintf(stderr, gettext(
						    "%s error\n"),
							"dir_add_instance");
					}
					(void) instance_addent(irec, (u_long)0,
								(u_long)0);
				}
				(void) dir_newsubdir(blk, ep);
				p->chld_dirblk = ep->de_directory;
			}
			return (p);
		}
		p = p->nxt;
	} while (p != (struct parent *)&plist);
	return ((struct parent *)0);
}

void
#ifdef __STDC__
newparent(const char *name,
	u_long dirblk,
	u_long chld,
	u_long dnode)
#else
newparent(name, dirblk, chld, dnode)
	char *name;
	u_long dirblk, chld;
	u_long dnode;
#endif
{
	struct parent *pp, *tp;

	if ((pp = (struct parent *)malloc(
			sizeof (struct parent))) == (struct parent *)0) {
		(void) fprintf(stderr,
			gettext("%s: out of memory"), "newparent");
		exit(1);
	}
	if ((pp->name = (char *)malloc((unsigned)(strlen(name)+1))) == NULL) {
		(void) fprintf(stderr,
			gettext("%s: out of memory"), "newparent");
		exit(1);
	}
	(void) strcpy(pp->name, name);
	pp->dnode = dnode;
	pp->this_dirblk = dirblk;
	pp->chld_dirblk = chld;
	tp = plist.prv->nxt;
	plist.prv->nxt = pp;
	pp->nxt = tp;
	pp->prv = plist.prv;
	tp->prv = pp;
}
