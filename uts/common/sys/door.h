/*
 * Copyright (c) 1994 Sun Microsystems, Inc.
 * All rights reserved.
 *
 * The I/F's described herein are expermental, highly volatile and
 * intended at this time only for use with Sun internal products.
 * SunSoft reserves the right to change these definitions in a minor
 * release.
 */

#ifndef	_SYS_DOOR_H
#define	_SYS_DOOR_H

#pragma ident	"@(#)door.h	1.6	95/07/20 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#if !defined(_ASM)

#include <sys/types.h>
#include <sys/mutex.h>
#include <sys/vnode.h>

/*
 * The door IPC I/F.
 */
#define	DOOR_INVAL -1			/* An invalid door descriptor */
#define	DOOR_UNREF_DATA ((void *)-1)	/* Unreferenced invocation flag */

/*
 * Attributes associated with doors
 */
#define	DOOR_LOCAL	0x01	/* Descriptor local to current process */
#define	DOOR_MOVE	0x02	/* Move the descriptor arg during invocation */
#define	DOOR_UNREF	0x04	/* Deliver an unref notification with door */
#define	DOOR_REVOKED	0x08	/* The door has been revoked */

/*
 * Max size of data allowed (for now)
 */
#define	DOOR_MAX_BUF	(16 * 1024)

/*
 * Structure used to pass descriptors in door invocations
 */
typedef struct door_desc {
	u_int	d_attributes;
	int	d_descriptor;
} door_desc_t;

typedef unsigned long long door_ptr_t;	/* Handle 64 bit pointers */

/*
 * Structure used to return info from door_info
 */
typedef struct door_info {
	pid_t		di_target;	/* Server process */
	door_ptr_t	di_proc;	/* Server procedure */
	door_ptr_t	di_data;	/* Data cookie */
	u_int		di_attributes;	/* Attributes associated with door */
	long		di_uniqifier;	/* Unique number */
} door_info_t;

/*
 * Structure used to return info from door_cred
 */
typedef struct door_cred {
	uid_t	dc_euid;	/* Effective uid of client */
	gid_t	dc_egid;	/* Effective gid of client */
	uid_t	dc_ruid;	/* Real uid of client */
	gid_t	dc_rgid;	/* Real gid of client */
	pid_t	dc_pid;		/* pid of client */
} door_cred_t;

#if defined(_KERNEL)

/*
 * More door flags
 */
#define	DOOR_DELAY_UNREF	0x10	/* Delayed unref delivery */

/*
 * Errors used for doors. Negative numbers to avoid conflicts with errnos
 */
#define	DOOR_WAIT	-1	/* Waiting for response */
#define	DOOR_EXIT	-2	/* Server thread has exited */

#define	VTOD(v)	((struct door_node *)(v))
#define	DTOV(d) ((struct vnode *)(d))

/*
 * Underlying 'filesystem' object definition
 */
typedef struct door_node {
	vnode_t		door_vnode;
	struct proc 	*door_target;	/* Proc handling this doors invoc's. */
	struct door_node *door_ulist;	/* Unref list */
	void		(*door_pc)();	/* Door server entry point */
	void		*door_data;	/* Cookie passed during inovcations */
	long		door_index;	/* Used as a uniquifier */
	u_short		door_flags;	/* State associated with door */
	u_short		door_active;	/* Number of active invocations */
} door_node_t;


/* Test if a door has been revoked */
#define	DOOR_INVALID(dp)	((dp)->door_flags & DOOR_REVOKED)

struct file;
int		door_create(void (*pc_cookie)(), void *data_cookie, u_int);
int		door_revoke(int);
int		door_insert(struct file *);
int		door_attributes(struct file *);
int		door_info(int, struct door_info *);
int		door_cred(struct door_cred *);
void		door_fd_close(door_desc_t *, int, u_int);
void		door_fp_close(struct file **, int);
void		door_slam(void);
void		door_deliver_unref(door_node_t *);
longlong_t	door_call(int, caddr_t, u_int, u_int, u_int);
longlong_t	door_return(caddr_t, u_int, u_int, u_int, caddr_t);
longlong_t	door_server_dispatch(struct door_data *, door_node_t *,
					caddr_t, int *);

/*
 * I/F to door invocation arguments
 */
struct regs;
void	dr_set_buf_size(struct regs *, u_int);
void	dr_set_actual_size(struct regs *, u_int);
void	dr_set_actual_ndid(struct regs *, u_int);

extern kmutex_t door_knob;
extern kcondvar_t door_cv;
#endif	/* defined(_KERNEL) */
#endif	/* !defined(_ASM) */

/*
 * System call subcodes
 */
#define	DOOR_CREATE	0
#define	DOOR_REVOKE	1
#define	DOOR_INFO	2
#define	DOOR_CALL	3
#define	DOOR_RETURN	4
#define	DOOR_CRED	5

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DOOR_H */
