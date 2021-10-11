/*
 *	autod_mount.c
 *
 *	Copyright (c) 1988-1993 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)autod_mount.c	1.30	95/10/10 SMI"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <errno.h>
#include <pwd.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/tiuser.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <nfs/nfs.h>
#include "automount.h"
#include <assert.h>
#include <fcntl.h>
#include "autofs_prot.h"

#ifndef _REENTRANT  /* see bugid 1178395 */
extern char *strtok_r(char *, const char *, char **);
#endif

extern struct mapent *do_mapent_hosts();
extern struct mapent *getmapent_fn(char *, char *, char *);
extern void del_mnttab(char *);
extern enum clnt_stat pingnfs(char *, int, u_long *);
extern enum nfsstat nfsmount(char *, char *, char *, char *, int, int);
extern int nfsunmount(struct mnttab *);
extern int loopbackmount(char *, char *, char *, int);
extern int parse_nfs(char *, struct mapent *, char *, char *,
				char **, char **, int);
extern void getword(char *, char *, char **, char **, char, int);
extern int rmdir_r(char *, int);
extern mount_nfs(struct mapent *, char *, char *, int);
extern int autofs_mkdir_r(char *, int *);
extern int mount_generic(char *, char *, char *, char *, int);
extern void free_mapent(struct mapent *);

extern int verbose;

static int unmount_mntpnt(struct mntlist *);
static void remount_hierarchy(struct mntlist *);
static int unmount_hierarchy(struct mntlist *);
static int fork_exec(char *, char *, char **, int);
static int parse_special(struct mapent *, char *, char *,
				char **, char **, int);
static int lofs_match(struct mnttab *mnt, struct umntrequest *ur);

#define	MNTTYPE_CACHEFS	"cachefs"

do_mount1(mapname, key, mapopts, path, cred)
	char *mapname;
	char *key;
	char *mapopts;
	char *path;
	struct authunix_parms *cred;
{
	struct mapline ml;
	struct mapent *me, *mapents = NULL;
	char mntpnt[MAXPATHLEN];
	char spec_mntpnt[MAXPATHLEN];
	int err, imadeit, depth;
	struct stat stbuf;
	char *private;
			/*
			 * data to be remembered by fs specific
			 * mounts. eg prevhost in case of nfs
			 */
	int overlay = 0;
	int isdirect = 0;
	int mount_ok = 0;
	int len;

	if (strncmp(mapname, FNPREFIX, FNPREFIXLEN) == 0) {
		mapents = getmapent_fn(key, mapname, mapopts);
	} else {
		err = getmapent(key, mapname, &ml);
		if (err == 0)
		    mapents = parse_entry(key, mapname, mapopts, &ml);
	}

	/*
	 * Now we indulge in a bit of hanky-panky.
	 * If the entry isn't found in the map and the
	 * name begins with an "=" then we assume that
	 * the name is an undocumented control message
	 * for the daemon.  This is accessible only
	 * to superusers.
	 */
	if (mapents == NULL) {
		if (*key == '=' && cred->aup_uid == 0) {
			if (isdigit(*(key+1))) {
				/*
				 * If next character is a digit
				 * then set the trace level.
				 */
				trace = atoi(key+1);
				trace_prt(1, "Automountd: trace level = %d\n",
					trace);
			} else if (*(key+1) == 'v') {
				/*
				 * If it's a "v" then
				 * toggle verbose mode.
				 */
				verbose = !verbose;
				trace_prt(1, "Automountd: verbose %s\n",
						verbose ? "on" : "off");
			}
		}

		err = ENOENT;
		goto done;
	}

	if (trace > 1) {
		struct mapfs  *mfs;

		trace_prt(1, "  mapname = %s, key = %s\n", mapname, key);
		for (me = mapents; me; me = me->map_next) {
			trace_prt(1, "     (%s,%s) %s \t-%s\t",
				me->map_fstype,
				me->map_mounter,
				*me->map_mntpnt ? me->map_mntpnt : "/",
				me->map_mntopts);
			for (mfs = me->map_fs; mfs; mfs = mfs->mfs_next)
				trace_prt(0, "%s:%s ",
					mfs->mfs_host ? mfs->mfs_host: "",
					mfs->mfs_dir);
			trace_prt(0, "\n");
		}
	}

	/*
	 * Each mapent in the list describes a mount to be done.
	 * Normally there's just a single entry, though in the
	 * case of /net mounts there may be many entries, that
	 * must be mounted as a hierarchy.  For each mount the
	 * automountd must make sure the required mountpoint
	 * exists and invoke the appropriate mount command for
	 * the fstype.
	 */
	private = "";
	for (me = mapents; me; me = me->map_next) {
		(void) sprintf(mntpnt, "%s%s%s", path, mapents->map_root,
				me->map_mntpnt);
		/*
		 * remove trailing /'s from mountpoint to avoid problems
		 * stating a directory with two or more trailing slashes.
		 * This will let us mount directories from machines
		 * which export with two or more slashes (apollo for instance).
		 */
		len = strlen(mntpnt) - 1;
		while (mntpnt[len] == '/')
			mntpnt[len--] = '\0';

		imadeit = 0;
		overlay = strcmp(mntpnt, key) == 0;
		if (overlay || isdirect) {
			/*
			 * direct mount and no offset
			 * the isdirect flag takes care of heirarchies
			 * mounted under a direct mount
			 */
			isdirect = 1;
			(void) strcpy(spec_mntpnt, mntpnt);
		} else
			(void) sprintf(spec_mntpnt, "%s%s", mntpnt, " ");

		if (lstat(spec_mntpnt, &stbuf) != 0) {
			depth = 0;
			if (autofs_mkdir_r(mntpnt, &depth) == 0) {
				imadeit = 1;
				if (stat(spec_mntpnt, &stbuf) < 0) {
					syslog(LOG_ERR,
					"Couldn't stat created mntpoint %s: %m",
						spec_mntpnt);
					continue;
				}
			} else {
				if (verbose)
					syslog(LOG_ERR,
					"Couldn't create mntpoint %s: %m",
						spec_mntpnt);
				continue;
			}
		} else if ((stbuf.st_mode & S_IFMT) == S_IFLNK) {
			if (verbose)
syslog(LOG_ERR, "%s symbolic link: not a valid auto-mountpoint.\n",
				spec_mntpnt);
			continue;
		}

		if (strcmp(me->map_fstype, MNTTYPE_NFS) == 0) {
			mutex_lock(&mt_unsafe);
			err = mount_nfs(me, spec_mntpnt, private, overlay);
			mutex_unlock(&mt_unsafe);
		} else {
			err = mount_generic(me->map_fs->mfs_dir,
					    me->map_fstype, me->map_mntopts,
					    spec_mntpnt, overlay);
		}

		if (!err) {
			mount_ok++;
		} else {
			/*
			 * If the first mount of a no-offset
			 * direct mount fails, further sub-mounts
			 * will block since they aren't using
			 * special paths.  Avoid hang and quit here.
			 */
			if (isdirect) {
				err = ENOENT;
				goto done;
			}

			if (imadeit)
				if (rmdir_r(spec_mntpnt, depth) < 0)
					trace_prt(1,
						"  Rmdir: err=%d, mnt='%s'\n",
						errno, spec_mntpnt);
		}
	}
	err = mount_ok ? 0 : ENOENT;
done:
	if (mapents)
		free_mapent(mapents);

	return (err);
}

/*
 * Check the option string for an "fstype"
 * option.  If found, return the fstype
 * and the option string with the fstype
 * option removed, e.g.
 *
 *  input:  "fstype=cachefs,ro,nosuid"
 *  opts:   "ro,nosuid"
 *  fstype: "cachefs"
 */
void
get_opts(input, opts, fstype)
	char *input;
	char *opts; 	/* output */
	char *fstype;   /* output */
{
	char *p, *pb;
	char buf[1024];
	char *placeholder;

	*opts = '\0';
	(void) strcpy(buf, input);
	pb = buf;
	while (p = (char *)strtok_r(pb, ",", &placeholder)) {
		pb = NULL;
		if (strncmp(p, "fstype=", 7) == 0) {
			(void) strcpy(fstype, p + 7);
		} else {
			if (*opts)
				(void) strcat(opts, ",");
			(void) strcat(opts, p);
		}
	}
}

struct mapent *
parse_entry(key, mapname, mapopts, ml)
	char *key;
	char *mapname;
	char *mapopts;
	struct mapline *ml;
{
	struct mapent *me, *ms, *mp;
	int err, implied;
	char *lp = ml->linebuf;
	char *lq = ml->lineqbuf;
	char w[MAXPATHLEN+1], wq[MAXPATHLEN+1];
	char entryopts[1024];
	char defaultopts[1024];
	char fstype[32], mounter[32];
	char *p;

	/*
	 * Assure the key is only one token long.
	 * This prevents options from sneaking in through the
	 * command line or corruption of /etc/mnttab.
	 */
	for (p = key; *p != '\0'; p++) {
		if (isspace(*p)) {
			syslog(LOG_ERR, "bad key in map %s: %s", mapname, key);
			return ((struct mapent *) NULL);
		}
	}

	if (strcmp(lp, "-hosts") == 0) {
		mutex_lock(&mt_unsafe);
		ms = do_mapent_hosts(mapopts, key);
		mutex_unlock(&mt_unsafe);
		return (ms);
	}

	if (macro_expand(key, lp, lq, LINESZ)) {
		syslog(LOG_ERR,
		    "map %s: line too long (max %d chars)",
		    mapname, LINESZ - 1);
		return ((struct mapent *) NULL);
	}
	getword(w, wq, &lp, &lq, ' ', sizeof (w));
	(void) strcpy(fstype, MNTTYPE_NFS);

	entryopts[0] = '\0';
	get_opts(mapopts, defaultopts, fstype);

	if (w[0] == '-') {	/* default mount options for entry */
		/*
		 * Since options may not apply to previous fstype,
		 * we want to set it to the default value again
		 * before we read the deffault mount options for entry.
		 */
		(void) strcpy(fstype, MNTTYPE_NFS);

		(void) get_opts(w+1, entryopts, fstype);
		getword(w, wq, &lp, &lq, ' ', sizeof (w));
	}

	implied = *w != '/';

	ms = NULL;
	me = NULL;
	while (*w == '/' || implied) {
		mp = me;
		me = (struct mapent *) malloc(sizeof (*me));
		if (me == NULL)
			goto alloc_failed;
		(void) memset((char *) me, 0, sizeof (*me));
		if (ms == NULL)
			ms = me;
		else
			mp->map_next = me;


		if (strcmp(w, "/") == 0 || implied)
			me->map_mntpnt = strdup("");
		else
			me->map_mntpnt = strdup(w);
		if (me->map_mntpnt == NULL)
			goto alloc_failed;

		if (implied)
			implied = 0;
		else
			getword(w, wq, &lp, &lq, ' ', sizeof (w));

		if (w[0] == '-') {	/* mount options */
			/*
			 * Since options may not apply to previous fstype,
			 * we want to set it to the default value again
			 * before we read the deffault mount options for entry.
			 */
			(void) strcpy(fstype, MNTTYPE_NFS);

			get_opts(w+1, entryopts, fstype);
			getword(w, wq, &lp, &lq, ' ', sizeof (w));
		}

		if (w[0] == '\0') {
			syslog(LOG_ERR, "bad key in map %s: %s", mapname, key);
			goto bad_entry;
		}

		(void) strcpy(mounter, fstype);

		me->map_mntopts = entryopts[0]?strdup(entryopts):
				strdup(defaultopts);
		if (me->map_mntopts == NULL)
			goto alloc_failed;

		/*
		 * The following ugly chunk of code crept in as
		 * a result of cachefs.  If it's a cachefs mount
		 * of an nfs filesystem, then it's important to
		 * parse the nfs special field.  Otherwise, just
		 * hand the special field to the fs-specific mount
		 * program with no special parsing.
		 */
		if (strcmp(fstype, MNTTYPE_CACHEFS) == 0) {
			struct mnttab m;
			char *p;

			m.mnt_mntopts = entryopts;
			if ((p = hasmntopt(&m, "backfstype")) != NULL) {
				int len = strlen(MNTTYPE_NFS);

				p += 11;
				if (strncmp(p, MNTTYPE_NFS, len) == 0 &&
				    (p[len] == '\0' || p[len] == ',')) {
					/*
					 * Cached nfs mount
					 */
					(void) strcpy(fstype, MNTTYPE_NFS);
					(void) strcpy(mounter, MNTTYPE_CACHEFS);
				}
			}
		}

		me->map_fstype = strdup(fstype);
		if (me->map_fstype == NULL)
			goto alloc_failed;
		me->map_mounter = strdup(mounter);
		if (me->map_mounter == NULL)
			goto alloc_failed;

		if (strcmp(fstype, MNTTYPE_NFS) == 0)
			err = parse_nfs(mapname, me, w, wq, &lp, &lq,
				sizeof (w));
		else
			err = parse_special(me, w, wq, &lp, &lq,
				sizeof (w));

		if (err < 0)
			goto alloc_failed;
		if (err > 0)
			goto bad_entry;
		me->map_next = NULL;
	}

	if (*key == '/') {
		*w = '\0';	/* a hack for direct maps */
	} else {
		(void) strcpy(w, "/");
		(void) strcat(w, key);
	}
	ms->map_root = strdup(w);
	if (ms->map_root == NULL)
		goto alloc_failed;

	return (ms);

alloc_failed:
	syslog(LOG_ERR, "Memory allocation failed: %m");
bad_entry:
	free_mapent(ms);
	return ((struct mapent *) NULL);
}


void
free_mapent(me)
	struct mapent *me;
{
	struct mapfs *mfs;
	struct mapent *m;

	while (me) {
		while (me->map_fs) {
			mfs = me->map_fs;
			if (mfs->mfs_host)
				free(mfs->mfs_host);
			if (mfs->mfs_dir)
				free(mfs->mfs_dir);
			me->map_fs = mfs->mfs_next;
			free((char *) mfs);
		}

		if (me->map_root)
			free(me->map_root);
		if (me->map_mntpnt)
			free(me->map_mntpnt);
		if (me->map_mntopts)
			free(me->map_mntopts);
		if (me->map_fstype)
			free(me->map_fstype);
		if (me->map_mounter)
			free(me->map_mounter);

		m = me;
		me = me->map_next;
		free((char *) m);
	}
}

static int
parse_special(me, w, wq, lp, lq, wsize)
	struct mapent *me;
	char *w, *wq, **lp, **lq;
	int wsize;
{
	char devname[MAXPATHLEN + 1], qbuf[MAXPATHLEN + 1];
	char *wlp, *wlq;
	struct mapfs *mfs;

	wlp = w;
	wlq = wq;
	getword(devname, qbuf, &wlp, &wlq, ' ', sizeof (devname));
	mfs = (struct mapfs *) malloc(sizeof (struct mapfs));
	if (mfs == NULL)
		return (-1);
	(void) memset(mfs, 0, sizeof (*mfs));

	/*
	 * A device name that begins with a slash could
	 * be confused with a mountpoint path, hence use
	 * a colon to escape a device string that begins
	 * with a slash, e.g.
	 *
	 *	foo  -ro  /bar  foo:/bar
	 * and
	 *	foo  -ro  /dev/sr0
	 *
	 * would confuse the parser.  The second instance
	 * must use a colon:
	 *
	 *	foo  -ro  :/dev/sr0
	 */
	mfs->mfs_dir = strdup(&devname[devname[0] == ':']);
	if (mfs->mfs_dir == NULL)
		return (-1);
	me->map_fs = mfs;
	getword(w, wq, lp, lq, ' ', wsize);
	return (0);
}

#define	ARGV_MAX	16
#define	VFS_PATH	"/usr/lib/fs"

mount_generic(special, fstype, opts, mntpnt, overlay)
	char *special, *fstype, *opts, *mntpnt;
	int overlay;
{
	struct mnttab m;
	struct stat stbuf;
	int i;
	char *newargv[ARGV_MAX];

	if (trace > 1) {
		trace_prt(1, "  mount: %s %s %s %s\n",
			special, mntpnt, fstype, opts);
	}

	if (stat(mntpnt, &stbuf) < 0) {
		syslog(LOG_ERR, "Couldn't stat %s: %m", mntpnt);
		return (ENOENT);
	}

	i = 2;

	if (overlay)
		newargv[i++] = "-O";

	if (opts && *opts) {
		m.mnt_mntopts = opts;
		if (hasmntopt(&m, MNTOPT_RO) != NULL)
			newargv[i++] = "-r";
		newargv[i++] = "-o";
		newargv[i++] = opts;
	}
	newargv[i++] = "--";
	newargv[i++] = special;
	newargv[i++] = mntpnt;
	newargv[i] = NULL;
	return (fork_exec(fstype, "mount", newargv, verbose));
}

static int
fork_exec(fstype, cmd, newargv, console)
	char *fstype;
	char *cmd;
	char **newargv;
	int console;
{
	char path[MAXPATHLEN];
	int i;
	int stat_loc;
	int fd = 0;
	struct stat stbuf;
	int res;
	int child_pid;

	/* build the full path name of the fstype dependent command */
	(void) sprintf(path, "%s/%s/%s", VFS_PATH, fstype, cmd);

	if (stat(path, &stbuf) != 0) {
		res = errno;
		if (trace > 1)
			trace_prt(1, "  fork_exec: stat %s returned %d\n",
				path, res);
		return (res);
	}

	if (trace > 1) {
		trace_prt(1, "  fork_exec: %s ", path);
		for (i = 2; newargv[i]; i++)
			trace_prt(0, "%s ", newargv[i]);
		trace_prt(0, "\n");
	}


	newargv[1] = cmd;
	switch ((child_pid = fork1())) {
	case -1:
		syslog(LOG_ERR, "Cannot fork: %m");
		return (errno);
	case 0:
		/*
		 * Child
		 */
		(void) setsid();
		fd = open(console ? "/dev/console" : "/dev/null", O_WRONLY);
		if (fd != -1) {
			(void) dup2(fd, 1);
			(void) dup2(fd, 2);
			(void) close(fd);
		}

		(void) execv(path, &newargv[1]);
		if (errno == EACCES)
			syslog(LOG_ERR, "exec %s: %m", path);

		_exit(errno);
	default:
		/*
		 * Parent
		 */
		(void) waitpid(child_pid, &stat_loc, WUNTRACED);

		if (WIFEXITED(stat_loc)) {
			if (trace > 1) {
				trace_prt(1,
				    "  fork_exec: returns exit status %d\n",
				    WEXITSTATUS(stat_loc));
			}

			return (WEXITSTATUS(stat_loc));
		} else
		if (WIFSIGNALED(stat_loc)) {
			if (trace > 1) {
				trace_prt(1,
				    "  fork_exec: returns signal status %d\n",
				    WTERMSIG(stat_loc));
			}
		} else {
			if (trace > 1)
				trace_prt(1,
				    "  fork_exec: returns unknown status\n");
		}

		return (1);
	}
}

do_unmount1(ul)
	umntrequest *ul;
{

	struct mntlist *mntl_head, *mntl;
	struct mntlist *getmntlist();
	struct mntlist *match;
	struct umntrequest *ur;
	int res = 0;

	mntl_head = getmntlist();
	if (mntl_head == NULL)
		return (1);

	for (ur = ul; ur; ur = ur->next) {
		/*
		 * Find the last entry with a matching
		 * device id.
		 * Loopback mounts have the same device id
		 * as the real filesystem, so the autofs
		 * gives us the rdev as well. The lofs
		 * assigns a unique rdev for each lofs mount
		 * so that they can be recognized by the
		 * automountd.
		 */
		match = NULL;
		for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
			if (ur->devid == mntl->mntl_dev &&
			    ! mntl->mntl_flags & MNTL_UNMOUNT) {
				if (strcmp(mntl->mntl_mnt->mnt_fstype,
				    MNTTYPE_LOFS) == 0) {
					if (lofs_match(mntl->mntl_mnt, ur)) {
						match = mntl;
						break;
					} else match = NULL;
				} else {
					/*
					 * devid matches; not lofs
					 * continue to find the last
					 * matching devid
					 */
					match = mntl;
				}
			}
		}

		if (match == NULL) {
			syslog(LOG_ERR, "dev %x, rdev %x  not in mnttab",
				ur->devid, ur->rdevid);
			if (trace > 1) {
				trace_prt(1,
					"  do_unmount: %x = ? "
					"<----- mntpnt not found\n",
					ur->devid);
			}
			continue;
		}

		/*
		 * Special case for NFS mounts.
		 * Don't want to attempt unmounts from
		 * a dead server.  If any member of a
		 * hierarchy belongs to a dead server
		 * give up (try later).
		 */
		if (strcmp(match->mntl_mnt->mnt_fstype, MNTTYPE_NFS) == 0) {
			char *server, *p;

			server = match->mntl_mnt->mnt_special;
			p = strchr(server, ':');
			if (p) {
				*p = '\0';
				if (pingnfs(server, 1, NULL) != RPC_SUCCESS) {
					res = 1;
					goto done;
				}
				*p = ':';
			}
		}

		match->mntl_flags |= MNTL_UNMOUNT;
		if (!ur->isdirect)
			(void) strcat(
				match->mntl_mnt->mnt_mountp, " ");
	}

	res = unmount_hierarchy(mntl_head);

done:
	fsfreemntlist(mntl_head);
	return (res);
}


/*
 * Mount every filesystem in the mount list
 * that has its unmount flag set.
 */
static void
remount_hierarchy(mntl_head)
	struct mntlist *mntl_head;
{
	struct mntlist *mntl, *m;
	char *special, *mntpnt, *fstype, *opts;
	char *server;
	char *p, *q;
	int overlay;

	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next) {
		if (!(mntl->mntl_flags & MNTL_UNMOUNT))
			continue;

		special = mntl->mntl_mnt->mnt_special;
		mntpnt = mntl->mntl_mnt->mnt_mountp;
		fstype = mntl->mntl_mnt->mnt_fstype;
		opts   = mntl->mntl_mnt->mnt_mntopts;

		/*
		 * If the mountpoint path matches a previous
		 * mountpoint then it's an overlay mount.
		 * Make sure the overlay flag is set for the
		 * remount otherwise the mount will fail
		 * with EBUSY.
		 */
		overlay = 0;
		for (m = mntl_head; m != mntl; m = m->mntl_next) {
			if (strcmp(m->mntl_mnt->mnt_mountp, mntpnt) == 0) {
				overlay = 1;
				break;
			}
		}

		/*
		 * Remove "dev=xxx" from the opts
		 */
		p = strstr(opts, "dev=");
		if (p) {
			for (q = p + 4; isalnum(*q); q++)
				;
			if (*q == ',')
				q++;
			(void) strcpy(p, q);
			p = opts + (strlen(opts) - 1);
			if (*p == ',')
				*p = '\0';
		}

		if (strcmp(fstype, MNTTYPE_NFS) == 0) {
			server = special;
			p = strchr(server, ':');
			*p = '\0';
			if (nfsmount(server, p + 1, mntpnt, opts, 0, overlay)) {
				syslog(LOG_ERR, "Could not remount %s\n",
					mntpnt);
			}

			*p = ':';
			continue;
		}

		/*
		 * For a cachefs mount, we need to determine the real
		 * "special", since it is maintained in the underlying
		 * NFS mount entry.  Look for a match of our current
		 * "mnt_special" against the "mnt_mountp" field of some
		 * other entry.  The "mnt_special" field of that entry
		 * is the one we want to use.
		 */
		if (strcmp(fstype, MNTTYPE_CACHEFS) == 0) {
			for (m = mntl; m; m = m->mntl_next) {
				if (strcmp(m->mntl_mnt->mnt_mountp, special)
				    == 0) {
					special = m->mntl_mnt->mnt_special;
					break;
				}
			}
		}

		if (strcmp(fstype, MNTTYPE_LOFS) == 0) {
			if (loopbackmount(special, mntpnt, opts, overlay)) {
				syslog(LOG_ERR, "Could not remount %s\n",
					mntpnt);
			}
			continue;
		}

		if (mount_generic(special, fstype, opts, mntpnt, overlay)) {
			syslog(LOG_ERR, "Could not remount %s",
				mntpnt);
		}
	}
}

static int
unmount_hierarchy(mntl_head)
	struct mntlist *mntl_head;
{
	struct mntlist *mntl;
	int res = 0;

	for (mntl = mntl_head; mntl; mntl = mntl->mntl_next)
		if (mntl->mntl_flags & MNTL_UNMOUNT)
			break;
	if (mntl == NULL)
		return (0);

	res = unmount_hierarchy(mntl->mntl_next);
	if (res)
		return (res);

	res = unmount_mntpnt(mntl);
	if (res)
		remount_hierarchy(mntl->mntl_next);

	return (res);
}

/* This procedure should return errno */

static int
unmount_mntpnt(mntl)
	struct mntlist *mntl;
{
	char *fstype = mntl->mntl_mnt->mnt_fstype;
	char *mountp = mntl->mntl_mnt->mnt_mountp;
	char *newargv[ARGV_MAX];
	int res;

	if (strcmp(fstype, MNTTYPE_NFS) == 0) {
		res = nfsunmount(mntl->mntl_mnt);
		if (res == 0)
			del_mnttab(mountp);
	} else {
		newargv[2] = mountp;
		newargv[3] = NULL;

		res = fork_exec(fstype, "umount", newargv, verbose);

		if (res == ENOENT) {
			res = 0;
			if (umount(mountp) < 0)
				res = errno;
			else
				del_mnttab(mountp);
		}
	}


	if (trace > 1)
		trace_prt(1, "  unmount %s %s\n",
			mountp, res ? "failed" : "OK");
	return (res);
}

/*
 * stats the patch refered to by mnt_mnt->mnt_mountp, checks its
 * st_rdev, and returns 1 if it matches "rdev", 0 otherwise.
 */
static int
lofs_match(struct mnttab *mnt, struct umntrequest *ur)
{
	struct stat stbuf;
	char *spec_mntpnt;
	int retcode = 0;

	if (ur->isdirect) {
		if (stat(mnt->mnt_mountp, &stbuf) == 0 &&
			stbuf.st_rdev == ur->rdevid)
				retcode = 1;
	} else {
		int len;
		/*
		 * for indirect mounts add a space to
		 * avoid deadlock			
		 */
		len = strlen(mnt->mnt_mountp);
		mnt->mnt_mountp[len] = ' ';
		mnt->mnt_mountp[len+1] = '\0';
		if (stat(mnt->mnt_mountp, &stbuf) == 0 &&
			stbuf.st_rdev == ur->rdevid)
				retcode = 1;
		mnt->mnt_mountp[len] = '\0';
	}
	return (retcode);
}
