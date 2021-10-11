/*
 *	pkeyhash.cc
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)pkeyhash.cc	1.9	92/10/09 SMI"

#include 	<stdio.h>
#include 	<stdlib.h>
#include 	<string.h>
#include 	<ctype.h>
#include 	<rpc/types.h>
#include	<syslog.h>

#include 	"pkeyhash.h"

extern "C" int strcasecmp(const char *, const char *);


static u_int hashsizes[] = {		/* PkeyHashtable sizes */
	113,
	337,
	977,
	2053,
	4073,
	8011,
	16001,
	0
};

/* Returns the next size to use for the hash table */
static u_int get_next_hashsize( u_int oldsize )
{
  u_int newsize, n;
  if (oldsize == 0)
    newsize = hashsizes[0];
  else { 
    for (n = 0; newsize = hashsizes[n++];)
      if (oldsize == newsize) {
	newsize = hashsizes[n];     /* get next size */
	break;
      }
    if (newsize == 0) 
      newsize = oldsize * 2 + 1;   /* just double */
  }
  return( newsize );
}



PkeyHashtable :: PkeyHashtable()
{
	tblsize = get_next_hashsize(0);
	tbl = (PkeyHashtable_entry**) calloc(tblsize, 
					     sizeof(PkeyHashtable_entry*));
	if (!tbl) {
		syslog(LOG_ERR,
		       "PkeyHashTable(): could not allocate memory for table");
		exit(1);
	}
	fullness = 0;
}



PkeyHashtable :: ~PkeyHashtable()
{ 
	free ((char*) tbl); 
}


/* grow the PkeyHashtable
 * moves all the old entries into the new table.
 */

bool_t
PkeyHashtable :: grow()
{
	int i;
	u_int oldsize = tblsize;
	PkeyHashtable_entry** oldtab = tbl;

	tblsize = get_next_hashsize(oldsize);
	tbl = (PkeyHashtable_entry**) calloc(tblsize, sizeof(PkeyHashtable_entry*));
	if (!tbl)
		return (FALSE);
	// transfer contents of old table into new one
	PkeyHashtable_entry *np, *next_np, **hp;
	for(i = 0; i < oldsize; i++) {
		for (np = oldtab[i]; np != NULL; np = next_np) {
			next_np = np->next;
			hp = &tbl[np->entry->get_hashval() % tblsize];
			np->next = *hp;
			*hp = np;
		}
	}
/*
	delete oldtab;
*/
	return (TRUE);
}




/* 
 * Update an entry to the PkeyHashtable
 * If the entry does not exists it adds a new entry
 * else it deletes the old one and adds the new one.
 * Note:  A pointer to the entry is stored in the PkeyHashtable so the 
 * calling routine  should make sure that the entry is valid for all time 
 * that it is in the PkeyHashtable. 
 * 
 * Note: As we are interested only in DH publickeys it only stores those
 * servers which have such a key in the hashtable. Otherwise it does nothing.
 */

bool_t
PkeyHashtable :: update( PkeyHashEntry *e )
{
	u_long 		t;
	netobj 		*k;
	u_int 		hval;
	u_int 		bucket;
	PkeyHashtable_entry 	*fst;
	PkeyHashtable_entry	*he;

	// don't know how to deal with anything else for now
	// this is for future extensions	
	e->get_pkey(&t, &k);
	if (t != NIS_PK_DH)
		return(FALSE);

	hval = e->get_hashval();
	bucket = hval % tblsize;
	
	fst = tbl[bucket];
	he = new PkeyHashtable_entry(e);
	if (!he)
		return (FALSE);
	if (fst == NULL) { // empty bucket
		// add entry to the head of the list
		he->next = NULL;
		tbl[bucket] = he;
	} else {
		PkeyHashtable_entry *curr, *prev;
		for (curr = prev = fst; curr != NULL; curr = curr->next) {
			if ( e->isEqual(curr->entry) ) {  
				// same entry found update 
				he->next = curr->next;
				if ( curr == fst)  // head
					tbl[bucket] = he;
				else
					prev->next = he;
				delete curr;
				fullness--;
				break;
			}
			prev = curr;
		}
		if (curr == NULL) {
			// add it to the head
			he->next = fst;
			tbl[bucket] = he;
		}
	}
	if (++fullness > (tblsize >> 1))
		grow();
	return (TRUE);
}




/* 
 * Removes the entry equivalent to e from the PkeyHashtable
 * Note 'e' should be delete, if so desired, by the calling routine
 */
bool_t 
PkeyHashtable :: remove( const PkeyHashEntry *e)
{
	u_int hval = e->get_hashval();
	u_int bucket = hval % tblsize;
	PkeyHashtable_entry *curr, *prev;

	PkeyHashtable_entry *fst = tbl[bucket];

	for (curr = prev = fst; curr != NULL; prev = curr, curr = curr->next) {
		if ( e->isEqual(curr->entry) ) {
			if (curr == fst)     // head
				tbl[bucket] = curr->next;
			else 
				prev->next = curr->next;
			delete curr;
			fullness--;
			return (TRUE);
		}
	}
	return (FALSE);
}






/* lookup in the PkeyHashtable for an entry equivalent to the one given
 * as defined by the PkeyHashEntry :: isEqual() function.
 * returns a pointer to the PkeyHashEntry if found
 * else returns NULL;
 */

PkeyHashEntry*
PkeyHashtable :: lookup(const PkeyHashEntry* e) const
{

	u_int hval = e->get_hashval();
	u_int bucket = hval % tblsize;
	PkeyHashtable_entry *curr = tbl[bucket];
	
	for ( ; curr != NULL; curr = curr->next) {
		if ( e->isEqual(curr->entry) ) 
			return (curr->entry);
	}
	return (NULL);
}



/* debugging routines */
void
PkeyHashtable :: print()
{
	if (__nis_debuglevel <= 2)
		return;
	PkeyHashtable_entry *curr;
	int i;
	printf("Entries in PkeyHashtable = %d\n", fullness);
	for (i = 0; i < tblsize; i++) {
		curr = tbl[i];
		if (curr) {
			printf("bucket[%d]:\n\t", i);
			for ( ; curr != NULL; ) {
				curr->entry->print();
				curr = curr->next;
				if (curr)
					printf(" || ");
			}
			printf("\n");
		}
	}
}


void
PkeyHashtable :: print(u_int bucket)
{
	if (__nis_debuglevel < 2)
		return;
	PkeyHashtable_entry *curr;

	if (bucket >= tblsize)
		return;
	curr = tbl[bucket];
	printf("bucket[%d]:\n\t", bucket);
	if (curr) {
		for ( ; curr != NULL; ) {
			curr->entry->print();
			curr = curr->next;
			if (curr)
				printf(" || ");
		}
		printf("\n");
	} else 
		printf("NULL\n");
}




/* pkeyhashentry specific functions */


#define HASHSHIFT	3
#define HASHMASK	0x1f

static u_int calculate_hashval( char* str )
{
	u_int hval = 0;

	if( str != NULL ) {
		while( *str != '\0' ) {
			hval = hval ^ (hval << HASHSHIFT);
			hval += (*str & HASHMASK);
			++str;
		}
	}
	
	return (hval);
}



PkeyHashEntry :: PkeyHashEntry(char* servName, u_long type, netobj *key)
{
	serv_name = strdup(servName);
	key_type = type;

	pkey.n_len = key->n_len;
	pkey.n_bytes = (char*)malloc(pkey.n_len);
	
	memcpy(pkey.n_bytes, key->n_bytes, key->n_len);

	hashval = calculate_hashval( servName );
	
}



PkeyHashEntry :: PkeyHashEntry(char* servName)
{
	serv_name = strdup(servName);
	hashval = calculate_hashval( servName );
}




PkeyHashEntry :: ~PkeyHashEntry()
{
	if (serv_name)
		free((char*)serv_name); 
	if (pkey.n_bytes)
		free((char*)pkey.n_bytes);
}


bool_t
PkeyHashEntry :: isEqual(const PkeyHashEntry* cmp) const
{
	PkeyHashEntry *e = (PkeyHashEntry*)cmp;
	return( hashval == e->hashval &&
	       strcasecmp(e->serv_name, serv_name) == 0 );

}



void
PkeyHashEntry :: print() const
{
	if (__nis_debuglevel < 2)
		return;
	printf("%s", serv_name);
}



PkeyHashtable_entry :: PkeyHashtable_entry(PkeyHashEntry* e) 
{
	entry = e; 
	next = NULL; 
}




PkeyHashtable_entry :: ~PkeyHashtable_entry()
{
	delete entry; 
}
