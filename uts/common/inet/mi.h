/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_INET_MI_H
#define	_INET_MI_H

#pragma ident	"@(#)mi.h	1.13	93/12/16 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL
#ifndef _VARARGS_
#include <sys/varargs.h>
#endif
#endif

#define	MI_COPY_IN		1
#define	MI_COPY_OUT		2
#define	MI_COPY_DIRECTION(mp)	(*(int *)&(mp)->b_cont->b_next)
#define	MI_COPY_COUNT(mp)	(*(int *)&(mp)->b_cont->b_prev)
#define	MI_COPY_CASE(dir, cnt)	(((cnt)<<2)|dir)
#define	MI_COPY_STATE(mp)	MI_COPY_CASE(MI_COPY_DIRECTION(mp), \
					MI_COPY_COUNT(mp))
#ifdef	_KERNEL
#ifdef __STDC__

#ifndef MPS
extern	int	mi_adjmsg(MBLKP mp, int len_to_trim);
#endif

#ifndef NATIVE_ALLOC
extern	caddr_t	mi_alloc(uint size, int pri);
#endif

#ifdef NATIVE_ALLOC_KMEM
extern	caddr_t	mi_alloc(uint size, int pri);
extern	caddr_t	mi_alloc_sleep(uint size, int pri);
#endif

extern	queue_t *	mi_allocq(struct streamtab * st);

extern	void	mi_bufcall(queue_t * q, int size, int pri);

extern	int	mi_close_comm(void ** mi_head, queue_t * q);

extern	int	mi_close_detached(void ** mi_head, IDP ptr);

extern	void	mi_copyin(queue_t * q, MBLKP mp, char * uaddr, int len);

extern	void	mi_copyout(queue_t * q, MBLKP mp);

extern	MBLKP	mi_copyout_alloc(queue_t * q, MBLKP mp, char * uaddr, int len);

extern	void	mi_copy_done(queue_t * q, MBLKP mp, int err);

extern	int	mi_copy_state(queue_t * q, MBLKP mp, MBLKP * mpp);

#ifndef NATIVE_ALLOC
extern	void	mi_free(char * ptr);
#endif

#ifdef NATIVE_ALLOC_KMEM
extern	void	mi_free(char * ptr);
#endif

extern	void	mi_detach(void ** mi_head, IDP ptr);

extern	int	mi_iprintf(char * fmt, va_list ap, pfi_t putc_func,
			char * cookie);

extern	boolean_t	mi_link_device(queue_t * orig_q, char * name);

extern	int	mi_mpprintf(MBLKP mp, char * fmt, ...);

extern	int	mi_mpprintf_nr(MBLKP mp, char * fmt, ...);

extern	int	mi_mpprintf_putc(char * cookie, int ch);

extern	IDP	mi_first_ptr(void ** mi_head);
extern	IDP	mi_next_ptr(void ** mi_head, IDP ptr);

#ifdef SVR3_STYLE
extern	int	mi_open_comm(void ** mi_head, uint size,
			queue_t * q, dev_t dev, int flag, int sflag);
#endif

#ifdef SVR4_STYLE
extern	int	mi_open_comm(void ** mi_head, uint size,
			queue_t * q, dev_t * devp, int flag, int sflag,
			cred_t * credp);
#endif

extern	u8 *	mi_offset_param(mblk_t * mp, u32 offset, u32 len);

extern	u8 *	mi_offset_paramc(mblk_t * mp, u32 offset, u32 len);

extern	int	mi_panic(char * fmt, ...);

extern	boolean_t	mi_set_sth_hiwat(queue_t * q, int size);

extern	boolean_t	mi_set_sth_lowat(queue_t * q, int size);

extern	boolean_t	mi_set_sth_maxblk(queue_t * q, int size);

extern	boolean_t	mi_set_sth_wroff(queue_t * q, int size);

extern	int	mi_sprintf(char * buf, char * fmt, ...);

extern	int	mi_sprintf_putc(char * cookie, int ch);

extern	int	mi_strcmp(char * cp1, char * cp2);

extern	int	mi_strlen(char * str);

extern	int	mi_strlog(queue_t * q, ...);

extern	long	mi_strtol(char * str, char ** ptr, int base);

extern	void	mi_timer(queue_t * q, MBLKP mp, long tim);

extern	MBLKP	mi_timer_alloc(uint size);

extern	void	mi_timer_free(MBLKP mp);

extern	boolean_t	mi_timer_valid(MBLKP mp);

extern	MBLKP	mi_reallocb(MBLKP mp, int new_size);

extern	MBLKP	mi_tpi_ack_alloc(MBLKP mp, uint size, uint type);

extern	MBLKP	mi_tpi_conn_con(MBLKP trailer_mp, char * src, int src_length,
				char * opt, int opt_length);

extern	MBLKP	mi_tpi_conn_ind(MBLKP trailer_mp, char * src, int src_length,
				char * opt, int opt_length, int seqnum);

extern	MBLKP	mi_tpi_discon_ind(MBLKP trailer_mp, int reason, int seqnum);

extern	MBLKP	mi_tpi_err_ack_alloc(MBLKP mp, int tlierr, int unixerr);

extern	MBLKP	mi_tpi_ok_ack_alloc(MBLKP mp);

extern	MBLKP	mi_tpi_ordrel_ind(void);

extern	MBLKP	mi_tpi_trailer_alloc(MBLKP trailer_mp, int size, int type);

extern	MBLKP	mi_tpi_uderror_ind(char * dest, int dest_length, char * opt,
				int opt_length, int error);

extern	IDP	mi_zalloc(uint size);
extern	IDP	mi_zalloc_sleep(uint size);

#endif	/* __STDC__ */
#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _INET_MI_H */
