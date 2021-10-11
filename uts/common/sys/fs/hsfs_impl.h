/*
 * High Sierra filesystem internal routine definitions.
 * Copyright (c) 1989, 1990, 1993 by Sun Microsystem, Inc.
 */

#ifndef	_SYS_FS_HSFS_IMPL_H
#define	_SYS_FS_HSFS_IMPL_H

#pragma ident	"@(#)hsfs_impl.h	1.3	94/04/11 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * global routines.
 */

extern int hsfs_putapage(vnode_t *, page_t *, u_int *, u_int *, int, cred_t *);
/* read a sector */
extern int hs_readsector(struct vnode *vp, u_int secno,	u_char *ptr);
/* lookup/construct an hsnode/vnode */
extern struct vnode *hs_makenode(struct hs_direntry *dp,
	u_int lbn, u_int off, struct vfs *vfsp);
/* make hsnode from directory lbn/off */
extern int hs_remakenode(u_int lbn, u_int off, struct vfs *vfsp,
	struct vnode **vpp);
/* lookup name in directory */
extern int hs_dirlook(struct vnode *dvp, char *name, int namlen,
	struct vnode **vpp, struct cred *cred);
/* find an hsnode in the hash list */
extern struct vnode *hs_findhash(u_long nodeid, struct vfs *vfsp);
/* destroy an hsnode */
extern void hs_freenode(struct hsnode *hp, struct vfs *vfsp, int nopage);
/* destroy the incore hnode table */
extern void hs_freehstbl(struct vfs *vfsp);
/* parse a directory entry */
extern int hs_parsedir(struct hsfs *fsp, u_char *dirp,
	struct hs_direntry *hdp, char *dnp, int *dnlen);
/* convert d-characters */
extern int hs_namecopy(char *from, char *to, int size, u_long flags);
/* destroy the incore hnode table */
extern void hs_filldirent(struct vnode *vp, struct hs_direntry *hdp);
/* check vnode protection */
extern int hs_access(struct vnode *vp, mode_t m, struct cred *cred);

extern int hs_synchash(struct vfs *vfsp);

extern void hs_parse_dirdate(u_char *dp, struct timeval *tvp);
extern void hs_parse_longdate(u_char *dp, struct timeval *tvp);
extern int hs_uppercase_copy(char *from, char *to, int size);
extern struct hstable *hs_inithstbl(struct vfs *vfsp);
extern void hs_log_bogus_disk_warning(struct hsfs *fsp, int errtype,
	u_int data);
extern int hsfs_valid_dir(struct hs_direntry *hd);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_HSFS_IMPL_H */
