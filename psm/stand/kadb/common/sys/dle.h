/*
 * Copyright (c) 1987 by Sun Microsystems, Inc.
 */

#ident  "@(#)dle.h 1.1     94/09/29 SMI"

#define	DLE_TBUFSIZE 128
#define	DLE_RBUFSIZE 2048
/* NUMRMDS: Number of receive rings, must be a power of two */
#define	NUMRMDS 8
/* RLEN is log base 2 of NUMRMDS */
#define	RLEN 3
#define	DLE_INDIV_RBUFSIZE (DLE_RBUFSIZE >> RLEN)

/*
 * this file contains information regarding this
 * simple implementation of an le driver
 */

struct dle_mem_header {
	struct lance_init_block ib;
	char pad[0x28];	/* for alignment */
	struct lmd rmd[NUMRMDS];
	struct lmd tmd;
};

struct dle_memory_image {
	unsigned char tbuf[DLE_TBUFSIZE];
	unsigned char rbuf[DLE_RBUFSIZE];
	struct dle_mem_header hdr;
};

#define	DLE_OFFSET(Field) \
	((unsigned int)&(((struct dle_memory_image *)0)->Field))

#define	DLE_BOGUS_HANDLE 0xabcdef57

#define	MINPKTSIZE 64

int (*obp_write)(), (*obp_read)();
int dle_getmacaddr(char *ea);
