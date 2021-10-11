/*
 * $Source: /mit/kerberos/src/appl/sample/RCS/sample_server.c,v $
 * $Author: jtkohl $
 *
 * Copyright 1987, 1988 by the Massachusetts Institute of Technology.
 *
 * For copying and distribution information,
 * please see the file <mit-copyright.h>.
 *
 * sample_server:
 * A sample Kerberos server, which reads a ticket from a TCP socket,
 * decodes it, and writes back the results (in ASCII) to the client.
 *
 * Usage:
 * sample_server
 *
 * file descriptor 0 (zero) should be a socket connected to the requesting
 * client (this will be correct if this server is started by inetd).
 */

#ifndef	lint
static char rcsid_sample_server_c[] =
"$Header: sample_server.c,v 4.0 89/01/23 09:53:36 jtkohl Exp $";
#endif	lint

#include <kerberos/mit-copyright.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <kerberos/krb.h>
#include <syslog.h>

#define	SAMPLE_SERVER	"sample"

#ifdef	DEBUG
#define	TEST_SERVER	"test"
#define	SRVTAB		"/tmp/srvtab"
#else
#define SRVTAB		""
#endif

main()
{
    struct sockaddr_in peername, myname;
    int namelen = sizeof(peername);
    int status, count, len;
    long authopts;
    AUTH_DAT auth_data;
    KTEXT_ST clt_ticket;
    Key_schedule sched;
    char instance[INST_SZ];
    char version[9];
    char retbuf[512];
    char lname[ANAME_SZ];

    /* open a log connection */

#ifndef LOG_DAEMON /* 4.2 syslog */
    openlog("sample_server", 0);
#else /* 4.3 syslog */
    openlog("sample_server", 0, LOG_DAEMON);
#endif /* 4.2 syslog */
    syslog(LOG_INFO, "sample_server: starting up");
    /*
     * To verify authenticity, we need to know the address of the
     * client.
     */
    if (getpeername(0, (struct sockaddr *)&peername, &namelen) < 0) {
	syslog(LOG_ERR, "getpeername: %m");
	exit(1);
    }

    /* for mutual authentication, we need to know our address */
    namelen = sizeof(myname);
    if (getsockname(0, (struct sockaddr *)&myname, &namelen) < 0) {
	syslog(LOG_ERR, "getsocknamename: %m");
	exit(1);
    }

    /* read the authenticator and decode it.  Since we
       don't care what the instance is, we use "*" so that krb_rd_req
       will fill it in from the authenticator */
    (void) strcpy(instance, "*");

    /* we want mutual authentication */
    authopts = KOPT_DO_MUTUAL;
    status = krb_recvauth(authopts, 0, &clt_ticket,
#ifdef DEBUG
		TEST_SERVER,
#else
		SAMPLE_SERVER,
#endif
		instance, &peername, &myname, &auth_data, SRVTAB,
		sched, version);
    if (status != KSUCCESS) {
	syslog(LOG_ERR, "Kerberos error: %s\n", krb_err_txt[status]);
	(void) sprintf(retbuf, "Kerberos error: %s\n",
		       krb_err_txt[status]);
    } else {
	/* Check the version string (8 chars) */
	if (strncmp(version, "VERSION9", 8)) {
	    /* didn't match the expected version */
	    /* could do something different, but we just log an error
	       and continue */
	    version[8] = '\0';		/* make sure null term */
	    syslog(LOG_ERR, "Version mismatch: '%s' isn't 'VERSION9'",
		   version);
	}
	/* now that we have decoded the authenticator, translate
	   the kerberos principal.instance@realm into a local name */
	if (krb_kntoln(&auth_data, lname) != KSUCCESS)
	    strcpy(lname,
		   "*No local name returned by krb_kntoln*");
	/* compose the reply */
	sprintf(retbuf,
		"You are %s.%s@%s (local name %s),\n at address %s, version %s, cksum %ld\n",
		auth_data.pname,
		auth_data.pinst,
		auth_data.prealm,
		lname,
		inet_ntoa(peername.sin_addr),
		version,
		auth_data.checksum);
    }

    /* write back the response */
    if ((count = write(0, retbuf, (len = strlen(retbuf) + 1))) < 0) {
	syslog(LOG_ERR,"write: %m");
	exit(1);
    } else if (count != len) {
	syslog(LOG_ERR, "write count incorrect: %d != %d\n",
		count, len);
	exit(1);
    }

    /* close up and exit */
    close(0);
    exit(0);
}
