#include <stdio.h>
#include <sys/types.h>

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*	@(#)cache.h 1.3 93/04/28 */

#define	CACHE_STATS
#ifdef CACHE_STATS
struct cache_stats {
	int hits;
	int misses;
	int searches;
	int maxsearch;
	int recycles;
	int dirty_recycles;
};
#endif

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
	u_long	flags;				/* see defines */
};

/*
 * definitions for cache_info.flags
 */
#define	CACHE_WRITE_THROUGH	0x00000001	/* otherwise write-back */

/*
 * misc definitions
 */
#define	NULL_CACHE_HEADER	(struct cache_header *)0
#define	NULL_CACHE_BLOCK	(struct cache_block *)0

#ifdef __STDC__
extern void free_cache(struct cache_header *);
extern struct cache_header *
	cache_init(caddr_t, int, int, struct cache_header *, int);
extern struct cache_block *cache_getblock(struct cache_header *, u_long);
extern struct cache_block *cache_alloc_block(struct cache_header *, u_long);
extern void cache_release(struct cache_header *, struct cache_block *);
#ifdef CACHE_STATS
extern void cache_stats(void);
#endif
#else	/* !__STDC__ */
extern void free_cache();
extern struct cache_header *cache_init();
extern struct cache_block *cache_getblock();
extern struct cache_block *cache_alloc_block();
extern void cache_release();
#ifdef CACHE_STATS
extern void cache_stats();
#endif
#endif
