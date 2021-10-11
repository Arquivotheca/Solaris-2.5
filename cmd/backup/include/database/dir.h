
/*	@(#)dir.h 1.4 92/05/07	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*
 * data structures for the database 'directory' file
 */
#ifndef DIR_H
#define	DIR_H

#include <sys/types.h>

#define	DIR_BLKSIZE		512

#define	DIRBLOCK_DATASIZE	(DIR_BLKSIZE - sizeof (struct dir_block_head))

/*
 * the header info for a block
 */
struct dir_block_head {
	u_long	dbh_next;	/* # of next blk for this dir */
	u_short	dbh_flags;	/* used during free space reclaim */
	u_short	dbh_spaceavail;	/* unused bytes at end of this blk's data */
};

/*
 * A block of the directory file
 */
struct dir_block {
	struct	dir_block_head	dbh;		/* as defined above... */
#define	db_next		dbh.dbh_next		/* next blk for this dir */
#define	db_flags	dbh.dbh_flags
#define	db_spaceavail	dbh.dbh_spaceavail	/* unused bytes in 'db_data' */
	char    db_data[DIRBLOCK_DATASIZE];	/* array of dir_entry */
};

/*
 * an entry in a directory block
 */
struct dir_entry {
	u_long	de_instances;	/* first idx in 'instances' for this file */
	u_long	de_directory;	/* blknum of subdirectory */
	u_short	de_name_len;	/* length of name field */
	char	de_name[1];	/* null terminated variable length name */
};

#define	NULL_DIRBLK		(struct dir_block *)0
#define	NULL_DIRENTRY		(struct dir_entry *)0
#define	DIR_FREEBLK		(u_long)0	/* list of free blocks */
#define	DIR_ROOTBLK		(u_long)1	/* first data block */

#define	DE_NAMEALIGN 		3
#define	DE_NAMELEN(i)		((i + DE_NAMEALIGN) & ~DE_NAMEALIGN)

#define	DE_END(dbp)	(struct dir_entry *) \
	(&(dbp)->db_data[DIRBLOCK_DATASIZE - (dbp)->db_spaceavail])
#define	DE_NEXT(dep)	(struct dir_entry *) \
	((unsigned)(dep)+sizeof (struct dir_entry)+\
	DE_NAMELEN((dep)->de_name_len))

#ifdef __STDC__
extern int dir_open(const char *);
extern int dir_newsubdir(u_long, struct dir_entry *);
extern int dir_add_instance(u_long, struct dir_entry *, u_long);
extern void dir_close(const char *);
extern void dir_dirtyblock(u_long);
extern int dir_trans(const char *);
extern struct dir_entry *dir_name_getblock(u_long *, struct dir_block **,
	const char *, int);
extern struct dir_entry *dir_addent(u_long *, struct dir_block **,
	const char *, int, u_long);
extern struct dir_block *dir_getblock(u_long);
#else
extern int dir_open();
extern int dir_newsubdir();
extern int dir_add_instance();
extern void dir_close();
extern void dir_dirtyblock();
extern int dir_trans();
extern struct dir_entry *dir_name_getblock();
extern struct dir_entry *dir_addent();
extern struct dir_block *dir_getblock();
#endif
#endif
