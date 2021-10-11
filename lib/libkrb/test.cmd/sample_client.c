/*
 * $Source: /mit/kerberos/src/appl/sample/RCS/sample_client.c,v $
 * $Author: jtkohl $
 *
 * Copyright 1987, 1988 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information,
 * please see the file <mit-copyright.h>.
 *
 * sample_client:
 * A sample Kerberos client, which connects to a server on a remote host,
 * at port "sample" (be sure to define it in /etc/services)
 * and authenticates itself to the server. The server then writes back
 * (in ASCII) the authenticated name.
 *
 * Usage:
 * sample_client <hostname> <checksum>
 *
 * <hostname> is the name of the foreign host to contact.
 *
 * <checksum> is an integer checksum to be used for the call to krb_mk_req()
 *	and mutual authentication
 *
 * If DEBUG is defined, authenticate to server "test.test".
 */

#ifndef	lint
static char rcsid_sample_client_c[] =
"$Header: sample_client.c,v 4.2 89/04/21 13:27:45 jtkohl Exp $";
#endif	lint

#include <kerberos/mit-copyright.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <krb_private.h>
#include <kerberos/krb.h>

#define SAMPLE_SERVICE	"sample"

#ifdef DEBUG
#define	TEST_SERVICE	"test"
#endif

char *malloc();

main(argc, argv)
int argc;
char **argv;
{
    struct servent *sp;
    struct hostent *hp;
    struct sockaddr_in sin, lsin;
    char *remote_host;
    int status;
    int sock, namelen;
    KTEXT_ST ticket;
    char buf[512];
    long authopts;
    MSG_DAT msg_data;
    CREDENTIALS cred;
    Key_schedule sched;
    long cksum;

    if (argc != 3) {
	fprintf(stderr, "usage: %s <hostname> <checksum>\n",argv[0]);
	exit(1);
    }
    
    /* convert cksum to internal rep */
    cksum = (long) atoi(argv[2]);

    (void) printf("Setting checksum to %ld\n",cksum);

    /* clear out the structure first */
    (void) bzero((char *)&sin, sizeof(sin));

    /* find the port number for knetd */
    sp = getservbyname(SAMPLE_SERVICE, "tcp");
    if (!sp) {
	fprintf(stderr,
		"unknown service %s/tcp; check /etc/services\n",
		SAMPLE_SERVICE);
	exit(1);
    }
    /* copy the port number */
    sin.sin_port = sp->s_port;
    sin.sin_family = AF_INET;

    /* look up the server host */
    hp = gethostbyname(argv[1]);
    if (!hp) {
	fprintf(stderr, "unknown host %s\n",argv[1]);
	exit(1);
    }

    /* copy the hostname into non-volatile storage */
    remote_host = malloc(strlen(hp->h_name) + 1);
    (void) strcpy(remote_host, hp->h_name);

    /* set up the address of the foreign socket for connect() */
    sin.sin_family = hp->h_addrtype;
    (void) bcopy((char *)hp->h_addr,
		 (char *)&sin.sin_addr,
		 sizeof(hp->h_addr));

    /* open a TCP socket */
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
	perror("socket");
	exit(1);
    }

    /* connect to the server */
    if (connect(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
	perror("connect");
	close(sock);
	exit(1);
    }

    /* find out who I am, now that we are connected and therefore bound */
    namelen = sizeof(lsin);
    if (getsockname(sock, (struct sockaddr *) &lsin, &namelen) < 0) {
	perror("getsockname");
	close(sock);
	exit(1);
    }

    /* call Kerberos library routine to obtain an authenticator,
       pass it over the socket to the server, and obtain mutual
       authentication. */

    authopts = KOPT_DO_MUTUAL;
    status = krb_sendauth(authopts, sock, &ticket,
#ifdef DEBUG
			  TEST_SERVICE, TEST_SERVICE,
#else
			  SAMPLE_SERVICE, remote_host,
#endif
			  NULL, cksum, &msg_data, &cred,
			  sched, &lsin, &sin, "VERSION9");
    if (status != KSUCCESS) {
	fprintf(stderr, "%s: cannot authenticate to server: %s\n",
		argv[0], krb_err_txt[status]);
	exit(1);
    }

    /* After we send the authenticator to the server, it will write
       back the name we authenticated to. Read what it has to say. */
    status = read(sock, buf, 512);
    if (status < 0) {
	perror("read");
	exit(1);
    }

    /* make sure it's null terminated before printing */
    if (status < 512)
	buf[status] = '\0';
    printf("The server says:\n%s\n", buf);

    close(sock);
    exit(0);
}
