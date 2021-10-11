
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef	_CHECKMOUNT_H
#define	_CHECKMOUNT_H

#pragma ident	"@(#)checkmount.h	1.3	93/03/25 SMI"

#ifdef	__cplusplus
extern "C" {
#endif


/*
 *	Prototypes for ANSI C
 */
int	checkmount(daddr_t start, daddr_t end);
int	check_label_with_mount();

#ifdef	__cplusplus
}
#endif

#endif	/* _CHECKMOUNT_H */
