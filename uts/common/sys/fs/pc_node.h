/*
 * Copyright (c) 1989, 1992 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_FS_PC_NODE_H
#define	_SYS_FS_PC_NODE_H

#pragma ident	"@(#)pc_node.h	1.17	94/08/12 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <vm/page.h>

struct pcnode {
	struct pcnode *pc_forw;		/* active list ptrs, must be first */
	struct pcnode *pc_back;
	int pc_flags;			/* see below */
	struct vnode * pc_vn;		/* vnode for pcnode */
	long pc_size;			/* size of file */
	pc_cluster_t pc_scluster;	/* starting cluster of file */
	daddr_t pc_eblkno;		/* disk blkno for entry */
	int pc_eoffset;			/* offset in disk block of entry */
	struct pcdir pc_entry;		/* directory entry of file */
};

/*
 * flags
 */
#define	PC_MOD		0x01		/* file data has been modified */
#define	PC_CHG		0x02		/* node data has been changed */
#define	PC_INVAL	0x04		/* node is invalid */
#define	PC_EXTERNAL	0x08		/* vnode ref is held externally */
#define	PC_RELEHOLD	0x80		/* node is being released */

#define	PCTOV(PCP)	((PCP)->pc_vn)
#define	VTOPC(VP)	((struct pcnode *)((VP)->v_data))

#define	PRIPCNODE	(PZERO - 1)

/*
 * Make a unique integer for a file
 */
#define	pc_makenodeid(BN, OFF, EP, ENTPS) \
	(ino_t) ((EP)->pcd_attr & PCA_DIR? \
		-ltohs((EP)->pcd_scluster) - 1: \
		((BN) * (ENTPS)) + ((OFF) / sizeof (struct pcdir)))

#define	NPCHASH 1

#if NPCHASH == 1
#define	PCFHASH(FSP, BN, O)	0
#define	PCDHASH(FSP, SC)	0
#else
#define	PCFHASH(FSP, BN, O)	(((unsigned)FSP + BN + O) % NPCHASH)
#define	PCDHASH(FSP, SC)	(((unsigned)FSP + SC) % NPCHASH)
#endif

struct pchead {
	struct pcnode *pch_forw;
	struct pcnode *pch_back;
};

/*
 * pcnode file, directory and invalid node operations vectors
 */
extern struct vnodeops pcfs_fvnodeops;
extern struct vnodeops pcfs_dvnodeops;
extern struct pchead pcfhead[];
extern struct pchead pcdhead[];

/*
 * pcnode routines
 */
extern void pc_init(void);
extern struct pcnode *pc_getnode(struct pcfs *, daddr_t, int, struct pcdir *);
/* release a pcnode */
extern void pc_rele(struct pcnode *);
/* mark pcnode with current time */
extern void pc_mark(struct pcnode *);
/* flush node blocks and update entry */
extern int pc_nodesync(struct pcnode *);
/* update node directory entry */
extern int pc_nodeupdate(struct pcnode *);
/* map logical to physical blocks */
extern int pc_bmap(struct pcnode *, daddr_t, daddr_t *, u_int *);

/* allocate blocks for a file */
extern int pc_balloc(struct pcnode *, daddr_t, int, daddr_t *);
/* free blocks for a file */
extern int pc_bfree(struct pcnode *, pc_cluster_t);
/* verify a pcnode with the disk */
extern int pc_verify(struct pcfs *);
/* call when disk change is detected */
extern void pc_diskchanged(struct pcfs *);
/* call when disk change is detected */
extern void pc_gldiskchanged(struct pcfs *);

extern int pc_dirlook(struct pcnode *, char *, struct pcnode **);
extern int pc_direnter(struct pcnode *, char *, struct vattr *,
	struct pcnode **);
extern int pc_dirremove(struct pcnode *, char *, struct vnode *, enum vtype);
extern int pc_rename(struct pcnode *, char *, char *);
extern int pc_blkatoff(struct pcnode *, long offset, struct buf **,
	struct pcdir **);
extern int pc_truncate(struct pcnode *, long);
extern int pc_fileclsize(struct pcfs *, pc_cluster_t);
extern int pcfs_putapage(struct vnode *, page_t *, u_int *, u_int *, int,
	struct cred *);
extern void pc_badfs(struct pcfs *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_PC_NODE_H */
