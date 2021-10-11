/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)collate.h	1.22	95/03/08 SMI"

#include "../head/_localedef.h"
#include "../head/charmap.h"
#include "../head/_collate.h"
/*
 * collating symbol
 */
typedef struct	collating_symbol {
	char	name[MAX_ID_LENGTH];
	unsigned int	r_weight;
	struct collating_symbol	*next;
} collating_symbol;

/*
 * Malloc macros
 */
#define	MALLOC_ENCODED\
	(encoded_val *)malloc(sizeof (encoded_val))
#define	MALLOC_COLLATING_ELEMENT\
	(collating_element *)malloc(sizeof (collating_element))
#define	MALLOC_COLLATING_SYMBOL\
	(collating_symbol *)malloc(sizeof (collating_symbol))
#define	MALLOC_ORDER\
	(order *)malloc(sizeof (order))
#define	MALLOC_OTM\
	(one_to_many *)malloc(sizeof (one_to_many))

/*
 * function prototypes
 */
extern encoded_val	*alloc_encoded(encoded_val *);
extern order	*alloc_order(int, union args *);
extern int	set_enbuf(char *);
