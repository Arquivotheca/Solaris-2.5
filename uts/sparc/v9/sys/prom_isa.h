/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_PROM_ISA_H
#define	_SYS_PROM_ISA_H

#pragma ident	"@(#)prom_isa.h	1.3	94/12/19 SMI"

/*
 * This file contains external ISA-specific promif interface definitions.
 * There may be none.  This file is included by reference in <sys/promif.h>
 *
 * This version of the file is for 32 bit client programs calling the
 * 64 bit cell-sized SPARC v9 firmware client interface handler.
 * On SPARC v9 machines, a 32-bit client program must provide
 * a function to manage the conversion of the 32-bit stack to
 * a 64-bit stack, before calling the firmware's client interface
 * handler.
 */

#ifdef	__cplusplus
extern "C" {
#endif

typedef	unsigned long long cell_t;

typedef	int	ihandle_t;
typedef	void	*phandle_t;
typedef	void	*dnode_t;

/*
 * Pointer macros assume pointer fits in an "unsigned", and should
 * not be sign extended when promoted.
 */
#define	p1275_ptr2cell(p)	((cell_t)((unsigned)((void *)(p))))
#define	p1275_int2cell(i)	((cell_t)((int)(i)))
#define	p1275_uint2cell(u)	((cell_t)((unsigned int)(u)))
#define	p1275_phandle2cell(ph)	((cell_t)((unsigned)((void *)(ph))))
#define	p1275_dnode2cell	p1275_phandle2cell
#define	p1275_ihandle2cell(ih)	((cell_t)((unsigned)((void *)(ih))))

#define	p1275_cell2ptr(p)	((void *)((cell_t)(p)))
#define	p1275_cell2int(i)	((int)((cell_t)(i)))
#define	p1275_cell2uint(u)	((unsigned int)((cell_t)(u)))
#define	p1275_cell2phandle(ph)	((void *)((cell_t)(ph)))
#define	p1275_cell2dnode	p1275_cell2phandle
#define	p1275_cell2ihandle(ih)	((void *)((cell_t)(ih)))

/*
 * Define default cif handlers:  This port uses SPARC V8 32 bit semantics
 * on the calling side and the prom side.
 */
#define	p1275_cif_init			p1275_sparc_cif_init
#define	p1275_cif_handler		p1275_sparc_cif_handler

extern void	*p1275_sparc_cif_init(void *);
extern int	p1275_cif_handler(void *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_PROM_ISA_H */
