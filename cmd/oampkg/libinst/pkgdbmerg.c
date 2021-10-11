/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/
/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*LINTLIBRARY*/
#ident	"@(#)pkgdbmerg.c	1.36	95/08/07 SMI"	/* SVr4.0 1.13.3.1 */
		/* SVr4.0 1.18.4.1 */

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <pkgstrct.h>
#include <sys/stat.h>
#include <locale.h>
#include <libintl.h>
#include <pkginfo.h>
#include "pkglib.h"
#include "libinst.h"

#define	ERR_OUTPUT	"unable to update contents file"
#define	ERR_MEMORY	"memory allocation failure, errno=%d"
#define	ERR_PINFO	"missing pinfo structure for <%s>"
#define	INFO_PROCESS	"   %2d%% of information processed; continuing ..."

#define	WRN_NOTFILE	"WARNING: %s <no longer a regular file>"
#define	WRN_NOTSYMLN	"WARNING: %s <no longer a symbolic link>"
#define	WRN_NOTLINK	"WARNING: %s <no longer a linked file>"
#define	WRN_NOTDIR	"WARNING: %s <no longer a directory>"
#define	WRN_NOTCHAR	"WARNING: %s <no longer a character special device>"
#define	WRN_NOTBLOCK	"WARNING: %s <no longer a block special device>"
#define	WRN_NOTPIPE	"WARNING: %s <no longer a named pipe>"
#define	WRN_TYPIGN	"WARNING: object type change ignored <%s>"

#define	WRN_ODDVERIFY	"WARNING: quick verify disabled for class %s."

extern char	*pkginst, *errstr, errbuf[];
extern int	nosetuid, nocnflct, otherstoo;

/* setlist.c */
extern void	cl_def_dverify(int idx);

extern void	quit(int exitval);

char dbst = '\0';	/* usually set by installf() or removef() */

int files_installed(void);	/* return number of files installed. */

static int	errflg = 0;
static int	eptnum;
static FILE	*fpproc;
static long	sizetot;
static int	seconds;
static int	installed;	/* # of files, already properly installed. */
static struct	pinfo	*pkgpinfo = (struct pinfo *)0;

static int	is_setuid(struct cfent *ent);
static int	is_setgid(struct cfent *ent);
static int	merg(struct mergstat *mstat, struct cfextra *el_ent,
		    struct cfent *cf_ent);
static int	do_like_ent(FILE *fpo, struct mergstat *mstat,
		    struct cfextra *el_ent, struct cfent *cf_ent, int ctrl);
static int	do_new_ent(FILE *fpo, struct mergstat *mstat,
		    struct cfextra *el_ent, int ctrl);
static int	typechg(struct cfent *cf_ent, struct cfent *el_ent);

static void	set_change(struct cfextra *el_ent, struct mergstat *mstat);
static void	chgclass(struct cfent *cf_ent, struct pinfo *pinfo);
static void	output(FILE *fpo, struct cfent *ent, struct pinfo *pinfo);
static struct	cfextra	*cp_cfent(struct cfent *cf_ent, struct cfextra *el_ent);

/* ARGSUNUSED */
void
notice(int n)
{
#ifdef lint
	int i = n;
	n = i;
#endif	/* lint */
	(void) signal(SIGALRM, SIG_IGN);
	if (sizetot)
		echo(gettext(INFO_PROCESS), ftell(fpproc) * 100L / sizetot);
	(void) signal(SIGALRM, notice);
	alarm(seconds);
}

/* ARGSUSED */

/*
 * This scans the extlist and the contents file to the end, copying out the
 * merged contents to the file at tmpfp. It updates the mergstat structures
 * and deals with administrative defaults regarding setuid and conflict.
 *
 * Since both the extlist and the contents file entries are in numerical order,
 * they both scan unidirectionally. If the entry in the extlist is found in
 * the contents file (by pathname) then do_like_ent() is called. If the
 * extlist entry is not found in the contents file then do_new_ent() is
 * called. srchcfile() is responsible for copying out non-matching contents
 * file entries. At contents file EOF, the eocontents flag is set and the
 * rest of the extlist are assumed to be new entries. At the end of the
 * extlist, the eoextlist flag is set and the remaining contents file ends up
 * copied out by srchcfile().
 */
int
pkgdbmerg(FILE *mapfp, FILE *tmpfp, struct cfextra **extlist,
    struct mergstat *mstat, int notify)
{
	static	struct	cfent	cf_ent;	/* scratch area */
	struct	cfextra	*el_ent;	/* extlist entry under review */
	int	eocontents = 0, eoextlist = 0;
	int	n, changed, assume_ok = 0;

	cf_ent.pinfo = (NULL);
	errflg = 0;
	eptnum = 0;
	installed = changed = 0;

	fpproc = mapfp;
	if (notify) {
		seconds = notify;
		(void) signal(SIGALRM, notice);
		(void) alarm(seconds);
	}

	sighold(SIGALRM);
	fseek(mapfp, 0L, 2); /* seek to end of file */
	sizetot = ftell(mapfp); /* store number of bytes in open file */
	fseek(mapfp, 0L, 0); /* rewind */
	fseek(tmpfp, 0L, 0); /* rewind */
	sigrelse(SIGALRM);

	do {
		sighold(SIGALRM);

		/*
		 * If there's an entry in the extlist at this position,
		 * process that entry.
		 */
		if (!eoextlist && (el_ent = extlist[eptnum])) {

			/* Metafiles don't get merged. */
			if (strchr("in", el_ent->cf_ent.ftype))
				continue;

			/*
			 * Normally dbst comes to us from installf() or
			 * removef() in order to specify their special
			 * database status codes. They cannot implement a
			 * quick verify (it just doesn't make sense). For
			 * that reason, we can test to see if we already have
			 * a special database status. If we don't (it's from
			 * pkgadd) then we can test to see if this is calling
			 * for a quick verify wherein we assume the install
			 * will work and fix it if it doesn't. In that case
			 * we set our own dbst to be ENTRY_OK.
			 */
			if (dbst == '\0') {
				if (cl_dvfy(el_ent->cf_ent.pkg_class_idx) ==
				    QKVERIFY) {
					assume_ok = 1;
				}
			} else {
				/*
				 * If we DO end up with an installf/quick
				 * verify combination, we fix that by simply
				 * denying the quick verify for this class.
				 * This forces everything to come out alright
				 * by forcing the standard assumptions as
				 * regards contents database for the rest of
				 * the load.
				 */
				if (cl_dvfy(el_ent->cf_ent.pkg_class_idx) ==
				    QKVERIFY) {
					logerr(gettext(WRN_ODDVERIFY),
					    cl_nam(
					    el_ent->cf_ent.pkg_class_idx));
					/*
					 * Set destination verification to
					 * default.
					 */
					cl_def_dverify(
					    el_ent->cf_ent.pkg_class_idx);
				}
			}

			/*
			 * Comply with administrative requirements regarding
			 * setuid/setgid processes.
			 */
			if (is_setuid(&(el_ent->cf_ent))) {
				mstat[eptnum].setuid++;
			}
			if (is_setgid(&(el_ent->cf_ent))) {
				mstat[eptnum].setgid++;
			}

			/*
			 * If setuid/setgid processes are not allowed, reset
			 * those bits.
			 */
			if (nosetuid && (mstat[eptnum].setgid ||
			    mstat[eptnum].setuid)) {
				el_ent->cf_ent.ainfo.mode &=
				    ~(S_ISUID | S_ISGID);
			}
		} else
			eoextlist = 1;	/* end of extlist[] */

		/*
		 * If we're not at the end of the contents file, get the next
		 * entry for comparison.
		 */
		if (!eocontents) {
			/* Search the contents file for this entry. */
			n = srchcfile(&cf_ent, el_ent ?
			    el_ent->cf_ent.path : NULL, mapfp, tmpfp);

			/*
			 * If there was an error, note it and return an error
			 * flag.
			 */
			if (n < 0) {
				logerr(gettext(
				    "bad entry read from contents file"));
				logerr(gettext("- pathname: %s"),
				    (cf_ent.path && *cf_ent.path) ?
				    cf_ent.path : "Unknown");
				logerr(gettext("- problem: %s"),
				    (errstr && *errstr) ? errstr : "Unknown");
				return (-1);
			/*
			 * If there was a match, then merge them into a
			 * single entry.
			 */
			} else if (n == 1) {
				/*
				 * If this package is overwriting a setuid or
				 * setgid process, set the status bits so we
				 * can inform the administrator.
				 */
				if (is_setuid(&cf_ent)) {
					mstat[eptnum].osetuid++;
				}
				if (is_setgid(&cf_ent)) {
					mstat[eptnum].osetgid++;
				}

				if (do_like_ent(tmpfp, &mstat[eptnum],
				    extlist[eptnum], &cf_ent, assume_ok))
					changed++;

			/*
			 * If the alphabetical position in the contents file
			 * is unfilled, then this will be a new entry. If n
			 * == 0, then we're also at the end of the contents
			 * file.
			 */
			} else {
				if (n == 0)
					eocontents = 1;

				/*
				 * If there is an extlist entry in the
				 * hopper, insert it at the end of the
				 * contents file.
				 */
				if (!eoextlist) {
					if (do_new_ent(tmpfp, &mstat[eptnum],
					    extlist[eptnum], assume_ok))
						changed++;
				}
			}
		/*
		 * We have passed the last entry in the contents file tagging
		 * these extlist entries onto the end.
		 */
		} else if (!eoextlist) {
			if (do_new_ent(tmpfp, &mstat[eptnum], extlist[eptnum],
			    assume_ok))
				changed++;
		}
		/* Else, we'll drop out of the loop. */

		sigrelse(SIGALRM);
	} while (eptnum++, (!eocontents || !eoextlist));

	if (notify) {
		(void) alarm(0);
		(void) signal(SIGALRM, SIG_IGN);
	}

	(void) fflush(tmpfp);
	return (errflg ? -1 : changed);
}

/*
 * Merge a new entry with an installed package object of the same name and
 * insert that object into the contents file. Obey administrative defaults as
 * regards conflicting files.
 */
static int
do_like_ent(FILE *fpo, struct mergstat *mstat, struct cfextra *el_ent,
    struct cfent *cf_ent, int ctrl)
{
	int	stflag, ignore, changed;

	ignore = changed = 0;
	pkgpinfo = eptstat(cf_ent, pkginst, DUP_ENTRY);
	stflag = pkgpinfo->status;

	if (otherstoo)
		mstat->shared++;

	/* If it's marked for erasure, make it official */
	if (el_ent->cf_ent.ftype == RM_RDY) {
		if (!errflg) {
			pkgpinfo = eptstat(cf_ent, pkginst, RM_RDY);

			/*
			 * Get copy of status character in case the object
			 * is "shared" by a server, in which case we need to
			 * maintain the shared status after the entry is
			 * written to the contents file with the '-' status.
			 * This is needed to support the `removef' command.
			 */
			stflag = pkgpinfo->status;
			pkgpinfo->status = RM_RDY;

			if (putcfile(cf_ent, fpo)) {
				progerr(gettext(ERR_OUTPUT));
				quit(99);
			}

			/*
			 * If object is provided by a server, allocate an
			 * info block and set the status to indicate this.
			 * This is needed to support the `removef' command.
			 */
			if (stflag == SHARED_FILE) {
				el_ent->cf_ent.pinfo =
				    (struct pinfo *)calloc(1,
				    sizeof (struct pinfo));
				el_ent->cf_ent.pinfo->next = NULL;
				el_ent->cf_ent.pinfo->status = SHARED_FILE;
			}
		}
		return (1);
	}

	if (!pkgpinfo) {
		progerr(gettext(ERR_PINFO), cf_ent->path);
		quit(99);
	}

	/*
	 * Do not allow installation if nocnflct is set and other packages
	 * reference this pathname. The cp_cfent() function below write the
	 * information from the installed file over the new entry, so the
	 * contents file will be unchanged.
	 */
	if ((nocnflct && mstat->shared && el_ent->cf_ent.ftype != 'e')) {
		/*
		 * First set the attrchg and contchg entries for proper
		 * messaging in the install phase.
		 */
		set_change(el_ent, mstat);

		/* Now overwrite the new entry with the actual entry. */
		el_ent = cp_cfent(cf_ent, el_ent);
		ignore++;
	} else if (merg(mstat, el_ent, cf_ent))
		changed++;

	/* el_ent structure now contains updated entry */
	if (!mstat->contchg && !ignore) {
		/*
		 * We know the DB entry matches the pkgmap, so now
		 * we need to see if actual object matches the pkgmap.
		 */
		set_change(el_ent, mstat);
	}

	if (!errflg) {
		if (ctrl == 1) {	/* quick verify assumes OK */
			pkgpinfo = eptstat(&(el_ent->cf_ent), pkginst,
			    ENTRY_OK);
			/*
			 * We could trust the prior pkginfo entry, but things
			 * could have changed and  we need to update the
			 * fs_tab[] anyway. We check for a server object
			 * here.
			 */
			if (is_remote_fs(el_ent->server_path,
			    &(el_ent->fsys_value)) &&
			    !is_fs_writeable(el_ent->server_path,
			    &(el_ent->fsys_value)))
				pkgpinfo->status = SHARED_FILE;
		} else {
			if (!ignore && mstat->contchg)
				pkgpinfo =
				    eptstat(&(el_ent->cf_ent), pkginst,
				    (dbst ? dbst : CONFIRM_CONT));
			else if (!ignore && mstat->attrchg)
				pkgpinfo =
				    eptstat(&(el_ent->cf_ent), pkginst,
				    (dbst ? dbst : CONFIRM_ATTR));
			else if (stflag != DUP_ENTRY) {
				pkgpinfo = eptstat(&(el_ent->cf_ent),
				    pkginst, '\0');
				changed++;
			}
		}
		output(fpo, &(el_ent->cf_ent), pkgpinfo);
	}

	if (pkgpinfo->aclass[0])
		(void) strcpy(el_ent->cf_ent.pkg_class, pkgpinfo->aclass);

	/* free up list of packages which reference this entry */
	cf_ent->pinfo = el_ent->cf_ent.pinfo;
	el_ent->cf_ent.pinfo = NULL;
	if (!mstat->attrchg && !mstat->contchg)
		installed++;
	return (changed);
}

static int
do_new_ent(FILE *fpo, struct mergstat *mstat, struct cfextra *el_ent, int ctrl)
{
	struct pinfo *pinfo;
	char	*tp;

	if (el_ent->cf_ent.ftype == RM_RDY)
		return (0);

	tp = el_ent->server_path;
	if (access(tp, 0) == 0) {
		/*
		 * Path exists, and although its not referenced by any
		 * package we make it look like it is so it appears as a
		 * conflicting file in case the user doesn't want it
		 * installed. We set the rogue flag to distinguish this from
		 * package object conflicts if the administrator is queried
		 * about this later. Note that noconflict means NO conflict
		 * at the file level. Even rogue files count.
		 */
		mstat->shared++;
		mstat->rogue = 1;
		set_change(el_ent, mstat);
	} else {
		/* since path doesn't exist, we're changing everything */
		mstat->rogue = 0;
		mstat->contchg++;
		mstat->attrchg++;
	}

	/*
	 * Do not allow installation if nocnflct is set and this pathname is
	 * already in place. Since this entry is new (not associated with a
	 * package), we don't issue anything to the database we're building.
	 */
	if ((nocnflct && mstat->shared))
		return (0);

	if (!errflg) {
		el_ent->cf_ent.npkgs = 1;
		pinfo = (struct pinfo *)calloc(1, sizeof (struct pinfo));
		if (!pinfo) {
			progerr(gettext(ERR_MEMORY), errno);
			quit(99);
		}
		el_ent->cf_ent.pinfo = pinfo;
		(void) strncpy(pinfo->pkg, pkginst, 14);
		if (ctrl == 1)	/* quick verify assumes OK */
			pinfo->status = dbst ? dbst : ENTRY_OK;
			/*
			 * The entry won't be verified, but the entry in the
			 * database isn't necessarily ENTRY_OK. If this is
			 * coming from a server, we need to note that
			 * instead.
			 */
			if (is_remote_fs(el_ent->server_path,
			    &(el_ent->fsys_value)) &&
			    !is_fs_writeable(el_ent->server_path,
			    &(el_ent->fsys_value)))
				pinfo->status = SHARED_FILE;
		else
			pinfo->status = dbst ? dbst : CONFIRM_CONT;
		output(fpo, &(el_ent->cf_ent), pinfo);
		free(pinfo);
		el_ent->cf_ent.pinfo = NULL;
	}
	if (!mstat->attrchg && !mstat->contchg)
		installed++;
	return (1);
}

int
files_installed(void)
{
	return (installed);
}
/*
 * This function determines if there is a difference between the file on
 * the disk and the file to be laid down. It set's mstat flags attrchg
 * and contchg accordingly.
 */
static void
set_change(struct cfextra *el_ent, struct mergstat *mstat)
{
	int	n;
	char 	*tp;

	tp = el_ent->server_path;
	if (strchr("fev", el_ent->cf_ent.ftype)) {
		if (cverify(0, &(el_ent->cf_ent.ftype), tp,
		    &(el_ent->cf_ent.cinfo)))
			mstat->contchg++;
		else if (!mstat->contchg && !mstat->attrchg) {
			if (averify(0, &(el_ent->cf_ent.ftype), tp,
			    &(el_ent->cf_ent.ainfo)))
				mstat->attrchg++;
		}
	} else if (!mstat->attrchg && strchr("dxcbp", el_ent->cf_ent.ftype)) {
		n = averify(0, &(el_ent->cf_ent.ftype), tp,
		    &(el_ent->cf_ent.ainfo));
		if (n == VE_ATTR)
			mstat->attrchg++;
		else if (n && (n != VE_EXIST))
			mstat->contchg++;
	}
}

static int
is_setuid(struct cfent *ent)
{
	return (strchr("fve", ent->ftype) && (ent->ainfo.mode != BADMODE) &&
		(ent->ainfo.mode & S_ISUID));
}

static int
is_setgid(struct cfent *ent)
{
	return (strchr("fve", ent->ftype) && (ent->ainfo.mode != BADMODE) &&
		(ent->ainfo.mode & S_ISGID) &&
		(ent->ainfo.mode & (S_IEXEC|S_IXUSR|S_IXOTH)));
}

char *types[] = {
	"fev",	/* type 1, regular files */
	"s", 	/* type 2, symbolic links */
	"l", 	/* type 3, linked files */
	"dx", 	/* type 4, directories */
	"c", 	/* type 5, character special devices */
	"b", 	/* type 6, block special devices */
	"p", 	/* type 7, named pipes */
	NULL
};

/*
 * This determines if the ftype of the file on the disk and the file to be
 * laid down are close enough. If they aren't, this either returns an error
 * or displays a warning. This returns 0 for OK, -1 for NOT ALLOWED and
 * 1 for WARNING ISSUED.
 */
static int
typechg(struct cfent *cf_ent, struct cfent *el_ent)
{
	int	i, etype, itype;

	/* If they are identical, return OK */
	if (cf_ent->ftype == el_ent->ftype)
		return (0);

	/* If contents file is ambiguous, set it to the new entity's ftype */
	if (cf_ent->ftype == '?') {
		cf_ent->ftype = el_ent->ftype;
		return (0); /* do nothing; not really different */
	}

	/* If the new entity is ambiguous, wait for the verify */
	if (el_ent->ftype == '?')
		return (0);

	etype = itype = 0;

	/* Set etype to that of the new entity */
	for (i = 0; types[i]; ++i) {
		if (strchr(types[i], el_ent->ftype)) {
			etype = i+1;
			break;
		}
	}

	/* Set itype to that in the contents file */
	for (i = 0; types[i]; ++i) {
		if (strchr(types[i], cf_ent->ftype)) {
			itype = i+1;
			break;
		}
	}

	if (itype == etype) {
		/* same basic object type */
		return (0);
	}

	/* allow change, but warn user of possible problems */
	switch (etype) {
	    case 1:
		logerr(gettext(WRN_NOTFILE), el_ent->path);
		break;

	    case 2:
		logerr(gettext(WRN_NOTSYMLN), el_ent->path);
		break;

	    case 3:
		logerr(gettext(WRN_NOTLINK), el_ent->path);
		break;

	    case 4:
		logerr(gettext(WRN_NOTDIR), el_ent->path);
		break;

	    case 5:
		logerr(gettext(WRN_NOTCHAR), el_ent->path);
		break;

	    case 6:
		logerr(gettext(WRN_NOTBLOCK), el_ent->path);
		break;

	    case 7:
		logerr(gettext(WRN_NOTPIPE), el_ent->path);
		break;
	}
	return (1);
}

static int
merg(struct mergstat *mstat, struct cfextra *el_ent, struct cfent *cf_ent)
{
	int	changed,
		n;

	changed = 0;

	/*
	 * we need to change the original entry to make it look like the new
	 * entry (the eptstat() routine has already added appropriate package
	 * information, but not about 'aclass'
	 */

	el_ent->cf_ent.pinfo = cf_ent->pinfo;

	if (cf_ent->ftype != el_ent->cf_ent.ftype) {
		n = typechg(&(el_ent->cf_ent), cf_ent);
		if (n < 0) {
			logerr(gettext(WRN_TYPIGN), cf_ent->path);
			el_ent = cp_cfent(cf_ent, el_ent);
			return (0);	/* don't change current cf_ent */
		} else if (n)
			mstat->contchg++;
		changed++;
	}

	if (strcmp(cf_ent->pkg_class, el_ent->cf_ent.pkg_class)) {
		/*
		 * we always allow a class change as long as we have
		 * consistent ftypes, which at this point we must
		 */
		changed++;
		if (strcmp(cf_ent->pkg_class, "?")) {
			(void) strcpy(pkgpinfo->aclass,
			    el_ent->cf_ent.pkg_class);
			(void) strcpy(el_ent->cf_ent.pkg_class,
			    cf_ent->pkg_class);
			chgclass(&(el_ent->cf_ent), pkgpinfo);
		}
	}

	if (strchr("sl", cf_ent->ftype)) {
		/* bug id 1094229 */
		if (cf_ent->ainfo.local && el_ent->cf_ent.ainfo.local)
			if (strcmp(cf_ent->ainfo.local,
			    el_ent->cf_ent.ainfo.local)) {
				changed++;
				if (strcmp(el_ent->cf_ent.ainfo.local,
				    "?") == 0)
					strcpy(el_ent->cf_ent.ainfo.local,
					    cf_ent->ainfo.local);
				else
					mstat->contchg++;
		}
		return (changed);
	} else if (el_ent->cf_ent.ftype == 'e') {
		/* the contents of edittable files assumed to be changing */
		mstat->contchg++;
		changed++;  /* content info is changing */
	} else if (strchr("fv", cf_ent->ftype)) {
		/*
		 * look at content information; a '?' in this field indicates
		 * the contents are unknown -- thus we assume that they are
		 * changing
		 */
		if (cf_ent->cinfo.size != el_ent->cf_ent.cinfo.size) {
			changed++; /* content info is changing */
			mstat->contchg++;
		} else if (cf_ent->cinfo.modtime !=
		    el_ent->cf_ent.cinfo.modtime) {
			changed++; /* content info is changing */
			mstat->contchg++;
		} else if (cf_ent->cinfo.cksum != el_ent->cf_ent.cinfo.cksum) {
			changed++; /* content info is changing */
			mstat->contchg++;
		}
	} else if (strchr("cb", cf_ent->ftype)) {
		if (cf_ent->ainfo.major != el_ent->cf_ent.ainfo.major) {
			changed++;  /* attribute info is changing */
			if (el_ent->cf_ent.ainfo.major <= BADMAJOR)
				el_ent->cf_ent.ainfo.major =
				    cf_ent->ainfo.major;
			else
				mstat->contchg++;
		}
		if (cf_ent->ainfo.minor != el_ent->cf_ent.ainfo.minor) {
			changed++;  /* attribute info is changing */
			if (el_ent->cf_ent.ainfo.minor <= BADMINOR)
				el_ent->cf_ent.ainfo.minor =
				    cf_ent->ainfo.minor;
			else
				mstat->contchg++;
		}
	}

	if (cf_ent->ainfo.mode != el_ent->cf_ent.ainfo.mode) {
		changed++;  /* attribute info is changing */
		if (el_ent->cf_ent.ainfo.mode == BADMODE)
			el_ent->cf_ent.ainfo.mode = cf_ent->ainfo.mode;
		else
			mstat->attrchg++;
	}
	if (strcmp(cf_ent->ainfo.owner, el_ent->cf_ent.ainfo.owner) != 0) {
		changed++;  /* attribute info is changing */
		if (strcmp(el_ent->cf_ent.ainfo.owner, "?") == 0)
			(void) strcpy(el_ent->cf_ent.ainfo.owner,
			    cf_ent->ainfo.owner);
		else
			mstat->attrchg++;
	}
	if (strcmp(cf_ent->ainfo.group, el_ent->cf_ent.ainfo.group) != 0) {
		changed++;  /* attribute info is changing */
		if (strcmp(el_ent->cf_ent.ainfo.group, "?") == 0)
			(void) strcpy(el_ent->cf_ent.ainfo.group,
			    cf_ent->ainfo.group);
		else
			mstat->attrchg++;
	}
	return (changed);
}

static void
output(FILE *fpo, struct cfent *ent, struct pinfo *pinfo)
{
	short	svvolno;
	char	*svpt;

	/* output without volume information */
	svvolno = ent->volno;
	ent->volno = 0;

	pinfo->editflag = 0;
	if (strchr("sl", ent->ftype)) {
		if (putcfile(ent, fpo)) {
			progerr(gettext(ERR_OUTPUT));
			quit(99);
		}
	} else {

		/* output without local pathname */
		svpt = ent->ainfo.local;
		ent->ainfo.local = NULL;
		if (putcfile(ent, fpo)) {
			progerr(gettext(ERR_OUTPUT));
			quit(99);
		}
		ent->ainfo.local = svpt;
		/*
		 * If this entry represents a file which is being edited, we
		 * need to store in memory the fact that it is an edittable
		 * file so that when we audit it after installation we do not
		 * worry about its contents; we do this by resetting the ftype
		 * to 'e' in the memory array which is later used to control
		 * the audit
		 */
		if (pinfo->editflag)
			ent->ftype = 'e';
	}
	/* restore volume information */
	ent->volno = svvolno;
}

static void
chgclass(struct cfent *cf_ent, struct pinfo *pinfo)
{
	struct pinfo *pp;
	char	*oldclass, newclass[CLSSIZ+1];
	int	newcnt, oldcnt;

	/*
	 * we use this routine to minimize the use of the aclass element by
	 * optimizing the use of the cf_ent->pkg_class element
	 */
	strcpy(newclass, pinfo->aclass);
	newcnt = 1;

	oldclass = cf_ent->pkg_class;
	oldcnt = 0;

	/*
	 * count the number of times the newclass will be used and see if it
	 * exceeds the number of times the oldclass is referenced
	 */
	pp = cf_ent->pinfo;
	while (pp) {
		if (pp->aclass[0]) {
			if (strcmp(pp->aclass, newclass) == 0)
				newcnt++;
			else if (strcmp(pp->aclass, oldclass) == 0)
				oldcnt++;
		}
		pp = pp->next;
	}
	if (newcnt > oldcnt) {
		pp = cf_ent->pinfo;
		while (pp) {
			if (pp->aclass[0] == '\0')
				strcpy(pp->aclass, oldclass);
			else if (strcmp(pp->aclass, newclass) == 0)
				pp->aclass[0] = '\0';
			pp = pp->next;
		}
		(void) strcpy(cf_ent->pkg_class, newclass);
	}
}

/*
 * Copy critical portions of cf_ent (from the contents file) and el_ent
 * (constructed from the pkgmap) into a merged cfent structure, tp.
 */
static struct cfextra *
cp_cfent(struct cfent *cf_ent, struct cfextra *el_ent)
{
	struct cfextra	*tp;
	struct pinfo	*tpinfo, *tpinfo1, *tpinfo2 = NULL;

	/* Allocate space for cfent copy */
	if ((tp = (struct cfextra *) malloc(sizeof (struct cfextra))) == NULL) {
		progerr(gettext("cp_cfent: memory allocation error"));
		return (0);
	}

	/* Copy everything from the contents file over */
	(void) memcpy(&(tp->cf_ent), cf_ent, sizeof (struct cfent));

	/* Now overlay new items from the package */
	/*
	 * Everything in the contents file is client-relative, so
	 * only path will be correct here and we'll have to expand it
	 * for insertion into tp.
	 */
	eval_path(&(tp->server_path),
	    &(tp->client_path),
	    &(tp->map_path),
	    cf_ent->path);
	tp->cf_ent.path = tp->server_path;

	tp->cf_ent.volno = el_ent->cf_ent.volno;
	tp->cf_ent.pkg_class_idx = el_ent->cf_ent.pkg_class_idx;

	if (el_ent->cf_ent.ainfo.local != NULL) {
		eval_path(&(tp->server_local), &(tp->client_local), NULL,
		    el_ent->cf_ent.ainfo.local);
		tp->cf_ent.ainfo.local = tp->server_local;
	}

	tp->cf_ent.pinfo = NULL;
	for (tpinfo = cf_ent->pinfo; tpinfo != NULL; tpinfo = tpinfo->next) {
		if ((tpinfo1 = (struct pinfo *) malloc(sizeof (struct pinfo)))
		    == NULL) {
			progerr(gettext("cp_cfent: memory allocation error"));
			return (0);
		}
		(void) memcpy(tpinfo1, tpinfo, sizeof (struct pinfo));
		tpinfo1->next = NULL;
		if (!tp->cf_ent.pinfo)
			tpinfo2 = (tp->cf_ent.pinfo = tpinfo1);
		else
			tpinfo2 = (tpinfo2->next = tpinfo1);
	}
	return (tp);
}
