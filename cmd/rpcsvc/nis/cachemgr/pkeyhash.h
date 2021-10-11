/*
 *	pkeyhash.h
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)pkeyhash.h	1.5	92/07/14 SMI"

#ifndef _PKEY_HASH_H
#define _PKEY_HASH_H

#include 	<rpcsvc/nis.h>
#include 	<rpcsvc/nis_cache.h>
#include 	<rpc/types.h>


class PkeyHashEntry
{
 public:
	void *operator new (size_t bytes)
	  { return calloc (1, bytes); }
	void operator delete (void *arg)
	  { (void) free((char*)arg); }

	PkeyHashEntry(char* servName, u_long key_type, netobj *pkey);
	PkeyHashEntry(char* servName);
	~PkeyHashEntry();

	bool_t 	isEqual(const PkeyHashEntry*) const;
	u_int 	get_hashval() const { return hashval; }
	void 	get_pkey(u_long *ptype, netobj **keyp) { 
		*ptype = key_type; *keyp = &pkey;
	}  
	void 	print() const;

 private:
	char 	*serv_name;
	u_long 	key_type;
	netobj 	pkey;
	u_int 	hashval;

};





class PkeyHashtable_entry
{
	PkeyHashEntry 		*entry;
	PkeyHashtable_entry	*next;
	
 public:
	void *operator new (size_t bytes)
	  { return calloc (1, bytes); }
	void operator delete (void *arg)
	  { (void) free((char*)arg); }

	PkeyHashtable_entry(PkeyHashEntry* e);
	~PkeyHashtable_entry();

	friend class PkeyHashtable;
};





class PkeyHashtable
{
public:
	void *operator new (size_t bytes)
	  { return calloc (1, bytes); }
	void operator delete (void *arg)
	  { (void) free((char*)arg); }

	PkeyHashtable();
	~PkeyHashtable();
	bool_t 		update(PkeyHashEntry*);
	bool_t 		remove(const PkeyHashEntry*);
	PkeyHashEntry* 	lookup(const PkeyHashEntry*) const;

	void print();
	void print(u_int bucket);
private:
	u_int 			tblsize;
	PkeyHashtable_entry	**tbl;
	int 			fullness;
	bool_t 			grow();
};





#endif /* !_PKEY_HASH_H */





