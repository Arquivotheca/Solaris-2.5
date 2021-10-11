/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)fastrank.c	1.4	95/06/22 SMI"

/*
 *	this routines implement a fast scheme to find the rank
 *	of a character.
 *
 *	Runtime code is in usr/src/lib/libc/port/gen/libcollate.c
 *
 */


#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "hash.h"
#include "collate.h"

#define NOT_AN_INDEX 0

typedef struct intlist {
  int num_entries;
  void * keytbl;
  void * ranktbl;
} intlist_t;

typedef struct builder {
  int 	      counter;
  intlist_t * intlist_ptr;
  inttab_t  * inttab_ptr;
} builder_t;


void * 
CreateIntList(int sizeguess)
{
  intlist_t * ptr = (intlist_t *)malloc(sizeof(*ptr));
  
  ptr->keytbl = make_ihash(sizeguess);;
  ptr->ranktbl = make_ihash(sizeguess);;
  ptr->num_entries = 0;

  return((void*)ptr);
}

AddToIntList(void * intlist, int keyin, int rankin)
{
  intlist_t * ptr = (intlist_t *) intlist;
  int ** keydata, ** rankdata;

  keydata = (int**)get_hash(ptr->keytbl, (unsigned char *)keyin);
  rankdata = (int**)get_hash(ptr->ranktbl, (unsigned char *)rankin);

  if(!*keydata && !*rankdata) { /* unique rank & key - all ok */
    *keydata = (int *) malloc(sizeof(int));
    *rankdata = (int *) malloc(sizeof(int));
    ptr->num_entries++;
  }
  else
      return((*keydata)?-1:-2);
  
  **keydata = rankin;
  **rankdata = keyin;

  return(0);
}

SizeIntList(void * intlist)
{
  intlist_t * ptr = (intlist_t * ) intlist;

  return(sizeof(inttab_t) * (ptr->num_entries +1));
}

CopyIntList(void * intlist, void * destination)
{
  static int build_it();
  builder_t build;

  build.counter = 1;
  build.intlist_ptr = (intlist_t * ) intlist;
  build.inttab_ptr = (inttab_t *) destination;
  
  memset(destination, 0, (build.intlist_ptr->num_entries+1)*sizeof(inttab_t));

  build.inttab_ptr->key = build.intlist_ptr->num_entries;

  operate_hash(build.intlist_ptr->keytbl, build_it, &build);

}

DestroyIntList(void * intlist)
{
  extern void free();
  intlist_t * ptr = (intlist_t *)intlist;
  destroy_hash(ptr->keytbl,  free, NULL);
  destroy_hash(ptr->ranktbl,  free, NULL);
  free(intlist);
}

static int build_it(unsigned int * data, void * usr_arg, unsigned int key)
{
  builder_t * ptr = (builder_t *) usr_arg;
  inttab_t * a;
  inttab_t * b;
  unsigned int * add_ptr;

  unsigned int bucket;
 
  a = ptr->inttab_ptr + ptr->counter;

  a->key 	= key;
  a->rank	= *data;
  /*
  a->next_key	= NOT_AN_INDEX;
  a->next_rank  = NOT_AN_INDEX;
  */

  /*
    insert into key hash chain
    */
  bucket = key % (ptr->intlist_ptr->num_entries) + 1;

  b = ptr->inttab_ptr + bucket;

  if(b->first_key == NOT_AN_INDEX) {
    add_ptr = &b->first_key;
  } else {
    b = ptr->inttab_ptr + b->first_key;
    while(b->next_key != NOT_AN_INDEX)
      b = ptr->inttab_ptr + b->next_key;
    add_ptr = &b->next_key;	
  }

  *add_ptr = ptr->counter;

  /*
    insert into rank hash chain
    */

  bucket = (*data) % (ptr->intlist_ptr->num_entries) + 1;

  b = ptr->inttab_ptr + bucket;

  if(b->first_rank == NOT_AN_INDEX) {
    add_ptr = &b->first_rank;
  } else {
    b = ptr->inttab_ptr + b->first_rank;
    while(b->next_rank != NOT_AN_INDEX)
      b = ptr->inttab_ptr + b->next_rank;
    add_ptr = &b->next_rank;	
  }

  *add_ptr = ptr->counter++;
}

#ifdef TEST

/*
  debugging routines
  */

main(int argc, char * argv[])
{
  FILE * f;
  unsigned char buffer1[200];
  unsigned char buffer2[200];

  void * space;
  void * handle;


  /* 
    if given two arguments argument, create the shared file and exit
    */

  if(argc > 2) {
    int size;
    FILE * out;
    int a, b;
    int lineno;

    if((out = fopen(argv[1], "w"))==NULL) {
      perror("open");
      exit(1);
    }
    
    if((f = fopen(argv[2], "r"))==NULL) {
      perror("open");
      exit(1);
    }

    handle = CreateIntList(30000);
    
    lineno = 0;

    while(fscanf(f, "%d %d\n", &a, &b) != EOF) {
	    switch((AddToIntList(handle, a,b))) {
	    case 0:
		    break;
	    case -1:
		    printf("duplicate key detected in line %d of input file\n",
			   lineno);
		    unlink(argv[1]);
		    exit(1);
		    break;
	    case -2:
		    printf("duplicate rank detected in line %d of input file\n",
			   lineno);
		    unlink(argv[1]);
		    exit(1);
		    break;
	    default:
		    abort();
	    }
	    
    }

    fclose(f);
    
    space = (void *)malloc(size = SizeIntList(handle));

    CopyIntList(handle, space);
    
    write(fileno(out), space, size);
    fclose(out);
    exit(0);
  }

 else {
    
    FILE * in; 
    struct stat buf;
    int a, b, c;
    int k,j,i;
    int table[50000][2];

    long long s, gethrtime();

    if(!(in=fopen(argv[1], "r"))) {
      perror("fopen");
      exit(1);
    }

    if(fstat(fileno(in), & buf) < 0) {
      perror("fstat");
      exit(1);
    }

    if((space = mmap(NULL, buf.st_size, PROT_READ, MAP_SHARED, fileno(in), 0)) == MAP_FAILED) {
      perror("mmap");
      exit(1);
    }

    i = 0;

    while(scanf("%d %d\n", &a, &b) != EOF) {
	    table[i][0] = a;
	    table[i++][1] = b;
    }

    s = gethrtime();

    for(k=0;k<14;k++) {
	    for(j=0;j<i;j++) {
		    if(table[j][1] != (c=IntTabFindRankbyChar(space, table[j][0]))) {
			    printf("line %d:char %d appears to have rank %d rather than rank %d\n",
				   j, table[j][0],c,table[j][1]);
		    }
	    }
    }

    printf("took %lf seconds to get rank of  %d characters\n", (double)(gethrtime()-s)/1.0e9,
	   k*i);
    
    s = gethrtime();

    for(k=0;k<14;k++) {
	    for(j=0;j<i;j++) {
		    if(table[j][0] != (c=IntTabFindCharbyRank(space, table[j][1]))) {
			    printf("line %d: Rank %d appears to match char %d rather than char %d\n",
				   j, table[j][1],c,table[j][0]);
		    }
	    }
    }

    printf("took %lf seconds to get chars for  %d ranks\n", (double)(gethrtime()-s)/1.0e9,
	   k*i);
    


  exit(0);
  }

}
#endif

  
