/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)message.h	1.4	95/03/08 SMI"
#include "../head/_localedef.h"
#include "../head/charmap.h"


#define	T_YES_STR		0
#define	T_NO_STR		1
#define	T_YES_EXP		2
#define	T_NO_EXP		3
#define	T_NUM_STRINGS	4
/*
 * Malloc macros
 */
#define	MALLOC_ENCODED\
	(encoded_val *)malloc(sizeof (encoded_val))

/*
 * function prototypes
 */
extern encoded_val	*alloc_encoded(encoded_val *);
extern int			set_enbuf(char *);
