/*
 *	client_search.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)client_search.cc	1.11	93/04/08 SMI"

/* 
 * Ported from SCCS version : 
 * "@(#)client_search.cc  1.7  91/03/13  Copyr 1988 Sun Micro";
 * 
 *  This file contains the NisSharedCache :: search() routine that
 * searches the shared cache for specified entries.
 */

#include        "../gen/nis_local.h"
#include	<stdlib.h>
#include	<sys/types.h>
#include	<sys/file.h>
#include	<sys/mman.h>
#include 	<unistd.h>
#include 	<rpc/rpc.h>
#include 	<string.h>
#include	<values.h>
#include	<rpcsvc/nis.h>
#include	<rpcsvc/nis_cache.h>

#include	"client.h"




/*
 * Search the cache for target directory name.
 * Returns:
 *	 HIT if exact match found
 *       MISS if not found (should not happen)
 *	 NEAR_MISS if an exact match is not found but a "closer" entry is found.
 * It returns a pointer to the NisDirCacheEntry that is "closest" to the target name.
 * This pointer points to a structure that is reused each time this function is
 * called so the caller should make a copy of the entry.
 * 
 * closer is defined by the _name_distance()  function which a returns a distance
 * between the two names supplied to it. The search() routine returns the
 * closest name in case of NEAR_MISS.
 *
 * If two directories are the same "distance" from the desired one the directory
 * which is closer to the root is returned.
 * 
 * If the shared cache is inaccessible then it does these operations in the 
 * LRU cache local to this process.
 * Memory handling:
 * If the cache is local it returns a pointer to the local cache in *f.
 * It returns a pointer to a copy of the cache entry
 * and returns that in *f. It allocates memory to hold the cache entry 
 * the caller is responsible for freeing this memory
 */


pc_status 
NisSharedCache :: search( 
			 char* 	    	target,      // the target directory name
			 NisDirCacheEntry *retEntry, // directroy object returned
			 nis_error 	*errp,       // error returned
			 bool_t 	prefixOnly)  // search for prefix matches?
{
	NisCfileEntry		*cfp;     	// current cache entry
	NisCfileEntry 		*minCfp;  	// closest cache entry found so far
	int 			minDistance;  	// minimum distance so far
	int 			max;          	// max entries in the cache
	int 			dist;            
	char 			**tgt;
	int 			tgtLevels;
	int 			i;
	bool_t 			is_prefix;
	struct timeval 		now;
	pc_status 		result;
	sigset_t		oset;
	

	minDistance = MAXINT;
	minCfp = NULL;
	*errp = NIS_SUCCESS;
	memset((void*)retEntry, 0, sizeof(directory_obj));

	tgt = __break_name(target, &tgtLevels);
	if (!tgt) {
		*errp = NIS_NOMEMORY;
		return (MISS);
	}
	
	thr_sigblock(&oset);
	for (;;) {
		if (lock_shared()) 
			goto local;

		if ( !isValid() ) {
			unlock_shared();
			goto local;
		}
		if (lastSize != *mapLen) {
			unlock_shared();
			if (!this->remap()) {
				goto local;
			}
			continue;
		} 

		break;
	}

	// start searching the shared cache
	max = *count;
	getthetime(&now);
	
	// check the home entry
	minCfp = getHomeEntry();
	minDistance = minCfp->distance(tgt, tgtLevels, &is_prefix);
	
	// if we are interested in prefix only and home entry is not a prefix
	// then forget it
	if (prefixOnly &&  !is_prefix) {
		minCfp = NULL;
		minDistance = MAXINT;
	}
	
	if (minDistance == 0)
		goto gotit;
	
	for ( cfp = (getHomeEntry())->next(), i = 0; i < max; i++, cfp = cfp->next() ) {

		// The directory names in the shared cache are in 
		// increasing order of number of levels
		// so if prefixOnly no need to search further
		if (prefixOnly && (cfp->get_dirlevels() > tgtLevels))
			break;

		if (cfp->get_dirlevels() - tgtLevels > minDistance) 
			break;
		dist = cfp->distance(tgt, tgtLevels, &is_prefix);
		// if looking only for prefix matches and this is not
		// then forget it
		if (prefixOnly && !is_prefix)
			continue;
		// if the distance of the current one is less than the
		// present least distnace make this one minCfp
		if (dist < minDistance) {
			minDistance = dist;
			minCfp = cfp;
		}
		if (dist == 0)
			break;
	}
	
      gotit:
	// free memory allocated by __break_name() etc.
	free((char*) *(tgt + tgtLevels - 1));
	free((char*) tgt);
	
	if (!minCfp) {
		*errp = NIS_NAMEUNREACHABLE;
		unlock_shared();
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);
		return (MISS);    
	}
	
	// make a copy of the entry to return it
	// can't return pointer as that may change.
	if ( retEntry->myConstructor( minCfp ) == FALSE ) { 
		*errp = NIS_NOMEMORY;
		unlock_shared();
		thr_sigsetmask(SIG_SETMASK, &oset, NULL);
		return (MISS);
	}
	
	unlock_shared();
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	
	if (minDistance == 0)
		result = HIT;
	else
		result = NEAR_MISS; 
	
	return ( result );

      local:
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	/* 
	 * come here when the shared cache operation fails for some 
	 * reason.
	 * create the LocalCache, nis_switch_to_local_cache() updates the
	 * variable ClientCache in client_cache_interface.cc so that future
	 * requests go directly to the local cache and do not come to the
	 * shared cache.
	 */
	*errp = __nis_switch_to_local_cache(&lCache, &lCache_lock);

	if (*errp != NIS_SUCCESS)
		return (MISS);

	/* LOCK Note:  no need to protect the lCache pointer	    */
	/* because, once initailzed, it will stay constant, and     */
	/* __nis_create_local_cache() just return NIS_SUCCESS	    */
	/* So it must have been intializied.			    */
	return ( lCache->search( target, retEntry, errp, prefixOnly) );
}


