/*
 * Copyright (c) 1990-1994 by Sun Microsystems, Inc.
 */

#ifndef DNODE_H
#define	DNODE_H

#pragma ident	"@(#)dnode.h	1.10	94/08/10 SMI"

#include <sys/types.h>

struct dnode {
	u_long	dn_mode;		/* from stat(2) */
	u_long	dn_uid;			/* from stat(2) */
	u_long	dn_gid;			/* from stat(2) */
	long	dn_size;		/* from stat(2) */
	long	dn_atime;		/* from stat(2) */
	long	dn_mtime;		/* from stat(2) */
	long	dn_ctime;		/* from stat(2) */
	long	dn_blocks;		/* from stat(2) */
	u_long	dn_flags;		/* defines below */
	u_long	dn_filename;		/* index in path_name file */
	u_long	dn_parent;		/* index (this file) of parent d-node */
	u_long	dn_volid;		/* index to header file tape vol id */
	u_long	dn_vol_position;	/* location of file on backup media */
	u_long	dn_inode;		/* inode of this file in dump */
};

/*
 * symlink values are constrained to be MAXPATHLEN long, thus we don't
 * need to keep a count of data blocks occupied for them.  Instead,
 * we overlay the blocks field as follows:
 *
 *	in a batch_update file `dn_symlink' contains the length of
 *	the symlink data string (which immediately follows the dnode
 *	in the update file)
 *
 *	in a dnode file, `dn_symlink' contains an offset into the file
 *	`links.dumpid' telling where to obtain the value of the symlink
 */
#define	dn_symlink	dn_blocks

/*
 * values for dn_flags
 */
#define	DN_ACTIVE	0x00000001	/* file was changing while dumped */
#define	DN_MULTITAPE	0x00000002	/* file spans tapes */
#define	DN_OFFSET	0x00000004	/*
					 * offsets specified in
					 * dn_vol_position are valid
					 * 1024-byte block offsets
					 */
#define	DN_ACL		0x00000008	/* file has an ACL
					 * note that the mode bits are
					 * already stripped to owner-only,
					 * so this bit is just FYI in case
					 * we want to know about it in the
					 * future.
					 */

#define	NULL_DNODE		(struct dnode *)0
#endif
