/*
 * Copyright (c) 1986-1992 by Sun Microsystems Inc.
 */

#ident	"@(#)getnetent_r.c	1.5	93/09/30	SMI"

#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <nss_dbdefs.h>


static int str2netent(const char *, int, void *, char *, int);

static int net_stayopen;
/*
 * Unsynchronized, but it affects only
 * efficiency, not correctness
 */

static DEFINE_NSS_DB_ROOT(db_root);
static DEFINE_NSS_GETENT(context);

static void
_nss_initf_net(p)
	nss_db_params_t	*p;
{
	p->name	= NSS_DBNAM_NETWORKS;
	p->default_config = NSS_DEFCONF_NETWORKS;
}

struct netent *
getnetbyname_r(name, result, buffer, buflen)
	const char	*name;
	struct netent	*result;
	char		*buffer;
	int		buflen;
{
	nss_XbyY_args_t arg;
	nss_status_t	res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, str2netent);
	arg.key.name	= name;
	arg.stayopen	= net_stayopen;
	res = nss_search(&db_root, _nss_initf_net,
		NSS_DBOP_NETWORKS_BYNAME, &arg);
	arg.status = res;
	(void) NSS_XbyY_FINI(&arg);
	return ((struct netent *) arg.returnval);
}

struct netent *
getnetbyaddr_r(net, type, result, buffer, buflen)
	long net;
	int type;
	struct netent	*result;
	char		*buffer;
	int		buflen;
{
	nss_XbyY_args_t arg;
	nss_status_t	res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, str2netent);
	arg.key.netaddr.net = net;
	arg.key.netaddr.type = type;
	arg.stayopen	= net_stayopen;
	res = nss_search(&db_root, _nss_initf_net,
		NSS_DBOP_NETWORKS_BYADDR, &arg);
	arg.status = res;
	(void) NSS_XbyY_FINI(&arg);
	return ((struct netent *) arg.returnval);
}

int
setnetent(stay)
	int stay;
{
	net_stayopen |= stay;	/* === Or maybe just "=" ? */
	nss_setent(&db_root, _nss_initf_net, &context);
	return (0);
}

int
endnetent()
{
	net_stayopen = 0;
	nss_endent(&db_root, _nss_initf_net, &context);
	nss_delete(&db_root);
	return (0);
}

struct netent *
getnetent_r(result, buffer, buflen)
	struct netent	*result;
	char		*buffer;
	int		buflen;
{
	nss_XbyY_args_t arg;
	nss_status_t	res;

	NSS_XbyY_INIT(&arg, result, buffer, buflen, str2netent);
	/* No stayopen flag;  of course you stay open for iteration */
	res = nss_getent(&db_root, _nss_initf_net, &context, &arg);
	arg.status = res;
	(void) NSS_XbyY_FINI(&arg);
	return ((struct netent *) arg.returnval);
}

/* Power-of-two alignments only.. */
#define	ROUND_DOWN(n, align)	(((long)n) & ~((align) - 1))
#define	ROUND_UP(n, align)	ROUND_DOWN(((long)n) + (align) - 1, (align))

/*
 * Return values: 0 = success, 1 = parse error, 2 = erange ...
 * The structure pointer passed in is a structure in the caller's space
 * wherein the field pointers would be set to areas in the buffer if
 * need be. instring and buffer should be separate areas.
 */
static int
str2netent(instr, lenstr, ent, buffer, buflen)
	const char	*instr;
	int		lenstr;
	void	*ent; /* really (struct netnet *) */
	char	*buffer;
	int	buflen;
{
	struct netent	*net	= (struct netent *)ent;
	const char	*p, *numstart, *limit, *namestart;
	int		numlen, namelen = 0;
	char		numbuf[16];

	if ((instr >= buffer && (buffer + buflen) > instr) ||
	    (buffer >= instr && (instr + lenstr) > buffer)) {
		return (NSS_STR_PARSE_PARSE);
	}

	p = instr;
	limit = p + lenstr;

	while (p < limit && isspace(*p)) {
		p++;
	}
	namestart = p;
	while (p < limit && !isspace(*p)) {
		p++;		/* Skip over the canonical name */
	}
	namelen = p - namestart;

	if (buflen <= namelen) { /* not enough buffer */
		return (NSS_STR_PARSE_ERANGE);
	}
	(void) memcpy(buffer, namestart, namelen);
	buffer[namelen] = '\0';
	net->n_name = buffer;

	while (p < limit && isspace(*p)) {
		p++;
	}
	if (p >= limit) {
		/* Syntax error -- no net number */
		return (NSS_STR_PARSE_PARSE);
	}
	numstart = p;
	do {
		p++;		/* Find the end of the net number */
	} while (p < limit && !isspace(*p));
	numlen = p - numstart;
	if (numlen >= sizeof (numbuf)) {
		/* Syntax error -- supposed number is too long */
		return (NSS_STR_PARSE_PARSE);
	}
	(void) memcpy(numbuf, numstart, numlen);
	numbuf[numlen] = '\0';
	net->n_net = inet_network(numbuf);
	net->n_addrtype = AF_INET;

	while (p < limit && isspace(*p)) {
		p++;
	}
	/*
	 * Although nss_files_XY_all calls us with # stripped,
	 * we should be able to deal with it here in order to
	 * be more useful.
	 */
	if (p >= limit || *p == '#') { /* no aliases, no problem */
		char **ptr;

		ptr = (char **) ROUND_UP(buffer + namelen + 1,
							sizeof (char *));
		if ((char *)ptr >= buffer + buflen) {
			net->n_aliases = 0; /* hope they don't try to peek in */
			return (NSS_STR_PARSE_ERANGE);
		} else {
			*ptr = 0;
			net->n_aliases = ptr;
			return (NSS_STR_PARSE_SUCCESS);
		}
	}
	net->n_aliases = _nss_netdb_aliases(p, lenstr - (p - instr),
				buffer + namelen + 1, buflen - namelen - 1);
	return (NSS_STR_PARSE_SUCCESS);
}
