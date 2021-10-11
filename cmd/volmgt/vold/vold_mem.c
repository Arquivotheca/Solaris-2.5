/*
 * Copyright (c) 1994 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)vold_mem.c	1.16	94/11/09 SMI"

/*
 * if DEBUG_MALLOC is not defined then we'll just use the
 *	one in libc (or libmalloc, ...)
 */
#ifdef	DEBUG_MALLOC

#include	<stdarg.h>
#include	<unistd.h>
#include	<stdlib.h>
#include	<errno.h>
#include	<libintl.h>

#include	"mallint.h"
#include	"multithread.h"
#include	"vold.h"


/*
 * vold_mem.c: malloc, realloc, and free.  If we are compiled for
 * LWP, we get a mutex before calling malloc, realloc, of free.
 * Yes, it's a really nasty implementation.
 */

static mutex_t	mem_mutex;

static u_int	nmemusers;

extern int	errno;

static void	*real_malloc(size_t);
static void	*real_realloc(void *, size_t);
static void	*real_memalign(size_t, size_t);
static void	real_free(void *);



void *
malloc(size_t size)
{
	int	err;
	void	*ret;


	if (size == 0) {
		return (NULL);
	}

	/*CONSTCOND*/
	while (1) {
		(void) mutex_enter(&mem_mutex);

		nmemusers++;
		ret = real_malloc(size);
		nmemusers--;

		(void) mutex_exit(&mem_mutex);

		if (ret) {
			break;
		}
		if (errno == ENOMEM) {
			(void) sleep(30);
		} else  {
			fatal(gettext("malloc failed with %m!\n"));
		}
	}
	return (ret);
}

void *
realloc(void *ptr, size_t size)
{
	int	err;
	void	*ret;

	if (ptr == NULL) {
		return (malloc(size));
	}

	/*CONSTCOND*/
	while (1) {
		(void) mutex_enter(&mem_mutex);

		nmemusers++;
		ret = real_realloc(ptr, size);
		nmemusers--;

		(void) mutex_exit(&mem_mutex);

		if (ret) {
			break;
		}
		if (errno == ENOMEM) {
			(void) sleep(30);
		} else {
			fatal(gettext("realloc failed with %m!\n"));
		}
	}
	return (ret);
}

void
free(void *ptr)
{
	int	err;


	if (ptr == NULL) {
		return;
	}

	(void) mutex_enter(&mem_mutex);

	nmemusers++;
	real_free(ptr);
	nmemusers--;

	(void) mutex_exit(&mem_mutex);
}

void *
valloc(size_t size)
{
	extern int		_sysconf(int);

	static unsigned int	pagesize = 0;



	if (pagesize == 0) {
		pagesize = (unsigned int)_sysconf(_SC_PAGESIZE);
	}
	return (memalign(pagesize, size));
}

void *
memalign(size_t alignment, size_t size)
{
	void 	*ret;
	int	err;


	if (size == 0) {
		return (NULL);
	}

	/*CONSTCOND*/
	while (1) {
		(void) mutex_enter(&mem_mutex);

		nmemusers++;
		ret = real_memalign(alignment, size);
		nmemusers--;

		(void) mutex_exit(&mem_mutex);

		if (ret) {
			break;
		}
		if (errno == ENOMEM) {
			(void) sleep(30);
		} else {
			fatal(gettext("memalign failed with %m!\n"));
		}
	}
	return (ret);
}

#define	S5EMUL

#if !defined(lint) && defined(SCCSIDS)
static	char sccsid[] = "@(#)malloc.c 1.42 89/07/07 SMI";
#endif

/*
 * Copyright (c) 1986 by Sun Microsystems, Inc.
 */

/*
 * take from file: malloc.c
 *
 * description:
 *	Yet another memory allocator, this one based on a method
 *	described in C.J. Stephenson, "Fast Fits"
 *
 *	The basic data structure is a "Cartesian" binary tree, in which
 *	nodes are ordered by ascending addresses (thus minimizing free
 *	list insertion time) and block sizes decrease with depth in the
 *	tree (thus minimizing search time for a block of a given size).
 *
 *	In other words: for any node s, let D(s) denote the set of
 *	descendents of s; for all x in D(left(s)) and all y in
 *	D(right(s)), we have:
 *
 *	a. addr(x) <  addr(s) <  addr(y)
 *	b. len(x)  <= len(s)  >= len(y)
 */

/* system interface */

#ifdef DEBUG_MALLOC_EXTRA
extern	char	*sbrk();
#endif

#ifdef DEBUG_MALLOC_EXTRA
extern	abort();
#endif
extern	int	errno;

static	int	nbpg = 4096;	/* halt-hack: !set by calling getpagesize() */
static	bool	morecore(uint);	/* get more memory into free space */

#ifdef	S5EMUL
#define	ptr_t		void *	/* ANSI C says these are voids */
#define	free_t		void	/* ANSI says void free(ptr_t ptr) */
#define	free_return(x)	return
#else
#define	ptr_t		char *	/* BSD still (4.3) wants char*'s */
#define	free_t		int	/* BSD says int free(ptr_t ptr) */
#define	free_return(x)	return (x)
#endif

/* SystemV-compatible information structure */
#define	INIT_MXFAST 0
#define	INIT_NLBLKS 100
#define	INIT_GRAIN ALIGNSIZ

struct	mallinfo __mallinfo = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			/* basic info */
	INIT_MXFAST, INIT_NLBLKS, INIT_GRAIN,	/* mallopt options */
	0, 0, 0
};

/* heap data structures */

Freehdr	_root	= NIL;			/* root of free space list */
char	*_lbound = NULL;		/* lower bound of heap */
char	*_ubound = NULL;		/* upper bound of heap */

/* free header list management */

static Freehdr	getfreehdr(void);
static void	putfreehdr(Freehdr);

static	Freehdr	freehdrptr = NIL;	/* ptr to block of available headers */
static	int	nfreehdrs = 0;		/* # of headers in current block */
static	Freehdr	freehdrlist = NIL;	/* List of available headers */

/* error checking -- set errno then prints msg if debug_level > 0 */
static void	m_error(const char *, ...);

int		malloc_debug(int);
int		malloc_verify(void);

/*
 * default level for malloc debugging (see debug_level below)
 */
#ifndef	DEFAULT_MALLOC_DEBUG_LEVEL
#define	DEFAULT_MALLOC_DEBUG_LEVEL	1
#endif
/*
 * debug_level - passed from calling routine(s) to set this packages
 *	debugging information output level.
 *
 *	value		meaning (each level includes the lower one)
 *	-----		-------------------------------------------
 *	  0		don't output any debugging information
 *	  1 (default)	output messages just in error cases
 *	  2		verify the heap on each package call
 *	  3		print msg each time heap is verified
 */
static	int debug_level = DEFAULT_MALLOC_DEBUG_LEVEL;

/*
 * A block with a negative size, a size that is not a multiple
 * of ALIGNSIZ, a size greater than the current extent of the
 * heap, or a size which extends beyond the end of the heap is
 * considered bad.
 */

#define	badblksize(p, size)	\
	(((size) < SMALLEST_BLK) || ((size) & (ALIGNSIZ-1)) || \
	    ((size) > heapsize()) || ((((char *)(p))+(size)) > _ubound))

/*
 * insert (newblk, len)
 *	Inserts a new node in the free space tree, placing it
 *	in the correct position with respect to the existing nodes.
 *
 * algorithm:
 *	Starting from the root, a binary search is made for the new
 *	node. If this search were allowed to continue, it would
 *	eventually fail (since there cannot already be a node at the
 *	given address); but in fact it stops when it reaches a node in
 *	the tree which has a length less than that of the new node (or
 *	when it reaches a null tree pointer).
 *
 *	The new node is then inserted at the root of the subtree for
 *	which the shorter node forms the old root (or in place of the
 *	null pointer).
 */

static void
insert(
    register Dblk newblk,		/* Ptr to the block to insert */
    register uint len)		/* Length of new node */
{
	register Freehdr *fpp;		/* Address of ptr to subtree */
	register Freehdr x;
	register Freehdr *left_hook;	/* Temp for insertion */
	register Freehdr *right_hook;	/* Temp for insertion */
	register Freehdr newhdr;

	/*
	 * check for bad block size.
	 */
	if (badblksize(newblk, len)) {
		m_error("insert: bad block size (%d) at %#x\n", len, newblk);
		return;
	}

	/*
	 * Search for the first node which has a weight less
	 *	than that of the new node; this will be the
	 *	point at which we insert the new node.
	 */
	fpp = &_root;
	x = *fpp;
	while (weight(x) >= len) {
		if (newblk < x->block) {
			fpp = &x->left;
		} else {
			fpp = &x->right;
		}
		x = *fpp;
	}

	/*
	 * Perform root insertion. The variable x traces a path through
	 *	the fpp, and with the help of left_hook and right_hook,
	 *	rewrites all links that cross the territory occupied
	 *	by newblk.
	 */

	if ((newhdr = getfreehdr()) == NIL) {
		/* Error message returned by getfreehdr() */
		return;
	}
	*fpp = newhdr;

	newhdr->left = NIL;
	newhdr->right = NIL;
	newhdr->block = newblk;
	newhdr->size = len;

	/*
	 * set length word in the block for consistency with the header.
	 */

	newblk->size = len;

	left_hook = &newhdr->left;
	right_hook = &newhdr->right;

	while (x != NIL) {
		/*
		 * Remark:
		 *	The name 'left_hook' is somewhat confusing, since
		 *	it is always set to the address of a .right link
		 *	field.  However, its value is always an address
		 *	below (i.e., to the left of) newblk. Similarly
		 *	for right_hook. The values of left_hook and
		 *	right_hook converge toward the value of newblk,
		 *	as in a classical binary search.
		 */
		if (x->block < newblk) {
			/*
			 * rewrite link crossing from the left
			 */
			*left_hook = x;
			left_hook = &x->right;
			x = x->right;
		} else {
			/*
			 * rewrite link crossing from the right
			 */
			*right_hook = x;
			right_hook = &x->left;
			x = x->left;
		}
	}

	*left_hook = *right_hook = NIL;		/* clear remaining hooks */

}


/*
 * delete(p)
 *	deletes a node from a cartesian tree. p is the address of
 *	a pointer to the node which is to be deleted.
 *
 * algorithm:
 *	The left and right branches of the node to be deleted define two
 *	subtrees which are to be merged and attached in place of the
 *	deleted node.  Each node on the inside edges of these two
 *	subtrees is examined and longer nodes are placed above the
 *	shorter ones.
 *
 * On entry:
 *	*p is assumed to be non-null.
 */
static void
delete(register Freehdr *p)
{
	register Freehdr x;
	register Freehdr left_branch;	/* left subtree of deleted node */
	register Freehdr right_branch;	/* right subtree of deleted node */
	register uint left_weight;
	register uint right_weight;

	x = *p;
	left_branch = x->left;
	left_weight = weight(left_branch);
	right_branch = x->right;
	right_weight = weight(right_branch);

	while (left_branch != right_branch) {
		/*
		 * iterate until left branch and right branch are
		 * both NIL.
		 */
		if (left_weight >= right_weight) {
			/*
			 * promote the left branch
			 */
			if (left_branch != NIL) {
				if (left_weight == 0) {
					/* zero-length block */
					m_error("blocksize=0 at %#x\n",
						(int)left_branch->block->data);
					break;
				}
				*p = left_branch;
				p = &left_branch->right;
				left_branch = *p;
				left_weight = weight(left_branch);
			}
		} else {
			/*
			 * promote the right branch
			 */
			if (right_branch != NIL) {
				if (right_weight == 0) {
					/* zero-length block */
					m_error("blocksize=0 at %#x\n",
					    (int)right_branch->block->data);
					break;
				}
				*p = right_branch;
				p = &right_branch->left;
				right_branch = *p;
				right_weight = weight(right_branch);
			}
		}
	}
	*p = NIL;
	putfreehdr(x);

}


/*
 * demote(p)
 *	Demotes a node in a cartesian tree, if necessary, to establish
 *	the required vertical ordering.
 *
 * algorithm:
 *	The left and right subtrees of the node to be demoted are to
 *	be partially merged and attached in place of the demoted node.
 *	The nodes on the inside edges of these two subtrees are
 *	examined and the longer nodes are placed above the shorter
 *	ones, until a node is reached which has a length no greater
 *	than that of the node being demoted (or until a null pointer
 *	is reached).  The node is then attached at this point, and
 *	the remaining subtrees (if any) become its descendants.
 *
 * on entry:
 *   a. All the nodes in the tree, including the one to be demoted,
 *	must be correctly ordered horizontally;
 *   b. All the nodes except the one to be demoted must also be
 *	correctly positioned vertically.  The node to be demoted
 *	may be already correctly positioned vertically, or it may
 *	have a length which is less than that of one or both of
 *	its progeny.
 *   c. *p is non-null
 */

static void
demote(register Freehdr *p)
{
	register Freehdr x;		/* addr of node to be demoted */
	register Freehdr left_branch;
	register Freehdr right_branch;
	register uint	left_weight;
	register uint	right_weight;
	register uint	x_weight;

	x = *p;
	x_weight = weight(x);
	left_branch = x->left;
	right_branch = x->right;
	left_weight = weight(left_branch);
	right_weight = weight(right_branch);

	while (left_weight > x_weight || right_weight > x_weight) {
		/*
		 * select a descendant branch for promotion
		 */
		if (left_weight >= right_weight) {
			/*
			 * promote the left branch
			 */
			*p = left_branch;
			p = &left_branch->right;
			left_branch = *p;
			left_weight = weight(left_branch);
		} else {
			/*
			 * promote the right branch
			 */
			*p = right_branch;
			p = &right_branch->left;
			right_branch = *p;
			right_weight = weight(right_branch);
		}
	}

	*p = x;				/* attach demoted node here */
	x->left = left_branch;
	x->right = right_branch;

}


/*
 * char*
 * malloc(nbytes)
 *	Allocates a block of length specified in bytes.  If nbytes is
 *	zero, a valid pointer (that should not be dereferenced) is returned.
 *
 * algorithm:
 *	The freelist is searched by descending the tree from the root
 *	so that at each decision point the "better fitting" branch node
 *	is chosen (i.e., the shorter one, if it is long enough, or
 *	the longer one, otherwise).  The descent stops when both
 *	branch nodes are too short.
 *
 * function result:
 *	Malloc returns a pointer to the allocated block. A null
 *	pointer indicates an error.
 *
 * diagnostics:
 *
 *	ENOMEM: storage could not be allocated.
 *
 *	EINVAL: either the argument was invalid, or the heap was found
 *	to be in an inconsistent state.  More detailed information may
 *	be obtained by enabling range checks (cf., malloc_debug()).
 *
 * Note: In this implementation, each allocated block includes a
 *	length word, which occurs before the address seen by the caller.
 *	Allocation requests are rounded up to a multiple of wordsize.
 */

static ptr_t
real_malloc(register uint nbytes)
{
	register Freehdr	allocp;	/* ptr to node to be allocated */
	register Freehdr	*fpp;		/* for tree modifications */
	register Freehdr	left_branch;
	register Freehdr	right_branch;
	register uint		left_weight;
	register uint		right_weight;
	Dblk			retblk;		/* blk returned to user */

	/*
	 * if rigorous checking was requested, do it.
	 */
	if (debug_level >= 2) {
		malloc_verify();
	}

	/*
	 * add the size of a length word to the request, and
	 * guarantee at least one word of usable data.
	 */
	nbytes += ALIGNSIZ;
	if (nbytes < SMALLEST_BLK) {
		nbytes = SMALLEST_BLK;
	} else {
		nbytes = roundup(nbytes, ALIGNSIZ);
	}

	/*
	 * ensure that at least one block is big enough to satisfy
	 *	the request.
	 */

	if (weight(_root) < nbytes) {
		/*
		 * the largest block is not enough.
		 */
		if (!morecore(nbytes)) {
			return (0);
		}
	}

	/*
	 * search down through the tree until a suitable block is
	 *	found.  At each decision point, select the better
	 *	fitting node.
	 */

	fpp = &_root;
	allocp = *fpp;
	left_branch = allocp->left;
	right_branch = allocp->right;
	left_weight = weight(left_branch);
	right_weight = weight(right_branch);

	while (left_weight >= nbytes || right_weight >= nbytes) {
		if (left_weight <= right_weight) {
			if (left_weight >= nbytes) {
				fpp = &allocp->left;
				allocp = left_branch;
			} else {
				fpp = &allocp->right;
				allocp = right_branch;
			}
		} else {
			if (right_weight >= nbytes) {
				fpp = &allocp->right;
				allocp = right_branch;
			} else {
				fpp = &allocp->left;
				allocp = left_branch;
			}
		}
		left_branch = allocp->left;
		right_branch = allocp->right;
		left_weight = weight(left_branch);
		right_weight = weight(right_branch);
	}

	/*
	 * allocate storage from the selected node.
	 */

	if (allocp->size - nbytes <= SMALLEST_BLK) {
		/*
		 * not big enough to split; must leave at least
		 * a dblk's worth of space.
		 */
		retblk = allocp->block;
		delete(fpp);
	} else {

		/*
		 * Split the selected block n bytes from the top. The
		 * n bytes at the top are returned to the caller; the
		 * remainder of the block goes back to free space.
		 */
		register Dblk nblk;

		retblk = allocp->block;
		nblk = nextblk(retblk, nbytes);		/* ^next block */
		nblk->size =  allocp->size = retblk->size - nbytes;
		__mallinfo.ordblks++;			/* count fragments */

		/*
		 * Change the selected node to point at the newly split
		 * block, and move the node to its proper place in
		 * the free space list.
		 */
		allocp->block = nblk;
		demote(fpp);

		/*
		 * set the length field of the allocated block; we need
		 * this because free() does not specify a length.
		 */
		retblk->size = nbytes;
	}
	/* maintain statistics */
	__mallinfo.uordbytes += retblk->size;		/* bytes allocated */
	__mallinfo.allocated++;				/* frags allocated */
	if (nbytes < __mallinfo.mxfast)
		__mallinfo.smblks++;	/* kludge to pass the SVVS */

	return ((ptr_t)retblk->data);

}


/*
 * free(p)
 *	return a block to the free space tree.
 *
 * algorithm:
 *	Starting at the root, search for and coalesce free blocks
 *	adjacent to one given.  When the appropriate place in the
 *	tree is found, insert the given block.
 *
 * Some sanity checks to avoid total confusion in the tree.
 *	If the block has already been freed, return.
 *	If the ptr is not from the sbrk'ed space, return.
 *	If the block size is invalid, return.
 */
static free_t
real_free(ptr_t	ptr)
{
	register uint		nbytes;	/* Size of node to be released */
	register Freehdr	*fpp;	/* For deletion from free list */
	register Freehdr	neighbor;	/* Node to be coalesced */
	register Dblk		neighbor_blk;	/* Ptr to potential neighbor */
	register uint		neighbor_size;	/* sz of potential neighbor */
	register Dblk		oldblk;	/* Ptr to block to be freed */

	/*
	 * if rigorous checking was requested, do it.
	 */
	if (debug_level >= 2) {
		malloc_verify();
	}

	/*
	 * Check the address of the old block.
	 */
	if (misaligned(ptr)) {
		m_error("free: illegal address (%#x)\n", ptr);
		free_return(0);
	}

	/*
	 * Freeing something that wasn't allocated isn't
	 * exactly kosher, but fclose() does it routinely.
	 */
	if ((ptr < (void *)_lbound) || (ptr > (void *)_ubound)) {
		errno = EINVAL;
		free_return(0);
	}

	/*
	 * Get node length by backing up by the size of a header.
	 * Check for a valid length.  It must be a positive
	 * multiple of ALIGNSIZ, at least as large as SMALLEST_BLK,
	 * no larger than the extent of the heap, and must not
	 * extend beyond the end of the heap.
	 */
	oldblk = (Dblk)((char *)ptr - ALIGNSIZ);
	nbytes = oldblk->size;
	if (badblksize(oldblk, nbytes)) {
		m_error("free: bad block size (%d) at %#x\n",
			(int)nbytes, (int)oldblk);
		free_return(0);
	}

	/* maintain statistics */
	__mallinfo.uordbytes -= nbytes;		/* bytes allocated */
	__mallinfo.allocated--;			/* frags allocated */

	/*
	 * Search the tree for the correct insertion point for this
	 *	node, coalescing adjacent free blocks along the way.
	 */
	fpp = &_root;
	neighbor = *fpp;
	while (neighbor != NIL) {
		neighbor_blk = neighbor->block;
		neighbor_size = neighbor->size;
		if (oldblk < neighbor_blk) {
			Dblk nblk = nextblk(oldblk, nbytes);
			if (nblk == neighbor_blk) {
				/*
				 * Absorb and delete right neighbor
				 */
				nbytes += neighbor_size;
				__mallinfo.ordblks--;
				delete(fpp);
			} else if (nblk > neighbor_blk) {
				/*
				 * The block being freed overlaps
				 * another block in the tree.  This
				 * is bad news.  Return to avoid
				 * further fouling up the the tree.
				 */
				m_error("free: blocks %#x, %#x overlap\n",
				    (int)oldblk, (int)neighbor_blk);
				free_return(0);
			} else {
				/*
				 * Search to the left
				 */
				fpp = &neighbor->left;
			}
		} else if (oldblk > neighbor_blk) {
			Dblk nblk = nextblk(neighbor_blk, neighbor_size);
			if (nblk == oldblk) {
				/*
				 * Absorb and delete left neighbor
				 */
				oldblk = neighbor_blk;
				nbytes += neighbor_size;
				__mallinfo.ordblks--;
				delete(fpp);
			} else if (nblk > oldblk) {
				/*
				 * This block has already been freed
				 */
				m_error("free: block %#x was already free\n",
					(int)ptr);
				free_return(0);
			} else {
				/*
				 * search to the right
				 */
				fpp = &neighbor->right;
			}
		} else {
			/*
			 * This block has already been freed
			 * as "oldblk == neighbor_blk"
			 */
			m_error("free: block %#x was already free\n",
			    (int)ptr);
			free_return(0);
		}

		/*
		 * Note that this depends on a side effect of
		 * delete(fpp) in order to terminate the loop!
		 */
		neighbor = *fpp;

	}

	/*
	 * Insert the new node into the free space tree
	 */
	insert(oldblk, nbytes);
	free_return(1);
}


/*
 * char *
 * shrink(oldblk, oldsize, newsize)
 *	Decreases the size of an old block to a new size.
 *	Returns the remainder to free space.  Returns the
 *	truncated block to the caller.
 */

static char *
shrink(register Dblk oldblk, register uint oldsize, register uint newsize)
{
	register Dblk remainder;
	if (oldsize - newsize >= SMALLEST_BLK) {
		/*
		 * Block is to be contracted. Split the old block
		 * and return the remainder to free space.
		 */
		remainder = nextblk(oldblk, newsize);
		remainder->size = oldsize - newsize;
		oldblk->size = newsize;

		/* maintain statistics */
		__mallinfo.ordblks++;		/* count fragments */
		__mallinfo.allocated++;		/* negate effect of free() */

		real_free(remainder->data);
	}
	return (oldblk->data);
}


/*
 * char *
 * realloc(ptr, nbytes)
 *
 * Reallocate an old block with a new size, returning the old block
 * if possible. The block returned is guaranteed to preserve the
 * contents of the old block up to min(size(old block), newsize).
 *
 * For backwards compatibility, ptr is allowed to reference
 * a block freed since the LAST call of malloc().  Thus the old
 * block may be busy, free, or may even be nested within a free
 * block.
 *
 * Some old programs have been known to do things like the following,
 * which is guaranteed not to work:
 *
 *	free(ptr);
 *	free(dummy);
 *	dummy = malloc(1);
 *	ptr = realloc(ptr, nbytes);
 *
 * This atrocity was found in the source for diff(1).
 */
static ptr_t
real_realloc(ptr_t ptr,	uint nbytes)
{
	static int		reclaim(register Dblk, uint, int);

	register Freehdr	*fpp;
	register Freehdr	fp;
	register Dblk		oldblk;
	register Dblk		freeblk;
	register Dblk		oldneighbor;
	register uint		oldsize;
	register uint		newsize;
	register uint		oldneighborsize;


	/*
	 * if rigorous checking was requested, do it.
	 */
	if (debug_level >= 2) {
		malloc_verify();
	}

	/*
	 * Check the address of the old block.
	 */
	if (misaligned(ptr) || (ptr < (void *)_lbound) ||
	    (ptr > (void *)_ubound)) {
		m_error("realloc: illegal address (%#x)\n", ptr);
		return (NULL);
	}

	/*
	 * check location and size of the old block and its
	 * neighboring block to the right.  If the old block is
	 * at end of memory, the neighboring block is undefined.
	 */
	oldblk = (Dblk)((char *)ptr - ALIGNSIZ);
	oldsize = oldblk->size;
	if (badblksize(oldblk, oldsize)) {
		m_error("realloc: bad block size (%d) at %#x\n",
			oldsize, oldblk);
		return (NULL);
	}
	oldneighbor = nextblk(oldblk, oldsize);

	/* *** tree search code pulled into separate subroutine *** */
	if (reclaim(oldblk, oldsize, 1) == -1) {
		return (NULL);		/* internal error */
	}

	/*
	 * At this point, we can guarantee that oldblk is out of free
	 * space. What we do next depends on a comparison of the size
	 * of the old block and the requested new block size.  To do
	 * this, first round up the new size request.
	 */
	newsize = nbytes + ALIGNSIZ;		/* add size of a length word */
	if (newsize < SMALLEST_BLK) {
		newsize = SMALLEST_BLK;
	} else {
		newsize = roundup(newsize, ALIGNSIZ);
	}

	/*
	 * Next, examine the size of the old block, and compare it
	 * with the requested new size.
	 */

	if (oldsize >= newsize) {
		/*
		 * Block is to be made smaller.
		 */
		return (shrink(oldblk, oldsize, newsize));
	}

	/*
	 * Block is to be expanded.  Look for adjacent free memory.
	 */
	if (oldneighbor < (Dblk)_ubound) {
		/*
		 * Search for the adjacent block in the free
		 * space tree.  Note that the tree may have been
		 * modified in the earlier loop.
		 */
		fpp = &_root;
		fp = *fpp;
		oldneighborsize = oldneighbor->size;
		if (badblksize(oldneighbor, oldneighborsize)) {
			m_error("realloc: bad blocksize(%d) at %#x\n",
				oldneighborsize, oldneighbor);
			return (NULL);
		}
		while (weight(fp) >= oldneighborsize) {
			freeblk = fp->block;
			if (oldneighbor < freeblk) {
				/*
				 * search to the left
				 */
				fpp = &(fp->left);
				fp = *fpp;
			} else if (oldneighbor > freeblk) {
				/*
				 * search to the right
				 */
				fpp = &(fp->right);
				fp = *fpp;
			} else {		/* oldneighbor == freeblk */
				/*
				 * neighboring block is free; is it big enough?
				 */
				if (oldsize + oldneighborsize >= newsize) {
					/*
					 * Big enough. Delete freeblk, join
					 * oldblk to neighbor, return newsize
					 * bytes to the caller, and return the
					 * remainder to free storage.
					 */
					delete(fpp);

					/* maintain statistics */
					__mallinfo.ordblks--;
					__mallinfo.uordbytes +=
						oldneighborsize;

					oldsize += oldneighborsize;
					oldblk->size = oldsize;
					return (shrink(oldblk, oldsize,
						    newsize));
				} else {
					/*
					 * Not big enough. Stop looking for a
					 * free lunch.
					 */
					break;
				}
			}
		}
	}

	/*
	 * At this point, we know there is no free space in which to
	 * expand. Malloc a new block, copy the old block to the new,
	 * and free the old block, IN THAT ORDER.
	 */
	ptr = real_malloc(nbytes);
	if (ptr != NULL) {
		(void) memcpy(ptr, oldblk->data, oldsize - ALIGNSIZ);
		real_free(oldblk->data);
	}
	return (ptr);

} /* real_realloc */


/*
 * *** The following code was pulled out of realloc() ***
 *
 * int
 * reclaim(oldblk, oldsize, flag)
 *	If a block containing 'oldsize' bytes from 'oldblk'
 *	is in the free list, remove it from the free list.
 *	'oldblk' and 'oldsize' are assumed to include the free block header.
 *
 *	Returns 1 if block was successfully removed.
 *	Returns 0 if block was not in free list.
 *	Returns -1 if block spans a free/allocated boundary (error() called
 *						if 'flag' == 1).
 */
static int
reclaim(register Dblk oldblk, uint oldsize, int flag)
{
	register Dblk oldneighbor;
	register Freehdr	*fpp;
	register Freehdr	fp;
	register Dblk		freeblk;
	register uint		size;

	/*
	 * Search the free space list for a node describing oldblk,
	 * or a node describing a block containing oldblk.  Assuming
	 * the size of blocks decreases monotonically with depth in
	 * the tree, the loop may terminate as soon as a block smaller
	 * than oldblk is encountered.
	 */

	oldneighbor = nextblk(oldblk, oldsize);

	fpp = &_root;
	fp = *fpp;
	while ((size = weight(fp)) >= oldsize) {
		freeblk = fp->block;
		if (badblksize(freeblk, size)) {
			m_error("realloc: bad block size (%d) at %#x\n",
				size, freeblk);
			return (-1);
		}
		if (oldblk == freeblk) {
			/*
			 * |<-- freeblk ...
			 * _________________________________
			 * |<-- oldblk ...
			 * ---------------------------------
			 * Found oldblk in the free space tree; delete it.
			 */
			delete(fpp);

			/* maintain statistics */
			__mallinfo.uordbytes += oldsize;
			__mallinfo.allocated++;
			return (1);
		} else if (oldblk < freeblk) {
			/*
			 * 		|<-- freeblk ...
			 * _________________________________
			 * |<--oldblk ...
			 * ---------------------------------
			 * Search to the left for oldblk
			 */
			fpp = &fp->left;
			fp = *fpp;
		} else {
			/*
			 * |<-- freeblk ...
			 * _________________________________
			 * |			|<--oldblk--->|<--oldneighbor
			 * ---------------------------------
			 * oldblk is somewhere to the right of freeblk.
			 * Check to see if it lies within freeblk.
			 */
			register Dblk freeneighbor;
			freeneighbor =  nextblk(freeblk, freeblk->size);
			if (oldblk >= freeneighbor) {
				/*
				 * |<-- freeblk--->|<--- freeneighbor ...
				 * _________________________________
				 * |		      |<--oldblk--->|
				 * ---------------------------------
				 * no such luck; search to the right.
				 */
				fpp =  &fp->right;
				fp = *fpp;
			} else {
				/*
				 * freeblk < oldblk < freeneighbor;
				 * i.e., oldblk begins within freeblk.
				 */
				if (oldneighbor > freeneighbor) {
					/*
					 * |<-- freeblk--->|<--- freeneighbor
					 * _________________________________
					 * |	 |<--oldblk--->|<--oldneighbor
					 * ---------------------------------
					 * oldblk straddles a block boundary!
					 */
					if (flag) {
	    m_error("realloc: block %#x straddles free block boundary\n",
		    oldblk);
					}
					return (-1);
				} else if (oldneighbor == freeneighbor) {
					/*
					 * |<-------- freeblk------------->|
					 * _________________________________
					 * |		 |<--oldblk--->|
					 * ---------------------------------
					 * Oldblk is on the right end of
					 * freeblk. Delete freeblk, split
					 * into two fragments, and return
					 * the one on the left to free space.
					 */
					delete(fpp);

					/* maintain statistics */
					__mallinfo.ordblks++;
					__mallinfo.uordbytes += oldsize;
					__mallinfo.allocated += 2;

					freeblk->size -= oldsize;
					real_free(freeblk->data);
					return (1);
				} else {
					/*
					 * |<-------- freeblk------------->|
					 * _________________________________
					 * |	|oldblk	 | oldneighbor	|
					 * ---------------------------------
					 * Oldblk is in the middle of freeblk.
					 * Delete freeblk, split into three
					 * fragments, and return the ones on
					 * the ends to free space.
					 */
					delete(fpp);

					/* maintain statistics */
					__mallinfo.ordblks += 2;
					__mallinfo.uordbytes += freeblk->size;
					__mallinfo.allocated += 3;

					/*
					 * split the left fragment by
					 * subtracting the size of oldblk
					 * and oldblk's neighbor
					 */
					freeblk->size -=
						((char *)freeneighbor
							- (char *)oldblk);
					/*
					 * split the right fragment by
					 * setting oldblk's neighbor's size
					 */
					oldneighbor->size =
						(char *)freeneighbor
							- (char *)oldneighbor;
					/*
					 * return the fragments to free space
					 */
					real_free(freeblk->data);
					real_free(oldneighbor->data);
					return (1);
				}
			}
		}
	}

	return (0);		/* free block not found */
}


/*
 * bool
 * morecore(nbytes)
 *	Add a block of at least nbytes from end-of-memory to the
 *	free space tree.
 *
 * return value:
 *	true	if at least n bytes can be allocated
 *	false	otherwise
 *
 * remarks:
 *
 *   -- free space (delimited by the extern variable _ubound) is
 *	extended by an amount determined by rounding nbytes up to
 *	a multiple of the system page size.
 *
 *   -- The lower bound of the heap is determined the first time
 *	this routine is entered. It does NOT necessarily begin at
 *	the end of static data space, since startup code (e.g., for
 *	profiling) may have invoked sbrk() before we got here.
 */

static bool
morecore(uint nbytes)
{
	Dblk p;
	Freehdr newhdr;

	if (nbpg == 0) {
		/*
		 * hack to avoid fragmenting the heap with the first
		 * freehdr page
		 */
		if ((newhdr = getfreehdr()) == NIL) {
			/* Error message returned by getfreehdr() */
			return (false);
		}
		putfreehdr(newhdr);
	}
	nbytes = roundup(nbytes, nbpg);
	p = (Dblk) sbrk((int)nbytes);
	if (p == (Dblk) -1)
		return (false);	/* errno = ENOMEM */
	if (_lbound == NULL)	/* set _lbound the first time through */
		_lbound = (char *) p;
	_ubound = (char *) p + nbytes;
	p->size = nbytes;

	/* maintain statistics */
	__mallinfo.arena = _ubound - _lbound;
	__mallinfo.uordbytes += nbytes;
	__mallinfo.ordblks++;
	__mallinfo.allocated++;

	real_free(p->data);
	return (true);

} /* morecore */


/*
 * Get a free block header from the free header list.
 * When the list is empty, allocate an array of headers.
 * When the array is empty, allocate another one.
 * When we can't allocate another array, we're in deep weeds.
 */
static	Freehdr
getfreehdr()
{
	Freehdr	r;
	register Dblk	blk;
	register uint	size;

	if (freehdrlist != NIL) {
		r = freehdrlist;
		freehdrlist = freehdrlist->left;
		return (r);
	}
	if (nfreehdrs <= 0) {
		size = NFREE_HDRS*sizeof (struct freehdr) + ALIGNSIZ;
		blk = (Dblk) sbrk(size);
		if ((int)blk == -1) {
			malloc_debug(1);
			m_error("getfreehdr: out of memory");
			return (NIL);
		}
		if (_lbound == NULL)	/* set _lbound on first allocation */
			_lbound = (char *)blk;
		blk->size = size;
		freehdrptr = (Freehdr)blk->data;
		nfreehdrs = NFREE_HDRS;
		_ubound = (char *) nextblk(blk, size);

		/* maintain statistics */
		__mallinfo.arena = _ubound - _lbound;
		__mallinfo.treeoverhead += size;
	}
	nfreehdrs--;
	return (freehdrptr++);
}


/*
 * Free a free block header
 * Add it to the list of available headers.
 */
static void
putfreehdr(Freehdr p)
{
	p->left = freehdrlist;
	freehdrlist = p;
}



/*
 * malloc_debug(level)
 *
 * description:
 *
 *	Controls the level of error diagnosis and consistency checking
 *	done by malloc() and free(). level is interpreted as follows:
 *
 *	0:  malloc() and free() return 0 if error detected in arguments
 *	    (errno is set to EINVAL)
 *	1:  malloc() and free() abort if errors detected in arguments
 *	2:  same as 1, but scan entire heap for errors on every call
 *	    to malloc() or free()
 *
 * function result:
 *	returns the previous level of error reporting.
 */
int
malloc_debug(int level)
{
	int	old_level;

	m_error("malloc_debug(): old=%d, new=%d\n", debug_level, level);

	old_level = debug_level;
	debug_level = level;
	return (old_level);
}


/*
 * check a free space tree pointer. Should be in
 * the static free pool or somewhere in the heap.
 */

#define	chkblk(p) \
	if (misaligned(p) || \
	    (((Dblk)(p) < (Dblk)_lbound) || ((Dblk)(p) > (Dblk)_ubound))) {\
		blkerror((Freehdr)(p)); \
		return (0); \
	}

#define	chkhdr(p) chkblk(p)

static void
blkerror(Freehdr p)
{
	m_error("Illegal block address (%#x)\n", (p));
}


/*
 * cartesian(p)
 *	returns 1 if free space tree p satisfies internal consistency
 *	checks.
 */
static int
cartesian(register Freehdr p)
{
	register Freehdr probe;
	register Dblk db, pdb;

	if (p == NIL) {				/* no tree to test */
		return (1);
	}
	/*
	 * check that root has a data block
	 */
	chkhdr(p);
	pdb = p->block;
	chkblk(pdb);

	/*
	 * check that the child blocks are no larger than the parent block.
	 */
	probe = p->left;
	if (probe != NIL) {
		chkhdr(probe);
		db = probe->block;
		chkblk(db);
		if (probe->size > p->size)	/* child larger than parent */
			return (0);
	}
	probe = p->right;
	if (probe != NIL) {
		chkhdr(probe);
		db = probe->block;
		chkblk(db);
		if (probe->size > p->size)	/* child larger than parent */
			return (0);
	}
	/*
	 * test data addresses in the left subtree,
	 * starting at the left subroot and probing to
	 * the right.  All data addresses must be < p->block.
	 */
	probe = p->left;
	while (probe != NIL) {
		chkhdr(probe);
		db = probe->block;
		chkblk(db);
		if (nextblk(db, probe->size) >= pdb) {	/* overlap */
			return (0);
		}
		probe = probe->right;
	}
	/*
	 * test data addresses in the right subtree,
	 * starting at the right subroot and probing to
	 * the left.  All addresses must be > nextblk(p->block).
	 */
	pdb = nextblk(pdb, p->size);
	probe = p->right;
	while (probe != NIL) {
		chkhdr(probe);
		db = probe->block;
		chkblk(db);
		if (db == NULL || db <= pdb) {		/* overlap */
			return (0);
		}
		probe = probe->left;
	}
	return (cartesian(p->left) && cartesian(p->right));
}


/*
 * malloc_verify()
 *
 * This is a verification routine.  It walks through all blocks
 * in the heap (both free and busy) and checks for bad blocks.
 * malloc_verify returns 1 if the heap contains no detectably bad
 * blocks; otherwise it returns 0.
 */

int
malloc_verify()
{
	register int	maxsize;
	register int	hdrsize;
	register int	size;
	register Dblk	p;
	uint	lb, ub;

	extern  char	end[];


	if (debug_level >= 3) {
	    m_error("malloc_verify: verifying heap ...\n");
	}

	if (_lbound == NULL) {	/* no allocation yet */
		return (1);
	}

	/*
	 * first check heap bounds pointers
	 */
	lb = (uint)end;
	ub = (uint)sbrk(0);

	if (((uint)_lbound < lb) || ((uint)_lbound > ub)) {
		m_error("malloc_verify: illegal heap lower bound (%#x)\n",
			_lbound);
		return (0);
	}
	if (((uint)_ubound < lb) || ((uint)_ubound > ub)) {
		m_error("malloc_verify: illegal heap upper bound (%#x)\n",
			_ubound);
		return (0);
	}
	maxsize = heapsize();
	p = (Dblk)_lbound;
	while (p < (Dblk) _ubound) {
		size = p->size;
		if (((size) < SMALLEST_BLK) ||
		    ((size) & (ALIGNSIZ-1)) ||
		    ((size) > heapsize()) ||
		    (((char *)(p))+(size) > _ubound)) {
			m_error("malloc_verify: bad block size (%d) at %#x\n",
			    size, p);
			return (0);		/* Badness */
		}
		p = nextblk(p, size);
	}
	if (p > (Dblk) _ubound) {
		m_error("malloc_verify: heap corrupted\n");
		return (0);
	}
	if (!cartesian(_root)) {
		m_error("malloc_verify: free space tree corrupted\n");
		return (0);
	}
	return (1);
}


/*
 * The following is a kludge to avoid dependency on stdio, which
 * uses malloc() and free(), one of which probably got us here in
 * the first place.
 */

#define	m_putchar(c)	(*buf++ = (c))
#define	m_fileno(x)	(x)		/* bletch */
#define	m_stderr	(2)		/* bletch */
#define	LBUFSIZ		256

static	char	stderrbuf[LBUFSIZ];

static int
m_sprintf(char *string, const char *fmt, va_list ap)
{
	char			updated_fmt[BUFSIZ];
	register const char	*p = fmt;
	register char		*u = updated_fmt;



	/* translate '%#x' to '0x%x' in fmt */
	while (*p) {
		if ((*p == '%' && (*(p+1) == '#') && (*(p+2) == 'x'))) {
			*u++ = '0';
			*u++ = 'x';
			*u++ = '%';
			*u++ = 'x';
			p += 4;
		} else {
			*u++ = *p++;
		}
	}
	*u = '\0';

	/* now just use vsprintf */
	vsprintf(string, updated_fmt, ap);

} /* m_sprintf */

/*
 * Error routine.
 * If debug_level == 0, does nothing except set errno = EINVAL.
 * Otherwise, prints an error message to stderr and generates a
 * core image.
 */

static void
m_error(const char *fmt, ...)
{
	va_list		ap;
	char		err_msg[MAXNAMELEN];
	register int	nbytes;


	errno = EINVAL;
	if (debug_level == 0) {
		return;
	}

	va_start(ap, fmt);
	nbytes = m_sprintf(stderrbuf, fmt, ap);
	va_end(ap);
	write(m_fileno(m_stderr), stderrbuf, nbytes);

#ifdef notdef
	abort();
#endif
}



#if !defined(lint) && defined(SCCSIDS)
static	char sccsid[] = "@(#)memalign.c 1.9 88/02/08 Copyr 1984 Sun Micro";
#endif


/*
 * memalign(align, nbytes)
 *
 * Description:
 *	Returns a block of specified size on a specified alignment boundary.
 *
 * Algorithm:
 *	Malloc enough to ensure that a block can be aligned correctly.
 *	Find the alignment point and return the fragments
 *	before and after the block.
 *
 * Errors:
 *	Returns NULL and sets errno as follows:
 *	[EINVAL]
 *		if nbytes = 0,
 *		or if alignment is misaligned,
 *		or if the heap has been detectably corrupted.
 *	[ENOMEM]
 *		if the requested memory could not be allocated.
 */

static void *
real_memalign(uint align, uint nbytes)
{
	uint	reqsize;		/* Num of bytes to get from malloc() */
	register char	*p;		/* Ptr returned from malloc() */
	register Dblk	blk;		/* For addressing fragment blocks */
	register uint	blksize;	/* Current (shrinking) block size */
	register char	*alignedp;	/* Ptr to properly aligned boundary */
	register Dblk	aligned_blk;	/* The block to be returned */
	register uint	frag_size;	/* size of fragments fore and aft */
	uint	x;			/* ccom can't do (char *)(uint/uint) */

	/*
	 * check for valid size and alignment parameters
	 */
	if (nbytes == 0 || misaligned(align)) {
		errno = EINVAL;
		return (NULL);
	}

	/*
	 * Malloc enough memory to guarantee that the result can be
	 * aligned correctly. The worst case is when malloc returns
	 * a block so close to the next alignment boundary that a
	 * fragment of minimum size cannot be created.
	 */
	nbytes = roundup(nbytes, ALIGNSIZ);
	reqsize = nbytes + align + SMALLEST_BLK;
	p = real_malloc(reqsize);
	if (p == NULL) {
		return (NULL);
	}

	/*
	 * get size of the entire block (overhead and all)
	 */
	blk = (Dblk)(p - ALIGNSIZ);	/* back up to get length word */
	blksize = blk->size;

	/*
	 * locate the proper alignment boundary within the block.
	 */
	x = roundup((uint)p, align);		/* ccom work-around */
	alignedp = (char *)x;
	aligned_blk = (Dblk)(alignedp - ALIGNSIZ);

	/*
	 * Check out the space to the left of the alignment
	 * boundary, and split off a fragment if necessary.
	 */
	frag_size = (uint)aligned_blk - (uint)blk;
	if (frag_size != 0) {
		/*
		 * Create a fragment to the left of the aligned block.
		 */
		if (frag_size < SMALLEST_BLK) {
			/*
			 * Not enough space. So make the split
			 * at the other end of the alignment unit.
			 */
			frag_size += align;
			aligned_blk = nextblk(aligned_blk, align);
		}
		blk->size = frag_size;
		blksize -= frag_size;
		aligned_blk->size = blksize;
		real_free(blk->data);
	}

	/*
	 * Is there a (sufficiently large) fragment to the
	 * right of the aligned block?
	 */
	nbytes += ALIGNSIZ;
	frag_size = blksize - nbytes;
	if (frag_size > SMALLEST_BLK) {
		/*
		 * split and free a fragment on the right
		 */
		blk = nextblk(aligned_blk, nbytes);
		blk->size = frag_size;
		aligned_blk->size -= frag_size;
		real_free(blk->data);
	}
	return ((void *)aligned_blk->data);
}
/*
 * mallopt -- System V-compatible malloc "optimizer"
 */
int
mallopt(int cmd, int value)
{
	if (__mallinfo.smblks != 0) {
		return (-1);		/* small block has been allocated */
	}

	switch (cmd) {
	case M_MXFAST:		/* small block size */
		if (value < 0) {
			return (-1);
		}
		__mallinfo.mxfast = value;
		break;

	case M_NLBLKS:		/* # small blocks per holding block */
		if (value <= 0) {
			return (-1);
		}
		__mallinfo.nlblks = value;
		break;

	case M_GRAIN:		/* small block rounding factor */
		if (value <= 0) {
			return (-1);
		}
		/* round up to multiple of minimum alignment */
		__mallinfo.grain = roundup(value, ALIGNSIZ);
		break;

	case M_KEEP:		/* Sun algorithm always preserves data */
		break;

	default:
		return (-1);
	}

	/* make sure that everything is consistent */
	__mallinfo.mxfast = roundup(__mallinfo.mxfast, __mallinfo.grain);

	return (0);
}


/*
 * mallinfo -- System V-compatible malloc information reporter
 */
struct mallinfo
mallinfo()
{
	struct mallinfo mi;

	mi = __mallinfo;
	mi.uordblks = mi.uordbytes - (mi.allocated * sizeof (uint));
	mi.fordblks = mi.arena - (mi.treeoverhead + mi.uordblks +
					    (mi.ordblks * sizeof (uint)));
	return (mi);
}

void
mallinfo_print()
{
	struct mallinfo mi = mallinfo();

	info("USAGE: Total space = %u\n", mi.arena);
	info(
	    "USAGE: Number of ordinary blocks %u, Number of small blocks %u\n",
	    mi.ordblks, mi.smblks);
	info("USAGE: Free ordinary blocks %u, Free small blocks %u\n",
	    mi.fordblks, mi.fsmblks);
	info("USAGE: Ordinary blocks %u, Small blocks %u\n",
	    mi.uordblks, mi.usmblks);
}

#endif	/* DEBUG_MALLOC */
