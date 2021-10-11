/*
 *	gethostent.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)gethostent.c	1.17	95/07/10 SMI"

#include	<stdio.h>
#include	<ctype.h>
#include	<sys/types.h>
#include	<sys/socket.h>
#include	<netinet/in.h>
#include	<netdb.h>
#include	<string.h>
#include	<thread.h>
#include	<arpa/nameser.h>
#include	<resolv.h>
#include	<syslog.h>
#include	<nsswitch.h>
#include	<nss_dbdefs.h>

typedef	struct	dns_backend	*dns_backend_ptr_t;
typedef	nss_status_t	(*dns_backend_op_t)(dns_backend_ptr_t, void *);

struct dns_backend {
	dns_backend_op_t	*ops;
	nss_dbop_t		n_ops;
};

extern	nss_status_t _herrno2nss(int h_errno);

extern	int strcasecmp();

int	h_errno;

#if PACKETSZ > 1024
#define	MAXPACKET PACKETSZ
#else
#define	MAXPACKET 1024
#endif

static mutex_t	one_lane = DEFAULTMUTEX;

static struct hostdata {
	int		stayopen;
	char		*host_aliases[MAXALIASES];
#define	HOSTADDRSIZE	4	/* assumed == sizeof u_long */
	char		hostaddr[MAXADDRS][HOSTADDRSIZE];
	char		*addr_list[MAXADDRS+1];
	char		line[BUFSIZ+1];
	struct hostent	host;
};

static struct hostdata	*hostdata;

static struct hostent	host;

typedef union {
	HEADER hdr;
	u_char buf[MAXPACKET];
} querybuf;

typedef union {
	long al;
	char ac;
} align;

static u_long inet_addr();
static struct in_addr host_addr;

/*
 * Internal routine to allocate hostdata on heap, instead of
 * putting lots of stuff into the data segment.
 */
static struct hostdata *
_hostdata()
{
	register struct hostdata *d = hostdata;

	if (d == 0) {
		d = (struct hostdata *)calloc(1, sizeof (struct hostdata));
		hostdata = d;
		if ((_res.options & RES_INIT) == 0) {
			if (res_init() == -1)
				return ((struct hostdata *)-1);
		}
	}
	return (d);
}

static struct hostent *
getanswer(answer, anslen, iquery)
	querybuf	*answer;
	int		anslen;
	int		iquery;
{
	register HEADER *hp;
	register u_char *cp;
	register int 	n;
	u_char		*eom;
	char		*bp, **ap;
	int		type, class, buflen, ancount, qdcount;
	int		haveanswer, getclass = C_ANY;
	char		**hap;
	struct hostdata *d = _hostdata();

	eom = answer->buf + anslen;
	/*
	 * find first satisfactory answer
	 */
	hp = &answer->hdr;
	ancount = ntohs(hp->ancount);
	qdcount = ntohs(hp->qdcount);
	bp = d->line;
	buflen = BUFSIZ;
	cp = answer->buf + sizeof (HEADER);
	if (qdcount) {
		if (iquery) {
			if ((n = dn_expand((char *)answer->buf, eom,
						cp, bp, buflen)) < 0) {
				h_errno = NO_RECOVERY;
				return ((struct hostent *) NULL);
			}
			cp += n + QFIXEDSZ;
			host.h_name = bp;
			n = strlen(bp) + 1;
			bp += n;
			buflen -= n;
		} else
			cp += dn_skipname(cp, eom) + QFIXEDSZ;
		while (--qdcount > 0)
			cp += dn_skipname(cp, eom) + QFIXEDSZ;
	} else if (iquery) {
		if (hp->aa)
			h_errno = HOST_NOT_FOUND;
		else
			h_errno = TRY_AGAIN;
		return ((struct hostent *) NULL);
	}
	ap = d->host_aliases;
	*ap = NULL;
	host.h_aliases = d->host_aliases;
	hap = d->addr_list;
	*hap = NULL;
#if BSD >= 43 || defined(h_addr)	/* new-style hostent structure */
	host.h_addr_list = d->addr_list;
#endif
	haveanswer = 0;
	while (--ancount >= 0 && cp < eom && haveanswer < MAXADDRS) {
		if ((n = dn_expand((char *)answer->buf, eom,
						cp, bp, buflen)) < 0)
			break;
		cp += n;
		if ((cp + (3 * sizeof (u_short)) + sizeof (u_long)) > eom)
			break;
		type = _getshort(cp);
		cp += sizeof (u_short);
		class = _getshort(cp);
		cp += sizeof (u_short) + sizeof (u_long);
		n = _getshort(cp);
		cp += sizeof (u_short);
		if (type == T_CNAME) {
			cp += n;
			if (ap >= &(d->host_aliases[MAXALIASES-1]))
				continue;
			*ap++ = bp;
			n = strlen(bp) + 1;
			bp += n;
			buflen -= n;
			continue;
		}
		if (iquery && type == T_PTR) {
			if ((n = dn_expand((char *)answer->buf, eom,
						cp, bp, buflen)) < 0) {
				cp += n;
				continue;
			}
			cp += n;
			host.h_name = bp;
			return (&host);
		}
		if (iquery || type != T_A)  {
#ifdef DEBUG
			if (_res.options & RES_DEBUG)
				printf("unexpected answer type %d, size %d\n",
					type, n);
#endif
			cp += n;
			continue;
		}
		if (haveanswer) {
			if (n != host.h_length) {
				cp += n;
				continue;
			}
			if (class != getclass) {
				cp += n;
				continue;
			}
		} else {
			host.h_length = n;
			getclass = class;
			host.h_addrtype = (class == C_IN) ? AF_INET : AF_UNSPEC;
			if (!iquery) {
				host.h_name = bp;
				bp += strlen(bp) + 1;
			}
		}

		bp += sizeof (align) - ((u_long)bp % sizeof (align));

		if (bp + n >= &d->line[BUFSIZ]) {
#ifdef DEBUG
			if (_res.options & RES_DEBUG)
				printf("size (%d) too big\n", n);
#endif
			break;
		}

		if (cp + n > eom)
			break;
#ifdef SYSV
		memcpy(*hap++ = bp, cp, n);
#else
		bcopy(cp, *hap++ = bp, n);
#endif
		bp += n;
		cp += n;
		haveanswer++;
	}
	if (haveanswer) {
		*ap = NULL;
#if BSD >= 43 || defined(h_addr)	/* new-style hostent structure */
		*hap = NULL;
#else
		host.h_addr = d->addr_list[0];
#endif
		return (&host);
	} else {
		h_errno = TRY_AGAIN;
		return ((struct hostent *) NULL);
	}
}

/*
 * Internet Name Domain Server (DNS) only implementation.
 */
static struct hostent *
_gethostbyaddr(h_errnop, addr, len, type)
	int			*h_errnop;
	char			*addr;
	int			len, type;
{
	struct hostdata	*d = _hostdata();
	int		n;
	querybuf	buf;
	register struct hostent *hp;
	char		qbuf[MAXDNAME];

	if ((d == (struct hostdata *)-1) ||
	    (d == (struct hostdata *)0) ||
	    (type != AF_INET) ||
	    (len != 4)) {	/* 4 octets, not 4 bytes */
		*h_errnop = NO_RECOVERY;
		return (NULL);
	}
	(void) sprintf(qbuf, "%u.%u.%u.%u.in-addr.arpa",
		((unsigned)addr[3] & 0xff),
		((unsigned)addr[2] & 0xff),
		((unsigned)addr[1] & 0xff),
		((unsigned)addr[0] & 0xff));
	n = res_query(qbuf, C_IN, T_PTR, (char *)&buf, sizeof (buf));

	if (n < 0) {
#ifdef DEBUG
		if (_res.options & RES_DEBUG)
			printf("res_query failed\n");
#endif
		*h_errnop = h_errno;
		return (NULL);
	}
	n = sizeof (buf) < n ? sizeof (buf) : n;
	hp = getanswer(&buf, n, 1);
	if (hp == NULL) {
		*h_errnop = HOST_NOT_FOUND;
		return (NULL);
	}
	hp->h_addrtype = type;
	hp->h_length = len;
	d->addr_list[0] = (char *)&host_addr;
	d->addr_list[1] = (char *)0;
	host_addr = *(struct in_addr *)addr;
#if BSD < 43 && !defined(h_addr)	/* new-style hostent structure */
	hp->h_addr = d->addr_list[0];
#endif
	*h_errnop = 0;	/* success (there's no #define) */
	return (hp);
}

static struct hostent *
_gethostbyname(h_errnop, name)
	int		*h_errnop;
	register char	*name;
{
	register struct hostent *hp;
	struct hostdata		*d = _hostdata();
	querybuf		buf;
	register char		*cp;
	int			n;

	if ((d == (struct hostdata *)-1) || (d == (struct hostdata *)0)) {
		*h_errnop = NO_RECOVERY;
		return (NULL);
	}

	/*
	 * The host name is guaranteed not to be an IP address
	 * in ASCII dot-separated notation.  In such cases,
	 * _get_hostserv_inetnetdir_byname() bypasses the switch.
	 */

	if ((n = res_search(name, C_IN, T_A, buf.buf, sizeof (buf))) < 0) {
#ifdef DEBUG
		if (_res.options & RES_DEBUG)
			printf("res_search failed\n");
#endif
		*h_errnop = h_errno;
		return (NULL);
	}
	n = sizeof (buf) < n ? sizeof (buf) : n;
	hp = getanswer(&buf, n, 0);

	*h_errnop = (hp == NULL) ? HOST_NOT_FOUND : 0;
	return (hp);
}

void
_sethostent(errp, stayopen)
	nss_status_t	*errp;
	int		stayopen;
{
	register struct hostdata *d = _hostdata();

	if ((d == (struct hostdata *)-1) || (d == (struct hostdata *)0)) {
		*errp = NSS_UNAVAIL;
		return;
	}
/*
	if (stayopen)
		_res.options |= RES_STAYOPEN | RES_USEVC;
*/
	*errp = NSS_SUCCESS;
}

void
_endhostent(errp)
	nss_status_t	*errp;
{
	register struct hostdata *d = _hostdata();

	if ((d == (struct hostdata *)-1) || (d == (struct hostdata *)0)) {
		*errp = NSS_UNAVAIL;
		return;
	}
/*
	_res.options &= ~(RES_STAYOPEN | RES_USEVC);
*/
	_res_close();
	*errp = NSS_SUCCESS;
}

/*
 * Internet address interpretation routine.
 * All the network library routines call this
 * routine to interpret entries in the data bases
 * which are expected to be an address.
 * The value returned is in network order.
 *
 * Duplicated here to avoid dependecy on libnsl.
 */
static u_long
inet_addr(cp)
	register char *cp;
{
	register u_long	val, base, n;
	register char	c;
	u_long		parts[4], *pp = parts;

again:
	/*
	 * Collect number up to ``.''.
	 * Values are specified as for C:
	 * 0x=hex, 0=octal, other=decimal.
	 */
	val = 0; base = 10;
	if (*cp == '0') {
		if (*++cp == 'x' || *cp == 'X')
			base = 16, cp++;
		else
			base = 8;
	}
	while (c = *cp) {
		if (isdigit(c)) {
			if ((c - '0') >= base)
				break;
			val = (val * base) + (c - '0');
			cp++;
			continue;
		}
		if (base == 16 && isxdigit(c)) {
			val = (val << 4) + (c + 10 - (islower(c) ? 'a' : 'A'));
			cp++;
			continue;
		}
		break;
	}
	if (*cp == '.') {
		/*
		 * Internet format:
		 *	a.b.c.d
		 *	a.b.c	(with c treated as 16-bits)
		 *	a.b	(with b treated as 24 bits)
		 */
		if (pp >= parts + 4)
			return (-1);
		*pp++ = val, cp++;
		goto again;
	}
	/*
	 * Check for trailing characters.
	 */
	if (*cp && !isspace(*cp))
		return (-1);
	*pp++ = val;
	/*
	 * Concoct the address according to
	 * the number of parts specified.
	 */
	n = pp - parts;
	switch (n) {

	case 1:				/* a -- 32 bits */
		val = parts[0];
		break;

	case 2:				/* a.b -- 8.24 bits */
		val = (parts[0] << 24) | (parts[1] & 0xffffff);
		break;

	case 3:				/* a.b.c -- 8.8.16 bits */
		val = (parts[0] << 24) | ((parts[1] & 0xff) << 16) |
			(parts[2] & 0xffff);
		break;

	case 4:				/* a.b.c.d -- 8.8.8.8 bits */
		val = (parts[0] << 24) | ((parts[1] & 0xff) << 16) |
				((parts[2] & 0xff) << 8) | (parts[3] & 0xff);
		break;

	default:
		return (-1);
	}
	val = htonl(val);
	return (val);
}


/*
 * Section below is added to serialize the DNS backend.
 */


static nss_status_t
getbyname(be, a)
	dns_backend_ptr_t	be;
	void			*a;
{
	struct hostent	*he;
	nss_XbyY_args_t	*argp = (nss_XbyY_args_t *) a;
	int		ret;

	sigset_t	oldmask, newmask;

	sigfillset(&newmask);
	_thr_sigsetmask(SIG_SETMASK, &newmask, &oldmask);
	_mutex_lock(&one_lane);

	he = _gethostbyname(&argp->h_errno, argp->key.name);
	if (he != NULL) {
		ret = ent2result(he, a);
		if (ret == NSS_STR_PARSE_SUCCESS) {
			argp->returnval = argp->buf.result;
		} else {
			argp->h_errno = HOST_NOT_FOUND;
			if (ret == NSS_STR_PARSE_ERANGE) {
				argp->erange = 1;
			}
		}
	}
	_mutex_unlock(&one_lane);
	_thr_sigsetmask(SIG_SETMASK, &oldmask, NULL);

	return (_herrno2nss(argp->h_errno));
}


static nss_status_t
getbyaddr(be, a)
	dns_backend_ptr_t	be;
	void			*a;
{
	int		n;
	struct hostent	*he, *he2;
	nss_XbyY_args_t	*argp = (nss_XbyY_args_t *) a;
	int		ret, save_h_errno;
	char		**ans, hbuf[MAXHOSTNAMELEN];

	sigset_t	oldmask, newmask;

	sigfillset(&newmask);
	_thr_sigsetmask(SIG_SETMASK, &newmask, &oldmask);
	_mutex_lock(&one_lane);

	he = _gethostbyaddr(&argp->h_errno, argp->key.hostaddr.addr,
		argp->key.hostaddr.len, argp->key.hostaddr.type);
	if (he != NULL) {

		/* save a copy of the (alleged) hostname */
		(void) strcpy(hbuf, he->h_name);
		n = strlen(hbuf);
		if (n < MAXHOSTNAMELEN-1 && hbuf[n-1] != '.') {
			strcat(hbuf, ".");
		}
		ret = ent2result(he, a);
		save_h_errno = argp->h_errno;
		if (ret == NSS_STR_PARSE_SUCCESS) {
			/*
			 * check to make sure by doing a forward query
			 * We use _gethostbyname() to avoid the stack, and
			 * then we throw the result from argp->h_errno away,
			 * becase we don't care.  And besides you want the
			 * return code from _gethostbyaddr() anyway.
			 */
			he2 = _gethostbyname(&argp->h_errno, hbuf);

			if (he2 != (struct hostent *)NULL) {
				/* until we prove name and addr match */
				argp->h_errno = HOST_NOT_FOUND;
				for (ans = he2->h_addr_list; *ans; ans++)
					if (memcmp(*ans,
						argp->key.hostaddr.addr,
						he2->h_length) == 0) {
					argp->h_errno = save_h_errno;
					argp->returnval = argp->buf.result;
					break;
				}
			} else {
				/*
				 * What to do if _gethostbyname() fails ???
				 * We assume they are doing something stupid
				 * like registering addresses but not names
				 * (some people actually think that provides
				 * some "security", through obscurity).  So for
				 * these poor lost souls, because we can't
				 * PROVE spoofing and because we did try (and
				 * we don't want a bug filed on this), we let
				 * this go.  And return the name from byaddr.
				 */
				argp->h_errno = save_h_errno;
				argp->returnval = argp->buf.result;
			}
			/* we've been spoofed, make sure to log it. */
			if (argp->h_errno == HOST_NOT_FOUND)
				syslog(LOG_NOTICE, "gethostbyaddr: %s != %s",
		hbuf, inet_ntoa(*(struct in_addr *)argp->key.hostaddr.addr));
		} else {
			argp->h_errno = HOST_NOT_FOUND;
			if (ret == NSS_STR_PARSE_ERANGE) {
				argp->erange = 1;
			}
		}
	}
	_mutex_unlock(&one_lane);
	_thr_sigsetmask(SIG_SETMASK, &oldmask, NULL);

	return (_herrno2nss(argp->h_errno));
}


nss_status_t
_nss_dns_getent(be, args)
	dns_backend_ptr_t	be;
	void			*args;
{
	return (NSS_UNAVAIL);
}


nss_status_t
_nss_dns_setent(be, dummy)
	dns_backend_ptr_t	be;
	void			*dummy;
{
	nss_status_t	errp;

	sigset_t	oldmask, newmask;

	sigfillset(&newmask);
	_thr_sigsetmask(SIG_SETMASK, &newmask, &oldmask);
	_mutex_lock(&one_lane);

	_sethostent(&errp, 1);

	_mutex_unlock(&one_lane);
	_thr_sigsetmask(SIG_SETMASK, &oldmask, NULL);

	return (errp);
}


nss_status_t
_nss_dns_endent(be, dummy)
	dns_backend_ptr_t	be;
	void			*dummy;
{
	nss_status_t	errp;

	sigset_t	oldmask, newmask;

	sigfillset(&newmask);
	_thr_sigsetmask(SIG_SETMASK, &newmask, &oldmask);
	_mutex_lock(&one_lane);

	_endhostent(&errp);

	_mutex_unlock(&one_lane);
	_thr_sigsetmask(SIG_SETMASK, &oldmask, NULL);

	return (errp);
}


nss_status_t
_nss_dns_destr(be, dummy)
	dns_backend_ptr_t	be;
	void			*dummy;
{
	nss_status_t	errp;

	if (be != 0) {
		/* === Should change to invoke ops[ENDENT] ? */
		sigset_t	oldmask, newmask;

		sigfillset(&newmask);
		_thr_sigsetmask(SIG_SETMASK, &newmask, &oldmask);
		_mutex_lock(&one_lane);

		_endhostent(&errp);

		_mutex_unlock(&one_lane);
		_thr_sigsetmask(SIG_SETMASK, &oldmask, NULL);

		free(be);
	}
	return (NSS_SUCCESS);   /* In case anyone is dumb enough to check */
}



nss_backend_t *
_nss_dns_constr(ops, n_ops)
	dns_backend_op_t	ops[];
	int			n_ops;
{
	dns_backend_ptr_t	be;

	if ((be = (dns_backend_ptr_t) malloc(sizeof (*be))) == 0)
		return (0);

	be->ops = ops;
	be->n_ops = n_ops;
	return ((nss_backend_t *) be);
}


static dns_backend_op_t host_ops[] = {
	_nss_dns_destr,
	_nss_dns_endent,
	_nss_dns_setent,
	_nss_dns_getent,
	getbyname,
	getbyaddr
};

nss_backend_t *
_nss_dns_hosts_constr(dummy1, dummy2, dummy3)
	const char	*dummy1, *dummy2, *dummy3;
{
	return (_nss_dns_constr(host_ops,
		sizeof (host_ops) / sizeof (host_ops[0])));
}


#define	ROUND_DOWN(n, align)	(((long)n) & ~((align) - 1))
#define	ROUND_UP(n, align)	ROUND_DOWN(((long)n) + (align) - 1, (align))
#define	DNS_ALIASES	0
#define	DNS_ADDRLIST	1

int
ent2result(he, argp)
	struct hostent		*he;
	nss_XbyY_args_t		*argp;
{
	char		*buffer, *limit;
	int		buflen = argp->buf.buflen;
	int		ret, count, len;
	struct hostent 	*host;
	struct in_addr	*addrp;

	limit = argp->buf.buffer + buflen;
	host = (struct hostent *) argp->buf.result;
	buffer = argp->buf.buffer;

	/* h_addrtype and h_length */
	host->h_addrtype = AF_INET;
	host->h_length = sizeof (u_long);

	/* h_name */
	len = strlen(he->h_name) + 1;
	host->h_name = buffer;
	if (host->h_name + len >= limit)
		return (NSS_STR_PARSE_ERANGE);
	memcpy(host->h_name, he->h_name, len);
	buffer += len;

	/* h_addr_list */
	addrp = (struct in_addr *) ROUND_DOWN(limit, sizeof (*addrp));
	host->h_addr_list = (char **) ROUND_UP(buffer, sizeof (char **));
	ret = dns_netdb_aliases(he->h_addr_list, host->h_addr_list,
		(char **)&addrp, DNS_ADDRLIST, &count);
	if (ret != NSS_STR_PARSE_SUCCESS)
		return (ret);

	/* h_aliases */
	host->h_aliases = host->h_addr_list + count + 1;
	ret = dns_netdb_aliases(he->h_aliases, host->h_aliases,
		(char **)&addrp, DNS_ALIASES, &count);
	if (ret == NSS_STR_PARSE_PARSE)
		ret = NSS_STR_PARSE_SUCCESS;

	return (ret);

}



int
dns_netdb_aliases(from_list, to_list, aliaspp, type, count)
	char	**from_list, **to_list, **aliaspp;
	int	type, *count;
{
	char	*fstr;
	int	cnt = 0;
	int	len;

	*count = 0;
	if ((char *)to_list >= *aliaspp)
		return (NSS_STR_PARSE_ERANGE);

	for (fstr = from_list[cnt]; fstr != NULL; fstr = from_list[cnt]) {
		if (type == DNS_ALIASES)
			len = strlen(fstr) + 1;
		else
			len = sizeof (u_long);
		*aliaspp -= len;
		to_list[cnt] = *aliaspp;
		if (*aliaspp <= (char *)&to_list[cnt+1])
			return (NSS_STR_PARSE_ERANGE);
		memcpy (*aliaspp, fstr, len);
		++cnt;
	}
	to_list[cnt] = NULL;

	*count = cnt;
	if (cnt == 0)
		return (NSS_STR_PARSE_PARSE);

	return (NSS_STR_PARSE_SUCCESS);
}
