/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)fusage.c	1.6	93/12/20 SMI"	/* SVr4.0 1.15.9.1	*/

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/mnttab.h>
#include <sys/utsname.h>
#include <ctype.h>
#include <sys/param.h>
#include <sys/fcntl.h>
#include <nlist.h>
#include <kvm.h>
#include <sys/vfs.h>
#include <errno.h>

#define	ROOTVFS		"rootvfs"
#define ADVTAB		"/etc/dfs/sharetab"
#define	REMOTE		"/etc/dfs/fstypes"
#define	FSTYPE_MAX	8
#define	REMOTE_MAX	64

#ifndef SZ_PATH
#define SZ_PATH		128
#endif

struct	nlist	nl[]={
	{ROOTVFS},
	{""}
};

char 		*malloc();
extern 	int 	errno;
extern	char	*sys_errlist[];

main(argc, argv)
	int	argc;
	char	*argv[];
{
	char	*cmdname;
	long	fs_blksize;
	FILE 	*atab;
	FILE	*mtab;
	int	clientsum;
	int	local;
	int	fswant;
	int	ii;
	int	jj;
	int	exitcode = 0;
	struct client 	*clientp;
	struct statfs	fsi;
	struct utsname	myname;
	struct mnttab 	mnttab;

	long	getcount();
	void	prdat();
	
	cmdname = argv[0];		/* get command name from argv list */
	for (ii = 0; ii < argc; ii++) {
		if (argv[ii][0] == '-') {
			fprintf(stderr, "Usage: %s [mounted file system]\n",
			  argv[0]);
			fprintf(stderr,
			  "          [mounted block special device]\n");
			exit(1);
		}
	}

	uname(&myname);
	printf("\nFILE USAGE REPORT FOR %.8s\n\n", myname.nodename);
	if ((mtab = fopen(MNTTAB, "r")) == NULL) {
		fprintf(stderr,"fusage: cannot open %s", MNTTAB);
		perror("open");
		exit(1);
	}
	
	/* 
	 * Process each entry in the mnttab.  If the entry matches a requested
	 * name, or all are being reported (default), print its data.
	 */
	while (getmntent(mtab, &mnttab) != -1) {
		if (mnttab.mnt_special == NULL || mnttab.mnt_mountp == NULL)
			continue;
		if (remote(mnttab.mnt_fstype))
			continue;
		if (shouldprint(argc, argv, mnttab.mnt_special,
		  mnttab.mnt_mountp)) {
			printf("\n\t%-15s      %s\n", mnttab.mnt_special, 
			  mnttab.mnt_special);
			fswant = 1;
		} else {
			fswant = 0;
		}
		if (statfs(mnttab.mnt_mountp, &fsi, sizeof(struct statfs), 0)
		  < 0) {
			fs_blksize = 1024;  /* per file system basis */
			printf("forced fs_blksize\n");
		} else {
			fs_blksize = fsi.f_bsize;
		}

		if (fswant) {
			printf("\n\t%15s      %s\n", "", mnttab.mnt_mountp);
			if ((local = getcount(mnttab.mnt_mountp, cmdname))
			  != -1) {
				prdat(myname.nodename, local, fs_blksize);
			}
		}
	}
	for (ii = 1; ii < argc; ii++) {
		if (argv[ii][0] != '\0') {
			exitcode = 2;
			fprintf(stderr,"'%s' not found\n", argv[ii]);
		}
	}
	exit(exitcode);
}

/*
 * Should the file system/resource named by dir and special be printed?
 */
int
shouldprint(argc, argv, dir, special)
	int	argc;
	char	*argv[];
	char	*dir;
	char	*special;
{
	int	found;
	int	i;

	found = 0;
	if (argc == 1) {
		return 1;	/* the default is "print everything" */
	}
	for (i = 0; i < argc; i++) {
		if (!strcmp(dir, argv[i]) || !strcmp(special, argv[i])) {
			argv[i][0] = '\0';	/* done with this arg */
			found++;		/* continue scan to find */
		}				/* duplicate requests */
	}
	return found;
}

void
prdat(s, n, bsize)
	char	*s;
	int	n;
	short	bsize;
{
	(void)printf("\t\t\t%15s %10d KB\n", s, n * bsize / 1024);
}

/*
 * Read up to len characters from the file fp and toss remaining characters
 * up to a newline.  If the last line is not terminated in a '\n', returns 0;
 */ 
int
getline(str, len, fp)
	char	*str;
	int	len;
	FILE	*fp;
{
	int	c;
	int	i = 1;
	char	*s = str; 

	for ( ; ; ) {
		switch (c = getc(fp)) {
		case EOF:
			*s = '\0';
			return 0;
		case '\n':
			*s = '\0';
			return 1;
		default:
			if (i < len) {
				*s++ = (char)c;
			}
			i++;
		}
	}
}

long
getcount(fs_name, cmdname)
	char		*fs_name;
	char		*cmdname;
{
	struct stat	statb;
	vfs_t		vfs_buf;
	vfs_t		*next_vfs;
	vfs_t		*rvfs;
	static int	nl_done = 0;
	static long	root_pos;
	static kvm_t	*kd = NULL;

	if (stat(fs_name, &statb) == -1) {
		perror(fs_name);
		fprintf(stderr, "%s: stat failed\n", cmdname);
		return (-1);
	}
	
	if (kd == NULL) {
		if ((kd = kvm_open(NULL, NULL, NULL, O_RDONLY, NULL)) == NULL) {
			perror("kvm_open");
			fprintf(stderr, "%s: cannot access kernel\n", cmdname);
			return (-1);
		}
		if (kvm_nlist(kd, nl) == -1) {
			perror("kvm_nlist");
			fprintf(stderr, "%s: cannot nlist rootvfs\n", cmdname);
			kvm_close(kd);
			kd = NULL;
			return (-1);
		}
		root_pos = nl[0].n_value;
	}

	if (kvm_read(kd, root_pos, (char *)&rvfs, sizeof (struct vfs *)) !=
	    sizeof (struct vfs *)) {
		fprintf(stderr, "%s: failed to read rootvfs\n", cmdname);
		return (-1);
	}
	next_vfs = rvfs;
	while (next_vfs) {
		if (kvm_read(kd, (unsigned long)next_vfs, (char *)&vfs_buf,
		    sizeof (struct vfs)) != sizeof (struct vfs)) {
			fprintf(stderr, "%s: cannot read next vfs\n", cmdname);
			return (-1);
		}
		/* check if this is the same device */
		if (vfs_buf.vfs_dev == statb.st_dev) {
			return (vfs_buf.vfs_bcount);
		} else {
			next_vfs = vfs_buf.vfs_next;
		}
	}

	/*
	 * not found in vfs list
	 */
	fprintf(stderr, "%s: %s not found in kernel\n", cmdname, fs_name);
	return (-1);
}
		
/*
 * Returns 1 if fstype is a remote file system type, 0 otherwise.
 */
int
remote(fstype)
	char		*fstype;
{
	char		buf[BUFSIZ];
	char		*fs;
	static int	first_call = 1;
 	static FILE	*fp;

	extern char	*strtok();

	if (first_call) {
		if ((fp = fopen(REMOTE, "r")) == NULL) {
			fprintf(stderr, "Unable to open %s\n", REMOTE);
			return 0;
		} else {
			first_call = 0;
		}
	} else if (fp != NULL) {
		rewind(fp);
	} else {
		return 0;	/* open of REMOTE had previously failed */
	}
	if (fstype == NULL || strlen(fstype) > FSTYPE_MAX) {
		return	0;	/* not a remote */
	}
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		fs = strtok(buf, " \t");
		if (!strcmp(fstype, fs)) {
			return	1;	/* is a remote fs */
		}
	}
	return	0;	/* not a remote */
}
