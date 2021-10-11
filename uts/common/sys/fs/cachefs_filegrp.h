/*
 * Copyright (c) 1992, Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_FS_CACHEFS_FILEGRP_H
#define	_SYS_FS_CACHEFS_FILEGRP_H

#pragma ident	"@(#)cachefs_filegrp.h	1.12	94/08/02 SMI"

#ifdef __cplusplus
extern "C" {
#endif

struct cachefs_metadata;

/*
 * filegrp structure represents a group of front files.
 */
struct filegrp {
	u_int			 fg_flags;	/* CFS_FS_* flags */
	int			 fg_count;	/* cnodes in group */
	ino_t			 fg_fileno;	/* starting fileno in group */
	struct fscache		*fg_fscp;	/* back ptr to fscache */

	struct filegrp		*fg_next;	/* pointer to next */
	struct vnode		*fg_dirvp;	/* filegrp directory vp */
	struct vnode		*fg_attrvp;	/* attrcache vp */
	struct attrcache_header	*fg_header;	/* Attrcache header */
	struct attrcache_index	*fg_offsets;	/* ptr to indexes in header */
	u_char			*fg_alloclist;	/* allocation bitmap */

	int			 fg_headersize;	/* attrcache header size */
	int			 fg_filesize;	/* size of attrcache file */
	kmutex_t		 fg_mutex;	/* filegrp contents/ac lock */
	kmutex_t		 fg_gc_mutex;	/* gc/inactive/lookup lock */
};
typedef struct filegrp filegrp_t;

/* fg_flags values */
#define	CFS_FG_NOCACHE		0x1	/* no cache mode */
#define	CFS_FG_ALLOC_ATTR	0x2	/* no attrcache file yet */
#define	CFS_FG_UPDATED		0x4	/* attrcache modified */
#define	CFS_FG_ALLOC_FILE	0x10	/* no front file dir yet */
#define	CFS_FG_LRU		0x20	/* attr file is on the lru list */
#define	CFS_FG_READ		0x40	/* attrcache can be read */
#define	CFS_FG_WRITE		0x80	/* attrcache can be written */

filegrp_t *filegrp_create(struct fscache *fscp, ino_t fileno);
void filegrp_destroy(filegrp_t *fgp);
int filegrp_allocattr(filegrp_t *fgp);
void filegrp_hold(filegrp_t *fgp);
void filegrp_rele(filegrp_t *fgp);
int filegrp_ffhold(filegrp_t *fgp);
void filegrp_ffrele(filegrp_t *fgp);

int filegrp_sync(filegrp_t *fgp);
int filegrp_read_metadata(filegrp_t *fgp, ino_t fileno,
    struct cachefs_metadata *mdp);
int filegrp_create_metadata(filegrp_t *fgp, struct cachefs_metadata *md,
    ino_t fileno);
int filegrp_write_metadata(filegrp_t *fgp, ino_t fileno,
    struct cachefs_metadata *mdp);
int filegrp_destroy_metadata(filegrp_t *fgp, ino_t fileno);
int filegrp_fileno_to_slot(filegrp_t *fgp, ino_t fileno);

filegrp_t *filegrp_list_find(struct fscache *fscp, ino_t fileno);
void filegrp_list_add(struct fscache *fscp, filegrp_t *fgp);
void filegrp_list_remove(struct fscache *fscp, filegrp_t *fgp);
void filegrp_list_gc(struct fscache *fscp);
void filegrp_list_enable_caching_ro(struct fscache *fscp);
void filegrp_list_enable_caching_rw(struct fscache *fscp);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_FS_CACHEFS_FILEGRP_H */
