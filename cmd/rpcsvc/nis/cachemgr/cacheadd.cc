/*
 *	cacheadd.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)cacheadd.cc	1.12	95/03/09 SMI"

#include 	<stdlib.h>
#include 	<sys/mman.h>
#include 	<sys/file.h>
#include 	<unistd.h>
#include 	<string.h>
#include 	<sys/types.h>
#include 	<sys/stat.h>
#include 	<syslog.h>
#include 	<values.h>
#include 	<rpc/rpc.h>

#include 	"cachemgr.h"
#include 	"pkeyhash.h"


extern "C"  char ** __break_name(nis_name, int*);;
void __nis_memcpy_safe(char* to, char* from,  int len);

extern PkeyHashtable *pkeyTbl;


// the time when nis_lookup() failed.
static struct timeval lasttime;


// free memory allocated by __break_name() etc.
inline void free_break_name(char** t, int levels)
{
	free((char*) *(t + levels - 1));
	free((char*) t);	
}


/*
 * check to see if the broken up name supplied is the same as the 
 * home entry
 */

bool_t 
MgrCache :: isHomeEntry(char** tgt, int tgtLevels) 
{
	bool_t 		prefix;
	
	if (home->get_dirlevels() == tgtLevels) {
		if (home->distance(tgt, tgtLevels, &prefix) == 0)
			return (TRUE);
	}
	return (FALSE);
}




/* 
 * releases hold on the file.
 * only after this is the data marked valid.
 */

void
MgrCache :: releaseLocks()
{
	valid();
	unlock_exclusive();
}




/*
 * This function adds a cache entry to the cache file. If an entry for
 * that contextname already exists then it just updates the information
 * in that entry i.e. performs an update.
 */

bool_t 
MgrCache :: add(NisDirCacheEntry *newce, bool_t isHome)
{
        NisCfileEntry 	*cfp;
	char 		*startmove;
	char		*endmove;	
	int 		newSize, extraSize;
	int		move;
	int 		oldsize, tlen;
	int 		tmp;
	int 		tgtLevels;
	int 		tmpsize;
	int 		i;
	struct 		timeval now;
	char  		**tgt;
	char		*target;
	int 		updateLevels = 0;
	bool_t 		update_inplace = FALSE;

	if(newce == NULL) 
		return FALSE;
	
	target = newce->do_name;

	if (mgrVerbose && !isHome)
		syslog(LOG_INFO, "adding directory object: '%s'", target);
	
	tgt = __break_name(target, &tgtLevels);
	
 tryagain:
	if (isHome) {
		if (home)  		// a home entry already exists.
			update_inplace = TRUE;
		else      		// the first time - file is being intialized.
			home = root; 
		cfp = home;
		if (mgrVerbose)
			syslog(LOG_INFO, 
			       "updating cold start directory object:' %s'\n", 
			       newce->do_name);
	} else { 
		// check for home
		if (isHomeEntry(tgt, tgtLevels)) {
			isHome = TRUE;
			update_inplace = TRUE;
			cfp = home;
			if (mgrVerbose)
				syslog(LOG_INFO, 
				       "updating cold start directory object:' %s'\n", 
				       newce->do_name);
			// write cold start file 
			if (!writeColdStartFile((directory_obj*)newce)) {
				syslog(LOG_ERR, 
				       "add():could not write cold start file\n");
			}
		}
	}
	// if ttl has expired - add it only if it is home entry otherwise 
	// just return
	if (newce->do_ttl <= 0) { 
		if (!isHome) {
			if (mgrVerbose)
				syslog(LOG_INFO, "add: trying to add entry whose TTL has expired : %s\n",
				       target);
			free_break_name(tgt, tgtLevels);
			return(FALSE);
		} else {         // home entry
			// complain and add it.
			if (mgrVerbose)
				syslog(LOG_INFO, 
				       "adding home directory object whose TTL has expired : %s\n",
				       target);
		}
	}

	if (!isHome)
		cfp = getEntry(tgt, tgtLevels, &update_inplace);

	// get exclusive access to the shared file
	lock_exclusive();
	inValid();
	
	// calculate space needed to store broken up name
	for (i = 0, tlen = 0; i < tgtLevels; i++) 
		tlen += strlen(tgt[i]) + 1;

	if (update_inplace) {
		tmp = __nis_align(cfp->sz_cfile() + tlen);
		oldsize = cfp->get_size() + tmp;
		oldsize = __nis_align(oldsize);
	}
	
	// to store sizeof entry sizeof(data) + sizeof(name)  
	tmp = __nis_align( cfp->sz_cfile() + tlen );
	tmp +=  (tmpsize = newce->get_size());
	newSize = __nis_align(tmp);

	
	// make sure the cache file is not full - if it is then
	// we have to increase the size of the file  and unmap/map it 
	
	if (update_inplace)
		extraSize = newSize - oldsize;
	else
		extraSize = newSize;

	if ((extraSize + *totalSize) > (*mapLen)) {
		// remap it 
		int mlen = *mapLen;
		if (mlen + incFileSize  > maxSize) {
			// remove random entry and tryagain
			if (!removeRandomEntry()) {
				if (mgrVerbose)
					syslog(LOG_ERR,
					       "add:could not create space for new entry\n");
				// free locks and return 
				free_break_name(tgt, tgtLevels);
				releaseLocks();
				return(FALSE);
			}
		} else {
			mlen += incFileSize;
			if ( !remap(mlen)) {
				// not enough space to expand file OR
				// reached our max limit
				// delete some entries and try again.
				syslog(LOG_ERR, "Could not grow cachefile : %m\n");
				if (!removeRandomEntry()) {
					if (mgrVerbose)
						syslog(LOG_ERR,
						       "could not create space for new entry");
					// free locks and return 
					free_break_name(tgt, tgtLevels);
					releaseLocks();
					return(FALSE);
				}
			}
		}
		releaseLocks();
		goto tryagain;
	}
	
	// new dir_name entry - move array down one 
	if (update_inplace) {
		startmove = (char *) cfp->next();
		if (startmove > ENDOFFILE )  // last entry 
			startmove = ENDOFFILE;
		cacheUpdate++;
	} else {
		startmove = (char *)cfp;
		cacheAdd++;
	}
	
	move = ENDOFFILE - startmove;
	endmove = (char *) ((char *)cfp + newSize); 
	
	__nis_memcpy_safe(endmove, startmove, move);
	
	// copy the broken up name into the file.
	tlen = cfp->set_dirname(tgt, tgtLevels);

	// where the data portion (directory object) starts
	cfp->set_dataoffset(tlen + cfp->sz_cfile());

	// copy the specific information at location starting after the prefix
	// and size entry 

	cfp->set_size ( newce->write_info(cfp->datap()) );
	if (cfp->get_size() != tmpsize) {
		syslog(LOG_ERR, "CacheAdd(): Internal error");
		exit(1);
	}
	// set the offset to the next entry
	// that is equal to the dataoffset + the size of the data part
	cfp->set_offset();
	cfp->set_ttl(newce->do_ttl);
	cfp->set_flags(0);

	getthetime(&now);
	if ((newce->do_ttl > 0) && ((newce->do_ttl + now.tv_sec)  < nextExpire)) {
		unsigned alarmAt = (unsigned) (newce->do_ttl + DELTAEXPIRE);
		if (alarmAt <= 0 || alarmAt > CACHE_EXPIRE)
			alarmAt = CACHE_EXPIRE;
		nextExpire = alarmAt + now.tv_sec;
		alarm((int)alarmAt);
	}
	
	if (!update_inplace && !isHome) 
		*count += 1;
	*totalSize += extraSize;

	// if home entry added update the root entry
	if (isHome)
		root = home->next();

	free_break_name(tgt, tgtLevels);

	// add the publickeys of the servers in this directory object to
	// the hashtable
	directory_obj* d = newce->get_directory_obj();
	for (i = 0; i < d->do_servers.do_servers_len; i++) {
		PkeyHashEntry *p = new PkeyHashEntry(
		    d->do_servers.do_servers_val[i].name, 
		    d->do_servers.do_servers_val[i].key_type,
		    &d->do_servers.do_servers_val[i].pkey);

		if(!p) {
			syslog(LOG_WARNING, "cacheadd: out of memory");
		} else {
			pkeyTbl->update(p);
			// don't free the memory for e because the pointer
			// itself is stored in the hashtable
		}
	}
	releaseLocks();
	return (TRUE);
}





/*
 * given a name -dir_name this routine deletes the 
 * entry  associated with that in the cache.
 * it deletes the entry and then compacts the cache file.
 * if a directory_object is specified it deletes the entry only if
 * the directory object is the same as the one passed in. This is used
 * when clients send in a request to remove an entry.
 * If dir_obj is NULL then it just deletes the entry and ignores the comparison.
 * if entry exists and is deleted it returns TRUE else returns FALSE.
 * if entry is deleted it shrinks the file.
 */
	
bool_t 
MgrCache :: remove(char *oldname, directory_obj *dir_obj)
{
	int 		size, move;
	bool_t 		found;
	char  		**tgt;
	int		tgtLevels;
	NisCfileEntry 	*cfp;
	NisDirCacheEntry de;
	struct timeval	now;

	tgt = __break_name(oldname, &tgtLevels);
	if (tgt == NULL)
		return (FALSE);
	found = FALSE;
	cfp = getEntry(tgt, tgtLevels, &found);

	if (!found) {
		// check to see if it is the home entry 
		if (isHomeEntry(tgt, tgtLevels)) {
		    // go out and try to get a new cold start directory object.
		    // if one is obtained then update the cold start file too.

		    getthetime(&now);
		    // get the current time and compare time when last failure
		    // occurred in looking up a directory object.
		    // ARBITRARY POLICY: don't process requests that come in
		    // during 30 secs of the last failure.
		    // 

		    if ( (now.tv_sec - lasttime.tv_sec) > 30 ) {
			if (!update_cold_start_entry()) {
			    if (mgrVerbose)
				syslog(LOG_ERR, 
			"Could not get valid cold start directory object");
			} else {
			    initColdStart();
			    root = home->next();
			}
		    }
		    // free memory allocated by __break_name()
		    free_break_name(tgt, tgtLevels);
		    cacheDelete++;
		    return(TRUE);
		}
	}
	// free memory allocated by __break_name()
	free_break_name(tgt, tgtLevels);

	if (!found) {
		return (FALSE);
	}

	if (dir_obj) {
		memset( (void*)&de, 0, sizeof(directory_obj) );
		if ( de.myConstructor( cfp ) == FALSE ) 
			return(FALSE);
		if ( !__nis_directory_objs_equal( dir_obj, (directory_obj*)&de) ) {
			// not the same one - the entry has changed
			de.myDestructor();
			return(FALSE);
		}
		de.myDestructor();
	}
	if (mgrVerbose)
		syslog(LOG_INFO, "removing directory object: %s", oldname);

	lock_exclusive();

	// mark file invalid
	inValid();

	size = cfp->get_offset();
	move = ENDOFFILE - ((char *)cfp + size);

	__nis_memcpy_safe((char *)cfp, (char *)((char *)cfp + size),  move);

	*count -= 1;

	*totalSize -= size;
	
	// if the empty space in cache file is greater than 2 times the 
	// file increment size shrink the file and remap it
	if ( (*mapLen - *totalSize) > (2 * incFileSize)) {
		int newlen = *mapLen - incFileSize;
		while ( (newlen - (2 * incFileSize)) > *totalSize ) 
			newlen -= incFileSize;
		remap(newlen);
	}
	releaseLocks();
	cacheDelete++;
	return (TRUE);
}



/* 
 * This will try to the directory object in the cache file
 * it does an nis_lookup() on the directory name to get a new directory_object.
 */

bool_t update_cached_directory_object(char *d_name)
{
	nis_result 	*res;
	nis_object 	*o;
	directory_obj 	*d_new;

	res = nis_lookup(d_name, FOLLOW_LINKS+USE_DGRAM);

	if (res->status != NIS_SUCCESS) {
		nis_freeresult(res);
		// record time this failure occurred
		getthetime(&lasttime);		
		return(FALSE);
	}
	o = res->objects.objects_val;
	d_new = &(o->DI_data);
	if ( (__type_of(o) != DIRECTORY_OBJ) || (d_new->do_type != NIS)) {
		nis_freeresult(res);
		return(FALSE);
	}
	mgrCache->add((NisDirCacheEntry*)d_new, FALSE);

	// free memory returned by nis_lookup call
	nis_freeresult(res);
	return(TRUE);
}



/* 
 * This will update the home entry that is present in the cold start file.
 * it does an nis_lookup() on the directory name to get a new directory_object.
 */

bool_t update_cold_start_entry()
{
	directory_obj 	d;
	bool_t 		res;

	memset((void*)&d, 0, sizeof(directory_obj));
	readColdStartFile(COLD_START_FILE, &d);

	res = update_cached_directory_object(d.do_name);
	
	// free memory allocated while reading coldstart file
	xdr_free((xdrproc_t)xdr_directory_obj, (char*)&d);
	return (res);
}






/*
 * Copy s1 to s2, always copy n bytes.
 * For overlapped copies it does the right thing.
 */
void
__nis_memcpy_safe(register char* s2, register char* s1, int len)
{
	register int 	n;

	if ((n = len) <= 0)
		return;

	if ((s1 < s2) && (n > abs(s1 - s2))) {		/* overlapped */
		s1 += (n - 1);
		s2 += (n - 1);
		do
			*s2-- = *s1--;
		while (--n);
	} else {					/* normal */
		memcpy(s2, s1, n);
	}
}


