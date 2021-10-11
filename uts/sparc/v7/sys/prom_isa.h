/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_PROM_ISA_H
#define	_SYS_PROM_ISA_H

#pragma ident	"@(#)prom_isa.h	1.2	94/11/14 SMI"

/*
 * This file contains external ISA-specific promif interface definitions.
 * There may be none.  This file is included by reference in <sys/promif.h>
 *
 * This version of the file is for 32 bit SPARC implementations.
 */

#ifdef	__cplusplus
extern "C" {
#endif

typedef	int	ihandle_t;
typedef	void	*phandle_t;
typedef	void	*dnode_t;

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_PROM_ISA_H */
