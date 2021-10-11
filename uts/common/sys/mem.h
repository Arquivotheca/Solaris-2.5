/*
 * Copyright (c) 1987-1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MEM_H
#define	_SYS_MEM_H

#pragma ident	"@(#)mem.h	1.15	94/06/10 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Memory Device Minor Numbers
 */
#define	M_MEM		0	/* /dev/mem - physical main memory */
#define	M_KMEM		1	/* /dev/kmem - virtual kernel memory & I/O */
#define	M_NULL		2	/* /dev/null - EOF & Rathole */
#define	M_ZERO		12	/* /dev/zero - source of private memory */

/*
 * EEPROM Device Minor numbers (XXX - shouldn't be here)
 */
#define	M_EEPROM	11	/* /dev/eeprom - on board eeprom device */
#define	M_METER		15	/* /dev/meter - Sunray performance meters */

#ifdef	_KERNEL

extern caddr_t mm_map;

extern int impl_obmem_pfnum(int);

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MEM_H */
