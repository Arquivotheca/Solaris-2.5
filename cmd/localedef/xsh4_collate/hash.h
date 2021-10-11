/*
  @(#)SSM2 hash.h 1.2 90/11/13 
*/

/*
 * File: hash.h
 *
 * Copyright (C) 1990 Sun Microsystems Inc.
 * All Rights Reserved.
 */
#ident	"@(#)hash.h	1.1	95/04/09 SMI"


/*
 *    Change Log
 * ============================================================================
 * Author      Date       Change 
 * barts     13 Nov 90	  Created.
 *
 */

#ifndef _hash_h
#define _hash_h

#ifndef Static
#ifdef STATIC 
#define Static static
#else
#define Static
#endif
#endif


typedef struct hash_entry {
  struct hash_entry 
    * next_entry,
    * right_entry,
    * left_entry;
  char *       	key;
  char * 	data;
} hash_entry;

typedef struct hash {
  int 		size;
  hash_entry ** table;
  hash_entry * 	start;   
  enum hash_type { String_Key = 0 , Integer_Key = 1} hash_type;
} hash;

Static hash * 		make_hash();
Static hash * 		make_ihash();
Static char ** 	get_hash();
Static char **		find_hash();
Static char *		del_hash();
Static int  		operate_hash();
Static void 		destroy_hash();

#endif /* _hash_h */









