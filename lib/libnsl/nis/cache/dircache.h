/*
 *	dircache.h
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)dircache.h	1.11	93/07/15 SMI"

#ifndef _DIRCACHE_H
#define _DIRCACHE_H

#include 	<stdio.h>
#include 	<sys/types.h>
#include 	<sys/ipc.h>
#include 	<sys/sem.h>
#include 	<rpc/rpc.h>
#include 	<malloc.h>
#include 	<string.h>
#include 	<sys/time.h>
#include 	<syslog.h>
#include 	<rpcsvc/nis.h>
#include 	<rpcsvc/nis_cache.h>

#include 	"cache_entry.h"
#include 	<thread.h>
#include 	<synch.h> 


#ifdef MT_LOCKS
#define CACHE_LOCK_FILE "/var/nis/NIS_SHARED_DIRCACHE_LOCK"
#define TMP_CACHE_LOCK_FILE "/var/nis/NIS_SHARED_DIRCACHE_LOCK.TMP"

enum lock_cmd {DO_NOT_LOCK = 0, LOCK_IT = 1};
enum lock_state {LOCK_IS_INVALID = 0, LOCK_IS_VALID = 1};
typedef enum lock_cmd lock_cmd_t;
typedef enum lock_state lock_state_t;

struct share_lock {
	lock_state_t  _lock_is_valid;
	/* do we want to put a version field here ?*/
	mutex_t _lock;
	cond_t _no_reader;
	cond_t _no_writer;
	unsigned long _reader_cnt;
	unsigned char _write_pending, _read_pending;
};

typedef struct share_lock share_lock_t;
#endif /*MT_LOCKS*/

#define CACHE_FILE_VERS 	1    // version of the shared cache file 


#define SEM_OWNER_READ         	0400
#define SEM_OWNER_ALTER         0200
#define SEM_GROUP_READ          0040
#define SEM_GROUP_ALTER         0020
#define SEM_OTHER_READ          0004
#define SEM_OTHER_ALTER         0002

#ifndef MT_LOCKS
#define NIS_SEM_R_KEY           100302   // key for the reader semaphore 
#define NIS_SEM_W_KEY           100303   // key for the writer semaphore 
#define NIS_W_NSEMS             2        // number of semaphores in the writer array 
#define NIS_R_NSEMS             1        // number of semaphores in the reader array 

// in the writer array 
#define NIS_SEM_MGR_UP        	0        // is the cache manager running? 
#define NIS_SEM_MGR_EXCL      	1        // Manager wants exclusive use 

// in the reader array 
#define NIS_SEM_READER        	0        // semaphore set when clients accessing cache
#else
/* We do not use reader semaphore in MT mode - we have a cheaper lock */
/* In fact, We are only using the writer semaphore to detect if the   */
/* cache manager is running, It is NOT used to write_lock the cache   */
/* ### MAY BE WE SHOULD RENAME THIS TO A MORE APPROIATE NAME ??       */
#define NIS_SEM_W_KEY           100303   // key for the writer semaphore 
#define NIS_W_NSEMS             1        // number of semaphores in the writer array 
#define NIS_SEM_MGR_UP        	0        // is the cache manager running? 
#endif /*!MT_LOCKS*/




// the header structure at the top of the cache mapped file 

struct mapFileHeader {
	int 	version;	// version of this cachefile protocol
	int     isInValid;    	// cache file is valid ? 
	int	headerSize;     // size of this header
	int 	count;		// number of cache entries in the file 
	int 	mapLen;		// size of the map(cache) file 
	long 	totalSize;	// total size of all data 
	char    uaddr[1];       // placeholder for char *uaddr of cachemgr
};



					

class NisCfileEntry {
      public:
	
	// makes the char** array point to the 
        // components of the name

	int 		get_broken_name(char**);   	
	// makes a string name from the broken up name in 	
	char*		make_name();	
	                                // the cachefile
	void set_dirlevels(int levels) 
	  {
		  dirLevels = levels;
	  }
	int  	get_dirlevels() 
	  {
		  return (dirLevels);
	  }
	int 	get_size()
	  {
		  return (size);
	  };
	
	void 	set_size(int s)
	  {
		  size = s;
	  };
	
	u_int 		set_dirname(char **name, int levels);

	void 	      	set_dataoffset(u_int);  // sets offset to data portio
	char* 		datap();	    	// returns pointer to data part 
	void 		set_offset();           // offset to next entry

	u_int 		get_offset() {
		return (offset);
	}

	// Gives a pointer to the next entry in the mapped file. i.e. 
	// a pointer to the byte after this entry 
	NisCfileEntry* 	next() {
		return( (NisCfileEntry *) ((char *)this + this->offset));
	}
	
	u_long 		get_ttl();
	void 		set_ttl(u_long ttl);      
	void 		set_flags(u_int f) 
	  {
		  flags = f;
	  }
		
	int 		distance(char** tgt_broken_name, int tgt_lvls, bool_t *is_prefix);
	
	// print out the entry in the mapped file 
	void 		print();	
	// print out just the dir name
	void 		printDirName();            

	int 		sz_cfile() 
	{ 
		return (sizeof (class NisCfileEntry) - sizeof(char)); 
	}
	
	
private:	
	// offset is the total size of this entry including the header for 
	// the entry and the data portion. (offset + current_pointer) points 
	// to next entry
	 
	int 	offset;       	// offset to start of next entry  
	int 	size;		// size of the entry (data part) 	
	u_int 	flags;          // flags
	long 	expTime;    	// expiry time for entry in seconds - clock time
	
	u_int 	dataOffset;     // offset from this pointer to start of data portion

	int 	dirLevels;      // levels in dir_name

	// this is only to get at the address for this location. 
	// The length of the name can be arbitrary
	char 	dir_name[1];	// directory_name
	
};


// printf dir name in dot form in right order.
void print_dir_name( char**, int levels);




class NisDirCache {
#ifdef MT_LOCKS
     private:

	mutex_t fd_lock;		// lock level 12
	int lock_file_fd;	      	// protected by fd_lock mutex
	share_lock_t *share_lock_ptr; 	// protected by fd_lock mutex

	void 	unmap_lock();
	bool_t	map_lock();
#endif /*MT_LOCKS*/

	
      public:
	
	void print();         	       // print the cache on stdout

      protected:
	
	int    	*version;            	// version of the cache file 
	caddr_t base;  		       	// base(addr) of the mapped file 
	int    	*headerSize;           	// size of the header in the cachefile
	char   	*cachefile;		// name of mmaped cachefile 
	int     *isInValid;  	        // whether map is valid 
	int     *mapLen;     	        // size of the cachefile 
	int     *count;   		// count of cached entries 
	NisCfileEntry *root; 		// base cache entry in the mapped file 
	long	*totalSize;  		// size of the file currently used 
	char    *uaddr;  	        // uaddr for cachemgr
#ifndef MT_LOCKS
	int 	sem_reader;             // semaphore id for reader 
	int 	sem_writer;             // semaphore id for write 
	
	int 	unlock_shared();       	// 0 on success, -1 on error
	int 	lock_shared();	        // 0 on success, -1 on error
#else
	int 	sem_writer;             // semaphore id for write 
					// it is only used to detect
					// if the cachemgr is running

	lock_state_t	bad_lock;
	lock_state_t   *lock_is_valid;
	mutex_t         *DirCache_lockp;	// lock level 13, this mutex is
						// used to implement the share
						// lock for the share cache
	cond_t       *no_reader, *no_writer;
	unsigned long   *reader_cnt;
	unsigned char   *write_pending, *read_pending;
        void    unlock_shared();
        int     lock_shared();
        bool_t  lock_constr();   // map in the lock, cachemgr is 
                                 // supposed to create the lock
#endif /*!MT_LOCKS*/

	void	unlock_exclusive();
	int	lock_exclusive();

	bool_t 	isMgrUp();             	// check to see if cachemgr is running
	NisCfileEntry *getHomeEntry() 	// directory obj from cold start file.
	{
		return ((NisCfileEntry *) (base + *headerSize));

	}

	bool_t 	isValid();               // check for validity of shared cache file
};




// used for aligning data in the cachefile on word boundaries 

inline int 
__nis_align (int x) 
{
	return ((x + 3) & ~3);
}




// write out (in XDR) a given directory object into a file 
bool_t writeColdStartFile(char* filename , directory_obj*);

// read a directory object from a cold start file 
bool_t readColdStartFile(char* filename, directory_obj*);

extern void __nis_print_sems(int sem_writer, int sem_reader);



#ifndef _sys_sem_h  // this guard is defined only in 4.1 systems
// this is to be included only in 5.0 as it is not defined there 
union semun {
	int val;
	struct semid_ds *buf;
	ushort *array;
};
#endif /* _sys_sem_h  */



// calculates the MD5 checksum and Encrypts it using DES.
// It takes a des key to use for encryption
extern "C" void __nis_calculate_encrypted_cksum(unsigned datalen, 
						unsigned char *data, 
						char *des_key, 
						unsigned *digestlen, 
						unsigned char **digest);


extern bool_t __nis_directory_objs_equal(directory_obj *o1, directory_obj *o2);
extern "C" bool_t xdr_directory_obj(XDR *, directory_obj *);


#endif /* _DIRCACHE_H */
	



