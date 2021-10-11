/*
 * Copyright (c) 1986-1994 by Sun Microsystems Inc.
 *
 * Ye olde non-reentrant interface (MT-unsafe, caveat utor)
 *
 * lib/libnsl/nss/gethostent.c
 */

#ident	"@(#)gethostent.c	1.15	94/01/31	SMI"

#include <netdb.h>
#include <nss_dbdefs.h>
#include <rpc/trace.h>
#include <netinet/in.h>
#include <sys/socket.h>

/*
 * Still just a global.  If you want per-thread h_errno,
 * use the reentrant interfaces (gethostbyname_r et al)
 */
int h_errno;

#ifdef	NSS_INCLUDE_UNSAFE

/*
 * Don't free this, even on an endhostent(), because bitter experience shows
 * that there's production code that does getXXXbyYYY(), then endXXXent(),
 * and then continues to use the pointer it got back.
 */
static nss_XbyY_buf_t *buffer;
#define	GETBUF()	\
	NSS_XbyY_ALLOC(&buffer, sizeof (struct hostent), NSS_BUFLEN_HOSTS)
	/* === ?? set ENOMEM on failure?  */

struct hostent *
gethostbyname(nam)
	const char	*nam;
{
	nss_XbyY_buf_t	*b;
	struct hostent	*res = 0;

	trace1(TR_gethostbyname, 0);
	if ((b = GETBUF()) != 0) {
		res = gethostbyname_r(nam,
		    b->result, b->buffer, b->buflen,
		    &h_errno);
	}
	trace1(TR_gethostbyname, 1);
	return (res);
}

struct hostent *
gethostbyaddr(addr, len, type)
	const char	*addr;
	int		len;
	int		type;
{
	nss_XbyY_buf_t	*b;
	struct hostent	*res = 0;

	trace2(TR_gethostbyaddr, 0, len);
	if ((b = GETBUF()) != 0) {
		res = gethostbyaddr_r(addr, len, type,
		    b->result, b->buffer, b->buflen,
		    &h_errno);
	}
	trace2(TR_gethostbyaddr, 1, len);
	return (res);
}

struct hostent *
gethostent()
{
	nss_XbyY_buf_t	*b;
	struct hostent	*res = 0;

	trace1(TR_gethostent, 0);
	if ((b = GETBUF()) != 0) {
		res = gethostent_r(b->result, b->buffer, b->buflen, &h_errno);
	}
	trace1(TR_gethostent, 1);
	return (res);
}

/*
 * Return values: 0 = success, 1 = parse error, 2 = erange ...
 * The structure pointer passed in is a structure in the caller's space
 * wherein the field pointers would be set to areas in the buffer if
 * need be. instring and buffer should be separate areas.
 */
int
str2hostent(instr, lenstr, ent, buffer, buflen)
	const char	*instr;
	int		lenstr;
	void	*ent;
	char	*buffer;
	int	buflen;
{
	struct hostent	*host	= (struct hostent *)ent;
	const char	*p, *addrstart, *limit;
	int		addrlen, res;
	char		addrbuf[18];
	struct in_addr	first_addr;
	struct in_addr	*addrp;
	char		**addrvec;

	trace3(TR_str2hostent, 0, lenstr, buflen);
	if ((instr >= buffer && (buffer + buflen) > instr) ||
	    (buffer >= instr && (instr + lenstr) > buffer)) {
		trace3(TR_str2hostent, 1, lenstr, buflen);
		return (NSS_STR_PARSE_PARSE);
	}

/*
 * ==== need code for multiple IP addresses (from DNS-via-YP).
 *	Talk to maslen for details.
 */

	p = instr;
	limit = p + lenstr;

	while (p < limit && isspace(*p)) {
		p++;
	}
	addrstart = p;
	while (p < limit && !isspace(*p)) {
		p++;
	}
	if (p >= limit) {
		/* Syntax error */
		trace3(TR_str2hostent, 1, lenstr, buflen);
		return (NSS_STR_PARSE_ERANGE);
	}
	addrlen = p - addrstart;
	if (addrlen >= sizeof (addrbuf)) {
		/* Syntax error -- supposed IP address is too long */
		trace3(TR_str2hostent, 1, lenstr, buflen);
		return (NSS_STR_PARSE_ERANGE);
	}
	memcpy(addrbuf, addrstart, addrlen);
	addrbuf[addrlen] = '\0';
	if ((first_addr.s_addr = inet_addr(addrbuf)) == (unsigned long)-1) {
		/* Syntax error -- bogus IP address */
		trace3(TR_str2hostent, 1, lenstr, buflen);
		return (NSS_STR_PARSE_ERANGE);
	}

	/* Allocate space for address and h_addr_list */
	addrp = (struct in_addr *) ROUND_DOWN(buffer + buflen,
	    sizeof (*addrp));
	--addrp;
	addrvec = (char **) ROUND_DOWN(addrp, sizeof (*addrvec));
	addrvec -= 2;

	while (p < limit && isspace(*p)) {
		p++;
	}
	host->h_aliases = _nss_netdb_aliases(p, lenstr - (p - instr),
	    buffer, ((char *)addrvec) - buffer);
	if (host->h_aliases == 0) {
		/* could be parsing error as well */
		res = NSS_STR_PARSE_ERANGE;
	} else {
		/* Success */
		host->h_name = host->h_aliases[0];
		host->h_aliases++;
		res = NSS_STR_PARSE_SUCCESS;
	}
	*addrp = first_addr;
	addrvec[0] = (char *)addrp;
	addrvec[1] = 0;
	host->h_addr_list = addrvec;
	host->h_addrtype  = AF_INET;
	host->h_length    = sizeof (first_addr);

	trace3(TR_str2hostent, 1, lenstr, buflen);
	return (res);
}

#endif	NSS_INCLUDE_UNSAFE
