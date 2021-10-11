/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _LIST_H
#define	_LIST_H

#pragma ident  "@(#)list.h 1.13 94/10/11 SMI"


/*
 * Includes
 */

#include "prbutl.h"
#include "expr.h"
#include "set.h"


/*
 * Declarations
 */

void			list_expr(spec_t * speclist_p, expr_t * expr_p);
void			list_set(spec_t * speclist_p, char *setname_p);
void			list_values(spec_t * speclist_p);

char		   *list_getattrs(prbctlref_t * prbctlref_p);

#endif				/* _LIST_H */
