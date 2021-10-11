/*
 * Copyright (C) 1992 by Sun Microsystems, Inc.
 */

#ifndef	_TLI_H
#define	_TLI_H

#pragma ident	"@(#)tli.h	1.1	94/03/02 SMI"

struct _ti_user *_t_checkfd(int fd);
int delete_tilink(int s);
int _rcv_conn_con(struct _ti_user *tiptr, struct t_call *call);
int _snd_conn_req(struct _ti_user *tiptr, struct t_call *call);
void _t_aligned_copy(char *buf, int len, int init_offset,
	char *datap, long *rtn_offset);
struct _ti_user *_t_create(int fd, struct t_info *info);
void _t_blocksigpoll(sigset_t *mask, int action);
int _t_do_ioctl(int fd, char *buf, int size, int cmd, int *retlen);
int _t_is_event(int fd, struct _ti_user *tiptr);
int _t_is_ok(int fd, struct _ti_user *tiptr, long type);
int _t_look_locked(int fd, struct _ti_user *tiptr);
int _t_max(int x, int y);
void _t_putback(struct _ti_user *tiptr, caddr_t dptr, int dsize,
	caddr_t cptr, int csize);

/*
 * External declarations
 */
extern mutex_t _ti_userlock;


#endif	/* _TIMT_H */
