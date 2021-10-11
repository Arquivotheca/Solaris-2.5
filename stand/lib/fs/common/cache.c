/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)cache.c	1.4	94/12/21 SMI"

#include <sys/param.h>
#include <sys/vnode.h>
#include <sys/fs/ufs_fsdir.h>
#include <sys/fs/ufs_fs.h>
#include <sys/fs/ufs_inode.h>
#include <sys/sysmacros.h>
#include <sys/promif.h>
#include <sys/filep.h>

#define	NULL 0

extern int diskread(fileid_t *filep);

/* inode cache */

typedef struct in_cache {
	struct	in_cache	*ic_next;
	ino_t			ic_num;
	struct	inode		*ic_inode;
	u_int			ic_usage;
} ic_t;

/* directory cache */

typedef struct dir_cache {
	struct	dir_cache	*dc_next;
	char	*dc_name;
	ino_t	dc_ino_num;
	ino_t	dc_pino_num;	/* inode of parent */
	int	dc_len;
} dc_t;

ic_t *ic_head, *ic_tail;
dc_t *dc_head, *dc_tail;

typedef struct ic_hash {
	struct 	ic_hash	*ich_next;
	ic_t	*ich_icp;
} ich_t;

typedef struct dc_hash {
	struct dc_hash *dch_next;
	dc_t	*dch_dcp;
} dch_t;

#define	MAX_CACHE	64

ich_t *ich[MAX_CACHE];
dch_t *dch[MAX_CACHE];

/*
 * Empirically derived,  we typically cache about 37 inodes in a multi-user
 * boot.  Of course they could all end with the same 6 bits! I hope not.
 */
#define	IC_HASH(x)	(x & (MAX_CACHE - 1))

/*
 * Ripped off from dnlc.c in the kernel.  It must be right!
 */
#define	DC_HASH(namep, namelen)    \
	((namep[0] + namep[namelen-1] + namelen) & (MAX_CACHE-1))

typedef struct db_cache {
	struct db_cache	*db_next;
	caddr_t		db_block;
	u_int		db_blocknum;
	u_int		db_size;
} db_t;

db_t *db_head, *db_tail;

int icache_num, ic_cache_hit, icache_size;
int dcache_num, dc_cache_hit, dcache_size;
int db_cache_num, db_cache_hit, db_cache_size;

int read_opt;

/*
 *	Given an inode number, look through the cache to see if we
 *	already have it in memory.
 */
struct inode *
get_icache(ino_t inode)
{
	ich_t *ichp;

	for (ichp = ich[IC_HASH(inode)]; ichp; ichp = ichp->ich_next) {
		if (ichp->ich_icp->ic_num == inode) {
			ic_cache_hit++;
			return (ichp->ich_icp->ic_inode);
		}
	}
	return ((struct inode *)(0));
}

/* Get a block from the cache.  Really need to hash this. */

caddr_t
get_db_cache(int block, int size)
{
	db_t *dbp;


	for (dbp = db_head; dbp; dbp = dbp->db_next) {
		if (block == dbp->db_blocknum &&
		    size == dbp->db_size) {
			db_cache_hit++;
			return (dbp->db_block);
		}
	}
	return ((caddr_t)(0));
}

/* Cache the inode passed in. */
void
set_icache(struct inode *ip, ino_t inum)
{
	ic_t *icp;
	ich_t *ichp, *ichp0;


	icp = (ic_t *)bkmem_alloc(sizeof (ic_t));
	if (ic_tail == NULL)
		ic_head = icp;
	else
		ic_tail->ic_next = icp;
	ic_tail = icp;
	icache_num++;
	bzero(icp, sizeof (ic_t));

	icp->ic_inode = ip;
	icp->ic_num = inum;
	icp->ic_usage++;
	ichp0 = (ich_t *)bkmem_alloc(sizeof (ich_t));
	ichp0->ich_icp = icp;
	ichp0->ich_next = NULL;
	if (ich[IC_HASH(inum)] == NULL) {
		ich[IC_HASH(inum)] = ichp0;
	} else {
		for (ichp = ich[IC_HASH(inum)]; ichp->ich_next != NULL;
		    ichp = ichp->ich_next)
			;
		ichp->ich_next = ichp0;
	}
}

/* Read a disk block and put it into the disk block cache */
int
set_dbcache(fileid_t *filep)
{

	db_t *dbp;

	dbp = (db_t *)bkmem_alloc(sizeof (db_t));
	dbp->db_block = (caddr_t)bkmem_alloc(filep->fi_count);
	filep->fi_memp = dbp->db_block;
	dbp->db_blocknum = 0;
	dbp->db_next = 0;
	if (diskread(filep)) {
		printf("Read error.\n");
		return (-1);
	}
	dbp->db_blocknum = filep->fi_blocknum;
	dbp->db_size = filep->fi_count;
	if (db_tail == NULL)
		db_head = dbp;
	else
		db_tail->db_next = dbp;
	db_tail = dbp;
	db_cache_num++;
	return (0);
}

/* Look for a directory entry in the cache, if found return the its inode num */
ino_t
get_dcache(char *name, int len, ino_t inode_num)
{
	dc_t *dcp;
	dch_t *dchp, dchp0;
	int hash;

	for (dchp = dch[DC_HASH(name, len)]; dchp;
	    dchp = dchp->dch_next) {
		dcp = dchp->dch_dcp;
		if (dcp->dc_pino_num == inode_num &&
		    dcp->dc_len == len &&
		    *dcp->dc_name == *name &&
		    strcmp(dcp->dc_name, name) == 0) {
			dc_cache_hit++;
			return (dcp->dc_ino_num);
		}
	}
	return ((ino_t)0);
}

/* Place a directory entry into the cache */
void
set_dcache(char *path, int len, ino_t pin, ino_t in)
{

	dc_t *dcp;
	dch_t *dchp, *dchp0;
	int hash;

	dcp = (dc_t *)bkmem_alloc(sizeof (dc_t));
	dcp->dc_name = (char *)bkmem_alloc(len + 1);
	strcpy(dcp->dc_name, path);
	dcp->dc_pino_num = pin;
	dcp->dc_ino_num = in;
	dcp->dc_len = len;
	if (dc_tail == NULL)
		dc_head = dcp;
	else
		dc_tail->dc_next = dcp;
	dc_tail = dcp;
	dcp->dc_next = (dc_t *)0;
	dchp0 = (dch_t *)bkmem_alloc(sizeof (dch_t));
	dchp0->dch_dcp = dcp;
	dchp0->dch_next = NULL;
	hash = DC_HASH(path, len);
	if (dch[hash] == NULL) {
		dch[hash] = dchp0;
	} else {
		for (dchp = dch[hash];
		    dchp->dch_next != NULL;
		    dchp = dchp->dch_next)
			;
		dchp->dch_next = dchp0;
	}
	dcache_num++;
}

/*
 * Print and reset cumulative cache statistics
 */
void
print_cache_data(void)
{
	printf("boot: rootfs cache statistics:\n");
	printf("inode hits %d, ", ic_cache_hit);
	printf("inodes cached %d, ", icache_num);
	printf("inode cache size %d bytes\n", icache_size);
	printf("directory hits %d, ", dc_cache_hit);
	printf("dirents cached %d, ", dcache_num);
	printf("dirent cache size %d bytes\n", dcache_size);
	printf("optimized reads %d\n", read_opt);
	printf("block hits %d, ", db_cache_hit);
	printf("blocks cached %d, ", db_cache_num);
	printf("block cache size %d bytes\n", db_cache_size);

	ic_cache_hit = dc_cache_hit = db_cache_hit = 0;
	icache_num = dcache_num = db_cache_num = 0;
	icache_size = dcache_size = db_cache_size = 0;
	read_opt = 0;
}

/* Called from closeall() */

void
release_cache(void)
{
	ic_t *icp;
	dc_t *dcp;
	db_t *dbp;
	int i;

	icp = ic_head;
	while (icp) {
		ic_t *icp1 = icp->ic_next;

		icache_size += sizeof (struct inode) + sizeof (ic_t);
		bkmem_free(icp->ic_inode, sizeof (struct inode));
		bkmem_free(icp, sizeof (ic_t));

		icp = icp1;
	}
	ic_head = ic_tail = NULL;

	dcp = dc_head;
	while (dcp) {
		dc_t *dcp1 = dcp->dc_next;

		dcache_size += strlen(dcp->dc_name) + 1 + sizeof (dc_t);
		bkmem_free(dcp->dc_name, strlen(dcp->dc_name) + 1);
		bkmem_free(dcp, sizeof (dc_t));

		dcp = dcp1;
	}
	dc_head = dc_tail = NULL;

	dbp = db_head;
	while (dbp) {
		db_t *dbp1 = dbp->db_next;

		db_cache_size += dbp->db_size + sizeof (db_t);
		bkmem_free(dbp->db_block, dbp->db_size);
		bkmem_free(dbp, sizeof (db_t));

		dbp = dbp1;
	}
	db_head = db_tail = NULL;

	for (i = 0; i < MAX_CACHE; i++) {
		ich_t *ichp = ich[i];

		while (ichp) {
			ich_t *ichp1 =  ichp->ich_next;

			icache_size += sizeof (ich_t);
			bkmem_free(ichp, sizeof (ich_t));

			ichp = ichp1;
		}
		ich[i] = NULL;
	}

	for (i = 0; i < MAX_CACHE; i++) {
		dch_t *dchp = dch[i];
		while (dchp) {
			dch_t *dchp1 =  dchp->dch_next;

			dcache_size += sizeof (dch_t);
			bkmem_free(dchp, sizeof (dch_t));

			dchp = dchp1;
		}
		dch[i] = NULL;
	}
}
