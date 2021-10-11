/*
 * Copyright (c) 1986 - 1993 by Sun Microsystems, Inc.
 */

#ifndef _RPC_SVC_MT_H
#define	_RPC_SVC_MT_H

#pragma ident	"@(#)svc_mt.h	1.1	93/11/08 SMI"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SVC flags
 */
#define	SVC_VERSQUIET	0x0001	/* keep quiet about version mismatch */
#define	SVC_DEFUNCT	0x0002	/* xprt is defunct, release asap */
#define	SVC_DGRAM	0x0004	/* datagram type */
#define	SVC_RENDEZVOUS	0x0008	/* rendezvous */
#define	SVC_CONNECTION	0x000c	/* connection */
#define	SVC_TYPE_MASK	0x000c	/* type mask */
#define	SVC_FAILED	0x0010	/* send/receive failed, used for VC */
#define	SVC_ARGS_CHECK	0x0020	/* flag to check for argument completion */

#define	svc_flags(xprt)		(SVCEXT(xprt)->flags)
#define	version_keepquiet(xprt)	(svc_flags(xprt) & SVC_VERSQUIET)
#define	svc_defunct(xprt)	((svc_flags(xprt) & SVC_DEFUNCT) ? TRUE : FALSE)
#define	svc_failed(xprt)	((svc_flags(xprt) & SVC_FAILED) ? TRUE : FALSE)
#define	svc_type(xprt)		(svc_flags(xprt) & SVC_TYPE_MASK)
#define	svc_send_mutex(xprt)	(SVCEXT(xprt)->send_mutex)


/*
 * The xp_p3 field the the service handle points to the SVCXPRT_EXT
 * extension structure.
 */
typedef struct svcxprt_list_t {
	struct svcxprt_list_t	*next;
	SVCXPRT			*xprt;
} SVCXPRT_LIST;

typedef struct svcxprt_ext_t {
	int		flags;		/* VERSQUIET, DEFUNCT flag */
	SVCXPRT		*parent;	/* points to parent (NULL in parent) */

	struct rpc_msg	*msg;		/* message */
	struct svc_req	*req;		/* request */
	char		*cred_area;	/* auth work area */
	int		refcnt;		/* number of parent references */
	SVCXPRT_LIST	*my_xlist;	/* list header for this copy */
	mutex_t		send_mutex;	/* for sequencing sends */
} SVCXPRT_EXT;

#define	SVCEXT(xprt)	((SVCXPRT_EXT *)((xprt)->xp_p3))


/*
 * Global/module private data and functions
 */
extern SVCXPRT **svc_xports;
extern XDR **svc_xdrs;
extern int svc_mt_mode;
extern mutex_t svc_thr_mutex;
extern cond_t svc_thr_fdwait;
extern int svc_nfds;
extern int svc_nfds_set;
extern int svc_max_fd;
extern mutex_t svc_mutex;
extern mutex_t svc_exit_mutex;
extern int svc_pipe[2];
extern bool_t svc_polling;

SVCXPRT *svc_xprt_alloc();
SVCXPRT *svc_dg_xprtcopy();
SVCXPRT *svc_vc_xprtcopy();
SVCXPRT *svc_fd_xprtcopy();
SVCXPRT *svc_copy();
void svc_xprt_free();
void svc_xprt_destroy();
void svc_dg_xprtfree();
void svc_vc_xprtfree();
void svc_fd_xprtfree();
void svc_args_done();
void _svc_dg_destroy_private();
void _svc_vc_destroy_private();
void _svc_destroy_private();

#ifdef __cplusplus
}
#endif

#endif /* !_RPC_SVC_MT_H */
