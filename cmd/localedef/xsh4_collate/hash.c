/*
 * File: hash.c
 *
 * Copyright (C) 1990 Sun Microsystems Inc.
 * All Rights Reserved.
 */
#ident	"@(#)hash.c	1.1	95/04/09 SMI"


/*
 *    Change Log
 * ============================================================================
 * Author      Date       Change 
 * barts     13 Nov 90	  Created.
 *
 */


/*

    Name:  hash.c

Synopsis:  

           #include "util/hash.h"

           hash * make_hash(size)
	   int size;

	   hash * make_ihash(size)
	   int size;

           char ** get_hash(tbl, key)
	   hash * tbl;
	   char * key;

	   char ** find_hash(tbl, key)
	   hash * tbl;
	   char * key;

	   char * del_hash(tbl, key)
	   hash * tbl;
	   char * key;

	   int operate_hash(tbl, op_func, usr_arg)
	   hash * tbl;
	   void (*op_func)();
	   char * usr_arg;

	   void destroy_hash(tbl, des_func, usr_arg) 	 hash * tbl;
	   int (*des_func)(); 	 char * usr_arg; 	 




 Description: 

   These routines provide a general purpose hash table facility that
supports multiple open hash tables.  Each entry in the table consists of a
key and a data ptr.  The key is a null terminated character string, while
the data ptr is opaque. Since all the entries are maintained in a doubly
linked lists, deletions and operations on entire table execute very quickly.
This make these routines suitable for use when the tables may be very ephemeral.

  Make_hash returns a pointer to the created table.  The size argument
indicate the number of buckets the routine is to allocate.  This should be ~
the max number of items expected in the table for maximum performance....
but /2 or /3 should still be ok.  Note that for maximum efficiency the hash
table size should be a prime number (a side effect of the hash alorithm).

  Make_ihash performs the same function as make_hash, except that the hash
routines will use the key arguments as arbitrary integers rather than strings.

  Get_hash searches the specified hash table tbl for an entry with the
specified key.  If the entry does not exist, it is created with a NULL data
ptr.  The routine returns a ptr to the area where the data ptr is (can be)
stored.

  Find_hash searchs the table for an entry with the specified key.  If the
entry is found, the address of the data pointer associated with the key is
returned.  If no such entry exists, the routine returns NULL.

  Del_hash deletes the specified table entry and returns the associated data
ptr.  If the entry did not exist ( or the data ptr was NULL), the routine
returns NULL.

  Operate_hash calls the routine pointed to by op_func once for each entry
in tbl, with three arguments: the data ptr, the usr_arg ptr and a ptr to the
key for that entry (which should NOT be altered).  This is useful for
transversing a hash table quickly and operating on the entries. Note that
the order of the traversal of the hash table is the reverse order of
insertion.

  Destroy_hash destroys the specified hash table after operating on it
with the specified des_func function as described for operate_hash.  All storage
allocated by the hash routines is reclaimed.

Author:  Bart Smaalders 1/89


*/
#include <stdio.h> /* grab NULL define */

/*
  support code inclusion
  */

#include "hash.h" 

extern char * strcpy();
extern int    strcmp();	
extern char * strdup();

static int    hash_string();

Static hash * make_hash(size)
     int size;
{
  hash * ptr;


  ptr        = (hash *) malloc(sizeof(*ptr));
  ptr->size  =   size;
  ptr->table = (hash_entry **) malloc( (unsigned) (sizeof(hash_entry *) * size) );
  (void)memset((char *) ptr->table, (char) 0, sizeof(hash_entry *)*size);
  ptr->start = NULL;
  ptr->hash_type = String_Key;
  return(ptr);
}

Static hash * make_ihash(size)
     int size;
{
  hash * ptr;


  ptr        = (hash *) malloc(sizeof(*ptr));
  ptr->size  =   size;
  ptr->table = (hash_entry **) malloc( (unsigned) (sizeof(hash_entry *) * size) );
  (void)memset((char *) ptr->table, (char) 0, sizeof(hash_entry *)*size);
  ptr->start = NULL;
  ptr->hash_type = Integer_Key;
  return(ptr);
}

  
Static char ** get_hash(tbl, key)
     hash * tbl;
     char * key;
{

  register int bucket;
  register hash_entry * tmp;
  hash_entry * new;

  if(tbl->hash_type == String_Key)
    tmp = tbl->table[bucket = hash_string(key, tbl->size)];
  else
    tmp = tbl->table[bucket = abs((int)key) % tbl->size];

  if(tbl->hash_type == String_Key)
    while(tmp!=NULL)
      {	
	if(strcmp(tmp->key, key)==0)
	  return(&tmp->data);
	tmp = tmp->next_entry;
      }
  else
    while(tmp!=NULL)
      {	
	if(tmp->key == key)
	  return(&tmp->data);
	tmp = tmp->next_entry;
      }
    
  /*
    not found.... 
    insert new entry into bucket...
    */

  new = (hash_entry *) malloc(sizeof(*new));
  new->key = ((tbl->hash_type == String_Key)?strdup(key):key);
  /*
    hook into chain from tbl...
    */
  new->right_entry = NULL;
  new->left_entry = tbl->start;
  tbl->start = new;
  /*
    hook into bucket chain
    */
  new->next_entry = tbl->table[bucket];
  tbl->table[bucket] = new;
  new->data = NULL;   /* so we know that it is new */
  return(& new->data);
}

Static char ** find_hash(tbl, key)
     hash * tbl;
     char * key;
{
  register hash_entry * tmp;

  if(tbl->hash_type == String_Key)
    {
      tmp = tbl->table[hash_string(key, tbl->size)];
      for(;tmp!=NULL; tmp = tmp->next_entry)
	if(!strcmp(tmp->key, key))
	  return(&tmp->data);
    }
  else
    {
      tmp = tbl->table[abs((int)key) % tbl->size];
      for(;tmp!=NULL; tmp = tmp->next_entry)
	if(tmp->key == key)
	  return(&tmp->data);
    }

  return(NULL);
}

Static char * del_hash(tbl, key)
     hash * tbl;
     char * key;
{
  register int bucket;
  register hash_entry * tmp, * prev = NULL;

  if(tbl->hash_type == String_Key)
    bucket = hash_string(key, tbl->size);
  else
    bucket = abs((int)key) % tbl->size;

  if((tmp = tbl->table[bucket])==NULL)
    return(NULL);

  else
    {
      if(tbl->hash_type == String_Key)
	while(tmp!=NULL)
	  {	
	    if(!strcmp(tmp->key, key))
	      break;  /* found item to delete ! */
	    prev = tmp;
	    tmp  = tmp->next_entry;
	  }
      else
	while(tmp!=NULL)
	  {
	    if(tmp->key == key)
	      break;
	    prev = tmp;
	    tmp  = tmp->next_entry;
	  }
      if(tmp == NULL)
	return(NULL); /* not found */
    }
      /* tmp now points to entry marked for deletion, prev to 
         item preceeding in bucket chain or NULL if tmp is first.
	 
	 remove from bucket chain first....

	 */
  if(tbl->hash_type == String_Key)  
    free(tmp->key);
  if(prev!=NULL)
    prev->next_entry = tmp->next_entry;
  else
    tbl->table[bucket] = tmp->next_entry;
  /*
    now remove from tbl chain....
    */
  if(tmp->right_entry !=NULL) /* not first in chain.... */
    tmp->right_entry->left_entry = (tmp->left_entry ? 
				    tmp->left_entry->right_entry: NULL);
      else	
	tbl->start = (tmp->left_entry ?tmp->left_entry->right_entry: NULL);
  return(tmp->data);	
}

Static int operate_hash(tbl, ptr, usr_arg)
     hash * tbl;
     void (*ptr)();
     char * usr_arg;
{
  register hash_entry * tmp = tbl->start;
  int c = 0;

  while(tmp)
    {
      (*ptr)(tmp->data,usr_arg, tmp->key);
      tmp = tmp->left_entry;
      c++;
    }
  return(c);
}

Static void destroy_hash(tbl, ptr, usr_arg)
     hash * tbl;
     int (*ptr)();
     char * usr_arg;
{
  register hash_entry * tmp = tbl->start, * prev;

  while(tmp)
    {
      if(ptr)
	(*ptr)(tmp->data,usr_arg, tmp->key);

      if(tbl->hash_type == String_Key)
	free(tmp->key);
      prev = tmp;
      tmp = tmp->left_entry;
      free((char *)prev);
    }
  free((char *)tbl->table);
  free(tbl);
}

static int hash_string(s,modulo)
register char *s;
int modulo;
{
	register unsigned result = 0;
	register int i=1;

 	while(*s!=0)
	  result += (*s++ << i++);

 	return(result % modulo); 
}

#ifdef HASHTEST

main(argc, argv)
char * argv[];
int argc;
{
  hash * tbl;
  char buff[100], test[100];
  char ** tmp, **get_hash(), **find_hash();
  int i, puts();

  tbl = make_hash((argc==1)?100:(atoi(argv[1]))); 
  
  for(i=0;(gets(buff));i++)
    get_hash(tbl, buff);

  destroy_hash(tbl, NULL, NULL);
}
#endif HASHTEST
 
