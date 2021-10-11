/*
 * Copyright (c) 1988-1993, by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)drvconfig.c	1.30	95/04/07 SMI"

#include <sys/types.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/modctl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/dditypes.h>
#include <sys/hwconf.h>
#include <sys/stat.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/instance.h>
#include <errno.h>
#include <sys/t_lock.h>
#include <libgen.h>
#include <ftw.h>

static struct modconfig mc;

static int add_bind;
static int noload_flag = 0;

static struct mperm *mphead;

#define	PERMFILE "/etc/minor_perm"
#define	ALIASFILE "/etc/driver_aliases"

static char *permfile = PERMFILE;
static char *alias_file = ALIASFILE;
static char *instance_file = INSTANCE_FILE;

#define	DEFAULT_USER	"root"
#define	DEFAULT_GROUP	"sys"

#define	ROOTDIR "/devices"

#define	FT_DEPTH 15 /* device tree depth for nftw() */

#define	NODE_PREPEND ".."
#define	NODE_PREPEND_LEN 2

static uid_t root_uid;
static gid_t sys_gid;

static struct aliases *a_head, *a_tail;

static void usage();
static void do_perm(char *, struct mperm *);
static void change_perm(char *, char *);
static int alias(char *, char *);
static int getvalue(char *token, int *valuep);
static int getnexttoken(char *next, char **nextp, char **tokenpp, char *tchar);
static int read_perm_file(void);
static int instance_sync(char *pgm, char *filename, int flags);
static int create_device_nodes();
static int check_node(const char *, const struct stat *,
	int, struct FTW *);
static char *dequote(char *src);

/*
 * System call prototypes
 */
int modctl(int, ...);

/*
 * Config the system
 */
main(int argc, char *argv[])
{
	int opt;
	char modname[256];
	struct passwd *pw;
	struct group *gp;
	int num_aliases = 0;
	struct aliases *ap = NULL;
	int len;
	int retval;
	int iflg = 0;

	mc.major = -1;

	/* the default user is root */

	if ((pw = getpwnam(DEFAULT_USER)) != NULL)
		root_uid = pw->pw_uid;
	else {
		(void) fprintf(stderr,
		    "%s: name service can't find user '%s'?\n",
		    argv[0], DEFAULT_USER);
		root_uid = (uid_t)0;	/* XXX root */
	}

	/* the default group is sys */

	if ((gp = getgrnam(DEFAULT_GROUP)) != NULL) {
		sys_gid = gp->gr_gid;
		(void) setgid(gp->gr_gid);
	} else {
		(void) fprintf(stderr,
		    "%s: name service can't find group '%s'?\n",
		    argv[0], DEFAULT_GROUP);
		sys_gid = (gid_t)3;	/* XXX sys */
	}

	(void) memset(modname, 0, 256);
	(void) strcpy(mc.rootdir, ROOTDIR);
	while ((opt = getopt(argc, argv, "a:bc:dm:np:r:i:")) != -1) {
		switch (opt) {
		case 'a':
			ap = calloc(sizeof (struct aliases), 1);
			ap->a_name = dequote(optarg);
			if (ap->a_name == NULL) {
				(void) fprintf(stderr, "drvconfig: not enough "
				    "memory for alias name\n");
				exit(1);
			}
			len = strlen(ap->a_name) + 1;
			if (len > 256) {
				(void) fprintf(stderr,
				    "drvconfig: alias name '%s' too long\n",
				    ap->a_name);
				exit(1);
			}
			ap->a_len = len;
			if (a_tail == NULL)
				a_head = ap;
			else
				a_tail->a_next = ap;
			a_tail = ap;
			num_aliases++;
			break;
		case 'b':
			add_bind++;
			break;
		case 'c':
			(void) strcpy(mc.drvclass, optarg);
			break;
		case 'd':
			mc.debugflag++;
			break;
		case 'm':
			mc.major = atoi(optarg);
			break;
		case 'n':
			noload_flag++;
			break;
		case 'p':
			instance_file = optarg;
			break;
		case 'r':
			(void) strcpy(mc.rootdir, optarg);
			break;
		case 'i':
			iflg++;
			(void) strcpy(mc.drvname, optarg);
			break;
		case '?':
			usage();
			exit(2);
		}
	}
	if (add_bind && (mc.major == -1 || mc.drvname[0] == NULL)) {
		(void) fprintf(stderr, "drvconfig: Must have major number "
		    "and driver name when using the -b flag\n");
		exit(1);
	}
	if (add_bind) {
		mc.num_aliases = num_aliases;
		mc.ap = a_head;
		retval =  modctl(MODADDMAJBIND, NULL, (caddr_t)&mc);
		if (retval < 0)
			perror(
			"Drvconfig: System call 'modctl_modconfig' failed");
		exit(retval);
	}
	if (iflg && mc.drvname[0] == NULL) {
		(void) fprintf(stderr,
		    "drvconfig: No name given with -i flag\n");
		exit(1);
	}

	(void) signal(SIGINT, SIG_IGN);
	(void) signal(SIGTERM, SIG_IGN);

	/*
	 * call into kernel to actually build device tree
	 * device files created by this call, will have a '..'
	 * prepended to the name.  This is used by create_device_nodes()
	 * to figure out which files were just created
	 * by modctl.  The files are renamed below.
	 */
	if (!noload_flag) {
		if (modctl(MODCONFIG, NULL, (caddr_t)&mc) < 0) {
			perror("drvconfig: System call 'modctl_modconfig' "
								"failed");
			exit(1);
		}
		if (instance_sync("drvconfig", instance_file, 0) == -1) {
			exit(1);
		}
		/*
		 * Move the ..<nodename> entries to <nodename> and
		 * make sure the new files have the correct permissions.
		 */
		if (create_device_nodes() == -1)
			exit(1);

	} /* if (!noload_flag) */

	(void) signal(SIGINT, SIG_DFL);
	(void) signal(SIGTERM, SIG_DFL);

	exit(0);
}

/*
 * check_node() is called by nftw().  Node contains the current
 * device node (full pathname).  If the node is a valid device file,
 * and node was just created by modctl(MODCONFIG),
 * check_node does two things:
 *   1. rename /devices/<stuff>/..<devicename>
 *   2. call change_perm() to see if the permissions or ownership
 *        need to be changed.
 */
static int
check_node(const char *node, const struct stat *node_stat, int flags,
	struct FTW *ftw_info)
{
	char *i;
	static char *devname = NULL;

	if (devname == NULL) {
		devname = (char *)malloc(256 + NODE_PREPEND_LEN);
		if (devname == NULL) {
			perror("drvconfig");
			return (-1);
		}
	}
	if ((flags == FTW_F) && ((node_stat->st_mode & S_IFCHR) ||
	    (node_stat->st_mode & S_IFBLK))) {
		if (strncmp(NODE_PREPEND, node + ftw_info->base,
		    NODE_PREPEND_LEN) == 0) {
			strcpy(devname, node);
			i = devname + ftw_info->base;

			sprintf(i, "%s", node + ftw_info->base +
				NODE_PREPEND_LEN);

			if (rename(node, devname) == -1)
				fprintf(stderr, "drvconfig:"
				    "rename of %s to %s failed.",
				    node, devname);
			else
				change_perm(mc.drvname, devname);
		}
	}
	return (0);
}

/*
 * create_device_nodes() verifies whether any of the device nodes
 * just created need to have their permissions fixed up as specified in
 * /etc/minor_perm.
 *
 * nftw() is used to walk through the device tree.  For each node,
 * check_node will be called.
 */
static int
create_device_nodes()
{
	int  walk_flags = FTW_PHYS | FTW_MOUNT;

	/*
	 * Read /etc/minor_perm
	 */
	if (read_perm_file() == -1) {
		fprintf(stderr, "read of permissions file failed\n");
		return (-1);
	}

	if (nftw(mc.rootdir, check_node, FT_DEPTH, walk_flags)
		== -1) {
			perror("drvconfig: nftw");
			return (-1);
	} else
		return (0);
}

static void
change_perm(char *drvname, char *devname)
{
	char devname1[256];
	char *p;
	char *q;
	int p_is_clone;
	int drvname_matches_p;
	int drvname_matches_q;
	int drvname_is_alias_of_p;
	int mp_drvname_matches_p;
	int mp_drvname_matches_q;
	int mp_drvname_is_clone;
	int mp_drvname_matches_drvname;
	struct mperm *mp;

	(void) strcpy(devname1, devname);
	p = strrchr(devname, '/');	/* node name is the last */
							/* component */
	if (p == NULL)
		return;
	q = strchr(++p, '@');		/* see if it has address part */
	if (q != NULL)
		*q++ = '\0';
	else
		q = p;
	q = strchr(q, ':');		/* look for minor name */
	if (q == NULL) {
		(void) fprintf(stderr, "no minor for %s\n", p);
		return;
	}
	*q++ = '\0';
	/*
	 * Now go through list of permissions and see if we have found
	 * a permissions entry for this device.  If we were passed a
	 * driver name then only do that driver else do all devices in
	 * the tree that have entries in the permissions file.
	 */

	/*
	 * p			= device name of /device entry
	 * q			= minor part of /device entry
	 * drvname		= device name (-i option of drvconfig)
	 * mp->mp_drvname	= device name from minor_perm
	 * mp->mp_minorname	= minor part of device name from
	 *				minor_perm
	 */
	p_is_clone = (strcmp(p, "clone") == 0);
	drvname_matches_p = (strcmp(drvname, p) == 0);
	drvname_matches_q = (strcmp(drvname, q) == 0);
	drvname_is_alias_of_p = alias(drvname, p);
	for (mp = mphead; mp != NULL; mp = mp->mp_next) {
		mp_drvname_matches_p =
		    (strcmp(mp->mp_drvname, p) == 0);
		mp_drvname_matches_q =
		    (strcmp(mp->mp_drvname, q) == 0);
		mp_drvname_is_clone =
		    (strcmp(mp->mp_drvname, "clone") == 0);
		mp_drvname_matches_drvname =
		    (strcmp(mp->mp_drvname, drvname) == 0);

		/*
		 * If one of the following 4 cases is true, then we
		 * try to change the permisions if a "shell global
		 * pattern match" of the minor perm minor entry
		 * matches q.
		 *
		 * No driver name passed in and
		 * minor_perm entry matches /devices entry
		 * driver name.
		 *
		 * or
		 *
		 * No driver name passed in and
		 * /devices entry is the clone device
		 * and minor_perm entry is the clone
		 * device or matches the minor part of
		 * the clone device.
		 *
		 * or
		 *
		 * driver name passed in and minor_perm entry
		 * match and current /device entry matches driver
		 * name or is an alias of drvier name.
		 *
		 * or
		 *
		 * /devices entry is the clone device,
		 * driver name passed in matches minor part
		 * of the clone device and minor_perm entry
		 * is the clone device or matches the minor
		 * protion of the clone device name.
		 */
		if ((drvname[0] == '\0' &&
		    (mp_drvname_matches_p ||
			(p_is_clone &&
			    (mp_drvname_is_clone ||
			    mp_drvname_matches_q)))) ||

		    (mp_drvname_matches_drvname &&
			(drvname_matches_p || drvname_is_alias_of_p)) ||

		    ((p_is_clone && drvname_matches_q) &&
		    (mp_drvname_is_clone || mp_drvname_matches_q))) {

			/*
			 * Check that the minor part of the
			 * device name from the minor_perm
			 * entry matches and if so, set the
			 * permissions.
			 */
			if (gmatch(q,
			    mp->mp_minorname))
				(void) do_perm(devname1, mp);
		}
	}

}

static void
do_perm(char *devname, struct mperm *mp)
{
	struct stat statbuf;

	if (stat(devname, &statbuf) == 0) {
		if (mp->mp_perm != (statbuf.st_mode & S_IAMB))
			if (chmod(devname, mp->mp_perm) == -1)
				perror(devname);

			if (mp->mp_uid != statbuf.st_uid ||
			    mp->mp_gid != statbuf.st_gid)
				if (chown(devname, mp->mp_uid,
					mp->mp_gid) == -1)
						perror(devname);
	} else
		perror(devname);
}



static int
read_perm_file(void)
{
	FILE *pfd;
	struct mperm *mp;
	char line[256];
	char *cp;
	char *p;
	char t;
	struct mperm *mptail = mphead;
	struct passwd *pw;
	struct group *gp;

	if ((pfd = fopen(permfile, "r")) == NULL) {
		(void) fprintf(stderr, "drvconfig: %s file not found\n",
		    permfile);
		return (-1);
	}
	while (fgets(line, 255, pfd) != NULL) {
		mp = (struct mperm *)calloc(sizeof (struct mperm), 1);
		if (mp == NULL) {
			(void) fprintf(stderr,
			    "drvconfig: not enough memory "
			    "for permission file\n");
			return (-1);
		}
		cp = line;
		(void) getnexttoken(cp, &cp, &p, &t);
		mp->mp_drvname = calloc(strlen(p) + 1, 1);
		if (mp->mp_drvname == NULL) {
			(void) fprintf(stderr,
			    "drvconfig: not enough memory "
			    "for permission file\n");
			return (-1);
		}
		(void) strcpy(mp->mp_drvname, p);
		if (t == '\n' || t == '\0') {
			(void) fprintf(stderr, "drvconfig: no permission "
			    "in permissions file\n");
			continue;
		}
		if (t == ':') {
			(void) getnexttoken(cp, &cp, &p, &t);
			mp->mp_minorname = calloc(strlen(p) +1, 1);
			if (mp->mp_minorname == NULL) {
				(void) fprintf(stderr, "drvconfig: not enough "
				    "memory for permission file\n");
				return (-1);
			}
			(void) strcpy(mp->mp_minorname, p);
		}
		if (t == '\n' || t == '\0') {
			(void) fprintf(stderr, "drvconfig: no permission "
			    "in permissions file\n");
			continue;
		}
		(void) getnexttoken(cp, &cp, &p, &t);
		(void) getvalue(p, &mp->mp_perm);
		if (t == '\n' || t == '\0') {	/* no owner or group */
			goto link;
		}
		(void) getnexttoken(cp, &cp, &p, &t);
		mp->mp_owner = calloc(strlen(p) + 1, 1);
		if (mp->mp_owner == NULL) {
			(void) fprintf(stderr, "drvconfig: not enough "
			    "memory for permission file\n");
			return (-1);
		}
		(void) strcpy(mp->mp_owner, p);
		if (t == '\n' || t == '\0') {	/* no group */
			goto link;
		}
		(void) getnexttoken(cp, &cp, &p, 0);
		mp->mp_group = calloc(strlen(p) + 1, 1);
		if (mp->mp_group == NULL) {
			(void) fprintf(stderr, "drvconfig: not enough "
			    "memory for permission file\n");
			return (-1);
		}
		(void) strcpy(mp->mp_group, p);
link:
		if (mphead == NULL)
			mphead = mp;
		else
			mptail->mp_next = mp;
		mptail = mp;

		/*
		 * Compute the uid's and gid's here - there are
		 * fewer lines in the /etc/minor_perm file than there
		 * are devices to be stat(2)ed.  And almost every
		 * device is 'root sys'.  See 1135520.
		 */
		if (mp->mp_owner == NULL ||
		    strcmp(mp->mp_owner, DEFAULT_USER) == 0 ||
		    (pw = getpwnam(mp->mp_owner)) == NULL)
			mp->mp_uid = root_uid;
		else
			mp->mp_uid = pw->pw_uid;

		if (mp->mp_group == NULL ||
		    strcmp(mp->mp_group, DEFAULT_GROUP) == 0 ||
		    (gp = getgrnam(mp->mp_group)) == NULL)
			mp->mp_gid = sys_gid;
		else
			mp->mp_gid = gp->gr_gid;
	}

	(void) fclose(pfd);

	return (0);
}


/*
 * Tokens are separated by ' ', '\t', ':', '=', '&', '|', ';', '\n', or '\0'
 *
 * Returns 0 if token found, 1 otherwise.
 */
static int
getnexttoken(char *next, char **nextp, char **tokenpp, char *tchar)
{
	register char *cp;
	register char *cp1;
	char *tokenp;

	cp = next;
	while (*cp == ' ' || *cp == '\t')
		cp++;			/* skip leading spaces */
	tokenp = cp;			/* start of token */
	while (*cp != '\0' && *cp != '\n' && *cp != ' ' && *cp != '\t' &&
	    *cp != ':' && *cp != '=' && *cp != '&' && *cp != '|' && *cp != ';')
		cp++;			/* point to next character */
	/*
	 * If terminating character is a space or tab, look ahead to see if
	 * there's another terminator that's not a space or tab.
	 * (This code handles trailing spaces.)
	 */
	if (*cp == ' ' || *cp == '\t') {
		cp1 = cp;
		while (*++cp1 == ' ' || *cp1 == '\t')
			;
		if (*cp1 == '=' || *cp1 == ':' || *cp1 == '&' || *cp1 == '|' ||
		    *cp1 == ';' || *cp1 == '\n' || *cp1 == '\0') {
			*cp = NULL;	/* terminate token */
			cp = cp1;
		}
	}
	if (tchar != NULL) {
		*tchar = *cp;		/* save terminating character */
		if (*tchar == '\0')
			*tchar = '\n';
	}
	*cp++ = '\0';			/* terminate token, point to next */
	*nextp = cp;			/* set pointer to next character */
	if (cp - tokenp - 1 == 0)
		return (1);
	*tokenpp = tokenp;
	return (0);
}

/*
 * get a decimal octal or hex number. Handle '~' for one's complement.
 */
static int
getvalue(char *token, int *valuep)
{
	int radix;
	int retval = 0;
	int onescompl = 0;
	int negate = 0;
	char c;

	if (*token == '~') {
		onescompl++; /* perform one's complement on result */
		token++;
	} else if (*token == '-') {
		negate++;
		token++;
	}
	if (*token == '0') {
		token++;
		c = *token;

		if (c == '\0') {
			*valuep = 0;	/* value is 0 */
			return (0);
		}

		if (c == 'x' || c == 'X') {
			radix = 16;
			token++;
		} else
			radix = 8;
	} else
		radix = 10;

	while ((c = *token++)) {
		switch (radix) {
		case 8:
			if (c >= '0' && c <= '7')
				c -= '0';
			else
				return (-1);	/* invalid number */
			retval = (retval << 3) + c;
			break;
		case 10:
			if (c >= '0' && c <= '9')
				c -= '0';
			else
				return (-1);	/* invalid number */
			retval = (retval * 10) + c;
			break;
		case 16:
			if (c >= 'a' && c <= 'f')
				c = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				c = c - 'A' + 10;
			else if (c >= '0' && c <= '9')
				c -= '0';
			else
				return (-1);	/* invalid number */
			retval = (retval << 4) + c;
			break;
		}
	}
	if (onescompl)
		retval = ~retval;
	if (negate)
		retval = -retval;
	*valuep = retval;
	return (0);
}

static int
alias(char *drvname, char *name)
{
	static int read_alias;
	FILE *afd;
	char line[256];
	char *cp;
	char *p;
	char t;
	struct aliases *ap;

	if (read_alias == 0) {

		if ((afd = fopen(alias_file, "r")) == NULL) {
			(void) fprintf(stderr, "drvconfig: %s file not found\n",
			    alias_file);
			return (-1);
		}
		while (fgets(line, 255, afd) != NULL) {
			cp = line;
			(void) getnexttoken(cp, &cp, &p, &t);
			if (strcmp(p, drvname) != 0)
				continue;
			if (t == '\n' || t == '\0') {
				(void) fprintf(stderr, "drvconfig: driver name "
				    "with no alias in alias file\n");
				continue;
			}
			(void) getnexttoken(cp, &cp, &p, &t);
			if (*p == '"') {
				if (p[strlen(p) - 1] == '"') {
					p[strlen(p) - 1] = '\0';
					p++;
				}
			}
			if ((ap = (struct aliases *)
			    calloc(sizeof (*ap), 1)) == NULL) {
				(void) fprintf(stderr, "drvconfig: not enough "
				    "memory for alias structure\n");
				return (-1);
			}
			if ((ap->a_name = (char *)
			    calloc(strlen(p) + 1, 1)) == NULL) {
				(void) fprintf(stderr, "drvconfig: not enough "
				    "memory for alias name\n");
				return (-1);
			}
			(void) strcpy(ap->a_name, p);
			if (a_head == NULL)
				a_head = ap;
			else
				a_tail->a_next = ap;
			a_tail = ap;
		}
		(void) fclose(afd);
		read_alias = 1;
	}
	for (ap = a_head; ap != NULL; ap = ap->a_next) {
		if (ap->a_name != NULL) {
			if (strcmp(ap->a_name, name) == 0)
				return (1);
		}
	}
	return (0);
}

static void
usage()
{
	(void) fprintf(stderr, "\nusage: drvconfig [-a alias_name]\n");
	(void) fprintf(stderr, "                 [-b]\n");
	(void) fprintf(stderr, "                 [-c class_name]\n");
	(void) fprintf(stderr, "                 [-i driver_name]\n");
	(void) fprintf(stderr, "                 [-m major_number]\n");
	(void) fprintf(stderr, "                 [-n]\n");
	(void) fprintf(stderr, "                 [-r rootdir]\n");
}

/*
 * This routine is used to write out the kernels instance number
 * data using the 'inst_sync' syscall.  It also keeps a backup
 * copy of the old file, in case of accidents.
 *
 *	- writes out the current instance number assignments
 *	  to 'filename'.$PID
 *	- copies the current 'filename' to 'filename'.old.$PID
 *	- renames 'filename'.old.$PID to 'filename'.old
 *	- renames 'filename'.$PID to 'filename'
 *
 * 'filename' is usually /etc/path_to_inst.
 */


/*
 * Call the loadable syscall.  This probably errs on the
 * side of being over-robust ..
 */
static int
do_syscall(char *pgm, char *filename, int flags)
{
	register void (*sigsaved)(int);
	register int err;

	sigsaved = signal(SIGSYS, SIG_IGN);
	if (inst_sync(filename, flags) == -1) {
		err = errno;
	} else
		err = 0;
	(void) signal(SIGSYS, sigsaved);

	switch (err) {

	case ENOSYS:
		(void) fprintf(stderr, "%s: Can't load system call\n", pgm);
		break;
		/*NOTREACHED*/

	case EPERM:
		(void) fprintf(stderr,
		    "%s: You must be superuser to sync instance numbers\n",
		    pgm);
		break;

	default:
		(void) fprintf(stderr, "%s: %s: %s\n", pgm, filename,
		    strerror(err));
		break;

	case 0:
		/*
		 * Success!
		 */
		return (0);
	}

	return (-1);
}

static int
instance_sync(char *pgm, char *filename, int flags)
{
	register char *newtmp = (char *)0;
	register char *oldtmp = (char *)0;
	register char *filename_old = (char *)0;
	register FILE *fp = (FILE *)0;
	register FILE *tmp_fp = (FILE *)0;
	register int c;
	register int err;

	newtmp = malloc(strlen(filename) + 1 + 6);
	(void) sprintf(newtmp, "%s.%d", filename, getpid());
	(void) unlink(newtmp);

	if ((err = do_syscall(pgm, newtmp, flags)) == -1) {
		goto out;
		/*NOTREACHED*/
	}

	/*
	 * Phew, it worked.
	 *
	 * Now we deal with the somewhat tricky updating and renaming
	 * of this critical piece of kernel state.
	 */

	/*
	 * Create a temporary file to contain a backup copy
	 * of 'filename'.  Of course if 'filename' doesn't exist,
	 * there's much less for us to do .. tee hee.
	 */
	if ((fp = fopen(filename, "r")) == (FILE *)0) {
		/*
		 * No such file.  Rename the new onto the old
		 */
		if ((err = rename(newtmp, filename)) != 0)
			(void) fprintf(stderr, "%s: '%s' - %s\n",
			    pgm, filename, strerror(errno));
		goto out;
		/*NOTREACHED*/
	}

	oldtmp = malloc(strlen(filename) + 1 + 4 + 6);
	(void) sprintf(oldtmp, "%s.old.%d", filename, getpid());
	(void) unlink(oldtmp);

	if ((tmp_fp = fopen(oldtmp, "w")) == (FILE *)0) {
		/*
		 * Can't open the 'oldtmp' file for writing.
		 * This is somewhat strange given that the syscall
		 * just succeeded to write a file out.. hmm.. maybe
		 * the fs just filled up or something nasty.
		 *
		 * Anyway, abort what we've done so far.
		 */
		(void) fprintf(stderr, "%s: can't update '%s'\n",
		    pgm, oldtmp);
		err = -1;
		goto out;
		/*NOTREACHED*/
	}

	/*
	 * Copy current instance file into the temporary file
	 */
	while ((c = getc(fp)) != EOF)
		if ((err = putc(c, tmp_fp)) == EOF)
			break;

	if (fclose(tmp_fp) == EOF || err == EOF) {
		(void) fprintf(stderr, "%s: can't update '%s'\n",
		    pgm, oldtmp);
		err = -1;
		goto out;
		/*NOTREACHED*/
	}

	/*
	 * Set permissions to be the same on the backup as
	 * /etc/path_to_inst.
	 */
	(void) chmod(oldtmp, 0444);

	/*
	 * So far, everything we've done is more or less reversible.
	 * But now we're going to commit ourselves.
	 *
	 * Fingers crossed.
	 */

	filename_old = malloc(strlen(filename) + 1 + 4);
	(void) sprintf(filename_old, "%s.old", filename);

	if ((err = rename(oldtmp, filename_old)) != 0) {

		(void) fprintf(stderr, "%s: '%s' - %s\n",
		    pgm, filename_old, strerror(errno));

	} else if ((err = rename(newtmp, filename)) != 0) {

		(void) fprintf(stderr, "%s: '%s' - %s\n",
		    pgm, filename, strerror(errno));
		(void) fprintf(stderr, "%s: Warning: '%s' was updated.\n",
		    pgm, filename);
	}

out:
	if (fp)
		(void) fclose(fp);

	if (newtmp) {
		(void) unlink(newtmp);
		free(newtmp);
	}

	if (oldtmp) {
		(void) unlink(oldtmp);
		free(oldtmp);
	}

	if (filename_old)
		free(filename_old);

	if (err != 0)
		(void) fprintf(stderr,
		    "%s: Warning: failed to update '%s'\n", pgm, filename);

	return (err);
}

static char *
dequote(char *src)
{
	char	*dst;
	int	len;

	len = strlen(src);
	dst = malloc(len + 1);
	if (dst == NULL)
		return (NULL);
	if (src[0] == '\"' && src[len - 1] == '\"') {
		len -= 2;
		(void) strncpy(dst, &src[1], len);
		dst[len] = '\0';
	} else {
		(void) strcpy(dst, src);
	}
	return (dst);
}
