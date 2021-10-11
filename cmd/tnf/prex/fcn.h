/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _FCN_H
#define	_FCN_H

#pragma ident  "@(#)fcn.h 1.11 94/08/25 SMI"

/*
 * Includes
 */

#include "queue.h"


/*
 * Typedefs
 */

typedef struct fcn {
	queue_node_t	qn;
	char		   *name_p;
	char		   *entry_name_p;
	caddr_t		 entry_addr;

} fcn_t;


/*
 * Declarations
 */

void fcn(char *name_p, char *func_entry_p);
void fcn_list(void);
fcn_t *fcn_find(char *name_p);
char *fcn_findname(char *entry_p);

#endif				/* _FCN_H */
