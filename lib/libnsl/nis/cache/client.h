/*
 *	client.h
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)client.h	1.13	95/01/24 SMI"

#ifndef _CACHE_CLIENT_H
#define _CACHE_CLIENT_H

#include "../gen/nis_clnt.h"
#include "dircache.h"

#ifdef TDRPC
#define	FREETYPE (char *)
#else
#define FREETYPE (void *)
#endif

class NisBindCache {
      public:

	void *operator new (size_t bytes)
	  { return calloc (1, bytes); }
	void operator delete (void *arg)
	  { (void) free(FREETYPE arg); }
	
	// get the NisDirCacheEntry for the name (char*). If entry not in cache 
	// it talks to NIS servers iteratively till it gets the desired server 
	// and updates the cache.
	
	nis_error 	bindDir(char*, directory_obj*);	

	// add entry in the cache
	virtual bool_t 	addEntry(fd_result*, NisDirCacheEntry*); 
	   
	// remove the entry
	virtual bool_t 	removeEntry(NisDirCacheEntry *);             

	// refresh the entry
	virtual void 	refreshEntry(char *dir_name);
	   

	// search for dirName, return neareast entry
	virtual	pc_status search(char *dirName, 
			 NisDirCacheEntry *returnEntry, 
			 nis_error *error, 
			 bool_t prefixOnly);
	
	// reinitialize from coldstart file
	virtual bool_t 	readColdStart();			

	   
	// print the cache
	virtual void  	print(); 			
	
};


struct local_entry {
	NisDirCacheEntry 	*centry;        // the cache entry
	char 			**brokenName;   // broken up name stored in a array of char*
	int 			levels;         // levels in the name
	long 			expTime;        // absolute expiry time in secs
};

#define LOCAL_CACHE_ENTRIES 35




class NisLocalCache : public NisBindCache {
	
      public: 
	
	// construct a local cache with size of maxentries
	NisLocalCache(int maxentries , nis_error *status);

	
	// add request to the cache mgr 
	bool_t 		addEntry (fd_result*, NisDirCacheEntry*);

	// remove Entry from cache
	bool_t 		removeEntry(NisDirCacheEntry*);             

	// refresh the entry
	void 		refreshEntry(char *dir_name);
	
	// search for a given dirName, return nearest entry
	pc_status 	search(char *dirName, 
			       NisDirCacheEntry *returnedEntry, 
			       nis_error *error, 
			       bool_t prefixOnly);

	bool_t 		readColdStart();

	// print the local cache
	void 		print();

      private:

	// LRU cache
	rwlock_t		LocalCache_lock;	/* lock level 11 */
	local_entry 	**entp;
	short 		max;
	short 		filled;

	void 		adjustLru(int entry_to_adjust, char *name);
	inline void 	freeLocalEntry(int entry, bool_t reuse);
	bool_t 		addEntry_MTunsafe (fd_result*, NisDirCacheEntry*);
	bool_t 		readColdStart_MTunsafe();

};







class NisSharedCache:  public  NisDirCache, public NisBindCache {

      public: 
	   
	NisSharedCache (char*, nis_error *status);
	
	bool_t 		add (NisDirCacheEntry *);
	
	// add entry in the cache
	bool_t 		addEntry (fd_result*, NisDirCacheEntry*); 
	
	// remove the entry
	bool_t 		removeEntry(NisDirCacheEntry*);             
	
	// refresh the entry
	virtual void 	refreshEntry(char *dir_name);

	// search for dirName, return neareast entry
	pc_status 	search(char *dirName, 
			       NisDirCacheEntry *returnEntry, 
			       nis_error *error, 
			       bool_t prefixOnly);
	
	// reinitialize from coldstart file
	bool_t 		readColdStart();			
	
	// print the cache
	void  		print(); 			
	
	
      private:
	
	// used to detect changes in size of map file
	int 			lastSize;	/* protected by lock_exclusive() */

	// pointer to local (in memory cache) if nis_cachemgr is not running 	
	mutex_t 		lCache_lock;	/* lock level 10 */
	NisLocalCache* 		lCache;   

	mutex_t 		cacheclnt_lock;	/* lock level ? */
	// CLIENT handle for cache manager 	   	
	CLIENT* 		cacheclnt;     /* protected by cacheclnt_lock */
	// previous uaddr of cachemgr
	char 			*last_uaddr;   /* protected by cacheclnt_lock */
	// process-id at time of cacheclnt creation
	pid_t			pid;           /* protected by cacheclnt_lock */
	// device id of fd in client handle
	dev_t			rdev;          /* protected by cacheclnt_lock */

	// get a CLIENT handle to the cachemgr 
	CLIENT* 		getCacheHandle(); /*protected by cacheclnt_lock*/

	// (re)maps the cache file 
	bool_t remap(); 			/* protected by lock_exclusive()*/
	bool_t remap_MTunsafe();

#ifndef MT_LOCKS
	// construct the  locks
	bool_t lock_constr();
#endif /*MTSAFE*/
	
};
	   

nis_error __nis_switch_to_local_cache(NisLocalCache **, mutex_t *);



#endif _CACHE_CLIENT_H


