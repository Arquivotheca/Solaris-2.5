/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef	_GENERIC_H
#define	_GENERIC_H

#pragma ident	"@(#)generic.h	1.1	93/11/22 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

extern int	cannot_audit(int);
extern void	aug_init(void);
extern dev_t	aug_getport(void);
extern int 	aug_machine(char *);
extern void	aug_save_auid(au_id_t);
extern void	aug_save_uid(uid_t);
extern void	aug_save_euid(uid_t);
extern void	aug_save_gid(gid_t);
extern void	aug_save_egid(gid_t);
extern void	aug_save_pid(pid_t);
extern void	aug_save_asid(au_asid_t);
extern void	aug_save_tid(dev_t, u_long);
extern int	aug_save_me(void);
extern int	aug_save_namask(void);
extern void	aug_save_event(au_event_t);
extern void	aug_save_sorf(int);
extern void	aug_save_text(char *);
extern void	aug_save_na(int);
extern void	aug_save_user(char *);
extern void	aug_save_path(char *);
extern int	aug_save_policy(void);
extern int	aug_audit(void);
extern int	aug_na_selected(void);
extern int	aug_selected(void);
extern int	aug_daemon_session(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _GENERIC_H */
