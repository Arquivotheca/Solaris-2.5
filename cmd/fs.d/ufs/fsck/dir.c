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
/*
 * Copyright (c) 1980, 1986, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that: (1) source distributions retain this entire copyright
 * notice and comment, and (2) distributions including binaries display
 * the following acknowledgement:  ``This product includes software
 * developed by the University of California, Berkeley and its contributors''
 * in the documentation or other materials provided with the distribution
 * and in all advertising materials mentioning features or use of this
 * software. Neither the name of the University nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ident	"@(#)dir.c	1.23	94/11/02 SMI"	/* SVr4.0 1.2 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/mntent.h>

#define bcopy(f,t,n)    memcpy(t,f,n)
#define bzero(s,n)	memset(s,0,n)
#define bcmp(s,d,n)	memcmp(s,d,n)

#define index(s,r)	strchr(s,r)
#define rindex(s,r)	strrchr(s,r)

#include <sys/fs/ufs_fs.h>
#include <sys/vnode.h>
#include <sys/fs/ufs_inode.h>
#define _KERNEL
#include <sys/fs/ufs_fsdir.h>
#undef _KERNEL
#include "fsck.h"

char	*lfname = "lost+found";
int	lfmode = 01700;
struct	dirtemplate emptydir = { 0, DIRBLKSIZ };
struct	dirtemplate dirhead = { 0, 12, 1, ".", 0, DIRBLKSIZ - 12, 2, ".." };

struct direct	*fsck_readdir();
struct bufarea	*getdirblk();

/*
 * Propagate connected state through the tree.
 */
propagate()
{
	register struct inoinfo **inpp, *inp;
	struct inoinfo **inpend;
	long change;

	inpend = &inpsort[inplast];
	do {
		change = 0;
		for (inpp = inpsort; inpp < inpend; inpp++) {
			inp = *inpp;
			if (inp->i_parent == 0)
				continue;
			if (statemap[inp->i_parent] == DFOUND &&
			    statemap[inp->i_number] == DSTATE) {
				statemap[inp->i_number] = DFOUND;
				change++;
			}
		}
	} while (change > 0);
}

/*
 * Scan each entry in a directory block.
 */
dirscan(idesc)
	register struct inodesc *idesc;
{
	register struct direct *dp;
	register struct bufarea *bp;
	int dsize, n;
	long blksiz;
	char dbuf[DIRBLKSIZ];

	if (idesc->id_type != DATA)
		errexit("wrong type to dirscan %d\n", idesc->id_type);
	if (idesc->id_entryno == 0 &&
	    (idesc->id_filesize & (DIRBLKSIZ - 1)) != 0)
		idesc->id_filesize = roundup(idesc->id_filesize, DIRBLKSIZ);
	blksiz = idesc->id_numfrags * sblock.fs_fsize;
	if (chkrange(idesc->id_blkno, idesc->id_numfrags)) {
		idesc->id_filesize -= blksiz;
		return (SKIP);
	}
	idesc->id_loc = 0;
	for (dp = fsck_readdir(idesc); dp != NULL; dp = fsck_readdir(idesc)) {
		/*
		 * If we were just passed a corrupt directory entry with
		 * d_reclen > DIRBLKSIZ so we don't bcopy() all over our stack.
		 * This directory gets cleaned up later.
		 */
		dsize = MIN(dp->d_reclen, DIRBLKSIZ);
		bcopy((char *)dp, dbuf, dsize);
		idesc->id_dirp = (struct direct *)dbuf;
		if ((n = (*idesc->id_func)(idesc)) & ALTERED) {
			bp = getdirblk(idesc->id_blkno, blksiz);
			bcopy(dbuf, bp->b_un.b_buf + idesc->id_loc - dsize,
			    dsize);
			dirty(bp);
			sbdirty();
		}
		if (n & STOP) 
			return (n);
	}
	return (idesc->id_filesize > 0 ? KEEPON : STOP);
}

/*
 * get next entry in a directory.
 */
struct direct *
fsck_readdir(idesc)
	register struct inodesc *idesc;
{
	register struct direct *dp, *ndp;
	register struct bufarea *bp;
	long size, blksiz;
	int dofixret;
	int origloc	= idesc->id_loc;

	blksiz = idesc->id_numfrags * sblock.fs_fsize;
	bp = getdirblk(idesc->id_blkno, blksiz);
	if (idesc->id_loc % DIRBLKSIZ == 0 && idesc->id_filesize > 0 &&
	    idesc->id_loc < blksiz) {
		dp = (struct direct *)(bp->b_un.b_buf + idesc->id_loc);
		if (dircheck(idesc, dp))
			goto dpok;
		idesc->id_loc += DIRBLKSIZ;
		idesc->id_filesize -= DIRBLKSIZ;
		dp->d_reclen = DIRBLKSIZ;
		dp->d_ino = 0;
		dp->d_namlen = 0;
		dp->d_name[0] = '\0';
		dofixret = dofix(idesc, "DIRECTORY CORRUPTED");
		/*
		 * dofix() may reuse bp; get it back
		 */
		bp = getdirblk(idesc->id_blkno, blksiz);
		dp = (struct direct *)(bp->b_un.b_buf + origloc);
		if (dofixret)
			dirty(bp);
		return (dp);
	}
dpok:
	if (idesc->id_filesize <= 0 || idesc->id_loc >= blksiz)
		return NULL;
	dp = (struct direct *)(bp->b_un.b_buf + idesc->id_loc);
	/*
	 * We don't want to be too trusting of this directory entry. It's
	 * possible to up here without running this through dircheck().
	 */
	idesc->id_loc += MIN(dp->d_reclen, DIRBLKSIZ);
	idesc->id_filesize -= MIN(dp->d_reclen, DIRBLKSIZ);
	if ((idesc->id_loc % DIRBLKSIZ) == 0)
		return (dp);
	ndp = (struct direct *)(bp->b_un.b_buf + idesc->id_loc);
	if (idesc->id_loc < blksiz && idesc->id_filesize > 0 &&
	    dircheck(idesc, ndp) == 0) {
		size = DIRBLKSIZ - (idesc->id_loc % DIRBLKSIZ);
		dp->d_reclen = MIN(dp->d_reclen, DIRBLKSIZ) + size;
		idesc->id_loc += size;
		idesc->id_filesize -= size;
		dofixret = dofix(idesc, "DIRECTORY CORRUPTED");
		/*
		 * dofix() may reuse bp; get it back
		 */
		bp = getdirblk(idesc->id_blkno, blksiz);
		dp = (struct direct *)(bp->b_un.b_buf + origloc);
		if (dofixret)
			dirty(bp);
	}
	return (dp);
}

/*
 * Verify that a directory entry is valid.
 * This is a superset of the checks made in the kernel.
 */
dircheck(idesc, dp)
	struct inodesc *idesc;
	register struct direct *dp;
{
	register int size;
	register char *cp;
	int spaceleft;

	size = DIRSIZ(dp);
	spaceleft = DIRBLKSIZ - (idesc->id_loc % DIRBLKSIZ);
	if (dp->d_ino < maxino &&
	    dp->d_reclen != 0 &&
	    (int)dp->d_reclen <= spaceleft &&
	    (dp->d_reclen & 0x3) == 0 &&
	    (int)dp->d_reclen >= size &&
	    idesc->id_filesize >= size &&
	    dp->d_namlen <= MAXNAMLEN) {
		if (dp->d_ino == 0)
			return (1);
		for (cp = dp->d_name, size = 0; size < (int)dp->d_namlen;
								size++, cp++)
			if ((*cp == 0) || (*cp == '/'))
				return (0);
		if (*cp == 0)
			return (1);
	}
	return (0);
}

direrror(ino, errmesg)
	ino_t ino;
	char *errmesg;
{

	fileerror(ino, ino, errmesg);
}

fileerror(cwd, ino, errmesg)
	ino_t cwd, ino;
	char *errmesg;
{
	register struct dinode *dp;
	char pathbuf[MAXPATHLEN + 1];

	pwarn("%s ", errmesg);
	pinode(ino);
	printf("\n");
	getpathname(pathbuf, cwd, ino);
	if (ino < UFSROOTINO || ino > maxino) {
		pfatal("NAME=%s\n", pathbuf);
		return;
	}
	dp = ginode(ino);
	if (ftypeok(dp))
		pfatal("%s=%s\n",
		    (dp->di_mode & IFMT) == IFDIR ? "DIR" : "FILE", pathbuf);
	else
		pfatal("NAME=%s\n", pathbuf);
}

adjust(idesc, lcnt)
	register struct inodesc *idesc;
	short lcnt;
{
	register struct dinode *dp;

	dp = ginode(idesc->id_number);
	if (dp->di_nlink == lcnt) {
		if (linkup(idesc->id_number, (ino_t)0) == 0) {
			clri(idesc, "UNREF", 0);
			return;
		}
		lcnt = (short)lncntp[idesc->id_number];
	}
	if (lcnt && dp->di_nlink != lcnt) {
		pwarn("LINK COUNT %s",
			(lfdir == idesc->id_number) ? lfname :
			(dp->di_mode & IFMT) == IFDIR ? "DIR" : 
			(dp->di_mode & IFMT) == IFSHAD ? "ACL" : "FILE");
		pinode(idesc->id_number);
		printf(" COUNT %d SHOULD BE %d",
			dp->di_nlink, dp->di_nlink - lcnt);
		if (preen) {
			if (lcnt < 0) {
				printf("\n");
				pfatal("LINK COUNT INCREASING");
			}
			printf(" (ADJUSTED)\n");
		}
		if (preen || reply("ADJUST") == 1) {
			dp->di_nlink -= lcnt;
			inodirty();
		}
	}
}

mkentry(idesc)
	struct inodesc *idesc;
{
	register struct direct *dirp = idesc->id_dirp;
	struct direct newent;
	int newlen, oldlen;

	newent.d_namlen = strlen(idesc->id_name);
	newlen = DIRSIZ(&newent);
	if (dirp->d_ino != 0)
		oldlen = DIRSIZ(dirp);
	else
		oldlen = 0;
	if ((int)dirp->d_reclen - oldlen < newlen)
		return (KEEPON);
	newent.d_reclen = dirp->d_reclen - oldlen;
	dirp->d_reclen = oldlen;
	dirp = (struct direct *)(((char *)dirp) + oldlen);
	dirp->d_ino = idesc->id_parent;	/* ino to be entered is in id_parent */
	dirp->d_reclen = newent.d_reclen;
	dirp->d_namlen = newent.d_namlen;
	bcopy(idesc->id_name, dirp->d_name, (int)dirp->d_namlen + 1);
	return (ALTERED|STOP);
}

chgino(idesc)
	struct inodesc *idesc;
{
	register struct direct *dirp = idesc->id_dirp;

	if (bcmp(dirp->d_name, idesc->id_name, (int)dirp->d_namlen + 1))
		return (KEEPON);
	dirp->d_ino = idesc->id_parent;
	return (ALTERED|STOP);
}

linkup(orphan, parentdir)
	ino_t orphan;
	ino_t parentdir;
{
	register struct dinode *dp;
	int lostdir;
	int lostshadow;
	ino_t oldlfdir;
	struct inodesc idesc;
	char tempname[BUFSIZ];
	extern int pass4check();

	bzero((char *)&idesc, sizeof(struct inodesc));
	dp = ginode(orphan);
	lostdir = (dp->di_mode & IFMT) == IFDIR;
	/*
	 * reclaim thread will remove this dir at mount
	 */
	if (lostdir &&
	    (willreclaim && dp->di_nlink <= 0 && lncntp[orphan] == -1)) {
		if (parentdir && lncntp[parentdir] < 0)
			++lncntp[parentdir];
		return (0);
	}
	lostshadow = (dp->di_mode & IFMT) == IFSHAD;
	pwarn("UNREF %s ", lostdir ? "DIR" : lostshadow ? "ACL" : "FILE");
	pinode(orphan);
	if (lostshadow || dp->di_size == 0)
		return (0);
	if (preen)
		printf(" (RECONNECTED)\n");
	else
		if (reply("RECONNECT") == 0)
			return (0);
	if (lfdir == 0) {
		dp = ginode(UFSROOTINO);
		idesc.id_name = lfname;
		idesc.id_type = DATA;
		idesc.id_func = findino;
		idesc.id_number = UFSROOTINO;
		idesc.id_fix = DONTKNOW;
		if ((ckinode(dp, &idesc) & FOUND) != 0) {
			lfdir = idesc.id_parent;
		} else {
			pwarn("NO lost+found DIRECTORY");
			if (preen || reply("CREATE")) {
				lfdir = allocdir(UFSROOTINO, (ino_t)0, lfmode);
				if (lfdir != 0) {
					if (makeentry(UFSROOTINO, lfdir, lfname) != 0) {
						if (preen)
							printf(" (CREATED)\n");
					} else {
						freedir(lfdir, UFSROOTINO);
						lfdir = 0;
						if (preen)
							printf("\n");
					}
				}
			}
		}
		if (lfdir == 0) {
			pfatal("SORRY. CANNOT CREATE lost+found DIRECTORY");
			printf("\n\n");
			return (0);
		}
	} else {	/* lost+found already created */
		idesc.id_name = lfname;
	}
	dp = ginode(lfdir);
	if ((dp->di_mode & IFMT) != IFDIR) {
		pfatal("lost+found IS NOT A DIRECTORY");
		if (reply("REALLOCATE") == 0)
			return (0);
		oldlfdir = lfdir;
		if ((lfdir = allocdir(UFSROOTINO, (ino_t)0, lfmode)) == 0) {
			pfatal("SORRY. CANNOT CREATE lost+found DIRECTORY\n\n");
			return (0);
		}
		if ((changeino(UFSROOTINO, lfname, lfdir) & ALTERED) == 0) {
			pfatal("SORRY. CANNOT CREATE lost+found DIRECTORY\n\n");
			return (0);
		}
		inodirty();
		idesc.id_type = ADDR;
		idesc.id_func = pass4check;
		idesc.id_number = oldlfdir;
		idesc.id_fix = DONTKNOW;
		lncntp[oldlfdir]++;
		adjust(&idesc, lncntp[oldlfdir]);
		lncntp[oldlfdir] = 0;
		dp = ginode(lfdir);
	}
	if (statemap[lfdir] != DFOUND) {
		pfatal("SORRY. NO lost+found DIRECTORY\n\n");
		return (0);
	}
	(void)lftempname(tempname, orphan);
	if (makeentry(lfdir, orphan, tempname) == 0) {
		pfatal("SORRY. NO SPACE IN lost+found DIRECTORY");
		printf("\n\n");
		return (0);
	}
	lncntp[orphan]--;
	if (lostdir) {
		if ((changeino(orphan, "..", lfdir) & ALTERED) == 0 &&
		    parentdir != (ino_t)-1)
			(void)makeentry(orphan, lfdir, "..");
		dp = ginode(lfdir);
		dp->di_nlink++;
		inodirty();
		lncntp[lfdir]++;
		if (parentdir != lfdir)
			lncntp[parentdir]++;
		pwarn("DIR I=%u CONNECTED. ", orphan);
		if (parentdir != (ino_t)-1)
			printf("PARENT WAS I=%u\n", parentdir);
		if (preen == 0)
			printf("\n");
	}
	return (1);
}

/*
 * fix an entry in a directory.
 */
changeino(dir, name, newnum)
	ino_t dir;
	char *name;
	ino_t newnum;
{
	struct inodesc idesc;

	bzero((char *)&idesc, sizeof(struct inodesc));
	idesc.id_type = DATA;
	idesc.id_func = chgino;
	idesc.id_number = dir;
	idesc.id_fix = DONTKNOW;
	idesc.id_name = name;
	idesc.id_parent = newnum;	/* new value for name */
	return (ckinode(ginode(dir), &idesc));
}

/*
 * make an entry in a directory
 */
makeentry(parent, ino, name)
	ino_t parent, ino;
	char *name;
{
	struct dinode *dp;
	struct inodesc idesc;
	char pathbuf[MAXPATHLEN + 1];
	
	if (parent < UFSROOTINO || parent >= maxino ||
	    ino < UFSROOTINO || ino >= maxino)
		return (0);
	bzero((char *)&idesc, sizeof(struct inodesc));
	idesc.id_type = DATA;
	idesc.id_func = mkentry;
	idesc.id_number = parent;
	idesc.id_parent = ino;	/* this is the inode to enter */
	idesc.id_fix = DONTKNOW;
	idesc.id_name = name;
	dp = ginode(parent);
	if (dp->di_size % DIRBLKSIZ) {
		dp->di_size = roundup(dp->di_size, DIRBLKSIZ);
		inodirty();
	}
	if ((ckinode(dp, &idesc) & ALTERED) != 0)
		return (1);
	getpathname(pathbuf, parent, parent);
	dp = ginode(parent);
	if (expanddir(dp, pathbuf) == 0)
		return (0);
	idesc.id_fix = DONTKNOW;
	return (ckinode(dp, &idesc) & ALTERED);
}

/*
 * Attempt to expand the size of a directory
 */
expanddir(dp, name)
	register struct dinode *dp;
	char *name;
{
	daddr_t lastbn, newblk;
	register struct bufarea *bp;
	char *cp, firstblk[DIRBLKSIZ];

	if (dp->di_size == 0)
		return (0);
	lastbn = lblkno(&sblock, dp->di_size - 1);
	if (lastbn >= NDADDR - 1 || dp->di_db[lastbn] == 0)
		return (0);
	if ((newblk = allocblk(sblock.fs_frag)) == 0)
		return (0);
	dp->di_db[lastbn + 1] = dp->di_db[lastbn];
	dp->di_db[lastbn] = newblk;
	dp->di_size += sblock.fs_bsize;
	dp->di_blocks += btodb(sblock.fs_bsize);
	bp = getdirblk(dp->di_db[lastbn + 1],
		dblksize(&sblock, dp, lastbn + 1));
	if (bp->b_errs)
		goto bad;
	bcopy(bp->b_un.b_buf, firstblk, DIRBLKSIZ);
	bp = getdirblk(newblk, sblock.fs_bsize);
	if (bp->b_errs)
		goto bad;
	bcopy(firstblk, bp->b_un.b_buf, DIRBLKSIZ);
	for (cp = &bp->b_un.b_buf[DIRBLKSIZ];
	     cp < &bp->b_un.b_buf[sblock.fs_bsize];
	     cp += DIRBLKSIZ)
		bcopy((char *)&emptydir, cp, sizeof emptydir);
	dirty(bp);
	bp = getdirblk(dp->di_db[lastbn + 1],
		dblksize(&sblock, dp, lastbn + 1));
	if (bp->b_errs)
		goto bad;
	bcopy((char *)&emptydir, bp->b_un.b_buf, sizeof emptydir);
	pwarn("NO SPACE LEFT IN %s", name);
	if (preen)
		printf(" (EXPANDED)\n");
	else if (reply("EXPAND") == 0)
		goto bad;
	dirty(bp);
	inodirty();
	return (1);
bad:
	dp->di_db[lastbn] = dp->di_db[lastbn + 1];
	dp->di_db[lastbn + 1] = 0;
	dp->di_size -= sblock.fs_bsize;
	dp->di_blocks -= btodb(sblock.fs_bsize);
	freeblk(newblk, sblock.fs_frag);
	return (0);
}

/*
 * allocate a new directory
 */
allocdir(parent, request, mode)
	ino_t parent, request;
	int mode;
{
	ino_t ino;
	char *cp;
	struct dinode *dp;
	struct inoinfo *inp;
	register struct bufarea *bp;

	ino = allocino(request, IFDIR|mode);
	if (ino == 0)
		return (0);
	dirhead.dot_ino = ino;
	dirhead.dotdot_ino = parent;
	dp = ginode(ino);
	bp = getdirblk(dp->di_db[0], sblock.fs_fsize);
	if (bp->b_errs) {
		freeino(ino);
		return (0);
	}
	bcopy((char *)&dirhead, bp->b_un.b_buf, sizeof dirhead);
	for (cp = &bp->b_un.b_buf[DIRBLKSIZ];
	     cp < &bp->b_un.b_buf[sblock.fs_fsize];
	     cp += DIRBLKSIZ)
		bcopy((char *)&emptydir, cp, sizeof emptydir);
	dirty(bp);
	dp->di_nlink = 2;
	inodirty();
	if (!inocached(ino)) {
		if (debug)
			printf("inode %d added to directory cache\n",
			    ino);
		cacheino(dp, ino);
	} else {
		/*
		 * re-using an old directory inode
		 */
		inp = getinoinfo(ino);
		inp->i_isize = dp->di_size;
		inp->i_numblks = dp->di_blocks * sizeof(daddr_t);
		inp->i_parent = parent;
		bcopy((char *)&dp->di_db[0], (char *)&inp->i_blks[0],
		    (int)inp->i_numblks);
	}
	if (ino == UFSROOTINO) {
		lncntp[ino] = dp->di_nlink;
		return(ino);
	}
	if (statemap[parent] != DSTATE && statemap[parent] != DFOUND) {
		freeino(ino);
		return (0);
	}
	statemap[ino] = statemap[parent];
	if (statemap[ino] == DSTATE) {
		lncntp[ino] = dp->di_nlink;
		lncntp[parent]++;
	}
	dp = ginode(parent);
	dp->di_nlink++;
	inodirty();
	return (ino);
}

/*
 * free a directory inode
 */
freedir(ino, parent)
	ino_t ino, parent;
{
	struct dinode *dp;

	if (ino != parent) {
		dp = ginode(parent);
		dp->di_nlink--;
		inodirty();
	}
	freeino(ino);
}

/*
 * generate a temporary name for the lost+found directory.
 */
lftempname(bufp, ino)
	char *bufp;
	ino_t ino;
{
	register ino_t in;
	register char *cp;
	int namlen;

	cp = bufp + 2;
	for (in = maxino; in > 0; in /= 10)
		cp++;
	*--cp = 0;
	namlen = cp - bufp;
	in = ino;
	while (cp > bufp) {
		*--cp = (in % 10) + '0';
		in /= 10;
	}
	*cp = '#';
	return (namlen);
}

/*
 * Get a directory block.
 * Insure that it is held until another is requested.
 */
struct bufarea *
getdirblk(blkno, size)
	daddr_t blkno;
	long size;
{
	if (pdirbp != 0)
		pdirbp->b_flags &= ~B_INUSE;
	pdirbp = getdatablk(blkno, size);
	return (pdirbp);
}
