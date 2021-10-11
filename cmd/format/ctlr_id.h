
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef	_CTLR_ID_H
#define	_CTLR_ID_H

#pragma ident	"@(#)ctlr_id.h	1.5	93/11/06 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Error types returned by id_cmd()
 */
#define	ID_SUCCESS		0
#define	ID_ERROR		(-1)
#define	ID_MAY_BE_RESERVED	(-2)


#ifdef	__STDC__
/*
 * ANSI prototypes for global functions
 */
int	id_rdwr(int, int, daddr_t, int, caddr_t, int);
#else
int	id_rdwr();
#endif	/* __STDC__ */

#ifdef	__cplusplus
}
#endif

#endif	/* _CTLR_ID_H */
