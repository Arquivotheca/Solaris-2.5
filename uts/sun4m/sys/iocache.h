/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#ifndef _SYS_IOCACHE_H
#define	_SYS_IOCACHE_H

#pragma ident	"@(#)iocache.h	1.9	93/05/28 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#define	IOC_CACHE_LINES	0x400		/* Num cache lines in the iocache  */

#define	IOC_ADDR_MSK	0x007FE000	/* mask to extract pa<13-22> */
#define	IOC_RW_SHIFT	0x8		/* shift pa<13-22> to pa<5-14> */
#define	IOC_FLUSH_PHYS_ADDR	0xDF020000
#define	IOC_TAG_PHYS_ADDR	0xDF000000

#define	IOC_PAGES_PER_LINE	0x2	/* one line maps 8k */
#define	IOC_LINE_MAP		0x2000	/* one line maps 8k */
#define	IOC_LINESIZE		32
#define	IOC_LINEMASK		(IOC_LINESIZE-1)

/* some bits in ioc tag */
#define	IOC_LINE_ENABLE		(0x00200000) /* IOC enabled bit in tag */
#define	IOC_LINE_WRITE		(0x00100000) /* IOC writeable bit in tag */

/* tell ioc_setup() what to do */
#define	IOC_LINE_INVALID	(0x1)
#define	IOC_LINE_NOIOC		(0x2)	/* do not set IC bit */
#define	IOC_LINE_LDIOC		(0x4)

#if defined(_KERNEL) && !defined(_ASM)

extern int ioc;

extern void ioc_setup(int, int);
extern void ioc_flush(int);
extern int ioc_read_tag(int);
extern int is_iocable(u_int, u_int, u_long);

extern void do_load_ioc(int, int);
extern int do_read_ioc(int);
extern void do_flush_ioc(int);

#endif /* _KERNEL && !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IOCACHE_H */
