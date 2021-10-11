/*
 * Copyright 1991 NCR Corporation - Dayton, Ohio, USA
 *
 *    Copyright (c) 1994, 1995 Sun Microsystems, Inc.
 *    All rights reserved.
 */

#pragma ident "@(#)lm_subr.c	1.46	95/10/03 SMI" /* NCR OS2.00.00 1.3 */

/*
 * This is general subroutines used by both the server and client side of
 * the Lock Manager.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/netconfig.h>
#include <sys/proc.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/cmn_err.h>
#include <sys/pathname.h>
#include <sys/utsname.h>
#include <sys/flock.h>
#include <netinet/in.h>
#include <rpc/rpc.h>
#include <rpc/pmap_prot.h>
#include <rpc/rpcb_prot.h>
#include <nfs/nfs.h>
#include <nfs/nfssys.h>
#include <nfs/lm.h>

/* XXX should be in header */
extern kmutex_t delay_lock;	/* protects sleeping threads */

/*
 * Definitions and declarations.
 */
struct lm_svc_args	lm_sa;
struct lm_stat		lm_stat;
kmutex_t		lm_lck;
struct			lm_client;	/* forward reference only */

static struct lm_sleep *lm_sleeps = NULL;
static struct kmem_cache *lm_sleep_cache = NULL;

	/* unrecoverable RPC status codes; cf. rfscall() */
#define	IS_UNRECOVERABLE_RPC(s)	(((s) == RPC_AUTHERROR) || \
	((s) == RPC_CANTENCODEARGS) || \
	((s) == RPC_CANTDECODERES) || \
	((s) == RPC_VERSMISMATCH) || \
	((s) == RPC_PROCUNAVAIL) || \
	((s) == RPC_PROGUNAVAIL) || \
	((s) == RPC_PROGVERSMISMATCH) || \
	((s) == RPC_CANTDECODEARGS))

/*
 * When trying to contact a portmapper that doesn't speak the version we're
 * using, we should theoretically get back RPC_PROGVERSMISMATCH.
 * Unfortunately, some (all?) 4.x hosts return an accept_stat of
 * PROG_UNAVAIL, which gets mapped to RPC_PROGUNAVAIL, so we have to check
 * for that, too.
 */
#define	PMAP_WRONG_VERSION(s)	((s) == RPC_PROGVERSMISMATCH || \
	(s) == RPC_PROGUNAVAIL)

/*
 * Flag to indicate whether the struct kmem_cache's have been created yet.
 * Protected by lm_lck.
 */
bool_t lm_caches_created = FALSE;

/*
 * Function prototypes.
 */
#ifdef LM_DEBUG
static void lm_hex_byte(int);
#endif /* LM_DEBUG */
static void grow_netbuf(struct netbuf *nb, size_t length);
static void lm_put_inet_port(struct netbuf *, u_short port);
static void lm_put_loopback_port(struct netbuf *, char *port);
static bool_t lm_same_host(struct knetconfig *config1, struct netbuf *addr1,
		struct knetconfig *config2, struct netbuf *addr2);
static void lm_clnt_destroy(CLIENT **);
static void lm_rel_client(struct lm_client *, int error);
static enum clnt_stat lm_get_client(struct lm_sysid *, u_long prog,
		u_long vers, int timout, struct lm_client **, bool_t);
static enum clnt_stat lm_get_pmap_addr(struct knetconfig *, u_long prog,
		u_long vers, struct netbuf *);
static enum clnt_stat lm_get_rpc_handle(struct knetconfig *configp,
		struct netbuf *addrp, u_long prog, u_long vers,
		int ignore_signals, CLIENT **clientp);
static enum clnt_stat lm_get_rpcb_addr(struct knetconfig *, u_long prog,
		u_long vers, struct netbuf *);

/*
 * Debug routines.
 */
#ifdef LM_DEBUG
static void
lm_hex_byte(int n)
{
	static char hex[] = "0123456789ABCDEF";
	int i;

	printf(" ");
	if ((i = (n & 0xF0) >> 4) != 0)
		printf("%c", hex[i]);
	printf("%c", hex[n & 0x0F]);
}

void
lm_debug(level, function, str, i1, i2, i3, i4, i5, i6, i7)
	int level;
	char *function;
	char *str;
	int i1, i2, i3, i4, i5, i6, i7;
{
	if (lm_sa.debug >= level) {
		printf("%x %s:\t", curthread->t_did, function);
		printf(str, i1, i2, i3, i4, i5, i6, i7);
		printf("\n");
	}
}

/*
 * print an alock structure
 *
 * N.B. this is one of the routines that is duplicated for NLM4.  changes
 * maded in this version should be made in the other version as well.
 */
void
lm_alock(int level, char *function, nlm_lock *alock)
{
	int i;

	if (lm_sa.debug >= level) {
		printf("%x %s:\tcaller= %s, svid= %u, offset= %u, len= %u",
			curthread->t_did, function,
			alock->caller_name, alock->svid, alock->l_offset,
			alock->l_len);
		printf("\nfh=");

		for (i = 0;  i < alock->fh.n_len;  i++)
			lm_hex_byte(alock->fh.n_bytes[i]);
		printf("\n");
	}
}

/*
 * print a shareargs structure
 *
 * N.B. this is one of the routines that is duplicated for NLM4.  changes
 * maded in this version should be made in the other version as well.
 */
void
lm_d_nsa(int level, char *function, nlm_shareargs *nsa)
{
	int i;

	if (lm_sa.debug >= level) {
		printf("%x %s:\tcaller= %s, mode= %d, access= %d, reclaim= %d",
			curthread->t_did, function,
			nsa->share.caller_name, nsa->share.mode,
			nsa->share.access, nsa->reclaim);
		printf("\nfh=");

		for (i = 0;  i < nsa->share.fh.n_len;  i++)
			lm_hex_byte(nsa->share.fh.n_bytes[i]);
		printf("\noh=");

		for (i = 0;  i < nsa->share.oh.n_len;  i++)
			lm_hex_byte(nsa->share.oh.n_bytes[i]);
		printf("\n");
	}
}

void
lm_n_buf(int level, char *function, char *name, struct netbuf *addr)
{
	int	i;

	if (lm_sa.debug >= level) {
		printf("%x %s:\t%s=", curthread->t_did, function, name);
		if (! addr)
			printf("(NULL)\n");
		else {
			for (i = 0;  i < addr->len;  i++)
				lm_hex_byte(addr->buf[i]);
			printf(" (%d)\n", addr->maxlen);
		}
	}
}

/*
 * print an alock structure
 *
 * N.B. this is one of the routines that is duplicated for NLM4.  changes
 * maded in this version should be made in the other version as well.
 */
void
lm_alock4(int level, char *function, nlm4_lock *alock)
{
	int i;

	if (lm_sa.debug >= level) {
		printf("%x %s:\tcaller= %s, svid= %lu, offset= %llu, len= %llu",
			curthread->t_did, function,
			alock->caller_name, alock->svid, alock->l_offset,
			alock->l_len);
		printf("\nfh=");

		for (i = 0;  i < alock->fh.n_len;  i++)
				lm_hex_byte(alock->fh.n_bytes[i]);
		printf("\n");
	}
}

/*
 * print a shareargs structure
 *
 * N.B. this is one of the routines that is duplicated for NLM4.  changes
 * maded in this version should be made in the other version as well.
 */
void
lm_d_nsa4(int level, char *function, nlm4_shareargs *nsa)
{
	int i;

	if (lm_sa.debug >= level) {
		printf("%x %s:\tcaller= %s, mode= %d, access= %d, reclaim= %d",
			curthread->t_did, function,
			nsa->share.caller_name, nsa->share.mode,
			nsa->share.access, nsa->reclaim);
		printf("\nfh=");

		for (i = 0;  i < nsa->share.fh.n_len;  i++)
			lm_hex_byte(nsa->share.fh.n_bytes[i]);
		printf("\noh=");

		for (i = 0;  i < nsa->share.oh.n_len;  i++)
			lm_hex_byte(nsa->share.oh.n_bytes[i]);
		printf("\n");
	}
}
#endif /*  LM_DEBUG */

/*
 * Utilities
 */

/*
 * Append the number n to the string str.
 */
void
lm_numcat(char *str, int n)
{
	char d[12];
	int  i = 0;

	while (*str++)
		;
	str--;
	while (n > 9) {
		d[i++] = n % 10;
		n /= 10;
	}
	d[i] = n;

	do {
		*str++ = "0123456789"[d[i]];
	} while (i--);
	*str = 0;
}

/*
 * A useful utility.
 */
char *
lm_dup(char *str, int len)
{
	char *s = (char *) kmem_zalloc(len, KM_SLEEP);
	bcopy(str, s, len);
	return (s);
}

/*
 * We want a version of delay which is interruptible.
 * Return EINTR if an interrupt occured.
 */
int
lm_delay(long ticks)
{
	int	rc;
	long	time;

	if (ticks <= 0)
		return (0);

	lm_debu4(1, "delay", "ticks = %d", ticks);

	mutex_enter(&delay_lock);
	/*
	 * compute absolute time to expire
	 */
	time = lbolt + ticks;
	/*
	 * loop until cv_timedwait-sig returns because of a signal or timeout
	 * it returns 0 for a signal, -1 for a timeout, and the amount of
	 * time left on the timer otherwise
	 */
	do {
		rc = cv_timedwait_sig(&curthread->t_delay_cv, &delay_lock,
				time);
	} while (rc > 0);
	mutex_exit(&delay_lock);
	/*
	 * rc == 0 implies that a signal was sent
	 */
	if (rc == 0) {
		lm_debu3(1, "delay", "EINTR");
		return (EINTR);
	}

	lm_debu3(1, "delay", "exiting");
	return (0);
}

/*
 * Clean up and exit.
 *
 * XXX NCR porting issues:
 *	1. This routine should go away.  The only place it's called, lm_svc(),
 *		should return a failure code instead of `exiting.'
 */
void
lm_exit()
{
	lm_debu3(2, "lm_exit", "Exiting\n");

	exit(CLD_EXITED, 0);
}

/*
 * Utilities for manipulating netbuf's.
 * These utilities are the only knc_protofmly specific functions in the LM.
 *
 * Note that loopback addresses are not null-terminated, so these utilities
 * typically use the strn* string routines.
 */

/*
 * Utilities to patch a port number (for NC_INET protocols) or a
 *	port name (for NC_LOOPBACK) into a network address.
 */
static void
lm_put_inet_port(struct netbuf *addr, u_short port)
{
	/*
	 * Easy - we always patch an unsigned short on top of an
	 * unsigned short.  No changes to addr's len or maxlen are
	 * necessary.
	 */
	((struct sockaddr_in *)(addr->buf))->sin_port = port;
	lm_n_buf(8, "put_inet_port", "addr", addr);
}

static void
lm_put_loopback_port(struct netbuf *addr, char *port)
{
	char *dot;
	char *newbuf;
	int newlen;

	/*
	 * We must make sure the addr has enough space for us,
	 * patch in `port', and then adjust addr's len and maxlen
	 * to reflect the change.
	 */
	if ((dot = strnrchr(addr->buf, '.', addr->len)) == (char *)NULL) {
		lm_debu4(8, "put_loopb_port", "malformed loopback addr %s",
			(int)addr->buf);
		return;
	}

	newlen = (dot - addr->buf + 1) + strlen(port);
	if (newlen > addr->maxlen) {
		newbuf = (char *) kmem_zalloc(newlen, KM_SLEEP);
		(void) bcopy(addr->buf, newbuf, addr->len);
		kmem_free(addr->buf, addr->maxlen);
		addr->buf = newbuf;
		addr->len = addr->maxlen = newlen;
		dot = strnrchr(addr->buf, '.', addr->len);
	} else {
		addr->len = newlen;
	}

	(void) strncpy(++dot, port, strlen(port));

#if 0
	lm_debu6(8, "put_loopb_port", "addr = %s, len = %d, maxlen = %d",
		(int)addr->buf, addr->len, addr->maxlen);
#endif
}

/*
 * Convert a loopback universal address to a loopback transport address.
 */
static void
loopb_u2t(const char *ua, struct netbuf *addr)
{
	size_t stringlen = strlen(ua) + 1;
	const char *univp;		/* ptr into universal addr */
	char *transp;			/* ptr into transport addr */

	/* Make sure the netbuf will be big enough. */
	if (addr->maxlen < stringlen) {
		grow_netbuf(addr, stringlen);
	}

	univp = ua;
	transp = addr->buf;
	while (*univp != NULL) {
		if (*univp == '\\' && *(univp+1) == '\\') {
			*transp = '\\';
			univp += 2;
		} else if (*univp == '\\') {
			/* octal character */
			*transp = (((*(univp+1) - '0') & 3) << 6) +
				(((*(univp+2) - '0') & 7) << 3) +
				((*(univp+3) - '0') & 7);
			univp += 4;
		} else {
			*transp = *univp;
			univp++;
		}
		transp++;
	}

	addr->len = transp - addr->buf;
	ASSERT(addr->len <= addr->maxlen);
}

/*
 * Make sure the given netbuf has a maxlen at least as big as the given
 * length.
 */
static void
grow_netbuf(struct netbuf *nb, size_t length)
{
	char *newbuf;

	if (nb->maxlen >= length)
		return;

	newbuf = kmem_zalloc(length, KM_SLEEP);
	bcopy(nb->buf, newbuf, nb->len);
	kmem_free(nb->buf, nb->maxlen);
	nb->buf = newbuf;
	nb->maxlen = length;
}

/*
 * Returns a reference to the lm_sysid for myself.  This is a loopback
 * kernel RPC endpoint used to talk with our own statd.
 */
struct lm_sysid *
lm_get_me()
{
	struct knetconfig config;
	struct netbuf addr;
	char keyname[SYS_NMLN + 16];
	struct vnode *vp;
	struct lm_sysid *ls;

	config.knc_semantics = NC_TPI_CLTS;
	config.knc_protofmly = NC_LOOPBACK;
	config.knc_proto = NC_NOPROTO;
	if (lookupname("/dev/ticlts", UIO_SYSSPACE,
			FOLLOW, NULLVPP, &vp) != 0) {
		/*
		 * XXX: Should handle catching a signal more
		 * gracefully than this (though doing so
		 * properly would take substantial effort).
		 */
		cmn_err(CE_PANIC, "lm_get_me: no ticlts");
		/*NOTREACHED*/
	}
	config.knc_rdev = vp->v_rdev;
	VN_RELE(vp);

	/*
	 * Get a unique (node,service) name from which we
	 * build up a netbuf.
	 */
	(void) strcpy(keyname, utsname.nodename);
	(void) strcat(keyname, ".");
	keyname[strlen(utsname.nodename) + 1] = NULL;	/* is strcat broken? */
	addr.buf = keyname;
	addr.len = addr.maxlen = strlen(keyname);

	lm_debu4(8, "get_me", "addr = %s", (int)addr.buf);

	rw_enter(&lm_sysids_lock, RW_READER);
	ls = lm_get_sysid(&config, &addr, "me", TRUE, NULL);
	rw_exit(&lm_sysids_lock);
	return (ls);
}

/*
 * Both config and addr must be the same.
 * Comparison of addr's must be done config dependent.
 */
static bool_t
lm_same_host(struct knetconfig *config1, struct netbuf *addr1,
		struct knetconfig *config2, struct netbuf *addr2)
{
	struct sockaddr_in *si1;
	struct sockaddr_in *si2;
	int namelen1, namelen2;
	char *dot;

	lm_n_buf(9, "same_host", "addr1", addr1);
	lm_n_buf(9, "same_host", "addr2", addr2);
	lm_debu7(9, "same_host", "fmly1= %s, rdev1= %x, fmly2= %s, rdev2= %x",
	    (int)config1->knc_protofmly, config1->knc_rdev,
	    (int)config2->knc_protofmly, config2->knc_rdev);

	if (strcmp(config1->knc_protofmly, config2->knc_protofmly) != 0 ||
	    strcmp(config1->knc_proto, config2->knc_proto) != 0 ||
	    config1->knc_rdev != config2->knc_rdev) {
		return (FALSE);
	}

	if (strcmp(config1->knc_protofmly, NC_INET) == 0) {
		si1 = (struct sockaddr_in *)(addr1->buf);
		si2 = (struct sockaddr_in *)(addr2->buf);
		if (si1->sin_family != si2->sin_family ||
		    si1->sin_addr.s_addr != si2->sin_addr.s_addr) {
			return (FALSE);
		}
	} else if (strcmp(config1->knc_protofmly, NC_LOOPBACK) == 0) {
		dot = strnrchr(addr1->buf, '.', addr1->len);
		ASSERT(dot != NULL);
		namelen1 = dot - addr1->buf;
		dot = strnrchr(addr2->buf, '.', addr2->len);
		ASSERT(dot != NULL);
		namelen2 = dot - addr2->buf;
		if (namelen1 != namelen2 || namelen1 <= 0 ||
		    strncmp(addr1->buf, addr2->buf, namelen1)) {
			return (FALSE);
		}
	} else {
		lm_debu4(1, "same_host", "UNSUPPORTED PROTO FAMILY %s",
			(int)config1->knc_protofmly);
			return (FALSE);
	}

	return (TRUE);
}

/*
 * The lm_sysids list is a cache of system IDs for which we have built RPC
 * client handles (either as client or server).  It is protected by
 * lm_sysids_lock.  See the definition of struct lm_sysid for more details.
 */
struct lm_sysid *lm_sysids = NULL;
krwlock_t lm_sysids_lock;
static struct kmem_cache *lm_sysid_cache = NULL;
unsigned int lm_sysid_len = 0;

/*
 * lm_get_sysid
 * Returns a reference to the sysid associated with the knetconfig
 * and address.
 *
 * If name == NULL then set name to "NoName".
 * If alloc == FALSE and the entry can't be found in the cache, don't
 * allocate a new one; panic instead (caller says it must be there.)
 * Callers that pass alloc == FALSE expect that lm_sysids_lock cannot
 * be dropped without panicking.
 * If dropped_read_lock != NULL, *dropped_read_lock is set to TRUE
 * if the lock was dropped else FALSE.  If dropped_read_lock == NULL,
 * there is no way to convey this information to the caller (which
 * therefore must assume that lm_sysids may be changed after the call.)
 *
 * All callers must hold lm_sysids_lock as a reader.  If alloc is TRUE
 * and the entry can't be found, this routine upgrades the lock to a
 * writer lock, inserts the entry into the list, and downgrades the
 * lock back to a reader lock.
 */
struct lm_sysid *
lm_get_sysid(struct knetconfig *config, struct netbuf *addr, char *name,
	bool_t alloc, bool_t *dropped_read_lock)
{
	struct lm_sysid *ls;
	sysid_t	i;
	bool_t writer = FALSE;

	lm_debu7(3, "get_sysid", "config= %x addr= %x name= %s alloc= %s",
		(int)config, (int)addr, (int)name,
		(int)((alloc == TRUE) ? "TRUE" : "FALSE"));

	/*
	 * We can't verify that caller has lm_sysids_lock as a reader the
	 * way we'd like to, but at least we can assert that somebody does.
	 */
	ASSERT(RW_READ_HELD(&lm_sysids_lock));
	if (dropped_read_lock != NULL)
		*dropped_read_lock = FALSE;

	/*
	 * Try to find an existing lm_sysid that contains what we're
	 * looking for.  Keep track of the highest sysid value that we've
	 * seen, so that we can generate a unique one if we have to.
	 */
start:
	i = LM_SYSID;
	for (ls = lm_sysids; ls; ls = ls->next) {
		mutex_enter(&ls->lock);
		ASSERT(ls->refcnt >= 0);
		if (ls->sysid > i) {
			i = ls->sysid;
		}
		if (lm_same_host(config, addr, &ls->config, &ls->addr)) {
			ls->refcnt++;
			mutex_exit(&ls->lock);
			if (writer == TRUE) {
				rw_downgrade(&lm_sysids_lock);
			}
			return (ls);
		} else {
			mutex_exit(&ls->lock);
		}
	}

	if (i == LM_SYSID_MAX) {
		/* should never happen */
		cmn_err(CE_PANIC, "lm_get_sysid: too many lm_sysid's");
	}
	if (alloc == FALSE) {
		cmn_err(CE_PANIC, "lm_get_sysid: cached entry not found");

	}

	i++;

	/*
	 * It's necessary to get write access to the lm_sysids list here.
	 * Since we already own a READER lock, we acquire the WRITER lock
	 * with some care.
	 *
	 * In particular, if we fail to upgrade to writer immediately, there
	 * is already a writer or there are one or more other threads that
	 * are already blocking waiting to become writers.  In this case,
	 * we wait to acquire the writer lock, *go back and recompute i*,
	 * and then add our new entry onto the list.  The next time past
	 * here we're already a writer, so we skip this stuff altogether.
	 */
	if (writer == FALSE) {
		if (rw_tryupgrade(&lm_sysids_lock) == 0) {
			rw_exit(&lm_sysids_lock);
			if (dropped_read_lock != NULL) {
				*dropped_read_lock = TRUE;
			}
			rw_enter(&lm_sysids_lock, RW_WRITER);
			writer = TRUE;
			goto start;
		} else {
			if (dropped_read_lock != NULL) {
				*dropped_read_lock = FALSE;
			}
		}
	}

	/*
	 * We have acquired the WRITER lock.  Create and add a
	 * new lm_sysid to the list.
	 */
	ls = kmem_cache_alloc(lm_sysid_cache, KM_SLEEP);
	mutex_init(&ls->lock, "lm_sysid lock", MUTEX_DEFAULT, DEFAULT_WT);
	ls->config.knc_semantics = config->knc_semantics;
	ls->config.knc_protofmly = lm_dup(config->knc_protofmly,
					strlen(config->knc_protofmly) + 1);
	ls->config.knc_proto = (config->knc_proto ?
		lm_dup(config->knc_proto, strlen(config->knc_proto) + 1) :
		NULL);
	ls->config.knc_rdev = config->knc_rdev;
	ls->addr.buf = lm_dup(addr->buf, addr->maxlen);
	ls->addr.len = addr->len;
	ls->addr.maxlen = addr->maxlen;
	ls->name = name ? lm_dup(name, strlen(name) + 1) : "NoName";
	ls->sysid = i;
	ls->sm_client = FALSE;
	ls->sm_server = FALSE;
	ls->in_recovery = FALSE;
	ls->refcnt = 1;
	ls->next = lm_sysids;
	lm_sysids = ls;
	lm_sysid_len++;

	lm_debu6(3, "get_sysid", "name= %s, sysid= %x, sysids= %d",
		(int)ls->name, ls->sysid, lm_sysid_len);
	lm_debu6(3, "get_sysid", "semantics= %d protofmly= %s proto= %s",
		(int)ls->config.knc_semantics,
		(int)ls->config.knc_protofmly,
		(int)ls->config.knc_proto);

	/*
	 * Make sure we return still holding just the READER lock.
	 */
	rw_downgrade(&lm_sysids_lock);
	return (ls);
}

/*
 * Increment the reference count for an lm_sysid.
 */

void
lm_ref_sysid(struct lm_sysid *ls)
{
	mutex_enter(&ls->lock);

	/*
	 * Most callers should already have a reference to the lm_sysid.
	 * Some routines walk the lm_sysids list, though, in which case the
	 * reference count could be zero.
	 */
	ASSERT(ls->refcnt >= 0);

	ls->refcnt++;

	mutex_exit(&ls->lock);
}

/*
 * Release the reference to an lm_sysid.  If the reference count goes to
 * zero, the lm_sysid is left around in case it will be used later.
 */

void
lm_rel_sysid(struct lm_sysid *ls)
{
	mutex_enter(&ls->lock);

	ls->refcnt--;
	ASSERT(ls->refcnt >= 0);

	mutex_exit(&ls->lock);
}

/*
 * Try to free all the lm_sysid's that we have registered.  In-use entries
 * are left alone.
 */
void
lm_free_sysid_table()
{
	struct lm_sysid *ls;
	struct lm_sysid *nextls = NULL;
	struct lm_sysid *prevls = NULL;	/* previous kept element */

	/*
	 * Free all the lm_client's that are unused.  This must be done
	 * first, so that they drop their references to the lm_sysid's.
	 */
	lm_flush_clients_mem();

	rw_enter(&lm_sysids_lock, RW_WRITER);

	lm_debu4(5, "free_sysid", "start length: %d\n", lm_sysid_len);

	for (ls = lm_sysids; ls != NULL; ls = nextls) {
		mutex_enter(&ls->lock);
		ASSERT(ls->refcnt >= 0);
		nextls = ls->next;

		if (ls->refcnt > 0 || flk_sysid_has_locks(ls->sysid) ||
		    flk_sysid_has_locks(ls->sysid | LM_SYSID_CLIENT)) {
			/* can't free now */
			lm_debu6(6, "free_sysid", "%x (%s) kept (ref %d)\n",
			    ls->sysid, (int)ls->name, ls->refcnt);
			prevls = ls;
			mutex_exit(&ls->lock);
		} else {
			lm_debu5(6, "free_sysid", "%x (%s) freed\n",
			    ls->sysid, (int)ls->name);
			if (prevls == NULL) {
				lm_sysids = nextls;
			} else {
				prevls->next = nextls;
			}
			ASSERT(lm_sysid_len != 0);
			--lm_sysid_len;
			kmem_free(ls->config.knc_protofmly,
			    strlen(ls->config.knc_protofmly) + 1);
			kmem_free(ls->config.knc_proto,
			    strlen(ls->config.knc_proto) + 1);
			kmem_free(ls->addr.buf, ls->addr.maxlen);
			kmem_free(ls->name, strlen(ls->name) + 1);
			mutex_destroy(&ls->lock);
			kmem_cache_free(lm_sysid_cache, ls);
		}
	}

	lm_debu4(5, "free_sysid", "end length: %d\n", lm_sysid_len);
	rw_exit(&lm_sysids_lock);
}

/*
 * lm_configs is a null-terminated (next == NULL) list protected by lm_lck.
 * lm_numconfigs is the number of elements in the list.
 */
static struct lm_config *lm_configs = NULL;
unsigned int lm_numconfigs = 0;
static struct kmem_cache *lm_config_cache = NULL;

/*
 * number of outstanding NLM requests Protected by lm_lck.
 */
unsigned int lm_num_outstanding = 0;

/*
 * Save the knetconfig information that was passed down to us from
 * userland during the _nfssys(LM_SVC) call.  This is used by the server
 * code ONLY to map fp -> config.
 */
struct lm_config *
lm_saveconfig(struct file *fp, struct knetconfig *config)
{
	struct lm_config *ln;

	lm_debu7(7, "saveconfig",
		"fp= %x semantics= %d protofmly= %s proto= %s",
		(int)fp,
		config->knc_semantics,
		(int)config->knc_protofmly,
		(int)config->knc_proto);

	mutex_enter(&lm_lck);
	for (ln = lm_configs; ln; ln = ln->next) {
		if (ln->fp == fp) {	/* happens with multiple svc threads */
			mutex_exit(&lm_lck);
			lm_debu4(7, "saveconfig", "found ln= %x", (int)ln);
			return (ln);
		}
	}

	ln = kmem_cache_alloc(lm_config_cache, KM_SLEEP);
	ln->fp = fp;
	ln->config = *config;
	ln->next = lm_configs;
	lm_configs = ln;
	++lm_numconfigs;
	mutex_exit(&lm_lck);

	lm_debu6(7, "saveconfig", "ln= %x fp= %x next= %x",
		(int)ln, (int)fp, (int)ln->next);
	return (ln);
}

/*
 * Fetch lm_config corresponding to an fp.  Server code only.
 */
struct lm_config *
lm_getconfig(struct file *fp)
{
	struct lm_config *ln;

	lm_debu4(7, "getconfig", "fp= %x", (int)fp);

	mutex_enter(&lm_lck);
	for (ln = lm_configs; ln != NULL; ln = ln->next) {
		if (ln->fp == fp)
			break;
	}
	mutex_exit(&lm_lck);

	lm_debu4(7, "getconfig", "ln= %x", (int)ln);
	return (ln);
}

/*
 * Remove an entry from the config table and decrement the config count.
 * This routine does not return the number of remaining entries, because
 * the caller probably wants to check it while holding lm_lck.
 */
void
lm_rmconfig(struct file *fp)
{
	struct lm_config *ln;
	struct lm_config *prev_ln;

	lm_debu4(7, "rmconfig", "fp=%x", (int)fp);

	mutex_enter(&lm_lck);

	for (ln = lm_configs, prev_ln = NULL; ln != NULL; ln = ln->next) {
		if (ln->fp == fp)
			break;
		prev_ln = ln;
	}
	if (ln == NULL) {
#ifdef DEBUG
		cmn_err(CE_WARN,
		    "lm_rmconfig: couldn't find config for fp 0x%x\n",
		    (int)fp);
#endif
	} else {
		lm_debu4(7, "rmconfig", "ln=%x", (int)ln);
		if (prev_ln == NULL) {
			lm_configs = ln->next;
		} else {
			prev_ln->next = ln->next;
		}
		--lm_numconfigs;
		/*
		 * no need to call lm_clear_knetconfig, because all of its
		 * strings are statically allocated.
		 */
		kmem_cache_free(lm_config_cache, ln);
	}

	mutex_exit(&lm_lck);
}

/*
 * When an entry in the lm_client cache is released, it is just marked
 * unused.  If space is tight, it can be freed later.  Because client
 * handles are potentially expensive to keep around, we try to reuse old
 * lm_client entries only if there are lm_max_clients entries or more
 * allocated.
 *
 * If time == 0, the client handle is not valid.
 */
struct lm_client {
	struct lm_client *next;
	struct lm_sysid *sysid;
	u_long prog;
	u_long vers;
	struct netbuf addr;	/* Address to this <prog,vers> on sysid */
	time_t time;		/* In seconds */
	CLIENT *client;
	bool_t in_use;
};
static struct lm_client *lm_clients = NULL; /* protected by lm_lck */
int lm_max_clients = 10;
static struct kmem_cache *lm_client_cache = NULL;


/*
 * We need an version of CLNT_DESTROY which also frees the auth structure.
 */
static void
lm_clnt_destroy(CLIENT **clp)
{
	if (*clp) {
		if ((*clp)->cl_auth) {
			AUTH_DESTROY((*clp)->cl_auth);
			(*clp)->cl_auth = NULL;
		}
		CLNT_DESTROY(*clp);
		*clp = NULL;
	}
}

/*
 * Release this lm_client entry.
 * Do also destroy the entry if there was an error != EINTR,
 * and mark the entry as not-valid, by setting time=0.
 */
static void
lm_rel_client(struct lm_client *lc, int error)
{
	lm_debu6(7, "rel_clien", "addr = (%x, %d %d)\n",
	    (int)lc->addr.buf, lc->addr.len, lc->addr.maxlen);
	if (error && error != EINTR) {
		lm_debu6(7, "rel_clien", "destroying addr = (%x, %d %d)\n",
			(int)lc->addr.buf, lc->addr.len,
			lc->addr.maxlen);
		lm_clnt_destroy(&lc->client);
		if (lc->addr.buf) {
			kmem_free(lc->addr.buf, lc->addr.maxlen);
			lc->addr.buf = NULL;
		}
		lc->time = 0;
	}
	lc->in_use = FALSE;
}

/*
 * Return a lm_client to the <ls,prog,vers>.
 * The lm_client found is marked as in_use.
 * It is the responsibility of the caller to release the lm_client by
 * calling lm_rel_client().
 *
 * Returns:
 * RPC_SUCCESS		Success.
 * RPC_CANTSEND		Temporarily cannot send to this sysid.
 * RPC_TLIERROR		Unspecified TLI error.
 * RPC_UNKNOWNPROTO	ls->config is from an unrecognised protocol family.
 * RPC_PROGNOTREGISTERED The NLM prog `prog' isn't registered on the server.
 * RPC_RPCBFAILURE	Couldn't contact portmapper on remote host.
 * Any unsuccessful return codes from CLNT_CALL().
 */
static enum clnt_stat
lm_get_client(struct lm_sysid *ls, u_long prog, u_long vers, int timout,
		struct lm_client **lcp, bool_t ignore_signals)
{
	struct lm_client *lc = NULL;
	struct lm_client *lc_old = NULL;
	enum clnt_stat status = RPC_SUCCESS;

	mutex_enter(&lm_lck);

	/*
	 * Prevent client-side lock manager from obtaining handles to a
	 * server during its crash recovery, e.g. so that one of our client
	 * processes can't attempt to release a lock that the kernel is
	 * busy trying to reclaim for him.
	 */
	if (ls->in_recovery && ttolwp(curthread) != NULL) {
		mutex_exit(&lm_lck);
		lm_debu5(7, "get_client",
			"ls= %x not used during crash recovery of %s",
			(int)ls, (int)ls->name);
		status = RPC_CANTSEND;
		goto out;
	}

	/*
	 * Search for an lm_client that is free, valid, and matches.
	 */
	for (lc = lm_clients; lc; lc = lc->next) {
		if (! lc->in_use) {
			if (lc->time && lc->sysid == ls && lc->prog  == prog &&
			    lc->vers  == vers) {
				/* Found a valid and matching lm_client. */
				break;
			} else if ((! lc_old) || (lc->time < lc_old->time)) {
				/* Possibly reuse this one. */
				lc_old = lc;
			}
		}
	}

	lm_debu6(7, "get_client", "Found lc= %x, lc_old= %x, timout= %d",
		(int)lc, (int)lc_old, timout);

	if (! lc) {
		/*
		 * We did not find an entry to use.
		 * Decide if we should reuse lc_old or create a new entry.
		 */
		if ((! lc_old) || (lm_stat.client_len < lm_max_clients)) {
			/*
			 * No entry to reuse, or we are allowed to create
			 * extra.
			 */
			lm_stat.client_len++;
			lc = kmem_cache_alloc(lm_client_cache, KM_SLEEP);
			lc->time = 0;
			lc->client = NULL;
			lc->next = lm_clients;
			lc->sysid = NULL;
			lm_clients = lc;
		} else {
			lm_rel_client(lc_old, EINVAL);
			lc = lc_old;
		}
		if (lc->sysid != NULL) {
			lm_rel_sysid(lc->sysid);
		}
		lc->sysid = ls;
		lm_ref_sysid(ls);
		lc->prog = prog;
		lc->vers = vers;
		lc->addr.buf = lm_dup(ls->addr.buf, ls->addr.maxlen);
		lc->addr.len = ls->addr.len;
		lc->addr.maxlen = ls->addr.maxlen;
	}
	lc->in_use = TRUE;

	mutex_exit(&lm_lck);

	lm_n_buf(7, "get_client", "addr", &lc->addr);

	/*
	 * If timout == 0 then one way RPC calls are used, and the CLNT_CALL
	 * will always return RPC_TIMEDOUT. Thus we will never know whether
	 * a client handle is still OK. Therefore don't use the handle if
	 * time is older than lm_sa.timout. Note, that lm_sa.timout == 0
	 * disables the client cache for one way RPC-calls.
	 */
	if (timout == 0) {
		if (lm_sa.timout <= time - lc->time) {	/* Invalidate? */
			lc->time = 0;
		}
	}

	if (lc->time == 0) {
		status = lm_get_rpc_handle(&ls->config, &lc->addr, prog, vers,
					ignore_signals, &lc->client);
		if (status != RPC_SUCCESS)
			goto out;
		lc->time = time;
	} else {
		/*
		 * Consecutive calls to CLNT_CALL() on the same client handle
		 * get the same transaction ID.  We want a new xid per call,
		 * so we first reinitialise the handle.
		 */
		(void) clnt_tli_kinit(lc->client, &ls->config, &lc->addr,
				lc->addr.maxlen, 0, CRED());
	}

out:
	lm_debu8(7, "get_client",
	    "End: lc= %x status= %d, time= %x, client= %x, clients= %d",
	    (int)lc,
	    status,
	    (lc ? (int)lc->time : -1),
	    (lc ? (int)lc->client : -1),
	    lm_stat.client_len);

	if (status == RPC_SUCCESS) {
		*lcp = lc;
	} else {
		if (lc) {
			mutex_enter(&lm_lck);
			lm_rel_client(lc, EINVAL);
			mutex_exit(&lm_lck);
		}
		*lcp = NULL;
	}

	return (status);
}

/*
 * Get the RPC client handle to talk to the service at addrp.
 * Returns:
 * RPC_SUCCESS		Success.
 * RPC_RPCBFAILURE	Couldn't talk to the remote portmapper (e.g.,
 * 			timeouts).
 * RPC_INTR		Caught a signal before we could successfully return.
 * RPC_TLIERROR		Couldn't initialize the handle after talking to the
 * 			remote portmapper (shouldn't happen).
 */

static enum clnt_stat
lm_get_rpc_handle(struct knetconfig *configp, struct netbuf *addrp,
		u_long prog, u_long vers, int ignore_signals,
		CLIENT **clientp /* OUT */)
{
	enum clnt_stat status;
	k_sigset_t oldmask;
	k_sigset_t newmask;
	int error;

	/*
	 * It's not clear whether this function should have a retry loop,
	 * as long as things like a portmapper timeout cause the
	 * higher-level code to retry.
	 */

	/*
	 * Try to get the address from either portmapper or rpcbind.
	 * We check for posted signals after trying and failing to
	 * contact the portmapper since it can take uncomfortably
	 * long for this entire procedure to time out.
	 */
	status = lm_get_pmap_addr(configp, prog, vers, addrp);
	if (IS_UNRECOVERABLE_RPC(status) &&
	    status != RPC_UNKNOWNPROTO &&
	    !PMAP_WRONG_VERSION(status)) {
		status = RPC_RPCBFAILURE;
		goto bailout;
	}

	if (!ignore_signals && lm_sigispending()) {
		lm_debu4(7, "get_rpc_handle", "posted signal, RPC stat= %d",
		    status);
		status = RPC_INTR;
		goto bailout;
	}

	if (status != RPC_SUCCESS) {
		status = lm_get_rpcb_addr(configp, prog, vers, addrp);
		if (status != RPC_SUCCESS) {
			lm_debu3(7, "get_rpc_handle",
				"can't contact portmapper or rpcbind");
			status = RPC_RPCBFAILURE;
			goto bailout;
		}
	}

	lm_clnt_destroy(clientp);

	/*
	 * Mask signals for the duration of the handle creation,
	 * allowing relatively normal operation with a signal
	 * already posted to our thread (e.g., when we are
	 * sending an NLM_CANCEL in response to catching a signal).
	 *
	 * Any further exit paths from this routine must restore
	 * the original signal mask.
	 */
	sigfillset(&newmask);
	sigreplace(&newmask, &oldmask);
	if ((error = clnt_tli_kcreate(configp, addrp, prog,
			vers, 0, 0, CRED(), clientp)) != 0) {
		status = RPC_TLIERROR;
		sigreplace(&oldmask, (k_sigset_t *)NULL);
		lm_debu4(7, "get_client", "kcreate(prog) returned %d",
			error);
		goto bailout;
	}
	sigreplace(&oldmask, (k_sigset_t *)NULL);

bailout:
	return (status);
}

/*
 * Try to get the address for the desired service by using the old
 * portmapper protocol.  Ignores signals.
 *
 * Returns RPC_UNKNOWNPROTO if the request uses the loopback transport.
 * Use lm_get_rpcb_addr instead.
 */

static enum clnt_stat
lm_get_pmap_addr(struct knetconfig *config, u_long prog, u_long vers,
		struct netbuf *addr)
{
	u_short	port = 0;
	int error;
	enum clnt_stat status;
	CLIENT *client = NULL;
	struct pmap parms;
	struct timeval tmo;
	k_sigset_t oldmask;
	k_sigset_t newmask;

	/*
	 * Call rpcbind version 2 or earlier (SunOS portmapper, remote
	 * only) to get an address we can use in an RPC client handle.
	 * We simply obtain a port no. for <prog, vers> and plug it
	 * into `addr'.
	 */
	if (strcmp(config->knc_protofmly, NC_INET) == 0) {
		lm_put_inet_port(addr, htons(PMAPPORT));
	} else {
		lm_debu4(7, "get_pmap_addr", "unsupported protofmly %s",
			(int)config->knc_protofmly);
		status = RPC_UNKNOWNPROTO;
		goto out;
	}

	lm_debu9(7, "get_pmap_addr",
	"semantics= %d, protofmly= %s, proto= %s, buf= %s, len= %d, maxlen= %d",
		config->knc_semantics,
		(int)config->knc_protofmly,
		(int)config->knc_proto,
		(int)addr->buf,
		addr->len,
		addr->maxlen);

	/*
	 * Mask signals for the duration of the handle creation and
	 * RPC call.  This allows relatively normal operation with a
	 * signal already posted to our thread (e.g., when we are
	 * sending an NLM_CANCEL in response to catching a signal).
	 *
	 * Any further exit paths from this routine must restore
	 * the original signal mask.
	 */
	sigfillset(&newmask);
	sigreplace(&newmask, &oldmask);

	if ((error = clnt_tli_kcreate(config, addr, PMAPPROG,
			PMAPVERS, 0, 0, CRED(), &client)) != RPC_SUCCESS) {
		status = RPC_TLIERROR;
		sigreplace(&oldmask, (k_sigset_t *)NULL);
		lm_debu4(7, "get_pmap_addr", "kcreate() returned %d", error);
		goto out;
	}

	parms.pm_prog = prog;
	parms.pm_vers = vers;
	if (strcmp(config->knc_proto, NC_TCP) == 0) {
		parms.pm_prot = IPPROTO_TCP;
	} else {
		parms.pm_prot = IPPROTO_UDP;
	}
	parms.pm_port = 0;
	tmo.tv_sec = LM_PMAP_TIMEOUT;
	tmo.tv_usec = 0;

	if ((status = CLNT_CALL(client, PMAPPROC_GETPORT,
				xdr_pmap, (char *)&parms,
				xdr_u_short, (char *)&port,
				tmo)) != RPC_SUCCESS) {
		sigreplace(&oldmask, (k_sigset_t *)NULL);
		lm_debu4(7, "get_pmap_addr", "CLNT_CALL(GETPORT) returned %d",
			status);
		goto out;
	}

	sigreplace(&oldmask, (k_sigset_t *)NULL);

	lm_debu4(7, "get_pmap_addr", "port= %d", port);
	lm_put_inet_port(addr, ntohs(port));

out:
	if (client)
		lm_clnt_destroy(&client);
	return (status);
}

/*
 * Try to get the address for the desired service by using the rpcbind
 * protocol.  Ignores signals.
 */

static enum clnt_stat
lm_get_rpcb_addr(struct knetconfig *config, u_long prog, u_long vers,
		struct netbuf *addr)
{
	int error;
	char *ua = NULL;
	enum clnt_stat status;
	RPCB parms;
	struct timeval tmo;
	CLIENT *client = NULL;
	k_sigset_t oldmask;
	k_sigset_t newmask;
	u_short port;

	/*
	 * Call rpcbind (local or remote) to get an address we can use
	 * in an RPC client handle.
	 */
	tmo.tv_sec = LM_PMAP_TIMEOUT;
	tmo.tv_usec = 0;
	parms.r_prog = prog;
	parms.r_vers = vers;
	parms.r_addr = parms.r_owner = "";

	if (strcmp(config->knc_protofmly, NC_INET) == 0) {
		if (strcmp(config->knc_proto, NC_TCP) == 0) {
			parms.r_netid = "tcp";
		} else {
			parms.r_netid = "udp";
		}
		lm_put_inet_port(addr, htons(PMAPPORT));
	} else if (strcmp(config->knc_protofmly, NC_LOOPBACK) == 0) {
		parms.r_netid = "ticlts";
		lm_put_loopback_port(addr, "rpc");
		lm_debu6(7, "get_rpcb_addr",
			"semantics= %s, protofmly= %s, proto= %s",
			(int)(config->knc_semantics == NC_TPI_CLTS ?
				"NC_TPI_CLTS" : "?"),
			(int)config->knc_protofmly,
			(int)config->knc_proto);
		lm_n_buf(7, "get_rpcb_addr", "addr", addr);
	} else {
		lm_debu4(7, "get_rpcb_addr", "unsupported protofmly %s",
			(int)config->knc_protofmly);
		status = RPC_UNKNOWNPROTO;
		goto out;
	}

	/*
	 * Mask signals for the duration of the handle creation and
	 * RPC calls.  This allows relatively normal operation with a
	 * signal already posted to our thread (e.g., when we are
	 * sending an NLM_CANCEL in response to catching a signal).
	 *
	 * Any further exit paths from this routine must restore
	 * the original signal mask.
	 */
	sigfillset(&newmask);
	sigreplace(&newmask, &oldmask);

	if ((error = clnt_tli_kcreate(config, addr, RPCBPROG,
			RPCBVERS, 0, 0, CRED(), &client)) != 0) {
		status = RPC_TLIERROR;
		sigreplace(&oldmask, (k_sigset_t *)NULL);
		lm_debu4(7, "get_rpcb_addr", "kcreate() returned %d", error);
		goto out;
	}

	if ((status = CLNT_CALL(client, RPCBPROC_GETADDR,
				xdr_rpcb, (char *)&parms,
				xdr_wrapstring, (char *)&ua,
				tmo)) != RPC_SUCCESS) {
		sigreplace(&oldmask, (k_sigset_t *)NULL);
		lm_debu4(7, "get_rpcb_addr", "CLNT_CALL(GETADDR) returned %d",
			status);
		goto out;
	}

	sigreplace(&oldmask, (k_sigset_t *)NULL);

	if (ua == NULL || *ua == NULL) {
		status = RPC_PROGNOTREGISTERED;
		lm_debu3(7, "get_rpcb_addr", "program not registered");
		goto out;
	}

	/*
	 * Convert the universal address to the transport address.
	 * Theoretically, we should call the local rpcbind to translate
	 * from the universal address to the transport address, but it gets
	 * complicated (e.g., there's no direct way to tell rpcbind that we
	 * want an IP address instead of a loopback address).  Note that
	 * the transport address is potentially host-specific, so we can't
	 * just ask the remote rpcbind, because it might give us the wrong
	 * answer.
	 */
	if (strcmp(config->knc_protofmly, NC_INET) == 0) {
		port = rpc_uaddr2port(ua);
		lm_put_inet_port(addr, ntohs(port));
	} else if (strcmp(config->knc_protofmly, NC_LOOPBACK) == 0) {
		loopb_u2t(ua, addr);
		lm_n_buf(7, "get_rpcb_addr", "taddr", addr);
	} else {
		/* "can't happen" - should have been checked for above */
		cmn_err(CE_PANIC, "lm_get_rpcb_addr: bad protocol family");
	}

out:
	if (client != NULL)
		lm_clnt_destroy(&client);
	if (ua != NULL)
		xdr_free(xdr_wrapstring, (char *)&ua);
	return (status);
}

/*
 * Free all RPC client-handles for the machine ls.  If ls is NULL, free all
 * RPC client handles.
 */
void
lm_flush_clients(struct lm_sysid  *ls)
{
	struct lm_client *lc;

	mutex_enter(&lm_lck);

	for (lc = lm_clients; lc; lc = lc->next) {
		lm_debu5(1, "flush_clients", "flushing lc %x, in_use %d",
			(int)lc, lc->in_use);
		if (! lc->in_use)
			if ((! ls) || (lc->sysid == ls))
				lm_rel_client(lc, EINVAL);
	}
	mutex_exit(&lm_lck);
}

/*
 * Try to free all lm_client objects, their RPC handles, and their
 * associated memory.  In-use entries are left alone.
 */
void
lm_flush_clients_mem()
{
	struct lm_client *lc;
	struct lm_client *nextlc = NULL;
	struct lm_client *prevlc = NULL; /* previous kept element */

	mutex_enter(&lm_lck);

	for (lc = lm_clients; lc; lc = nextlc) {
		nextlc = lc->next;
		if (lc->in_use) {
			prevlc = lc;
		} else {
			if (prevlc == NULL) {
				lm_clients = nextlc;
			} else {
				prevlc->next = nextlc;
			}
			--lm_stat.client_len;
			lm_clnt_destroy(&lc->client);
			if (lc->addr.buf) {
				kmem_free(lc->addr.buf, lc->addr.maxlen);
			}
			lm_rel_sysid(lc->sysid);
			kmem_cache_free(lm_client_cache, lc);
		}
	}
	mutex_exit(&lm_lck);
}

/*
 * Make an RPC call to addr via config.
 *
 * Returns:
 * 0		Success.
 * EIO		Couldn't get client handle, timed out, or got unexpected
 *		RPC status within LM_RETRY attempts.
 * EINVAL	Unrecoverable error in RPC call.  Causes client handle
 *		to be destroyed.
 * EINTR	RPC call was interrupted within LM_RETRY attempts.
 */
int
lm_callrpc(struct lm_sysid *ls, u_long prog, u_long vers, u_long proc,
	xdrproc_t inproc, caddr_t in, xdrproc_t outproc, caddr_t out,
	int timout, int tries)
{
	struct timeval tmo;
	struct lm_client *lc = NULL;
	enum clnt_stat stat;
	int error;
	int signalled;
	int iscancel;
	k_sigset_t oldmask;
	k_sigset_t newmask;

	lm_debu9(6, "callrpc", "Calling [%lu, %lu, %lu] on '%s' (%x) via '%s'",
		prog, vers, proc, (int)ls->name, ls->sysid,
		(int)ls->config.knc_proto);

	tmo.tv_sec = timout;
	tmo.tv_usec = 0;
	signalled = 0;
	sigfillset(&newmask);
	iscancel = (prog == NLM_PROG &&
		    (proc == NLM_CANCEL || proc == NLMPROC4_CANCEL));

	while (tries--) {
		/*
		 * If any signal has been posted to our (user) thread,
		 * bail out as quickly as possible.  The exception is
		 * if we are doing any type of CANCEL:  in that case,
		 * we may already have a posted signal and we need to
		 * live with it.
		 */
		if (lm_sigispending()) {
			lm_debu3(6, "callrpc", "posted signal");
			if (iscancel == 0) {
				error = EINTR;
				break;
			}
		}

		error = 0;
		stat = lm_get_client(ls, prog, vers, timout, &lc, iscancel);
		if (IS_UNRECOVERABLE_RPC(stat)) {
			error = EINVAL;
			goto rel_client;
		} else if (stat != RPC_SUCCESS) {
			error = EIO;
			continue;
		}

		if (lm_sigispending()) {
			lm_debu3(6, "callrpc",
			    "posted signal after lm_get_client");
			if (iscancel == 0) {
				error = EINTR;
				goto rel_client;
			}
		}
		ASSERT(lc != NULL);
		ASSERT(lc->client != NULL);

		lm_n_buf(6, "callrpc", "addr", &lc->addr);

		sigreplace(&newmask, &oldmask);
		stat = CLNT_CALL(lc->client, proc, inproc, in,
				outproc, out, tmo);
		sigreplace(&oldmask, (k_sigset_t *)NULL);

		if (lm_sigispending()) {
			lm_debu3(6, "callrpc", "posted signal after CLNT_CALL");
			signalled = 1;
		}

		switch (stat) {
		case RPC_SUCCESS:
			/*
			 * Update the timestamp on the client cache entry.
			 */
			lc->time = time;
			error = 0;
			break;

		case RPC_TIMEDOUT:
			lm_debu3(6, "callrpc", "RPC_TIMEDOUT");
			if (signalled && (iscancel == 0)) {
				error = EINTR;
				break;
			}
			if (timout == 0) {
				/*
				 * We will always time out when timout == 0.
				 * Don't update the lc->time stamp. We do not
				 * know if the client handle is still OK.
				 */
				error = 0;
				break;
			}
			/* FALLTHROUGH */
		case RPC_CANTSEND:
		case RPC_XPRTFAILED:
		default:
			if (IS_UNRECOVERABLE_RPC(stat)) {
				error = EINVAL;
			} else if (signalled && (iscancel == 0)) {
				error = EINTR;
			} else {
				error = EIO;
			}
		}

rel_client:
		lm_debu5(6, "callrpc", "RPC stat= %d error= %d", stat, error);
		if (lc != NULL)
			lm_rel_client(lc, error);

		/*
		 * If EIO, loop else we're done.
		 */
		if (error != EIO) {
			break;
		}
	}

	mutex_enter(&lm_lck);
	lm_stat.tot_out++;
	lm_stat.bad_out += (error != 0);
	lm_stat.proc_out[proc]++;
	mutex_exit(&lm_lck);

	lm_debu7(6, "callrpc", "End: error= %d, tries= %d, tot= %u, bad= %u",
		error, tries, lm_stat.tot_out, lm_stat.bad_out);

	return (error);
}

/*
 * lm_async
 *
 * List of outstanding asynchronous RPC calls.
 * An entry in the list is free iff in_use == FALSE.
 * Only nlm_stats are expected as replies (can easily be extended).
 */
struct lm_async {
	struct lm_async *next;
	int cookie;
	kcondvar_t cv;
	enum nlm_stats stat;
	bool_t reply;
	bool_t in_use;
};

static struct lm_async *lm_asyncs = NULL;
static struct kmem_cache *lm_async_cache = NULL;

/*
 * lm_asynrpc
 *
 * Make an asynchronous RPC call.
 * Since the call is asynchronous, we put ourselves onto a list and wait for
 * the reply to arrive.  If a reply has not arrived within timout seconds,
 * we retransmit.  So far, this routine is only used by lm_block_lock()
 * to send NLM_GRANTED_MSG to asynchronous (NLM_LOCK_MSG-using) clients.
 * Note: the stat given by caller is updated in lm_asynrply().
 */
int
lm_asynrpc(struct lm_sysid *ls, u_long prog, u_long vers, u_long proc,
	xdrproc_t inproc, caddr_t in, int cookie, enum nlm_stats *stat,
	int timout, int tries)
{
	int error;
	struct lm_async *la;

	/*
	 * Start by inserting the call in lm_asyncs.
	 * Find an empty entry, or create one.
	 */
	mutex_enter(&lm_lck);
	for (la = lm_asyncs; la; la = la->next)
		if (! la->in_use)
			break;
	if (!la) {
		la = kmem_cache_alloc(lm_async_cache, KM_SLEEP);
		cv_init(&la->cv, "la_asyncs_cv", CV_DEFAULT, NULL);
		la->next = lm_asyncs;
		lm_asyncs = la;
		lm_stat.async_len++;
	}
	la->cookie = cookie;
	la->stat = -1;
	la->reply = FALSE;
	la->in_use = TRUE;
	mutex_exit(&lm_lck);

	lm_debu9(5, "asynrpc",
		"la= %x cookie= %d stat= %d reply= %d in_use= %d asyncs= %d",
		(int)la, la->cookie, la->stat, la->reply, la->in_use,
		lm_stat.async_len);

	/*
	 * Call the host asynchronously, i.e. with no timeout.
	 * Sleep timout seconds or until a reply has arrived.
	 * Note that the sleep may NOT be interrupted (we're
	 * a kernel thread).
	 */
	while (tries--) {
		if (error = lm_callrpc(ls, prog, vers, proc, inproc, in,
		    xdr_void, NULL, LM_NO_TIMOUT, 1))
			break;

		mutex_enter(&lm_lck);
		error = cv_timedwait(&la->cv, &lm_lck,
					lbolt + (long)timout * HZ);
		lm_debu4(5, "asynrpc", "cv_timedwait returned %d", error);

		/*
		 * Our thread may have been cv_signal'ed.
		 */
		if (la->reply == TRUE) {
			error = 0;
		} else {
			lm_debu3(5, "asynrpc", "timed out");
			error = EIO;
		}
		mutex_exit(&lm_lck);

		if (error == 0) {
			break;
		}

		lm_debu4(5, "asynrpc", "Timed out. tries= %d", tries);
	}

	*stat = la->stat;
	lm_debu7(5, "asynrpc",
		"End: error= %d, tries= %d, stat= %d, reply= %d",
		error, tries, la->stat, la->reply);

	/*
	 * Release entry in lm_asyncs.
	 */
	la->in_use = FALSE;
	return (error);
}

/*
 * lm_asynrply():
 * Find the lm_async and update reply and stat.
 * Don't bother if lm_async does not exist.
 *
 * Note that the caller can identify the async call with just the cookie.
 * This is because we generated the async call and we know that each new
 * call gets a new cookie.
 */
void
lm_asynrply(int cookie, enum nlm_stats stat)
{
	struct lm_async *la;

	lm_debu5(5, "asynrply", "cookie= %d stat= %d", cookie, stat);

	mutex_enter(&lm_lck);
	for (la = lm_asyncs; la; la = la->next) {
		if (la->in_use)
			if (cookie == la->cookie)
				break;
		lm_debu6(5, "asynrply", "passing la= %x in_use= %d cookie= %d",
			(int)la, la->in_use, la->cookie);
	}

	if (la) {
		la->stat = stat;
		la->reply = TRUE;
		lm_debu4(5, "asynrply", "signalling la= %x", (int)la);
		cv_signal(&la->cv);
	} else {
		lm_debu3(5, "asynrply", "Couldn't find matching la");
	}
	mutex_exit(&lm_lck);
}

/*
 * lm_waitfor_granted
 *
 * Wait for an NLM_GRANTED corresponding to the given lm_sleep.
 *
 * Return value:
 * 0     an NLM_GRANTED has arrived.
 * EINTR sleep was interrupted.
 * -1    the sleep timed out.
 */
int
lm_waitfor_granted(struct lm_sleep *lslp)
{
	int	error = 0;
	long	time;

	mutex_enter(&lm_lck);
	time = lbolt + (long)LM_BLOCK_SLP * HZ;
	error = 0;
	while (lslp->waiting && (error == 0)) {
		error = cv_timedwait_sig(&lslp->cv, &lm_lck, time);
		switch (error) {
		case -1:		/* timed out */
			break;
		case 0:			/* caught a signal */
			error = EINTR;
			break;
		default:		/* cv_signal woke us */
			/*
			 * error is amount of time left on timer
			 * set error to zero so the test at the
			 * top of this loop is only concerned
			 * about the status of the sleeping lock
			 */
			error = 0;
			break;
		}
	};
	mutex_exit(&lm_lck);

	lm_debu4(5, "waitfor_granted", "End: error= %d", error);

	return (error);
}

/*
 * lm_signal_granted():
 * Find the lm_sleep corresponding to the given arguments.
 * If lm_sleep is found, wakeup process and return 0.
 * Otherwise return -1.
 */
int
lm_signal_granted(pid_t pid, struct netobj *fh, struct netobj *oh,
	offset_t offset, offset_t length)
{
	struct lm_sleep *lslp;

	mutex_enter(&lm_lck);
	for (lslp = lm_sleeps; lslp != NULL; lslp = lslp->next) {
		/*
		 * Theoretically, only the oh comparison is necessary to
		 * determine a match.  The other comparisons are for
		 * additional safety.  (Remember that a false match would
		 * cause a process to think it has a lock when it doesn't,
		 * which can cause file corruption.)  We can't compare
		 * sysids because the callback might come in using a
		 * different netconfig than the one the lock request went
		 * out on.
		 */
		if (lslp->in_use && pid == lslp->pid &&
		    lslp->offset == offset && lslp->length == length &&
		    lslp->oh.n_len == oh->n_len &&
		    bcmp(lslp->oh.n_bytes, oh->n_bytes, oh->n_len) == 0 &&
		    lslp->fh.n_len == fh->n_len &&
		    bcmp(lslp->fh.n_bytes, fh->n_bytes, fh->n_len) == 0)
			break;
	}

	if (lslp) {
		lslp->waiting = FALSE;
		cv_signal(&lslp->cv);
	}
	mutex_exit(&lm_lck);

	lm_debu6(5, "signal_granted", "pid= %d, in_use= %d, sleeps= %d",
		(int)(lslp ? lslp->pid    : -1),
		(int)(lslp ? lslp->in_use : -1), (int)lm_stat.sleep_len);

	return (lslp ? 0 : -1);
}

/*
 * Allocate and fill in an lm_sleep, and put it in the global list.
 */
struct lm_sleep *
lm_get_sleep(struct lm_sysid *ls, struct netobj *fh, struct netobj *oh,
	offset_t offset, offset_t length)
{
	struct lm_sleep *lslp;

	mutex_enter(&lm_lck);
	for (lslp = lm_sleeps; lslp; lslp = lslp->next)
		if (! lslp->in_use)
			break;
	if (lslp == NULL) {
		lslp = kmem_cache_alloc(lm_sleep_cache, KM_SLEEP);
		cv_init(&lslp->cv, "lm_sleeps_cv", CV_DEFAULT, NULL);
		lslp->next = lm_sleeps;
		lm_sleeps = lslp;
		lm_stat.sleep_len++;
	}

	lslp->pid = curproc->p_pid;
	lslp->in_use = TRUE;
	lslp->waiting = TRUE;
	lslp->sysid = ls;
	lm_ref_sysid(ls);
	lslp->fh.n_len = fh->n_len;
	lslp->fh.n_bytes = lm_dup(fh->n_bytes, fh->n_len);
	lslp->oh.n_len = oh->n_len;
	lslp->oh.n_bytes = lm_dup(oh->n_bytes, oh->n_len);
	lslp->offset = offset;
	lslp->length = length;

	lm_debu6(5, "get_sleep", "pid= %d, in_use= %d, sleeps= %d",
		lslp->pid, lslp->in_use, lm_stat.sleep_len);

	mutex_exit(&lm_lck);

	return (lslp);
}

/*
 * Release the given lm_sleep.  Resets its contents and frees any memory
 * that it owns.
 */
void
lm_rel_sleep(struct lm_sleep *lslp)
{
	mutex_enter(&lm_lck);
	lslp->in_use = FALSE;

	lm_rel_sysid(lslp->sysid);
	lslp->sysid = NULL;

	kmem_free(lslp->fh.n_bytes, lslp->fh.n_len);
	lslp->fh.n_bytes = NULL;
	lslp->fh.n_len = 0;

	kmem_free(lslp->oh.n_bytes, lslp->oh.n_len);
	lslp->oh.n_bytes = NULL;
	lslp->oh.n_len = 0;

	mutex_exit(&lm_lck);
}

/*
 * Free any unused lm_sleep structs.
 */
void
lm_free_sleep()
{
	struct lm_sleep *prevslp = NULL; /* previously kept sleep */
	struct lm_sleep *nextslp = NULL;
	struct lm_sleep *slp;

	mutex_enter(&lm_lck);
	lm_debu4(5, "free_sleep", "start length: %d\n", lm_stat.sleep_len);

	for (slp = prevslp; slp != NULL; slp = nextslp) {
		nextslp = slp->next;
		if (slp->in_use) {
			prevslp = slp;
		} else {
			if (prevslp == NULL) {
				lm_sleeps = nextslp;
			} else {
				prevslp->next = nextslp;
			}
			--lm_stat.sleep_len;
			ASSERT(slp->sysid == NULL);
			ASSERT(slp->fh.n_bytes == NULL);
			ASSERT(slp->oh.n_bytes == NULL);
			cv_destroy(&slp->cv);
			kmem_cache_free(lm_sleep_cache, slp);
		}
	}

	lm_debu4(5, "free_sleep", "end length: %d\n", lm_stat.sleep_len);
	mutex_exit(&lm_lck);
}

/*
 * Create the kmem caches for allocating various lock manager tables.
 */
void
lm_caches_init()
{
	mutex_enter(&lm_lck);
	if (!lm_caches_created) {
		lm_caches_created = TRUE;
		lm_server_caches_init();
		lm_sysid_cache = kmem_cache_create("lm_sysid",
				sizeof (struct lm_sysid), 0, NULL, NULL,
				lm_free_sysid_table);
		lm_client_cache = kmem_cache_create("lm_client",
				sizeof (struct lm_client), 0, NULL, NULL,
				lm_flush_clients_mem);
		lm_async_cache = kmem_cache_create("lm_async",
				sizeof (struct lm_async), 0, NULL, NULL, NULL);
		lm_sleep_cache = kmem_cache_create("lm_sleep",
				sizeof (struct lm_sleep), 0, NULL, NULL,
				lm_free_sleep);
		lm_config_cache = kmem_cache_create("lm_config",
				sizeof (struct lm_config), 0, NULL, NULL,
				NULL);
	}
	mutex_exit(&lm_lck);
}

/*
 * Determine whether or not a signal is pending on the calling thread.
 * If so, return 1 else return 0.
 *
 * XXX: Fixes to this code should probably be propagated to (or from)
 * the common signal-handling code in sig.c.  See bugid 1201594.
 */
int
lm_sigispending()
{
	klwp_t *lwp;

	/*
	 * Some callers may be non-signallable kernel threads, in
	 * which case we always return 0.  Allowing such a thread
	 * to (pointlessly) call ISSIG() would result in a panic.
	 */
	lwp = ttolwp(curthread);
	if (lwp == (klwp_t *) NULL) {
		return (0);
	}

	/*
	 * lwp_asleep and lwp_sysabort are modified only for the sake of
	 * /proc, and should always be set to 0 after the ISSIG call.
	 * Note that the lwp may sleep for a long time inside
	 * ISSIG(FORREAL) - a human being may be single-stepping in a
	 * debugger, for example - so we must not hold any mutexes or
	 * other critical resources here.
	 */
	lwp->lwp_asleep = 1;
	lwp->lwp_sysabort = 0;
	/* ASSERT(no mutexes or rwlocks are held) */
	if (ISSIG(curthread, FORREAL) || lwp->lwp_sysabort || ISHOLD(curproc)) {
		lwp->lwp_asleep = 0;
		lwp->lwp_sysabort = 0;
		return (1);
	}

	lwp->lwp_asleep = 0;
	return (0);
}
