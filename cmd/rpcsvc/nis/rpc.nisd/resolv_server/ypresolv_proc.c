/*
 * Copyright (c) 1993 Sun Microsystems
 *
 * The resolv code was lifted from 4.1.3 ypserv. References to child/pid
 * have been changed to cache/nres to reflect what is really happening.
 *
 */

/* ******************** include headers ****************************** */
#include <netdb.h>
#include <ctype.h>
#include <syslog.h>
#include <rpc/rpc.h>
#include <rpcsvc/yp_prot.h>
#ifdef TDRPC
#include <sys/socket.h>
#else
#include <netdir.h>
#endif
#include "../resolv_common.h"
#include "prnt.h"

#define	RESP_NOW	0
#define	RESP_LATER	2

#define	NQTIME		10	/* minutes */
#define	PQTIME		30	/* minutes */

/* *Cache entries for storing rpc.nisd req and resolv req info* */
struct cache_ent {
	struct nres	*nres;
	datum		key;
	datum		val;
	char		*map;
	struct timeval	enqtime;
	int		h_errno;
	SVCXPRT		*xprt;
#ifdef TDRPC
	struct sockaddr_in caller;
#else
	struct netbuf	caller;
	char buf[MAX_UADDR];
#endif
	unsigned long   xid;
	struct cache_ent   *next_cache_ent;
};


/* ******************** static vars and funcs ************************ */
static struct cache_ent *cache_head = NULL;
static u_long svc_setxid();
static int my_done();


/* ******************** extern vars and funcs ************************ */
extern int verbose;
extern SVCXPRT *reply_xprt;
extern struct netconfig *udp_nconf;
extern bool_t xdr_ypfwdreq_key();
extern struct nres* nres_gethostbyname();
extern struct nres* nres_gethostbyaddr();


void yp_resolv(req, transp)
	struct ypfwdreq_key req;
	SVCXPRT *transp;
{
	struct ypresp_val resp;
	int respond = RESP_NOW;
#ifdef TDRPC
	struct sockaddr_in addr;
#else
	int i;
	char tmp[5], buf[MAX_UADDR];
	struct netbuf *nbuf;
	struct bogus_data *bd = NULL;
#endif

	resp.valdat.dptr = NULL;
	resp.valdat.dsize = 0;

	/* Set the reply_xprt: xid and caller here, to fit yp_matchdns() */
#ifdef TDRPC
	addr.sin_family = AF_INET;
	addr.sin_port = req.port;
	addr.sin_addr.s_addr = req.ip;
	SETCALLER(transp, &addr);
	transp->xp_addrlen = sizeof (addr);
#else
	buf[0] = '\0';
	for (i = 3; i >= 0; i--){
		sprintf (tmp, "%u.", (req.ip>>(8*i)) & 0x000000ff);
		strcat (buf, tmp);
	}
	sprintf (tmp, "%u.%u", (req.port>>8) & 0x00ff, req.port & 0x00ff);
	strcat (buf, tmp);
	if ((nbuf = uaddr2taddr(udp_nconf, buf)) == NULL){
		prnt (P_ERR, "can't get args.\n");
		return;
	}
	if (nbuf->len > MAX_UADDR){ /* added precaution */
		prnt (P_ERR, "uaddr too big for cache.\n");
		netdir_free ((void*)nbuf, ND_ADDR);
		return;
	}
	SETCALLER(transp, nbuf);
	/*
	 * Set su_tudata.addr for sendreply() t_sendudata()
	 * since we never did a recv on this unreg'ed xprt.
	 */
	if (!bd) { /* just set maxlen and buf once */
		bd = getbogus_data(transp);
		bd->su_tudata.addr.maxlen = GETCALLER(transp)->maxlen;
		bd->su_tudata.addr.buf = GETCALLER(transp)->buf;
	}
	bd->su_tudata.addr.len = nbuf->len;
	netdir_free ((void*)nbuf, ND_ADDR);
#endif
	(void) svc_setxid(transp, req.xid);

	respond = yp_matchdns(req.map, req.keydat, &resp.valdat,
						&resp.status, transp);

	if (respond == RESP_NOW)
		if (!svc_sendreply(transp,
				(xdrproc_t)xdr_ypresp_val, (char*)&resp)) {
			prnt(P_ERR, "can't respond to rpc request.\n");
		}
}

void dispatch(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	struct ypfwdreq_key req;

	switch (rqstp->rq_proc){
	case NULLPROC:
		if (!svc_sendreply(transp, xdr_void, 0))
			prnt (P_ERR, "can't respond to ping.\n");
		break;
	case YPDNSPROC:
		req.map = NULL;
		req.keydat.dptr = NULL;
		req.xid = 0;
		req.ip = 0;
		req.port = 0;

		if (!svc_getargs(transp, xdr_ypfwdreq_key, (char*) &req)) {
			prnt (P_ERR, "can't get args.\n");
			svcerr_decode(transp);
			return;
		}

		/* args were ok: don't wait for resolver */
		if (!svc_sendreply(transp, xdr_void, 0))
			prnt (P_ERR, "can't ack nisd req.\n");

		yp_resolv(req, reply_xprt);

		if (!svc_freeargs(transp, xdr_ypfwdreq_key, (char*)&req)){
			prnt(P_ERR, "can't free args.\n");
			exit (1);
		}

		break;
	default:
		prnt (P_ERR, "call to bogus proc.\n");
		svcerr_noproc(transp);
		break;
	}

	return;
}

struct cache_ent* cache_ent_bykey(map, keydat)
	char *map;
	datum keydat;
{
	struct cache_ent   *chl;
	struct cache_ent   *prev;
	struct timeval  now;
	struct timezone tzp;
	int		secs;
	if (keydat.dptr == NULL)
		return (NULL);
	if (keydat.dsize <= 0)
		return (NULL);
	if (map == NULL)
		return (NULL);
	gettimeofday(&now, &tzp);

	for (prev = cache_head, chl = cache_head; chl; 1) {
		/* check for expiration */
		if (chl->h_errno == TRY_AGAIN)
			secs = NQTIME * 60;
		else
			secs = PQTIME * 60;
		if ((chl->nres == 0) &&
				(chl->enqtime.tv_sec + secs) < now.tv_sec) {
			prnt(P_INFO,
				"bykey:stale cache_ent flushed %x.\n", chl);
			/* deleteing the first element is tricky */
			if (chl == cache_head) {
				cache_head = cache_head->next_cache_ent;
				free_cache_ent(chl);
				prev = cache_head;
				chl = cache_head;
				continue;
			} else {
			/* deleteing a middle element */
				prev->next_cache_ent = chl->next_cache_ent;
				free_cache_ent(chl);
				chl = prev->next_cache_ent;
				continue;
			}
		} else if (chl->map)
			if (0 == strcmp(map, chl->map))
				if (chl->key.dptr) {
					/* supress trailing null */
					if (keydat.dptr[keydat.dsize - 1] == 0)
						keydat.dsize--;
					if ((chl->key.dsize == keydat.dsize))
						if (!strncasecmp(chl->key.dptr,
						keydat.dptr, keydat.dsize)) {
							/* move to beginning */
							if (chl != cache_head) {
				prev->next_cache_ent = chl->next_cache_ent;
				chl->next_cache_ent = cache_head;
				cache_head = chl;
							}
							return (chl);
						}
				}
		prev = chl;
		chl = chl->next_cache_ent;
	}
	return (NULL);
}

struct cache_ent* new_cache_ent(map, keydat)
	char		*map;
	datum		keydat;
{
	char		*strdup();
	struct cache_ent   *chl;
	struct timezone tzp;

	chl = (struct cache_ent *) calloc(1, sizeof (struct cache_ent));
	if (chl == NULL)
		return (NULL);
	prnt(P_INFO, "cache_ent enqed.\n");
#ifndef TDRPC
	chl->caller.buf = chl->buf;
	chl->caller.maxlen = sizeof (chl->buf);
#endif
	chl->map = (char *) strdup(map);
	if (chl->map == NULL) {
		free(chl);
		return (NULL);
	}
	chl->key.dptr = (char*)malloc(keydat.dsize + 1);
	if (chl->key.dptr == NULL) {
		free(chl->map);
		free(chl);
		return (NULL);
	}
	if (keydat.dptr != NULL)
		/* delete trailing null */
		if (keydat.dptr[keydat.dsize - 1] == 0)
			keydat.dsize = keydat.dsize - 1;
	chl->key.dsize = keydat.dsize;
	chl->key.dptr[keydat.dsize] = '\0';
	chl->val.dptr = 0;
#ifdef TDRPC
	bcopy(keydat.dptr, chl->key.dptr, keydat.dsize);
#else
	memcpy(chl->key.dptr, keydat.dptr, keydat.dsize);
#endif
	gettimeofday(&(chl->enqtime), &tzp);
	chl->next_cache_ent = cache_head;
	cache_head = chl;
	return (chl);
}

struct cache_ent* deq_cache_ent(x)
	struct cache_ent   *x;
{
	struct cache_ent   *chl;
	struct cache_ent   *prev;
	if (x == cache_head) {
		cache_head = cache_head->next_cache_ent;
		x->next_cache_ent == NULL;
		return (x);
	}
	for (chl = cache_head, prev = cache_head; chl;
					chl = chl->next_cache_ent) {
		if (chl == x) {
			/* deq it */
			prev->next_cache_ent = chl->next_cache_ent;
			chl->next_cache_ent == NULL;
			return (chl);
		}
		prev = chl;
	}
	return (NULL);		/* bad */
}

int free_cache_ent(x)
	struct cache_ent   *x;
{
	if (x == NULL)
		return (-1);
	if (x->map)
		free(x->map);
	if (x->key.dptr)
		free(x->key.dptr);
	if (x->val.dptr)
		free(x->val.dptr);
	free(x);
	return (0);
}

static u_long svc_setxid(xprt, xid)
	register SVCXPRT *xprt;
	u_long xid;
{
	register struct bogus_data *su = getbogus_data(xprt);
	u_long old_xid;
	if (su == NULL) return (0);
	old_xid = su->su_xid;
	su->su_xid = xid;
	return (old_xid);
}


int yp_matchdns(map, keydat, valdatp, statusp, transp)
	char		*map;	/* map name */
	datum		keydat;	/* key to match (e.g. host name) */
	datum		*valdatp; /* returned value if found */
	unsigned	*statusp; /* returns the status */
	SVCXPRT		*transp;
{
	struct nres	*h;
	struct nres	*nres;
	int		byname, byaddr;
	struct cache_ent   *chl;
	struct cache_ent    chld;
	struct timeval  now;
	struct timezone tzp;
	int		try_again;
	int		mask;
	int		i;

	try_again = 0;
	/*
	 * Skip the domain resolution if: 1. it is not turned on 2. map other
	 * than hosts.byXXX 3. a null string (usingypmap() likes to send
	 * these) 4. a single control character (usingypmap() again)
	 */
	byname = strcmp(map, "hosts.byname") == 0;
	byaddr = strcmp(map, "hosts.byaddr") == 0;
	if ((!byname && !byaddr) ||
			keydat.dsize == 0 || keydat.dptr[0] == '\0' ||
			!isascii(keydat.dptr[0]) || !isgraph(keydat.dptr[0])) {
		*statusp = YP_NOKEY;
		return (RESP_NOW);
	}

	chl = cache_ent_bykey(map, keydat);
	if (chl) {
		gettimeofday(&now, &tzp);
		if (chl->h_errno == TRY_AGAIN)
			try_again = 1;
		else if (chl->nres) {
			/* update xid */
			if (transp) {
				chl->xprt = transp;
#ifdef TDRPC
				chl->caller = (* (GETCALLER(transp)));
#else
				chl->caller.len = transp->xp_rtaddr.len;
				memcpy(chl->caller.buf, transp->xp_rtaddr.buf,
							transp->xp_rtaddr.len);
#endif
				chl->xid = svc_getxid(transp);
				prnt(P_INFO, "cache_ent %s: xid now %d.\n",
						chl->key.dptr, chl->xid);
			}
			return (RESP_LATER);	/* drop */
		}
		switch (chl->h_errno) {
		case NO_RECOVERY:
#ifndef NO_DATA
#define	NO_DATA	NO_ADDRESS
#endif
		case NO_DATA:
		case HOST_NOT_FOUND:
			prnt(P_INFO, "cache NO_KEY.\n");
			*statusp = YP_NOKEY;
			return (RESP_NOW);

		case TRY_AGAIN:
			prnt(P_INFO, "try_again.\n");
			try_again = 1;
			break;
		case 0:
			prnt(P_INFO, "cache ok.\n");
			if (chl->val.dptr) {
				*valdatp = chl->val;
				*statusp = YP_TRUE;
				return (RESP_NOW);
			}
			break;

		default:
			free_cache_ent(deq_cache_ent(chl));
			chl = NULL;
			break;
		}
	}
	/* have a trier activated -- tell them to try again */
	if (try_again) {
		if (chl->nres) {
			*statusp = YP_NOMORE;	/* try_again overloaded */
			return (RESP_NOW);
		}
	}
	if (chl) {
		gettimeofday(&(chl->enqtime), &tzp);
	} else
		chl = new_cache_ent(map, keydat);

	if (chl == NULL) {
		perror("new_cache_ent failed");
		*statusp = YP_YPERR;
		return (RESP_NOW);
	}
	keydat.dptr[keydat.dsize] = 0;
	if (byname)
		h = nres_gethostbyname(keydat.dptr, my_done, chl);
	else {
		long	addr;
		addr = inet_addr(keydat.dptr);
		h = nres_gethostbyaddr(&addr, sizeof (addr), AF_INET,
							my_done, chl);
	}
	if (h == 0) {		/* definite immediate reject */
		prnt(P_INFO, "imediate reject.\n");
		free_cache_ent(deq_cache_ent(chl));
		*statusp = YP_NOKEY;
		return (RESP_NOW);
	} else if (h == (struct nres*) -1) {
		perror("nres failed\n");
		*statusp = YP_YPERR;
		return (RESP_NOW);
	} else {
		chl->nres = h;
		/* should stash transport so my_done can answer */
		if (try_again) {
			*statusp = YP_NOMORE;	/* try_again overloaded */
			return (RESP_NOW);

		}
		chl->xprt = transp;
		if (transp) {
#ifdef TDRPC
			chl->caller = (* (GETCALLER(transp)));
#else
			chl->caller.len = transp->xp_rtaddr.len;
			memcpy(chl->caller.buf, transp->xp_rtaddr.buf,
						transp->xp_rtaddr.len);
#endif
			chl->xid = svc_getxid(transp);
		}
		return (RESP_LATER);
	}
}

static int my_done(n, h, chl, errcode)
	char		*n;	/* opaque */
	struct hostent	*h;
	struct cache_ent	*chl;
	int errcode;
{
	static char	buf[1024];
	char		*endbuf;
	datum		valdatp;
	int		i;
	SVCXPRT		*transp;
	unsigned long	xid_hold;
	struct ypresp_val resp;
	struct timezone tzp;
#ifdef TDRPC
	struct sockaddr_in caller_hold, *addrp;
#else
	struct netbuf caller_hold, *addrp;
	char uaddr[25];
#endif

	prnt(P_INFO, "my_done: %s.\n", chl->key.dptr);
	gettimeofday(&(chl->enqtime), &tzp);
	chl->nres = 0;
#ifndef TDRPC
	caller_hold.maxlen = sizeof (uaddr);
	caller_hold.buf = uaddr;
#endif

	if (h == NULL) {
		chl->h_errno = errcode;
		if (chl->h_errno == TRY_AGAIN)
			resp.status = YP_NOMORE;
		else
			resp.status = YP_NOKEY;
		valdatp.dptr = NULL;
		valdatp.dsize = 0;
	} else {
		chl->h_errno = 0;
		endbuf = buf;
		for (i = 0; h->h_addr_list[i]; i++) {
			sprintf(endbuf, "%s\t%s\n", inet_ntoa(
				*(struct in_addr *) (h->h_addr_list[i])),
				h->h_name);
			endbuf = &endbuf[strlen(endbuf)];
			if ((&buf[sizeof (buf)] - endbuf) < 300)
				break;
		}
		valdatp.dptr = buf;
		valdatp.dsize = strlen(buf);
		chl->val.dsize = valdatp.dsize;
		chl->val.dptr = (char*)malloc(valdatp.dsize);
		if (chl->val.dptr == NULL) {
			perror("my_done");
			free_cache_ent(deq_cache_ent(chl));
			return (-1);
		}
#ifdef TDRPC
		bcopy(valdatp.dptr, chl->val.dptr, valdatp.dsize);
#else
		memcpy(chl->val.dptr, valdatp.dptr, valdatp.dsize);
#endif
		resp.status = YP_TRUE;
	}
	/* try to answer here */

	transp = chl->xprt;
	if (transp) {
#ifdef TDRPC
		caller_hold = *(GETCALLER(transp));
#else
		caller_hold.len = transp->xp_rtaddr.len;
		memcpy (caller_hold.buf, transp->xp_rtaddr.buf,
					transp->xp_rtaddr.len);
#endif
		xid_hold = svc_setxid(transp, chl->xid);
		addrp = &(chl->caller);
		SETCALLER(transp, addrp);
		resp.valdat = valdatp;
		if (!svc_sendreply(transp, (xdrproc_t)xdr_ypresp_val,
							(char*)&resp)){
		}
		addrp = &caller_hold;
		SETCALLER(transp, addrp);
		svc_setxid(transp, xid_hold);
	}
	return (0);
}
