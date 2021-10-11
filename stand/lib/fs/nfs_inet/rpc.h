/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef	_RPC_H
#define	_RPC_H

#ident	"@(#)rpc.h	1.3	91/06/04 SMI"

/*
 * Rather than include rpc/types.h, and run into the conflict of
 * the kernel/boot kmem_alloc prototypes, just include what we need
 * here.
 */
#ifndef	_RPC_TYPES_H
#define	_RPC_TYPES_H
#endif	/* _RPC_TYPES_H */

#define	bool_t	int
#define	enum_t	int
#define	__dontcare__	-1

#ifndef	TRUE
#define	TRUE	(1)
#endif

#ifndef	FALSE
#define	FALSE	(0)
#endif

#ifndef NULL
#define	NULL	0
#endif

#include <sys/time.h>

#endif	/* _RPC_H */
