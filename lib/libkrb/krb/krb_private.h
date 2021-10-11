/*
 * krb_private.h, Private header file for the Kerberos library.
 *
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef	_KRB_PRIVATE_H
#define	_KRB_PRIVATE_H


#pragma ident	"@(#)(krb_private.h)	1.3	93/06/08 SMI"


#ifdef	__cplusplus
extern "C" {
#endif

#if defined(SYSV) && !defined(_KERNEL)

#define	bcopy(x1, x2, cnt) memmove(x2, x1, cnt)
#define	bcmp(x1, x2, cnt) memcmp(x2, x1, cnt)
#define	bzero(s, cnt) memset(s, 0, cnt)

#endif /* SYSV && !_KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _KRB_PRIVATE_H */
