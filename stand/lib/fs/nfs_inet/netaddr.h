/*
 * Copyright (c) 1991, by Sun Microsystems, Inc.
 */

#ifndef _NETADDR_H
#define	_NETADDR_H

#pragma ident	"@(#)netaddr.h	1.12	94/08/09 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * enumerated types for use by netaddr.c and functions that call it's
 * functions.
 */
enum netaddr_type {
	SOURCE = 0,	/* our address */
	DESTIN = 1	/* our destination's address */
};

/*
 * Maximum size of a ethernet packet according to ethernet protocol
 * is 1536 bytes, which includes the ethernet header.  However, we
 * need to allocate a big enough packet to handle any potential h/w
 * link level protocols.  FDDI/S for example, is larger that your
 * typical ethernet.  The MAX_PKT_SIZE should be the "MTU" (Max transport
 * unit)  property of the underlying interface, but until we fix that,
 * we'll just make it arbitrarily large, which wastes space like crazy
 * in all the static buffers in inetboot.
 */

#ifdef i386		/* i386 version cannot take huge packet size yet! */
#define	MAX_PKT_SIZE	1536	/* ethernet pkt maximum data size. */
#else
#define	MAX_PKT_SIZE	((16*1024)+128)	/* MTU: 16K plus a bit of fluff */
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _NETADDR_H */
