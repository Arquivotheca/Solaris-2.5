/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * Copyright (c) 1991, Sun Microsytems, Inc.
 */

#ident	"@(#)textmem.c	1.9	93/12/25 SMI"	/* SVR4/MNLS 1.1.2.1 */


/*
 * Simplified version of malloc(), free() and realloc(), to be linked with
 * utilities that use [s]brk() and do not define their own version of the
 * routines.
 * The algorithm maps /dev/zero to get extra memory space.
 * Each call to mmap() creates a page. The pages are linked in a list.
 * Each page is divided in blocks. There is at least one block in a page.
 * New memory chunks are allocated on a first-fit basis.
 * Freed blocks are joined in larger blocks. Free pages are unmapped.
 */
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <thread.h>
#include <synch.h>


#ifdef _REENTRANT
static mutex_t lock = DEFAULTMUTEX;
#endif _REENTRANT

struct block {
	unsigned size;		/* Space available for user */
	struct page *page;	/* Backwards reference to page */
	int status;
	struct block *next;
	void *memstart[1];
};
	
struct page {
	unsigned size;		/* Total page size (incl. header) */
	struct page *next;
	struct block block[1];
};

#define FREE	0
#define BUSY	1

#define HDR_BLOCK	(sizeof(struct block) - sizeof(void *))
#define HDR_PAGE	(sizeof(struct page) - sizeof(void *))
#define MINSZ		sizeof(double)

/* for convenience */
#ifndef NULL
#define	NULL		(0)
#endif

static int fd = -1;
struct page *memstart;
static int pagesize;
static void defrag(), split();
static void *malloc_unlocked();

void *
malloc(size)
unsigned size;
{
	void *retval;
	_mutex_lock(&lock);
	retval = malloc_unlocked(size);
	_mutex_unlock(&lock);
	return retval;
}
  

static void *
malloc_unlocked(size)
unsigned size;
{
	struct block *block;
	struct page *page;
	
	if (fd == -1){
		if ((fd = open("/dev/zero", O_RDWR)) == -1)
			return 0;
		pagesize = sysconf(_SC_PAGESIZE);
	}

	size = align(size, MINSZ);

	/*
	 * Try to locate necessary space
	 */
	for (page = memstart; page; page = page->next){
		for (block = page->block; block; block = block->next){
			if (block->status == FREE && block->size >= size)
				goto found;
		}
	}
found:

	/*
	 * Need to allocate a new page
	 */
	if (!page){
		unsigned totsize = size + HDR_PAGE;
		int totpage = align(totsize, pagesize);

		if ((page = (struct page *)mmap(0, totpage,
			PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0)) == 0)
			return 0;

		page->next = memstart;
		memstart = page;
		page->size = totpage;
		block = page->block;
		block->next = 0;
		block->status = FREE;
		block->size = totpage - HDR_PAGE;
		block->page = page;
	}
		
	split(block, size);

	block->status = BUSY;
	return &block->memstart;
}

void *
realloc(ptr, size)
void *ptr;
unsigned size;
{
	struct block *block;
	unsigned osize;
	void *newptr;
	
	_mutex_lock(&lock);
	block = (struct block *)((char *)ptr - HDR_BLOCK);
	size = align(size, MINSZ);
	osize = block->size;

	/*
	 * Join block with next one if it is free
	 */
	if (block->next && block->next->status == FREE){
		block->size += block->next->size + HDR_BLOCK;
		block->next = block->next->next;
	}

	if (size <= block->size){
		split(block, size);
		_mutex_unlock(&lock);
		return ptr;
	}
	
	newptr = malloc_unlocked(size);
	(void)memcpy(newptr, ptr, osize);
	block->status = FREE;
	defrag(block->page);
	_mutex_unlock(&lock);
	return newptr;
}

void
free(ptr)
void *ptr;
{
	struct block *block;

	_mutex_lock(&lock);
	if (ptr == NULL) {
		_mutex_unlock(&lock);
		return;
	}	
	block = (struct block *)((char *)ptr - HDR_BLOCK);
	block->status = FREE;
	
	defrag(block->page);
	_mutex_unlock(&lock);
}

/*
 * Align size on an appropriate boundary
 */
static
align(size, bound)
unsigned size;
int bound;
{
	if (size < bound)
		return bound;
	else
		return size + bound - 1 - (size + bound - 1) % bound;
}

static void
split(block, size)
struct block *block;
unsigned size;
{
	if (block->size > size + sizeof(struct block)){
		struct block *newblock;
		newblock = (struct block *)((char *)block + HDR_BLOCK + size);
		newblock->next = block->next;
		block->next = newblock;
		newblock->status = FREE;
		newblock->page = block->page;
		newblock->size = block->size - size - HDR_BLOCK;
		block->size = size;
	}
}

/*
 * Defragmentation
 */
static void
defrag(page)
struct page *page;
{
	struct block *block;

	for (block = page->block ; block ; block = block->next){
		struct block *block2;

		if (block->status == BUSY)
			continue;
		for (block2 = block->next ; block2 && block2->status == FREE ;
							block2 = block2->next){
			block->next = block2->next;
			block->size += block2->size + HDR_BLOCK;
		}
	}

	/*
	 * Free page
	 */
	if (page->block->size == page->size - HDR_PAGE){
		if (page == memstart)
			memstart = page->next;
		else {
			struct page *page2;
			for (page2 = memstart ; page2->next ; page2 = page2->next){
				if (page2->next == page){
					page2->next = page->next;
					break;
				}
			}
		}
		(void)munmap((caddr_t)page, page->size);
	}
}
