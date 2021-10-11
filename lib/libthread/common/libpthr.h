/*	Copyright (c) 1993, by Sun Microsystems, Inc.	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF SMI	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/


#ifndef _LIBPTHR_H
#define	_LIBPTHR_H

#pragma ident	"@(#)libpthr.h	1.6	94/11/03 SMI"

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef	struct	_cvattr {
	void	*next;		/* in case we decide to make link list */
	int	pshared;
} cvattr_t;


typedef	struct	_mattr {
	void	*next;		/* in case we decide to make link list */
	int	pshared;
	int	protocol;
	int 	prioceiling;
} mattr_t;


typedef	struct	_thrattr {
	void	*next;		/* in case we decide to make link list */
	size_t	stksize;
	void	*stkaddr;
	int	detachstate;
	int 	scope;
	int 	prio;
	int 	policy;
	int 	inherit;
} thrattr_t;


#ifdef __STDC__

caddr_t	_alloc_attr(int size);
int	_free_attr(caddr_t attr);

#endif /* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _LIBPTHR_H */
