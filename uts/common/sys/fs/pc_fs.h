/*
 * Copyright (c) 1989 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_FS_PC_FS_H
#define	_SYS_FS_PC_FS_H

#pragma ident	"@(#)pc_fs.h	1.17	95/01/04 SMI"

#include <sys/thread.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef	unsigned short	pc_cluster_t;

/*
 * PC (MSDOS) compatible virtual file system.
 *
 * A main goal of the implementaion was to maintain statelessness
 * except while files are open. Thus mounting and unmounting merely
 * declared the file system name. The user may change disks at almost
 * any time without concern (just like the PC). It is assumed that when
 * files are open for writing the disk access light will be on, as a
 * warning not to change disks. The implementation must, however, detect
 * disk change and recover gracefully. It does this by comparing the
 * in core entry for a directory to the on disk entry whenever a directory
 * is searched. If a discrepancy is found active directories become root and
 * active files are marked invalid.
 *
 * There are only two type of nodes on the PC file system; files and
 * directories. These are represented by two separate vnode op vectors,
 * and they are kept in two separate tables. Files are known by the
 * disk block number and block (cluster) offset of the files directory
 * entry. Directories are known by the starting cluster number.
 *
 * The file system is locked for during each user operation. This is
 * done to simplify disk verification error conditions.
 */

struct pcfs {
	struct vfs *pcfs_vfs;		/* vfs for this fs */
	int pcfs_flags;			/* flags */
	int pcfs_ldrv;			/* logical DOS drive number */
	dev_t pcfs_xdev;		/* actual device that is mounted */
	struct vnode *pcfs_devvp;	/*   and a vnode for it */
	int pcfs_secsize;		/* sector size in bytes */
	int pcfs_spcl;			/* sectors per cluster */
	int pcfs_spt;			/* sectors per track */
	int pcfs_sdshift;		/* shift to convert sector into */
					/* DEV_BSIZE "sectors"; assume */
					/* pcfs_secsize is 2**n times of */
					/* DEV_BSIZE */
	int pcfs_fatsec;		/* number of sec per FAT */
	int pcfs_numfat;		/* number of FAT copies */
	int pcfs_rdirsec;		/* number of sec in root dir */
	daddr_t pcfs_dosstart;		/* start blkno of DOS partition */
	daddr_t pcfs_fatstart;		/* start blkno of first FAT */
	daddr_t pcfs_rdirstart;		/* start blkno of root dir */
	daddr_t pcfs_datastart;		/* start blkno of data area */
	int pcfs_clsize;		/* cluster size in bytes */
	int pcfs_ncluster;		/* number of clusters in fs */
	int pcfs_entps;			/* number of dir entry per sector */
	int pcfs_nrefs;			/* number of active pcnodes */
	int pcfs_frefs;			/* number of active file pcnodes */
	int pcfs_nxfrecls;		/* next free cluster */
	struct buf *pcfs_fatbp;		/* ptr to FAT buffer */
	u_char *pcfs_fatp;		/* ptr to FAT data */
	time_t pcfs_fattime;		/* time FAT becomes invalid */
	time_t pcfs_verifytime;		/* time to reverify disk */
	kmutex_t	pcfs_lock;		/* per filesystem lock */
	kthread_id_t pcfs_owner;		/* id of thread locking pcfs */
	int pcfs_count;			/* # of pcfs locks for pcfs_owner */
	struct pcfs *pcfs_nxt;		/* linked list of all mounts */
};

/*
 * flags
 */
#define	PCFS_FATMOD	0x01		/* FAT has been modified */
#define	PCFS_LOCKED	0x02		/* fs is locked */
#define	PCFS_WANTED	0x04		/* locked fs is wanted */
#define	PCFS_FAT16	0x400		/* 16 bit FAT */
#define	PCFS_NOCHK	0x800		/* don't resync fat on error */

#define	PCFS_PCMCIA_NO_CIS 0x4000	/* PCMCIA psuedo floppy */

struct pcfs_args {
	int	secondswest;	/* seconds west of Greenwich */
	int	dsttime;    	/* type of dst correction */
};

/*
 * Disk timeout value in sec.
 * This is used to time out the in core FAT and to re-verify the disk.
 * This should be less than the time it takes to change floppys
 */
#define	PCFS_DISKTIMEOUT	2

#define	VFSTOPCFS(VFSP)		((struct pcfs *)((VFSP)->vfs_data))
#define	PCFSTOVFS(FSP)		((FSP)->pcfs_vfs)

/*
 * special cluster numbers in FAT
 */
#define	PCF_FREECLUSTER		0x00	/* cluster is available */
#define	PCF_ERRORCLUSTER	0x01	/* error occurred allocating cluster */
#define	PCF_12BCLUSTER		0xFF0	/* 12-bit version of reserved cluster */
#define	PCF_RESCLUSTER		0xFFF0	/* 16-bit version of reserved cluster */
#define	PCF_BADCLUSTER		0xFFF7	/* bad cluster, do not use */
#define	PCF_LASTCLUSTER		0xFFF8	/* >= means last cluster in file */
#define	PCF_FIRSTCLUSTER	2	/* first valid cluster number */

/*
 * file system constants
 */
#define	PC_MAXFATSEC	256		/* maximum number of sectors in FAT */

/*
 * file system parameter macros
 */
#define	pc_blksize(PCFS, PCP, OFF)	/* file system block size */ \
	(PCTOV(PCP)->v_flag & VROOT? \
	    ((OFF) >= \
	    ((PCFS)->pcfs_rdirsec & \
	    ~((PCFS)->pcfs_spcl - 1)) * ((PCFS)->pcfs_secsize)? \
	    ((PCFS)->pcfs_rdirsec & \
	    ((PCFS)->pcfs_spcl - 1)) * ((PCFS)->pcfs_secsize): \
	    (PCFS)->pcfs_clsize): \
	    (PCFS)->pcfs_clsize)

#define	pc_blkoff(PCFS, OFF)		/* offset within block */ \
	((OFF) & ((PCFS)->pcfs_clsize - 1))

#define	pc_lblkno(PCFS, OFF)		/* logical block (cluster) no */ \
	((daddr_t)((OFF) / (PCFS)->pcfs_clsize))

#define	pc_dbtocl(PCFS, DB)		/* disk blks to clusters */ \
	((int)((DB) / (PCFS)->pcfs_spcl))

#define	pc_cltodb(PCFS, CL)		/* clusters to disk blks */ \
	((daddr_t)((CL) * (PCFS)->pcfs_spcl))

#define	pc_cldaddr(PCFS, CL)	/* DEV_BSIZE "sector" addr for cluster */ \
	(((daddr_t)((PCFS)->pcfs_datastart + \
	    ((CL) - PCF_FIRSTCLUSTER) * (PCFS)->pcfs_spcl)) << \
	    (PCFS)->pcfs_sdshift)

#define	pc_daddrcl(PCFS, DADDR)		/* cluster for disk address */ \
	((int)(((((DADDR) >> (PCFS)->pcfs_sdshift) - (PCFS)->pcfs_datastart) / \
	(PCFS)->pcfs_spcl) + 2))

#define	pc_dbdaddr(PCFS, DB)	/* sector to DEV_BSIZE "sector" addr */ \
	((DB) << (PCFS)->pcfs_sdshift)

#define	pc_daddrdb(PCFS, DADDR)	/* DEV_BSIZE "sector" addr to sector addr */ \
	((DADDR) >> (PCFS)->pcfs_sdshift)

#define	pc_validcl(PCFS, CL)		/* check that cluster no is legit */ \
	((int) (CL) >= PCF_FIRSTCLUSTER && \
	    (int) (CL) < (PCFS)->pcfs_ncluster + PCF_FIRSTCLUSTER)

/*
 * external routines.
 */
extern int pc_lockfs(struct pcfs *);	/* lock fs and get fat */
extern void pc_unlockfs(struct pcfs *);	/* ulock the fs */
extern int pc_getfat(struct pcfs *);	/* get fat from disk */
extern void pc_invalfat(struct pcfs *);	/* invalidate incore fat */
extern int pc_syncfat(struct pcfs *);	/* sync fat to disk */
extern int pc_freeclusters(struct pcfs *);	/* num free clusters in fs */
extern pc_cluster_t pc_alloccluster
	(struct pcfs *, int); 		/* allocate a new cluster */
extern void pc_setcluster		/* set the next cluster FAT */
	(struct pcfs *, pc_cluster_t, pc_cluster_t);

/*
 * debugging
 */
extern void prom_printf(char *fmt, ...);
extern int pcfsdebuglevel;
#define	PCFSDEBUG(X)  if (X <= pcfsdebuglevel)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_PC_FS_H */
