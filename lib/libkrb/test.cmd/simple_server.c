/*
 * $Source: /mit/kerberos/src/appl/sample/RCS/simple_server.c,v $
 * $Author: jtkohl $
 *
 * Copyright 1989 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information, please see the file
 * <mit-copyright.h>.
 *
 * Simple UDP-based server application.  For demonstration.
 * This program performs no useful function.
 */

#ifndef	lint
static char rcsid_simple_server_c[] =
"$Header: simple_server.c,v 4.2 89/03/23 10:01:34 jtkohl Exp $";
#endif	lint

#include <kerberos/mit-copyright.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <krb_private.h>
#include <kerberos/krb.h>
#include "simple.h"

main()
{
	int sock, i;
	int flags = 0;			/* for recvfrom() */
	int any = 0;			/* don't care (krb_rd_req) */
	struct servent *serv;
	struct hostent *host;
	struct sockaddr_in s_sock;	/* server's address */
	KTEXT_ST k;
	KTEXT ktxt = &k;		/* Kerberos data */
	AUTH_DAT ad;			/* authentication data */
	char hostname[64];		/* for hostname */
	char instance[64];		/* for server instance */

	/* for krb_rd_safe/priv */
	struct sockaddr_in c_sock;	/* client's address */
	MSG_DAT msg_data;		/* received message */

	/* for krb_rd_priv */
	des_key_schedule sched;		/* session key schedule */

	/* Set up server address */
	bzero((char *)&s_sock, sizeof(s_sock));
	s_sock.sin_family = AF_INET;

	/* Look up service */
	if ((serv = getservbyname(SERVICE, "udp")) == NULL) {
		fprintf(stderr, "service unknown: %s/udp\n", SERVICE);
		exit(1);
	}
	s_sock.sin_port = serv->s_port;

	if (gethostname(hostname, sizeof(hostname)) < 0) {
	    perror("gethostname");
	    exit(1);
	}

	if ((host = gethostbyname(hostname)) == (struct hostent *)0) {
	    fprintf(stderr, "%s: host unknown\n", hostname);
	    exit(1);
	}
	bcopy(host->h_addr, (char *)&s_sock.sin_addr, host->h_length);

	/* Open socket */
	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("opening datagram socket");
		exit(1);
	}

	/* Bind the socket */
	if (bind(sock, (struct sockaddr *)&s_sock, sizeof(s_sock))) {
		perror("binding datagram socket");
		exit(1);
	}

#ifdef DEBUG
	printf("socket has port # %d\n", ntohs(s_sock.sin_port));
#endif

	/* GET KRB_MK_REQ MESSAGE */

	i = read(sock, (char *)ktxt->dat, MAX_KTXT_LEN);
	if (i < 0) {
		perror("receiving datagram");
		exit(1);
	}
	printf("Received %d bytes\n", i);
	ktxt->length = i;

	/* Check authentication info */
	strcpy(instance, "*");
	i = krb_rd_req(ktxt, SERVICE, instance, any, &ad, "");
#ifdef DEBUG
	printf("krb_rd_req returned %d: %s\n", i, krb_err_txt[i]);
	printf("krb_rd_req returned instance %s\n", instance);
#endif
	if (i != KSUCCESS) {
		fprintf(stderr, "%s\n", krb_err_txt[i]);
		exit(1);
	}
	printf("Got authentication info from %s%s%s@%s\n", ad.pname,
		*ad.pinst ? "." : "", ad.pinst, ad.prealm);
	
	/* GET KRB_MK_SAFE MESSAGE */

	/* use "recvfrom" so we know client's address */
	i = sizeof(c_sock);
	i = recvfrom(sock, (char *)ktxt->dat, MAX_KTXT_LEN, flags,
		     (struct sockaddr *)&c_sock, &i);
	if (i < 0) {
		perror("receiving datagram");
		exit(1);
	}
#ifdef DEBUG
	printf("&c_sock.sin_addr is %s\n", inet_ntoa(c_sock.sin_addr));
#endif
	printf("Received %d bytes\n", i);

	/* Verify the checksummed message */
	i = krb_rd_safe(ktxt->dat, i, ad.session, &c_sock,
			&s_sock, &msg_data);
#ifdef DEBUG
	printf("krb_rd_safe returned %d: %s\n", i, krb_err_txt[i]);
#endif
	if (i != KSUCCESS) {
		fprintf(stderr, "%s\n", krb_err_txt[i]);
		exit(1);
	}
	printf("Safe message is: %s\n", msg_data.app_data);
	
	/* NOW GET ENCRYPTED MESSAGE */

#ifdef NOENCRYPTION
	bzero((char *)sched, sizeof(sched));
#else
	/* need key schedule for session key */
	des_key_sched(ad.session, sched);
#endif

	/* use "recvfrom" so we know client's address */
	i = sizeof(c_sock);
	i = recvfrom(sock, (char *)ktxt->dat, MAX_KTXT_LEN, flags,
		     (struct sockaddr *)&c_sock, &i);
	if (i < 0) {
		perror("receiving datagram");
		exit(1);
	}
	printf("Received %d bytes\n", i);
	i = krb_rd_priv(ktxt->dat, i, sched, ad.session, &c_sock,
			&s_sock, &msg_data);
#ifdef DEBUG
	printf("krb_rd_priv returned %d: %s\n", i, krb_err_txt[i]);
#endif
	if (i != KSUCCESS) {
		fprintf(stderr, "%s\n", krb_err_txt[i]);
		exit(1);
	}
	printf("Decrypted message is: %s\n", msg_data.app_data);
	return(0);
}
