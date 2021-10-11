/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#ident	"@(#)setmnt.c	1.6	94/02/18 SMI"	/* SVr4.0 4.13	*/

#include	<stdio.h>
#include	<string.h>
#include	<unistd.h>
#include	<sys/mnttab.h>
#include	<sys/mntent.h>
#include	<sys/vfstab.h>
#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/statvfs.h>

#define	LINESIZ		BUFSIZ
#define	OPTSIZ		1024

/*
 * MNTTAB is usually defined in <sys/mnttab.h>
 */
#ifndef MNTTAB
#define	MNTTAB		/etc/mnttab
#endif

/*
 * VFSTAB is usually defined in <sys/vfstab.h>
 */
#ifndef VFSTAB
#define	VFSTAB		/etc/vfstab
#endif

static char	*opts(char *, char *, char *, u_long);
extern time_t	time(time_t *);

static char	line[LINESIZ];
static char	sepstr[] = " \t\n";

static char	mnttab[] = MNTTAB;


/* ARGSUSED */
int
main(int argc, char **argv)
{
	char	*lp;
	char	*myname;
	time_t	date;
	FILE	*fp;

	myname = strrchr(argv[0], '/');
	if (myname)
		myname++;
	else
		myname = argv[0];

	(void) umask(~(S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH) & S_IAMB);

	if ((fp = fopen(mnttab, "w")) == NULL) {
		(void) fprintf(stderr,
			"%s: Cannot open %s for writing\n", myname, mnttab);
		exit(1);
	}

	(void) time(&date);
	while ((lp = fgets(line, LINESIZ, stdin)) != NULL) {
		struct mnttab	mm;
		struct statvfs	sbuf;

		/*
		 * We expect each input line to have the format:
		 *	special_device mount_point
		 */
		if ((mm.mnt_special = strtok(lp, sepstr)) == NULL ||
		    (mm.mnt_mountp = strtok(NULL, sepstr)) == NULL)
			continue;

		if (statvfs(mm.mnt_mountp, &sbuf) == -1)
			continue;

		(void) fprintf(fp, "%s\t%s\t%s\t%s\t%ld\n",
			mm.mnt_special,
			mm.mnt_mountp,
			sbuf.f_basetype ? sbuf.f_basetype : "-",
			opts(mm.mnt_special, mm.mnt_mountp,
				sbuf.f_basetype, sbuf.f_flag),
			date);
	}
	(void) fclose(fp);

	exit(0);
	/* NOTREACHED */
}


/*
 * Determine the proper options to use in the 'options' field of
 * the mnttab entry.
 * 'flag' holds the options known to the kernel (obtained from statvfs(2))
 */
static char *
opts(char *special_device, char *mount_point, char *fstype, u_long flag)
{
	static char	mntopts[OPTSIZ];
	static char	*valid_options[] =
	{
		/*
		 * If we find any of the following mount options in VFSTAB,
		 * we will include it in the generated MNTTAB entry.
		 * Options are listed in alphanumeric order.
		 */
		MNTOPT_ACREGMIN,
		MNTOPT_ACREGMIN,
		MNTOPT_ACREGMAX,
		MNTOPT_ACDIRMIN,
		MNTOPT_ACDIRMAX,
		MNTOPT_ACTIMEO,
		MNTOPT_BG,
		MNTOPT_FG,
		MNTOPT_HARD,
		MNTOPT_GRPID,
		MNTOPT_QUOTA,	MNTOPT_NOQUOTA,
		MNTOPT_IGNORE,
		MNTOPT_INTR,	MNTOPT_NOINTR,
		MNTOPT_KERB,
		MNTOPT_LLOCK,
		MNTOPT_MULTI,
		MNTOPT_NOSUB,
		MNTOPT_NOAC,
		MNTOPT_NOCTO,
		MNTOPT_PORT,
		MNTOPT_POSIX,
		MNTOPT_RETRY,
		MNTOPT_RQ,
		MNTOPT_SECURE,
		MNTOPT_SOFT,
		MNTOPT_TIMEO,
		NULL
	};
	static FILE	*vfstab_fp;
	struct vfstab	vfs_search;
	struct vfstab	vfs_found;
	struct stat	st;
	int		cc;
	int		len = 0;
	char		*opt;

	/*
	 * If this is the first call, open the file
	 */
	if (vfstab_fp == NULL)
		vfstab_fp = fopen(VFSTAB, "r");
	else
		rewind(vfstab_fp);

	cc = sprintf(mntopts, "%s,%s",
		(flag & ST_RDONLY) ? MNTOPT_RO : MNTOPT_RW,
		(flag & ST_NOSUID) ? MNTOPT_NOSUID : MNTOPT_SUID);
	len += cc;

	if (stat(mount_point, &st) != -1) {
		cc = sprintf(&mntopts[ len ], ",dev=%lx", st.st_dev);
		len += cc;
	}

	if (vfstab_fp == NULL)
		return (mntopts);

	/*
	 * Find the vfstab options that apply to this file system
	 */
	vfsnull(&vfs_search);
	vfs_search.vfs_special = special_device;
	vfs_search.vfs_mountp = mount_point;
	vfs_search.vfs_fstype = fstype;
	if (getvfsany(vfstab_fp, &vfs_found, &vfs_search) != 0)
		return (mntopts);

	/*
	 * If no options are specified, we are done
	 */
	if (vfs_found.vfs_mntopts == NULL)
		return (mntopts);

	for (opt = vfs_found.vfs_mntopts; ; ) {
		char	**pp;
		int	opt_name_len	= strcspn(opt, "=,");
		char	*next_opt	= strchr(opt, ',');

		for (pp = valid_options; *pp; pp++) {
			int opt_len;

			if (strncmp(*pp, opt, opt_name_len) != 0)
				continue;

			if (strncmp(opt, MNTOPT_RQ, opt_name_len) == 0) {
				/*
				 * We handle the 'rq' option differently,
				 * because we have already set the ro/rw
				 * option. Therefore, we add the 'quota'
				 * option if appropriate.
				 */
				if (! (flag & ST_RDONLY)) {
					cc = sprintf(&mntopts[ len ], ",quota");
					len += cc;
				}
				continue;
			}

			if (next_opt == NULL)
				opt_len = strlen(opt);
			else
				opt_len = next_opt - opt;

			cc = sprintf(&mntopts[ len ], ",%.*s", opt_len, opt);
			len += cc;
		}

		if (next_opt == NULL)
			break;

		opt = next_opt + 1;	/* skip the comma */
	}

	return (mntopts);
}
