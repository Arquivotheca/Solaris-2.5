/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)_libcollate.h	1.2	94/10/05 SMI"

#include "_localedef.h"
#include "_collate.h"

typedef struct start_points {
	one_to_many *start_otm;
	one_to_many *otm_starts[MAX_WEIGHTS];
	collating_element *start_coll;
	order *start_order;
} start_points;

typedef struct _coll_info {
	int fd;
	unsigned int size;
	start_points _coll_starts;
	union {
		header *hp;
		char *mapin;
	} u;
} _coll_info;

#define COLL_USE_C      0
#define COLL_USE_LOCALE 1

#define	T_STRXFRM	0
#define	T_WCSXFRM	1
