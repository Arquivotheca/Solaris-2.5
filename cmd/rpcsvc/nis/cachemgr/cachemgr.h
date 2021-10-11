/*
 *	cachemgr.h
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)cachemgr.h	1.7	92/07/14 SMI"

#ifndef _CACHE_MGR_H
#define _CACHE_MGR_H

#ifdef TDRPC
#define FREETYPE (char *)
#else
#define FREETYPE (void *)
#endif

#include 	"dircache.h"


#define DELTAEXPIRE 2          // delta time in secs to disregard while purging
#define CACHE_EXPIRE  3600     // check if cache is expired

#define ENDOFFILE ((char *) (base + (*totalSize)))


class MgrCache : public  NisDirCache {
	
      public:

	void *operator new (size_t bytes)
	{ return calloc (1, bytes); }
	void operator delete (void *arg)
	{ (void) free( FREETYPE arg); }

	MgrCache(char *mapfilename, bool_t initCache, int init_filesize,
		 int max_filesize , char *uaddr);
	
	// (re)map the file
	bool_t remap(int size, bool_t firsttime = FALSE,  bool_t newspace = TRUE); 
	
	// read in the coldstart file
	bool_t initColdStart();	
	
	// add entry into cache file
	bool_t add( NisDirCacheEntry* newEntry, bool_t isHome = FALSE ); 
	
	// remove entry from cache file
	bool_t remove(char* dirName, directory_obj *);			
	
	// get publickey of principal
	bool_t get_server_publickey( nis_name serverName, u_long *key_type, 
				    netobj **pkey);
	
	// cleanup before exiting
	void cleanup();
	
	// purge expired entries from the cache
	void purge();
	
	// add all the publckeys in the cache file to in memory table
	bool_t      addPublickeys();

	// Mark that the Mgr is running	
	bool_t      markMgrUp();  

	// print performance statistics
	void dumpStatistics();		
	

      private:
	int      fd;                  	// fd of open cache map file
	long     nextExpire;           	// secs at which next entry will expire
	int      maxSize;              	// maximum size of cache file
	int      initSize;             	// initial size of the cache file
	int      incFileSize;          	// chunks to increment file size
	NisCfileEntry	*home;		// pointer to 'home' entry
		
	// performance statistics 
	int      cacheAdd;
	int 	 cacheUpdate;
	int      cacheDelete;
	
	NisCfileEntry* getEntry(char** nameBroken, int levels, bool_t * isFound);
	bool_t      isHomeEntry(char** nameBroken, int nameLevels);
	void        inValid();
	void        valid();
	char**      anyEntryExpired();
	bool_t      removeRandomEntry();
	void        releaseLocks();
	
	// semaphores 
	bool_t      lock_constr();      // construct the semaphores 
	void        lock_exclusive();   // lock the cachefile to update 
	void        unlock_exclusive(); // unlock the file to access 
};


extern MgrCache *mgrCache;  // global variable pointing to the shared cache
extern bool_t mgrVerbose;   // send status messages to syslogd

extern "C" int __nis_getpagesize();
extern "C" bool_t xdr_directory_obj(XDR *, directory_obj *);
extern "C" bool_t xdr_fd_result(XDR *, fd_result *);
extern "C" bool_t update_cold_start_entry();
extern bool_t update_cached_directory_object(char *d_name);

#endif /*  _CACHE_MGR_H  */








