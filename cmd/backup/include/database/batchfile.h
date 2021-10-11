/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*	@(#)batchfile.h 1.3 92/03/11 */

#include <config.h>

/*
 * a batch_update file is structured as follows:
 *
 *	file header record
 *	dump_header record	"header.h"
 *	bu_name record
 *	ascii name
 *	bu_name record
 *	ascii name
 *	.
 *	.
 *	.
 *	dnode record		"dnode.h"
 *	dnode record
 *	.
 *	.
 *	.
 *	bu_tape record
 *	bu_tape record
 *	.
 *	.
 *	.
 *	file header record (duplicate of the one at the beginning)
 *
 * The name records occur in directory order, i.e., a directory
 * record (with type == 1) is immediately followed by all of the
 * records for its entries.  However, no hierarchical relationship
 * among directories can be inferred from the order of the entries.
 * The root directory is always assumed to
 * be the first name entry in the file (and/or inode number 2).
 *
 * XXX: note that the mount point in the dump header record is critical
 * for building correct path names.
 */
#ifndef BATCHFILE_H
#define	BATCHFILE_H

#include <rpc/rpc.h>			/* for vnode.h */
#include <sys/time.h>			/* for vnode.h */
#include <sys/vnode.h>			/* for inode.h */
#ifdef USG
#include <netdb.h>			/* for MAXHOSTNAMELEN */
#include <sys/fs/ufs_inode.h>		/* for dumprestore.h */
#else
#include <sys/param.h>			/* for MAXHOSTNAMELEN */
#include <ufs/inode.h>			/* for dumprestore.h */
#endif
#include <protocols/dumprestore.h>	/* for LBLSIZE */

struct bu_header {
	char	dbhost[BCHOSTNAMELEN];	/* db server host */
	u_long	name_cnt;	/* number of name entries in the file */
	u_long	dnode_cnt;	/* number of dnode entries in the file */
	u_long	tape_cnt;	/* number of tape entries in the file */
};

struct bu_name {
	u_long	inode;		/* inode number of this file */
	u_char	type;		/* directory or not */
	u_short	namelen;	/* length of null-terminated name */
};
#define	DIRECTORY	1

struct bu_tape {
	char	label[LBLSIZE];	/* tape label */
	u_long	first_inode;		/* 1st file on this tape */
	u_long	last_inode;		/* last file on this tape */
	u_long	filenum;		/* file number of this dump on tape */
};

#ifdef __STDC__
extern bool_t xdr_bu_header(XDR *, struct bu_header *);
extern bool_t xdr_bu_name(XDR *, struct bu_name *);
extern bool_t xdr_bu_tape(XDR *, struct bu_tape *);

extern int update_start(const char *, const char *);
extern int update_data(int, const char *);
extern int update_process(int);
extern int delete_bytape(const char *, const char *);
#else
extern bool_t xdr_bu_header();
extern bool_t xdr_bu_name();
extern bool_t xdr_bu_tape();

extern int update_start();
extern int update_data();
extern int update_process();
extern int delete_bytape();
#endif
#endif
