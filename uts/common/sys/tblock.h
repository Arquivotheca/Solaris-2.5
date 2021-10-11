/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_TBLOCK_H
#define	_SYS_TBLOCK_H

#pragma ident	"@(#)tblock.h	1.13	94/07/29 SMI"

#include <sys/thread.h>
#include <sys/sobject.h>

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

#ifdef	__STDC__
extern void t_block_sig(turnstile_t *, caddr_t, sobj_ops_t *, disp_lock_t *);
extern void t_block(turnstile_t *, caddr_t, sobj_ops_t *, disp_lock_t *);
extern void t_release(turnstile_t *, turnstile_id_t *, qobj_t);
extern void t_release_all(turnstile_t *, turnstile_id_t *, qobj_t);

extern void t_block_sig_chan(caddr_t);
extern void t_block_chan(caddr_t);
extern void t_release_chan(caddr_t);
extern void t_release_all_chan(caddr_t);
extern int t_waitqempty_chan(caddr_t);

#else

extern void t_block_sig();
extern void t_block();
extern void t_release();
extern void t_release_all();

extern void t_block_sig_chan();
extern void t_block_chan();
extern void t_release_chan();
extern void t_release_all_chan();
extern int t_waitqempty_chan();

#endif	/* __STDC__ */

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TBLOCK_H */
