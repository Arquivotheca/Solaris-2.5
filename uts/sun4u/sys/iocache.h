/*
 * Copyright (c) 1991-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_IOCACHE_H
#define	_SYS_IOCACHE_H

#pragma ident	"@(#)iocache.h	1.10	95/02/23 SMI"

#ifndef _ASM
#include <sys/sysiosbus.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#define	OFF_STR_BUF_CTRL_REG	0x2800
#define	STR_BUF_CTRL_REG_SIZE	(NATURAL_REG_SIZE)
#define	OFF_STR_BUF_FLUSH_REG	0x2808
#define	STR_BUF_FLUSH_REG_SIZE	(NATURAL_REG_SIZE)
#define	OFF_STR_BUF_SYNC_REG	0x2810
#define	STR_BUF_SYNC_REG_SIZE	(NATURAL_REG_SIZE)

#define	STREAM_BUF_ENABLE	0x1ull
#define	IOCACHE_LINE_SIZE_MASK	0x3f		/* 64 byte line size */
#define	STREAM_BUF_SYNC_WAIT	50		/* 50 ticks = 1/2 second */
#define	STREAM_BUF_OFF		1		/* All stream bufs off */
#define	STREAM_BUF_TIMEOUT	2		/* Streaming buf timed out */

#if defined(_KERNEL) && !defined(_ASM)

extern int stream_buf_init(struct sbus_soft_state *, int);
extern int stream_buf_resume_init(struct sbus_soft_state *);
extern void sync_stream_buf(struct sbus_soft_state *, u_long, u_int);

#endif /* _KERNEL && !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_IOCACHE_H */
