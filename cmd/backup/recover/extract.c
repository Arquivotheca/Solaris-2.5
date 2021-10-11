#ident	"@(#)extract.c 1.43 94/08/10"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "config.h"
#include "recover.h"
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#ifndef USG
#include <vfork.h>
#endif
#include <recrest.h>
#include "cmds.h"
#include "extract.h"

int recover_verbose = 0;	/* I get sick of those hard link messages... */

/*
 * support for the file extraction list
 */
static struct tape_list *extractlist;
static int listcnt;
static int already_onlist;

#ifdef __STDC__
static int add(char *, struct dir_entry *, char *, char *, int, time_t);
static int addone(char *, struct dnode *, u_long, char *, char *, int);
static void add_dirs(char *, char *, char *fullname, time_t);
static void chk_direntry(char *, u_long, struct dnode *, char *, int);
static void delete(char *, struct dir_entry *, time_t, char *);
static struct file_list *get_extfile(u_long, u_long,
	struct dump_list **, struct tape_list **);
static struct tape_list *add_dumptapes(struct dheader *, u_long);
static struct tape_list *locate_tape(char *, struct tape_list **);
static struct dump_list *newdump(u_long, u_long, time_t);
static struct file_list *newfile(struct dnode *, char *, int);
static struct file_list *make_link(struct file_list *, char *);
static void init_namehash(void);
static int name_hash(char *);
static void addtonamehash(struct file_list *);
static struct file_list *hash_namelookup(char *);
static void addtoinodehash(struct hash_head *, struct file_list *);
static struct file_list *hash_inodelookup(struct hash_head *, u_long);
static void catcher(int);
static void clear_list(void);
#else
static int add();
static int addone();
static void add_dirs();
static void chk_direntry();
static void delete();
static struct file_list *get_extfile();
static struct tape_list *add_dumptapes();
static struct tape_list *locate_tape();
static struct dump_list *newdump();
static struct file_list *newfile();
static struct file_list *make_link();
static void init_namehash();
static int name_hash();
static void addtonamehash();
static struct file_list *hash_namelookup();
static void addtoinodehash();
static struct file_list *hash_inodelookup();
static void catcher();
static void clear_list();
#endif

/*
 * see if a given file (identified by dumpid and inode number)
 * is on the extraction list
 */
int
onextractlist(dumpid, inode)
	u_long dumpid;
	u_long inode;
{
	register struct tape_list *t;
	register struct dump_list *d;

	for (t = extractlist; t; t = t->nxt_tapelist) {
		for (d = t->dump_list; d; d = d->nxt_dumplist) {
			if (d->dumpid == dumpid) {
				if (hash_inodelookup(d->inohash, inode))
					return (1);
			}
		}
	}
	return (0);
}

int
#ifdef __STDC__
check_extractlist(void)
#else
check_extractlist()
#endif
{
	int rc = 1;

	if (extractlist && listcnt) {
		rc = yesorno(gettext(
			"Extract list is not empty.  Exit anyway?"));
	}
	return (rc);
}

/*
 * add the given files to the extraction list
 */
void
addfiles(host, localdir, ap, newname, timestamp)
	char *host;
	char *localdir;
	struct arglist *ap;
	char *newname;
	time_t	timestamp;
{
	register struct afile *p = ap->head;
	char addname[MAXPATHLEN+1];
	int inlocaldir, localdirlen, namelen;
	int onlist, inplace, argcnt;

	already_onlist = inplace = argcnt = 0;
	onlist = listcnt;
	localdirlen = strlen(localdir);
	while (p < ap->last) {
		argcnt++;
		inlocaldir = 0;
		if (newname) {
			namelen = strlen(newname);
			if (namelen > MAXPATHLEN) {
				(void) fprintf(stderr,
					gettext("name too long\n"));
				return;
			}
			if (p != ap->head) {
				(void) fprintf(stderr,
					gettext("newname botch\n"));
				return;
			}
			if (*newname == '/') {
				(void) strcpy(addname, newname);
			} else {
				if ((localdirlen+namelen+1) > MAXPATHLEN) {
					(void) fprintf(stderr,
						gettext("name too long\n"));
					return;
				}
				(void) sprintf(addname, "%s/%s",
						localdir, newname);
			}
		} else {
			namelen = strlen(p->name);
			if (namelen > MAXPATHLEN) {
				(void) fprintf(stderr,
					gettext("name too long\n"));
				return;
			}
			if ((strcmp(localdir, p->name) == 0) ||
			    ((p->name[localdirlen] == '/') &&
			    (strncmp(localdir, p->name, localdirlen) == 0))) {
				/*
				 * a special case: we don't prepend the
				 * local dir when we're recovering files
				 * from there.  (i.e., we don't want to
				 * 	recover `/home/jde/foo' to
				 * 	`/home/jde/home/jde/foo')
				 */
				inlocaldir = 1;
				inplace++;
				(void) strcpy(addname, p->name);
			} else if (strcmp(p->name, "/") == 0) {
				(void) strcpy(addname, localdir);
			} else {
				if ((localdirlen+namelen+1) > MAXPATHLEN) {
					(void) fprintf(stderr,
						gettext("name too long\n"));
					return;
				}
				(void) sprintf(addname, "%s%s",
						localdir, p->name);
			}
		}
		smashpath(addname);
		if (add(host, p->dep, addname, p->name,
				(int) newname, timestamp) == -1) {
			if (p->expanded == 0)
				(void) printf(gettext("`%s' not found\n"),
					p->name);
			else
				(void) printf(gettext(
					"bad file specification\n"));
		} else if (!newname) {
			if (inlocaldir)
				add_dirs(host, "", p->name, timestamp);
			else
				add_dirs(host, localdir, p->name, timestamp);
		}
		p++;
	}
	if (already_onlist != 0) {
		if (already_onlist == 1)
			(void) printf(gettext(
				"1 file already on the extraction list\n"));
		else
			(void) printf(gettext(
				"%d files already on the extraction list\n"),
				already_onlist);
	}
	onlist = listcnt - onlist;
	if (onlist == 0) {
		(void) printf(gettext("nothing added to extraction list\n"));
		return;
	}

	/*
	 * print a message telling the results of the add command
	 * (so user is not confused about how name is built)
	 */
	if (newname) {
		if (onlist > 1) {
			if (*newname == '/')
				(void) printf(gettext(
				    "%d files to be restored rooted at %s\n"),
					onlist, newname);
			else
				(void) printf(gettext(
			    "%d files to be restored rooted at %s/%s\n"),
					onlist, localdir, newname);
		} else
			(void) printf(gettext(
			    "1 file to be restored -- `%s'\n"), addname);
	} else {
		if (onlist > 1) {
			if (inplace == argcnt)
				(void) printf(gettext(
				    "%d files to be restored in place\n"),
					onlist);
			else
				(void) printf(gettext(
				    "%d files to be restored rooted at %s\n"),
					onlist, localdir);
		} else {
			if (inplace == argcnt)
				(void) printf(gettext(
				    "1 file to be restored in place\n"));
			else
				(void) printf(gettext(
				    "1 file to be restored rooted at %s\n"),
					localdir);
		}
	}

}

/*
 * delete the given files from the extraction list
 */
void
deletefiles(host, ap, timestamp)
	char *host;
	struct arglist *ap;
	time_t	timestamp;
{
	register struct afile *p = ap->head;

	while (p < ap->last) {
		delete(host, p->dep, timestamp, p->name);
		p++;
	}
	if (listcnt == 0)
		/* make sure all directory stuff is gone too */
		clear_list();
}

/*
 * add a file to the extraction list.  If the file is a directory,
 * we recursively add its subtree.
 */
static int
add(host, ep, newname, dbname, renamed, timestamp)
	char *host;
	struct dir_entry *ep;
	char *newname;
	char *dbname;
	int renamed;
	time_t	timestamp;
{
	struct dnode dn;
	u_long dumpid, startblock, nextblock;
	struct dir_block *dbp;
	struct dir_entry *dep;
	char fullname[MAXPATHLEN];
	char fulldbname[MAXPATHLEN];
	struct dir_block myblock;
	int newnamelen, dbnamelen;

	if (ep == NULL_DIRENTRY) {
		return (-1);
	}

	if ((dumpid = getdnode(host, &dn, ep, VREAD,
			timestamp, LOOKUP_DEFAULT, dbname)) == 0) {
		return (-1);
	}

	if (dumpid != 1)
		/*
		 * dumpid == 1 is a place holder for directories which
		 * have no active instances...
		 */
		if (addone(host, &dn, dumpid, newname, dbname, renamed))
			return (0);

	if (S_ISDIR(dn.dn_mode) && ep->de_directory != NONEXISTENT_BLOCK) {
		if (permchk(&dn, VEXEC, host))
			return (0);
		startblock = nextblock = ep->de_directory;
		do {
			if ((dbp = dir_getblock(nextblock)) == NULL_DIRBLK) {
				(void) fprintf(stderr, "add: dir_getblock\n");
				return (0);
			}
			/*
			 * keep a local copy of the block since we
			 * may lose it out of the cache if there's
			 * a lot of recursion here
			 */
			bcopy((char *)dbp, (char *)&myblock,
				sizeof (struct dir_block));
			newnamelen = strlen(newname);
			dbnamelen = strlen(dbname);
			/*LINTED [alignment ok]*/
			for (dep = (struct dir_entry *)myblock.db_data;
			    /*LINTED [alignment ok]*/
			    dep != DE_END(&myblock);
			    dep = DE_NEXT(dep)) {
				if (strcmp(dep->de_name, ".") == 0 ||
						strcmp(dep->de_name, "..") == 0)
					continue;
				if ((int)(dbnamelen + dep->de_name_len + 2) >
							MAXPATHLEN) {
					(void) fprintf(stderr,
						gettext("name too long\n"));
					continue;
				}
				(void) sprintf(fulldbname, "%s/%s",
						dbname, dep->de_name);
				if ((int)(newnamelen + dep->de_name_len + 2) >
							MAXPATHLEN) {
					(void) fprintf(stderr,
						gettext("name too long\n"));
					continue;
				}
				(void) sprintf(fullname, "%s/%s",
						newname, dep->de_name);
				(void) add(host, dep, fullname,
					fulldbname, renamed, timestamp);
			}
			nextblock = myblock.db_next;
		} while (nextblock != startblock);
	}
	return (0);
}

/*
 * add a single file to the extraction list.
 */
static int
addone(host, dnp, dumpid, newname, dbname, renamed)
	char *host;
	struct dnode *dnp;
	u_long dumpid;
	char *newname;
	char *dbname;
	int renamed;
{
	register struct tape_list *thistape;
	register struct dump_list *d, *ld, *thisdump;
	register struct file_list *thisfile, *front;
	struct dheader *header;
	u_long dumpfile;	/* position of this dump on tape */
	time_t	dumptime;	/* time of dump */

	if ((!S_ISDIR(dnp->dn_mode)) && hash_namelookup(newname)) {
		/* duplicate name */
		already_onlist++;
		return (-1);
	}

	/*
	 * read dumpheader and locate tape label
	 */
	if (header_get(dbserv, host, dumpid, &header)) {
		/* can't read header */
		(void) fprintf(stderr, gettext("Cannot read dump header\n"));
		return (-1);
	}

	/*
	 * XXX: look for label in activetapes.
	 * If tape is offsite, tell
	 * the user - no point in trying to mount it.
	 */

	/*
	 * locate label in tape_list (add a new tape_list entry
	 * if necessary)
	 */
	thistape = add_dumptapes(header, dnp->dn_volid);
	if (dnp->dn_volid)
		/*
		 * when a dump spans volumes it is the first file on
		 * all of the tapes except maybe the first...
		 */
		dumpfile = 1;
	else
		/*
		 * may be other dumps ahead of us on the first tape
		 * volume
		 */
		dumpfile = header->dh_position;
	dumptime = header->dh_time;

	/*
	 * locate dump in this tape's dump list (add a new dump_list
	 * entry if necessary)
	 */
	d = thistape->dump_list;
	ld = thisdump = NULL_DUMPLIST;
	for (; d; d = d->nxt_dumplist) {
		if (d->dumpid == dumpid) {
			thisdump = d;
			break;
		} else if (d->dump_pos > dumpfile) {
			break;
		}
		ld = d;
	}
	if (thisdump == NULL_DUMPLIST) {
		thisdump = newdump(dumpid, dumpfile, dumptime);
		thisdump->nxt_dumplist = d;
		if (ld) {
			ld->nxt_dumplist = thisdump;
		} else {
			thistape->dump_list = thisdump;
		}
	}

	if (S_ISDIR(dnp->dn_mode)) {
		chk_direntry(host, thisdump->dumpid, dnp, newname, renamed);
		return (0);
	}

	/*
	 * Add this file at the end of the file list.  The extract
	 * command is responsible for ordering the list by inode
	 * before passing it to restore...
	 *
	 * If we already have a request for this dump/inode, this
	 * one may be treated as a hard link.  The following
	 * restrictions apply to hard links:
	 *
	 * - addname may not be used.  If a file is initially added using
	 *   `addname' no hard links to it may be created.  Likewise,
	 *   a link to a file placed on the list cannot be made using
	 *   `addname'.
	 * - each linked name must have a distinct entry in the database
	 *   (i.e., can't say `add a; lcd foo; add a')
	 */
	if (thisfile = hash_inodelookup(thisdump->inohash, dnp->dn_inode)) {
		char *p;

		p = strstr(thisfile->name, dbname);
		if ((p && strcmp(p, dbname) == 0) || renamed ||
				(thisfile->flags & F_RENAMED)) {
			(void) fprintf(stderr, gettext(
				"%s: already on extract list as `%s'\n"),
				dbname, thisfile->name);
			already_onlist++;
			return (-1);
		}
		(void) make_link(thisfile, newname);
		if (recover_verbose)
			(void) fprintf(stderr,
			    gettext("%s will be hard linked to %s\n"),
			    newname, thisfile->name);
		listcnt++;
		return (0);
	}
	thisfile = newfile(dnp, newname, renamed);
	if (S_ISDIR(dnp->dn_mode)) {
		(void) fprintf(stderr,
			gettext("unexpected dir in `%s'\n"), "addone");
		front = &thisdump->dir_list;
	} else {
		front = &thisdump->file_list;
	}
	addtoinodehash(thisdump->inohash, thisfile);
	thisfile->nxt_file = front;
	thisfile->prv_file = front->prv_file;
	front->prv_file->nxt_file = thisfile;
	front->prv_file = thisfile;
	if (dnp->dn_flags & DN_MULTITAPE) {
		if (thistape->nxt_tapelist) {
			thistape->nxt_tapelist->flags |= TAPE_FILESPAN;
		}
	}
	(thisdump->file_count)++;
	listcnt++;
	return (0);
}

static void
add_dirs(host, localdir, fullname, timestamp)
	char *host, *localdir, *fullname;
	time_t	timestamp;
{
	register char *cp;
	char *start;
	u_long blk;
	struct dir_block *bp;
	struct dir_entry *ep;
	struct dnode dn;
	char myname[MAXPATHLEN];
	u_long dumpid;

	start = strchr(fullname, '/');
	if (start == 0)
		return;
	for (cp = start; *cp != '\0'; cp++) {
		if (*cp != '/')
			continue;
		*cp = '\0';
		if (*fullname && (ep = dir_path_lookup(&blk, &bp, fullname))) {
			if (dumpid = getdnode(host, &dn, ep, VEXEC,
					timestamp, LOOKUP_DEFAULT, fullname)) {
				if (dumpid != 1) {
					(void) sprintf(myname, "%s%s",
							localdir, fullname);
					chk_direntry(host, dumpid,
							&dn, myname, 0);
				}
			}
		}
		*cp = '/';
	}
}

static void
chk_direntry(host, dumpid, dnp, name, renamed)
	char *host;
	u_long dumpid;
	struct dnode *dnp;
	char *name;
	int renamed;
{
	register struct file_list *f;
	register struct tape_list *t;
	register struct dump_list *d, *td, *ld;
	struct dheader *header;
	int dumpfile;

	if (onextractlist(dumpid, dnp->dn_inode))
		return;
	if (f = hash_namelookup(name)) {
		if (f->dirp->dir_mtime > dnp->dn_mtime)
			return;

		/*
		 * remove it from some other dump's list.
		 */
		f->prv_inode->nxt_inode = f->nxt_inode;
		f->nxt_inode->prv_inode = f->prv_inode;
		f->prv_file->nxt_file = f->nxt_file;
		f->nxt_file->prv_file = f->prv_file;
	} else {
		f = newfile(dnp, name, renamed);
		f->dirp = (struct dirstats *)malloc(sizeof (struct dirstats));
		if (!f->dirp) {
			(void) fprintf(stderr, "Cannot malloc dirdata\n");
			free(f->name);
			free((char *)f);
			return;
		}
	}
	f->dirp->dir_uid = dnp->dn_uid;
	f->dirp->dir_gid = dnp->dn_gid;
	f->dirp->dir_mode = dnp->dn_mode;
	f->dirp->dir_atime = dnp->dn_atime;
	f->dirp->dir_mtime = dnp->dn_mtime;
	/*
	 * add the directory to the appropriate dump_list.
	 */
	for (t = extractlist; t; t = t->nxt_tapelist) {
		for (d = t->dump_list; d; d = d->nxt_dumplist) {
			if (d->dumpid == dumpid) {
				addtoinodehash(d->inohash, f);
				f->nxt_file = d->dir_list.nxt_file;
				d->dir_list.nxt_file->prv_file = f;
				f->prv_file = &d->dir_list;
				d->dir_list.nxt_file = f;
				return;
			}
		}
	}

	/*
	 * here we've got a new dump which wasn't on our list before.
	 * Place it on the extract list, and link this directory to it.
	 */
	if (header_get(dbserv, host, dumpid, &header)) {
		(void) fprintf(stderr, "Cannot read dump header\n");
		f->prv_name->nxt_name = f->nxt_name;
		f->nxt_name->prv_name = f->prv_name;
		free((char *)f->dirp);
		free(f->name);
		free((char *)f);
		return;
	}
	t = add_dumptapes(header, dnp->dn_volid);
	dumpfile = 1;
	if (dnp->dn_volid == 0)
		dumpfile = header->dh_position;
	d = newdump(dumpid, header->dh_position, header->dh_time);
	for (td = t->dump_list, ld = NULL_DUMPLIST; td; td = td->nxt_dumplist) {
		if (td->dump_pos > dumpfile)
			break;
		ld = td;
	}
	if (ld) {
		d->nxt_dumplist = ld->nxt_dumplist;
		ld->nxt_dumplist = d;
	} else {
		d->nxt_dumplist = t->dump_list;
		t->dump_list = d;
	}
	addtoinodehash(d->inohash, f);
	f->nxt_file = d->dir_list.nxt_file;
	d->dir_list.nxt_file->prv_file = f;
	f->prv_file = &d->dir_list;
	d->dir_list.nxt_file = f;
}

/*
 * remove a single entry from the extraction list.  If the entry
 * is a directory, recursively remove its descendents.
 */
static void
delete(host, ep, timestamp, name)
	char *host;
	struct dir_entry *ep;
	time_t	timestamp;
	char *name;
{
	struct file_list *f;
	struct dump_list *d;
	struct tape_list *t;
	struct file_list *l, *pl;
	u_long dumpid;
	struct dnode dn;
	u_long startblock, nextblock;
	struct dir_block *dbp, myblock;
	struct dir_entry *dep;
	char *p;
	char fullname[MAXPATHLEN];
	int namelen;

	if ((dumpid = getdnode(host, &dn, ep, VREAD,
			timestamp, LOOKUP_DEFAULT, name)) == 0)
		return;
	if (f = get_extfile(dumpid, dn.dn_inode, &d, &t)) {
		/*
		 * remove file from all of its lists, and
		 * free the memory used by it.
		 */
		p = strstr(f->name, ep->de_name);
		if (f->lnkp == NULL_FILELIST) {
			/* no links - remove this entry from the list */
			f->prv_name->nxt_name = f->nxt_name;
			f->nxt_name->prv_name = f->prv_name;
			free(f->name);
			if (f->dirp)
				free((char *)f->dirp);
			else
				listcnt--;
			f->prv_inode->nxt_inode = f->nxt_inode;
			f->nxt_inode->prv_inode = f->prv_inode;
			f->prv_file->nxt_file = f->nxt_file;
			f->nxt_file->prv_file = f->prv_file;
			free((char *)f);
			(d->file_count)--;
		} else if (p && (strcmp(p, ep->de_name) == 0)) {
			/*
			 * removing a file that has links.
			 * Promote the first link
			 */
			f->prv_name->nxt_name = f->nxt_name;
			f->nxt_name->prv_name = f->prv_name;
			free(f->name);
			if (f->dirp)
				free((char *)f->dirp);
			else
				listcnt--;

			l = f->lnkp;
			l->nxt_file = f->nxt_file;
			l->prv_file = f->prv_file;
			f->prv_file->nxt_file = l;
			f->nxt_file->prv_file = l;
			l->lnkp = l->nxt_inode;
			l->nxt_inode = f->nxt_inode;
			l->prv_inode = f->prv_inode;
			f->nxt_inode->prv_inode = l;
			f->prv_inode->nxt_inode = l;
			l->file_inode = f->file_inode;
			l->file_offset = 0;	/* == 0 means no seek */
			free((char *)f);
		} else {
			l = f->lnkp;
			pl = NULL_FILELIST;
			while (l) {
				p = strstr(l->name, ep->de_name);
				if (p && (strcmp(p, ep->de_name) == 0)) {
					if (pl)
						pl->nxt_inode = l->nxt_inode;
					else
						f->lnkp = l->nxt_inode;
					l->prv_name->nxt_name = l->nxt_name;
					l->nxt_name->prv_name = l->prv_name;
					free(l->name);
					free((char *)l);
					listcnt--;
					break;
				}
				pl = l;
				l = l->nxt_inode;
			}
		}

		if (dn.dn_flags & DN_MULTITAPE) {
			if (t->nxt_tapelist) {
				t->nxt_tapelist->flags &= ~TAPE_FILESPAN;
			}
		}
	}

	/*
	 * XXX: note that this recursive delete strategy fails if the
	 * database has been updated (e.g., we're trying to take entries
	 * off the list because they've been deleted from the DB).
	 */
	namelen = strlen(name);
	if (S_ISDIR(dn.dn_mode) && ep->de_directory != NONEXISTENT_BLOCK) {
		if (permchk(&dn, VEXEC, host))
			return;
		startblock = nextblock = ep->de_directory;
		do {
			if ((dbp = dir_getblock(nextblock)) == NULL_DIRBLK) {
				(void) fprintf(stderr,
					"delete: dir_getblock\n");
				return;
			}
			/*
			 * keep a local copy of the block since we
			 * may lose it out of the cache if there's
			 * a lot of recursion here
			 */
			bcopy((char *)dbp, (char *)&myblock,
				sizeof (struct dir_block));
			/*LINTED [alignment ok]*/
			for (dep = (struct dir_entry *)myblock.db_data;
			    /*LINTED [alignment ok]*/
			    dep != DE_END(&myblock);
			    dep = DE_NEXT(dep)) {
				if (strcmp(dep->de_name, ".") == 0 ||
						strcmp(dep->de_name, "..") == 0)
					continue;
				if ((int)(namelen + dep->de_name_len + 2) >
							MAXPATHLEN) {
					(void) fprintf(stderr,
						"name too long\n");
					continue;
				}
				(void) sprintf(fullname, "%s/%s",
						name, dep->de_name);
				delete(host, dep, timestamp, fullname);
			}
			nextblock = myblock.db_next;
		} while (nextblock != startblock);
	}
}

static struct file_list *
get_extfile(dumpid, inode, dlist, tlist)
	u_long dumpid;
	u_long inode;
	struct dump_list **dlist;
	struct tape_list **tlist;
{
	register struct tape_list *t;
	register struct dump_list *d;
	register struct file_list *f;

	*dlist = NULL_DUMPLIST;
	*tlist = NULL_TAPELIST;
	for (t = extractlist; t; t = t->nxt_tapelist) {
		for (d = t->dump_list; d; d = d->nxt_dumplist) {
			if (d->dumpid == dumpid) {
				if (f = hash_inodelookup(d->inohash, inode)) {
					*dlist = d;
					*tlist = t;
					return (f);
				}
			}
		}
	}
	return ((struct file_list *)0);
}

static struct tape_list *
add_dumptapes(header, idx)
	struct dheader *header;
	u_long idx;
{
	register struct tape_list *t, *last;
	struct tape_list *prv;
	char *thislabel;
	register int i;

	last = NULL_TAPELIST;
	for (i = 0; i < header->dh_ntapes; i++) {
		thislabel = header->dh_label[i];
		if ((t = locate_tape(thislabel, &prv)) == NULL_TAPELIST) {
			t = (struct tape_list *)
				malloc(sizeof (struct tape_list));
			if (t == NULL_TAPELIST) {
				panic("out of memory\n");
				/*NOTREACHED*/
			}
			t->nxt_tapelist = NULL_TAPELIST;
			t->dump_list = NULL_DUMPLIST;
			t->flags = 0;
			bcopy(thislabel, t->label, LBLSIZE);
			if (last) {
				t->nxt_tapelist = last->nxt_tapelist;
				last->flags |= TAPE_ORDERED;
				last->nxt_tapelist = t;
			} else {
				t->nxt_tapelist = extractlist;
				extractlist = t;
			}
		} else if (prv && last) {
			if (prv == last) {
				/*
				 * already in order
				 */
				prv->flags |= TAPE_ORDERED;
			} else if (prv->flags & TAPE_ORDERED) {
				/*
				 * order breakdown
				 */
				panic("tape order botch\n");
				/*NOTREACHED*/
			} else {
				/*
				 * moving to a new spot.
				 */
				prv->nxt_tapelist = t->nxt_tapelist;
				t->nxt_tapelist = last->nxt_tapelist;
				last->flags |= TAPE_ORDERED;
				last->nxt_tapelist = t;
			}
		} else if (last) {
			/*
			 * moving the node that formerly headed the list
			 */
			extractlist = t->nxt_tapelist;
			last->nxt_tapelist = t;
			t->nxt_tapelist = NULL_TAPELIST;
			last->flags |= TAPE_ORDERED;
		}
		last = t;
	}

	for (t = extractlist; t; t = t->nxt_tapelist) {
		if (bcmp(header->dh_label[idx], t->label, LBLSIZE) == 0)
			return (t);
	}
	panic("tape botch\n");
	/*NOTREACHED*/
}

static struct tape_list *
locate_tape(label, prv)
	char *label;
	struct tape_list **prv;
{
	register struct tape_list *t;

	/*
	 * locate the specified tape label and remove it from the
	 * current extract list (caller will re-attach it in the
	 * appropriate place)
	 */
	*prv = NULL_TAPELIST;
	for (t = extractlist; t; t = t->nxt_tapelist) {
		if (bcmp(label, t->label, LBLSIZE) == 0) {
			return (t);
		}
		*prv = t;
	}
	return (NULL_TAPELIST);
}

static struct dump_list *
newdump(dumpid, dumpfile, dumptime)
	u_long dumpid;
	u_long dumpfile;
	time_t	dumptime;
{
	struct dump_list *thisdump;
	register int i;
	register struct hash_head *hp;

	thisdump = (struct dump_list *)malloc(sizeof (struct dump_list));
	if (thisdump == NULL_DUMPLIST) {
		(void) fprintf(stderr, "Cannot malloc dumplist\n");
		exit(1);
	}
	thisdump->nxt_dumplist = NULL_DUMPLIST;
	hp = (struct hash_head *)malloc(
		INODE_HASHSIZE*sizeof (struct hash_head));
	if (hp == (struct hash_head *)0) {
		(void) fprintf(stderr, "Cannot malloc dumphash\n");
		exit(1);
	}
	thisdump->inohash = hp;
	for (i = 0; i < INODE_HASHSIZE; i++) {
		hp[i].nxt_inode = hp[i].prv_inode = (struct file_list *)&hp[i];
	}
	thisdump->dumpid = dumpid;
	thisdump->dump_pos = dumpfile;
	thisdump->dump_time = dumptime;
	thisdump->file_count = 0;
	thisdump->file_list.nxt_file =
		thisdump->file_list.prv_file = &thisdump->file_list;
	thisdump->dir_list.nxt_file =
		thisdump->dir_list.prv_file = &thisdump->dir_list;
	return (thisdump);
}

static struct file_list *
newfile(dnp, name, renamed)
	struct dnode *dnp;
	char *name;
	int renamed;
{
	struct file_list *fp;

	fp = (struct file_list *)malloc(sizeof (struct file_list));
	if (fp == NULL_FILELIST) {
		(void) fprintf(stderr, "Cannot allocate filelist\n");
		exit(1);
	}
	fp->name = (char *)malloc((unsigned)(strlen(name)+1));
	if (fp->name == NULL) {
		(void) fprintf(stderr, "Cannot allocate filelist name\n");
		exit(1);
	}
	(void) strcpy(fp->name, name);
	fp->file_inode = dnp->dn_inode;
	if (attempt_seek && (dnp->dn_flags & DN_OFFSET))
		fp->file_offset = dnp->dn_vol_position;
	else
		fp->file_offset = 0;	/* == 0 means no seek */
	fp->dirp = NULL;
	fp->lnkp = NULL;
	fp->flags = 0;
	if (renamed)
		fp->flags |= F_RENAMED;
	addtonamehash(fp);
	return (fp);
}

static struct file_list *
make_link(f, name)
	struct file_list *f;
	char *name;
{
	struct file_list *lp;

	lp = (struct file_list *)malloc(sizeof (struct file_list));
	if (lp == NULL_FILELIST) {
		(void) fprintf(stderr, "Cannot allocate linklist\n");
		exit(1);
	}
	lp->name = (char *)malloc((unsigned)(strlen(name)+1));
	if (lp->name == NULL) {
		(void) fprintf(stderr, "Cannot allocate linklist name\n");
		exit(1);
	}
	lp->file_inode = LL_LINK;
	lp->file_offset = 0;
	(void) strcpy(lp->name, name);
	lp->nxt_inode = f->lnkp;
	lp->flags = 0;
	lp->dirp = NULL;
	f->lnkp = lp;
	addtonamehash(lp);
	return (lp);
}

#define	HASHSIZE	31531
static struct hash_head name_buckets[HASHSIZE];
static int name_hash_initted;

static void
#ifdef __STDC__
init_namehash(void)
#else
init_namehash()
#endif
{
	register int i;

	if (name_hash_initted)
		return;

	for (i = 0; i < HASHSIZE; i++) {
		name_buckets[i].nxt_name =
			name_buckets[i].prv_name =
			(struct file_list *)&name_buckets[i];
	}
	name_hash_initted = 1;
}

static int
name_hash(p)
	char *p;
{
	int sum = 0;

	for (; *p; p++)
		sum = (sum << 3) + *p;
	return ((sum&0x7fffffff) % HASHSIZE);
}

static void
addtonamehash(fp)
	struct file_list *fp;
{
	int bucketnum;
	struct file_list *p;

	if (!name_hash_initted)
		init_namehash();

	bucketnum = name_hash(fp->name);
	p = (struct file_list *)&name_buckets[bucketnum];
	fp->nxt_name = p->nxt_name;
	fp->prv_name = p;
	p->nxt_name->prv_name = fp;
	p->nxt_name = fp;
}

static int maxhash;

static struct file_list *
hash_namelookup(name)
	char *name;
{
	register struct file_list *p;
	int bucketnum;
	int cnt;

	if (!name_hash_initted)
		init_namehash();
	cnt = 0;
	bucketnum = name_hash(name);
	for (p = name_buckets[bucketnum].nxt_name;
			p != (struct file_list *)&name_buckets[bucketnum];
			p = p->nxt_name) {
		if (++cnt > maxhash)
			maxhash = cnt;
		if (strcmp(name, p->name) == 0)
			return (p);
	}
	return (NULL_FILELIST);
}

static void
addtoinodehash(hp, fp)
	struct hash_head *hp;
	struct file_list *fp;
{
	register struct file_list *p;

	p = (struct file_list *)&hp[INODE_HASH(fp->file_inode)];
	fp->nxt_inode = p->nxt_inode;
	fp->prv_inode = p;
	p->nxt_inode->prv_inode = fp;
	p->nxt_inode = fp;
}

static struct file_list *
hash_inodelookup(hp, inode)
	struct hash_head *hp;
	u_long inode;
{
	register struct file_list *p;
	static int maxpop;
	int searchcnt;

	searchcnt = 0;
	for (p = hp[INODE_HASH(inode)].nxt_inode;
			p != (struct file_list *)&hp[INODE_HASH(inode)];
			p = p->nxt_inode) {
		if (++searchcnt > maxpop)
			maxpop = searchcnt;
		if (p->file_inode == inode)
			return (p);
	}
	return (NULL_FILELIST);
}

void
#ifdef __STDC__
print_extractlist(void)
#else
print_extractlist()
#endif
{
	struct tape_list *t;
	struct dump_list *d;
	struct file_list *f;
	struct file_list *l;
	char termbuf[MAXPATHLEN+50];

	if (listcnt == 0)
		return;

	term_start_output();
	(void) sprintf(termbuf, "list count: %d\n", listcnt);
	term_putline(termbuf);
	t = extractlist;
	while (t) {
		/* (void) fprintf(outfp, "tape: %s\n", t->label); */
		d = t->dump_list;
		while (d) {
			f = d->file_list.nxt_file;
			while (f != &d->file_list) {
				(void) sprintf(termbuf, "\trecover path: %s\n",
						f->name);
				term_putline(termbuf);
				l = f->lnkp;
				if (l) {
					(void) sprintf(termbuf,
						"\thard links:\n");
					term_putline(termbuf);
				}
				while (l) {
					(void) sprintf(termbuf, "\t\t%s\n",
							l->name);
					term_putline(termbuf);
					l = l->nxt_inode;
				}
				f = f->nxt_file;
			}
			d = d->nxt_dumplist;
		}
		t = t->nxt_tapelist;
	}
	term_finish_output();
}

void
remove_extractlist(dumpid)
	u_long dumpid;
{
	register struct tape_list *t;
	register struct dump_list *d, *lastd;
	register struct file_list *f, *tf;

	lastd = (struct dump_list *)0;
	for (t = extractlist; t; t = t->nxt_tapelist) {
		for (d = t->dump_list; d; d = d->nxt_dumplist) {
			if (d->dumpid == dumpid) {
				f = d->file_list.nxt_file;
				while (f != &d->file_list) {
					tf = f;
					f = f->nxt_file;
					tf->prv_name->nxt_name =
						tf->nxt_name;
					tf->nxt_name->prv_name =
						tf->prv_name;
					tf->prv_inode->nxt_inode =
						tf->nxt_inode;
					tf->nxt_inode->prv_inode =
						tf->prv_inode;
					free(tf->name);
					free((char *)tf);
					listcnt--;
				}
				f = d->dir_list.nxt_file;
				while (f != &d->dir_list) {
					tf = f;
					f = f->nxt_file;
					tf->prv_name->nxt_name =
						tf->nxt_name;
					tf->nxt_name->prv_name =
						tf->prv_name;
					tf->prv_inode->nxt_inode =
						tf->nxt_inode;
					tf->nxt_inode->prv_inode =
						tf->prv_inode;
					free(tf->name);
					free((char *)tf->dirp);
					free((char *)tf);
				}
				if (lastd) {
					lastd->nxt_dumplist = d->nxt_dumplist;
				} else {
					t->dump_list = d->nxt_dumplist;
				}
				free((char *)d);
				break;
			}
			lastd = d;
		}
	}
}

static int gotsig;
static int rest_status;

static void
catcher(sig)
	int sig;
{
	int rc;

	if (sig == SIGCHLD) {
		rc = waitpid(-1, &rest_status,  WNOHANG);
		if (rc && rc != -1) {
			if (!WIFSTOPPED(rest_status))
				gotsig++;
		}
	} else {
		gotsig++;
	}
}

void
extract(notify)
	int notify;
{
	register struct tape_list *t;
	register struct dump_list *d;
	register struct file_list *f;
	register struct file_list *l, *tl;
	FILE *fp;
	char extractfile[256];
	int firsttape, firstdump, pid, lastinode;
#ifdef USG
	sigset_t myset;
#else
	int mymask;
#endif
	register int i, filecnt;
	struct sigvec newvec, ovec, chldvec;
	extern char *tapedev;
	extern char *restorepath;
	char msg[256];

#define	ROOT_INODE	2

	if (extractlist == NULL_TAPELIST || listcnt == 0) {
		(void) printf("extract list is empty\n");
		return;
	}
	(void) sprintf(msg, "Extract list contains %d file%s.  Ok to proceed?",
			listcnt, listcnt == 1 ? "" : "s");
	if (yesorno(msg) == 0)
		return;

#if 1
	/*
	 * XXX: should this be ifdef DEBUG?
	 */
	if (restorepath == NULL) {
		restorepath = getenv("RESTOREPATH");
	}
#endif

	gotsig = 0;
	(void) sprintf(extractfile, "/var/tmp/extractfile.%lu",
		(u_long)getpid());
	if (mkfifo(extractfile, 0600) == -1) {
		perror("extract/mkfifo");
		return;
	}
	setdelays(1);
	newvec.sv_handler = catcher;
#ifdef USG
	(void) sigemptyset(&newvec.sa_mask);
	(void) sigaddset(&newvec.sa_mask, SIGCHLD);
	newvec.sa_flags = 0;
#else
	newvec.sv_mask = sigmask(SIGCHLD);
	newvec.sv_flags = SV_INTERRUPT;
#endif
	if (sigvec(SIGCHLD, &newvec, &chldvec) == -1)
		perror("sigvec");
	/*
	 * XXX: we use vfork() so the new process doesn't have to
	 * do a swap reservation to match ours (our allocated space
	 * can become substantial for very large extract lists...)
	 */
	pid = vfork();
	if (pid == -1) {
		perror("extract/fork");
		return;
	} else if (pid) {
#ifdef USG
		sigset_t newset;

		(void) sigprocmask(0, (sigset_t *)0, &newset);
		(void) sigaddset(&newset, SIGINT);
		(void) sigprocmask(SIG_BLOCK, &newset, &myset);

		(void) sigemptyset(&newvec.sv_mask);
		(void) sigaddset(&newvec.sv_mask, SIGPIPE);
		(void) sigaddset(&newvec.sv_mask, SIGINT);
		newvec.sv_flags = 0;
#else
		mymask = sigblock(sigmask(SIGINT));

		newvec.sv_mask = sigmask(SIGPIPE) | sigmask(SIGINT);
		newvec.sv_flags = SV_INTERRUPT;
#endif
		newvec.sv_handler = catcher;
		if (sigvec(SIGPIPE, &newvec, &ovec))
			perror("sigvec");
	} else {
		char restpath[MAXPATHLEN+1];

		(void) sprintf(restpath, "%s/hsmrestore", gethsmpath(sbindir));
		if (tapedev) {
#if 1
			/*
			 * XXX: should this be DEBUG only?
			 */
			if (restorepath)
				(void) execl(restorepath, "hsmrestore", "Mf",
					extractfile, tapedev, 0);
#endif
			(void) execl(restpath, "hsmrestore", "Mf",
				extractfile, tapedev, 0);
			(void) execl("/usr/sbin/hsmrestore", "hsmrestore",
				"Mf", extractfile, tapedev, 0);
			(void) execl("/usr/etc/hsmrestore", "hsmrestore", "Mf",
				extractfile, tapedev, 0);
			(void) execl("/sbin/hsmrestore", "hsmrestore", "Mf",
				extractfile, tapedev, 0);
		} else {
#if 1
			if (restorepath)
				(void) execl(restorepath, "hsmrestore", "M",
					extractfile, 0);
#endif
			(void) execl(restpath, "hsmrestore", "M",
				extractfile, 0);
			(void) execl("/usr/sbin/hsmrestore", "hsmrestore", "M",
				extractfile, 0);
			(void) execl("/usr/etc/hsmrestore", "hsmrestore", "M",
				extractfile, 0);
			(void) execl("/sbin/hsmrestore", "hsmrestore", "M",
				extractfile, 0);
		}
		perror("execl");
		_exit(1);
	}

	if ((fp = fopen(extractfile, "w")) == NULL) {
		(void) fprintf(stderr,
		    gettext("%s: cannot open fifo\n"), "extract");
		clear_list();
		goto done;
	}

	if (notify) {
		(void) putc(NOTIFYREC, fp);
		(void) putw(notify, fp);
	}

	t = extractlist;
	while (t) {
		firsttape = 1;
		if (t->flags & TAPE_FILESPAN) {
			(void) putc(TAPEREC, fp);
			(void) fwrite(t->label, LBLSIZE, 1, fp);
			firsttape = 0;
		}
		d = t->dump_list;
		while (d) {
			lastinode = ROOT_INODE;
			firstdump = 1;
			filecnt = 0;
			while (filecnt < d->file_count) {
				if (gotsig) {
					goto done;
				}
				if (firsttape) {
					(void) putc(TAPEREC, fp);
					(void) fwrite(t->label, LBLSIZE, 1, fp);
					firsttape = 0;
				}
				if (firstdump) {
					(void) putc(DUMPREC, fp);
					(void) putw(d->dump_pos, fp);
					(void) putw((int)d->dump_time, fp);
					firstdump = 0;
				}
				/*
				 * go in inode order
				 */
				for (i = lastinode; /* ever */; i++) {
					if (f = hash_inodelookup(d->inohash,
								(u_long)i)) {
						if (f->dirp)
							/*
							 * no dirs yet
							 */
							continue;
						lastinode = i+1;
						filecnt++;
						break;
					}
				}
				(void) putc(FILEREC, fp);
				(void) putw((int)f->file_inode, fp);
				(void) putw((int)f->file_offset, fp);
				(void) fputs(f->name, fp);
				(void) putc('\0', fp);
				l = f->lnkp;
				while (l) {
					tl = l;
					l = l->nxt_inode;
					if (tl->file_inode == LL_LINK) {
						(void) putc(LINKREC, fp);
					} else if (tl->file_inode == LL_COPY) {
						(void) putc(COPYREC, fp);
					} else {
						(void) fprintf(stderr,
							"bad lnk flag\n");
						exit(1);
					}
					(void) fputs(tl->name, fp);
					(void) putc('\0', fp);
				}
			}
			d = d->nxt_dumplist;
		}
		t = t->nxt_tapelist;
	}

	t = extractlist;
	while (t) {
		d = t->dump_list;
		while (d) {
			f = d->dir_list.nxt_file;
			while (f != &d->dir_list) {
				if (gotsig) {
					goto done;
				}
				(void) putc(DIRREC, fp);
				(void) putw((int)f->file_inode, fp);
				(void) fputs(f->name, fp);
				(void) putc('\0', fp);
				(void) fwrite((char *)f->dirp,
					sizeof (struct dirstats), 1, fp);
				f = f->nxt_file;
			}
			d = d->nxt_dumplist;
		}
		t = t->nxt_tapelist;
	}
done:
	(void) fclose(fp);
	(void) sigvec(SIGCHLD, &chldvec, (struct sigvec *)NULL);
	(void) waitpid(pid, &rest_status, NULL);
	(void) unlink(extractfile);
	setdelays(0);
	(void) sigvec(SIGPIPE, &ovec, (struct sigvec *)NULL);
	if (WIFSIGNALED(rest_status)) {
		(void) printf("extraction terminated by signal %d\n",
				WTERMSIG(rest_status));
		(void) printf("Extract list unmodified: %d items\n", listcnt);
	} else if (WIFEXITED(rest_status)) {
		(void) printf("extraction complete.  %s exit status: %d\n",
			"hsmrestore", WEXITSTATUS(rest_status));
		if (WEXITSTATUS(rest_status) == 0) {
			(void) printf("Clearing extraction list...\n");
			clear_list();
		} else {
			(void) printf("Extract list unmodified: %d items\n",
				listcnt);
		}
	} else {
		(void) printf("unknown %s termination.  status = 0x%x\n",
				"hsmrestore", rest_status);
		(void) printf("Extract list unmodified: %d items\n", listcnt);
	}
#ifdef USG
	(void) sigprocmask(SIG_SETMASK, &myset, (sigset_t *)0);
#else
	(void) sigsetmask(mymask);
#endif
}

static void
#ifdef __STDC__
clear_list(void)
#else
clear_list()
#endif
{
	register struct tape_list *t, *tt;
	register struct dump_list *d, *td;
	register struct file_list *f, *tf;
	register struct file_list *l, *tl;

	t = extractlist;
	while (t) {
		d = t->dump_list;
		while (d) {
			f = d->file_list.nxt_file;
			while (f != &d->file_list) {
				tf = f;
				f = f->nxt_file;
				l = tf->lnkp;
				while (l) {
					tl = l;
					l = l->nxt_inode;
					tl->prv_name->nxt_name = tl->nxt_name;
					tl->nxt_name->prv_name = tl->prv_name;
					free(tl->name);
					free((char *)tl);
				}
				tf->prv_name->nxt_name = tf->nxt_name;
				tf->nxt_name->prv_name = tf->prv_name;
				tf->prv_inode->nxt_inode = tf->nxt_inode;
				tf->nxt_inode->prv_inode = tf->prv_inode;
				free(tf->name);
				free((char *)tf);
			}
			f = d->dir_list.nxt_file;
			while (f != &d->dir_list) {
				tf = f;
				f = f->nxt_file;
				tf->prv_name->nxt_name = tf->nxt_name;
				tf->nxt_name->prv_name = tf->prv_name;
				tf->prv_inode->nxt_inode = tf->nxt_inode;
				tf->nxt_inode->prv_inode = tf->prv_inode;
				free(tf->name);
				free((char *)tf->dirp);
				free((char *)tf);
			}
			td = d;
			d = d->nxt_dumplist;
			free((char *)td);
		}
		tt = t;
		t = t->nxt_tapelist;
		free((char *)tt);
	}
	extractlist = NULL_TAPELIST;
	listcnt = 0;
}

int
yesorno(s)
	char *s;
{
	int c;
	char *yesorno = "yn";

	do {
		(void) fprintf(stderr, "%s [%s] ", s, yesorno);
		(void) fflush(stderr);
		c = getc(stdin);
		while (c != '\n' && getc(stdin) != '\n')
			if (feof(stdin))
				exit(0);
	} while (c != yesorno[0] && c != yesorno[1]);
	if (c == yesorno[0])
		return (1);
	return (0);
}
