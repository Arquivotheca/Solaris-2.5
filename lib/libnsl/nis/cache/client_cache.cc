/*
 *	client_cache.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)client_cache.cc	1.17	95/03/09 SMI"

/*
 * Ported from SCCS version :
 * "@(#)client_cache.cc  1.41  91/03/19  Copyr 1988 Sun Micro";
 *
 * This file contains most of the NisSharedCache  class  functions 
 * The other functions are in client_search.cc
 * NisSharedCache is derived from DirCache which is defined in dircache.h
 * Most these functions are called from corresponding C interfaces that
 * are defined in client_cache_interface.cc
 * There is a per process variable, NisSharedCache that points to the
 * one instance of the NisSharedCache.
*/

#include 	"../gen/nis_local.h"
#include 	<stdlib.h>
#include 	<sys/fcntl.h>
#include 	<sys/mman.h>
#include 	<unistd.h>
#include 	<string.h>
#include 	<syslog.h>
#include 	<values.h>
#include 	<rpcsvc/nis_cache.h>
#include 	"client.h"



/* LOCK note: read only variable; no locking nereded */
/* Default timeout can be changed using clnt_control() */
static struct timeval TIMEOUT = { 25, 0 };

/* 
 * The constructor for the NisSharedCache class.
 *
 * The constructor first tries to map in the per-machine shared cache that 
 * is maintained by the cachemgr process.
 * It returns an errorr in statp if an error is encountered in initalizing.
 * The error returned can be a out of memory error, or if there is no 
 * cold_start file to initialize the  cache from.
 */

NisSharedCache :: NisSharedCache(char* mapfile, nis_error *statp)
{

#ifndef MT_LOCKS
	sem_reader = sem_writer = -1;
#else
        sem_writer = semget(NIS_SEM_W_KEY, NIS_W_NSEMS, 0);
	mutex_init(&lCache_lock, USYNC_THREAD, NULL);
	mutex_init(&cacheclnt_lock, USYNC_THREAD, NULL);
#endif /* ! MT_LOCKS */
	lCache = NULL;
	*statp = NIS_SUCCESS;
	lastSize = -1; // to indicate cache is uninitialized
	last_uaddr = NULL;
	
	
	if (!lock_constr() || (lock_exclusive() == -1) ) {
		*statp = NIS_FAIL;
		return;
	}
	
	if ( (cachefile = strdup(mapfile)) == NULL) {
		*statp = NIS_NOMEMORY;
		unlock_shared();
		return;
	}
	if (!this->remap_MTunsafe() || (*version != CACHE_FILE_VERS)) {
		*statp = NIS_FAIL;
	}
	unlock_exclusive();
}




/* 
 * basic algorithm:
 * will look in the cache for the name.
 * if cache returns HIT then we are done.
 * if it returns a MISS (we cannot be done) there is some error.
 * 	the cache cannot return a total MISS. The local directory object
 *	from coldstart or otherwise should always be there.
 * if cache_search returns a NEAR_MISS we pass of the directory object returned
 * iteratively to nis_find_domain() to get us nearer to the desired directory.
 * The results are then also passed along to the cache manager process via a 
 * message so that the the cachemanager can update the shared system wide cache
 * if the information provided by this client is from a verifiable source.
 * 
 * The information returned by the find_domain() call is an Xdr'ed directory object
 * along with a signature that is used by the cache manager to verify the information.
 * 
 * NOTE: It returns a pointer to an allocated directory object structure
 * The caller would have to free this structure by calling xdr_directory_obj with
 * XDR_FREE and then freeing the actual directory object structure
 *
 * The 's_name' variable keeps track of the search name.  It starts out
 * pointing to the 'name' argument.  If we need to back up to a parent
 * domain, we allocate a new name (stored in 'newname'), and point s_name
 * to it.
 */


nis_error
NisBindCache :: bindDir(char* name, directory_obj *do_p)
{
	fd_result 	*f_res;
	nis_error 	err;
	XDR 		xdrs;
	bool_t 		gotFromCache;
	nis_name 	s_name;
	char 		*newname = NULL;
	char 		*oldname = NULL;
	bool_t 		prefixOnly;
	pc_status	st;
	int 		loopcnt = 100;  // XXX just a hack to detect loops


	err = NIS_SUCCESS;
	prefixOnly = FALSE;
	s_name = name;

 search_again:
	
	st = search(s_name, (NisDirCacheEntry*)do_p, &err, prefixOnly);
	if (newname)
		free(newname);
	newname = NULL;

	switch (st) {

	case HIT:
		return(NIS_SUCCESS);

	case MISS:
		return(err);

	default: 
		return (NIS_SYSTEMERROR);

	case NEAR_MISS:
		// gotFromCache indicates whether this entry is from the cache
		// if it is it could be stale and hence should try to get a fresh 
		// on failure. (like auth_error)
		// if it is from a server (recently resolved) then we can just 
		// fail?? - atleast do not need to try the same name again.

		gotFromCache = TRUE;

again:
		// get NisDirCacheEntry closer to desired server from NIS 
		// keep old name of directory that is closer to target aorund to 
		// detect if server returns itelf as the next server - detect loops
		if (oldname)
			free(oldname);
		oldname = strdup(do_p->do_name);  

		f_res = nis_finddirectory(do_p, name);

		if (f_res == NULL)  { // XXX
			if (!gotFromCache) {
				if (oldname)
					free(oldname);
				xdr_free((xdrproc_t)xdr_directory_obj, (char*)do_p);
				return(NIS_NAMEUNREACHABLE);
			} else {
				// The entry is got from the cache.
				// the directory object that the cache return do_p
				// is invalid.
				// we are trying to resolve name, try to search the
				// cache for another directory object, that is
				// a prefix of the name of the directory that the
				// the cache returned the first time. This would also
				// be a prefix of 'name' that we are trying to resolve.


				// make a copy because nis_domain_of() returns a 
				// pointer into the memory area passed in.
				if ( (newname = strdup(do_p->do_name)) == NULL)
					return(NIS_NOMEMORY);

				removeEntry((NisDirCacheEntry*)do_p);

				s_name = nis_domain_of(newname);
				prefixOnly = TRUE;
				// free memory allocated in search() last time
				xdr_free((xdrproc_t)xdr_directory_obj, (char*)do_p);
				goto search_again;
			}
		}
		// free memory allocated for do_p last time.
		if (do_p->do_name != NULL) 
			xdr_free((xdrproc_t)xdr_directory_obj, (char*)do_p);
		memset( (void*)do_p, 0, sizeof(directory_obj) );

		err = f_res->status;
		if (err != NIS_SUCCESS && err != NIS_FOREIGNNS) {
			xdr_free((xdrproc_t)xdr_fd_result, (char*)f_res);
			if (oldname)
				free(oldname);
			if (newname)
				free(newname);
			return(err);
		}

		// deserialize the results which are sent as opaque (xdr'ed) data 
		xdrmem_create(&xdrs, (char *)f_res->dir_data.dir_data_val,
			      f_res->dir_data.dir_data_len, XDR_DECODE);

		if ( !xdr_directory_obj(&xdrs, do_p) ) {
			syslog(LOG_WARNING, 
			       "CacheBind: xdr_directory_obj failed\n");
			if (oldname)
				free(oldname);
			if (newname)
				free(newname);
			xdr_free((xdrproc_t)xdr_fd_result, (char*)f_res);
			return(NIS_SYSTEMERROR);
		}

#if 0
		nis_sort_directory_servers(do_p);
#endif /* 0 */

		if ( err != NIS_FOREIGNNS ) {
			// got a new entry which is closer to the target
			// add new entry into the cache
			this->addEntry(f_res, (NisDirCacheEntry*)do_p);

			// check to see if there is a loop
			if (nis_dir_cmp(do_p->do_name, name) != SAME_NAME) {
				if ( (loopcnt == 0) ||
				    (!gotFromCache &&
				     (nis_dir_cmp(oldname, do_p->do_name) == SAME_NAME)))
				{
					// loop detected. 
					// The server is returning itself as the 
					// next directory object to contact
					syslog(LOG_ERR, 
					      "possible loop detected in name space (directory name:%s", do_p->do_name);
					err = NIS_SYSTEMERROR;
					goto done;
				}
				gotFromCache = FALSE;
				// free the memory allocated for the results
				xdr_free((xdrproc_t)xdr_fd_result, (char*)f_res);
				loopcnt--;
				goto again;
			}
		}
	    done:
		xdr_free((xdrproc_t)xdr_fd_result, (char*)f_res);
		if (newname) 
			free(newname);
		if (oldname)
			free(oldname);
		return (err);
	}
}




/* 
 * remaps the file bacause of changed size 
 * If init == TRUE (startup) then does not unmap file.
 * if init == FALSE, unmaps/maps the file based on the new size (default)
 * assumes that the file is opened i.e. a valid fd is in the 
 * NisSharedCache structure.
 * (re)initializes the pointers in the NisSharedCache (DirCache)
 * structure to point to the cache (newly mapped) file.
 */
	
	
bool_t
NisSharedCache :: remap_MTunsafe()
{
	int 		size;
	int	 	newsize;
	int      	fd;			// fd of the open mmaped file 
	mapFileHeader 	*head;


	if ((fd = open(cachefile, O_RDONLY)) < 0) {
		return(FALSE);
	}

#ifdef MT_LOCKS
	ASSERT(MUTEX_HELD(DirCache_lockp));
#endif
	if (lastSize == -1) {
		// cache is being opened for the first time
		// find out the size of the map by reading in the header
		// of the map file

		size = sizeof(struct mapFileHeader);
		base = mmap((caddr_t)0, (size_t)size, PROT_READ, MAP_SHARED, fd, 
			    (off_t)0);
		if (base == (caddr_t)-1) { 
			close(fd);
			return(FALSE);
		}
		head = (struct mapFileHeader *)base;
		newsize = head->mapLen;
		munmap(base, size);
		base = mmap((caddr_t)0, (size_t)newsize, PROT_READ, MAP_SHARED, fd, 
			    (off_t)0);
		mutex_lock(&cacheclnt_lock);
		cacheclnt = NULL;
		mutex_unlock(&cacheclnt_lock);

	} else {
		// remap it 
		// first have to open the file again and then map it.
		size = lastSize;
		newsize = *mapLen;
		munmap(base, size);
		base = mmap((caddr_t)0, (size_t)newsize, PROT_READ, MAP_SHARED, fd, 
			    (off_t)0);
	}
	if (base == (caddr_t)-1) {  
		close(fd);
		return(FALSE);
	}
	// reinitialize the pointers as the cache file can be mapped
	// in at a different place.
	head = (struct mapFileHeader *)base;
	uaddr = (char*) &head->uaddr;
	headerSize = &head->headerSize;
	count = &head->count;
	mapLen = &head->mapLen;
	totalSize = &head->totalSize;
	isInValid = &head->isInValid;
	version = &head->version;
	root = (getHomeEntry())->next();
	lastSize = newsize;
	// close the file. The pages remain mapped in.
	close(fd);
	return(TRUE);
}




bool_t
NisSharedCache :: remap()
{
	bool_t rc;
	sigset_t	oset;
	
	thr_sigblock(&oset);
	lock_exclusive();
	rc = remap_MTunsafe();
	unlock_exclusive();
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);
	return (rc);
}




/* 
 * Add a NisDirCacheEntry (directory_object) in the cache.
 * This sends a message to the cache manager to update the system wide cache.
 * if this is not possible (because of cache being invalid, no cachemanager)
 * then it adds it to the LRU cache (NisLocalCache) for this process.
 */

bool_t
NisSharedCache :: addEntry( fd_result *f_result, NisDirCacheEntry *sp)
{
	CLIENT 		*clnt;
	nis_error 	status;
	char		clnt_res = 0;

	// if cachemanager is up and we can create a valid CLIENT
	// handle to it, update the entry in the shared cache
	if ( isMgrUp() ) {
		if ((clnt =  this->getCacheHandle()) != NULL) {
			clnt_call(clnt, 
				NIS_CACHE_ADD_ENTRY,
				(xdrproc_t) xdr_fd_result, (caddr_t) f_result,
				(xdrproc_t) xdr_void, (caddr_t) &clnt_res,
				TIMEOUT);
			return(TRUE);
		}
	}

	// cachemgr has stopped running
	// if local cache does not exist - create it 
	
	status = __nis_switch_to_local_cache(&lCache, &lCache_lock);

	if (status != NIS_SUCCESS) 
		return(FALSE);
		
	/* LOCK Note:  no need to protect lCache pointer 	    */
	/* because, once initailzed, it will stay constant, and     */
	/* __nis_switch_to_local_cache() just return NIS_SUCCESS    */
	/* So it must have been intializied.			    */
	// add in local cache
	return ( lCache->addEntry(f_result, sp) );
}





/*
 * Remove an entry from the cache.
 * sends a message to the cache manager to remove the entry in the
 * shared cache.
 * if the cachemgr is not up this entry is deleted in the local cache
 */

bool_t
NisSharedCache :: removeEntry( NisDirCacheEntry* slistp )
{
	CLIENT 		*clnt;
	nis_error 	status;
	char clnt_res = 0;

	if ( isMgrUp() ) {
		if ((clnt =  this->getCacheHandle()) != NULL) {
			clnt_call(clnt, NIS_CACHE_REMOVE_ENTRY,
				(xdrproc_t) xdr_directory_obj, (caddr_t) slistp,
				(xdrproc_t) xdr_void, (caddr_t) &clnt_res,
				TIMEOUT);
			return(TRUE);
		}
	}

	// cachemgr has stopped running if local cache does not 
	// exist - create it and make shared cache invalid
	// does this make sense XXX
	// we could not have gotten the entry from the local cache
	// as that would have made the shared cache invalid 
	// and we would not have come in here this time.
	
	status = __nis_switch_to_local_cache(&lCache, &lCache_lock);

	if (status != NIS_SUCCESS) 
		return(FALSE);

	/* LOCK Note:  no need to protect lCache_local pointer */
	/* because, once initailzed, it will stay constant, and     */
	/* __nis_switch_to_local_cache() just return NIS_SUCCESS	    */
	/* So it must have been intializied.			    */
	// XXX remove from local cache
	return (lCache->removeEntry( slistp ));
}




void
NisSharedCache :: refreshEntry(char *dir_name)
{
	CLIENT 		*clnt;
	char 		clnt_res = 0;

	if ( isMgrUp() ) 
		if ((clnt =  this->getCacheHandle()) != NULL) 
			clnt_call(clnt, NIS_CACHE_REFRESH_ENTRY,
			(xdrproc_t) xdr_wrapstring, (caddr_t) &dir_name,
			(xdrproc_t) xdr_void, (caddr_t) &clnt_res,
			TIMEOUT);
}	



/*
 * It tells the caching system to reinitialize from the coldstart file.
 * sends a message to cachemgr if the cachefile is valid to do this or
 * if local_cache is valid reads in the coldstart on its own.
 */

bool_t
NisSharedCache :: readColdStart()
{
	CLIENT 		*clnt;
	void 		*null = 0;
	char clnt_res = 0;

	if ( isMgrUp() ) {
		if ((clnt =  this->getCacheHandle()) != NULL) {

			clnt_call(clnt, NIS_CACHE_READ_COLDSTART,
			(xdrproc_t) xdr_void, (caddr_t) null,
			(xdrproc_t) xdr_void, (caddr_t) &clnt_res,
			TIMEOUT);
			return TRUE;
		}
	}
	if (__nis_switch_to_local_cache(&lCache, &lCache_lock) != NIS_SUCCESS)
		return FALSE;

	/* LOCK Note:  no need to protect lCache pointer */
	/* because, once initailzed, it will stay constant, and     */
	/* __nis_switch_to_local_cache() just return NIS_SUCCESS    */
	/* So it must have been intializied.			    */
	return (lCache->readColdStart());
}







/* 
 * initialize the semaphores that are created by the cachemgr
 * this creates a reader and writer semaphore.
 * The writer semaphore is writable only by the cachemgr (root)
 * and is used to indicate when the cachemgr is running. The
 * second semaphore in the writer array is used by the cachemgr
 * to indicate when it wants to lock the shared cache for writing.
 * The reader semaphore is writable by anybody and is used by
 * the clients to indicate when they are reading from the shared cache
 * file, so that the cachemgr cannot write into it.
 */

#ifndef MT_LOCKS
bool_t
NisSharedCache :: lock_constr()
{
	int 	semflg = 0;

	sem_reader = semget(NIS_SEM_R_KEY, NIS_R_NSEMS, semflg);
	sem_writer = semget(NIS_SEM_W_KEY, NIS_W_NSEMS, semflg);
	
	if (sem_reader == -1 || sem_writer == -1) {
		return (FALSE);
	} 
	return( isMgrUp() );
}
#endif /* !MT_LOCKS */





/* 
 * print the cache on stdout 
 * mainly used for debugging and by the program nisshowcahe.
 */

void
NisSharedCache :: print() 
{
	NisBindCache 	*cptr; /*cache pointer */
	sigset_t	oset;

	thr_sigblock(&oset);
	mutex_lock(&lCache_lock);
	cptr = lCache;
	mutex_unlock(&lCache_lock);
	thr_sigsetmask(SIG_SETMASK, &oset, NULL);

	if (cptr) 
		cptr->print();
	else	NisDirCache :: print();
}



/* 
 * virtual funtions for NisBindCache 
 * these should never be called.
 * These are defined here because of the vagaries of C++ code in
 * Libraries and programs that are compiled and linked using C.
 */


bool_t 
NisBindCache :: addEntry ( fd_result*, NisDirCacheEntry* )
{
	return(FALSE);
}


bool_t 
NisBindCache :: removeEntry(NisDirCacheEntry* )
{
	return(FALSE);
}


void
NisBindCache :: refreshEntry(char *)
{
}

	   
pc_status 
NisBindCache :: search(char *, NisDirCacheEntry *, nis_error *, bool_t )
{
	return(MISS);
}
	
bool_t
NisBindCache :: readColdStart()
{
	return FALSE;
}

	   
void  
NisBindCache :: print()
{
	syslog(LOG_ERR, 
	       "NIS+: NisBindCache runtime error: pure virtual function print() called");
}
