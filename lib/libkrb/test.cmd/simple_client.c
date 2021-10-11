/*
 * $Source: /mit/kerberos/src/appl/sample/RCS/simple_client.c,v $
 * $Author: jtkohl $
 *
 * Copyright 1989 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please see the file
 * <mit-copyright.h>.
 *
 * Simple UDP-based sample client program.  For demonstration.
 * This program performs no useful function.
 */

#ifndef	lint
static char rcsid_simple_client_c[] =
"$Header: simple_client.c,v 4.2 89/03/23 10:01:22 jtkohl Exp $";
#endif	lint

#include <kerberos/mit-copyright.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <krb_private.h>
#include <kerberos/krb.h>
#include "simple.h"

#define MSG "Hi there!!"		/* message text */

extern long krb_mk_priv();

main(argc, argv)
char **argv;
{
	int sock, i;
	int flags = 0;			/* flags for sendto() */
	long len;
	u_long cksum = 0L;		/* cksum not used */
	char c_realm[REALM_SZ];		/* local Kerberos realm */
	char *s_realm;			/* server's Kerberos realm */
	struct servent *serv;
	struct hostent *host;
	struct sockaddr_in s_sock;	/* server address */
	char hostname[64];		/* local hostname */
	char *shost;			/* server hostname */

	KTEXT_ST k;			/* Kerberos data */
	KTEXT ktxt = &k;

	extern char *krb_realmofhost();

	/* for krb_mk_safe/priv */
	struct sockaddr_in c_sock;	/* client address */
	CREDENTIALS c;			/* ticket & session key */
	CREDENTIALS *cred = &c;

	/* for krb_mk_priv */
	des_key_schedule sched;		/* session key schedule */

	if (argc > 1)
		shost = argv[1];
	else
		shost = "localhost";

	/* Look up service */
	if ((serv = getservbyname(SERVICE, "udp")) == NULL) {
		fprintf(stderr, "service unknown: %s/udp\n", SERVICE);
		exit(1);
	}

	/* Look up server host */
	if ((host = gethostbyname(shost)) == (struct hostent *) 0) {
		fprintf(stderr, "%s: unknown host\n", shost);
		exit(1);
	}

	/* get the server's official hostname for ticketing */
	shost = (char *)malloc(strlen(host->h_name) + 1);
	(void) strcpy(shost, host->h_name);

	/* Set server's address */
	bzero((char *)&s_sock, sizeof(s_sock));
	bcopy(host->h_addr, (char *)&s_sock.sin_addr, host->h_length);
#ifdef DEBUG
	printf("s_sock.sin_addr is %s\n", inet_ntoa(s_sock.sin_addr));
#endif
	s_sock.sin_family = AF_INET;
	s_sock.sin_port = serv->s_port;

	if (gethostname(hostname, sizeof(hostname)) < 0) {
	    perror("gethostname");
	    exit(1);
	}

	if ((host = gethostbyname(hostname)) == (struct hostent *)0) {
	    fprintf(stderr, "%s: unknown host\n", hostname);
	    exit(1);
	}

	/* Open a socket */
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("opening datagram socket");
		exit(1);
	}

	bzero((char *)&c_sock, sizeof(c_sock));
	bcopy(host->h_addr, (char *)&c_sock.sin_addr, host->h_length);
	c_sock.sin_family = AF_INET;

	/* Bind it to set the address; kernel will fill in port # */
	if (bind(sock, (struct sockaddr *)&c_sock, sizeof(c_sock)) < 0) {
	    perror("binding datagram socket");
	    exit(1);
	}
	
	/* Get local realm, not needed, just an example */
	if ((i = krb_get_lrealm(c_realm, 1)) != KSUCCESS) {
		fprintf(stderr, "can't find local Kerberos realm\n");
		exit(1);
	}
	printf("Local Kerberos realm is %s\n", c_realm);

	/* Get Kerberos realm of host */
	s_realm = krb_realmofhost(shost);
#ifdef DEBUG
	printf("Kerberos realm of %s is %s\n", shost, s_realm);
#endif

	/* PREPARE KRB_MK_REQ MESSAGE */

	/* Get credentials for server, create krb_mk_req message */
	if ((i = krb_mk_req(ktxt, SERVICE, shost, s_realm, cksum))
		!= KSUCCESS) {
		fprintf(stderr, "%s\n", krb_err_txt[i]);
		exit(1);
	}
	printf("Got credentials for %s.\n", SERVICE);

	/* Send authentication info to server */
	i = sendto(sock, (char *)ktxt->dat, ktxt->length, flags,
		   (struct sockaddr *)&s_sock, sizeof(s_sock));
	if (i < 0)
		perror("sending datagram message");
	printf("Sent authentication data: %d bytes\n", i);

	/* PREPARE KRB_MK_SAFE MESSAGE */

	/* Get my address */
	bzero((char *) &c_sock, sizeof(c_sock));
	i = sizeof(c_sock);
	if (getsockname(sock, (struct sockaddr *)&c_sock, &i) < 0) {
		perror("getsockname");
		exit(1);
	}

	/* Get session key */
	i = krb_get_cred(SERVICE, shost, s_realm, cred);
#ifdef DEBUG
	printf("krb_get_cred returned %d: %s\n", i, krb_err_txt[i]);
#endif
	if (i != KSUCCESS)
		exit(1);

	/* Make the safe message */
	len = krb_mk_safe(MSG, ktxt->dat, strlen(MSG)+1,
		cred->session, &c_sock, &s_sock);
#ifdef DEBUG
	printf("krb_mk_safe returned %ld\n", len);
#endif

	/* Send it */
	i = sendto(sock, (char *)ktxt->dat, (int) len, flags,
		   (struct sockaddr *)&s_sock, sizeof(s_sock));
	if (i < 0)
		perror("sending safe message");
	printf("Sent checksummed message: %d bytes\n", i);

	/* PREPARE KRB_MK_PRIV MESSAGE */

#ifdef NOENCRYPTION
	bzero((char *)sched, sizeof(sched));
#else
	/* Get key schedule for session key */
	des_key_sched(cred->session, sched);
#endif

	/* Make the encrypted message */
	len = krb_mk_priv(MSG, ktxt->dat, strlen(MSG)+1,
			  sched, cred->session, &c_sock, &s_sock);
#ifdef DEBUG
	printf("krb_mk_priv returned %ld\n", len);
#endif

	/* Send it */
	i = sendto(sock, (char *)ktxt->dat, (int) len, flags,
		   (struct sockaddr *)&s_sock, sizeof(s_sock));
	if (i < 0)
		perror("sending encrypted message");
	printf("Sent encrypted message: %d bytes\n", i);
	return(0);
}
