/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 * 
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 * 
 * 
 * 
 * 		Copyright Notice 
 * 
 * Notice of copyright on this source code product does not indicate 
 * publication.
 * 
 * 	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 * 	          All rights reserved.
 *  
 */

#ident	"@(#)clri.c	1.14	94/06/01 SMI"	/* SVr4.0 1.4 */

/*
 * clri filsys inumber ...
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/mntent.h>

#define bcopy(f,t,n)    memcpy(t,f,n)
#define bzero(s,n)	memset(s,0,n)
#define bcmp(s,d,n)	memcmp(s,d,n)

#define index(s,r)	strchr(s,r)
#define rindex(s,r)	strrchr(s,r)

#include <sys/vnode.h>
#include <sys/fs/ufs_inode.h>
#include <sys/fs/ufs_fs.h>

extern offset_t llseek();

#define ISIZE	(sizeof(struct dinode))
#define	NI	(MAXBSIZE/ISIZE)

struct	dinode	buf[NI];

union {
	char		dummy[SBSIZE];
	struct fs	sblk;
} sb_un;
#define sblock sb_un.sblk

int	status;

main(argc, argv)
	int argc;
	char *argv[];
{
	register	i, f;
	unsigned	n;
	int		j, k;
	offset_t	off;
	long		gen;

	if (argc < 3) {
		printf("ufs usage: clri filsys inumber ...\n");
		exit(35);
	}
	f = open(argv[1], 2);
	if (f < 0) {
		printf("cannot open %s\n", argv[1]);
		exit(35);
	}
	llseek(f, (offset_t)(SBLOCK * DEV_BSIZE), 0);
	if (read(f, &sblock, SBSIZE) != SBSIZE) {
		printf("cannot read %s\n", argv[1]);
		exit(35);
	}
	if (sblock.fs_magic != FS_MAGIC) {
		printf("bad super block magic number\n");
		exit(35);
	}
	for (i = 2; i < argc; i++) {
		if (!isnumber(argv[i])) {
			printf("%s: is not a number\n", argv[i]);
			status = 1;
			continue;
		}
		n = atoi(argv[i]);
		if (n == 0) {
			printf("%s: is zero\n", argv[i]);
			status = 1;
			continue;
		}
		off = fsbtodb(&sblock, itod(&sblock, n));
		off *= DEV_BSIZE;
		llseek(f, off, 0);
		if (read(f, (char *)buf, sblock.fs_bsize) != sblock.fs_bsize) {
			printf("%s: read error\n", argv[i]);
			status = 1;
		}
	}
	if (status)
		exit(status+31);

	/*
	 * Update the time in superblock, so fsck will check this filesystem.
	 */
	llseek(f, (offset_t)(SBLOCK * DEV_BSIZE), 0);
	(void) time(&sblock.fs_time);
	if (write(f, &sblock, SBSIZE) != SBSIZE) {
		printf("cannot update %s\n", argv[1]);
		exit(35);
	}

	for (i = 2; i < argc; i++) {
		n = atoi(argv[i]);
		printf("clearing %u\n", n);
		off = fsbtodb(&sblock, itod(&sblock, n));
		off *= DEV_BSIZE;
		llseek(f, off, 0);
		read(f, (char *)buf, sblock.fs_bsize);
		j = itoo(&sblock, n);
		gen = buf[j].di_gen;
		bzero((caddr_t)&buf[j], ISIZE);
		buf[j].di_gen = gen + 1;
		llseek(f, off, 0);
		write(f, (char *)buf, sblock.fs_bsize);
	}
	if (status)
		exit(status+31);
	close(f);
	exit(0);
	/* NOTREACHED */
}

isnumber(s)
	char *s;
{
	register c;

	while(c = *s++)
		if (c < '0' || c > '9')
			return(0);
	return(1);
}
