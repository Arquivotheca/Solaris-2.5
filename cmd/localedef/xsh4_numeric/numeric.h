/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)numeric.h	1.3	95/03/03 SMI"
#include "../head/_localedef.h"
#include "../head/charmap.h"

#define	T_DECIMAL_POINT	10
#define	T_THOUSANDS_SEP	20
#define	T_GROUPING	30

/*
 * Malloc macros
 */
#define	MALLOC_ENCODED\
	(encoded_val *)malloc(sizeof (encoded_val))

/*
 * function prototypes
 */
extern encoded_val	*alloc_encoded(encoded_val *);
extern int	set_enbuf(char *);
