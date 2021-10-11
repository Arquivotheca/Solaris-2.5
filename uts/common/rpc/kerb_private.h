/*
 * kerb_private.h, Protocol for Kerberos style authentication for RPC
 *
 * Copyright (C) 1990, Sun Microsystems, Inc.
 */

#ifndef	_RPC_KERB_PRIVATE_H
#define	_RPC_KERB_PRIVATE_H

#pragma ident	"@(#)kerb_private.h	1.8	94/06/30 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This struct is pointed to by the ah_private field of an "AUTH *"
 */
struct _ak_private {
	char ak_service[ANAME_SZ];	/* service name */
	char ak_srv_inst[INST_SZ];	/* server instance */
	char ak_realm[REALM_SZ];	/* realm */
	u_int ak_window;		/* client specified window */
	bool_t ak_dosync;		/* synchronize? */
#ifdef _KERNEL
	struct	netbuf ak_syncaddr;	/* remote host to sync with */
	struct	knetconfig ak_synconfig; /* remote host to sync with */
	int	ak_calltype;		/* use rpc or straight call for sync */
#else /* !_KERNEL */
	char	*ak_timehost;		/* remote host to sync with */
#endif /* _KERNEL */
	struct timeval ak_timediff;	/* server's time - client's time */
	u_long ak_nickname;		/* server's nickname for client */
	struct timeval ak_timestamp;	/* timestamp sent */
	struct authkerb_cred ak_cred;	/* storage for credential */
	struct authkerb_verf ak_verf;	/* storage for verifier */
	KTEXT_ST ak_ticket;		/* kerberos ticket */
};

#ifdef __STDC__
extern AUTH_STAT kerb_get_session_cred(char *, char *, u_long, KTEXT,
		authkerb_clnt_cred *);
extern AUTH_STAT kerb_error(int);
#else
extern AUTH_STAT kerb_get_session_cred();
extern AUTH_STAT kerb_error();
#endif

#ifdef _KERNEL
extern int	kerb_get_session_key(struct _ak_private *, des_block *);
/* PRINTFLIKE1 */
extern void	_kmsgout(char *fmt, ...);
#ifdef KERB_DEBUG
/* PRINTFLIKE1 */
extern void	kprint(char *fmt, ...);
#endif
extern int	krb_get_lrealm(char *, int);
extern char	*krb_get_default_realm(void);
extern int	kerb_mkcred(char *, char *, char *, u_long, KTEXT,
			des_block *, enum clnt_stat *);
extern int	kerb_rdcred(KTEXT, char *, char *, u_long, AUTH_DAT *,
			enum clnt_stat *);
extern int	kerb_getpwnam(char *, uid_t *, gid_t *, short *, int *,
			enum clnt_stat *);

extern char	*krb_err_txt[];
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* !_RPC_KERB_PRIVATE_H */
