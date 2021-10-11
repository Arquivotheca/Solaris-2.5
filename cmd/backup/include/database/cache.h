#ifndef CACHE_H
#define	CACHE_H

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*	@(#)cache.h 1.2 91/12/20 */

#include <stdio.h>
#include <sys/types.h>

struct cache_block {
	struct cache_block *nxt, *prv;		/* for lru/free list */
	struct cache_block *link, *rlink;	/* for hash chain */
	caddr_t	data;				/* pointer to data */
	u_long	flags;				/* see defines */
	u_long	blknum;				/* blknum in file */
};

/*
 * definitions for cache_block.flags
 */
#define	CACHE_ENT_VALID		0x00000001
#define	CACHE_ENT_DIRTY		0x00000002

struct block_head {
	struct cache_block *nxt, *prv;
	struct cache_block *link, *rlink;
};

struct cache_header {
	struct block_head *hb;			/* pointer to hash buckets */
	struct block_head lru;			/* lru list */
	struct block_head freelist;		/* free list */
	struct cache_block *blkdescrip;		/* one for each block */
	int	cache_size;			/* # of entries in cache */
	int	cache_blksize;			/* size of a memory block */
};

/*
 * misc definitions
 */
#define	NULL_CACHE_HEADER	(struct cache_header *)0
#define	NULL_CACHE_BLOCK	(struct cache_block *)0

#ifdef __STDC__
extern void cache_insert(struct cache_header *, struct cache_block *);
extern void cache_dirtyblock(struct cache_header *, u_long);
extern struct cache_header *cache_init(caddr_t,
			int, int, struct cache_header *);
extern struct cache_block *cache_getblock(struct cache_header *, u_long);
extern struct cache_block *cache_alloc_block(struct cache_header *);
#else
extern void cache_insert();
extern void cache_dirtyblock();
extern struct cache_header *cache_init();
extern struct cache_block *cache_getblock();
extern struct cache_block *cache_alloc_block();
#endif
#endif
