/*
 * Copyright (c) 1987, by Sun Microsystems, Inc.
 */

#ifndef _SYS_TTY_H
#define	_SYS_TTY_H

#pragma ident	"@(#)tty.h	2.24	92/07/14 SMI"	/* SunOS 4.0 2.13 */

#include <sys/stream.h>
#include <sys/termios.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct {
	int	t_flags;
	queue_t	*t_readq;	/* stream's read queue */
	queue_t	*t_writeq;	/* stream's write queue */
	unsigned long t_iflag;	/* copy of iflag from tty modes */
	unsigned long t_cflag;	/* copy of cflag from tty modes */
	u_char	t_stopc;	/* copy of c_cc[VSTOP] from tty modes */
	u_char	t_startc;	/* copy of c_cc[VSTART] from tty modes */
	struct winsize t_size;	/* screen/page size */
	mblk_t	*t_iocpending;	/* ioctl reply pending successful allocation */
	kmutex_t	t_excl;		/* keeps struct consistent */
} tty_common_t;

#define	TS_XCLUDE	0x00000001	/* tty is open for exclusive use */
#define	TS_SOFTCAR	0x00000002	/* force carrier on */

#ifdef	__STDC__
extern void	ttycommon_close(tty_common_t *tc);
extern void	ttycommon_qfull(tty_common_t *tc, queue_t *q);
extern unsigned	ttycommon_ioctl(tty_common_t *tc, queue_t *q, mblk_t *mp,
		    int *errorp);
#else
extern void	ttycommon_close();
extern void	ttycommon_qfull();
extern unsigned	ttycommon_ioctl();
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_TTY_H */
