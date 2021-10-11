/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef _INET_ND_H
#define	_INET_ND_H

#pragma ident	"@(#)nd.h	1.7	93/11/11 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	ND_BASE		('N' << 8)	/* base */
#define	ND_GET		(ND_BASE + 0)	/* Get a value */
#define	ND_SET		(ND_BASE + 1)	/* Set a value */

#if defined(_KERNEL) && defined(__STDC__)

/* Free the table pointed to by 'ndp' */
extern	void	nd_free(caddr_t * nd_pparam);

extern	int	nd_getset(queue_t * q, caddr_t nd_param, MBLKP mp);

/*
 * This routine may be used as the get dispatch routine in nd tables
 * for long variables.  To use this routine instead of a module
 * specific routine, call nd_load as
 *	nd_load(&nd_ptr, "name", nd_get_long, set_pfi, &long_variable)
 * The name of the variable followed by a space and the value of the
 * variable will be printed in response to a get_status call.
 */
extern	int	nd_get_long(queue_t * q, MBLKP mp, caddr_t data);

/*
 * Load 'name' into the named dispatch table pointed to by 'ndp'.
 * 'ndp' should be the address of a char pointer cell.  If the table
 * does not exist (*ndp == 0), a new table is allocated and 'ndp'
 * is stuffed.  If there is not enough space in the table for a new
 * entry, more space is allocated.
 */
extern	boolean_t	nd_load(caddr_t * nd_pparam, char * name,
				pfi_t get_pfi, pfi_t set_pfi, caddr_t data);

extern	int	nd_set_long(queue_t * q, MBLKP mp, char * value, caddr_t data);

#endif	/* defined(_KERNEL) && defined(__STDC__) */

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_ND_H */
