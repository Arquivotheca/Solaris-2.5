/*LINTLIBRARY*/
#ident	"@(#)dblib_update.c 1.12 92/09/22"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include <config.h>
#include <stdio.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/param.h>
#include <rpc/rpc.h>
#include <netdb.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef USG
#include <sys/utsname.h>
#include <sys/systeminfo.h>
#include <rpc/clnt_soc.h>
#endif
#include <database/dbserv.h>

static struct timeval TIMEOUT = { 60, 0 };

static CLIENT *cl;
static int handle;

static char *domainname = "hsm_libdump";	/* for dgettext() */

#ifdef __STDC__
int
update_start(
	const char *dbserv,	/* the database server machine */
	const char *host)	/* the owner of the dumped files */
#else
int
update_start(dbserv, host)
	char *dbserv;		/* the database server machine */
	char *host;		/* the owner of the dumped files */
#endif
{
	char hostname[BCHOSTNAMELEN+1];
	char fullname[BCHOSTNAMELEN+30], *fnp;
	char *dot;
	struct hostent *hp;
	struct in_addr inaddr;
#ifdef USG
	struct sockaddr_in addr;
	int socket = RPC_ANYSOCK;
#endif

	if (host == NULL) {
#ifndef USG
		if (gethostname(hostname, BCHOSTNAMELEN+1) == -1) {
			perror("start_update/gethostname");
#else
		if (sysinfo(SI_HOSTNAME, hostname, BCHOSTNAMELEN+1) < 0) {
			perror("start_update/sysinfo");
#endif
			return (-1);
		}
		host = hostname;
	}
	if ((hp = gethostbyname(host)) == NULL) {
		(void) fprintf(stderr, "start_update: gethostbyname\n");
		return (-1);
	}
	/*LINTED [h_addr is char * and therefore properly aligned]*/
	inaddr.s_addr = *((u_long *)hp->h_addr);
	(void) strcpy(fullname, host);
	if ((dot = strchr(fullname, '.')) != NULL)
		*dot = '\0';
	(void) strcat(fullname, ".");
	(void) strcat(fullname, inet_ntoa(inaddr));

	if (cl != (CLIENT *)0) {
		/*
		 * already had a client handle -- get rid of that before
		 * allocating a new one.
		 */
		auth_destroy(cl->cl_auth);
		clnt_destroy(cl);
		cl = (CLIENT *)0;
	}

#ifdef USG
	if ((hp = gethostbyname(dbserv)) == NULL) {
		(void) fprintf(stderr, "start_update: gethostbyname\n");
		return (-1);
	}
	/*LINTED [h_addr is char * and therefore properly aligned]*/
	addr.sin_addr.s_addr = *((u_long *)hp->h_addr);
	addr.sin_family = AF_INET;
	addr.sin_port = 0;

	cl = clnttcp_create(&addr, DBSERV, DBVERS, &socket, 0, 0);
#else
	cl = clnt_create(dbserv, DBSERV, DBVERS, "tcp");
#endif
	if (cl == (CLIENT *)0) {
		clnt_pcreateerror((char *)dbserv);
		return (-1);
	}
#ifdef DES
	user2netname(servername, getuid(), NULL);
	cl->cl_auth = authdes_create(servername, 60, NULL, NULL);
#else
	cl->cl_auth = authunix_create_default();
#endif
	fnp = fullname;
	if (clnt_call(cl, START_UPDATE, xdr_wrapstring, (caddr_t)&fnp,
	    xdr_int, (caddr_t)&handle, TIMEOUT) != RPC_SUCCESS) {
		clnt_perror(cl, "update_start");
		auth_destroy(cl->cl_auth);
		clnt_destroy(cl);
		cl = (CLIENT *)0;
		return (-1);
	}

	if (handle < 0) {
		/*
		 * we did not get a valid handle.  Caller cannot proceed
		 * without calling `update_start' again, so he won't need
		 * this connection anymore...
		 */
		auth_destroy(cl->cl_auth);
		clnt_destroy(cl);
		cl = (CLIENT *)0;
	}

	/*
	 * XXX: for now we keep track of handle and client struct
	 * statically since we don't ever believe that one process
	 * will have more than one update active at a time.
	 */
	return (handle);
}

#ifdef __STDC__
int
update_data(int h, const char *file)
#else
int
update_data(h, file)
	int h;
	char *file;
#endif
{
	struct blast_arg b;
	int rc;
	enum clnt_stat rpc_rc;

	if (h != handle) {
		(void) fprintf(stderr, dgettext(domainname,
			"%s: handle botch!\n"), "update_data");
		return (-1);
	}
	if ((b.fp = fopen(file, "r")) == NULL) {
		(void) fprintf(stderr, dgettext(domainname,
			"%s: cannot open `%s'\n"), "update_data", file);
		return (-2);
	}
	b.handle = h;
	rpc_rc = clnt_call(cl, BLAST_FILE, xdr_datafile,
		(caddr_t)&b, xdr_int, (caddr_t)&rc, TIMEOUT);
	if (rpc_rc != RPC_SUCCESS || rc != 0) {
		if (rpc_rc != RPC_SUCCESS)
			clnt_perror(cl, "update_data");
		auth_destroy(cl->cl_auth);
		clnt_destroy(cl);
		cl = (CLIENT *)0;
		rc = -1;
	}
	(void) fclose(b.fp);
	return (rc);
}

int
update_process(h)
	int h;
{
	struct process p;
	int rc;

	if (h != handle) {
		(void) fprintf(stderr,
		    dgettext(domainname, "%s: handle botch!\n"),
			"update_process");
		return (-1);
	}
	p.handle = h;
	p.filesize = 0;
	if (clnt_call(cl, PROCESS_UPDATE, xdr_process, (caddr_t)&p,
			xdr_int, (caddr_t)&rc, TIMEOUT) != RPC_SUCCESS) {
		clnt_perror(cl, "update_process");
		rc = -1;
	}
	auth_destroy(cl->cl_auth);
	clnt_destroy(cl);
	cl = (CLIENT *)0;
	return (rc);
}

#ifdef __STDC__
int
delete_bytape(const char *dbserv, const char *label)
#else
int
delete_bytape(dbserv, label)
	char *dbserv;
	char *label;
#endif
{
	int res;
	CLIENT *clnt;
#ifdef __STDC__
	const char *p;
#else
	char *p;
#endif
#ifdef USG
	struct hostent *hp;
	struct sockaddr_in addr;
	int socket = RPC_ANYSOCK;

	if ((hp = gethostbyname(dbserv)) == NULL) {
		(void) fprintf(stderr, "delete_bytape: gethostbyname\n");
		return (-2);
	}
	/*LINTED [h_addr is char * and therefore properly aligned]*/
	addr.sin_addr.s_addr = *((u_long *)hp->h_addr);
	addr.sin_family = AF_INET;
	addr.sin_port = 0;

	clnt = clnttcp_create(&addr, DBSERV, DBVERS, &socket, 0, 0);
#else
	clnt = clnt_create(dbserv, DBSERV, DBVERS, "tcp");
#endif
	if (clnt == (CLIENT *)0) {
		clnt_pcreateerror((char *)dbserv);
		return (-2);
	}
#ifdef DES
	user2netname(servername, getuid(), NULL);
	clnt->cl_auth = authdes_create(servername, 60, NULL, NULL);
#else
	clnt->cl_auth = authunix_create_default();
#endif
	p = label;
	if (clnt_call(clnt, DELETE_BYTAPE, xdr_tapelabel, (caddr_t)&p,
			xdr_int, (caddr_t)&res, TIMEOUT) != RPC_SUCCESS) {
		clnt_perror(clnt, "delete_bytape");
		clnt_destroy(clnt);
		return (-2);
	}
	clnt_destroy(clnt);
	return (res);
}
