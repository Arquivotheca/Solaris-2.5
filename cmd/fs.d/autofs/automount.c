/*
 *	automount.c
 *
 *	Copyright (c) 1988-1993 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)automount.c	1.10	94/11/04 SMI"

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <varargs.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <syslog.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/signal.h>
#include <sys/utsname.h>
#include <sys/tiuser.h>
#include <rpc/rpc.h>
#include <rpcsvc/nfs_prot.h>
#include <sys/fs/autofs.h>
#include "automount.h"


int mkdir_r();
struct mntlist *getmntlist();
struct autodir *dir_head;
struct autodir *dir_tail;
extern struct mntlist *current_mounts;
static struct mnttab *find_mount();
int verbose;

static void usage();
static int compare_opts(char *, char *);
static void do_unmounts();

extern void ns_setup();
extern int loadmaster_map(char *, char *);
extern void fix_mnttab(struct mnttab *);
extern void del_mnttab(char *);
extern void pr_msg();

static int mount_timeout = 5 * 60;	/* 5 minutes */
static int rpc_timeout = 60;

main(argc, argv)
	int argc;
	char *argv[];
{
	extern char *optarg;
	extern int optind;
	int c;
	struct auto_args ai;
	struct utsname utsname;
	char autofs_addr[128];
	struct autodir *dir, *d;
	struct stat stbuf;
	char *master_map = "auto_master";
	int null;
	struct mnttab mnt, *mntp;
	char mntopts[1000];
	int mntflgs;
	int count = 0;

	while ((c = getopt(argc, argv, "mM:D:f:t:v?")) != EOF) {
		switch (c) {
		case 'm':
			pr_msg("Warning: -m option not supported");
			break;
		case 'M':
			pr_msg("Warning: -M option not supported");
			break;
		case 'D':
			pr_msg("Warning: -D option not supported");
			break;
		case 'f':
			pr_msg("Error: -f option no longer supported");
			usage();
			break;
		case 't':
			if (strchr(optarg, '=')) {
				pr_msg("Error: invalid value for -t");
				usage();
			}
			mount_timeout = atoi(optarg);
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
			break;
		}
	}

	if (optind < argc) {
		pr_msg("%s: command line mountpoints/maps "
			"no longer supported",
			argv[optind]);
		usage();
	}

	if (geteuid() != 0) {
		pr_msg("must be root");
		exit(1);
	}

	current_mounts = getmntlist();
	if (current_mounts == NULL) {
		pr_msg("Couldn't establish current mounts");
		exit(1);
	}

	(void) umask(0);
	ns_setup();

	openlog("automount", LOG_PID, LOG_DAEMON);
	(void) loadmaster_map(master_map, "");
	closelog();

	if (uname(&utsname) < 0) {
		pr_msg("uname: %m");
		exit(1);
	}
	(void) strcpy(autofs_addr, utsname.nodename);
	(void) strcat(autofs_addr, ".autofs");
	ai.addr.buf	= autofs_addr;
	ai.addr.len	= strlen(ai.addr.buf);
	ai.addr.maxlen	= ai.addr.len;

	ai.mount_to	= mount_timeout;
	ai.rpc_to	= rpc_timeout;

	/*
	 * Mount the daemon at its mount points.
	 */
	for (dir = dir_head; dir; dir = dir->dir_next) {

		/*
		 * Skip null entries
		 */
		if (strcmp(dir->dir_map, "-null") == 0)
			continue;

		/*
		 * Skip null'ed entries
		 */
		null = 0;
		for (d = dir->dir_prev; d; d = d->dir_prev) {
			if (strcmp(dir->dir_name, d->dir_name) == 0)
				null = 1;
		}
		if (null)
			continue;

		/*
		 * Check whether there's already an entry
		 * in the mnttab for this mountpoint.
		 */
		if (mntp = find_mount(dir->dir_name, 1)) {
			/*
			 * If it's not an autofs mount - don't
			 * mount over it.
			 */
			if (strcmp(mntp->mnt_fstype, MNTTYPE_AUTOFS) != 0) {
				pr_msg("%s: already mounted",
					mntp->mnt_mountp);
				continue;
			}

			/*
			 * Compare the mnttab entry with the master map
			 * entry.  If the map or mount options are
			 * different, then update this information
			 * with a remount.
			 */
			if (strcmp(mntp->mnt_special, dir->dir_map) == 0 &&
				compare_opts(dir->dir_opts,
					mntp->mnt_mntopts) == 0) {
				continue;	/* no change */
			}

			/*
			 * Check for an overlaid direct autofs mount.
			 * Cannot remount since it's inaccessible.
			 */
			if (hasmntopt(mntp, "direct") != NULL) {
				mntp = find_mount(dir->dir_name, 0);
				if (hasmntopt(mntp, "direct") == NULL) {
					if (verbose)
						pr_msg("%s: cannot remount",
							dir->dir_name);
					continue;
				}
			}

			dir->dir_remount = 1;
		}

		/*
		 * Create a mount point if necessary
		 * If the path refers to an existing symbolic
		 * link, refuse to mount on it.  This avoids
		 * future problems.
		 */
		if (lstat(dir->dir_name, &stbuf) == 0) {
			if ((stbuf.st_mode & S_IFMT) != S_IFDIR) {
				pr_msg("%s: Not a directory", dir->dir_name);
				continue;
			}
		} else {
			if (mkdir_r(dir->dir_name)) {
				pr_msg("%s: %m", dir->dir_name);
				continue;
			}
		}

		ai.path 	= dir->dir_name;
		ai.opts		= dir->dir_opts;
		ai.map		= dir->dir_map;
		ai.direct 	= dir->dir_direct;
		mntflgs = dir->dir_remount ? MS_REMOUNT : 0;

		if (mount("", dir->dir_name, MS_DATA | mntflgs, MNTTYPE_AUTOFS,
				&ai, sizeof (ai)) < 0) {
			pr_msg("mount %s: %m", dir->dir_name);
			continue;
		}

		/*
		 * Add autofs entry to /etc/mnttab
		 */
		mnt.mnt_special = dir->dir_map;
		mnt.mnt_mountp  = dir->dir_name;
		mnt.mnt_fstype  = MNTTYPE_AUTOFS;
		(void) sprintf(mntopts, "ignore,%s",
			dir->dir_direct  ? "direct" : "indirect");
		if (dir->dir_opts && *dir->dir_opts) {
			(void) strcat(mntopts, ",");
			(void) strcat(mntopts, dir->dir_opts);
		}
		mnt.mnt_mntopts = mntopts;
		if (dir->dir_remount)
			fix_mnttab(&mnt);
		else
			(void) add_mnttab(&mnt);

		count++;

		if (verbose) {
			if (dir->dir_remount)
				pr_msg("%s remounted", dir->dir_name);
			else
				pr_msg("%s mounted", dir->dir_name);
		}
	}

	if (verbose && count == 0)
		pr_msg("no mounts");

	/*
	 * Now compare the /etc/mnttab with the master
	 * map.  Any autofs mounts in the /etc/mnttab
	 * that are not in the master map must be
	 * unmounted
	 */
	do_unmounts();

	return (0);
}

/*
 * Find a mount entry given
 * the mountpoint path.
 * Optionally return the first
 * or last entry.
 */
static struct mnttab *
find_mount(mntpnt, first)
	char *mntpnt;
	int first;
{
	struct mntlist *mntl;
	struct mnttab *found = NULL;

	for (mntl = current_mounts; mntl; mntl = mntl->mntl_next) {

		if (strcmp(mntpnt, mntl->mntl_mnt->mnt_mountp) == 0) {
			found = mntl->mntl_mnt;
			if (first)
				break;
		}
	}

	return (found);
}

static char *ignore_opts[] = {"ignore", "direct", "indirect", "dev", NULL};

/*
 * Compare mount options
 * ignoring "ignore", "direct", "indirect"
 * and "dev=".
 */
static int
compare_opts(opts, mntopts)
	char *opts, *mntopts;
{
	char optbuff[1000], *bp = optbuff;
	char *p, *q;
	int c, i, found;

	(void) strcpy(bp, mntopts);

	while (*bp) {

		if ((p = strchr(bp, ',')) == NULL)
			p = bp + strlen(bp);
		else
			p++;

		q = bp + strcspn(bp, ",=");
		c = *q;
		*q = '\0';
		found = 0;
		for (i = 0; ignore_opts[i]; i++) {
			if (strcmp(ignore_opts[i], bp) == 0) {
				found++;
				break;
			}
		}
		if (found) {
			(void) strcpy(bp, p);
		} else {
			*q = c;
			bp = p;
		}
	}

	p = optbuff + (strlen(optbuff) - 1);
	if (*p == ',')
		*p = '\0';

	return (strcmp(opts, optbuff));
}

static void
usage()
{
	pr_msg("Usage: automount  [ -v ]  [ -t duration ]");
	exit(1);
	/* NOTREACHED */
}

/*
 * Unmount any autofs mounts that
 * aren't in the master map
 */
static void
do_unmounts()
{
	struct mntlist *mntl;
	struct mnttab *mnt;
	extern struct autodir *dir_head;
	struct autodir *dir;
	extern int verbose;
	int current;
	int count = 0;

	for (mntl = current_mounts; mntl; mntl = mntl->mntl_next) {
		mnt = mntl->mntl_mnt;
		if (strcmp(mnt->mnt_fstype, MNTTYPE_AUTOFS) != 0)
			continue;

		/*
		 * Don't unmount autofs mounts done
		 * from the autofs mount command.
		 * How do we tell them apart ?
		 * Autofs mounts not eligible for auto-unmount
		 * have the "nest" pseudo-option.
		 */
		if (hasmntopt(mnt, "nest") != NULL)
			continue;

		current = 0;
		for (dir = dir_head; dir; dir = dir->dir_next) {
			if (strcmp(dir->dir_name, mnt->mnt_mountp) == 0) {
				current = strcmp(dir->dir_map, "-null");
				break;
			}
		}
		if (current)
			continue;


		if (umount(mnt->mnt_mountp) == 0) {
			del_mnttab(mnt->mnt_mountp);
			if (verbose) {
				pr_msg("%s unmounted",
					mnt->mnt_mountp);
			}
			count++;
		}
	}
	if (verbose && count == 0)
		pr_msg("no unmounts");
}

/*
 * Print an error.
 * Works like printf (fmt string and variable args)
 * except that it will subsititute an error message
 * for a "%m" string (like syslog).
 */
/* VARARGS1 */
void
pr_msg(fmt, va_alist)
	const char *fmt;
	va_dcl
{
	va_list ap;
	char buf[BUFSIZ], *p2;
	char *p1;
	char *nfmt;
	extern int errno, sys_nerr;
	extern char *sys_errlist[];

	(void) strcpy(buf, "automount: ");
	p2 = buf + strlen(buf);

	nfmt = gettext(fmt);

	for (p1 = nfmt; *p1; p1++) {
		if (*p1 == '%' && *(p1+1) == 'm') {
			if (errno < sys_nerr) {
				(void) strcpy(p2, sys_errlist[errno]);
				p2 += strlen(p2);
			}
			p1++;
		} else {
			*p2++ = *p1;
		}
	}
	if (p2 > buf && *(p2-1) != '\n')
		*p2++ = '\n';
	*p2 = '\0';

	va_start(ap);
	(void) vfprintf(stderr, buf, ap);
	va_end(ap);
}
