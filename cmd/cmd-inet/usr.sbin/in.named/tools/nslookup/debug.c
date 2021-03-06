/*
 * Copyright (c) 1985,1989 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted provided
 * that: (1) source distributions retain this entire copyright notice and
 * comment, and (2) distributions including binaries display the following
 * acknowledgement:  ``This product includes software developed by the
 * University of California, Berkeley and its contributors'' in the
 * documentation or other materials provided with the distribution and in
 * all advertising materials mentioning features or use of this software.
 * Neither the name of the University nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ident	"@(#)debug.c	1.7	93/04/16 SMI"	/* SVr4.0 1.1 */

/*
 *
 *  debug.c --
 *
 *	Routines to print out packets received from a name server query.
 *
 *	Modified version of 4.3BSD BIND res_debug.c 5.30 6/27/90
 *
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <resolv.h>
#include <netdb.h>
#include "res.h"

/*
 *  Imported from res_debug.c
 */
extern char *_res_resultcodes[];
extern char *_res_opcodes[];

/*
 *  Used to highlight the start of a record when printing it.
 */
#define	INDENT "    ->  "



/*
 * Print the contents of a query.
 * This is intended to be primarily a debugging routine.
 */

Print_query(msg, eom, printHeader)
	char *msg, *eom;
	int printHeader;
{
	Fprint_query(msg, eom, printHeader, stdout);
}

Fprint_query(msg, eom, printHeader, file)
	char *msg, *eom;
	int printHeader;
	FILE *file;
{
	register char *cp;
	register HEADER *hp;
	register int n;
	short class;
	short type;

	/*
	 * Print header fields.
	 */
	hp = (HEADER *)msg;
	cp = msg + sizeof (HEADER);
	if (printHeader || (_res.options & RES_DEBUG2)) {
	    fprintf(file, "    HEADER:\n");
	    fprintf(file, "\topcode = %s", _res_opcodes[hp->opcode]);
	    fprintf(file, ", id = %d", ntohs(hp->id));
	    fprintf(file, ", rcode = %s\n", _res_resultcodes[hp->rcode]);
	    fprintf(file, "\theader flags: ");
	    if (hp->qr) {
		    fprintf(file, " response");
	    } else {
		    fprintf(file, " query");
	    }
	    if (hp->aa)
		    fprintf(file, ", auth. answer");
	    if (hp->tc)
		    fprintf(file, ", truncation");
	    if (hp->rd)
		    fprintf(file, ", want recursion");
	    if (hp->ra)
		    fprintf(file, ", recursion avail.");
	    if (hp->pr)
		    fprintf(file, ", primary");
	    fprintf(file, "\n\tquestions = %d", ntohs(hp->qdcount));
	    fprintf(file, ",  answers = %d", ntohs(hp->ancount));
	    fprintf(file, ",  authority records = %d", ntohs(hp->nscount));
	    fprintf(file, ",  additional = %d\n\n", ntohs(hp->arcount));
	}

	/*
	 * Print question records.
	 */
	if (n = ntohs(hp->qdcount)) {
		fprintf(file, "    QUESTIONS:\n");
		while (--n >= 0 && cp < eom) {
			fprintf(file, "\t");
			cp = Print_cdname(cp, msg, eom, file);
			if (cp == NULL)
				return;
			type = _getshort(cp);
			cp += sizeof (u_short);
			class = _getshort(cp);
			cp += sizeof (u_short);
			fprintf(file, ", type = %s", p_type(type));
			fprintf(file, ", class = %s\n", p_class(class));
		}
	}
	/*
	 * Print authoritative answer records
	 */
	if (n = ntohs(hp->ancount)) {
		fprintf(file, "    ANSWERS:\n");
		while (--n >= 0 && cp < eom) {
			fprintf(file, INDENT);
			cp = Print_rr(cp, msg, eom, file);
			if (cp == NULL)
				return;
		}
	}
	/*
	 * print name server records
	 */
	if (n = ntohs(hp->nscount)) {
		fprintf(file, "    AUTHORITY RECORDS:\n");
		while (--n >= 0 && cp < eom) {
			fprintf(file, INDENT);
			cp = Print_rr(cp, msg, eom, file);
			if (cp == NULL)
				return;
		}
	}
	/*
	 * print additional records
	 */
	if (n = ntohs(hp->arcount)) {
		fprintf(file, "    ADDITIONAL RECORDS:\n");
		while (--n >= 0 && cp < eom) {
			fprintf(file, INDENT);
			cp = Print_rr(cp, msg, eom, file);
			if (cp == NULL)
				return;
		}
	}
	fprintf(file, "\n------------\n");
}


char *
Print_cdname_sub(cp, msg, eom, file, format)
	char *cp, *msg, *eom;
	FILE *file;
	int format;
{
	int n;
	char name[MAXDNAME];
	extern char *strcpy();

	if ((n = dn_expand(msg, eom, cp, name, sizeof (name))) < 0)
		return (NULL);
	if (name[0] == '\0') {
	    (void) strcpy(name, "(root)");
	}
	if (format) {
	    fprintf(file, "%-30s", name);
	} else {
	    fputs(name, file);
	}
	return (cp + n);
}

char *
Print_cdname(cp, msg, eom, file)
	char *cp, *msg, *eom;
	FILE *file;
{
	return (Print_cdname_sub(cp, msg, eom, file, 0));
}

char *
Print_cdname2(cp, msg, eom, file)
	char *cp, *msg, *eom;
	FILE *file;
{
	return (Print_cdname_sub(cp, msg, eom, file, 1));
}

/*
 * Print resource record fields in human readable form.
 */
char *
Print_rr(cp, msg, eom, file)
	char *cp, *msg, *eom;
	FILE *file;
{
	int type, class, dlen, n, c;
	unsigned long rrttl, ttl;
	struct in_addr inaddr;
	char *cp1, *cp2;
	int debug;

	if ((cp = Print_cdname(cp, msg, eom, file)) == NULL) {
		fprintf(file, "(name truncated?)\n");
		return (NULL);			/* compression error */
	}

	if ((cp + 3*sizeof (u_short) + sizeof (u_long)) > eom) {
		fprintf(file, "\n*** Error: hit eom, truncation?\n\n");
		return (NULL);
	} else {
		type = _getshort(cp);
		cp += sizeof (u_short);
		class = _getshort(cp);
		cp += sizeof (u_short);
		rrttl = _getlong(cp);
		cp += sizeof (u_long);
		dlen = _getshort(cp);
		cp += sizeof (u_short);
	}

	debug = _res.options & (RES_DEBUG|RES_DEBUG2);
	if (debug) {
	    if (_res.options & RES_DEBUG2) {
		fprintf(file, "\n\ttype = %s, class = %s, dlen = %d",
			    p_type(type), p_class(class), dlen);
	    }
	    if (type == T_SOA) {
		fprintf(file, "\n\tttl = %lu (%s)", rrttl, p_time(rrttl));
	    }
	    (void) putc('\n', file);
	}

	cp1 = cp;

	/*
	 * Print type specific data, if appropriate
	 */
	switch (type) {
	case T_A:
		switch (class) {
		case C_IN:
		case C_HS:
			if (cp + sizeof (inaddr) > eom) {
				fprintf(file,
				"\n*** Error: hit eom, truncation?\n\n");
				return (NULL);
			} else {
#ifdef SYSV
				memcpy((void *)&inaddr, cp, sizeof (inaddr));
#else
				bcopy(cp, (char *)&inaddr, sizeof (inaddr));
#endif
			}
			if (dlen == 4) {
				fprintf(file, "\tinternet address = %s\n",
					inet_ntoa(inaddr));
				cp += dlen;
			} else if (dlen == 7) {
				if (cp + 6 > eom) {
					fprintf(file,
				"\n*** Error: hit eom, truncation?\n\n");
					return (NULL);
				} else {
					fprintf(file, "\tinternet address = %s",
						inet_ntoa(inaddr));
					fprintf(file, ", protocol = %d", cp[4]);
					fprintf(file, ", port = %d\n",
						(cp[5] << 8) + cp[6]);
					cp += dlen;
				}
			}
			break;
		default:
			fprintf(file, "\taddress, class = %d, len = %d\n",
			    class, dlen);
			cp += dlen;
		}
		break;

	case T_CNAME:
		fprintf(file, "\tcanonical name = ");
		goto doname;

	case T_MG:
		fprintf(file, "\tmail group member = ");
		goto doname;
	case T_MB:
		fprintf(file, "\tmail box = ");
		goto doname;
	case T_MR:
		fprintf(file, "\tmailbox rename = ");
		goto doname;
	case T_MX:
		fprintf(file, "\tpreference = %u", _getshort(cp));
		cp += sizeof (u_short);
		fprintf(file, ", mail exchanger = ");
		goto doname;
	case T_NS:
		fprintf(file, "\tnameserver = ");
		goto doname;
	case T_PTR:
		fprintf(file, "\tname = ");
doname:
		cp = Print_cdname(cp, msg, eom, file);
		(void) putc('\n', file);
		break;

	case T_HINFO:
		if (cp + 1 > eom) {
			fprintf(file, "\n*** Error: hit eom, truncation?\n\n");
			return (NULL);
		} else {
			if (n = *cp++) {
				fprintf(file, "\tCPU = %.*s", n, cp);
				cp += n;
			}
			if (n = *cp++) {
				fprintf(file, "\tOS = %.*s\n", n, cp);
				cp += n;
			}
		}
		break;

	case T_SOA:
		if ((cp + 5*sizeof (u_long)) > eom){
			fprintf(file, "\n*** Error: hit eom, truncation?\n\n");
			return (NULL);
		} else {
			if (!debug)
				(void) putc('\n', file);
			fprintf(file, "\torigin = ");
			cp = Print_cdname(cp, msg, eom, file);
			fprintf(file, "\n\tmail addr = ");
			cp = Print_cdname(cp, msg, eom, file);
			fprintf(file, "\n\tserial = %lu", _getlong(cp));
			cp += sizeof (u_long);
			ttl = _getlong(cp);
			fprintf(file,  "\n\trefresh = %lu (%s)", ttl,
								p_time(ttl));
			cp += sizeof (u_long);
			ttl = _getlong(cp);
			fprintf(file, "\n\tretry   = %lu (%s)", ttl,
								p_time(ttl));
			cp += sizeof (u_long);
			ttl = _getlong(cp);
			fprintf(file, "\n\texpire  = %lu (%s)", ttl,
								p_time(ttl));
			cp += sizeof (u_long);
			ttl = _getlong(cp);
			fprintf(file, "\n\tminimum ttl = %lu (%s)\n", ttl,
								p_time(ttl));
			cp += sizeof (u_long);
		}
		break;

	case T_MINFO:
		if (!debug)
		    (void) putc('\n', file);
		fprintf(file, "\trequests = ");
		cp = Print_cdname(cp, msg, eom, file);
		fprintf(file, "\n\terrors = ");
		cp = Print_cdname(cp, msg, eom, file);
		(void) putc('\n', file);
		break;

	case T_TXT:
		(void) fputs("\ttext = \"", file);
		cp2 = cp1 + dlen;
		while (cp < cp2 && cp < eom) {
			if (n = (unsigned char) *cp++) {
				for (c = n; c > 0 && cp < cp2 && cp < eom; c--)
					if (*cp == '\n') {
					    (void) putc('\\', file);
					    (void) putc(*cp++, file);
					} else
					    (void) putc(*cp++, file);
			}
		}
		(void) fputs("\"\n", file);
		break;

	case T_UINFO:
		fprintf(file, "\tuser info = %s\n", cp);
		cp += dlen;
		break;

	case T_UID:
	case T_GID:
		if (dlen == 4) {
			if (cp + sizeof (long) > eom) {
				fprintf(file,
				    "\n*** Error: hit eom, truncation?\n\n");
				return (NULL);
			} else {
				fprintf(file,
				    "\t%cid = %lu\n", type == T_UID ? 'u' : 'g',
					_getlong(cp));
				cp += sizeof (int);
			}
		} else {
			fprintf(file, "\t%cid of length %d?\n",
			    type == T_UID ? 'u' : 'g', dlen);
			cp += dlen;
		}
		break;

	case T_WKS: {
		struct protoent *protoPtr;

		if (dlen < sizeof (u_long) + 1)
			break;
		if (!debug)
		    (void) putc('\n', file);

		if (cp + sizeof (inaddr) + sizeof (u_long) > eom) {
			fprintf(file, "\n*** Error: hit eom, truncation?\n\n");
			return (NULL);
		} else {
#ifdef SYSV
			memcpy((void *)&inaddr, cp, sizeof (inaddr));
#else
			bcopy(cp, (char *)&inaddr, sizeof (inaddr));
#endif
			cp += sizeof (u_long);
			if ((protoPtr = getprotobynumber(*cp)) != NULL) {
			fprintf(file, "\tinet address = %s, protocol = %s\n\t",
				inet_ntoa(inaddr), protoPtr->p_name);
			} else {
			fprintf(file, "\tinet address = %s, protocol = %d\n\t",
				inet_ntoa(inaddr), *cp);
			}
		}
		cp++;
		n = 0;
		while ((cp < cp1 + dlen) && cp < eom) {
			c = *cp++;
			do {
				struct servent *s;

				if (c & 0200) {
					s = getservbyport(n, NULL);
					if (s != NULL) {
					    fprintf(file, "  %s", s->s_name);
					} else {
					    fprintf(file, " #%d", n);
					}
				}
				c <<= 1;
			} while (++n & 07);
		}
		putc('\n', file);
	    }
	    break;

	case T_NULL:
		fprintf(file, "\tNULL (dlen %d)\n", dlen);
		cp += dlen;
		break;

	default:
		fprintf(file, "\t??? unknown type %d ???\n", type);
		cp += dlen;
	}
	if (_res.options & RES_DEBUG && type != T_SOA) {
	    fprintf(file, "\tttl = %lu (%s)\n", rrttl, p_time(rrttl));
	}
	if (cp != cp1 + dlen) {
		fprintf(file,
			"\n*** Error: record size incorrect (%d != %d)\n\n",
			cp - cp1, dlen);
		cp = NULL;
	}
	if (cp > eom) {
		fprintf(file,
			"\n*** Error: passed eom, truncation?\n\n");
		cp = NULL;
	}
	return (cp);
}
