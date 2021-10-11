/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef	_VOLMGT_PRIVATE_H
#define	_VOLMGT_PRIVATE_H

#pragma ident	"@(#)volmgt_private.h	1.3	94/06/06 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Interfaces that are private to the volmgt library.
 */
char 	*getrawpart0(char *path);
char 	*volmgt_getfullblkname(char *n);
char 	*volmgt_getfullrawname(char *n);
char 	*volmgt_completename(char *name);

#define	DEFAULT_ROOT	"/vol"
#define	DEFAULT_CONFIG	"/etc/vold.conf"
#define	MAXARGC		100

#ifndef	TRUE
#define	TRUE		1
#define	FALSE		0
#endif

#ifndef	NULLC
#define	NULLC		'\0'
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _VOLMGT_PRIVATE_H */
