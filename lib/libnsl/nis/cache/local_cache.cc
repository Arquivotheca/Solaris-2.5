/*
 *	local_cache.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)local_cache.cc	1.12	95/01/24 SMI"

/*
 * Ported from SCCS version :
 * "@(#)local_cache.cc  1.13  91/03/19  Copyr 1988 Sun Micro";
 *
 *
 * This file contains the procedures to act on the per process
 * LocalCache that is created when the cachemgr is not running
 * and so the shared cache cannot be accessed.
 * As it is the intent, that the cachemgr should be running at all times,
 * this localCache is provided only as a fallback mechanism so that
 * processes can continue to work and access NIS+ even if the cachemgr
 * has crashed or not started.
 * Hence, this cache is a very simple minded LRU cache.
 * It is a simple array, if this become important then a more efficient
 * implementation should be provided.
 */

#include 	"../gen/nis_local.h"
#include 	<stdlib.h>
#include 	<rpc/rpc.h>
#include 	<values.h>
#include 	<string.h>
#include 	<rpcsvc/nis.h>
#include 	<rpcsvc/nis_cache.h>
#include 	"client.h"

extern "C" int strcasecmp(const char *, const char *);

/*
 * Overall Comment:
 * The home ( zero'th entry is special and is the one that is read in
 * from the cold start file. It is never deleted even when it has
 * expired.
 * ==== If the home (zeroth) entry is as much of a special case 
 *     maybe we should keep it separately from the rest?
 */
	   


/* 
 * constructor for the local cache.
 * It creates a cache with a fixed number of maxentries.
 */

NisLocalCache :: NisLocalCache(int maxentries, nis_error *statp)
{
	rwlock_init(&LocalCache_lock, USYNC_THREAD , NULL);
	max = maxentries;
	filled = 0;
	*statp = NIS_SUCCESS;

	if ( (entp = (local_entry**) calloc( max, sizeof(local_entry*))) == NULL) {
		*statp = NIS_NOMEMORY;
		return;
	}

	//  read in entry from coldstart file 
	if ( !readColdStart() ) {
		*statp = NIS_COLDSTART_ERR; 
	}
}



// return the NisDirCacheEntry with least distance to the target

enum pc_status
NisLocalCache :: search(char *target, NisDirCacheEntry *retEntry, nis_error* errp, 
			 bool_t prefixOnly)
{
	int 		i;
	enum pc_status 	result;
	char 		**tgt;
	int 		minDistance, distance;
	int 		found;
	int  		tgt_level, minLevels;;
	struct timeval 	now;
	name_pos 	pos;
	sigset_t	oset;


	found = -1;
	minDistance = MAXINT;
	minLevels = MAXINT;
	*errp = NIS_SUCCESS;
	
	getthetime(&now);
	tgt = __break_name(target, &tgt_level);
	if (!tgt) {
		*errp = NIS_NOMEMORY;
		return(MISS);
	}	
	thr_sigblock(&oset);
	rw_rdlock(&LocalCache_lock);
	for (i = 0; i < filled; i++) {
		if (prefixOnly) {

			// if levels of name in cache is more than target then
			// it cannot be a prefix - so ignore it
			if  (entp[i]->levels > tgt_level)
				continue;
			
			pos = nis_dir_cmp(entp[i]->centry->do_name, target);

			// if it is not the same name or the name in the cache is not
			// 'higher' i.e. is not a prefix then ignore it
			
			if ( ! (pos == SAME_NAME || pos == HIGHER_NAME) )
				continue;
		}

		// if any entry other than the home is expired - ignore it
		if ( now.tv_sec > entp[i]->expTime && i )
			continue;
		distance = __name_distance(tgt, entp[i]->brokenName);
		if (distance <= minDistance) {

			// if the 2 directories are at the same distance then we 
			// want to select the directory closer to the root.
			if ( (distance == minDistance) &&
			    (entp[i]->levels > minLevels)) {
				// this one further from the root - ignore.
				continue;
			}
			minDistance = distance;
			found = i;
			minLevels = entp[i]->levels;
		}
		if (distance == 0)
			break;
	}
	
	free(FREETYPE *(tgt + tgt_level - 1));
	free(FREETYPE tgt);
	
	if ( found < 0 ) {  // should never miss in cache - error!
		*errp = NIS_NAMEUNREACHABLE;
		rw_unlock(&LocalCache_lock);
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);
		return (MISS);
	}
	if ( minDistance == 0 ) 
		result = HIT;
	else 
		result = NEAR_MISS;
	// overloaded operator =, makes a copy of the entry in the cache.
	*retEntry = (*entp[found]->centry);
	rw_unlock(&LocalCache_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	adjustLru (found, target);
	return (result);
}



inline void
NisLocalCache :: freeLocalEntry(int n, bool_t resuse)
{
	ASSERT(RW_WRITE_HELD(&LocalCache_lock)); 
	// this does not free the memory for NisDirCacheEntry - optimization to
	// reuse this space.
	entp[n]->centry->myDestructor();

	free (FREETYPE (entp[n]->brokenName));
	
	// if reuse is set then this is not freed here so that the entry can 
	// be resused. 

	if (!resuse) {
		free(FREETYPE (entp[n]->centry));
		free (FREETYPE (entp[n]));
	}
}




// move the entry that has been used to the top of the LRU list
void
NisLocalCache :: adjustLru ( int used, char *name ) 
{
	int i;
	local_entry *lep;
	sigset_t	oset;
	
	if (used == 0)  // home entry not affected
		return;

	thr_sigblock(&oset);
	rw_wrlock(&LocalCache_lock);

	/*
	 *  The cache may have changed before we acquired the
	 *  write lock so we do some sanity checking.  If the
	 *  target entry is outside the current bounds, then
	 *  our entry was deleted.  We just return.  If the
	 *  entry doesn't match the directory name, then the
	 *  cache was shuffled by another thread, and we
	 *  just return.
	 */
	lep = entp[used];
	if (used >= filled ||
	    nis_dir_cmp(name, lep->centry->do_name) != SAME_NAME) {
		rw_unlock(&LocalCache_lock);
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);
		return;
	}

	// move the cache down till the used entry
	for ( i = used  ; i > 1;  i-- ) 
		entp[i] = entp[i-1];
	entp[1] = lep;  // put the entry at the first place
	rw_unlock(&LocalCache_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
}



bool_t
NisLocalCache :: addEntry_MTunsafe( fd_result *, NisDirCacheEntry *newp) 
{
	local_entry 	*newEntry;
	int 		i;
	struct timeval 	now;
	int 		place; 		// place to add - 0 for home
	bool_t 		isHome = FALSE;
	int 		freeEntry = -1;

	ASSERT(RW_WRITE_HELD(&LocalCache_lock));
	getthetime(&now);
	
	/*
	 *  If the entry is already in the cache, then we replace
	 *  it.
	 */
	for (i=0; i<filled; i++) {
		if (nis_dir_cmp(newp->do_name, entp[i]->centry->do_name) ==
							SAME_NAME) {
			freeEntry = i;
			break;
		}
	}
	// if not already in cache and the cache is full - free the last entry 
	if (freeEntry == -1 && filled == max)
		freeEntry =  max - 1;

	// first entry - has to be the home
	if (filled == 0)
		isHome = TRUE;

	if (!isHome && filled &&
	    (nis_dir_cmp(newp->do_name, entp[0]->centry->do_name) == 
	     SAME_NAME))
		isHome = TRUE; // home entry being added again
	
	if ( (newp->do_ttl <= 0) && !isHome )
		return(FALSE);

	if (isHome) {
		if ( filled )
			freeEntry = 0; // free the home entry
		else 
			filled = 1;    // this is the first one
		place = 0;
	} 
	
	if (freeEntry != -1)  {
		// free entry and resuse the memory allocated
		freeLocalEntry(freeEntry, TRUE);
		newEntry = entp[freeEntry];
	} else {
		// allocating a new entry
		if ((newEntry = (local_entry*) malloc(sizeof(local_entry)))==NULL ||
		    (newEntry->centry = (NisDirCacheEntry*)
		     calloc(1, sizeof(NisDirCacheEntry))) ==NULL )
			return(FALSE);
	}
	if (!isHome) {
		if (freeEntry == -1) {
			place = 1;
			// move the cache down  except home entry entp[0]
			i = (filled < max)? (filled-1) : (max-2);
			for ( ; i > 0; i-- ) 
				entp[i+1] = entp[i]; 
		} else {
			place = freeEntry;
		}
	}
	// copy the NisDirCacheEntry into the local cache (overload operator)
	// and create the broken name in the cache
	if ( ((*(newEntry->centry) = *newp) == FALSE) ||
	    ( (newEntry->brokenName = __break_name(newp->do_name, 
						   &newEntry->levels)) == NULL) ) {
		newEntry->centry->myDestructor();
		free(FREETYPE (newEntry->centry));
		free(FREETYPE (newEntry));
		return(FALSE);
	}

	newEntry->expTime = now.tv_sec + newp->do_ttl;

	// add entry to cache (home entry or first in the LRU)
	entp[place] = newEntry;
	if (freeEntry == -1 && !isHome )
		filled++;

	return (TRUE);
}

bool_t
NisLocalCache :: addEntry( fd_result * x, NisDirCacheEntry *newp) 
{
	bool_t rc;
	sigset_t	oset;

	thr_sigblock(&oset);
	rw_wrlock(&LocalCache_lock);
	rc = addEntry_MTunsafe( x, newp);
	rw_unlock(&LocalCache_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	return rc;
}

bool_t
NisLocalCache :: removeEntry ( NisDirCacheEntry *oldEntry )
{
	int 	i, k;

	sigset_t	oset;

	thr_sigblock(&oset);
	rw_wrlock(&LocalCache_lock);
	for (i = 0; i < filled; i++) {
		if (strcasecmp(entp[i]->centry->do_name, 
			       oldEntry->do_name) == 0) {

			// found the entry
			if ( i == 0 ) { // homeEntry
				if ( !readColdStart_MTunsafe() )
					return(FALSE);
			} else {
				freeLocalEntry(i, FALSE);
				// move the cache up  except home entry entp[0]
				for ( k = i+1 ; k < filled; k++ )
					entp[k-1] = entp[k];
				filled--;
			}
			rw_unlock(&LocalCache_lock);
			thr_sigsetmask(SIG_SETMASK, &oset, NULL);
			return(TRUE);
		}
	}
	rw_unlock(&LocalCache_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	return (FALSE);
}


// read in the coldstart file and add to the local cache

bool_t
NisLocalCache :: readColdStart()
{
	bool_t 			ret;

	sigset_t	oset;

	thr_sigblock(&oset);
	rw_wrlock(&LocalCache_lock);

	ret = readColdStart_MTunsafe();

	rw_unlock(&LocalCache_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	return (ret);
}


// read in the coldstart file and add to the local cache
// assumes LocalCache_lock is already held.

bool_t
NisLocalCache :: readColdStart_MTunsafe()
{
	NisDirCacheEntry 	s;
	bool_t 			ret;


	ASSERT(RW_WRITE_HELD(&LocalCache_lock));
	if (!readColdStartFile(COLD_START_FILE, (directory_obj*)&s)) {
		return(FALSE);
	}
	ret = addEntry_MTunsafe (NULL, &s);

	// free memory allocated by readcoldstart
	xdr_free((xdrproc_t)xdr_directory_obj, (char*)&s);
	return (ret);
}


void
NisLocalCache :: refreshEntry(char *)
{
	/* 
	 * This is called when one of the servers in a directory object
	 * returns AUTH_ERROR - possibly because the directory object is
	 * out of date.
	 * It does not make sense to refresh local cache entry.
	 * This function is needed only for the shared cache.
	 * The cachemgr in that case can go and try to get a 
	 * a possibly more up to date entry.
	 */
}



void
NisLocalCache :: print()
{
	int 		i;
	sigset_t	oset;

	if (__nis_debuglevel) {
		thr_sigblock(&oset);
		rw_rdlock(&LocalCache_lock);
		for(i = 0; i < filled; i++) {
			// hack for special format in nisshowcache
			if (__nis_debuglevel != 6) {
				if (i == 0)	
					printf("\nCold Start directory:\n");
				else 
					printf("\nNisLocalCacheEntry[%d]:\n", i);
			}
			entp[i]->centry->print();
		}
		rw_unlock(&LocalCache_lock);
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	}
}



