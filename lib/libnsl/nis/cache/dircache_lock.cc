/*
 *	dircache_lock.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)dircache_lock.cc	1.9	92/09/24	SMI"

/*
 * Ported from SCCS Version :
 * "@(#)dircache_lock.cc  1.6  91/03/19  Copyr 1991 by Sun Micro";
 *
 *
 * dircache_lock.cc:
 * Routines to do reader/writer locking on the cache file.  
#ifndef MT_LOCKS
 * use system V semaphores
 * 
 * 2 semaphore array's:
 *     'sem_writer' to do help do exclusive locking by the cachemgr when 
 *     it wants to update the cachefile. It is MODIFY only by the creater 
 *     i.e. root (who runs nis_cachemgr).
 *     2 semaphores in sem_writer array
 *        [0] - set if the cachemgr is running. It is automatically unset 
 *              if the cachemgr crashes as SEM_UNDO flag is set on the 
 *              operation to set this semaphore.
 *        [1] - set if manager wants exlusive access to the file. 
 *              Clients wait on this semaphore to go to 0 before accessing 
 *              the cachefile.
 *
 *     'sem_reader' is modfiable by eveyrbody and is set whenever a process 
 *     is reading the cachefile. Clients increment this when they enter the 
 *     cache file and decrement this when the exit the cachegile. SEM_UNDO 
 *     flag is set on these operations so that if the client crashes 
 *     while accessing the cache file this will autoimatically decrement the 
 *     semaphore.
#endif !MT_LOCKS
 */


#include "../gen/nis_local.h"
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <rpcsvc/nis.h>
#include <sys/stat.h>
#include <errno.h>
#include "client.h"

#include <sys/mman.h>

#ifndef MT_LOCKS
static int decrement_reader(int sem_reader);



int
NisDirCache :: lock_shared()
{
	struct sembuf buf;

	if (sem_reader == -1 || sem_writer == -1)
		return(-1);

	if (!isMgrUp())
		return(-1);

	/* ALORITHM:
	 * again:
	 *     block on  MGR_EXCL flag to be 0 in sem_writer
	 *     increment reader count, in sem_reader
	 *     check on  MGR_EXCL flag to be 0 in sem_writer
	 *    if it is 0 
	 *         proceed.
	 *    else
	 *        decrement reader count, in sem_reader
	 *        goto again;
	 */

	   

      again:

	// block on  MGR_EXCL flag to be 0 in sem_writer 
	buf.sem_num = NIS_SEM_MGR_EXCL;
	buf.sem_op = 0;
	buf.sem_flg = 0;

	if ( semop(sem_writer, &buf, 1) == -1)
		return(-1);
	
	// increment reader count, in sem_reader 
	buf.sem_num = NIS_SEM_READER;
	buf.sem_op = 1;
	buf.sem_flg = SEM_UNDO;
	if ( semop(sem_reader, &buf, 1) == -1) 
		return(-1);
	

	// test (don't block) on  MGR_EXCL flag to be 0 in sem_writer 
	buf.sem_num = NIS_SEM_MGR_EXCL;
	buf.sem_op = 0;
	buf.sem_flg = IPC_NOWAIT;
	
	if ( semop(sem_writer, &buf, 1) == -1) {
		if (errno == EAGAIN) {
			// cachemanager has set the excl flag indicating it
			// wants to write
			// decrement reader count, in sem_reader
			// goto again;
			if (decrement_reader(sem_reader) != 0) 
				return(-1);
			goto again;
		} else {
			// some other failure 
			//decrement reader and return
			decrement_reader(sem_reader);
			return(-1);
		}
	}
	return(0);
}
#else
int
NisDirCache :: lock_shared()
{

        if (!(*lock_is_valid)) {
		mutex_lock(&fd_lock);
                unmap_lock();
                map_lock(); 
		mutex_unlock(&fd_lock);
        	if (!(*lock_is_valid)) { /* should have mapped in a valid lock */
			return(-1);
		}
        }

        mutex_lock(DirCache_lockp);
        while (*write_pending) {
                *read_pending = 0xff;
                cond_wait(no_writer, DirCache_lockp);
        }

        (*reader_cnt)++;
        /***********************************/
        /* if we get a signal and die here */
        /* the cachemgr will block until it*/
	/* timed out.			   */
        /***********************************/
        mutex_unlock(DirCache_lockp);
 
        return(0);
}
#endif /*!MT_LOCKS*/




#ifndef MT_LOCKS
int
NisDirCache :: unlock_shared()
{
	if (sem_reader == -1)
		return(-1);

	decrement_reader(sem_reader);
	return(0);
}
#else
void
NisDirCache :: unlock_shared()
{

#ifdef DESTROY_LOCK /* see also code in re_init_lock()  */
                    /* in cachemgr                      */
        if (!(*lock_is_valid)) {
                return;
        }
#endif

        mutex_lock(DirCache_lockp);
        if ((--(*reader_cnt) == 0) && (*write_pending))
                cond_signal(no_reader);
        /***********************************/
        /* if we get a signal and die here */
        /* the cachemgr will block until it*/
	/* timed out.			   */
        /***********************************/
        mutex_unlock(DirCache_lockp);	
}
#endif /*!MT_LOCKS*/

#ifndef MT_LOCKS
int
NisDirCache :: lock_exclusive()
{
 struct sembuf   buf;
 
        // set the exclusive flag - indicating that the cachemanager wants to
        // write
        buf.sem_num = NIS_SEM_MGR_EXCL;
        buf.sem_op = 1;
        buf.sem_flg = SEM_UNDO;
 
 
        if ( semop(sem_writer, &buf, 1) == -1) {
                return (-1);
        }        
 
        // wait for the reader count to go to 0
        buf.sem_num = NIS_SEM_READER;
        buf.sem_op = 0;
        buf.sem_flg = 0;
 
        if ( semop(sem_reader, &buf, 1) == -1) {
                return(-1);
        }
	return(0);
}

void
NisDirCache :: unlock_exclusive()
{
        struct sembuf   buf;

        // unset the exclusive flag
        buf.sem_num = NIS_SEM_MGR_EXCL;
        buf.sem_op = -1;
        buf.sem_flg = SEM_UNDO;

        semop(sem_writer, &buf, 1);
}

#else
int
NisDirCache :: lock_exclusive()
{
        if (!(*lock_is_valid)) {
		mutex_lock(&fd_lock);
                unmap_lock();
                map_lock(); 
		mutex_unlock(&fd_lock);
        	if (!(*lock_is_valid)) { /* should have mapped in a valid lock */
			return(-1);
		}
        }

	mutex_lock(DirCache_lockp); 

	/* if we get here, we got the cache_lock */
	while (*reader_cnt != 0) {
		*write_pending = TRUE; 
		/* wait for all reader to exit */
		if (!cond_wait(no_reader, DirCache_lockp)) 
			*write_pending = FALSE;
	}
	return(0);
}


void
NisDirCache :: unlock_exclusive()
{
	if (*read_pending)  {
		*read_pending = FALSE;
		cond_broadcast(no_writer);
	}
	mutex_unlock(DirCache_lockp); 
}
#endif /*!MT_LOCKS */





#ifndef MT_LOCKS
bool_t
NisDirCache :: isMgrUp()
{
	ushort w_array[NIS_W_NSEMS];
	union semun semarg;

	if (sem_reader == -1 || sem_writer == -1)
		return(FALSE);

	semarg.array = w_array;
	if ( semctl(sem_writer, 0, GETALL, semarg) == -1) 
		return(FALSE);
	
	if (w_array[NIS_SEM_MGR_UP] == 0) {
		// cache manager not running 
		return(FALSE);
	}
	return (TRUE);
}
#else
bool_t
NisDirCache :: isMgrUp()
{
        ushort w_array[NIS_W_NSEMS];
        union semun semarg;
	

        if (sem_writer == -1) {
                return(FALSE);
	}

        semarg.array = w_array;
        if ( semctl(sem_writer, 0, GETALL, semarg) == -1) {
                return(FALSE);
	}

        if (w_array[NIS_SEM_MGR_UP] == 0) {
                // cache manager not running
                return(FALSE);
        }
        return (TRUE);
}
#endif /*!MT_LOCKS*/





#ifndef MT_LOCKS
static int
decrement_reader( int sem_reader )
{
	struct sembuf buf;

	// decrement the reader semaphore 
	// should do a non blocking decrement!! XXX
	buf.sem_num = NIS_SEM_READER;
	buf.sem_op = -1;
	buf.sem_flg = SEM_UNDO | IPC_NOWAIT;

	if ( semop(sem_reader, &buf, 1) == -1) 
		return(-1);
	return(0);
}






void 
__nis_print_sems(int sem_writer, int sem_reader)
{
	int i;
	ushort w_array[NIS_W_NSEMS];
	ushort r_array[NIS_R_NSEMS];
	union semun semarg;


	if ( __nis_debuglevel <=2 )
		return;

	// cache_manager (writer) semaphores 
	semarg.array = w_array;
	if ( semctl(sem_writer, 0, GETALL, semarg) == -1) {
		syslog(LOG_ERR, "nis_print_sems: semctl GETALL failed");
		return;
	} 
	for (i = 0; i < NIS_W_NSEMS; i++)
		printf("sem_writer[%d] = %d\n", i, w_array[i]);


	// reader 
	semarg.array = r_array;
	if ( semctl(sem_reader, 0, GETALL, semarg) == -1) {
		syslog(LOG_ERR, "nis_print_sems: semctl GETALL failed");
		return;
	} 
	for (i = 0; i < NIS_R_NSEMS; i++)
		printf("sem_reader[%d] = %d\n", i, r_array[i]);
}
#endif /*!MT_LOCKS */


#ifdef MT_LOCKS
bool_t
NisDirCache :: lock_constr()
{



	mutex_init(&fd_lock, 0, NULL); 
	bad_lock = LOCK_IS_INVALID;
	lock_is_valid = &bad_lock;
	lock_file_fd = -1;

	if (!isMgrUp()) {
		return(FALSE);
	}
			
	mutex_lock(&fd_lock);
	map_lock();
	mutex_unlock(&fd_lock);

        return(TRUE);
}


void
NisDirCache :: unmap_lock()
{
        ASSERT(MUTEX_HELD(&fd_lock));
        if (lock_file_fd == -1)  /* someone already unmap it */
                return;
        munmap((caddr_t) share_lock_ptr, sizeof(share_lock_t));
        close(lock_file_fd);
        lock_file_fd = -1;
}

bool_t
NisDirCache :: map_lock()
{
	const 	int max_retry=10;
        char    *lock_file=CACHE_LOCK_FILE;
        int     i;

        ASSERT(MUTEX_HELD(&fd_lock));
        if (lock_file_fd != -1)  /* someone already map it */
                return(1);

	lock_is_valid = &bad_lock;
        for (i=0; i< max_retry ; i++) {
                if ((lock_file_fd = open(lock_file, O_RDWR)) < 0) {
#ifdef DEBUG
			printf("waiting for cachemgr to init-lock file..\n");
#endif
			sleep(5); /*cachemgr may be re-initializing*/
		}
                else    break;
        }

        if (lock_file_fd < 0) {
                return(0);
        }

        /* got the lock file, now we map the lock into  */
        /* our address spac, the cachemgr should have   */
        /* already initialized it                       */
        share_lock_ptr = (share_lock_t *)
                mmap((caddr_t)0, (size_t)sizeof(share_lock_t),
                        PROT_READ | PROT_WRITE, MAP_SHARED,
                        lock_file_fd, (off_t)0);
         

        if (share_lock_ptr == (share_lock_t *)-1)  {
#ifdef DEBUG
                printf("can not mmap lock file\n");
#endif
                perror("mmap:");
                close(lock_file_fd);
                return(0);
        }
        
        /* init a set of direct pointers to variours field of the lock */
        /* this make the lock/unlock operation a little more efficient */
	/* WARNING: The following will only work if the cachemgr is    */
	/* compiled with the same C++ complier, i.e field allocation   */
	/* within a structure is the same as the current complier, if  */
	/* someday we decided to run cachemgr on a different CPU type. */
	/* or if we build the cachemgr with a different compiler.      */
	/* e.g. on a Mixed CPU Multi-processor system :-)              */
	/* we will need to make the share_lock representation 	       */
	/* machine/compiler independent.			       */

 
        lock_is_valid = &(share_lock_ptr->_lock_is_valid);
        ASSERT((*lock_is_valid));
        DirCache_lockp = &(share_lock_ptr->_lock);
        no_reader = &(share_lock_ptr->_no_reader);
        no_writer = &(share_lock_ptr->_no_writer);
        reader_cnt = &(share_lock_ptr->_reader_cnt);
        write_pending = &(share_lock_ptr->_write_pending);
        read_pending = &(share_lock_ptr->_read_pending);
        return(1);
}
#endif /*!MT_LOCKS*/




