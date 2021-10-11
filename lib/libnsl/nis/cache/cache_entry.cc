/*
 *	cache_entry.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)cache_entry.cc	1.7	93/12/25 SMI"

/*
 * Ported from SCCS version :
 * "@(#)cache_entry.cc  1.16  91/03/14  Copyr 1988 Sun Micro";
 *
 * This file contains all the methods for NisDirCacheEntry class.
 * It operates on the 
 */
#include <unistd.h>
#include <rpc/rpc.h>
#include <string.h>
#include <sys/types.h>
#include <rpcsvc/nis.h>
#include <rpcsvc/nis_cache.h>

#include "cache_entry.h"
#include "dircache.h"


extern void __nis_print_directory_special(directory_obj *);


/* 
 * takes a NisCfileEntry
 * forms into a  NisDirCacheEntry from the serial data from memory (cachefile)
 * the name and ttl come from the name and exptime of the NisCfile entry.
 * 
 */
	
bool_t
NisDirCacheEntry::myConstructor( class NisCfileEntry* cfp )
{
	XDR 		xdrs;
	char 		*t;
	char 		*start = cfp->datap();
	directory_obj 	*d = this->get_directory_obj();

	t = (char*)__nis_align((int)start);
	if (t != start)      // internal bug - alignment error
		return(FALSE);
	xdrmem_create(&xdrs, start, cfp->get_size(), XDR_DECODE);

	// 'this' should be been zeroed out 
	memset((void*)d, 0, sizeof(directory_obj));
	if (!xdr_directory_obj(&xdrs, d)) {
		syslog(LOG_ERR, 
		       "NIS+:NisDirCacheEntry()::myConstructor - xdr_directory_obj failed");
		return (FALSE);
	};
	do_ttl = cfp->get_ttl();
	return(TRUE);
}



/* 
 * takes a NisDirCacheEntry object and makes a copy of that in 'this'.
 */

bool_t
NisDirCacheEntry::operator= (const NisDirCacheEntry& ce)
{
	char 		*buf;
	XDR 		xdrs;
	int 		len;
	directory_obj 	*d = this->get_directory_obj();
	
	len = ce.get_size();
	buf = (char*)malloc(len * sizeof(char));
	if (!buf)
		return(FALSE);
	xdrmem_create(&xdrs, buf, len, XDR_ENCODE);
	if (!xdr_directory_obj(&xdrs, ce.get_directory_obj())) {
		free(buf);
		return(FALSE);
	}
	
	xdrmem_create(&xdrs, buf, len, XDR_DECODE);
	memset((void*)d, 0, sizeof(directory_obj));
	if (!xdr_directory_obj(&xdrs, d)) {
		free(buf);
		return(FALSE);		
	}
	free(buf);
	return(TRUE);
}




/* 
 * delete the cache entry 
 */
void
NisDirCacheEntry::myDestructor()
{
	directory_obj 	*d = this->get_directory_obj();

	xdr_free((xdrproc_t)xdr_directory_obj, (char*)d);

	// this is not freed here as an optimization. The calling routines
	// then have to allocate a cache_entry structure only once and reuse it.
	// this is called from local_cache.cc and client_search.cc
	// free((char*) this);  

	// zero it out in case someone xdr's into it
	memset((void*)d, 0, sizeof(directory_obj));

}





/* 
 * knows the specific structure of the data portion in a NisDirCacheEntry.
 * write the information obtained about a directory object into the cache file.
 * takes as argument pointer to memory location to write the info. into.
 * returns size (bytes to serialize entry) of this entry.
*/


int
NisDirCacheEntry::write_info(void *start)
{

	XDR 		xdrs;
	char 		*t;
	unsigned 	int len;
	directory_obj 	*d = get_directory_obj();

	t = (char*)__nis_align((int)start);
	if (t != start) {
		syslog(LOG_ERR, "NIS+: NisDirCacheEntry::write_info :alignmemt error");
	}
	// find out length needed to serialize
	len = get_size();
	
	xdrmem_create(&xdrs, (caddr_t)start, len, XDR_ENCODE);
	if (!xdr_directory_obj(&xdrs, d)) {
		syslog(LOG_ERR, "NisDirCacheEntry::write: xdr_directory_obj failed");
	};
	return( len );

}



/* 
 * returns space needed (sizeof this entry) 
 * returns only the size needed for the data part.
 * This excludes the storage needed for the dir_name and the ttl of the
 * NisDirCacheEntry. These are stored in the NisCfileEntry separately before the 
 * rest of the directory object in the cache file.
 */
	
int
NisDirCacheEntry::get_size() const
{
	int 	    	n;
	directory_obj 	*d = get_directory_obj();

	n = (int) xdr_sizeof((xdrproc_t)xdr_directory_obj, (void*)d);
	return( n );
}




void 
NisDirCacheEntry::print() const
{
	if (__nis_debuglevel == 6) { 
		// special - set in nisshowcache for testing 
		__nis_print_directory_special((directory_obj *)this);
	} else {
		if (__nis_debuglevel) 
			nis_print_directory((directory_obj *)this);
	}
}





