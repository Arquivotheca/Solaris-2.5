/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _PCTYPES_H
#define	_PCTYPES_H

#pragma ident	"@(#)pctypes.h	1.5	95/01/30 SMI"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PCMCIA General Types
 */

typedef int irq_t;		/* IRQ level */
typedef unsigned char *baseaddr_t; /* memory base address */
#if defined(i386)
typedef unsigned int ioaddr_t;
#endif
#if defined(sparc)
typedef caddr_t ioaddr_t;
#endif

typedef u_int (*intrfunc_t)(caddr_t);

#if defined(sparc)
#define	inb(port)	(*(volatile u_char *)(port))
#define	inw(port)	(*(volatile u_short *)(port))
#define	inl(port)	(*(volatile u_long *)(port))
#define	outb(port, value)	(*(u_char *)(port) = (value))
#define	outw(port, value)	(*(u_short *)(port) = (value))
#define	outl(port, value)	(*(u_long *)(port) = (value))
#define	repinsb(port, addr, count) \
		{int __i; volatile u_char *__c; \
			for (__i = 0, __c = (u_char *)(addr);\
				__i < (count); __i++) \
					*__c++ = inb(port);\
		}
#define	repinsw(port, addr, count) \
		{int __i; volatile u_short *__s; \
			for (__i = 0, __s = (u_short *)(addr);\
				__i < (count); __i++) \
					*__s++ = inw(port);\
		}
#define	repinsd(port, addr, count) \
		{int __i; volatile u_long *__l; \
			for (__i = 0, __l = (u_long *)(addr);\
				__i < (count); __i++) \
					*__l++ = inl(port);\
		}
#define	repoutsb(port, addr, count) \
		{int __i; volatile u_char *__c; \
			for (__i = 0, __c = (u_char *)(addr);\
				__i < (count); __i++)\
					outb(port, *__c++);\
		}
#define	repoutsw(port, addr, count) \
		{int __i; volatile u_short *__s; \
			for (__i = 0, __s = (volatile u_short *)(addr);\
				__i < (count); __i++) \
					outw(port, *__s++);\
		}
#define	repoutsd(port, addr, count) \
		{int __i; volatile u_long *__l; \
			for (__i = 0, __l = (u_long *)(addr);\
				__i < (count); __i++) \
					outl(port, *__l++);\
		}

#endif

#if defined(_BIG_ENDIAN)
#define	leshort(a)	((((a) & 0xFF) << 8) | (((a) >> 8) & 0xFF))
#define	lelong(a)	(leshort((a) >> 16) | (leshort(a) << 16))
#else
#define	leshort(a)	(a)
#define	lelong(a)	(a)
#endif

#ifdef __cplusplus
}
#endif

#endif /* _PCTYPES_H */
