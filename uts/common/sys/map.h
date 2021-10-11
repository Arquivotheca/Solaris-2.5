/*
 * Copyright (c) 1982, 1986 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

#ifndef _SYS_MAP_H
#define	_SYS_MAP_H

#pragma ident	"@(#)map.h	1.18	94/08/12 SMI"	/* from SunOS 4.0 2.8 */

#include <sys/t_lock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Resource Allocation Maps.
 *
 * Associated routines manage sub-allocation of an address space using
 * an array of segment descriptors.  The first element of this array
 * is a map structure, describing the number of free elements in the array
 * and a count of the number of processes sleeping on an allocation failure.
 * Each additional structure represents a free segment of the address space.
 * The final entry has a zero in the segment size and a pointer to the name
 * of the controlled object in the address field.
 *
 * A call to rmallocmap dynamically allocates and initializes a
 * resource map. rmfree is then used to associate the map with the
 * actual resource.
 *
 * Subsequent calls to rmalloc and rmfree allocate and free space in the
 * resource map.  rmalloc() returns zero if insufficient space is available.
 * To wait for space, use rmalloc_wait().  If the resource
 * map becomes too fragmented to be described in the available space,
 * then some of the resource is discarded.  This may lead to critical
 * shortages, but is better than not checking (as the previous versions
 * of these routines did) or giving up and calling panic().  The routines
 * could use linked lists and call a memory allocator when they run
 * out of space, but that would not solve the out of space problem when
 * called at interrupt time.
 *
 * N.B.: The address 0 in the resource address space is not available
 * as it is used internally by the resource map routines.
 */

struct map {
	ulong_t	m_size;		/* size of this segment of the map */
	ulong_t	m_addr;		/* resource-space addr of start of segment */
};

struct map_head {
	ulong_t		m_free;		/* number of free slots in map */
	ulong_t		m_want;		/* # of threads sleeping on map */
	char		*m_nam;		/* name of resource */
	int		m_size;		/* number of map entries */
	kmutex_t	m_lock;		/* lock on the map */
	kcondvar_t	m_cv;		/* use to wait for space */
	struct map	m_map[1];	/* actual map - m_size entries */
};

/*
 * For statically allocated map arrays, the minimum size of the array
 * must allow for the map header.  There should be MAP_OVERHEAD extra
 * map entries in the array.  Overhead includes extra map entry at end.
 */
#define	MAP_OVERHEAD \
	((sizeof (struct map_head) + 2 * sizeof (struct map) - 1) / \
		sizeof (struct map))

#define	mapstart(X)	(&((struct map_head *)(X))->m_map[0])
#define	mapfree(X)	(((struct map_head *)(X))->m_free)
#define	mapwant(X)	(((struct map_head *)(X))->m_want)
#define	mapname(X)	(((struct map_head *)(X))->m_nam)
#define	maplock(X)	(((struct map_head *)(X))->m_lock)
#define	map_cv(X)	(((struct map_head *)(X))->m_cv)

#ifdef _KERNEL
extern struct	map *kernelmap;
extern struct	map *ekernelmap;

void	mapinit(struct map *, long, ulong_t, char *, int);
void	rmfree(struct map *, size_t, ulong_t);
void	mfree(struct map *, size_t, ulong_t);
ulong_t	rmalloc(struct map *, size_t);
ulong_t	rmalloc_wait(struct map *, size_t);
ulong_t	rmalloc_locked(struct map *, size_t);
ulong_t	malloc(struct map *, size_t);
struct map *rmallocmap(ulong_t mapsize);
struct map *rmallocmap_wait(ulong_t mapsize);
void	rmfreemap(struct map *);
int	rmget(struct map *, size_t, ulong_t);

#endif /* KERNEL */

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_MAP_H */
