/*
 * Copyright (c) 1986-1991 by Sun Microsystems Inc.
 */

#pragma ident	"@(#)svcauth_des.c 1.21	95/05/30 SMI"

/*
 * svcauth_des.c, server-side des authentication
 *
 * We insure for the service the following:
 * (1) The timestamp microseconds do not exceed 1 million.
 * (2) The timestamp plus the window is less than the current time.
 * (3) The timestamp is not less than the one previously
 *	seen in the current session.
 *
 * It is up to the server to determine if the window size is
 * too small.
 *
 */

#include <assert.h>
#include "rpc_mt.h"
#include <rpc/des_crypt.h>
#include <rpc/rpc.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <stdlib.h>
#include <string.h>

#ifdef KERNEL
#include <sys/kernel.h>
#define	gettimeofday(tvp, tzp)	(*(tvp) = time)
#else
#include <syslog.h>
#endif

extern int key_decryptsession_pk();

#ifndef NGROUPS
#define	NGROUPS 16
#endif

#define	USEC_PER_SEC	1000000L
#define	BEFORE(t1, t2) timercmp(t1, t2, < /* EMPTY */)


/*
 * LRU cache of conversation keys and some other useful items.
 */
#define	DEF_AUTHDES_CACHESZ 128
int authdes_cachesz = DEF_AUTHDES_CACHESZ;
struct cache_entry {
	des_block key;			/* conversation key */
	char *rname;			/* client's name */
	u_int window;			/* credential lifetime window */
	struct timeval laststamp;	/* detect replays of creds */
	char *localcred;		/* generic local credential */
	int index;			/* where are we in array? */
	struct cache_entry *prev;	/* prev entry on LRU list */
	struct cache_entry *next;	/* next entry on LRU list */
};

static const char __getucredstr[] = "authdes_getucred:";

static struct cache_entry *_rpc_authdes_cache;	/* [authdes_cachesz] */
static struct cache_entry *cache_head;	/* cache (in LRU order) */
static struct cache_entry *cache_tail;	/* cache (in LRU order) */

/*
 *	A rwlock_t would seem to make more sense, but it turns out we always
 *	muck with the cache entries, so would always need a write lock (in
 *	which case, we might as well use a mutex).
 */
extern mutex_t	authdes_lock;


static void cache_init();		/* initialize the cache */
static short cache_spot();		/* find an entry in the cache */
static void cache_ref(/*short sid*/);	/* note that sid was ref'd */
static void invalidate();		/* invalidate entry in cache */
static void __msgout();
static void __msgout2(const char *, const char *);

/*
 * cache statistics
 */
struct {
	u_long ncachehits;	/* times cache hit, and is not replay */
	u_long ncachereplays;	/* times cache hit, and is replay */
	u_long ncachemisses;	/* times cache missed */
} svcauthdes_stats;

/*
 * Service side authenticator for AUTH_DES
 */
enum auth_stat
__svcauth_des(rqst, msg)
	register struct svc_req *rqst;
	register struct rpc_msg *msg;
{

	register long	*ixdr;
	des_block	cryptbuf[2];
	register struct authdes_cred	*cred;
	struct authdes_verf	verf;
	int	status;
	register struct cache_entry	*entry;
	short	sid;
	des_block	*sessionkey;
	des_block	ivec;
	u_int	window;
	struct timeval	timestamp;
	u_long	namelen;
	struct area {
		struct authdes_cred area_cred;
		char area_netname[MAXNETNAMELEN+1];
	} *area;
	int	fullname_rcvd = 0;

	trace1(TR___svcauth_des, 0);
	mutex_lock(&authdes_lock);
	if (_rpc_authdes_cache == NULL) {
		cache_init();
	}
	mutex_unlock(&authdes_lock);

	area = (struct area *)rqst->rq_clntcred;
	cred = (struct authdes_cred *)&area->area_cred;

	/*
	 * Get the credential
	 */
	ixdr = (long *)msg->rm_call.cb_cred.oa_base;
	cred->adc_namekind = IXDR_GET_ENUM(ixdr, enum authdes_namekind);
	switch (cred->adc_namekind) {
	case ADN_FULLNAME:
		namelen = IXDR_GET_U_LONG(ixdr);
		if (namelen > MAXNETNAMELEN) {
			trace1(TR___svcauth_des, 1);
			return (AUTH_BADCRED);
		}
		cred->adc_fullname.name = area->area_netname;
		memcpy(cred->adc_fullname.name, (char *)ixdr, (u_int)namelen);
		cred->adc_fullname.name[namelen] = 0;
		ixdr += (RNDUP(namelen) / BYTES_PER_XDR_UNIT);
		cred->adc_fullname.key.key.high = (u_long)*ixdr++;
		cred->adc_fullname.key.key.low = (u_long)*ixdr++;
		cred->adc_fullname.window = (u_long)*ixdr++;
		fullname_rcvd++;
		break;
	case ADN_NICKNAME:
		cred->adc_nickname = (u_long)*ixdr++;
		break;
	default:
		trace1(TR___svcauth_des, 1);
		return (AUTH_BADCRED);
	}

	/*
	 * Get the verifier
	 */
	ixdr = (long *)msg->rm_call.cb_verf.oa_base;
	verf.adv_xtimestamp.key.high = (u_long)*ixdr++;
	verf.adv_xtimestamp.key.low = (u_long)*ixdr++;
	verf.adv_int_u = (u_long)*ixdr++;

	mutex_lock(&authdes_lock);

	/*
	 * Get the conversation key
	 */
	if (fullname_rcvd) {	/* ADN_FULLNAME */
		netobj	pkey;
		char	pkey_data[1024];

		sessionkey = &cred->adc_fullname.key;
		if (! getpublickey(cred->adc_fullname.name, pkey_data)) {

			/*
			 * if the user has no public key, treat him as the
			 * unauthenticated identity - nobody. If this
			 * works, it means the client didn't find the
			 * user's keys and used nobody's secret key
			 * as a backup.
			 */

			if (! getpublickey("nobody", pkey_data)) {
				__msgout(LOG_ERR,
				"_svcauth_des: no public key for nobody or",
				cred->adc_fullname.name);
				mutex_unlock(&authdes_lock);
				trace1(TR___svcauth_des, 1);
				return (AUTH_BADCRED); /* no key */
			} else {

				/*
				 * found a public key for nobody. change
				 * the fullname id to nobody, so the caller
				 * thinks the client specified nobody
				 * as the user identity.
				 */

				strcpy(cred->adc_fullname.name, "nobody");
			}
		}
		pkey.n_bytes = pkey_data;
		pkey.n_len = strlen(pkey_data) + 1;
		if (key_decryptsession_pk(cred->adc_fullname.name, &pkey,
				sessionkey) < 0) {
		__msgout(LOG_INFO,
			"_svcauth_des: key_decryptsessionkey failed for",
			cred->adc_fullname.name);
			mutex_unlock(&authdes_lock);
			trace1(TR___svcauth_des, 1);
			return (AUTH_BADCRED);	/* key not found */
		}
	} else { /* ADN_NICKNAME */
		sid = cred->adc_nickname;
		if (sid >= authdes_cachesz) {
			__msgout(LOG_INFO, "_svcauth_des:", "bad nickname");
			mutex_unlock(&authdes_lock);
			trace1(TR___svcauth_des, 1);
			return (AUTH_BADCRED);	/* garbled credential */
		}
		/* actually check that the entry is not null */
		entry = &_rpc_authdes_cache[sid];
		if (entry->rname == NULL) {
			mutex_unlock(&authdes_lock);
			trace1(TR___svcauth_des, 1);
			return (AUTH_BADCRED);	/* cached out */
		}
		sessionkey = &_rpc_authdes_cache[sid].key;
	}

	/*
	 * Decrypt the timestamp
	 */
	cryptbuf[0] = verf.adv_xtimestamp;
	if (fullname_rcvd) {	/* ADN_FULLNAME */
		cryptbuf[1].key.high = cred->adc_fullname.window;
		cryptbuf[1].key.low = verf.adv_winverf;
		ivec.key.high = ivec.key.low = 0;
		status = cbc_crypt((char *)sessionkey, (char *)cryptbuf,
			2 * sizeof (des_block), DES_DECRYPT | DES_HW,
			(char *)&ivec);
	} else {
		status = ecb_crypt((char *)sessionkey, (char *)cryptbuf,
			sizeof (des_block), DES_DECRYPT | DES_HW);
	}
	if (DES_FAILED(status)) {
		__msgout(LOG_ERR, "_svcauth_des: DES decryption failure for",
			fullname_rcvd ? cred->adc_fullname.name :
			_rpc_authdes_cache[sid].rname);
		mutex_unlock(&authdes_lock);
		trace1(TR___svcauth_des, 1);
		return (AUTH_FAILED);	/* system error */
	}

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
		int	nick;
		int	winverf;

		if (fullname_rcvd) {
			window = IXDR_GET_U_LONG(ixdr);
			winverf = IXDR_GET_U_LONG(ixdr);
			if (winverf != window - 1) {
				__msgout(LOG_INFO,
					"_svcauth_des: corrupted window from",
					cred->adc_fullname.name);
				mutex_unlock(&authdes_lock);
				trace1(TR___svcauth_des, 1);
				/* garbled credential or invalid secret key */
				return (AUTH_BADCRED);
			}
			sid = cache_spot(sessionkey, cred->adc_fullname.name,
					&timestamp);
			if (sid < 0) {
			__msgout(LOG_INFO,
				"_svcauth_des: replayed credential from",
				cred->adc_fullname.name);
				mutex_unlock(&authdes_lock);
				trace1(TR___svcauth_des, 1);
				return (AUTH_REJECTEDCRED);	/* replay */
			}
			nick = 0;
		} else {	/* ADN_NICKNAME */
			window = _rpc_authdes_cache[sid].window;
			nick = 1;
		}

		if (timestamp.tv_usec >= USEC_PER_SEC) {
		__msgout(LOG_INFO,
			"_svcauth_des: invalid timestamp received from",
			fullname_rcvd ? cred->adc_fullname.name :
				_rpc_authdes_cache[sid].rname);
			/* cached out (bad key), or garbled verifier */
			mutex_unlock(&authdes_lock);
			trace1(TR___svcauth_des, 1);
			return (nick ? AUTH_REJECTEDVERF : AUTH_BADVERF);
		}
		if (nick && BEFORE(&timestamp,
				&_rpc_authdes_cache[sid].laststamp)) {
			__msgout(LOG_INFO,
	"_svcauth_des: timestamp is earlier than the one previously seen from",
			fullname_rcvd ? cred->adc_fullname.name :
				_rpc_authdes_cache[sid].rname);
			mutex_unlock(&authdes_lock);
			trace1(TR___svcauth_des, 1);
			return (AUTH_REJECTEDVERF);	/* replay */
		}
		(void) gettimeofday(&current, (struct timezone *)NULL);
		current.tv_sec -= window;	/* allow for expiration */
		if (!BEFORE(&current, &timestamp)) {
			__msgout(LOG_INFO,
				"_svcauth_des: timestamp expired for",
				fullname_rcvd ? cred->adc_fullname.name :
					_rpc_authdes_cache[sid].rname);
			/* replay, or garbled credential */
			mutex_unlock(&authdes_lock);
			trace1(TR___svcauth_des, 1);
			return (nick ? AUTH_REJECTEDVERF : AUTH_BADCRED);
		}
	}

	/*
	 * Set up the reply verifier
	 */
	verf.adv_nickname = sid;

	/*
	 * xdr the timestamp before encrypting
	 */
	ixdr = (long *)cryptbuf;
	IXDR_PUT_LONG(ixdr, timestamp.tv_sec - 1);
	IXDR_PUT_LONG(ixdr, timestamp.tv_usec);

	/*
	 * encrypt the timestamp
	 */
	status = ecb_crypt((char *)sessionkey, (char *)cryptbuf,
				sizeof (des_block), DES_ENCRYPT | DES_HW);
	if (DES_FAILED(status)) {
		__msgout(LOG_ERR, "_svcauth_des: DES encryption failure for",
			fullname_rcvd ? cred->adc_fullname.name :
			_rpc_authdes_cache[sid].rname);
		mutex_unlock(&authdes_lock);
		trace1(TR___svcauth_des, 1);
		return (AUTH_FAILED);	/* system error */
	}
	verf.adv_xtimestamp = cryptbuf[0];

	/*
	 * Serialize the reply verifier, and update rqst
	 */
	ixdr = (long *)msg->rm_call.cb_verf.oa_base;
	*ixdr++ = (long)verf.adv_xtimestamp.key.high;
	*ixdr++ = (long)verf.adv_xtimestamp.key.low;
	*ixdr++ = (long)verf.adv_int_u;

	rqst->rq_xprt->xp_verf.oa_flavor = AUTH_DES;
	rqst->rq_xprt->xp_verf.oa_base = msg->rm_call.cb_verf.oa_base;
	rqst->rq_xprt->xp_verf.oa_length =
		(char *)ixdr - msg->rm_call.cb_verf.oa_base;

	/*
	 * We succeeded, commit the data to the cache now and
	 * finish cooking the credential.
	 */
	entry = &_rpc_authdes_cache[sid];
	entry->laststamp = timestamp;
	cache_ref(sid);
	if (cred->adc_namekind == ADN_FULLNAME) {
		cred->adc_fullname.window = window;
		cred->adc_nickname = sid;	/* save nickname */
		if (entry->rname != NULL) {
			mem_free(entry->rname, strlen(entry->rname) + 1);
		}
		entry->rname =
			(char *)mem_alloc((u_int)strlen(cred->adc_fullname.name)
					+ 1);
		if (entry->rname != NULL) {
			(void) strcpy(entry->rname, cred->adc_fullname.name);
		} else {
			__msgout(LOG_CRIT, "_svcauth_des:", "out of memory");
		}
		entry->key = *sessionkey;
		entry->window = window;
		/* mark any cached cred invalid */
		invalidate(entry->localcred);
	} else { /* ADN_NICKNAME */
		/*
		 * nicknames are cooked into fullnames
		 */
		cred->adc_namekind = ADN_FULLNAME;
		cred->adc_fullname.name = entry->rname;
		cred->adc_fullname.key = entry->key;
		cred->adc_fullname.window = entry->window;
	}
	mutex_unlock(&authdes_lock);
	trace1(TR___svcauth_des, 1);
	return (AUTH_OK);	/* we made it! */
}


/*
 * Initialize the cache
 */
static void
cache_init()
{
	register int i;

/* LOCK HELD ON ENTRY: authdes_lock */

	trace1(TR_cache_init, 0);
	assert(MUTEX_HELD(&authdes_lock));
	_rpc_authdes_cache = (struct cache_entry *)
		mem_alloc(sizeof (struct cache_entry) * authdes_cachesz);
	memset((char *)_rpc_authdes_cache, 0,
		sizeof (struct cache_entry) * authdes_cachesz);

	/*
	 * Initialize the lru chain (linked-list)
	 */
	for (i = 1; i < (authdes_cachesz - 1); i++) {
		_rpc_authdes_cache[i].index = i;
		_rpc_authdes_cache[i].next = &_rpc_authdes_cache[i + 1];
		_rpc_authdes_cache[i].prev = &_rpc_authdes_cache[i - 1];
	}
	cache_head = &_rpc_authdes_cache[0];
	cache_tail = &_rpc_authdes_cache[authdes_cachesz - 1];

	/*
	 * These elements of the chain need special attention...
	 */
	cache_head->index = 0;
	cache_tail->index = authdes_cachesz - 1;
	cache_head->next = &_rpc_authdes_cache[1];
	cache_head->prev = cache_tail;
	cache_tail->next = cache_head;
	cache_tail->prev = &_rpc_authdes_cache[authdes_cachesz - 2];
	trace1(TR_cache_init, 1);
}


/*
 * Find the lru victim
 */
static short
cache_victim()
{

/* LOCK HELD ON ENTRY: authdes_lock */

	trace1(TR_cache_victim, 0);
	assert(MUTEX_HELD(&authdes_lock));
	trace1(TR_cache_victim, 1);
	return (cache_head->index);			/* list in lru order */
}

/*
 * Note that sid was referenced
 */
static void
cache_ref(sid)
	register short sid;
{
	register int i;
	register struct cache_entry *curr = &_rpc_authdes_cache[sid];


/* LOCK HELD ON ENTRY: authdes_lock */

	trace1(TR_cache_ref, 0);
	assert(MUTEX_HELD(&authdes_lock));

	/*
	 * move referenced item from its place on the LRU chain
	 * to the tail of the chain while checking for special
	 * conditions (mainly for performance).
	 */
	if (cache_tail == curr) {			/* no work to do */
		/*EMPTY*/;
	} else if (cache_head == curr) {
		cache_head = cache_head->next;
		cache_tail = curr;
	} else {
		(curr->next)->prev = curr->prev;	/* fix thy neighbor */
		(curr->prev)->next = curr->next;
		curr->next = cache_head;		/* fix thy self... */
		curr->prev = cache_tail;
		cache_head->prev = curr;		/* fix the head  */
		cache_tail->next = curr;		/* fix the tail  */
		cache_tail = curr;			/* move the tail */
	}

	trace1(TR_cache_ref, 1);
}

/*
 * Find a spot in the cache for a credential containing
 * the items given. Return -1 if a replay is detected, otherwise
 * return the spot in the cache.
 */
static short
cache_spot(key, name, timestamp)
	register des_block *key;
	char *name;
	struct timeval *timestamp;
{
	register struct cache_entry *cp;
	register int i;
	register u_long hi;
	static short dummy;

/* LOCK HELD ON ENTRY: authdes_lock */

	trace1(TR_cache_spot, 0);
	assert(MUTEX_HELD(&authdes_lock));
	hi = key->key.high;
	for (cp = _rpc_authdes_cache, i = 0; i < authdes_cachesz; i++, cp++) {
		if (cp->key.key.high == hi &&
		    cp->key.key.low == key->key.low &&
		    cp->rname != NULL &&
		    memcmp(cp->rname, name, strlen(name) + 1) == 0) {
			if (BEFORE(timestamp, &cp->laststamp)) {
				svcauthdes_stats.ncachereplays++;
				trace1(TR_cache_spot, 1);
				return (-1);	/* replay */
			}
			svcauthdes_stats.ncachehits++;
			trace1(TR_cache_spot, 1);
			return (i);
			/* refresh */
		}
	}
	svcauthdes_stats.ncachemisses++;
	dummy = cache_victim();
	trace1(TR_cache_spot, 1);
	return (dummy);	/* new credential */
}


/*
 * Local credential handling stuff.
 * NOTE: bsd unix dependent.
 * Other operating systems should put something else here.
 */
#define	UNKNOWN 	-2	/* grouplen, if cached cred is unknown user */
#define	INVALID		-1 	/* grouplen, if cache entry is invalid */

struct bsdcred {
	uid_t uid;		/* cached uid */
	gid_t gid;		/* cached gid */
	short grouplen;	/* length of cached groups */
	short groups[NGROUPS];	/* cached groups */
};

static void
invalidate(cred)
	char *cred;
{
	trace1(TR_invalidate, 0);
	if (cred == NULL) {
		trace1(TR_invalidate, 1);
		return;
	}
	((struct bsdcred *)cred)->grouplen = INVALID;
	trace1(TR_invalidate, 1);
}



/*
 * Map a des credential into a unix cred.
 * We cache the credential here so the application does
 * not have to make an rpc call every time to interpret
 * the credential.
 */


authdes_getucred(adc, uid, gid, grouplen, groups)
	const struct authdes_cred *adc;
	uid_t *uid;
	gid_t *gid;
	short *grouplen;
	gid_t *groups;
{
	unsigned sid;
	register int i;
	uid_t i_uid;
	gid_t i_gid;
	int i_grouplen;
	struct bsdcred *cred;

	trace1(TR_authdes_getucred, 0);
	sid = adc->adc_nickname;
	if (sid >= authdes_cachesz) {
		__msgout2(__getucredstr, "invalid nickname");
		trace1(TR_authdes_getucred, 1);
		return (0);
	}
	mutex_lock(&authdes_lock);
	cred = (struct bsdcred *)_rpc_authdes_cache[sid].localcred;
	if (cred == NULL) {
		cred = (struct bsdcred *)mem_alloc(sizeof (struct bsdcred));
		_rpc_authdes_cache[sid].localcred = (char *)cred;
		cred->grouplen = INVALID;
	}
	if (cred->grouplen == INVALID) {
		/*
		 * not in cache: lookup
		 */
		if (!netname2user(adc->adc_fullname.name, (uid_t *) &i_uid,
			(gid_t *)&i_gid, &i_grouplen, (gid_t *) groups)) {
			__msgout2(__getucredstr, "unknown netname");
			/* mark as lookup up, but not found */
			cred->grouplen = UNKNOWN;
			mutex_unlock(&authdes_lock);
			trace1(TR_authdes_getucred, 1);
			return (0);
		}
		__msgout2(__getucredstr, "missed ucred cache");
		*uid = cred->uid = i_uid;
		*gid = cred->gid = i_gid;
		*grouplen = cred->grouplen = i_grouplen;
		for (i = i_grouplen - 1; i >= 0; i--) {
			cred->groups[i] = groups[i];	/* int to short */
		}
		mutex_unlock(&authdes_lock);
		trace1(TR_authdes_getucred, 1);
		return (1);
	} else if (cred->grouplen == UNKNOWN) {
		/*
		 * Already lookup up, but no match found
		 */
		mutex_unlock(&authdes_lock);
		trace1(TR_authdes_getucred, 1);
		return (0);
	}

	/*
	 * cached credentials
	 */
	*uid = cred->uid;
	*gid = cred->gid;
	*grouplen = cred->grouplen;
	for (i = cred->grouplen - 1; i >= 0; i--) {
		groups[i] = cred->groups[i];	/* short to int */
	}
	mutex_unlock(&authdes_lock);
	trace1(TR_authdes_getucred, 1);
	return (1);
}


static void
__msgout(level, str, strarg)
int	level;
const	char *str;
const	char *strarg;
{
	trace1(TR___msgout, 0);
#ifdef	KERNEL
		printf("%s %s\n", str, strarg);
#else
		(void) syslog(level, "%s %s", str, strarg);
#endif
	trace1(TR___msgout, 1);
}


static void
__msgout2(str, str2)
	const char *str, *str2;
{
	trace1(TR___msgout, 0);
#ifdef	KERNEL
		printf("%s %s", str, str2);
#else
		(void) syslog(LOG_DEBUG, "%s %s", str, str2);
#endif
	trace1(TR___msgout, 1);
}
