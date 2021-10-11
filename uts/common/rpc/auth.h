/*
 * Copyright (c) 1986 - 1991 by Sun Microsystems, Inc.
 */

/*
 * auth.h, Authentication interface.
 *
 * The data structures are completely opaque to the client. The client
 * is required to pass a AUTH * to routines that create rpc
 * "sessions".
 */

#ifndef	_RPC_AUTH_H
#define	_RPC_AUTH_H

#pragma ident	"@(#)auth.h	1.27	94/08/09 SMI"

#include <rpc/xdr.h>
#include <sys/cred.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	MAX_AUTH_BYTES	400
#define	MAXNETNAMELEN	255	/* maximum length of network user's name */

/*
 * Status returned from authentication check
 */
enum auth_stat {
	AUTH_OK = 0,
	/*
	 * failed at remote end
	 */
	AUTH_BADCRED = 1,		/* bogus credentials (seal broken) */
	AUTH_REJECTEDCRED = 2,		/* client should begin new session */
	AUTH_BADVERF = 3,		/* bogus verifier (seal broken) */
	AUTH_REJECTEDVERF = 4,		/* verifier expired or was replayed */
	AUTH_TOOWEAK = 5,		/* rejected due to security reasons */
	/*
	 * failed locally
	*/
	AUTH_INVALIDRESP = 6,		/* bogus response verifier */
	AUTH_FAILED = 7,			/* some unknown reason */
	/*
	 * kerberos errors
	 */
	AUTH_KERB_GENERIC = 8,		/* kerberos generic error */
	AUTH_TIMEEXPIRE = 9,		/* time of credential expired */
	AUTH_TKT_FILE = 10,		/* something wrong with ticket file */
	AUTH_DECODE = 11,		/* can't decode authenticator */
	AUTH_NET_ADDR = 12		/* wrong net address in ticket */
};
typedef enum auth_stat AUTH_STAT;

/*
 * The following assumes a 32 bit operating system where an unsigned long
 * is 32 bits.  That is the case with Solaris.  Should this change in the
 * future, this following will need attention.  However, even in the planned
 * world of 64 bit machines a long is expected to remain 32 bits.
 */
union des_block {
	struct  {
		u_long high;
		u_long low;
	} key;
	char c[8];
};
typedef union des_block des_block;

#ifdef __STDC__
extern bool_t xdr_des_block(XDR *, des_block *);
#else
extern bool_t xdr_des_block();
#endif


/*
 * Authentication info. Opaque to client.
 */
struct opaque_auth {
	enum_t	oa_flavor;		/* flavor of auth */
	caddr_t	oa_base;		/* address of more auth stuff */
	u_int	oa_length;		/* not to exceed MAX_AUTH_BYTES */
};


/*
 * Auth handle, interface to client side authenticators.
 */
typedef struct __auth {
	struct	opaque_auth	ah_cred;
	struct	opaque_auth	ah_verf;
	union	des_block	ah_key;
	struct auth_ops {
#ifdef __STDC__
		void	(*ah_nextverf)(struct __auth *);
#ifdef _KERNEL
		int	(*ah_marshal)(struct __auth *, XDR *, struct cred *);
#else
		int	(*ah_marshal)(struct __auth *, XDR *);
#endif
		/* nextverf & serialize */
		int	(*ah_validate)(struct __auth *,
					struct opaque_auth *);
		/* validate varifier */
#ifdef _KERNEL
		int	(*ah_refresh)(struct __auth *, cred_t *);
#else
		int	(*ah_refresh)(struct __auth *);
		/* refresh credentials */
#endif
		void	(*ah_destroy)(struct __auth *);
		/* destroy this structure */
#else
		void	(*ah_nextverf)();
		int	(*ah_marshal)();	/* nextverf & serialize */
		int	(*ah_validate)();	/* validate varifier */
		int	(*ah_refresh)();	/* refresh credentials */
		void	(*ah_destroy)();	/* destroy this structure */
#endif
	} *ah_ops;
	caddr_t ah_private;
} AUTH;


/*
 * Authentication ops.
 * The ops and the auth handle provide the interface to the authenticators.
 *
 * AUTH	*auth;
 * XDR	*xdrs;
 * struct opaque_auth verf;
 */
#define	AUTH_NEXTVERF(auth)		\
		((*((auth)->ah_ops->ah_nextverf))(auth))
#define	auth_nextverf(auth)		\
		((*((auth)->ah_ops->ah_nextverf))(auth))


#ifdef _KERNEL
#define	AUTH_MARSHALL(auth, xdrs, cred)	\
		((*((auth)->ah_ops->ah_marshal))(auth, xdrs, cred))
#define	auth_marshall(auth, xdrs, cred)	\
		((*((auth)->ah_ops->ah_marshal))(auth, xdrs, cred))
#else
#define	AUTH_MARSHALL(auth, xdrs)	\
		((*((auth)->ah_ops->ah_marshal))(auth, xdrs))
#define	auth_marshall(auth, xdrs)	\
		((*((auth)->ah_ops->ah_marshal))(auth, xdrs))
#endif


#define	AUTH_VALIDATE(auth, verfp)	\
		((*((auth)->ah_ops->ah_validate))((auth), verfp))
#define	auth_validate(auth, verfp)	\
		((*((auth)->ah_ops->ah_validate))((auth), verfp))

#ifdef _KERNEL
#define	AUTH_REFRESH(auth, cr)		\
		((*((auth)->ah_ops->ah_refresh))(auth, cr))
#define	auth_refresh(auth, cr)		\
		((*((auth)->ah_ops->ah_refresh))(auth, cr))
#else
#define	AUTH_REFRESH(auth)		\
		((*((auth)->ah_ops->ah_refresh))(auth))
#define	auth_refresh(auth)		\
		((*((auth)->ah_ops->ah_refresh))(auth))
#endif

#define	AUTH_DESTROY(auth)		\
		((*((auth)->ah_ops->ah_destroy))(auth))
#define	auth_destroy(auth)		\
		((*((auth)->ah_ops->ah_destroy))(auth))


extern struct opaque_auth _null_auth;


/*
 * These are the various implementations of client side authenticators.
 */

/*
 * System style authentication
 * AUTH *authsys_create(machname, uid, gid, len, aup_gids)
 *	const char *machname;
 *	const uid_t uid;
 *	const gid_t gid;
 *	const int len;
 *	const gid_t *aup_gids;
 */
#ifdef _KERNEL
extern AUTH *authkern_create();		/* takes no parameters */
#else
#ifdef  __STDC__
extern AUTH *authsys_create(const char *, const uid_t, const gid_t, const int,
const gid_t *);
extern AUTH *authsys_create_default(void);	/* takes no parameters */
extern AUTH *authnone_create(void);		/* takes no parameters */
#else
extern AUTH *authsys_create();
extern AUTH *authsys_create_default();	/* takes no parameters */
extern AUTH *authnone_create();	/* takes no parameters */
#endif
/* Will get obsolete in near future */
#define	authunix_create		authsys_create
#define	authunix_create_default authsys_create_default

#endif

/*
 * DES style authentication
 * AUTH *authdes_seccreate(servername, window, timehost, ckey)
 *	const char *servername;		- network name of server
 *	const u_int window;			- time to live
 *	const char *timehost;			- optional hostname to sync with
 *	const des_block *ckey;		- optional conversation key to use
 */
/* Will get obsolete in near future */
#ifdef _KERNEL
extern int authdes_create();
#else
#ifdef __STDC__
extern AUTH *authdes_seccreate(const char *, const u_int, const  char *,
const  des_block *);
#else
extern AUTH *authdes_seccreate();
#endif
#endif

/*
 *  Netname manipulating functions
 *
 */

#ifdef	_KERNEL
#ifdef __STDC__
extern enum clnt_stat netname2user(char *, uid_t *, gid_t *, int *, int *);
#else
extern enum clnt_stat netname2user();
#endif
#endif
#ifdef __STDC__
extern int getnetname(char *);
extern int host2netname(char *, const char *, const char *);
extern int user2netname(char *, const uid_t, const char *);
#ifndef	_KERNEL
extern int netname2user(const char *, uid_t *, gid_t *, int *, gid_t *);
#endif
extern int netname2host(const char *, char *, const int);
#else
extern int getnetname();
extern int host2netname();
extern int user2netname();
extern int netname2host();
#endif

/*
 *
 * These routines interface to the keyserv daemon
 *
 */

#ifdef	_KERNEL
extern enum clnt_stat key_decryptsession();
extern enum clnt_stat key_encryptsession();
extern enum clnt_stat key_gendes();
#endif

#ifdef  __STDC__
#ifndef _KERNEL
extern int key_decryptsession(const char *, des_block *);
extern int key_encryptsession(const char *, des_block *);
extern int key_gendes(des_block *);
extern int key_setsecret(const char *);
extern int key_secretkey_is_set(void);
#endif
#else
#ifndef _KERNEL
extern int key_decryptsession();
extern int key_encryptsession();
extern int key_gendes();
extern int key_setsecret();
extern int key_secretkey_is_set();
#endif
#endif


/*
 * Kerberos style authentication
 * AUTH *authkerb_seccreate(service, srv_inst, realm, window, timehost, status)
 *	const char *service;			- service name
 *	const char *srv_inst;			- server instance
 *	const char *realm;			- server realm
 *	const u_int window;			- time to live
 *	const char *timehost;			- optional hostname to sync with
 *	int *status;			- kerberos status returned
 */
#ifdef _KERNEL
extern int authkerb_create();
#else
#ifdef __STDC__
extern AUTH *authkerb_seccreate(const char *, const char *, const  char *,
const u_int, const char *, int *);
#else
extern AUTH *authkerb_seccreate();
#endif
#endif /* _KERNEL */

/*
 * Map a kerberos credential into a unix cred.
 *
 *  authkerb_getucred(rqst, uid, gid, grouplen, groups)
 *	const struct svc_req *rqst;		- request pointer
 *	uid_t *uid;
 *	gid_t *gid;
 *	short *grouplen;
 *	int   *groups;
 *
 */
#ifdef __STDC__
extern int	authkerb_getucred(/* struct svc_req *, uid_t *, gid_t *,
			short *, int * */);
#else
extern int authkerb_getucred();
#endif

#ifdef _KERNEL
/*
 * XDR an opaque authentication struct.  See auth.h.
 */
#ifdef __STDC__
extern bool_t	xdr_opaque_auth(XDR *, struct opaque_auth *);
#else
extern bool_t	xdr_opaque_auth();
#endif
#endif

#ifdef _KERNEL
struct svc_req;
struct rpc_msg;
extern enum auth_stat _authenticate(struct svc_req *, struct rpc_msg *);
#endif

#define	AUTH_NONE	0		/* no authentication */
#define	AUTH_NULL	0		/* backward compatibility */
#define	AUTH_SYS	1		/* unix style (uid, gids) */
#define	AUTH_UNIX	AUTH_SYS
#define	AUTH_SHORT	2		/* short hand unix style */
#define	AUTH_DES	3		/* des style (encrypted timestamps) */
#define	AUTH_KERB	4		/* kerberos style */

#ifdef _KERNEL
extern char	loopback_name[];
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* !_RPC_AUTH_H */
