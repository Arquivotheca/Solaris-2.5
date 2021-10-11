/*
 * auth_kerb.c, client-side implementation of KERBEROS authentication
 *
 * Copyright (C) 1987, Sun Microsystems, Inc.
 */

#ident	"@(#)auth_kerb.c	1.13	95/01/26 SMI"

/* Force SUNDES_CRYPT for the kernel */
#ifndef SUNDES_CRYPT
#define	SUNDES_CRYPT
#endif /* !SUNDES_CRYPT */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/tiuser.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/proc.h>
#include <sys/session.h>

#ifdef SUNDES_CRYPT
#include <rpc/des_crypt.h>
#endif /* SUNDES_CRYPT */
#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/clnt.h>
#include <rpc/auth_kerb.h>
#include <rpc/kerb_private.h>
#include <rpc/auth_des.h>
#include <rpc/xdr.h>

#define	MILLION		1000000L
#define	RTIME_TIMEOUT	5		/* seconds to wait for sync */

#define	AUTH_PRIVATE(auth)	(struct _ak_private *) auth->ah_private
#define	ALLOC(object_type)	(object_type *) mem_alloc(sizeof (object_type))
#define	FREE(ptr, size)		mem_free((char *)(ptr), (int) size)
#define	ATTEMPT(xdr_op)		if (!(xdr_op))\
					return (FALSE)

#define	gettimeofday(tvp, tzp)	uniqtime(tvp)

static void	authkerb_nextverf(AUTH *);
static bool_t	authkerb_marshal(AUTH *, XDR *, struct cred *);
static bool_t	authkerb_validate(AUTH *, struct opaque_auth *);
static bool_t	authkerb_refresh(AUTH *, cred_t *);
static void	authkerb_destroy(AUTH *);
static bool_t	synchronize(struct knetconfig *, struct netbuf *,
			int, struct timeval *);
static struct auth_ops *authkerb_ops(void);
static int Kerr;

/*
 * Create the client kerberos authentication object
 *
 *	service		server principal: name of service
 *	srv_inst	server principal: instance (generally hostname)
 *	realm		server principal: realm
 *	window
 *	syncaddr
 *	status		if non-null, returns the kerberos result
 *	retauth		returned AUTH handle
 *
 * return value is system errno
 */

#define	SETSTATUS(x) if (status) *status = (x)

int
authkerb_create(char *service, char *srv_inst, char *realm, u_int window,
	struct netbuf *syncaddr, int *status, struct knetconfig *synconfig,
	int calltype, AUTH **retauth)
{
	AUTH *auth;
	struct _ak_private *ak;
	int retval;
	int error = 0;

	if (retauth == NULL)
		return (EINVAL);
	*retauth = NULL;

	/*
	 * Allocate everything now
	 */
	auth = ALLOC(AUTH);
	ak = ALLOC(struct _ak_private);
	bzero((char *)ak, sizeof (*ak));
	SETSTATUS(KSUCCESS);

	if (auth == NULL || ak == NULL) {
		cmn_err(CE_NOTE, "authkerb_create: out of memory");
		error = ENOMEM;
		goto failed;
	}
	strncpy(ak->ak_service, service, ANAME_SZ);

	/* if no instance specified, make it the NULL instance */
	if (srv_inst == (char *) NULL)
		ak->ak_srv_inst[0] = '\0';
	else
		strncpy(ak->ak_srv_inst, srv_inst, INST_SZ);

	/* get current realm if not passed in */
	if (realm == (char *) NULL) {
		retval = krb_get_lrealm(ak->ak_realm, 1); /* return error */
		if (retval != KSUCCESS) {
			_kmsgout("get_realm: kerberos error %d (%s)",
				retval,
				retval > 0 ? krb_err_txt[retval]
					    : "system error");
			SETSTATUS(retval);
			goto failed;
		}
	}
	else
		strncpy(ak->ak_realm, realm, REALM_SZ);

	/*
	 * Set up private data
	 */
	if (syncaddr != NULL) {
		ak->ak_syncaddr = *syncaddr;
		ak->ak_synconfig = *synconfig;
		ak->ak_dosync = TRUE;
		ak->ak_calltype = calltype;
	} else {
		ak->ak_timediff.tv_sec = 0;
		ak->ak_timediff.tv_usec = 0;
		ak->ak_dosync = FALSE;
	}
	ak->ak_window = window;

	/*
	 * Set up auth handle
	 */
	auth->ah_cred.oa_flavor = AUTH_KERB;
	auth->ah_verf.oa_flavor = AUTH_KERB;
	auth->ah_ops = authkerb_ops();
	auth->ah_private = (caddr_t)ak;

	/* initialize the ticket and session key */
	if (!authkerb_refresh(auth, CRED())) {
		SETSTATUS(Kerr);
		goto failed;
	}
	*retauth = auth;
	return (0);

failed:
	if (auth != NULL)
		FREE(auth, sizeof (AUTH));
	if (ak != NULL)
		FREE(ak, sizeof (struct _ak_private));
	if (status && (*status == KSUCCESS))
		*status = KFAILURE;
	return ((error == 0) ? EIO : error);		/* XXX */
}
#undef SETSTATUS

/*
 * Implement the five authentication operations
 */

/*
 * 1. Next Verifier
 */
/* ARGSUSED */
static void
authkerb_nextverf(AUTH *auth)
{

#ifdef KERB_DEBUG
	struct _ak_private *ak = AUTH_PRIVATE(auth);
	kprint("authkerb_nextverf: auth %x service %s inst %s\n",
		auth, ak->ak_service, ak->ak_srv_inst);
#endif /* KERB_DEBUG */
	/* no action required */
}

/*
 * 2. Marshal
 */
/* ARGSUSED */
static bool_t
authkerb_marshal(AUTH *auth, XDR *xdrs, struct cred *cr)
{
	struct _ak_private *ak = AUTH_PRIVATE(auth);
	struct authkerb_cred *cred = &ak->ak_cred;
	struct authkerb_verf *verf = &ak->ak_verf;
	des_block cryptbuf[2];
#ifdef SUNDES_CRYPT
	int status;
	des_block ivec;
#else /* !SUNDES_CRYPT */
	des_key_schedule key_s;
#endif /* SUNDES_CRYPT */
	int len;
	register long *ixdr;

	/*
	 * Figure out the "time", accounting for any time difference
	 * with the server if necessary.
	 */
	(void) gettimeofday(&ak->ak_timestamp, (struct timezone *)NULL);
#ifdef KERB_DEBUG
	if (cred->akc_namekind == AKN_FULLNAME) {
	    if (ak->ak_timediff.tv_sec || ak->ak_timediff.tv_usec)
		kprint("authkerb_marshal: timediff %d sec %d usec\n",
			ak->ak_timediff.tv_sec, ak->ak_timediff.tv_usec);
	}
#endif /* KERB_DEBUG */
	ak->ak_timestamp.tv_sec += ak->ak_timediff.tv_sec;
	ak->ak_timestamp.tv_usec += ak->ak_timediff.tv_usec;
	if (ak->ak_timestamp.tv_usec >= MILLION) {
		ak->ak_timestamp.tv_usec -= MILLION;
		ak->ak_timestamp.tv_sec += 1;
	}

	/*
	 * XDR the timestamp and possibly some other things, then
	 * encrypt them.
	 */
	ixdr = (long *)cryptbuf;
	IXDR_PUT_LONG(ixdr, ak->ak_timestamp.tv_sec);
	IXDR_PUT_LONG(ixdr, ak->ak_timestamp.tv_usec);
#ifndef NOENCRYPTION
	if (cred->akc_namekind == AKN_FULLNAME) {
#ifdef KERB_DEBUG
		kprint("authkerb_marshal: FULLNAME svc %s inst %s\n",
			ak->ak_service, ak->ak_srv_inst);
#endif /* KERB_DEBUG */
		IXDR_PUT_U_LONG(ixdr, ak->ak_window);
		IXDR_PUT_U_LONG(ixdr, ak->ak_window - 1);
#ifdef SUNDES_CRYPT
#ifdef DEBUG_CRYPT
		printf("  -->fullname: sun crypt\n");
#endif /* DEBUG_CRYPT */
		ivec = auth->ah_key;
		status = cbc_crypt((char *)&auth->ah_key, (char *)cryptbuf,
			2*sizeof (des_block), DES_ENCRYPT,
			(char *)&ivec);
#else /* !SUNDES_CRYPT */
		/* XXXXX use pcbc_encrypt */
		/* encrypt */
#ifdef DEBUG_CRYPT
		printf("  -->fullname: kerberos crypt\n");
#endif /* DEBUG_CRYPT */
		(void) key_sched((des_cblock *)&auth->ah_key, key_s);
		(void) cbc_encrypt(cryptbuf, cryptbuf, 2*sizeof (des_block),
			key_s, (des_cblock *)&auth->ah_key, 1);
		/* clean up */
		bzero((char *) key_s, sizeof (key_s));
#endif /* SUNDES_CRYPT */
	} else {
#ifdef SUNDES_CRYPT
#ifdef DEBUG_CRYPT
		printf("  -->nickname: sun crypt\n");
#endif /* DEBUG_CRYPT */
		status = ecb_crypt((char *)&auth->ah_key, (char *)cryptbuf,
			sizeof (des_block), DES_ENCRYPT);
#else /* !SUNDES_CRYPT */
		/* encrypt */
#ifdef DEBUG_CRYPT
		printf("  -->nickname: kerberos crypt\n");
#endif /* DEBUG_CRYPT */
		(void) key_sched((des_cblock *)&auth->ah_key, key_s);
		(void) des_ecb_encrypt(cryptbuf, cryptbuf, key_s, 1);
		/* clean up */
		bzero((char *) key_s, sizeof (key_s));
#endif /* SUNDES_CRYPT */
	}
#ifdef SUNDES_CRYPT
	if (DES_FAILED(status)) {
		_kmsgout("authkerb_marshal: DES encryption failure");
		return (FALSE);
	}
#endif /* SUNDES_CRYPT */

	verf->akv_xtimestamp = cryptbuf[0];
	if (cred->akc_namekind == AKN_FULLNAME) {
		cred->akc_fullname.window = cryptbuf[1].key.high;
		verf->akv_winverf = cryptbuf[1].key.low;
	} else {
		cred->akc_nickname = ak->ak_nickname;
		verf->akv_winverf = 0;
	}

#else /* NOENCRYPTION */
	if (cred->akc_namekind == AKN_FULLNAME) {
#ifdef KERB_DEBUG
		kprint("authkerb_marshal: FULLNAME [nocrypt] svc %s inst %s\n",
			ak->ak_service, ak->ak_srv_inst);
#endif /* KERB_DEBUG */
		IXDR_PUT_U_LONG(ixdr, ak->ak_window);
		IXDR_PUT_U_LONG(ixdr, ak->ak_window - 1);
	} else {
#ifdef KERB_DEBUG
		kprint("\n authkerb_marshal: NOT FULLNAME \n");
#endif /* KERB_DEBUG */
	}

	verf->akv_xtimestamp.key.high = ak->ak_timestamp.tv_sec;
	verf->akv_xtimestamp.key.low = ak->ak_timestamp.tv_usec;
	if (cred->akc_namekind == AKN_FULLNAME) {
		cred->akc_fullname.window = 60;
		verf->akv_winverf = 59;
	} else {
		cred->akc_nickname = ak->ak_nickname;
		verf->akv_winverf = 0;
	}
#endif /* NOENCRYPTION */

	/*
	 * Serialize the credential and verifier into opaque
	 * authentication data.
	 */
	if (cred->akc_namekind == AKN_FULLNAME) {
		len = ((1 + 1 +
			((cred->akc_fullname.ticket.length + 3) /4)
			    +  1) * BYTES_PER_XDR_UNIT);
	} else {
		len = (1 + 1)*BYTES_PER_XDR_UNIT;
	}

	if (ixdr = xdr_inline(xdrs, 2*BYTES_PER_XDR_UNIT)) {
		IXDR_PUT_LONG(ixdr, AUTH_KERB);
		IXDR_PUT_LONG(ixdr, len);
	} else {
		ATTEMPT(xdr_putlong(xdrs, (long *)&auth->ah_cred.oa_flavor));
		ATTEMPT(xdr_putlong(xdrs, (long *)&len));
	}

	ATTEMPT(xdr_authkerb_cred(xdrs, cred));

	len = (2 + 1)*BYTES_PER_XDR_UNIT;
	if (ixdr = xdr_inline(xdrs, 2*BYTES_PER_XDR_UNIT)) {
		IXDR_PUT_LONG(ixdr, AUTH_KERB);
		IXDR_PUT_LONG(ixdr, len);
	} else {
		ATTEMPT(xdr_putlong(xdrs, (long *)&auth->ah_verf.oa_flavor));
		ATTEMPT(xdr_putlong(xdrs, (long *)&len));
	}
	ATTEMPT(xdr_authkerb_verf(xdrs, verf));
#ifdef KERB_DEBUG
	if (cred->akc_namekind == AKN_FULLNAME)
		kprint(" authkerb_marshal ret ok\n");
#endif /* KERB_DEBUG */
	return (TRUE);
}

/*
 * 3. Validate
 */
static bool_t
authkerb_validate(AUTH *auth, struct opaque_auth *rverf)
{
	struct _ak_private *ak = AUTH_PRIVATE(auth);
	struct authkerb_verf verf;
#ifdef SUNDES_CRYPT
	int status;
#else /* !SUNDES_CRYPT */
	des_key_schedule key_s;
#endif /* SUNDES_CRYPT */
	register u_long *ixdr;

	if (rverf->oa_length != (2 + 1) * BYTES_PER_XDR_UNIT) {
#ifdef KERB_DEBUG
		kprint("authkerb_validate: oa_length %d != %d\n",
			rverf->oa_length, (2 + 1) * BYTES_PER_XDR_UNIT);
#endif /* KERB_DEBUG */
		return (FALSE);
	}
	ixdr = (u_long *)rverf->oa_base;
	verf.akv_xtimestamp.key.high = (u_long)*ixdr++;
	verf.akv_xtimestamp.key.low = (u_long)*ixdr++;
	verf.akv_nickname = IXDR_GET_U_LONG(ixdr);

	/*
	 * Decrypt the timestamp
	 */
#ifndef NOENCRYPTION
#ifdef SUNDES_CRYPT
#ifdef DEBUG_CRYPT
	printf("  -->verifier: sun crypt\n");
#endif /* DEBUG_CRYPT */
	status = ecb_crypt((char *)&auth->ah_key, (char *)&verf.akv_xtimestamp,
		sizeof (des_block), DES_DECRYPT);

	if (DES_FAILED(status)) {
		_kmsgout("authkerb_validate: DES decryption failure: %d",
			status);
		return (FALSE);
	}
#else /* !SUNDES_CRYPT */
	/* decrypt */
#ifdef DEBUG_CRYPT
	printf("  -->verifier: kerberos crypt\n");
#endif /* DEBUG_CRYPT */
	(void) ey_sched((des_cblock *)&auth->ah_key, key_s);
	(void) es_ecb_encrypt((des_cblock *)&verf.akv_xtimestamp,
			    (des_cblock *)&verf.akv_xtimestamp, key_s, 0);
	/* clean up */
	bzero((char *) key_s, sizeof (key_s));
#endif /* SUNDES_CRYPT */
#endif /* NO ENCRYPTION */

	/*
	 * xdr the decrypted timestamp
	 */
	ixdr = (u_long *)verf.akv_xtimestamp.c;
	verf.akv_timestamp.tv_sec = IXDR_GET_LONG(ixdr) + 1;
	verf.akv_timestamp.tv_usec = IXDR_GET_LONG(ixdr);

#ifdef KERB_DEBUG
	kprint("verifier received: %d %d\n",
		verf.akv_timestamp.tv_sec - 1,
		verf.akv_timestamp.tv_usec);
#endif /* KERB_DEBUG */
	/*
	 * validate
	 */
	if (bcmp((char *)&ak->ak_timestamp, (char *)&verf.akv_timestamp,
	    sizeof (struct timeval)) != 0) {
		_kmsgout("authkerb_validate: verifier mismatch");
		return (FALSE);
	}

	/*
	 * We have a nickname now, let's use it
	 */
	ak->ak_nickname = verf.akv_nickname;
	ak->ak_cred.akc_namekind = AKN_NICKNAME;
	return (TRUE);
}

/*
 * 4. Refresh
 *
 *  Copy the current ticket from the private area (where it was put
 *  by kerb_get_session_key) into the raw cred structure where it
 *  is used over the wire.
 *
 *  cr is added here to match the interface changes for authdes_refresh.
 *  it is not being used in this routine.
 */
/* ARGSUSED */
static bool_t
authkerb_refresh(AUTH *auth, cred_t *cr)
{
	struct _ak_private *ak = AUTH_PRIVATE(auth);
	struct authkerb_cred *cred = &ak->ak_cred;

	if (ak->ak_dosync &&
			!synchronize(&ak->ak_synconfig, &ak->ak_syncaddr,
					ak->ak_calltype, &ak->ak_timediff)) {
		/*
		 * Hope the clocks are synced!
		 */
		timerclear(&ak->ak_timediff);
		_kmsgout("authkerb_refresh: unable to synchronize with server");
	}
#ifdef KERB_DEBUG
	kprint("authkerb_refresh...FULLNAME\n");
#endif /* KERB_DEBUG */

	/*
	 * get the session key.  also gets a valid ticket and stores in
	 * ak->ak_ticket.
	 */
	if ((Kerr = kerb_get_session_key(ak, &auth->ah_key)) != KSUCCESS) {
		/* return kerberos error */
		char *errtxt = (Kerr > 0 ? krb_err_txt[Kerr] : "system error");
		_kmsgout("authkerb_refresh: no key: error = %d (%s)",
			Kerr, errtxt);
		/* also notify user directly */
		if (curproc->p_sessp->s_vp)
			uprintf("Can't get kerberos key: %s\n", errtxt);
		return (FALSE);
	}

	bcopy((char *)&ak->ak_ticket, (char *)&cred->akc_fullname.ticket,
		sizeof (KTEXT_ST));
	if (ak->ak_ticket.length > MAX_AUTH_BYTES) {
		_kmsgout(
"authkerb_refresh: warning: ticket length (%d) > MAX_AUTH_BYTES (%d)",
			ak->ak_ticket.length, MAX_AUTH_BYTES);
	}
	cred->akc_namekind = AKN_FULLNAME;
	return (TRUE);
}

/*
 * 5. Destroy
 */
static void
authkerb_destroy(AUTH *auth)
{
	struct _ak_private *ak = AUTH_PRIVATE(auth);

	FREE(ak, sizeof (struct _ak_private));
	FREE(auth, sizeof (AUTH));
}

/*
 * Synchronize with the server at the given address, that is,
 * adjust timep to reflect the delta between our clocks
 */
static bool_t
synchronize(struct knetconfig *synconfig, struct netbuf *syncaddr, int calltype,
	struct timeval *timep)
{
	struct timeval mytime;
	struct timeval timeout;

	timeout.tv_sec = RTIME_TIMEOUT;
	timeout.tv_usec = 0;
	if (rtime(synconfig, syncaddr, calltype, timep, &timeout) < 0) {
		return (FALSE);
	}
	(void) gettimeofday(&mytime, (struct timezone *)NULL);
	timep->tv_sec -= mytime.tv_sec;
	if (mytime.tv_usec > timep->tv_usec) {
		timep->tv_sec -= 1;
		timep->tv_usec += MILLION;
	}
	timep->tv_usec -= mytime.tv_usec;
	return (TRUE);
}

static struct auth_ops *
authkerb_ops(void)
{
	static struct auth_ops ops;
	extern kmutex_t	authkerb_ops_lock;

	mutex_enter(&authkerb_ops_lock);
	if (ops.ah_nextverf == NULL) {
		ops.ah_nextverf = authkerb_nextverf;
		ops.ah_marshal = authkerb_marshal;
		ops.ah_validate = authkerb_validate;
		ops.ah_refresh = authkerb_refresh;
		ops.ah_destroy = authkerb_destroy;
	}
	mutex_exit(&authkerb_ops_lock);
	return (&ops);
}
