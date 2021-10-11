/*
 *	client_cache_interface.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)client_cache_interface.cc	1.9	92/09/23 SMI"

/*
 * Ported from SCCS version :
 * "@(#)client_cache_interface.cc  1.11  91/03/14  Copyr 1988 Sun Micro";
 */


/* The C interface to NisSharedCache  class  functions */


#include "../gen/nis_local.h"
#include <stdlib.h>
#include <syslog.h>
#include <sys/types.h>

#include <rpc/rpc.h>
#include <rpc/types.h>
#include <rpcsvc/nis.h>
#include <rpcsvc/nis_cache.h>
#include "client.h"

/* global structure used by the client routines to access the cache */

static mutex_t clientCache_lock = DEFAULTMUTEX;    /* lock level 3 */
static NisBindCache *clientCache = NULL; /*protected by clientCache_lock */

extern "C" {

/*
 * Initializes the client cache. Allocates the global data strucuture 
 * NisSharedCache which is used by the other cache routines.
 */

nis_error
__nis_CacheInit(NisBindCache **cache)
{
	nis_error status = NIS_SUCCESS;

	mutex_lock(&clientCache_lock);
	if (clientCache == NULL) {
		clientCache = new NisSharedCache(CACHEFILE, &status);
		if (clientCache == NULL || status != NIS_SUCCESS) {
			clientCache = new NisLocalCache(LOCAL_CACHE_ENTRIES, &status);
			if (clientCache == NULL)  {
				status = NIS_NOMEMORY;
			}
		}
	}
	*cache = clientCache;
	mutex_unlock(&clientCache_lock);
	return (status);
}



/*
 * The C interface to NisSharedCache::bindDir()
 * returns a directory structure for a given dir_name. 
 * See also: more details in the comments for NisSharedCache::bind()
 */

nis_error
__nis_CacheBind( char *dir_name, directory_obj *do_p )
{
	nis_error	status;
	NisBindCache *cptr; /* cache pointer */

	if ( (status =  __nis_CacheInit(&cptr)) != NIS_SUCCESS)
		return (status);
		
	memset( (void*)do_p, 0, sizeof(directory_obj) );
	return ( cptr->bindDir( dir_name, do_p ));
}



/*
 * The C interface to NisSharedCache::removeEntry()
 * removes an entry from the cache.
 * See also: more details in the comments for NisSharedCache::removeEntry()
 */

bool_t
__nis_CacheRemoveEntry( directory_obj *slistp )
{
	NisBindCache *cptr; /* cache pointer */

	if (__nis_CacheInit(&cptr) != NIS_SUCCESS)
		return (FALSE);

	return (cptr->removeEntry( (NisDirCacheEntry*)slistp));

}
	


/*
 * The C interface to NisSharedCache::read_coldstart().
 * It tells the caching system to reinitialize from the coldstart file.
 * sends a message to cachemgr if the cachefile is valid to do this or
 * if local_cache is valid reads in the coldstart on its own.
 */

void
__nis_CacheRestart()
{
	NisBindCache *cptr; /* cache pointer */

	if(__nis_CacheInit(&cptr) == NIS_SUCCESS)
		cptr->readColdStart();
}
	


/*
 * The C interface to NisSharedCache::search()
 * searches the cache for a given directory_name
 * See also: more details in the comments for NisSharedCache::search()
 * returns nis_error
 */

nis_error
__nis_CacheSearch( char *dir_name, directory_obj *do_p )
{

	nis_error 	err;
	NisBindCache *cptr; /* cache pointer */

	if ( (err =  __nis_CacheInit(&cptr)) != NIS_SUCCESS)
		return (err);

	if (cptr->search(dir_name, 
				(NisDirCacheEntry*)do_p, &err, FALSE) == MISS)
		err = NIS_NOTFOUND;
	return (err);
}


/*
 * The C interface to NisSharedCache::print()
 * dumps the entrire cache on stdout.
 */

void
__nis_CachePrint()
{
	NisBindCache *cptr; /* cache pointer */

	if (__nis_CacheInit(&cptr) == NIS_SUCCESS)
		cptr->print();
}



}  /* extern "C" */


extern "C"
bool_t
__nis_CacheAddEntry(fd_result *f_result, NisDirCacheEntry *sp)
{
	NisBindCache *cptr; /* cache pointer */

	if (__nis_CacheInit(&cptr) != NIS_SUCCESS)
		return (FALSE);
	return ( cptr->addEntry(f_result, sp) );
}

extern "C"
void
__nis_CacheRefreshEntry(char *dir_name)
{
	NisBindCache *cptr; /* cache pointer */

	if (__nis_CacheInit(&cptr) == NIS_SUCCESS)
		cptr->refreshEntry(dir_name);
}


/*
 * Creates a local cache. Called when it is detected that the shared
 * cache is no longer valid.
 * 
 * NOTE: This has the side effect of changing the global variable 'clientCache'
 * to point to the local cache so that the next time any of the cache
 * functions are called, the go straight to the local cache.
 */

nis_error 
__nis_switch_to_local_cache(NisLocalCache **lc, mutex_t *lc_lockp)
{
	nis_error status;
	
	/* Important: lc_lockp MUST be a mutex that is 	     */
	/* lower in the lock hierachy than cache_switch lock */
	/* otherwise  we may get a dead lock situation.	     */
	/* also there should not be any mutex held with      */
	/* lock level < 3 before this fuction is called      */
	mutex_lock(&clientCache_lock); /* level 3 lock */
	mutex_lock(lc_lockp);		 
	if (*lc != NULL) {
		status = NIS_SUCCESS;
	} else {

		if ( (*lc =  new NisLocalCache(LOCAL_CACHE_ENTRIES, &status)) == NULL)  {
			status = NIS_NOMEMORY;
		} else {
			/* update cache ptr	*/
		        /* so next time we  	*/
			/* go directly to the 	*/
			/* the local cache.  	*/
			if (status == NIS_SUCCESS)
				clientCache = *lc; 
		}
	}

	mutex_unlock(lc_lockp);
	mutex_unlock(&clientCache_lock); /* level 1 lock */
	return (status);
}
