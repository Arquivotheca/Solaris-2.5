#ident	"@(#)ufs_inode.c	1.17	95/03/02 SMI"
			/* SVr4.0 1.2.3.1 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 *		Copyright (C) 1991  Sun Microsystems, Inc
 *			All rights reserved.
 *		Notice of copyright on this source code
 *		product does not indicate publication.
 *
 *		RESTRICTED RIGHTS LEGEND:
 *   Use, duplication, or disclosure by the Government is subject
 *   to restrictions as set forth in subparagraph (c)(1)(ii) of
 *   the Rights in Technical Data and Computer Software clause at
 *   DFARS 52.227-7013 and in similar clauses in the FAR and NASA
 *   FAR Supplement.
 */


#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/mntent.h>
#include <stdio.h>
#include <nlist.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/var.h>
#include <sys/vfs.h>
#include <sys/fstyp.h>
#include <sys/fsid.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/vnode.h>
#include <sys/elf.h>
#define	_KERNEL
#include <sys/fs/ufs_inode.h>
#undef	_KERNEL
#include <sys/cred.h>
#include <sys/stream.h>
#include "crash.h"

struct nlist	UFSIfreelist;
Elf32_Sym	*Ngrps;
struct nlist    i_head;
struct inode	ufs_ibuf;		/* buffer for UFS inode */
long		u_ninode;		/* size of UFS inode table */
long		u_inohsz;		/* size of UFS hash table */

/*
 * structure for inode-lookup performance
 */
struct icursor {
	long slot;				/* inode "slot" number	*/
	struct inode *nextip;			/* next ip read addr	*/
	union ihead *kern_ihead, *kern_itail;	/* hash-chain head/tail	*/
	union ihead ihbuf;			/* buf for hash chain	*/
	union ihead *ih;			/* hash chain ptr	*/
};

struct fsnames {
	char	*name;
	char	*vnsym;
	long	vnaddr;
} fsnames[] = {
	{"AUTO", "auto_vnodeops",	0},
	{"CACH", "cachefs_vnodeops",	0},
	{"FD  ", "fdvnodeops",		0},
	{"FIFO", "fifo_vnodeops",	0},
	{"HSFS", "hsfs_vnodeops",	0},
	{"LOFS", "lo_vnodeops",		0},
	{"NAME", "nm_vnodeops",		0},
	{"NFS ", "nfs_vnodeops",	0},
	{"PCFS", "pcfs_dvnodeops",	0},
	{"PCFS", "pcfs_fvnodeops",	0},
	{"PROC", "prvnodeops",		0},
	{"SPEC", "spec_vnodeops",	0},
	{"SWAP", "swap_vnodeops",	0},
	{"TMP ", "tmp_vnodeops",	0},
	{"UFS ", "ufs_vnodeops",	0},
	{" ?? ",  0,			0}
};

static void print_ufs_inode();
static void list_ufs_inode();
long slot_to_inode();

static void prfile(int, int, void *);
static void kmfile(void *kaddr, void *buf);


/*
 * get_ufsinfo: gets a copy of kernel data needed by
 * "ui" and "lck" commands for ufs-inode lookups.
 */
void
get_ufsinfo()
{
	static int	got_info = 0;
	struct nlist	UFSNinode;
	struct nlist	UFSinohsz;

	if (got_info)
		return;

	if (nl_getsym("ihead", &i_head))
		error("ihead inode table not found in symbol table\n");

	if (nl_getsym("ufs_ninode", &UFSNinode))
		error("UFS inode table size not found in symbol table\n");
	readmem(UFSNinode.n_value, 1, -1, (char *)&u_ninode, sizeof (u_ninode),
		"size of UFS inode table");

	if (nl_getsym("inohsz", &UFSinohsz))
		error("UFS inode hash table size not found in symbol table\n");
	readmem(UFSinohsz.n_value, 1, -1, (char *)&u_inohsz, sizeof (u_inohsz),
		"size of UFS inode hash table");
	got_info++;
}


/*
 * Get arguments for UFS inode.
 */
int
get_ufs_inode()
{
	int	slot = -1;
	int	full = 0;
	int	dump = 0;
	int	lock = 0;
	int	all = 0;
	int	phys = 0;
	long	addr = -1;
	long	arg1 = -1;
	long	arg2 = -1;
	int	free = 0;
	long	next;
	int	c;
	struct inode	*firstfree;
	struct inode    ip;
	char *heading =
"SLOT MAJ/MIN    INUMB  RCNT LINK   UID   GID    SIZE    MODE  FLAGS\n";

	get_ufsinfo();

	optind = 1;
	while ((c = getopt(argcnt, args, "defprlw:")) != EOF) {
		switch (c) {
		case 'e':	all = 1;
				break;

		case 'f':	full = 1;
				break;

		case 'l':	lock = 1;
				break;

		case 'd':	dump = 1;
				break;

		case 'p':	phys = 1;
				break;

		case 'r':	free = 1;
				break;

		case 'w':	redirect();
				break;

		default:	longjmp(syn, 0);
		}
	}

	if (dump)
		list_ufs_inode();
	else {
		fprintf(fp, "UFS INODE TABLE SIZE = %d\n", u_ninode);
		if (!full && !lock) {
			fprintf(fp, "%s", heading);
		}
		if (free) {
			if (nl_getsym("ifreeh", &UFSIfreelist))
				error("ifreeh not found in symbol table\n");
			readmem((long)UFSIfreelist.n_value, 1, -1,
				(char *)&firstfree,
				sizeof (int), "ifreeh buffer");
			next = (long)firstfree;
			while (next) {
				if (slot_to_inode(slot, next, 0, &ip))
					print_ufs_inode(slot, all, full, &ip,
						heading, lock);
				else error("Could not find inode\n");
				next = (long)ufs_ibuf.i_freef;
				if (next == (long)firstfree)
					next = 0;
			}
		} else if (args[optind]) {
			all = 1;
			do {
				getargs(u_ninode, &arg1, &arg2, phys);
				if (arg1 == -1)
					continue;
				if (arg2 != -1) {
					for (slot = arg1; slot <= arg2;
					    slot++) {
						if (slot_to_inode(slot,
						    addr, phys, &ip))
							print_ufs_inode(slot,
								all, full, &ip,
								heading, lock);
						else
							error("Could not "
								"find inode\n");
					}
				} else {
					if ((unsigned long)arg1 < u_ninode)
						slot = arg1;
					else addr = arg1;
					if (slot_to_inode(slot, addr, phys,
					    &ip))
						print_ufs_inode(slot, all, full,
						    &ip, heading, lock);
					else
						error("Could not find inode\n");
				}
				slot = addr = arg1 = arg2 = -1;
			} while (args[++optind]);
		} else for (slot = 0; slot < u_ninode; slot++) {
			if (slot_to_inode(slot, addr, phys, &ip))
				print_ufs_inode(slot, all, full, &ip,
					heading, lock);
			else
				error("Could not find inode\n");
		}
	}
	return (0);
}

static void
list_ufs_inode()
{
	int		i;
	long		next;
	struct inode	*firstfree;
	struct inode    ip;
	long		addr, slot_to_inode();

	if (nl_getsym("ifreeh", &UFSIfreelist))
		error("ifreeh not found in symbol table\n");

	(void) fprintf(fp, "The following UFS inodes are in use:\n");
	(void) fprintf(fp, "    ADDRESS      I-NUMBER\n");
	for (i = 0; i < u_ninode; i++) {
		if (addr = slot_to_inode(i, -1, 0, &ip)) {
			if ((ip.i_vnode.v_count != 0) && (ip.i_flag & IREF))
				fprintf(fp, "   0x%x    %d\n",
					addr, ip.i_number);
		}
	}

	readmem(UFSIfreelist.n_value, 1, -1, (char *)&firstfree, sizeof (long),
		"ifreeh buffer");
	next = (long)firstfree;

	fprintf(fp, "\n\nThe following UFS inodes are on the freelist:\n");
	(void) fprintf(fp, "	ADDRESS		I-NUMBER\n");
	while (next) {
		readmem(next, 1, -1, (char *)&ufs_ibuf, sizeof (ufs_ibuf),
								"UFS inode");
		fprintf(fp, "   0x%x    %d\n", next, ufs_ibuf.i_number);
		next = (long)ufs_ibuf.i_freef;
		if (next == (long)firstfree)
			next = 0;
	}

	next = (long)firstfree;
	fprintf(fp, "\n\nThe following UFS inodes are on the freelist but");
	fprintf(fp, " have non-zero reference counts:\n");
	(void) fprintf(fp, "	ADDRESS		I-NUMBER\n");
	while (next) {
		readmem(next, 1, -1, (char *)&ufs_ibuf, sizeof (ufs_ibuf),
								"UFS inode");
		if (ufs_ibuf.i_vnode.v_count != 0)
			fprintf(fp, "   0x%x    %d\n", next, ufs_ibuf.i_number);
		next = (long)ufs_ibuf.i_freef;
		if (next == (long)firstfree)
			next = 0;
	}
	fprintf(fp, "\n");
}

/*
 * Print UFS inode table.
 */

static void
print_ufs_inode(slot, all, full, ip, heading, lock)
	int	slot, all, full;
	struct inode *ip;
	char	*heading;
	int lock;
{
	char		ch;
	int		i;

	if (!ip->i_vnode.v_count && !all)
		return;
	if (full || lock)
		fprintf(fp, "%s", heading);
	if (slot == -1)
		fprintf(fp, "  - ");
	else
		fprintf(fp, "%3d", slot);
	fprintf(fp, " %4u, %-5u %6u %3d %4d %5d %5d %8ld",
		getemajor(ip->i_dev),
		geteminor(ip->i_dev),
		ip->i_number,
		ip->i_vnode.v_count,
		ip->i_nlink,
		ip->i_uid,
		ip->i_gid,
		ip->i_size);
	switch (ip->i_vnode.v_type) {
		case VDIR: ch = 'd'; break;
		case VCHR: ch = 'c'; break;
		case VBLK: ch = 'b'; break;
		case VREG: ch = 'f'; break;
		case VLNK: ch = 'l'; break;
		case VFIFO: ch = 'p'; break;
		default:    ch = '-'; break;
	}
	fprintf(fp, "  %c", ch);
	fprintf(fp, "%s%s%s%03o",
		ip->i_mode & ISUID ? "u" : "-",
		ip->i_mode & ISGID ? "g" : "-",
		ip->i_mode & ISVTX ? "v" : "-",
		ip->i_mode & 0777);

	fprintf(fp, "%s%s%s%s%s%s%s%s%s\n",
		ip->i_flag & IUPD ? " up" : "",
		ip->i_flag & IACC ? " ac" : "",
		ip->i_flag & IMOD ? " md" : "",
		ip->i_flag & ICHG ? " ch" : "",
		ip->i_flag & INOACC ? " na" : "",
		ip->i_flag & IMODTIME ? "md" : "",
		ip->i_flag & IREF ? " rf" : "",
		ip->i_flag & ISYNC ? " sy" : "",
		ip->i_flag & IFASTSYMLNK ? " fl" : "");
	if (lock) {
		fprintf(fp, "\ni-rwlock: ");
		prrwlock(&(ip->i_rwlock));
		fprintf(fp, "i-contents: ");
		prrwlock(&(ip->i_contents));
		fprintf(fp, "i_tlock: ");
		prmutex(&(ip->i_tlock));
		prcondvar(&ip->i_wrcv, "i_wrcv");
	}

	if (!full)
		return;

	fprintf(fp, "\t  NEXTR \n");
	fprintf(fp, " %8x\n", ufs_ibuf.i_nextr);

	if ((ip->i_vnode.v_type == VDIR) || (ip->i_vnode.v_type == VREG) ||
		(ip->i_vnode.v_type == VLNK)) {
		for (i = 0; i < NADDR; i++) {
			if (!(i & 3))
				fprintf(fp, "\n\t");
			fprintf(fp, "[%2d]: %-10x", i, ip->i_db[i]);
		}
		fprintf(fp, "\n");
	} else
		fprintf(fp, "\n");

	/* print vnode info */
	fprintf(fp, "\nVNODE :\n");
	fprintf(fp, "VCNT VFSMNTED   VFSP   STREAMP VTYPE   RDEV VDATA    ");
	fprintf(fp, "   VFILOCKS   VFLAG \n");
	prvnode(&ip->i_vnode, lock);
	fprintf(fp, "\n");
}


/*
 * slot_to_inode: looks up an inode based on a "slot" number
 * (or counted position) within the hash chains, or lookup
 * by address using the arg addr.
 *
 * Most calls to this routine are a sequential range of lookup requests
 * by slot number, usually (0 to u_ninode-1).  To avoid the need to
 * re-read through all previous inodes in the hash-chains, an internal
 * "cursor" structure (lkup) is maintained with the prev slot number,
 * next inode read address and hash-chain info.
 */

long
slot_to_inode(slot, addr, phys, node)
int slot;
long addr;
int phys;
struct inode *node;
{
	static struct icursor lkup = { -1 };
	register struct inode *ip;

	if (addr != -1) {
		readbuf(addr, 0, phys, -1,
		    (char *) node, sizeof (struct inode), "inode");
		return (addr);
	}

	if (slot < 0 || slot >= u_ninode)
		error("slot_to_inode: bad slot # %d, range is 0 to %d\n",
		    slot, u_ninode);

	if (lkup.slot == -1) {
		/*
		 * get value at &ihead and set tail pointer;
		 * set slot to an out-of-range value and cause
		 * a cursor reset below
		 */
		readmem(i_head.n_value, 1, -1,
		    (char *) &lkup.kern_ihead, sizeof (lkup.kern_ihead),
		    "ihead value");
		lkup.kern_itail = lkup.kern_ihead + u_inohsz;
		lkup.slot = (u_ninode * 2);
	}

	/*
	 * if not a sequential lookup request,
	 * reset cursor to starting position.
	 *
	 * lkup.slot is the # of the last "slot" read;
	 * lkup.nextip is the next inode read addr
	 */
	if (slot != (lkup.slot + 1)) {
		lkup.slot = -1;
		lkup.ih = lkup.kern_ihead - 1;
		lkup.nextip = (struct inode *) lkup.ih;
	}

	/*
	 * search through inode hash chains until the counter
	 * matches the requested "slot" number
	 */
	while (lkup.ih < lkup.kern_itail) {
		/*
		 * if nextip points to the top of a hash chain,
		 * incr to the next chain and read a union ihead;
		 * repeat the check since the next hash chain
		 * may be empty
		 */
		if (lkup.nextip == (struct inode *) lkup.ih) {
			lkup.ih++;
			readmem((u_int) lkup.ih, 1, -1,
			    (char *) &lkup.ihbuf, sizeof (lkup.ihbuf),
			    "ihead table");
			lkup.nextip = lkup.ihbuf.ih_chain[0];
			continue;
		}

		readmem((u_int) lkup.nextip, 1, -1,
		    (char *) node, sizeof (*node), "inode");

		ip = lkup.nextip;
		lkup.nextip = node->i_forw;
		lkup.slot++;

		if (lkup.slot == slot)
			return ((long) ip);
	}

	return (0);
}


/* get arguments for vnode function */
int
getvnode()
{
	long addr = -1;
	int phys = 0;
	int lock = 0;
	int c;
	struct vnode vnbuf;


	optind = 1;
	while ((c = getopt(argcnt, args, "lpw:")) != EOF) {
		switch (c) {
			case 'l' :	lock = 1;
					break;
			case 'p' :	phys = 1;
					break;
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
					break;
		}
	}
	if (args[optind]) {
		fprintf(fp, "VCNT  VFSMNTED   VFSP   STREAMP VTYPE   ");
		fprintf(fp, "RDEV   VDATA   VFILOCKS VFLAG     \n");
		do {
			if ((addr = strcon(args[optind], 'h')) == -1)
				continue;
			readbuf(addr, 0, phys, -1, (char *)&vnbuf,
				sizeof (vnbuf), "vnode structure");
			prvnode(&vnbuf, lock);
		} while (args[++optind]);
	} else longjmp(syn, 0);

	return (0);
}

prvnode(vnptr, lock)
struct vnode *vnptr;
int lock;
{
	fprintf(fp, "%3d   %6x %8x    %x",
		vnptr->v_count,
		vnptr->v_vfsmountedhere,
		vnptr->v_vfsp,
		vnptr->v_stream);
	switch (vnptr->v_type) {
		case VREG :	fprintf(fp, "      f       - "); break;
		case VDIR :	fprintf(fp, "      d       - "); break;
		case VLNK :	fprintf(fp, "      l       - "); break;
		case VCHR :
				fprintf(fp, "       c  %4u,%-3u",
					getemajor(vnptr->v_rdev),
					geteminor(vnptr->v_rdev));
				break;
		case VBLK :
				fprintf(fp, "      b  %4u,%-3u",
					getemajor(vnptr->v_rdev),
					geteminor(vnptr->v_rdev));
				break;
		case VFIFO :	fprintf(fp, "      p       - "); break;
		case VNON :	fprintf(fp, "      n       - "); break;
		default :	fprintf(fp, "      -       - "); break;
	}
	fprintf(fp, " %4x %8x",
		vnptr->v_data,
		vnptr->v_filocks);
	fprintf(fp, "%s\n",
		vnptr->v_flag & VROOT ? "  root" : "   -");
	if (lock) {
		fprintf(fp, "mutex v_lock:");
		prmutex(&(vnptr->v_lock));
		prcondvar(&vnptr->v_cv, "v_cv");
	}
	return (0);
}

static char *fileheading = "ADDRESS  RCNT    TYPE/ADDR       OFFSET   FLAGS\n";
static int filefull;

/* get arguments for file function */
int
getfile()
{
	struct nlist nl;
	struct fsnames *fsn;
	int all = 0;
	int phys = 0;
	int c;
	long filep;
	char *heading = "ADDRESS  RCNT    TYPE/ADDR        OFFSET   FLAGS\n";

	filefull = 0;
	optind = 1;
	while ((c = getopt(argcnt, args, "epfw:")) != EOF) {
		switch (c) {
			case 'e' :	all = 1;
					break;
			case 'f' :	filefull = 1;
					break;
			case 'p' :	phys = 1;
					break;
			case 'w' :	redirect();
					break;
			default  :	longjmp(syn, 0);
		}
	}

	for (fsn = fsnames; fsn->vnsym; fsn++) {
		nl_getsym(fsn->vnsym, &nl);
		fsn->vnaddr = nl.n_value;
	}

	if (!filefull)
		fprintf(fp, "%s", fileheading);
	if (args[optind]) {
		all = 1;
		do {
			filep = strcon(args[optind], 'h');
			if (filep == -1)
				continue;
			else
				prfile(all, phys, (void *)filep);
			filep = -1;
		} while (args[++optind]);
	} else {
		kmem_cache_apply(kmem_cache_find("file_cache"), kmfile);
	}
	return (0);
}

static void
kmfile(void *kaddr, void *buf)
{
	prfile(0, 0, kaddr);
}

/* print file table */
static void
prfile(int all, int phys, void *addr)
{
	struct fsnames *fsn;
	struct file fbuf;
	struct cred *credbufp;
	int ngrpbuf;
	short i;
	char fstyp[5];
	struct vnode vno;

	readbuf((unsigned)addr, 0, phys, -1, (char *)&fbuf, sizeof (fbuf),
		"file table");
	if (fbuf.f_count == 0 && all == 0)
		return;
	if (filefull)
		fprintf(fp, "\n%s", fileheading);
	fprintf(fp, "%.8x", addr);
	fprintf(fp, " %3d", fbuf.f_count);

	if (fbuf.f_count && (fbuf.f_vnode != 0)) {
		/* read in vnode */
		readmem((unsigned)fbuf.f_vnode, 1, -1, (char *)&vno,
			sizeof (vno), "vnode");

		for (fsn = fsnames; fsn->vnsym; fsn++)
			if (vno.v_op == (struct vnodeops *)fsn->vnaddr)
				break;
		strcpy(fstyp, fsn->name);

	} else
		strcpy(fstyp, " ?  ");
	fprintf(fp, "    %s/%8x", fstyp, fbuf.f_vnode);
	fprintf(fp, " %10lld", fbuf.f_offset);
	fprintf(fp, "  %s%s%s%s%s%s%s%s\n",
		fbuf.f_flag & FREAD ? " read" : "",
		fbuf.f_flag & FWRITE ? " write" : "",  /* print the file flag */
		fbuf.f_flag & FAPPEND ? " appen" : "",
		fbuf.f_flag & FSYNC ? " sync" : "",
		fbuf.f_flag & FCREAT ? " creat" : "",
		fbuf.f_flag & FTRUNC ? " trunc" : "",
		fbuf.f_flag & FEXCL ? " excl" : "",
		fbuf.f_flag & FNDELAY ? " ndelay" : "");

	if (!filefull)
		return;

	/* user credentials */
	if (!Ngrps)
		if (!(Ngrps = symsrch("ngroups_max")))
			error("ngroups_max not found in symbol table\n");
	readmem(Ngrps->st_value, 1, -1, (char *)&ngrpbuf,
		sizeof (ngrpbuf), "max groups");

	credbufp = (struct cred *)
		malloc(sizeof (struct cred) + sizeof (uid_t) * (ngrpbuf-1));

	readmem((unsigned)fbuf.f_cred, 1, -1, (char *)credbufp,
		sizeof (struct cred) + sizeof (uid_t) * (ngrpbuf-1),
		"user cred");

	fprintf(fp, "User Credential:\n");
	fprintf(fp, "\trcnt:%d,  uid:%d,  gid:%d,  ruid:%d,",
		credbufp->cr_ref,
		credbufp->cr_uid,
		credbufp->cr_gid,
		credbufp->cr_ruid);
	fprintf(fp, "  rgid:%d,  ngroup:%d",
		credbufp->cr_rgid,
		credbufp->cr_ngroups);
	for (i = 0; i < (short)credbufp->cr_ngroups; i++) {
		if (!(i % 4))
			fprintf(fp, "\n");
		fprintf(fp, "group[%d]:%4d ", i, credbufp->cr_groups[i]);
	}
	fprintf(fp, "\n");
}
