/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_STABS_H
#define	_SYS_STABS_H

#pragma ident	"@(#)stabs.h	1.7	95/03/16 SMI"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <setjmp.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define	MAXLINE	1024

#define	BUCKETS	128

struct	tdesc	*hash_table[BUCKETS];
struct	tdesc	*name_table[BUCKETS];

struct node {
	char *name;
	struct child *child;
};

struct	child {
	char *name;
	char *format;
	struct child *next;
};

#define	HASH(NUM)		((int)(NUM & (BUCKETS - 1)))

enum type {
	INTRINSIC,
	POINTER,
	ARRAY,
	FUNCTION,
	STRUCT,
	UNION,
	ENUM,
	FORWARD,
	TYPEOF,
};

struct tdesc {
	char	*name;
	struct	tdesc *next;
	enum	type type;
	int	size;
	union {
		struct	tdesc *tdesc;	/* *, f , to */
		struct	ardef *ardef;	/* ar */
		struct	mlist *members;	/* s, u */
		struct  elist *emem; /* e */
	} data;
	int	id;
	struct tdesc *hash;
};

struct elist {
	char	*name;
	int	number;
	struct elist *next;
};

struct element {
	struct tdesc *index_type;
	int	range_start;
	int	range_end;
};

struct ardef {
	struct tdesc	*contents;
	struct element	*indices;
};

struct mlist {
	int	offset;
	int	size;
	char	*name;
	struct	mlist *next;
	struct	tdesc *fdesc;		/* s, u */
};

#define	ALLOC(t)		((t *)malloc(sizeof (t)))

struct	tdesc *lookupname();

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_STABS_H */
