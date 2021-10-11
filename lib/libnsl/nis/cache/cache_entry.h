/*
 *	cache_entry.h
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)cache_entry.h	1.7	94/05/16 SMI"

#ifndef _CACHE_ENTRY_H
#define _CACHE_ENTRY_H

#include 	<sys/time.h>
#include 	"dircache.h"

// directory_obj structure defined in nis.x (nis.h) file.
// This class just defines some accessor functions on the 
// directory_object structure. The compiler does the right thing
// as far as sizing the structure etc. is concerned. However,
// note that virtual functions and member variables should not be 
// added in this class.


class NisDirCacheEntry : private directory_obj {

public:
	// constructor
	bool_t 	myConstructor(class NisCfileEntry*);

	// assignemnt operator
	bool_t 	operator= (const NisDirCacheEntry&);

	// destructor
	void 	myDestructor();

	// returns sizeof (data) in this entry 	
	int 	get_size() const;

	// funcitons specific to the structure of the data 
	// takes a pointer to write the data into
	// in a serial fashion - returns size of data written
	int 	write_info(void *writeTo);  

	// returns a pointer to a directory_obj structure (itself)
	directory_obj* 	get_directory_obj() const {
		return ( (directory_obj*) this );
	}

	// print the cache entry
	void 	print() const;
	
	friend 	class NisLocalCache;
	friend 	class NisSharedCache;
	friend 	class MgrCache;
};


/*
 * getthetime() -- wrapper around gettimeofday().
 */

// extern "C" int		gettimeofday(struct timeval *, void *);
#define getthetime(tp)	gettimeofday(tp, NULL)



#endif _CACHE_ENTRY_H


