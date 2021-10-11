/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _NEW_H
#define	_NEW_H

#pragma ident  "@(#)new.h 1.6 94/08/24 SMI"

/*
 * Includes
 */

#include <sys/types.h>


/*
 * Defines
 */

#define	new(t)		((t *) (new_alloc(sizeof (t))))


/*
 * Declarations
 */

void		   *new_alloc(size_t size);

#endif				/* _NEW_H */
