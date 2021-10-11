/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MCPBUF_H
#define	_SYS_MCPBUF_H

#pragma ident	"@(#)mcpbuf.h	1.5	94/01/06 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

struct zs_dmabuf {
	u_char	*d_baddr;	/* Base Addr of the Buf */
	short	d_wc;		/* Current word count */
};

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_MCPBUF_H */
