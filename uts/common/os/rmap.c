#ident	"@(#)rmap.c	2.33	94/11/17 SMI" /* from SunOS 4.0 2.11 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/map.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>


/*
 * Resource map handling routines.
 *
 * A resource map is an array of structures each
 * of which describes a segment of the address space of an available
 * resource.  The segments are described by their base address and
 * length, and sorted in address order.  Each resource map has a fixed
 * maximum number of segments allowed.  Resources are allocated
 * by taking part or all of one of the segments of the map.
 *
 * Returning of resources will require another segment if
 * the returned resources are not adjacent in the address
 * space to an existing segment.  If the return of a segment
 * would require a slot which is not available, then one of
 * the resource map segments is discarded after a warning is printed.
 * Returning of resources may also cause the map to collapse
 * by coalescing two existing segments and the returned space
 * into a single segment.  In this case the resource map is
 * made smaller by copying together to fill the resultant gap.
 *
 * N.B.: the current implementation uses a dense array and does
 * not admit the value ``0'' as a legal address, since that is used
 * as a delimiter.
 */

/*
 * Initialize map mp to have (mapsize-MAP_OVERHEAD) segments
 * and to be called ``name'', which we print if
 * the slots become so fragmented that we lose space.
 * The map itself is initialized with size elements free
 * starting at addr.
 *
 * Direct usage of this routine is discouraged, and should only be for
 * system maps that get allocated before kmem_alloc() and rmallocmap() can
 * be used.
 *
 * Most kernel code and all drivers should use rmallocmap() to get the map.
 */
void
mapinit(struct map *mp, long size, ulong_t addr, char *name, int mapsize)
{
	register struct map *ep = mapstart(mp);
	register struct map_head *hp = (struct map_head *)mp;

	/*
	 * The map must be large enough for the header and one free segment.
	 */
	if (mapsize < MAP_OVERHEAD + 1)
		panic("mapinit: map too small\n");

	/*
	 * Some of the mapsize slots are taken by the map_head structure.
	 * The final segment in the * array has size 0 and acts as a delimiter.
	 * We insure that we never use segments past the end of
	 * the array by maintaining a free segment count in m_free.
	 * Instead, when excess segments occur we discard some resources.
	 */
	hp->m_free = hp->m_size = mapsize - MAP_OVERHEAD;
	hp->m_nam = name;

	/*
	 * Simulate a rmfree(), but with the option to
	 * call with size 0 and addr 0 when we just want
	 * to initialize without freeing.
	 */
	ep->m_size = size;
	ep->m_addr = addr;
	if (size != 0)
		mapfree(mp)--;

	/*
	 * Initialize the lock in the map.
	 */
	mutex_init(&maplock(mp), name, MUTEX_DEFAULT, NULL);

	/*
	 * Initialize the condition variable in the map.
	 */
	cv_init(&map_cv(mp), name, CV_DEFAULT, NULL);
}

/*
 * Dynamically allocate a map.
 *   This is a DKI routine and should not change interfaces.
 *
 * Does not sleep.
 *
 * Driver defined basic locks, read/write locks, and sleep locks may be held
 * across calls to this function.
 *
 * DDI/DKI conforming drivers may only use map structures which have been
 * allocated and initialized using rmallocmap().
 */
struct map *
rmallocmap(ulong_t mapsize)
{
	struct map *mp;

	mp = (struct map *) kmem_zalloc((MAP_OVERHEAD + mapsize) *
		sizeof (struct map), KM_NOSLEEP);
	if (mp != NULL)
		mapinit(mp, 0, 0, "rmallocmap", mapsize + MAP_OVERHEAD);
	return (mp);
}

/*
 * Dynamically allocate a map.
 *   This is a DDI routine and should not change interfaces.
 *
 * It DOES sleep.
 *
 * DDI/DKI conforming drivers may only use map structures which have been
 * allocated and initialized using rmallocmap() and rmallocmap_wait().
 */
struct map *
rmallocmap_wait(ulong_t mapsize)
{
	struct map *mp;

	mp = (struct map *) kmem_zalloc((MAP_OVERHEAD + mapsize) *
		sizeof (struct map), KM_SLEEP);
	mapinit(mp, 0, 0, "rmallocmap", mapsize + MAP_OVERHEAD);
	return (mp);
}

/*
 * Free a dynamically allocated map.
 *   This is a DKI routine and should not change interfaces.
 *
 * Does not sleep.
 *
 * Driver defined basic locks, read/write locks, and sleep locks may be held
 * across calls to this function.
 *
 * Before freeing the map, the caller must ensure that nobody is using space
 * managed by the map, and that nobody is waiting for space in the map.
 */
void
rmfreemap(struct map *mp)
{
	struct map_head *hp = (struct map_head *)mp;

	mutex_destroy(&maplock(mp));
	kmem_free((caddr_t) mp,
		(MAP_OVERHEAD + hp->m_size) * sizeof (struct map));
}

/*
 * Allocate 'size' units from the given
 * map. Return the base of the allocated space.
 * In a map, the addresses are increasing and the
 * list is terminated by a 0 size.
 *
 * Algorithm is first-fit.
 */
ulong_t
rmalloc(register struct map *mp, size_t size)
{
	register ulong_t addr;

	if ((int)size <= 0)
		panic("rmalloc");

	mutex_enter(&maplock(mp));
	addr = rmalloc_locked(mp, size);
	mutex_exit(&maplock(mp));
	return (addr);
}

/*
 * Like rmalloc but wait, if necessary, until space is available.
 */
ulong_t
rmalloc_wait(register struct map *mp, size_t size)
{
	register ulong_t addr;

	if ((int)size <= 0)
		panic("rmalloc_wait");

	mutex_enter(&maplock(mp));

	while ((addr = rmalloc_locked(mp, size)) == 0) {
		mapwant(mp) = 1;
		cv_wait(&map_cv(mp), &maplock(mp));
	}

	mutex_exit(&maplock(mp));
	return (addr);
}

/*
 * Like rmalloc but called with lock on map held.
 */
ulong_t
rmalloc_locked(register struct map *mp, size_t size)
{
	register struct map *ep = mapstart(mp);
	register ulong_t addr;
	register struct map *bp;

	/*
	 * Checks for positive size and lock held.
	 * Don't do these checks unless debug is defined.
	 * That way this can be a leaf routine (and faster).
	 * Callers (usually rmalloc or rmalloc_wait) do the right thing.
	 */
	ASSERT((int)size > 0);
	ASSERT(MUTEX_HELD(&maplock(mp)));

	/*
	 * Search for a piece of the resource map which has enough
	 * free space to accomodate the request.
	 */
	for (bp = ep; bp->m_size; bp++) {
		if (bp->m_size >= size) {
			/*
			 * Allocate from the map.
			 * If there is no space left of the piece
			 * we allocated from, move the rest of
			 * the pieces to the left and increment the
			 * free segment count.
			 */
			addr = bp->m_addr;
			bp->m_addr += size;
			if ((bp->m_size -= size) == 0) {
				do {
					bp++;
					(bp-1)->m_addr = bp->m_addr;
				} while (((bp-1)->m_size = bp->m_size) != 0);
				mapfree(mp)++;
			}
			return (addr);
		}
	}
	return (0);
}

/*
 * Free the previously allocated space at addr
 * of size units into the specified map.
 * Sort addr into map and combine on
 * one or both ends if possible.
 */
void
rmfree(struct map *mp, size_t size, ulong_t addr)
{
	struct map *firstbp;
	register struct map *bp;
	register int t;

	mutex_enter(&maplock(mp));

	/*
	 * Address must be non-zero and size must be
	 * positive, or the protocol has broken down.
	 */
	if (addr == 0 || (int)size <= 0)
		goto badrmfree;

	/*
	 * Locate the piece of the map which starts after the
	 * returned space (or the end of the map).
	 */
retry:
	firstbp = bp = mapstart(mp);
	for (; bp->m_addr <= addr && bp->m_size != 0; bp++)
		continue;
	/*
	 * If the piece on the left abuts us,
	 * then we should combine with it.
	 */
	if (bp > firstbp && (bp-1)->m_addr+(bp-1)->m_size >= addr) {
		/*
		 * Check no overlap (internal error).
		 */
		if ((bp-1)->m_addr+(bp-1)->m_size > addr)
			goto badrmfree;
		/*
		 * Add into piece on the left by increasing its size.
		 */
		(bp-1)->m_size += size;
		/*
		 * If the combined piece abuts the piece on
		 * the right now, compress it in also,
		 * by shifting the remaining pieces of the map over.
		 * Also, increment free segment count.
		 */
		if (bp->m_size && addr+size >= bp->m_addr) {
			if (addr+size > bp->m_addr)
				goto badrmfree;
			(bp-1)->m_size += bp->m_size;
			while (bp->m_size) {
				bp++;
				(bp-1)->m_addr = bp->m_addr;
				(bp-1)->m_size = bp->m_size;
			}
			mapfree(mp)++;
		}
		goto done;
	}
	/*
	 * Don't abut on the left, check for abutting on
	 * the right.
	 */
	if (addr+size >= bp->m_addr && bp->m_size) {
		if (addr+size > bp->m_addr)
			goto badrmfree;
		bp->m_addr -= size;
		bp->m_size += size;
		goto done;
	}
	/*
	 * Don't abut at all.  Check for map overflow.
	 * Discard the smaller of the last/next-to-last entries.
	 * Then retry the rmfree operation.
	 */
	if (mapfree(mp) == 0) {
		ulong_t	lost_start, lost_end;

		/* locate final entry */
		for (firstbp = bp; firstbp->m_size != 0; firstbp++)
			continue;

		/* point to smaller of the last two segments */
		bp = firstbp - 1;
		if (bp->m_size > (bp-1)->m_size)
			bp--;
		lost_start = bp->m_addr;
		lost_end = bp->m_addr + bp->m_size;

		/* destroy one entry, compressing down; inc free count */
		bp[0] = bp[1];
		bp[1].m_size = 0;
		mapfree(mp)++;
		mutex_exit(&maplock(mp));	/* drop lock for printf */
		cmn_err(CE_WARN, "!%s: rmap overflow, lost [%d, %d]\n",
			mapname(mp),
			(int)lost_start, (int)lost_end); /* log message */
		mutex_enter(&maplock(mp));	/* reacquire lock */
		goto retry;
	}
	/*
	 * Make a new entry and push the remaining ones up
	 */
	do {
		t = bp->m_addr;
		bp->m_addr = addr;
		addr = t;
		t = bp->m_size;
		bp->m_size = size;
		bp++;
	} while ((size = t) != 0);
	mapfree(mp)--;		/* one less free segment remaining */
done:
	/* if anyone blocked on rmalloc failure, wake 'em up */
	if (mapwant(mp)) {
		mapwant(mp) = 0;
		cv_broadcast(&map_cv(mp));
	}
	mutex_exit(&maplock(mp));
	return;

badrmfree:
	mutex_exit(&maplock(mp));
	panic("bad rmfree");
}

/*
 * Allocate 'size' units from the given map, starting at address 'addr'.
 * Return 'addr' if successful, 0 if not.
 * This may cause the creation or destruction of a resource map segment.
 *
 * This routine will return failure status if there is not enough room
 * for a required additional map segment.
 */
rmget(register struct map *mp, size_t size, ulong_t addr)
{
	register struct map *ep = mapstart(mp);
	register struct map *bp, *bp2;

	if ((int)size <= 0)
		panic("rmget");

	ASSERT(MUTEX_HELD(&maplock(mp)));
	/*
	 * Look for a map segment containing the requested address.
	 * If none found, return failure.
	 */
	for (bp = ep; bp->m_size; bp++)
		if (bp->m_addr <= addr && bp->m_addr + bp->m_size > addr)
			break;
	if (bp->m_size == 0) {
		return (0);
	}

	/*
	 * If segment is too small, return failure.
	 * If big enough, allocate the block, compressing or expanding
	 * the map as necessary.
	 */
	if (bp->m_addr + bp->m_size < addr + size) {
		return (0);
	}
	if (bp->m_addr == addr) {
		if (bp->m_size == size) {
			/*
			 * Allocate entire segment and compress map
			 * Increment free segment map
			 */
			bp2 = bp;
			while (bp2->m_size) {
				bp2++;
				(bp2-1)->m_addr = bp2->m_addr;
				(bp2-1)->m_size = bp2->m_size;
			}
			mapfree(mp)++;
		} else {
			/*
			 * Allocate first part of segment
			 */
			bp->m_addr += size;
			bp->m_size -= size;
		}
	} else {
		if (bp->m_addr + bp->m_size == addr + size) {
			/*
			 * Allocate last part of segment
			 */
			bp->m_size -= size;
		} else {
			/*
			 * Allocate from middle of segment, but only
			 * if table can be expanded.
			 */
			if (mapfree(mp) == 0) {
				return (0);
			}
			for (bp2 = bp; bp2->m_size != 0; bp2++)
				continue;

			/*
			 * Since final m_addr is also m_nam,
			 * set terminating m_size without destroying m_nam
			 */
			((bp2--)+1)->m_size = 0;

			while (bp2 > bp) {
				(bp2+1)->m_addr = bp2->m_addr;
				(bp2+1)->m_size = bp2->m_size;
				bp2--;
			}
			mapfree(mp)--;

			(bp+1)->m_addr = addr + size;
			(bp+1)->m_size =
			    bp->m_addr + bp->m_size - (addr + size);
			bp->m_size = addr - bp->m_addr;
		}
	}
	return (addr);
}
