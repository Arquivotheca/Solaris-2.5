#ident	"@(#)auth_unix.c	1.14	94/02/07 SMI"
	 /* from 1.25 90/03/30 SunOS 4.1  */

/*
 * Adapted for use by the boot program.
 *
 * auth_unix.c, Implements UNIX style authentication parameters.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * The system is very weak.  The client uses no encryption for its
 * credentials and only sends null verifiers.  The server sends backs
 * null verifiers or optionally a verifier that suggests a new short hand
 * for the credentials.
 *
 */

#include <sys/sysmacros.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/auth_unix.h>
#include <sys/promif.h>

static struct auth_ops *authunix_ops();
/*
 * This struct is pointed to by the ah_private field of an auth_handle.
 */
struct audata {
	struct opaque_auth	au_origcred;	/* original credentials */
	struct opaque_auth	au_shcred;	/* short hand cred */
	u_long			au_shfaults;	/* short hand cache faults */
	char			au_marshed[MAX_AUTH_BYTES];
	u_int			au_mpos;	/* xdr pos at end of marshed */
};
#define	AUTH_PRIVATE(auth)	((struct audata *)auth->ah_private)

static bool_t marshal_new_auth();

/*
 * Static allocation
 */
AUTH auth_buf;					/* in create */
struct audata audata_buf;			/* ditto */
char auth_base[MAX_AUTH_BYTES + sizeof (long)];	/* ditto */

/*
 * Create a unix style authenticator.
 * Returns an auth handle with the given stuff in it.
 */
AUTH *
authunix_create(char *machname, uid_t uid, gid_t gid, int len,
    gid_t *aup_gids)
{
	extern struct opaque_auth _null_auth; /* defined in rpc_prot.c */
	struct authunix_parms aup;
	XDR xdrs;
	register AUTH *auth;
	register struct audata *au;
	register char *buf = (char *)LALIGN(&auth_base[0]);

	/*
	 * set up auth handle to use STATIC storage.
	 */

	auth = &auth_buf;
	au = &audata_buf;

	/* setup authenticator. */
	auth->ah_ops = authunix_ops();
	auth->ah_private = (caddr_t)au;

	/* structure copies */
	auth->ah_verf = au->au_shcred = _null_auth;

	au->au_shfaults = 0;

	/*
	 * fill in param struct from the given params
	 */
	aup.aup_time = (prom_gettime() / 1000);
	aup.aup_machname = machname;
	aup.aup_uid = uid;
	aup.aup_gid = gid;
	aup.aup_len = (u_int)len;
	aup.aup_gids = (gid_t *)aup_gids;

	/*
	 * Serialize the parameters into origcred
	 */
	xdrmem_create(&xdrs, buf, MAX_AUTH_BYTES, XDR_ENCODE);
	if (! xdr_authunix_parms(&xdrs, &aup)) {
		printf("authunix_create:  xdr_authunix_parms failed");
		return ((AUTH *)0);
	}
	au->au_origcred.oa_length = XDR_GETPOS(&xdrs);
	au->au_origcred.oa_flavor = (u_int)AUTH_UNIX;
	au->au_origcred.oa_base = buf;


	/*
	 * set auth handle to reflect new cred.
	 */
	auth->ah_cred = au->au_origcred;
	marshal_new_auth(auth);
	return (auth);
}

/*
 * Returns an auth handle with parameters determined by doing lots of
 * syscalls.
 */
AUTH *
authunix_create_default(void)
{
	register int len;
	char machname[MAX_MACHINE_NAME + 1];
	uid_t uid;
	gid_t gid;
	gid_t gids[NGRPS];

	if (gethostname(machname, MAX_MACHINE_NAME) == -1) {
		printf("authunix_create_default:  gethostname failed");
		return (0);
	}
	machname[MAX_MACHINE_NAME] = 0;
	uid = 0;	/* Always root from the boot program. */
	gid = 1;	/* ditto */
	len = 1;	/* we don't care about the other possibilities */
	gids[0] = 1;
	return (authunix_create(machname, uid, gid, len, &gids[0]));
}

/*
 * authunix operations
 */

/* ARGSUSED */
static void
authunix_nextverf(AUTH *auth)
{
}

/* ARGSUSED */
static bool_t
authunix_marshal(AUTH *auth, XDR *xdrs, cred_t *cr)
{
	register struct audata *au = AUTH_PRIVATE(auth);


	return (XDR_PUTBYTES(xdrs, au->au_marshed, au->au_mpos));
}

static bool_t
authunix_validate(AUTH *auth, struct opaque_auth *verf)
{
	register struct audata *au;
	XDR xdrs;

	if (verf->oa_flavor == AUTH_SHORT) {
		au = AUTH_PRIVATE(auth);


		xdrmem_create(&xdrs, verf->oa_base, verf->oa_length,
		    XDR_DECODE);

		if (xdr_opaque_auth(&xdrs, &au->au_shcred)) {
			auth->ah_cred = au->au_shcred;
		} else {
			xdrs.x_op = XDR_FREE;
			(void) xdr_opaque_auth(&xdrs, &au->au_shcred);
			au->au_shcred.oa_base = 0;
			auth->ah_cred = au->au_origcred;
		}
		marshal_new_auth(auth);
	}

	return (TRUE);
}

static bool_t
authunix_refresh(AUTH *auth, cred_t *cr)
{
	register struct audata *au = AUTH_PRIVATE(auth);
	struct authunix_parms aup;
	XDR xdrs;
	register int stat;


	if (auth->ah_cred.oa_base == au->au_origcred.oa_base) {
		/* there is no hope.  Punt */
		return (FALSE);
	}
	au->au_shfaults ++;

	/* first deserialize the creds back into a struct authunix_parms */
	aup.aup_machname = (char *)0;
	aup.aup_gids = (gid_t *)0;
	xdrmem_create(&xdrs, au->au_origcred.oa_base,
			au->au_origcred.oa_length, XDR_DECODE);
	stat = xdr_authunix_parms(&xdrs, &aup);
	if (! stat)
		goto done;

	/* update the time and serialize in place */
	aup.aup_time = (prom_gettime() / 1000);
	xdrs.x_op = XDR_ENCODE;
	XDR_SETPOS(&xdrs, 0);
	stat = xdr_authunix_parms(&xdrs, &aup);
	if (! stat)
		goto done;
	auth->ah_cred = au->au_origcred;
	marshal_new_auth(auth);
done:
	/* free the struct authunix_parms created by deserializing */
	xdrs.x_op = XDR_FREE;
	(void) xdr_authunix_parms(&xdrs, &aup);
	XDR_DESTROY(&xdrs);
	return (stat);
}

static void
authunix_destroy(AUTH *auth)
{
	extern void bzero();
	register struct audata *au = AUTH_PRIVATE(auth);


	/* simply bzero, the buffers are static. */
	bzero(au->au_origcred.oa_base, au->au_origcred.oa_length);
	bzero(auth->ah_private, sizeof (struct audata));
	bzero((caddr_t)auth, sizeof (*auth));
}

/*
 * Marshals (pre-serializes) an auth struct.
 * sets private data, au_marshed and au_mpos
 */
static bool_t
marshal_new_auth(AUTH *auth)
{
	XDR		xdr_stream;
	register XDR	*xdrs = &xdr_stream;
	register struct audata *au = AUTH_PRIVATE(auth);

	xdrmem_create(xdrs, au->au_marshed, MAX_AUTH_BYTES, XDR_ENCODE);
	if ((! xdr_opaque_auth(xdrs, &(auth->ah_cred))) ||
	    (! xdr_opaque_auth(xdrs, &(auth->ah_verf)))) {
		printf("marshal_new_auth - Fatal marshalling problem");
	} else {
		au->au_mpos = XDR_GETPOS(xdrs);
	}
	XDR_DESTROY(xdrs);
}


static struct auth_ops *
authunix_ops(void)
{
	static struct auth_ops ops;

	if (ops.ah_nextverf == 0) {
		ops.ah_nextverf = authunix_nextverf;
		ops.ah_marshal = authunix_marshal;
		ops.ah_validate = authunix_validate;
		ops.ah_refresh = authunix_refresh;
		ops.ah_destroy = authunix_destroy;
	}
	return (&ops);
}

/*
 * XDR for unix authentication parameters.
 */
bool_t
xdr_authunix_parms(XDR *xdrs, struct authunix_parms *p)
{

	if (xdr_u_long(xdrs, &(p->aup_time)) &&
	    xdr_string(xdrs, &(p->aup_machname), MAX_MACHINE_NAME) &&
	    xdr_int(xdrs, (int *)&(p->aup_uid)) &&
	    xdr_int(xdrs, (int *)&(p->aup_gid)) &&
	    xdr_array(xdrs, (caddr_t *)&(p->aup_gids),
		    &(p->aup_len), NGRPS, sizeof (int), xdr_int)) {
		return (TRUE);
	}
	return (FALSE);
}
