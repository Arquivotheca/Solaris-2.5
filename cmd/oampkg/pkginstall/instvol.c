/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/*	  All Rights Reserved	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any	*/
/*	actual or intended publication of such source code.	*/
/*
 * Copyright (c) 1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident	"@(#)instvol.c	1.42	95/07/13 SMI"	/* SVr4.0 1.13.3.1 */

/*  5-20-92	add newroot functions */

#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <libintl.h>
#include "install.h"	/* includes sys/types.h & limits.h */
#include <pkgstrct.h>
#include <pkgdev.h>
#include <pkglocs.h>
#include <archives.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <wait.h>
#include <pkglib.h>
#include "libadm.h"
#include "libinst.h"
#include "pkginstall.h"

extern char	*rw_block_size;
extern int	debug;
extern ulong	pkgmap_blks; 		/* main.c */

extern struct	pkgdev pkgdev;

extern char	tmpdir[], pkgsav[], pkgbin[], errbuf[], instdir[],
		*errstr, *pkginst;

extern int	dbchg, nosetuid, nocnflct, warnflag;

extern void	ckreturn(int retcode, char *msg);

extern void	quit(int retcode);

/* cppath.c */
extern int	cppath(int ctrl, char *f1, char *f2, mode_t mode);

/* backup.c */
extern void	backup(char *path, int mode);

/* fixpath.c */
extern int	get_orig_offset(void);

/* setlist.c */
extern int	cl_deliscript(int i);
extern unsigned	cl_svfy(int i);
extern unsigned	cl_dvfy(int i);
extern unsigned	cl_pthrel(int i);

/* pkgvolume.c */
extern void	pkgvolume(struct pkgdev *devp, char *pkg, int part, int nparts);

/* verify.c */
extern int	fverify(int fix, char *ftype, char *path, struct ainfo *ainfo,
    struct cinfo *cinfo);

/* doulimit.c */
extern int	set_ulimit(char *script, char *err_msg);
extern int	clr_ulimit();

#define	ck_efile(s, p)	\
		((p->cinfo.modtime >= 0) && \
		p->ainfo.local && \
		cverify(0, &p->ftype, s, &p->cinfo))

#define	MSG_PKG		"%s <already present on Read Only file system>"
#define	MSG_UGMOD	"%s <reset setuid/setgid bits>"
#define	MSG_UGID	"%s <installed with setuid/setgid bits reset>"
#define	MSG_SHIGN	"%s <conflicting pathname not installed>"
#define	MSG_SYMLINK	"%s <symbolic link>"
#define	MSG_ATTRIB	"%s <attribute change only>"

#define	WRN_NONE 	"WARNING: %s <not present on Read Only file system>"

#define	ERR_CASFAIL	"class action script did not complete successfully"
#define	ERR_CLIDX	"invalid class index of <%d> detected for file %s."
#define	ERR_TMPFILE	"unable to open temp file <%s>"
#define	ERR_CORRUPT	"source path <%s> is corrupt"
#define	ERR_CHDIR	"unable to change directory to <%s>"
#define	ERR_CFBAD	"bad entry read of contents file"
#define	ERR_CFMISSING	"missing entry in contents file for <%s>"
#define	ERR_SCRULIMIT	"script <%s> created a file exceeding ULIMIT."

static int	eocflag;
static int	domerg(struct cfextra **extlist, struct mergstat *mstat,
			int part, int nparts, int myclass, char **srcp,
			char **dstp);
static void	endofclass(struct cfextra **extlist, struct mergstat *mstat,
		    int myclass, int ckflag);
static int	fix_attributes(struct cfextra **extlist,
		    struct mergstat *mstat, int idx);

/*
 * This is the function that actually installs one volume (usually that's
 * all there is). Upon entry, the extlist is entirely correct:
 *
 *	1. It contains only those files which are to be installed
 *	   from all volumes.
 *	2. The mode bits in the ainfo structure for each file are set
 *	   correctly in accordance with administrative defaults.
 *	3. mstat->setuid/setgid reflect what the status *was* before
 *	   pkgdbmerg() processed compliance.
 */
void
instvol(struct cfextra **extlist, struct mergstat *mstat, char *srcinst,
    int part, int nparts)
{
	FILE	*listfp;
	static	char dirloc[BUFSIZ];
	int	i, n, count, tcount;
	int	c, cl_incomplete;
	struct cfent *ept;
	char	*listfile,
		*srcp,
		*dstp;
	int	do_norm = 0;
	int	nc = 0;

	if (part == 1)
		pkgvolume(&pkgdev, srcinst, part, nparts);

	tcount = 0;
	nc = cl_getn();

	/*
	 * For each class in this volume, install those files.
	 *
	 * NOTE : This loop index may be decremented by code below forcing a
	 * second trip through for the same class. This happens only when a
	 * class is split between an archive and the tree. Examples would be
	 * old WOS packages and the occasional class containing dynamic
	 * libraries which require special treatment.
	 */
	for (i = 0; i < nc; i++) {
		int pass_relative = 0;
		int rel_init = 0;

		eocflag = count = 0;
		listfp = (FILE *) 0;
		/* Now what do we pass to the class action script */
		if (cl_pthrel(i) == REL_2_CAS)
			pass_relative = 1;

		for (;;) {
			if (!tcount++) {
				/* first file to install */
				echo(gettext("## Installing part %d of %d."),
					part, nparts);
			}
			n = domerg(extlist, mstat, (count ? 0 : part),
			    nparts, i, &srcp, &dstp);
			if (n < 0)
				break; /* no more entries to process */
			count++;

			/*
			 * If there's an install class action script and no
			 * list file has been created yet, create that file
			 * and provide the pointer in listfp.
			 */
			if (cl_iscript(i) && !listfp) {
				/* create list file */
				listfile = tempnam(tmpdir, "list");
				if ((listfp = fopen(listfile, "w")) ==
				    NULL) {
					progerr(gettext(ERR_TMPFILE),
					    listfile);
					quit(99);
				}
			}

			ept = &(extlist[n]->cf_ent);

			pkgvolume(&pkgdev, srcinst, part, nparts);

			/*
			 * If source verification is OK for this class, make
			 * sure the source we're passing to the class action
			 * script is useable.
			 */
			if (cl_svfy(i) != NOVERIFY) {
				if (cl_iscript(i) ||
				    strchr("en", ept->ftype)) {
					if (ck_efile(srcp, ept)) {
						progerr(gettext(ERR_CORRUPT),
						    srcp);
						logerr(errbuf);
						warnflag++;
						continue;
					}
				}
			}

			/*
			 * If there's a class action script for this class,
			 * just collect names in a temporary file
			 * that will be used as the stdin when the
			 * class action script is invoked.
			 */
			if (cl_iscript(i)) {
				if (pass_relative) {
					if (!rel_init) {
						(void) fprintf(listfp, "%s\n",
						    instdir);
						rel_init++;
					}
					(void) fprintf(listfp, "%s\n",
					    extlist[n]->map_path);
				} else
					(void) fprintf(listfp, "%s %s\n",
					    (srcp ? srcp: "/dev/null"), dstp);
			} else {
				/*
				 * For read-only remote filesystems
				 * don't attempt to install the file.
				 */
				if (is_remote_fs(ept->path,
				    &(extlist[n]->fsys_value)) &&
				    !is_fs_writeable(ept->path,
				    &(extlist[n]->fsys_value))) {
					mstat[n].attrchg = 0;
					mstat[n].contchg = 0;
					/* bug id 1094301 */
					if (!isfile(NULL, dstp))
						echo(gettext(MSG_PKG), dstp);
					else {
						echo(gettext(WRN_NONE), dstp);
					}
					continue;
				}
				echo("%s", dstp);

				if (srcp) {
					/*
					 * Copy from source media to target
					 * path and fix file mode and
					 * permission now in case installation
					 * is halted.
					 */
					if (cppath(SETMODE | DISPLAY,
					    srcp, dstp, ept->ainfo.mode))
						warnflag++;
					else if (!finalck(ept, 1, 1)) {
						/*
						 * everything checks
						 * here
						 */
						mstat[n].attrchg = 0;
						mstat[n].contchg = 0;
					}
				}
			}
		}

		/*
		 * We have now completed processing of all pathnames
		 * associated with this volume and class.
		 */
		if (cl_iscript(i)) {
			/*
			 * Execute appropriate class action script
			 * with list of source/destination pathnames
			 * as the input to the script.
			 */
			if (chdir(pkgbin)) {
				progerr(gettext(ERR_CHDIR), pkgbin);
				quit(99);
			}
			if (listfp)
				(void) fclose(listfp);

			/* Use ULIMIT if supplied. */
			set_ulimit(cl_iscript(i), gettext(ERR_CASFAIL));

			if (eocflag) {
				/*
				 * Since there are no more volumes which
				 * contain pathnames associated with this
				 * class, execute class action script with
				 * the ENDOFCLASS argument; we do this even
				 * if none of the pathnames associated with
				 * this class and volume needed installation
				 * to guarantee the class action script is
				 * executed at least once during package
				 * installation.
				 */
				n = pkgexecl((listfp ? listfile : CAS_STDIN),
				    CAS_STDOUT, CAS_USER, CAS_GRP, SHELL,
				    cl_iscript(i), "ENDOFCLASS", NULL);
				ckreturn(n, gettext(ERR_CASFAIL));
			} else if (count) {
				/* execute class action script */
				n = pkgexecl(listfile, CAS_STDOUT, CAS_USER,
				    CAS_GRP, SHELL, cl_iscript(i), NULL);
				ckreturn(n, gettext(ERR_CASFAIL));
			}
			clr_ulimit();
			unlink(listfile);
		}

		if (eocflag) {
			if (cl_dvfy(i) == QKVERIFY) {
				/*
				 * The quick verify just fixes everything.
				 * If it returns 0, all is well. If it
				 * returns 1, then the class installation
				 * was incomplete and we retry on the
				 * stuff that failed in the conventional
				 * way (without a CAS). this is primarily
				 * to accomodate old archives such as are
				 * found in pre-2.5 WOS; but, it is also
				 * used when a critical dynamic library
				 * is not archived with its class.
				 */
				if (!fix_attributes(extlist, mstat, i)) {
					/*
					 * Reset the CAS pointer. If the
					 * function returns 0 then there
					 * was no script there in the first
					 * place and we'll just have to
					 * call this a miss.
					 */
					if (cl_deliscript(i))
						/* decrement i for next pass */
						i--;
				}
			} else
				/*
				 * finalize merge. This checks to make sure
				 * file attributes are correct and any links
				 * specified are created
				 */
				endofclass(extlist, mstat, i,
				    (cl_iscript(i) ? 0 : 1));
		}
	}

	if (tcount == 0)
		echo(gettext("## Installation of part %d of %d is complete."),
			part, nparts);
}

static int
domerg(struct cfextra **extlist, struct mergstat *mstat, int part, int nparts,
	int myclass, char **srcp, char **dstp)
{
	static int	svindx = 0;
	static int	svpart = 0;
	static int	maxvol = 0;
	int	i, n, msg_ugid;
	struct cfent	*ept;

	if (part) {
		maxvol = 0;
		svindx = 0;
		svpart = part;
	} else {
		i = svindx;
		part = svpart;
	}

	/*
	 * This goes through the pkgmap entries one by one testing them
	 * for inclusion in the contents file as well as for validity
	 * against existing files.
	 */
	for (i = svindx; extlist[i]; i++) {
		ept = &(extlist[i]->cf_ent);

		/* ignore information files */
		if (ept->ftype == 'i')
			continue;

		/* if the class is invalid, announce it & exit */
		if (ept->pkg_class_idx == -1) {
			progerr(gettext(ERR_CLIDX), ept->pkg_class_idx,
			    (ept->path && *ept->path) ? ept->path : "unknown");
			logerr(gettext("pathname=%s\n"),
			    (ept->path && *ept->path) ? ept->path : "unknown");
			logerr(gettext("class=<%s>\n"),
			    (ept->pkg_class && *ept->pkg_class) ?
			    ept->pkg_class : "Unknown");
			logerr(gettext("CLASSES=<%s>\n"),
			    getenv("CLASSES") ? getenv("CLASSES") : "Not Set");
			quit(99);
		}

		/* if this isn't the class of current interest, skip it */
		if (myclass != ept->pkg_class_idx)
			continue;

		/* adjust the max volume number appropriately */
		if (ept->volno > maxvol)
			maxvol = ept->volno;

		/* if this part goes into another volume, skip it */
		if (part != ept->volno)
			continue;

		/*
		 * If it's a conflicting file and it's not supposed to be
		 * installed, note it and skip.
		 */
		if (nocnflct && mstat[i].shared && ept->ftype != 'e') {
			if (mstat[i].contchg || mstat[i].attrchg) {
				echo(gettext(MSG_SHIGN), ept->path);
			}
			continue;
		}

		/*
		 * If we want to set uid or gid but user says no, note it.
		 * Remember that the actual mode bits in the structure have
		 * already been adjusted and the mstat flag is telling us
		 * about the original mode.
		 */
		if (nosetuid && (mstat[i].setuid || mstat[i].setgid)) {
			msg_ugid = 1;	/* don't repeat attribute message. */
			if (is_fs_writeable(ept->path,
			    &(extlist[i]->fsys_value))) {
				if (!(mstat[i].contchg) && mstat[i].attrchg)
					echo(gettext(MSG_UGMOD), ept->path);
				else
					echo(gettext(MSG_UGID), ept->path);
			}
		} else
			msg_ugid = 0;

		switch (ept->ftype) {
		    case 'l':	/* hard link */
			continue; /* defer to final proc */

		    case 's': /* for symlink, verify without fix first */

			/* Do this only for default verify */
			if (cl_dvfy(myclass) == DEFAULT) {
				if (averify(0, &ept->ftype,
				    ept->path, &ept->ainfo))
					echo(gettext(MSG_SYMLINK),
					    ept->path);
			}

				/*FALLTHRU*/
		    case 'd':	/* directory */
		    case 'x':	/* exclusive directory */
		    case 'c':	/* character special device */
		    case 'b':	/* block special device */
		    case 'p':	/* named pipe */
			/*
			 * For read-only remote filesystems
			 * don't attempt to verify these.
			 */
			if (is_remote_fs(ept->path,
			    &(extlist[i]->fsys_value)) &&
			    !is_fs_writeable(ept->path,
			    &(extlist[i]->fsys_value))) {
				mstat[i].attrchg = 0;
				mstat[i].contchg = 0;
				break;
			}

			if (averify(1, &ept->ftype,
			    ept->path,
			    &ept->ainfo) == 0) {
				mstat[i].contchg = mstat[i].attrchg = 0;
			}

			break;

		    default:
			break;
		}

		if (mstat[i].contchg) {
			*dstp = ept->path;
			if (strchr("fev", ept->ftype)) {
				*srcp = ept->ainfo.local;
				if (*srcp[0] == '~') {
					/* translate source pathname */
					*srcp = srcpath(instdir,
					    &(ept->ainfo.local[1]),
					    part, nparts);
				}
			} else
				*srcp = NULL;
			svindx = i+1;
			backup(*dstp, 1);
			return (i);
		}
		if (mstat[i].attrchg) {
			backup(ept->path, 0);
			if (!msg_ugid)
				echo(gettext(MSG_ATTRIB), ept->path);

			/* fix the attributes now for robustness sake */
			if (averify(1, &ept->ftype,
			    ept->path,
			    &ept->ainfo) == 0) {
				mstat[i].attrchg = 0;
			}
		}
	}

	if (maxvol == part)
		eocflag++; /* endofclass */
	return (-1); /* no entry on this volume */
}

/*
 * This is the function that cleans up the installation of this class.
 * This is where hard links get put in since the stuff they're linking
 * probably exists by now.
 */
static void
endofclass(struct cfextra **extlist, struct mergstat *mstat, int myclass,
	int ckflag)
{
	struct	cfextra entry;
	struct	cfent *ept;
	struct	pinfo *pinfo;
	int	n, idx, flag;
	FILE	*fp;
	FILE	*fpo;
	char	*save_path, *special, *mountp;
	char	path[PATH_MAX];
	char	*temppath;

	if (!ocfile(&fp, &fpo, pkgmap_blks))
		quit(99);

	echo(gettext("[ verifying class <%s> ]"), cl_nam(myclass));

	entry.cf_ent.pinfo = NULL;
	for (idx = 0; /* void */; idx++) {
		/* find next package object in this class */
		while (extlist[idx]) {
			if ((extlist[idx]->cf_ent.ftype != 'i') &&
			    extlist[idx]->cf_ent.pkg_class_idx == myclass)
				break;
			idx++;
		}

		ept = &(extlist[idx]->cf_ent);

		temppath =
		    extlist[idx] ? extlist[idx]->client_path :
		    NULL;

		n = srchcfile(&(entry.cf_ent), (ept ? temppath : NULL), fp,
		    fpo);

		if (n == 0)
			break;
		else if (n < 0) {
			progerr(gettext(ERR_CFBAD));
			logerr(gettext("pathname=%s\n"),
			    (entry.cf_ent.path && *entry.cf_ent.path) ?
			    entry.cf_ent.path : "Unknown");
			logerr(gettext("problem=%s\n"),
			    (errstr && *errstr) ? errstr : "Unknown");
			quit(99);
		} else if (n != 1) {
			/* check if path should not be in the contents file */
			if ((mstat[idx].shared && nocnflct))
				continue;
			progerr(gettext(ERR_CFMISSING), ept->path);
			quit(99);
		}

		/*
		 * validate this entry and change the status
		 * flag in the 'contents' file
		 */
		if (ept->ftype == RM_RDY)
			(void) eptstat(&(entry.cf_ent), pkginst, STAT_NEXT);
		else {
			if (ept->ftype == 'l') {
				if (averify(0, &ept->ftype,
				    ept->path, &ept->ainfo)) {
					echo(gettext("%s <linked pathname>"),
						ept->path);
					mstat[idx].attrchg++;
				}
			}

			/*
			 * Don't install or verify objects for remote,
			 * read-only filesystems.  We need only flag
			 * them as shared from some server. Otherwise,
			 * ok to do final check.
			 */
			if (is_remote_fs(ept->path,
			    &(extlist[idx]->fsys_value)) &&
			    !is_fs_writeable(ept->path,
			    &(extlist[idx]->fsys_value)))
				flag = -1;
			else
				flag = finalck(ept, mstat[idx].attrchg,
				    (ckflag ? mstat[idx].contchg : (-1)));

			pinfo = entry.cf_ent.pinfo;
			while (pinfo) {
				if (strcmp(pkginst, pinfo->pkg) == 0)
					break;
				pinfo = pinfo->next;
			}
			if (pinfo)
				if (flag < 0)
					/* network object */
					pinfo->status = SHARED_FILE;
				else
					pinfo->status = (flag ? NOT_FND :
					    ENTRY_OK);
		}

		if (entry.cf_ent.npkgs)
			if (putcfile(&(entry.cf_ent), fpo))
				quit(99);
	}

	if ((n = swapcfile(fp, fpo, (dbchg ? pkginst : NULL))) == RESULT_WRN)
		warnflag++;
	else if (n == RESULT_ERR)
		quit(99);
}

/*
 * This function goes through and fixes all the attributes. This is called
 * out by using DST_QKVERIFY=this_class in the pkginfo file. The primary
 * use for this is to fix up files installed by a class action script
 * which is time-critical and reliable enough to assume likely success.
 * The first such format was for WOS compressed-cpio'd file sets.
 */
static int
fix_attributes(struct cfextra **extlist, struct mergstat *mstat, int idx)
{
	struct	cfextra entry;
	int	i, retval = 1;
	int 	nc = cl_getn();
	struct cfent *ept;

	for (i = 0; extlist[i]; i++) {
		ept = &(extlist[i]->cf_ent);

		/*
		 * We don't care about 'i'nfo files because, they
		 * aren't laid down, 'e'ditable files can change
		 * anyway, so who cares and 's'ymlinks were already
		 * fixed in domerg(); however, certain old WOS
		 * package symlinks depend on a bug in the old
		 * pkgadd which has recently been expunged. For
		 * those packages in 2.2, we repeat the verification
		 * of symlinks.
		 *
		 * By 2.6 or so, ftype == 's' should be added to this.
		 */
		if (ept->ftype == 'i' ||
		    ept->ftype == 'e' ||
		    (mstat[i].shared && nocnflct))
			continue;

		if (ept->pkg_class_idx < 0 || ept->pkg_class_idx > nc) {
			progerr(gettext(ERR_CLIDX), ept->pkg_class_idx,
			    (ept->path && *ept->path) ? ept->path : "unknown");
			continue;
		}

		/* If this is the right class, do the fast verify. */
		if (ept->pkg_class_idx == idx) {
			if (fverify(1, &ept->ftype, ept->path,
			    &ept->ainfo, &ept->cinfo) == 0) {
				mstat[i].attrchg = 0;
				mstat[i].contchg =  0;
			} else	/* We'll try full verify later */
				retval = 0;
		}
	}

	return (retval);
}
