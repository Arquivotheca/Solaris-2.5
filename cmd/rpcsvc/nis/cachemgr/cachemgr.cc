/*
 *	cachemgr.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)cachemgr.cc	1.11	93/04/08 SMI"

/* MgrCache functions (also see file cacheadd.cc) */


#include 	<stdlib.h>
#include 	<rpc/rpc.h>
#include 	<sys/file.h>
#include 	<sys/mman.h>
#include 	<sys/types.h>
#ifdef TDRPC
#include	<sysent.h>
#endif
#include 	<unistd.h>
#include 	<string.h>
#include 	<values.h>
#include 	<sys/stat.h>
#include 	<syslog.h>
#include 	<sys/ipc.h>
#include 	<errno.h>

#include 	"cachemgr.h"
#include 	"pkeyhash.h"

extern "C" void cache_cleanup_onsig();
extern void __nis_memcpy_safe(char* to, char* from,  int len);


PkeyHashtable *pkeyTbl;



MgrCache :: MgrCache(char* mapfile, bool_t initialize, int init_size, int max_size,
		     char* myuaddr)
{
	struct stat 	buf;
	int 		act_size;
	int 		hsize;

	if (geteuid() != (uid_t)0) {
		syslog(LOG_ERR, "nis_cachemgr has to be run as root");
		exit(1);
	}
	if ( (cachefile = strdup(mapfile)) == NULL) {
		syslog(LOG_ERR, "MgrCache: couldn't allocate memory :%m");
		exit(1);
	}


	// create/open the semaphores
	if (!lock_constr()) {
		syslog(LOG_ERR, "MgrCache: could not create lock semaphores: %m");
		exit(1);
	}

	if (stat (mapfile, &buf) == -1) { // some error occurred (only interested in ENOENT)
		if (errno == ENOENT) {	// mapfile does not exist => create the file 
			if ((fd = open(mapfile, O_CREAT| O_RDWR | O_SYNC, 0644)) < 0) {
				syslog(LOG_ERR, "MgrCache:could not create new cache file %s : %m", mapfile);
				exit(1);
			}
			initialize = TRUE; // even if false initially, force it to be true NOW
		}
		else {	
			syslog(LOG_ERR, "MgrCache:could not stat cache file %s : %m", mapfile);
			exit(1);
		}
	}
	else 
		if ((fd = open(mapfile, O_RDWR | O_SYNC, 0644)) < 0) {
			syslog(LOG_ERR, "MgrCache:could not open existing cache file %s : %m", mapfile);
			exit(1);
		}

	// make sure the file has the right permissions
	if (fchmod(fd, 0644) == -1) {
		syslog(LOG_ERR, 
		       "MgrCache: could not open cache file with right permissions: %m", mapfile);
		exit(1);
	}

 start_over:
	if (initialize) {
		act_size = init_size;
	} else {
		fstat(fd, &buf);
		act_size = (int) buf.st_size;
		act_size = (act_size > init_size) ? act_size:init_size;
	}
	// record the initial size of the cache file
	initSize = act_size;
	if ( !remap(act_size, TRUE, initialize) ) {
		// not enough space to write
		syslog(LOG_ERR, "initcache:could not allocate space for cachefile :'%s': %m",
		       mapfile);
		exit(1);
	};

	// if newfile flag is specified then initialize the cache fields. else 
	// let the fields be what they are in the old file.

	hsize = sizeof(struct mapFileHeader) + strlen(myuaddr) + 1;
	hsize = __nis_align(hsize);

	if ( *isInValid  == 1 ) {
		// file left in a corrupt state
		initialize = TRUE;
		goto start_over;
	}
	// mark file in while we update valid
	*isInValid = 1;

	if (initialize) {
		*mapLen = act_size;
		*count = 0;
		*headerSize = hsize;
		*totalSize = hsize;
		*version = CACHE_FILE_VERS;
		home = NULL;
		root = (NisCfileEntry *) (base + hsize);
	} else { 
		// not being initialized - reuse the old cache

		// if version mismatch
		if (*version != CACHE_FILE_VERS) {
			syslog(LOG_WARNING, 
			       "nis_cachefile version mismatch - recreating file");
			initialize = TRUE; 
			goto start_over; 
		}
		// if the new hsize (different sized uaddr) is less than the old
		// headerSize then let the old size remain and carry on.
		// else move the cache down to accomodate the new header size
		// and update *headerSize

		if (hsize > *headerSize) {
			char *startmove, *endmove;
			int movelen;

			startmove = base + *headerSize;  // base + old headerSize
			endmove = (char*) (base + *totalSize);
			movelen = hsize - *headerSize;
			movelen = __nis_align(movelen);

			*headerSize = *headerSize + movelen;

			__nis_memcpy_safe((base + *headerSize), startmove, 
					  (endmove - startmove) );

			*totalSize += movelen;
		}
		home = (NisCfileEntry *) (base + *headerSize);
		root = home->next();
	}

	// write uaddr of cachemgr that clients can use without contacting
	// rpcbind to get the loopback address of the cachemgr
	strcpy((char*)uaddr, (char*) myuaddr);

	cacheAdd = cacheDelete = cacheUpdate = 0;
	nextExpire = MAXLONG;

	// create the hashtable for storing publickeys for servers
	pkeyTbl = new PkeyHashtable();
	if (!pkeyTbl) {
		syslog(LOG_ERR, "initcache:could not allocate hashtable");
		exit(1);
	}

	maxSize = (max_size < act_size) ? act_size : max_size;
	// set the increment size for cache file
	incFileSize = __nis_getpagesize();

	// mark file valid
	*isInValid = 0;

	if (mgrVerbose)
		syslog(LOG_INFO, 
		       "cachefile : init size = %d (bytes), max size = %d (bytes)",
		       initSize, maxSize);
}




// read in the coldstart file 
// if the there is an error while reading the file it returns FALSE
// else returns TRUE;

bool_t
MgrCache :: initColdStart()
{
	NisDirCacheEntry 	s;
	bool_t			stat;

	if (!readColdStartFile(COLD_START_FILE, (directory_obj*)&s))
		return (FALSE);

	stat = add(&s, TRUE);

	// free memory allocated by readcoldstart
	xdr_free((xdrproc_t)xdr_directory_obj, (char*)&s);
	return (stat);
}


/* 
 * Remaps the cache file and adjustst the MgrCache object (header information) 
 * to point to the new address where the file gets mapped.
 * Adjusts the maplen to the size specified in newsize.
 * if init is TRUE it creates a new mapping of newsize. Used first time.
 * if init is FALSE it first unmaps and then maps the file in.
 */


bool_t
MgrCache :: remap(int newsize, bool_t firsttime, bool_t newspace)
{
	int 		oldsize;
	int 		extrasize;
	caddr_t 	addr = 0;
	mapFileHeader 	*head;
	
	if (firsttime)
		oldsize = 0;
	else 
		oldsize = *mapLen;

	if (ftruncate(fd, (size_t)newsize) < 0) {
		syslog(LOG_ERR, "remap: ftruncate failed: %m");
		return(FALSE);
	}
	if (mgrVerbose && !firsttime)
		syslog(LOG_INFO, 
		       "remap'ing cachefile: newsize = %d, oldsize = %d", 
		       newsize, oldsize);

	extrasize = newsize - oldsize;
	if (extrasize > 0 && newspace ) {
		// check to see if we actually have space on disk to write.
		// ftrucate does not guarantee that.
		char *buf;

		if ( (buf = (char*)malloc(extrasize)) == NULL)
			return(FALSE);
		memset( (void*)buf, 0, extrasize);
		if ( (lseek(fd, (int)oldsize, SEEK_SET) < 0) ||
		    (write(fd, buf, extrasize) != extrasize) ) {
			if (mgrVerbose)
				syslog(LOG_ERR, 
				       "remap:Could not increase size of file");
			free(buf);
			return(FALSE);
		}
		free(buf);
	}

	if (firsttime) {
		base = mmap(addr, (size_t)newsize, PROT_READ|PROT_WRITE, MAP_SHARED, 
			    fd, (off_t)0 );
	} else {
		// remap it
		munmap(base, oldsize);
		base = mmap(addr, (size_t)newsize, PROT_READ|PROT_WRITE, MAP_SHARED,
			    fd, (off_t) 0);
	}
	if (base == (caddr_t)-1) {
		syslog(LOG_ERR, "mmap error: %m");
		exit(2);
	}
	head = (struct mapFileHeader *)base;
	uaddr =  (char*) &head->uaddr;
	headerSize = &head->headerSize;
	count = &head->count;
	totalSize = &head->totalSize;
	version = &head->version;
	mapLen = &head->mapLen;
	isInValid = &head->isInValid;
	*mapLen = newsize;   // write the new size of the file
	if (!firsttime) {
		home = (NisCfileEntry *) (base + *headerSize);
		root = home->next();
	}
	if (msync(base, *mapLen, 0)){
		syslog(LOG_ERR, "msync operation failed : %m");
		exit(2);
	}

	return(TRUE);
}


// prints the cache manager statistics 
void 
MgrCache :: dumpStatistics()
{
	printf("\nCache Statistics: \n");
	printf("\t cacheAdds = %d, cacheUpdates = %d, cacheDeletes = %d\n\n",
	       cacheAdd, cacheUpdate, cacheDelete);
}




/*
 * loops thru the cache to find the entry with the same name as that
 * given as an argument. 
 * *foundp is set to TRUE if entry is found else set to FALSE.
 * the return value is a pointer to a cache_file entry.
 * if an entry with "name" is in cache then the return value points to it.
 * If no entry is found then the return pointer points to the memory
 * position where a new entry should be inserted in the cache.
 *
 * NOTE: This function assumes that the file is locked by the caller 
 */


NisCfileEntry*
MgrCache :: getEntry(char **nameBroken, int nameLevels, bool_t *foundp)
{
	register NisCfileEntry *cfp;
	int 		i;
	int 		max;
	bool_t 		prefix;

	max = *count;
	*foundp  = FALSE;

	for( i = 0, cfp = root;   i < max; i++, cfp = cfp->next()) {
		// asecending order of levels
		if (cfp->get_dirlevels() < nameLevels)
			continue;
		else if (cfp->get_dirlevels() > nameLevels)
			break;
		else { 
			// same number of levels
			if (cfp->distance(nameBroken, nameLevels, &prefix) == 0) {
				*foundp = TRUE;
				return(cfp);
			}
		}
	}
	return( cfp );
}




/* 
 * returns the publickey  (type, len and value)
 * returns TRUE if key found for 'server' else returns FALSE
 * This pointer points to a entry in the hashtable so could get changed
 * by calls to other routines to the hashtable.
 *
 */
	
bool_t
MgrCache :: get_server_publickey( nis_name server, u_long *pkey_type, 
			       netobj **pkey)
{
	PkeyHashEntry  	*e;
	PkeyHashEntry 	*p = new PkeyHashEntry(server);
	
	if (!p) 
		return(FALSE);

	e = (PkeyHashEntry*) pkeyTbl->lookup(p);
	delete p;
	if (!e) {
		if (mgrVerbose)
			syslog(LOG_INFO, "publickey not found for server: %s", server);
		return(FALSE);
	}

	e->get_pkey(pkey_type, pkey);

	return(TRUE);
}



/* looks through the entire cache file and purges the entries that have
 * expired.
 * should be called periodically.
 */

void
MgrCache :: purge()
{
	char 	*entryNamep = NULL;
	char 	**list;
	char 	**tmp;

	if (list = anyEntryExpired()) {
		// some entries have expired
		tmp = list;
		while(*list) {
			remove(*list, NULL);
			free(*list++);
		}
		free((void*)tmp);
	}
}


/* 
 * checks to see if any entry in the cache file has expired.
 * if there are expired entries then it returns a null terminated array of
 * names of these entries.
 * if there are no expired entries then the first entry is a null name
 * resets the alarm() for the lowest expiry time
*/


char**
MgrCache :: anyEntryExpired()
{
	NisCfileEntry 	*cfp;
	int 		i; 
	int		max;
	struct timeval 	now;
	char		**names;
	long 		minExpire = CACHE_EXPIRE;
	int 		howMany = 0;

	max = *count;
	if ( (names = (char**) malloc(sizeof(char*) * (max + 1))) == NULL) {
		return(NULL);
	}
	for( i = 0, cfp = home;   i <= max; i++, cfp = cfp->next()) {
		if ( cfp->get_ttl() <= 0 ) { 
			// entry expired
			names[howMany] = cfp->make_name();
			howMany++;
		} else {
			if (cfp->get_ttl() < minExpire)
				minExpire = cfp->get_ttl();
		}
	}
	names[howMany] = NULL;
	if (minExpire <= 0 || minExpire > CACHE_EXPIRE)
		minExpire = CACHE_EXPIRE;

	// just a delta over the exact time the next entry will expire
	minExpire += DELTAEXPIRE;
	// real time (secs) at which next entry will expire
	getthetime(&now);
	nextExpire = now.tv_sec + minExpire;

	alarm((unsigned)minExpire);
	return(names);
}



/* 
 * go thru cachefile and add all servers names to the hashtable cache
 * for the servers publickeys
 */

bool_t
MgrCache :: addPublickeys()
{
	int 		i;
	int 		n;
	int 		nServers;
	NisCfileEntry 	*cfp;
	NisDirCacheEntry	*ce;
	nis_server 		*s;
	int 		max = *count;

	ce = (NisDirCacheEntry*)malloc(sizeof(NisDirCacheEntry));
	
	for( i = 0, cfp = home;   i <= max; i++, cfp = cfp->next()) {
		ce->myConstructor(cfp);
		nServers = ce->do_servers.do_servers_len;
		for (n = 0; n < nServers; n++) {
			s = &ce->do_servers.do_servers_val[n];
			PkeyHashEntry *p = new PkeyHashEntry(s->name, s->key_type,  
							     &s->pkey);
			if(!p) {
				syslog(LOG_ERR, "addPkey:out of memory");
				return(FALSE);
			}
			pkeyTbl->update(p);
		}
		// this does not free the memory for ce
		ce->myDestructor();
	}
	free((char*)ce);
	return(TRUE);
}



/* 
 * removes a random entry from the cache.
 * Does not remove the home entry.
 */

bool_t
MgrCache :: removeRandomEntry()
{
	NisCfileEntry 	*cfp;
	int 		i;
	char 		*name;
	int 		max = *count;

	if (!max) 
		return (FALSE);

	int entry = (int) (rand() % max);
	if (entry == 0)
		entry = max - 1;
	for ( i = 0, cfp = root; i < entry; i++, cfp = cfp->next()) ;
	
	name = cfp->make_name();
	if (!name)
		return(FALSE);
	remove(name, NULL);
	free(name);
	return(TRUE);
}




/*  
 * lock constructor for the cache manager
 * sets up the semaphores
 * sets the NIS_SEM_MGR_UP semaphore.
 */

extern int errno;

bool_t
MgrCache :: lock_constr()
{
	ushort 		w_array[NIS_W_NSEMS];
	union semun 	semarg;
	int 		semflg;


	// create the writer (cache_manager) semaphore 
	semflg = IPC_CREAT;
	semflg |= SEM_OWNER_READ | SEM_OWNER_ALTER | SEM_GROUP_READ |
		  SEM_OTHER_READ | SEM_OTHER_ALTER | SEM_GROUP_ALTER;
	
	if ( (sem_writer = semget(NIS_SEM_W_KEY, NIS_W_NSEMS, semflg)) == -1) {
		syslog(LOG_ERR, "lock_constr:semget failed writer");
		return(FALSE);
	}
	// create the reader semaphore 	
	// owner read and alter 
	// group and others read  and alter 

	semflg = IPC_CREAT;
	semflg |= SEM_OWNER_READ | SEM_OWNER_ALTER | SEM_GROUP_READ |
		SEM_OTHER_READ | SEM_OTHER_ALTER | SEM_GROUP_ALTER;
	
	if ( (sem_reader = semget(NIS_SEM_R_KEY, NIS_R_NSEMS, semflg)) == -1) {
		return(FALSE);
	}

	semarg.array = w_array;
	if ( semctl(sem_writer, 0, GETALL, semarg) == -1) {
		return(FALSE);
	} 
	// check to see if cachemgr is already running 
	if (w_array[NIS_SEM_MGR_UP] != 0) {
		syslog(LOG_ERR, "WARNING: nis_cachemgr already running");
		// reset the flag
		semarg.val = 0;
		if ( semctl(sem_writer, NIS_SEM_MGR_UP, SETVAL, semarg) == -1) {
			return(FALSE);
		} 
	}
	// reset the exclusive(writer) semaphore
	semarg.val = 0;
	if ( semctl(sem_writer, NIS_SEM_MGR_EXCL, SETVAL, semarg) == -1) {
		return(FALSE);
	} 

	return (TRUE);
}



/* 
 * mark the semaphores to indicate that the cache mgr is running
 */

bool_t
MgrCache :: markMgrUp()
{
	struct sembuf 	buf;

	// indicate that the cachegmr is running 
	buf.sem_num = NIS_SEM_MGR_UP;
	buf.sem_op = 1;
	buf.sem_flg = SEM_UNDO; // will be undone when cachemgr exits
	
	// register function to cleanup on exit

#ifdef TDRPC
	on_exit( (void (*)(int, ...) )cache_cleanup_onsig, 0);
#else
	atexit( cache_cleanup_onsig );
#endif
		
	if ( semop(sem_writer, &buf, 1) == -1) {
		syslog(LOG_ERR, "semop failed while setting NIS_SEM_MGR_UP: %m");
		return(FALSE);
	} 

	return(TRUE);
}



/* 
 * Mark file invalid
 * this is done before any update so that if the cachmegr crashes, 
 * the state of the files is marked appropriately.
 */

void
MgrCache :: inValid()
{ 
	*isInValid = 1; 
	if (msync(base, *mapLen, 0)) {
		syslog(LOG_ERR, "msync operation failed : %m");
		exit(2);
	}
}


void
MgrCache :: valid()
{ 
	// paranoia? 
	if (msync(base, *mapLen, 0)) {
		syslog(LOG_ERR, "msync operation failed : %m");
		exit(2);
	}
	*isInValid = 0; 
	if (msync(base, *mapLen, 0)) {
		syslog(LOG_ERR, "msync operation failed : %m");
		exit(2);
	}
}

/* 
 * used only by the cache manager
 * first indicate that it wants to lock it exclusive by setting the
 * NIS_SEM_MGR_EXCL semaphore that indicates that the cachemgr wants exclusive 
 * access and then wait for the NIS_SEM_ACCESS semaphore to go to 0 indicating
 * that all the clients who were accessing the cache have finished.
 */

void
MgrCache :: lock_exclusive()
{
	struct sembuf 	buf;

	// set the exclusive flag - indicating that the cachemanager wants to 
	// write
	buf.sem_num = NIS_SEM_MGR_EXCL;
	buf.sem_op = 1;
	buf.sem_flg = SEM_UNDO;


	if ( semop(sem_writer, &buf, 1) == -1) {
		syslog(LOG_ERR, "semop failed: setting NIS_SEM_MGR_EXCL: %m");
		exit(1);
	} 	

	// wait for the reader count to go to 0 
	buf.sem_num = NIS_SEM_READER;
	buf.sem_op = 0;
	buf.sem_flg = 0;

	if ( semop(sem_reader, &buf, 1) == -1) {
		syslog(LOG_ERR, "semop failed for NIS_SEM_READER: %m");
		exit(1);
	} 	
}




void
MgrCache :: unlock_exclusive()
{
	struct sembuf 	buf;

	// unset the exclusive flag 
	buf.sem_num = NIS_SEM_MGR_EXCL;
	buf.sem_op = -1;
	buf.sem_flg = SEM_UNDO;

	if ( semop(sem_writer, &buf, 1) == -1) {
		syslog(LOG_ERR, "semop failed: unset NIS_SEM_MGR_EXCL: %m");
		exit(1);
	} 	
}

