/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ident	"@(#)snoop_pf.c 1.7	94/06/02 SMI"	/* SunOS	*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/isa_defs.h>

#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netdb.h>
#include <rpc/rpc.h>
#include <setjmp.h>

#include <sys/pfmod.h>
#include "snoop.h"

/*
 * This module generates code for the kernel packet filter.
 * The kernel packet filter is more efficient since it
 * operates without context switching or moving data into
 * the capture buffer.  On the other hand, it is limited
 * in its filtering ability i.e. can't cope with variable
 * length headers, can't compare the packet size, 1 and 4 octet
 * comparisons are awkward, code space is limited to ENMAXFILTERS
 * halfwords, etc.
 * The parser is the same for the user-level packet filter though
 * more limited in the variety of expressions it can generate
 * code for.  If the pf compiler finds an expression it can't
 * handle, it just gives up - we then fall back to compiling
 * for the user-level filter.
 */

extern struct packetfilt pf;
u_short *pfp;
jmp_buf env;

int eaddr;	/* need ethernet addr */

int opstack;	/* operand stack depth */

#define	EQ(val) (strcmp(token, val) == 0)

char *tkp;
char *token;
enum { EOL, ALPHA, NUMBER, FIELD, ADDR_IP, ADDR_ETHER, SPECIAL } tokentype;
u_int tokenval;

enum direction { ANY, TO, FROM };
enum direction dir;

pf_emit(x)
	u_short x;
{
	if (pfp > &pf.Pf_Filter[ENMAXFILTERS - 1])
		longjmp(env, 1);
	*pfp++ = x;
}

pf_codeprint(code, len)
	u_short *code;
	int len;
{
	u_short *pc;
	u_short *plast = code + len;
	int op, action;

	for (pc = code; pc < plast; pc++) {
		printf("\t%3d: ", pc - code);

		op = *pc & 0xfc00;	/* high 10 bits */
		action = *pc & 0x3ff;	/* low   6 bits */

		switch (action) {
		case ENF_PUSHLIT:
			printf("PUSHLIT ");
			break;
		case ENF_PUSHZERO:
			printf("PUSHZERO ");
			break;
#ifdef ENF_PUSHONE
		case ENF_PUSHONE:
			printf("PUSHONE ");
			break;
#endif
#ifdef ENF_PUSHFFFF
		case ENF_PUSHFFFF:
			printf("PUSHFFFF ");
			break;
#endif
#ifdef ENF_PUSHFF00
		case ENF_PUSHFF00:
			printf("PUSHFF00 ");
			break;
#endif
#ifdef ENF_PUSH00FF
		case ENF_PUSH00FF:
			printf("PUSH00FF ");
			break;
#endif
		}

		if (action >= ENF_PUSHWORD)
			printf("PUSHWORD %d ", action - ENF_PUSHWORD);

		switch (op) {
		case ENF_EQ:
			printf("EQ ");
			break;
		case ENF_LT:
			printf("LT ");
			break;
		case ENF_LE:
			printf("LE ");
			break;
		case ENF_GT:
			printf("GT ");
			break;
		case ENF_GE:
			printf("GE ");
			break;
		case ENF_AND:
			printf("AND ");
			break;
		case ENF_OR:
			printf("OR ");
			break;
		case ENF_XOR:
			printf("XOR ");
			break;
		case ENF_COR:
			printf("COR ");
			break;
		case ENF_CAND:
			printf("CAND ");
			break;
		case ENF_CNOR:
			printf("CNOR ");
			break;
		case ENF_CNAND:
			printf("CNAND ");
			break;
		case ENF_NEQ:
			printf("NEQ ");
			break;
		}

		if (action == ENF_PUSHLIT) {
			pc++;
			printf("\n\t%3d:   %d (0x%04x)", pc - code, *pc, *pc);
		}

		printf("\n");
	}
}

/*
 * Emit packet filter code to check a
 * field in the packet for a particular value.
 * Need different code for each field size.
 * Since the pf can only compare 16 bit quantities
 * we have to use masking to compare byte values.
 * Long word (32 bit) quantities have to be done
 * as two 16 bit comparisons.
 */
pf_compare_value(offset, len, val)
	u_int offset, len, val;
{

	switch (len) {
	case 1:
		pf_emit(ENF_PUSHWORD + offset / 2);
#if defined(_BIG_ENDIAN)
		if (offset % 2)
#else
		if (!(offset % 2))
#endif
		{
#ifdef ENF_PUSH00FF
			pf_emit(ENF_PUSH00FF | ENF_AND);
#else
			pf_emit(ENF_PUSHLIT | ENF_AND);
			pf_emit(0x00FF);
#endif
			pf_emit(ENF_PUSHLIT | ENF_EQ);
			pf_emit(val);
		} else {
#ifdef ENF_PUSHFF00
			pf_emit(ENF_PUSHFF00 | ENF_AND);
#else
			pf_emit(ENF_PUSHLIT | ENF_AND);
			pf_emit(0xFF00);
#endif
			pf_emit(ENF_PUSHLIT | ENF_EQ);
			pf_emit(val << 8);
		}
		break;

	case 2:
		pf_emit(ENF_PUSHWORD + offset / 2);
		pf_emit(ENF_PUSHLIT | ENF_EQ);
		pf_emit(htons((u_short)val));
		break;

	case 4:
		pf_emit(ENF_PUSHWORD + offset / 2);
		pf_emit(ENF_PUSHLIT | ENF_EQ);
#if defined(_BIG_ENDIAN)
		pf_emit(val >> 16);
#elif defined(_LITTLE_ENDIAN)
		pf_emit(val & 0xffff);
#else
#error One of _BIG_ENDIAN and _LITTLE_ENDIAN must be defined
#endif
		pf_emit(ENF_PUSHWORD + (offset / 2) + 1);
		pf_emit(ENF_PUSHLIT | ENF_EQ);
#if defined(_BIG_ENDIAN)
		pf_emit(val & 0xffff);
#else
		pf_emit(val >> 16);
#endif
		pf_emit(ENF_AND);
		break;
	}
}

/*
 * Same as above except mask the field value
 * before doing the comparison.
 */
pf_compare_value_mask(offset, len, val, mask)
	u_int offset, len, val;
{
	switch (len) {
	case 1:
		pf_emit(ENF_PUSHWORD + offset / 2);
#if defined(_BIG_ENDIAN)
		if (offset % 2)
#else
		if (!offset % 2)
#endif
		{
			pf_emit(ENF_PUSHLIT | ENF_AND);
			pf_emit(mask & 0x00ff);
			pf_emit(ENF_PUSHLIT | ENF_EQ);
			pf_emit(val);
		} else {
			pf_emit(ENF_PUSHLIT | ENF_AND);
			pf_emit((mask << 8) & 0xff00);
			pf_emit(ENF_PUSHLIT | ENF_EQ);
			pf_emit(val << 8);
		}
		break;

	case 2:
		pf_emit(ENF_PUSHWORD + offset / 2);
		pf_emit(ENF_PUSHLIT | ENF_AND);
		pf_emit(htons((u_short)mask));
		pf_emit(ENF_PUSHLIT | ENF_EQ);
		pf_emit(htons((u_short)val));
		break;

	case 4:
		pf_emit(ENF_PUSHWORD + offset / 2);
		pf_emit(ENF_PUSHLIT | ENF_AND);
		pf_emit(htons((u_short) ((mask >> 16) & 0xffff)));
		pf_emit(ENF_PUSHLIT | ENF_EQ);
		pf_emit(htons((u_short) ((val >> 16) & 0xffff)));

		pf_emit(ENF_PUSHWORD + (offset / 2) + 1);
		pf_emit(ENF_PUSHLIT | ENF_AND);
		pf_emit(htons((u_short) (mask & 0xffff)));
		pf_emit(ENF_PUSHLIT | ENF_EQ);
		pf_emit(htons((u_short) (val & 0xffff)));

		pf_emit(ENF_AND);
		break;
	}
}

/*
 * Generate pf code to match an IP address.
 * This code checks the source address in
 * ARP packets too, though the packet type
 * is never checked.  At worst, we'll let
 * through too many packets.
 */
pf_ipaddr_match(which, hostname)
	enum direction which;
	char *hostname;
{
	u_int addr;
	struct hostent *hp;

	if (isdigit(*hostname)) {
		addr = inet_addr(hostname);
	} else {
		hp = gethostbyname(hostname);
		if (hp == NULL)
			pr_err("host %s not known", hostname);
		addr = *(u_int *) hp->h_addr;
	}

	switch (which) {
	case TO:
		pf_compare_value(30, 4, addr);	/* dst IP addr */
		break;
	case FROM:
		pf_compare_value(26, 4, addr);	/* src IP addr */
		pf_compare_value(28, 4, addr);	/* src ARP addr */
		pf_emit(ENF_OR);
		break;
	case ANY:
		pf_compare_value(26, 4, addr);	/* src IP addr */
		pf_compare_value(30, 4, addr);	/* dst IP addr */
		pf_emit(ENF_OR);
		pf_compare_value(28, 4, addr);	/* src ARP addr */
		pf_emit(ENF_OR);
		break;
	}
}

/*
 * Compare ethernet addresses.
 * This routine cheats and does a simple
 * longword comparison i.e. the first
 * two octets of the address are ignored.
 */
pf_etheraddr_match(which, hostname)
	enum direction which;
	char *hostname;
{
	u_int addr;
	struct ether_addr e, *ep;
	struct ether_addr *ether_aton();

	if (isdigit(*hostname)) {
		ep = ether_aton(hostname);
		if (ep == NULL)
			pr_err("bad ether addr %s", hostname);
	} else {
		if (ether_hostton(hostname, &e))
			pr_err("cannot obtain ether addr for %s",
				hostname);
		ep = &e;
	}
	memcpy(&addr, (u_short *) ep + 1, 4);

	switch (which) {
	case TO:
		pf_compare_value(2, 4, addr);
		break;
	case FROM:
		pf_compare_value(8, 4, addr);
		break;
	case ANY:
		pf_compare_value(2, 4, addr);
		pf_compare_value(8, 4, addr);
		pf_emit(ENF_OR);
		break;
	}
}

/*
 * Emit code to compare the network part of
 * an IP address.
 */
pf_netaddr_match(which, netname)
	enum direction which;
	char *netname;
{
	u_int addr;
	u_int mask = 0xff000000;
	struct netent *np;

	if (isdigit(*netname)) {
		addr = inet_network(netname);
	} else {
		np = getnetbyname(netname);
		if (np == NULL)
			pr_err("net %s not known", netname);
		addr = np->n_net;
	}

	/*
	 * Left justify the address and figure
	 * out a mask based on the supplied address.
	 * Set the mask according to the number of zero
	 * low-order bytes.
	 * Note: this works only for whole octet masks.
	 */
	if (addr) {
		while ((addr & ~mask) != 0) {
			mask |= (mask >> 8);
		}
	}

	switch (which) {
	case TO:
		pf_compare_value_mask(30, 4, addr, mask);
		break;
	case FROM:
		pf_compare_value_mask(26, 4, addr, mask);
		break;
	case ANY:
		pf_compare_value_mask(26, 4, addr, mask);
		pf_compare_value_mask(30, 4, addr, mask);
		pf_emit(ENF_OR);
		break;
	}
}

pf_primary()
{
	int s;

	for (;;) {
		if (tokentype == FIELD)
			break;

		if (EQ("ip")) {
			pf_compare_value(12, 2, ETHERTYPE_IP);
			opstack++;
			next();
			break;
		}

		if (EQ("arp")) {
			pf_compare_value(12, 2, ETHERTYPE_ARP);
			opstack++;
			next();
			break;
		}

		if (EQ("rarp")) {
			pf_compare_value(12, 2, ETHERTYPE_REVARP);
			opstack++;
			next();
			break;
		}

		if (EQ("tcp")) {
			pf_compare_value(23, 1, IPPROTO_TCP);
			pf_compare_value(12, 2, ETHERTYPE_IP);
			pf_emit(ENF_AND);
			opstack++;
			next();
			break;
		}

		if (EQ("udp")) {
			pf_compare_value(23, 1, IPPROTO_UDP);
			pf_compare_value(12, 2, ETHERTYPE_IP);
			pf_emit(ENF_AND);
			opstack++;
			next();
			break;
		}

		if (EQ("icmp")) {
			pf_compare_value(23, 1, IPPROTO_ICMP);
			pf_compare_value(12, 2, ETHERTYPE_IP);
			pf_emit(ENF_AND);
			opstack++;
			next();
			break;
		}

		if (EQ("(")) {
			next();
			s = opstack;
			pf_expression();
			if (!EQ(")"))
				next();
			break;
		}

		if (EQ("to") || EQ("dst")) {
			dir = TO;
			next();
			continue;
		}

		if (EQ("from") || EQ("src")) {
			dir = FROM;
			next();
			continue;
		}

		if (EQ("ether")) {
			eaddr = 1;
			next();
			continue;
		}

		if (EQ("proto")) { /* ignore */
			next();
			continue;
		}

		if (EQ("broadcast")) {
			pf_compare_value(0, 4, 0xffffffff);
			opstack++;
			next();
			break;
		}

		if (EQ("multicast")) {
			pf_compare_value_mask(0, 1, 0x01, 0x01);
			opstack++;
			next();
			break;
		}

		if (EQ("ethertype")) {
			next();
			if (tokentype != NUMBER)
				pr_err("ether type expected");
			pf_compare_value(12, 2, tokenval);
			opstack++;
			next();
			break;
		}

		if (EQ("net") || EQ("dstnet") || EQ("srcnet")) {
			if (EQ("dstnet"))
				dir = TO;
			else if (EQ("srcnet"))
				dir = FROM;
			next();
			pf_netaddr_match(dir, token);
			dir = ANY;
			opstack++;
			next();
			break;
		}

		/*
		 * Give up on anything that's obviously
		 * not a primary.
		 */
		if (EQ("and") || EQ("or") ||
		    EQ("not") || EQ("decnet") || EQ("apple") ||
		    EQ("length") || EQ("less") || EQ("greater") ||
		    EQ("port") || EQ("srcport") || EQ("dstport") ||
		    EQ("rpc") || EQ("gateway") || EQ("nofrag")) {
			break;
		}

		if (EQ("host") || EQ("between") ||
		    tokentype == ALPHA || /* assume its a hostname */
		    tokentype == ADDR_IP ||
		    tokentype == ADDR_ETHER) {
			if (EQ("host") || EQ("between"))
				next();
			if (eaddr || tokentype == ADDR_ETHER)
				pf_etheraddr_match(dir, token);
			else
				pf_ipaddr_match(dir, token);
			dir = ANY;
			eaddr = 0;
			opstack++;
			next();
			break;
		}

		break;	/* unknown token */
	}
}

pf_alternation()
{
	int s = opstack;

	pf_primary();
	for (;;) {
		if (EQ("and"))
			next();
		pf_primary();
		if (opstack != s + 2)
			break;
		pf_emit(ENF_AND);
		opstack--;
	}
}

pf_expression()
{
	int s = opstack;

	pf_alternation();
	while (EQ("or") || EQ(",")) {
		next();
		pf_alternation();
		pf_emit(ENF_OR);
		opstack--;
	}
}

/*
 * Attempt to compile the expression
 * in the string "e".  If we can generate
 * pf code for it then return 1 - otherwise
 * return 0 and leave it up to the user-level
 * filter.
 */
pf_compile(e, print)
	char *e;
	int print;
{
	e = strdup(e);
	if (e == NULL)
		pr_err("no mem");
	tkp = e;
	dir = ANY;

	pfp = &pf.Pf_Filter[0];
	if (setjmp(env)) {
		return (0);
	}
	next();
	pf_expression();
	if (tokentype != EOL) {
		return (0);
	}
	pf.Pf_FilterLen = pfp - &pf.Pf_Filter[0];
	pf.Pf_Priority = 5;	/* unimportant, so long as > 2 */
	if (print)
		pf_codeprint(&pf.Pf_Filter[0], pf.Pf_FilterLen);
	return (1);
}
