#ident	"@(#)svcauth_kerb.c	1.5	93/06/08 SMI"

/*
 * svcauth_kerb.c, server-side kerberos authentication
 * Copyright (C) 1987, Sun Microsystems, Inc.
 *
 * We insure for the service the following:
 * (1) The timestamp microseconds do not exceed 1 million.
 * (2) The timestamp plus the window is less than the current time.
 * (3) The timestamp is not less than the one previously
 *	seen in the current session.
 *
 * It is up to the server to determine if the window size is
 * too small .
 *
 */

/*
 *  Define SUNDES_CRYPT to use Sun DES library; otherwise use DES routines
 *  from libkrb kerberos library.  These options interoperate with each other.
 */

#ifdef SUNDES_CRYPT
#include <rpc/des_crypt.h>
#endif SUNDES_CRYPT
#include <sys/param.h>
#include <rpc/rpc.h>
#include <rpc/kerb_private.h>

#include <sys/syslog.h>

#ifdef KERB_DEBUG
#define	debug(msg)	 kprint("svcauth_kerb: %s\n", msg)
#else
#define	debug(msg)
#endif KERB_DEBUG

extern char *strcpy();

#define	USEC_PER_SEC	((u_long) 1000000L)
#define	BEFORE(t1, t2)	timercmp(t1, t2, < /* EMPTY */)

/*
 * LRU cache of conversation keys and some other useful items.
 */
/* define KERB_UCRED to include kerberos principal to unix cred map code */
#define	KERB_UCRED

#define	AUTHKERB_CACHESZ	64
struct cache_entry {
	authkerb_clnt_cred clnt_cred;
	u_int window;			/* credential lifetime window */
	struct timeval laststamp;	/* detect replays of creds */
#ifdef KERB_UCRED
	char *localcred;		/* generic local credential */
#endif KERB_UCRED
};
static struct cache_entry *_rpc_authkerb_cache = NULL; /* [AUTHKERB_CACHESZ] */
static short *authkerb_lru	/* [AUTHKERB_CACHESZ] */;

/* my information */
struct mycred {
	struct mycred *next;
	SVCXPRT *xprt;
	char name[ANAME_SZ];
	char inst[INST_SZ];
	char realm[REALM_SZ];
};
static struct mycred def_mycred = {NULL};	/* init .next to NULL */
static struct mycred *my_credp = NULL;

static void cache_init();	/* initialize the cache */
static short cache_spot();	/* find an entry in the cache */
static void cache_ref(/*short sid*/);	/* note that sid was ref'd */

enum auth_stat _svcauth_kerb();

#ifdef KERB_UCRED
static void invalidate();	/* invalidate entry in cache */
#endif KERB_UCRED

/*
 * cache statistics
 */
struct {
	u_long ncachehits;	/* times cache hit, and is not replay */
	u_long ncachereplays;	/* times cache hit, and is replay */
	u_long ncachemisses;	/* times cache missed */
} svcauthkerb_stats;

/*
 *  Register the service name, instance, and realm.
 *  Must be done before _svcauth_kerb is invoked on behalf of a request.
 *  Returns 0 for success, else -1.
 */
svc_kerb_reg(xprt, name, inst, realm)
	SVCXPRT *xprt;
	char *name;
	char *inst;
	char *realm;
{
	int retval;
	struct mycred *mcp;

	if (xprt == NULL) {
		/* store into the default struct */
		def_mycred.next = &def_mycred;		/* mark it used */
		mcp = &def_mycred;
	} else {
		mcp = (struct mycred *)mem_alloc(sizeof (struct mycred));
		if (mcp == NULL) {
			_kmsgout("svc_kerb_reg: can't mem_alloc mycred");
			return (-1);
		}
	}

	/* copy the service name */
	strncpy(mcp->name, name, ANAME_SZ);

	/* if no instance specified, make it the NULL instance */
	if ((inst == (char *)NULL) || *inst == '\0')
		mcp->inst[0] = '\0';
	else
		strncpy(mcp->inst, inst, INST_SZ);

	/* get current realm if not passed in */
	if (realm == (char *)NULL) {
		retval = krb_get_lrealm(mcp->realm, 1);
		if (retval != KSUCCESS){
			_kmsgout("get_realm: kerberos error %d (%s)",
				retval,
				retval > 0 ? krb_err_txt[retval]
					: "system error");
			mcp->realm[0] = '\0';
		}
	} else
		strncpy(mcp->realm, realm, REALM_SZ);

	mcp->xprt = xprt;
	if (xprt) {
		mcp->next = my_credp;
		my_credp  = mcp;
	}

	/*
	 * register the auth service handler with RPC
	 */
	retval = svc_auth_reg(AUTH_KERB, _svcauth_kerb);
	return (retval >= 0 ? 0 : -1);
}




/*
 * Service side authenticator for AUTH_KERB
 */
enum auth_stat
_svcauth_kerb(rqst, msg)
	register struct svc_req *rqst;
	register struct rpc_msg *msg;
{

	register long *ixdr;
	des_block cryptbuf[2];
	register struct authkerb_clnt_cred *cred;
	struct authkerb_verf verf;
#ifdef SUNDES_CRYPT
	int status;
	des_block ivec;
#else !SUNDES_CRYPT
	des_key_schedule key_s;
#endif SUNDES_CRYPT
	register struct cache_entry *entry;
	short sid;
	des_block sessionkey;
	u_int window;
	struct timeval timestamp;
	register int length;
	KTEXT_ST ticket;
	enum authkerb_namekind namekind;
	AUTH_STAT retval;
	u_long raddr = 0;
	struct sockaddr_in *sin;
	SVCXPRT *xprt = rqst->rq_xprt;
	struct mycred *mcp;

	if (_rpc_authkerb_cache == NULL) {
		cache_init();
	}

	/* we are responsible for filling in the client cred */
	cred = (struct authkerb_clnt_cred *)rqst->rq_clntcred;

	/*
	 * Get the credential
	 */
	ixdr = (long *)msg->rm_call.cb_cred.oa_base;
	namekind = IXDR_GET_ENUM(ixdr, enum authkerb_namekind);
	switch (namekind) {
	case AKN_FULLNAME:
		debug("Fullname: get ticket...");
		/*
		 *  Zero the ticket as the actual size may be less.
		 *  Ticket encoded using xdr_bytes(), which means that
		 *  there is a length, followed by actual bytes.
		 */
		memset((char *)&ticket, 0, sizeof (ticket));

		/* get the length of the ticket */
		length = IXDR_GET_LONG(ixdr);
		if (length > MAX_AUTH_BYTES) {
			/*
			 *  XXX
			 *  Sender can send up to MAX_KTXT_LEN bytes
			 *  in the ticket, but the server side is currently
			 *  constrained to MAX_AUTH_BYTES for now.
			 *  This should not be a problem in practice.
			 */
			debug("ticket length too long");
			return (AUTH_DECODE);
		}

		/* copy the ticket */
		memcpy ((char *)(ticket.dat),  ixdr, length);
		ixdr += (RNDUP(length) / BYTES_PER_XDR_UNIT);
		ticket.length = length;
		ticket.mbz = 0;

		window = (u_long)*ixdr++;	/* get the window */

		/* get the caller's address */
		/* XXX check this section out */
		sin = (struct sockaddr_in *)svc_getcaller(xprt);
		if (sin->sin_family == AF_INET)
			raddr = ntohl(sin->sin_addr.s_addr);
		break;
	case AKN_NICKNAME:
		cred->nickname = (u_long)*ixdr++;
#ifdef KERB_DEBUG
		kprint("svcauth_kerb: Nickname = %d\n", cred->nickname);
#endif KERB_DEBUG
		break;
	default:
		return (AUTH_BADCRED);
	}

	/*
	 * Get the verifier
	 */
	ixdr = (long *)msg->rm_call.cb_verf.oa_base;
	verf.akv_xtimestamp.key.high = (u_long)*ixdr++;
	verf.akv_xtimestamp.key.low =  (u_long)*ixdr++;
	verf.akv_winverf = (u_long)*ixdr++;

	/*
	 *	Fill in the creds and get the conversation key
	 */
	if (namekind == AKN_FULLNAME) {
		/*
		 *  Find the correct kerberos cred structure to use
		 *  for this transport.  If none exists, use the default.
		 */
		for (mcp = my_credp; mcp; mcp = mcp->next) {
			if (mcp->xprt == xprt) {
				/* found it */
				break;
			}
		}
		if (mcp == NULL) {
			/*
			 *  None found, so use the default.  Initialize
			 *  if necessary.
			 */
			if (def_mycred.next == NULL) {
				def_mycred.xprt	= NULL;
				def_mycred.name[0] = '\0';
				def_mycred.inst[0] = '*';
				def_mycred.inst[1] = '\0';
				def_mycred.realm[0] = '\0';
				def_mycred.next = &def_mycred;
			}
			mcp = &def_mycred;
		}
		if ((retval = kerb_get_session_cred(mcp->name, mcp->inst,
			raddr, &ticket, cred)) != AUTH_OK)
		{
			debug("kerb_get_session_cred failed");
			return (retval); /* key not found or not valid */
		}
		memcpy((char *)&sessionkey, (char *)cred->session,
			sizeof (des_block));
	} else {	/* AKN_NICKNAME */
		sid = cred->nickname;
		if (sid < 0 || sid >= AUTHKERB_CACHESZ) {
			(void) syslog(LOG_DEBUG, "_svcauth_kerb: bad nickname");
			return (AUTH_BADCRED);	/* garbled credential */
		}
		memcpy(&sessionkey, _rpc_authkerb_cache[sid].clnt_cred.session,
			sizeof (des_block));
	}

#ifndef NOENCRYPTION
	/*
	 * Decrypt the timestamp
	 */

	cryptbuf[0] = verf.akv_xtimestamp;
	if (namekind == AKN_FULLNAME) {
		cryptbuf[1].key.high = window;
		cryptbuf[1].key.low = verf.akv_winverf;
#ifdef SUNDES_CRYPT
#ifdef DEBUG_CRYPT
		printf("  -->fullname: sun crypt\n");
#endif DEBUG_CRYPT
		ivec = sessionkey;
		status = cbc_crypt((char *) &sessionkey, (char *)cryptbuf,
			2*sizeof (des_block), DES_DECRYPT, (char *)&ivec);
#else !SUNDES_CRYPT
		/* XXXXX use pcbc_encrypt */
		/* decrypt */
#ifdef DEBUG_CRYPT
		printf("  -->fullname: kerberos crypt\n");
#endif DEBUG_CRYPT
		(void) key_sched((des_cblock *)&sessionkey, key_s);
		(void) cbc_encrypt(cryptbuf, cryptbuf, 2*sizeof (des_block),
			key_s, (des_cblock *)&sessionkey, 0);
		/* clean up */
		memset((char *) key_s, 0, sizeof (key_s));
#endif SUNDES_CRYPT
	} else {
#ifdef SUNDES_CRYPT
#ifdef DEBUG_CRYPT
		printf("  -->nickname: sun crypt\n");
#endif DEBUG_CRYPT
		status = ecb_crypt((char *) &sessionkey, (char *)cryptbuf,
			sizeof (des_block), DES_DECRYPT);
#else !SUNDES_CRYPT
		/* decrypt */
#ifdef DEBUG_CRYPT
		printf("  -->nickname: kerberos crypt\n");
#endif DEBUG_CRYPT
		(void) key_sched((des_cblock *)&sessionkey, key_s);
		(void) des_ecb_encrypt(cryptbuf, cryptbuf, key_s, 0);
		/* clean up */
		memset((char *) key_s, 0, sizeof (key_s));
#endif SUNDES_CRYPT
	}
#ifdef SUNDES_CRYPT
	if (DES_FAILED(status)) {
		debug("decryption failure");
		return (AUTH_FAILED);	/* system error */
	}
#endif SUNDES_CRYPT
	debug("timestamp decrypted ok");

#else /* NOENCRYPTION */
	cryptbuf[1].key.high = window;
	cryptbuf[1].key.low = verf.akv_winverf;
	cryptbuf[0] = verf.akv_xtimestamp;
#endif

	/*
	 * XDR the decrypted timestamp
	 */
	ixdr = (long *)cryptbuf;
	timestamp.tv_sec = IXDR_GET_LONG(ixdr);
	timestamp.tv_usec = IXDR_GET_LONG(ixdr);

	/*
	 * Check for valid credentials and verifiers.
	 * They could be invalid because the key was flushed
	 * out of the cache, and so a new session should begin.
	 * Be sure and send AUTH_REJECTED{CRED, VERF} if this is the case.
	 */
	{
		struct timeval current;
		int nick;
		int winverf;

		if (namekind == AKN_FULLNAME) {
			window = IXDR_GET_U_LONG(ixdr);
			winverf = IXDR_GET_U_LONG(ixdr);
#ifdef KERB_DEBUG
			kprint("\
_svcauth_kerb: window %d wverf %d timestamp %d %d\n",
				window, winverf,
				timestamp.tv_sec, timestamp.tv_usec);
#endif KERB_DEBUG
			if (winverf != window - 1) {
				debug("window verifier mismatch");
				return (AUTH_BADCRED);	/* garbled credential */
			}
			sid = cache_spot(cred, &timestamp);
			if (sid < 0) {
				debug("replayed credential");
				return (AUTH_REJECTEDCRED);	/* replay */
			}
			nick = 0;
		} else {	/* AKN_NICKNAME */
			window = _rpc_authkerb_cache[sid].window;
			nick = 1;
		}

		if ((u_long)timestamp.tv_usec >= USEC_PER_SEC) {
			debug("invalid usecs");
			(void) syslog(LOG_DEBUG,
				"_svcauth_kerb: invalid usecs");
			/* cached out (bad key), or garbled verifier */
			return (nick ? AUTH_REJECTEDVERF : AUTH_BADVERF);
		}
		if (nick && BEFORE(&timestamp,
				&_rpc_authkerb_cache[sid].laststamp)) {
			debug("timestamp before last seen");

			return (AUTH_REJECTEDVERF);	/* replay */
		}
		(void) gettimeofday(&current, (struct timezone *)NULL);
#ifdef KERB_DEBUG
		if (nick)
		    kprint(" expiry %d < current %d?\n",
			_rpc_authkerb_cache[sid].clnt_cred.expiry,
			current.tv_sec);
#endif KERB_DEBUG

		if (nick && _rpc_authkerb_cache[sid].clnt_cred.expiry
			< current.tv_sec) {
			/* credential expired - errro ?? */
/* XXX */		_kmsgout("*** kerberos ticket expired - rejecting");
			return (AUTH_TIMEEXPIRE);
		}
		current.tv_sec -= window;	/* allow for expiration */
		if (!BEFORE(&current, &timestamp)) {
			debug("timestamp expired");
#ifdef KERB_DEBUG
			kprint("current %d window %d expire %d; timestamp %d\n",
				current.tv_sec + window, window,
				current.tv_sec, timestamp.tv_sec);
#endif KERB_DEBUG
			/* replay, or garbled credential */
			return (nick ? AUTH_REJECTEDVERF : AUTH_BADCRED);
		}
	}

	/*
	 * Set up the reply verifier
	 */
	verf.akv_nickname = sid;

	/*
	 * xdr the timestamp before encrypting
	 */
	ixdr = (long *)cryptbuf;
	IXDR_PUT_LONG(ixdr, timestamp.tv_sec - 1);
	IXDR_PUT_LONG(ixdr, timestamp.tv_usec);

#ifndef NOENCRYPTION
	/*
	 * encrypt the timestamp
	 */
#ifdef SUNDES_CRYPT
#ifdef DEBUG_CRYPT
	printf("  -->verifier: sun crypt\n");
#endif DEBUG_CRYPT
	status = ecb_crypt((char *) &sessionkey, (char *)cryptbuf,
	    sizeof (des_block), DES_ENCRYPT);
	if (DES_FAILED(status)) {
		debug("encryption failure");

		return (AUTH_FAILED);	/* system error */
	}
#else !SUNDES_CRYPT
	/* encrypt */
#ifdef DEBUG_CRYPT
	printf("  -->verifier: kerberos crypt\n");
#endif DEBUG_CRYPT
	(void) key_sched((des_cblock *)&sessionkey, key_s);
	(void) des_ecb_encrypt(cryptbuf, cryptbuf, key_s, 1);
	/* clean up */
	memset((char *) key_s, 0, sizeof (key_s));
#endif SUNDES_CRYPT
#endif /* NOENCRYPTION */

	verf.akv_xtimestamp = cryptbuf[0];

	/*
	 * Serialize the reply verifier, and update rqst
	 */
	ixdr = (long *)msg->rm_call.cb_verf.oa_base;
	*ixdr++ = (long)verf.akv_xtimestamp.key.high;
	*ixdr++ = (long)verf.akv_xtimestamp.key.low;
	*ixdr++ = (long)verf.akv_nickname;

	xprt->xp_verf.oa_flavor = AUTH_KERB;
	xprt->xp_verf.oa_base = msg->rm_call.cb_verf.oa_base;
	xprt->xp_verf.oa_length =
		(char *)ixdr - msg->rm_call.cb_verf.oa_base;

	/*
	 * We succeeded, commit the data to the cache now and
	 * finish cooking the credential.
	 */
	entry = &_rpc_authkerb_cache[sid];
	entry->laststamp = timestamp;
	cache_ref(sid);
	if (namekind == AKN_FULLNAME) {
		cred->nickname = sid;	/* save nickname */
		cred->window = window;	/* save window */
		strncpy(entry->clnt_cred.pname, cred->pname, ANAME_SZ);
		strncpy(entry->clnt_cred.pinst, cred->pinst, INST_SZ);
		strncpy(entry->clnt_cred.prealm, cred->prealm, REALM_SZ);
		entry->clnt_cred.expiry = cred->expiry;
		memcpy(entry->clnt_cred.session, &sessionkey,
			sizeof (des_block));
		entry->window = window;
#ifdef KERB_UCRED
		invalidate(entry->localcred); /* mark any cached cred invalid */
#endif KERB_UCRED
	} else { /* AKN_NICKNAME */
		/*
		 * nicknames are cooked into fullnames
		 */
		strncpy(cred->pname, entry->clnt_cred.pname, ANAME_SZ);
		strncpy(cred->pinst, entry->clnt_cred.pinst, INST_SZ);
		strncpy(cred->prealm, entry->clnt_cred.prealm, REALM_SZ);
		cred->window = entry->window;
		cred->expiry = entry->clnt_cred.expiry;
	}
	debug("ret OK!");
	return (AUTH_OK);	/* we made it! */
}


/*
 * Initialize the cache
 */
static void
cache_init()
{
	register short i;

	_rpc_authkerb_cache = (struct cache_entry *)
		mem_alloc(sizeof (struct cache_entry) * AUTHKERB_CACHESZ);
	memset((char *)_rpc_authkerb_cache, 0,
		sizeof (struct cache_entry) * AUTHKERB_CACHESZ);

	authkerb_lru = (short *)mem_alloc(sizeof (short) * AUTHKERB_CACHESZ);
	/*
	 * Initialize the lru list
	 */
	for (i = 0; i < AUTHKERB_CACHESZ; i++) {
		authkerb_lru[i] = i;
	}
}

#ifdef KERB_DEBUG
void
authkerb_cache_flush(name, instance, realm)
register char *name;
register char *instance;
char *realm;
{
	struct cache_entry *cp;
	register authkerb_clnt_cred *cu;
	int i;

	debug("**cache_flush ....");
	if (name == NULL) { /* do the entire cache */
		debug(" doo allll");
		memset((char *)_rpc_authkerb_cache, 0,
			sizeof (struct cache_entry) * AUTHKERB_CACHESZ);
		/*
		 * Initialize the lru list
		 */
		for (i = 0; i < AUTHKERB_CACHESZ; i++) {
			authkerb_lru[i] = i;
		}
		return;
	}
	for (cp = _rpc_authkerb_cache, i = 0; i < AUTHKERB_CACHESZ; i++, cp++) {
		cu = &cp->clnt_cred;
		if (!memcmp(cu->pname, name, strlen(name) + 1) &&
		    !memcmp(cu->pinst, instance, strlen(instance) + 1) &&
		    !memcmp(cu->prealm, realm, strlen(realm) + 1)) {
			memset(cu->session, 0, sizeof (des_block));
			debug("flushcache:..found it");
			/* should change lru scheme ?? */
			return;
		}
	}
}
#endif KERB_DEBUG


/*
 * Find the lru victim
 */
static short
cache_victim()
{
	return (authkerb_lru[AUTHKERB_CACHESZ-1]);
}

/*
 * Note that sid was referenced
 */
static void
cache_ref(sid)
	register short sid;
{
	register int i;
	register short curr;
	register short prev;

	prev = authkerb_lru[0];
	authkerb_lru[0] = sid;
	for (i = 1; prev != sid; i++) {
		curr = authkerb_lru[i];
		authkerb_lru[i] = prev;
		prev = curr;
	}
}


/*
 * Find a spot in the cache for a credential containing
 * the items given.  Return -1 if a replay is detected, otherwise
 * return the spot in the cache.
 */
static short
cache_spot(au, timestamp)
	register authkerb_clnt_cred *au;
	struct timeval *timestamp;
{
	register struct cache_entry *cp;
	register int i;
	authkerb_clnt_cred *cu;

	for (cp = _rpc_authkerb_cache, i = 0; i < AUTHKERB_CACHESZ; i++, cp++) {
		cu = &cp->clnt_cred;
		if (!memcmp(cu->session, au->session, sizeof (C_Block)) &&
		    !memcmp(cu->pname, au->pname, strlen(au->pname) + 1) &&
		    !memcmp(cu->pinst, au->pinst, strlen(au->pinst) + 1) &&
		    !memcmp(cu->prealm, au->prealm, strlen(au->prealm) + 1)) {
			if (BEFORE(timestamp, &cp->laststamp)) {
				svcauthkerb_stats.ncachereplays++;
				return (-1);	/* replay */
			}
			svcauthkerb_stats.ncachehits++;
			return (i);	/* refresh */
		}
	}
	svcauthkerb_stats.ncachemisses++;
	return (cache_victim());	/* new credential */
}


#ifdef KERB_UCRED
/*
 * Local credential handling stuff.
 * NOTE: bsd unix dependent.
 * Other operating systems should put something else here.
 */
#define	UNKNOWN 	-2	/* grouplen, if cached cred is unknown user */
#define	INVALID		-1 	/* grouplen, if cache entry is invalid */

struct bsdcred {
	uid_t uid;			/* cached uid */
	gid_t gid;			/* cached gid */
	short grouplen;			/* length of cached groups */
	gid_t groups[NGROUPS_UMAX];	/* cached groups */
};

#include <pwd.h>
#include <grp.h>

static int __krb_getgroups();		/* get grouplist for principal */

/*
 * Map a kerberos credential into a unix cred.
 * We cache the credential here so the application does
 * not have to make an rpc call every time to interpret
 * the credential.
 * NOTE that this routine must be called while the request is still in
 * process (i.e., before svc_sendreply is called).
 */
authkerb_getucred(rqst, uid, gid, grouplen, groups)
struct svc_req *rqst;		/* request pointer */
uid_t *uid;			/* returned uid */
gid_t *gid;			/* returned gid */
short *grouplen;		/* returned size of groups[] */
register int *groups;		/* int groups[NGROUPS_UMAX]; */
{
	register int i;
	register struct cache_entry *entry;
	struct bsdcred *cred;
	struct passwd *pw;
	u_long sid;				/* nickname into cache */
	struct authkerb_clnt_cred *rcred;	/* cred in request */
	struct authkerb_clnt_cred *kcred;	/* cached kerberos cred */
	struct mycred *mcp;

	rcred = (struct authkerb_clnt_cred *)rqst->rq_clntcred;
	sid = rcred->nickname;
	if (sid >= AUTHKERB_CACHESZ) {
		debug("authkerb_getucred: invalid nickname");
		(void) syslog(LOG_DEBUG, "authkerb_getucred: invalid nickname");
		return (0);
	}
	entry = &_rpc_authkerb_cache[sid];
	kcred = &entry->clnt_cred;
	cred = (struct bsdcred *)entry->localcred;
	if (cred == NULL) {
		cred = (struct bsdcred *)mem_alloc(sizeof (struct bsdcred));
		entry->localcred = (char *)cred;
		cred->grouplen = INVALID;
	}
	if (cred->grouplen == INVALID) {
		/*
		 *  Not in cache: lookup
		 *
		 *  Strategy:
		 *	If the client's realm is different than our own,
		 *	then treat the user as unknown.
		 *	[XXX handle different realms better]
		 *
		 *	Look up the principal name, ignoring
		 *	the instance, to see if it matches a valid
		 *	user name locally.  If so, use the uid and gid
		 *	from the passwd entry.
		 */
#ifdef KERB_DEBUG
		kprint("authkerb_getucred: lookup %s.%s@%s\n",
			kcred->pname, kcred->pinst, kcred->prealm);
#endif KERB_DEBUG

		/* client in same realm as us? */
		for (mcp = my_credp; mcp; mcp = mcp->next) {
			if (mcp->xprt == rqst->rq_xprt) {
				/* found mycred struct */
				break;
			}
		}
		if (mcp == NULL) {
			if (def_mycred.next == NULL)
				def_mycred.realm[0] = '\0';
			mcp = &def_mycred;
		}
		if (mcp->realm[0] == '\0' || strcmp(mcp->realm, kcred->prealm))
		{
			debug("authkerb_getucred: different realm");
			goto unkpname;
		}

		if ((pw = getpwnam(kcred->pname)) == NULL)
		{
		    unkpname:
			(void) syslog(LOG_DEBUG,
			    "authkerb_getucred:  unknown netname");
			cred->grouplen = UNKNOWN;
			/* mark as lookup up, but not found */
			return (0);
		}
		(void) syslog(LOG_DEBUG,
		    "authkerb_getucred:  missed ucred cache");

		*uid = pw->pw_uid;
		*gid = pw->pw_gid;
		*grouplen = (short)__krb_getgroups(kcred->pname, groups,
					NGROUPS_UMAX);

		/* save locally in cache */
		cred->uid = *uid;
		cred->gid = *gid;
		cred->grouplen = *grouplen;
		for (i = 0; i < *grouplen; i++)
			cred->groups[i] = (gid_t)groups[i];

		/*
		 *  Check to see if principal maps to root.  If so,
		 *  disallow uid mapping at this level.
		 */
		if (*uid == 0) {
			debug("authkerb_getucred: root mapping denied");
			return (0);
		}
#ifdef KERB_DEBUG
		kprint("authkerb_getucred: uid %d gid %d ngrps %d\n",
			cred->uid, cred->gid, cred->grouplen);
#endif KERB_DEBUG
		return (1);
	} else if (cred->grouplen == UNKNOWN) {
		/*
		 * Already lookup up, but no match found
		 */
#ifdef KERB_DEBUG
		kprint("authkerb_getucred: %s.%s@%s UNKNOWN\n",
			kcred->pname, kcred->pinst, kcred->prealm);
#endif KERB_DEBUG
		return (0);
	}

	/*
	 * cached credentials
	 */

	/*
	 *  Check to see if principal maps to root.  If so,
	 *  disallow uid mapping at this level.
	 */
	if (cred->uid == 0) {
		debug("authkerb_getucred: from cache: root mapping denied");
		return (0);
	}

	*uid = cred->uid;
	*gid = cred->gid;
	*grouplen = cred->grouplen;
	for (i = cred->grouplen - 1; i >= 0; i--) {
		groups[i] = (int)cred->groups[i];
	}
#ifdef KERB_DEBUG
	kprint("authkerb_getucred: %s.%s@%s from cache: uid %d gid %d\n",
		kcred->pname, kcred->pinst, kcred->prealm,
		cred->uid, cred->gid);
#endif KERB_DEBUG
	return (1);
}

static void
invalidate(cred)
	char *cred;
{
	if (cred == NULL) {
		return;
	}
	((struct bsdcred *)cred)->grouplen = INVALID;
}

/*
 *  Initialize the groups list via getgrent.  Assumes that
 *  groups has been initialized by the caller
 *  to point to an array of at least size maxgrps.
 *  Returns the number of values stored.
 */
int kerb_dogrouplist = 1;		/* can be turned off if desired */

static int
__krb_getgroups(name, groups, maxgrps)
char *name;		/* username to look for */
int *groups;		/* array to initialize */
u_int maxgrps;		/* max number of groups to list */
{
	register char **pp;
	int count;
	struct group *group;

	if (!kerb_dogrouplist)
		maxgrps = 0;

	count = 0;
	if (maxgrps > 0) {
		setgrent();
		while (group = getgrent()) {
			for (pp = group->gr_mem; *pp; pp++) {
				if (strcmp(*pp, name) == 0) {
					/* found one */
					*groups++ = (int)group->gr_gid;
					if (++count == maxgrps)
						goto done; /* filled list */
					break; /* go to next entry */
				}
			}
		}

done:
		endgrent();
	}

	return (count);
}

#endif KERB_UCRED
