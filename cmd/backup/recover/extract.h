
/*	@(#)extract.h 1.7 92/04/15	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*
 * definitions for file extraction (restoration)
 */
#ifndef EXTRACT_H
#define	EXTRACT_H

/*
 * files in the list are hashed by name and by inode.
 * This makes it easy to check for duplicate names, as well as
 * noting files on the extract list when a `ls' command is done.
 */
struct hash_head {
	struct file_list *nxt_name;
	struct file_list *prv_name;
	struct file_list *nxt_inode;
	struct file_list *prv_inode;
};

struct file_list {
	struct	file_list	*nxt_name;	/* hash list - by name */
	struct	file_list	*prv_name;	/* hash list - by name */
	struct	file_list	*nxt_inode;	/* hash list - by inode */
	struct	file_list	*prv_inode;	/* hash list - by inode */
	struct	file_list	*nxt_file;	/* linked list: inode order */
	struct	file_list	*prv_file;	/* linked list: inode order */
	u_long	file_inode;			/* inode number on tape */
	u_long	file_offset;			/* offset into media */
	char	*name;				/* name for restored copy */
	struct	dirstats	*dirp;		/* info for restored dir */
	struct	file_list	*lnkp;		/* list of links/copies */
	int	flags;				/* defines follow */
};
#define	NULL_FILELIST	(struct file_list *)0
#define	F_RENAMED	1

/*
 * hard links are kept on the extract list as a chain of file_list
 * structures off of `lnkp'.  Links are chained together using
 * the `nxt_inode' pointer.  In the `file_inode' field, a link keeps
 * one of the following flags:
 */
#define	LL_LINK		1
#define	LL_COPY		2

struct dump_list {
	struct	dump_list 	*nxt_dumplist;	/* linked list - next dump */
	u_long	dumpid;				/* the database dumpid */
	int	dump_pos;			/* position on tape */
	time_t	dump_time;			/* time of dump */
	int	file_count;			/* # extract from this dump */
	struct	file_list	dir_list;	/* dirs to restore */
	struct	file_list	file_list;	/* files to restore */
	struct	hash_head	*inohash;
};
#define	NULL_DUMPLIST	(struct dump_list *)0
#define	INODE_HASHSIZE	16384
#define	INODE_HASH(num)	(num & (INODE_HASHSIZE-1))

struct tape_list {
	struct tape_list *nxt_tapelist;		/* linked list - next tape */
	char	label[LBLSIZE];			/* label of this tape */
	struct dump_list *dump_list;		/* first dump on this tape */
	int	flags;				/* defines below */
};
#define	NULL_TAPELIST	(struct tape_list *)0
#define	TAPE_FILESPAN	1			/* continued from prv tape */
#define	TAPE_ORDERED	2			/* don't change nxt ptr    */
#endif
