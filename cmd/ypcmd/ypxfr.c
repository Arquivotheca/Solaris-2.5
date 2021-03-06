/*
 * Copyright (c) 1986-1992 by Sun Microsystems Inc.
 */

#ident	"@(#)ypxfr.c	1.9	95/09/07 SMI"

/*
 * This is a user command which gets a NIS data base from some running
 * server, and gets it to the local site by using the normal NIS client
 * enumeration functions.  The map is copied to a temp name, then the real
 * map is removed and the temp map is moved to the real name.  ypxfr then
 * sends a "YPPROC_CLEAR" message to the local server to insure that he will
 * not hold a removed map open, so serving an obsolete version.
 *
 * ypxfr [ -h <host> ] [ -d <domainname> ]
 *		[ -s <domainname> ] [-f] [-c] [-C tid prot name] map
 *
 * If the host is ommitted, ypxfr will attempt to discover the master by
 * using normal NIS services.  If it can't get the record, it will use
 * the address of the callback, if specified. If the host is specified
 * as an internet address, no NIS services need to be locally available.
 *
 * If the domain is not specified, the default domain of the local machine
 * is used.
 *
 * If the -f flag is used, the transfer will be done even if the master's
 * copy is not newer than the local copy.
 *
 * The -c flag suppresses the YPPROC_CLEAR request to the local ypserv.  It
 * may be used if ypserv isn't currently running to suppress the error message.
 *
 * The -C flag is used to pass callback information to ypxfr when it is
 * activated by ypserv.  The callback information is used to send a
 * yppushresp_xfr message with transaction id "tid" to a yppush process
 * speaking a transient protocol number "prot".  The yppush program is
 * running on the host "name".
 *
 * The -s option is used to specify a source domain which may be
 * different from the destination domain, for transfer of maps
 * that are identical in different domains (e.g. services.byname)
 *
 */

#include <ndbm.h>
#undef NULL
#define	DATUM

#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <netdb.h>
#include <netconfig.h>
#include <netdir.h>
#include <rpc/rpc.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <dirent.h>
#include <rpcsvc/ypclnt.h>
#include <rpcsvc/yp_prot.h>
#include <unistd.h>
#include <stdlib.h>
#include "ypdefs.h"
#include "yp_b.h"

USE_YP_MASTER_NAME
USE_YP_SECURE
USE_YP_INTERDOMAIN
USE_YP_LAST_MODIFIED
USE_YPDBPATH
USE_DBM

# define PARANOID 1	/* make sure maps have the right # entries */

#define	CALLINTER_TRY 10		/* Seconds between callback tries */
#define	CALLTIMEOUT CALLINTER_TRY*6	/* Total timeout for callback */

DBM *db;

int	debug = FALSE;
int	treepush = FALSE;
#define	TREEPUSH 1
int	defwrite = TRUE;

char *domain = NULL;
char *source = NULL;
char *map = NULL;
char *master = NULL;
char *pushhost = NULL;
/* The name of the xfer peer as specified as a
 *   -h option, -C name option or from querying the NIS
 */
struct dom_binding master_server; /* To talk to above */
unsigned int master_prog_vers;	/* YPVERS (barfs at YPOLDVERS !) */
char *master_name = NULL;	/* Map's master as contained in the map */
unsigned *master_version = NULL; /* Order number as contained in the map */
char *master_ascii_version;	/* ASCII order number as contained in the map */
bool fake_master_version = FALSE;
/*
 * TRUE only if there's no order number in
 *  the map, and the user specified -f
 */
bool force = FALSE;		/* TRUE iff user specified -f flag */
bool logging = FALSE;		/* TRUE iff no tty, but log file exists */
bool check_count = FALSE;	/* TRUE causes counts to be checked */
bool send_clear = TRUE;		/* FALSE iff user specified -c flag */
bool callback = FALSE;
/* TRUE iff -C flag set.  tid, proto and name
 * will be set to point to the command line args.
 */
bool secure_map = FALSE;	/* TRUE if there is yp_secure in the map */
bool interdomain_map = FALSE;
/* TRUE if there is yp_interdomain in either
 * the local or the master version of the map
 */
int interdomain_sz = 0;		/* Size of the interdomain value */
#define	UDPINTER_TRY 10		/* Seconds between tries for udp */
#define	UDPTIMEOUT UDPINTER_TRY*4	/* Total timeout for udp */
#define	CALLINTER_TRY 10	/* Seconds between callback tries */
#define	CALLTIMEOUT CALLINTER_TRY*6	/* Total timeout for callback */
struct timeval udp_timeout = { UDPTIMEOUT, 0};
struct timeval tcp_timeout = { 180, 0}; /* Timeout for map enumeration */

char *interdomain_value; 	/* place to store the interdomain value */
char *tid;
char *proto;
int entry_count;		/* counts entries in the map */
char logfile[] = "/var/yp/ypxfr.log";
static char err_usage[] =
"Usage:\n\
ypxfr [-f] [ -h host ] [ -d domainname ]\n\
	[ -s domainname ] [-c] [-C tid prot servname ] map\n\n\
where\n\
	-f forces transfer even if the master's copy is not newer.\n\
	host is the server from where the map should be transfered\n\
	-d domainname is specified if other than the default domain\n\
	-s domainname is a source for the map that is same across domains\n\
	-c inhibits sending a \"Clear map\" message to the local ypserv.\n\
	-C is for use only by ypserv to pass callback information.\n";
char err_bad_args[] =
	"%s argument is bad.\n";
char err_cant_get_kname[] =
	"Can't get %s back from system call.\n";
char err_null_kname[] =
	"%s hasn't been set on this machine.\n";
char err_bad_hostname[] = "hostname";
char err_bad_mapname[] = "mapname";
char err_bad_domainname[] = "domainname";
char err_udp_failure[] =
	"Can't set up a udp connection to ypserv on host %s.\n";
char yptempname_prefix[] = "ypxfr_map.";
char ypbkupname_prefix[] = "ypxfr_bkup.";

void get_command_line_args();
bool bind_to_server();
bool ping_server();
bool  get_private_recs();
bool get_order();
bool get_v1order();
bool get_v2order();
bool get_misc_recs();
bool get_master_name();
bool get_v1master_name();
bool get_v2master_name();
void find_map_master();
bool move_map();
unsigned get_local_version();
void mkfilename();
void mk_tmpname();
bool rename_map();
bool check_map_existence();
bool get_map();
bool add_private_entries();
bool new_mapfiles();
void del_mapfiles();
void set_output();
void logprintf();
bool send_ypclear();
void xfr_exit();
void send_callback();
int ypall_callback();
int map_yperr_to_pusherr();
extern CLIENT *__yp_clnt_create_rsvdport();

extern int errno;


/*
 * This is the mainline for the ypxfr process.
 */

void
main(argc, argv)
	int argc;
	char **argv;

{

	static char default_domain_name[YPMAXDOMAIN];
	static unsigned big = 0xffffffff;
	int status;

	set_output();

	get_command_line_args(argc, argv);

	if (!domain) {

		if (!getdomainname(default_domain_name, YPMAXDOMAIN)) {
			domain = default_domain_name;
		} else {
			logprintf(err_cant_get_kname,
			    err_bad_domainname);
			xfr_exit(YPPUSH_RSRC);
		}

		if (strlen(domain) == 0) {
			logprintf(err_null_kname,
			    err_bad_domainname);
			xfr_exit(YPPUSH_RSRC);
		}
	}
	if (!source)
		source = domain;

	if (!master) {
		find_map_master();
	}
	/*
	 * if we were unable to get the master name, either from
	 * the -h option or from -C "name" option or from NIS,
	 * we are doomed !
	 */
	if (!master) {
		xfr_exit(YPPUSH_MADDR);
	}

	if (!bind_to_server(master, &master_server,
	    &master_prog_vers, &status)) {
		xfr_exit(status);
	}

	if (!get_private_recs(&status)) {
		xfr_exit(status);
	}

	if (!master_version) {

		if (force) {
			master_version = &big;
			fake_master_version = TRUE;
		} else {
			logprintf(
"Can't get order number for map %s from server at %s: use the -f flag.\n",
			    map, master);
			xfr_exit(YPPUSH_FORCE);
		}
	}

	if (!move_map(&status)) {
		xfr_exit(status);
	}

	if (send_clear && !send_ypclear(&status)) {
		xfr_exit(status);
	}

	if (logging) {
		logprintf("Transferred map %s from %s (%d entries).\n",
		    map, master, entry_count);
	}

	xfr_exit(YPPUSH_SUCC);
	/* NOTREACHED */
}

/*
 * This decides whether we're being run interactively or not, and, if not,
 * whether we're supposed to be logging or not.  If we are logging, it sets
 * up stderr to point to the log file, and sets the "logging"
 * variable.  If there's no logging, the output goes in the bit bucket.
 * Logging output differs from interactive output in the presence of a
 * timestamp, present only in the log file.  stderr is reset, too, because it
 * it's used by various library functions, including clnt_perror.
 */
void
set_output()
{
	if (!isatty(1)) {
		if (access(logfile, W_OK)) {
			(void) freopen("/dev/null", "w", stderr);
		} else {
			(void) freopen(logfile, "a", stderr);
			logging = TRUE;
		}
	}
}

/*
 * This constructs a logging record.
 */
void
logprintf(arg1, arg2, arg3, arg4, arg5, arg6, arg7)
/*VARARGS*/
{
	struct timeval t;

	fseek(stderr, 0, 2);
	if (logging) {
		(void) gettimeofday(&t, NULL);
		(void) fprintf(stderr, "%19.19s: ", ctime(&t.tv_sec));
	}
	(void) fprintf(stderr, (char *)arg1, arg2, arg3, arg4, arg5, arg6, arg7);
	fflush(stderr);
}

/*
 * This does the command line argument processing.
 */
void
get_command_line_args(argc, argv)
	int argc;
	char **argv;

{
	argv++;

	if (argc < 2) {
		logprintf(err_usage);
		xfr_exit(YPPUSH_BADARGS);
	}

	while (--argc) {

		if ((*argv)[0] == '-') {

			switch ((*argv)[1]) {

			case 'f': {
				force = TRUE;
				argv++;
				break;
			}

			case 'D': {
				debug = TRUE;
				argv++;
				break;
			}

			case 'T': {
				treepush = TRUE;
				argv++;
				break;
			}
			case 'P': {
				check_count = TRUE;
				argv++;
				break;
			}
			case 'W': {
				defwrite = FALSE;
				argv++;
				break;
			}
			case 'c': {
				send_clear = FALSE;
				argv++;
				break;
			}

			case 'h': {

				if (argc > 1) {
					argv++;
					argc--;
					master = *argv;
					argv++;

					if (strlen(master) > 256) {
						logprintf(
						    err_bad_args,
						    err_bad_hostname);
						xfr_exit(YPPUSH_BADARGS);
					}

				} else {
					logprintf(err_usage);
					xfr_exit(YPPUSH_BADARGS);
				}

				break;
			}

			case 'd':
				if (argc > 1) {
					argv++;
					argc--;
					domain = *argv;
					argv++;

					if (strlen(domain) > YPMAXDOMAIN) {
						logprintf(
						    err_bad_args,
						    err_bad_domainname);
						xfr_exit(YPPUSH_BADARGS);
					}

				} else {
					logprintf(err_usage);
					xfr_exit(YPPUSH_BADARGS);
				}
				break;

			case 's':
				if (argc > 1) {
					argv++;
					argc--;
					source = *argv;
					argv++;

					if (strlen(source) > YPMAXDOMAIN) {
						logprintf(
						    err_bad_args,
						    err_bad_domainname);
						xfr_exit(YPPUSH_BADARGS);
					}

				} else {
					logprintf(err_usage);
					xfr_exit(YPPUSH_BADARGS);
				}
				break;

			case 'C':
			    if (argc > 3) {
				callback = TRUE;
				tid = *(++argv);
				proto = *(++argv);
				pushhost = *(++argv);
				if (strlen(pushhost) > 256) {
				    logprintf(err_bad_args, err_bad_hostname);

				    xfr_exit(YPPUSH_BADARGS);
				}
				argc -= 3;
				argv++;
			    } else {
				logprintf(err_usage);
				xfr_exit(YPPUSH_BADARGS);
			    }
			    break;

			case 'b': {
				interdomain_map = TRUE;
				interdomain_value = "";
				interdomain_sz = 0;
				argv++;
				break;
			}


			default: {
				logprintf(err_usage);
				xfr_exit(YPPUSH_BADARGS);
			}

			}

		} else {

			if (!map) {
				map = *argv;
				argv++;

				if (strlen(map) > YPMAXMAP) {
					logprintf(err_bad_args,
					err_bad_mapname);
					xfr_exit(YPPUSH_BADARGS);
				}

			} else {
				logprintf(err_usage);
				xfr_exit(YPPUSH_BADARGS);
			}
		}
	}

	if (!map) {
		logprintf(err_usage);
		xfr_exit(YPPUSH_BADARGS);
	}
}

/*
 * This tries to get the master name for the named map, from any
 * server's version, using the vanilla NIS client interface.  If we get a
 * name back, the global "master" gets pointed to it.
 */
void
find_map_master()
{
	int err;

	if (err = __yp_master_rsvdport(source, map, &master)) {
		logprintf("Can't get master of %s. Reason: %s.\n", map,
		    yperr_string(err));
	}

	yp_unbind(source);
}

#ifdef TREEPUSH
chk_treepush(name)
char *name;
{
	char inmap[256];
	char inkey[256];
	int inkeylen;
	char *outval;
	int outvallen;
	int err;
	outval = NULL;
	inkey[0] = 0;
	strcpy(inmap, "ypslaves.");
	strcat(inmap, name);
	gethostname(inkey, 256);
	inkeylen = strlen(inkey);

	err = yp_match(source, inmap, inkey, inkeylen, &outval, &outvallen);
	yp_unbind(source);
	return (err);
}
#endif

/*
 * This sets up a udp connection to speak the correct program and version
 * to a NIS server.  vers is set to YPVERS, doesn't give a damn about
 * YPOLDVERS.
 */
bool
bind_to_server(host, pdomb, vers, status)
	char *host;
	struct dom_binding *pdomb;
	unsigned int *vers;
	int *status;
{
	if (ping_server(host, pdomb, YPVERS, status)) {
		*vers = YPVERS;
		return (TRUE);
	} else
		return (FALSE);
}

/*
 * This sets up a UDP channel to a server which is assumed to speak an input
 * version of YPPROG.  The channel is tested by pinging the server.  In all
 * error cases except "Program Version Number Mismatch", the error is
 * reported, and in all error cases, the client handle is destroyed and the
 * socket associated with the channel is closed.
 */
bool
ping_server(host, pdomb, vers, status)
	char *host;
	struct dom_binding *pdomb;
	unsigned int vers;
	int *status;
{
	enum clnt_stat rpc_stat;

	if (pdomb->dom_client = __yp_clnt_create_rsvdport(host, YPPROG, vers,
						     "udp", 0, 0)) {

		/*
		 * if we are on a c2 system, we should only accept data
		 * from a server which is on a reserved port.
		 */
	/*
	 * NUKE this for 5.0DR.

		if (issecure() &&
		    (pdomb->dom_server_addr.sin_family != AF_INET ||
		    pdomb->dom_server_addr.sin_port >= IPPORT_RESERVED)) {
			clnt_destroy(pdomb->dom_client);
			close(pdomb->dom_socket);
			(void) logprintf("bind_to_server: \
				server is not using a privileged port\n");
			*status = YPPUSH_YPERR;
			return (FALSE);
		}
	*/

		rpc_stat = clnt_call(pdomb->dom_client, YPBINDPROC_NULL,
		    xdr_void, 0, xdr_void, 0, udp_timeout);

		if (rpc_stat == RPC_SUCCESS) {
			return (TRUE);
		} else {
			clnt_destroy(pdomb->dom_client);
			if (rpc_stat != RPC_PROGVERSMISMATCH) {
				(void) clnt_perror(pdomb->dom_client,
				    "ypxfr: bind_to_server clnt_call error");
			}

			*status = YPPUSH_RPC;
			return (FALSE);
		}
	} else {
		logprintf("bind_to_server __clnt_create_rsvd error");
		(void) clnt_pcreateerror("");
		fflush(stderr);
		*status = YPPUSH_RPC;
		return (FALSE);
	}
}

/*
 * This gets values for the YP_LAST_MODIFIED and YP_MASTER_NAME keys from the
 * master server's version of the map.  Values are held in static variables
 * here.  In the success cases, global pointer variables are set to point at
 * the local statics.
 */
bool
get_private_recs(pushstat)
	int *pushstat;
{
	static char anumber[20];
	static unsigned number;
	static char name[YPMAXPEER + 1];
	int status;

	status = 0;

	if (get_order(anumber, &number, &status)) {
		master_version = &number;
		master_ascii_version = anumber;
		if (debug) fprintf(stderr,
			"ypxfr: Master Version is %s\n", master_ascii_version);
	} else {

		if (status != 0) {
			*pushstat = status;
			if (debug) fprintf(stderr,
		"ypxfr: Couldn't get map's master version number, \
		status was %d\n", status);
			return (FALSE);
		}
	}

	if (get_master_name(name, &status)) {
		master_name = name;
		if (debug) fprintf(stderr,
			"ypxfr: Maps master is '%s'\n", master_name);
	} else {

		if (status != 0) {
			*pushstat = status;
			if (debug) fprintf(stderr,
		"ypxfr: Couldn't get map's master name, status was %d\n",
			status);
			return (FALSE);
		}
		master_name = master;
	}

	if (debug)
		fprintf(stderr,
			"ypxfr: Getting private records from master.\n");
	if (get_misc_recs(&status)) {
		if (debug)
			fprintf(stderr,
		    "ypxfr: Masters map %s secure and %s an interdomain map.\n",
			(secure_map) ? "is" : "is not",
			(interdomain_map) ? "is" : "is not");
	} else {
		if (status != 0) {
			*pushstat = status;
			if (debug)
				fprintf(stderr,
	"ypxfr: Couldn't get state of secure and interdomain flags in map.\n");
			return (FALSE);
		}
	}

	return (TRUE);
}

/*
 * This gets the map's order number from the master server
 */
bool
get_order(an, n, pushstat)
	char *an;
	unsigned *n;
	int *pushstat;
{
	if (master_prog_vers == YPVERS) {
		return (get_v2order(an, n, pushstat));
	} else
		return (FALSE);
}

bool
get_v2order(an, n, pushstat)
	char *an;
	unsigned *n;
	int *pushstat;
{
	struct ypreq_nokey req;
	struct ypresp_order resp;
	int retval;

	req.domain = source;
	req.map = map;

	/*
	 * Get the map''s order number, null-terminate it and store it,
	 * and convert it to binary and store it again.
	 */
	retval = FALSE;

	if ((enum clnt_stat) clnt_call(master_server.dom_client,
	    YPPROC_ORDER, (xdrproc_t)xdr_ypreq_nokey, (char *)&req,
	    (xdrproc_t) xdr_ypresp_order, (char *) &resp,
	    udp_timeout) == RPC_SUCCESS) {

		if (resp.status == YP_TRUE) {
			sprintf(an, "%d", resp.ordernum);
			*n = resp.ordernum;
			retval = TRUE;
		} else if (resp.status != YP_BADDB) {
			*pushstat = ypprot_err(resp.status);

			if (!logging) {
				logprintf(
	"(info) Can't get order number from ypserv at %s.  Reason: %s.\n",
				    master, yperr_string(
				    ypprot_err(resp.status)));
			}
		}

		CLNT_FREERES(master_server.dom_client,
			(xdrproc_t)xdr_ypresp_order,
		    (char *)&resp);
	} else {
		*pushstat = YPPUSH_RPC;
		logprintf("ypxfr(get_v2order) RPC call to %s failed", master);
		clnt_perror(master_server.dom_client, "");
	}

	return (retval);
}

/*
 * Pick up the state of the YP_SECURE and YP_INTERDOMAIN records from the
 * master. Only works on 4.0 V2 masters that will match a YP_ private key
 * when asked to explicitly.
 */
bool
get_misc_recs(pushstat)
	int *pushstat;
{
	struct ypreq_key req;
	struct ypresp_val resp;
	int retval;

	req.domain = source;
	req.map = map;
	req.keydat.dptr   = yp_secure;
	req.keydat.dsize  = yp_secure_sz;

	resp.valdat.dptr = NULL;
	resp.valdat.dsize = 0;

	/*
	 * Get the value of the IS_SECURE key in the map.
	 */
	retval = FALSE;

	if (debug)
		fprintf(stderr, "ypxfr: Checking masters secure key.\n");
	if ((enum clnt_stat) clnt_call(master_server.dom_client,
	    YPPROC_MATCH, (xdrproc_t)xdr_ypreq_key, (char *)&req,
	(xdrproc_t)xdr_ypresp_val, (char *)&resp,
	    udp_timeout) == RPC_SUCCESS) {
		if (resp.status == YP_TRUE) {
			if (debug)
				fprintf(stderr, "ypxfr: SECURE\n");
			secure_map = TRUE;
			retval = TRUE;
		} else if ((resp.status != YP_NOKEY) &&
			    (resp.status != YP_VERS)) {
			*pushstat = ypprot_err(resp.status);

			if (!logging) {
				logprintf(
	"(info) Can't get secure flag from ypserv at %s.  Reason: %s.\n",
				    master, yperr_string(
				    ypprot_err(resp.status)));
			}
		}

		CLNT_FREERES(master_server.dom_client,
			(xdrproc_t)xdr_ypresp_val,
		    (char *) &resp);
	} else {
		*pushstat = YPPUSH_RPC;
		logprintf("ypxfr(get_misc_recs) RPC call to %s failed", master);
		clnt_perror(master_server.dom_client, "");
	}

	if (debug)
		fprintf(stderr, "ypxfr: Checking masters INTERDOMAIN key.\n");
	req.keydat.dptr   = yp_interdomain;
	req.keydat.dsize  = yp_interdomain_sz;

	resp.valdat.dptr = NULL;
	resp.valdat.dsize = 0;

	/*
	 * Get the value of the INTERDOMAIN key in the map.
	 */

	if ((enum clnt_stat) clnt_call(master_server.dom_client,
	    YPPROC_MATCH, (xdrproc_t)xdr_ypreq_key, (char *)&req,
	(xdrproc_t) xdr_ypresp_val, (char *)&resp,
	    udp_timeout) == RPC_SUCCESS) {
		if (resp.status == YP_TRUE) {
			if (debug)
				fprintf(stderr, "ypxfr: INTERDOMAIN\n");
			interdomain_map = TRUE;
			interdomain_value = (char *)malloc(resp.valdat.dsize+1);
			(void) memmove(interdomain_value, resp.valdat.dptr,
					resp.valdat.dsize);
			*(interdomain_value+resp.valdat.dsize) = '\0';
			interdomain_sz = resp.valdat.dsize;
			retval = TRUE;
		} else if ((resp.status != YP_NOKEY) &&
			    (resp.status != YP_VERS)) {
			*pushstat = ypprot_err(resp.status);

			if (!logging) {
				logprintf(
	"(info) Can't get interdomain flag from ypserv at %s.  Reason: %s.\n",
				    master, yperr_string(
				    ypprot_err(resp.status)));
			}
		}

		CLNT_FREERES(master_server.dom_client,
			(xdrproc_t)xdr_ypresp_val,
		    (char *)&resp);
	} else {
		*pushstat = YPPUSH_RPC;
		logprintf("ypxfr(get_misc_recs) RPC call to %s failed", master);
		clnt_perror(master_server.dom_client, "");
	}


	return (retval);
}

/*
 * This gets the map's master name from the master server
 */
bool
get_master_name(name, pushstat)
	char *name;
	int *pushstat;
{
	if (master_prog_vers == YPVERS) {
		return (get_v2master_name(name, pushstat));
	} else
		return (FALSE);
}

bool
get_v2master_name(name, pushstat)
	char *name;
	int *pushstat;
{
	struct ypreq_nokey req;
	struct ypresp_master resp;
	int retval;

	req.domain = source;
	req.map = map;
	resp.master = NULL;
	retval = FALSE;

	if ((enum clnt_stat) clnt_call(master_server.dom_client,
	    YPPROC_MASTER, (xdrproc_t)xdr_ypreq_nokey, (char *)&req,
	    (xdrproc_t)xdr_ypresp_master, (char *) &resp,
	    udp_timeout) == RPC_SUCCESS) {

		if (resp.status == YP_TRUE) {
			strcpy(name, resp.master);
			retval = TRUE;
		} else if (resp.status != YP_BADDB) {
			*pushstat = ypprot_err(resp.status);

			if (!logging) {
				logprintf(
"(info) Can't get master name from ypserv at %s. Reason: %s.\n",
				    master, yperr_string(
				    ypprot_err(resp.status)));
			}
		}

		CLNT_FREERES(master_server.dom_client,
			(xdrproc_t)xdr_ypresp_master,
		    (char *)&resp);
	} else {
		*pushstat = YPPUSH_RPC;
		logprintf(
		    "ypxfr(get_v2master_name) RPC call to %s failed", master);
		clnt_perror(master_server.dom_client, "");
	}

	return (retval);
}

/*
 * This does the work of transferrring the map.
 */
bool
move_map(pushstat)
	int *pushstat;
{
	unsigned local_version;
	char map_name[MAXNAMLEN + 1];
	char tmp_name[MAXNAMLEN + 1];
	char bkup_name[MAXNAMLEN + 1];
	char an[11];
	unsigned n;
	datum key;
	datum val;
	int  hgstatus;

	mkfilename(map_name);

	if (!force) {
		local_version = get_local_version(map_name);
		if (debug) fprintf(stderr,
			"ypxfr: Local version of map '%s' is %d\n",
			map_name, local_version);

		if (local_version >= *master_version) {
			logprintf(
			    "Map %s at %s is not more recent than local.\n",
			    map, master);
			*pushstat = YPPUSH_AGE;
			return (FALSE);
		}
	}

	mk_tmpname(yptempname_prefix, tmp_name);

	if (!new_mapfiles(tmp_name)) {
		logprintf(
		    "Can't create temp map %s.\n", tmp_name);
		*pushstat = YPPUSH_FILE;
		return (FALSE);
	}

	if ((hgstatus = ypxfrd_getdbm(tmp_name, master, source, map)) < 0)
	{
	logprintf(
"(info) %s %s %s ypxfrd getdbm failed (reason = %d) -- using ypxfr\n",
		master, domain, map, hgstatus);

	    db = dbm_open(tmp_name, O_RDWR + O_CREAT + O_TRUNC, 0644);
	if (db == NULL){
		logprintf(
		    "Can't dbm init temp map %s.\n", tmp_name);
		del_mapfiles(tmp_name);
		*pushstat = YPPUSH_DBM;
		return (FALSE);
	}
	if (defwrite) dbm_setdefwrite(db);

	if (!get_map(tmp_name, pushstat)) {
		del_mapfiles(tmp_name);
		return (FALSE);
	}

	if (!add_private_entries(tmp_name)) {
		del_mapfiles(tmp_name);
		*pushstat = YPPUSH_DBM;
		return (FALSE);
	}

	/*
	 * Decide whether the map just transferred is a secure map.
	 * If we already know the local version was secure, we do not
	 * need to check this version.
	 */
	if (!secure_map) {
		key.dptr = yp_secure;
		key.dsize = yp_secure_sz;
		val = dbm_fetch(db, key);
		if (val.dptr != NULL) {
			secure_map = TRUE;
		}
	}

	if (dbm_close_status(db) < 0) {
		logprintf(
		    "Can't do dbm close operation on temp map %s.\n",
		    tmp_name);
		del_mapfiles(tmp_name);
		*pushstat = YPPUSH_DBM;
		return (FALSE);
	}

	if (!get_order(an, &n, pushstat)) {
		return (FALSE);
	}
	if (n != *master_version) {
		logprintf(
		    "Version skew at %s while transferring map %s.\n",
		    master, map);
		del_mapfiles(tmp_name);
		*pushstat = YPPUSH_SKEW;
		return (FALSE);
	}

	if (check_count)
	if (!count_mismatch(tmp_name, entry_count)) {
		del_mapfiles(tmp_name);
		*pushstat = YPPUSH_DBM;
		return (FALSE);
	}
	} else {
	/* touch up the map */
	    db = dbm_open(tmp_name, 2, 0644);
	if (db == NULL){
		logprintf(
		    "Can't dbm init temp map %s.\n", tmp_name);
		del_mapfiles(tmp_name);
		*pushstat = YPPUSH_DBM;
		return (FALSE);
		}


	if (!add_private_entries(tmp_name)) {
		del_mapfiles(tmp_name);
		*pushstat = YPPUSH_DBM;
		return (FALSE);
	}

	/*
	 * Decide whether the map just transferred is a secure map.
	 * If we already know the local version was secure, we do not
	 * need to check this version.
	 */
	if (!secure_map) {
		key.dptr = yp_secure;
		key.dsize = yp_secure_sz;
		val = dbm_fetch(db, key);
		if (val.dptr != NULL) {
			secure_map = TRUE;
		}
	}

	if (dbm_close_status(db) < 0) {
		logprintf(
		    "Can't do dbm close operation on temp map %s.\n",
		    tmp_name);
		del_mapfiles(tmp_name);
		*pushstat = YPPUSH_DBM;
		return (FALSE);
	}

	}

	/* this shit renames the map */
	if (!check_map_existence(map_name)) {

		if (!rename_map(tmp_name, map_name)) {
			del_mapfiles(tmp_name);
			logprintf(
			    "Rename error:  couldn't mv %s to %s.\n",
			    tmp_name, map_name);
			*pushstat = YPPUSH_FILE;
			return (FALSE);
		}

	} else {
		mk_tmpname(ypbkupname_prefix, bkup_name);

		if (!rename_map(map_name, bkup_name)) {
			(void) rename_map(bkup_name, map_name);
			logprintf(
		"Rename error:  check that old %s is still intact.\n",
		map_name);
			del_mapfiles(tmp_name);
			*pushstat = YPPUSH_FILE;
			return (FALSE);
		}

		if (rename_map(tmp_name, map_name)) {
			del_mapfiles(bkup_name);
		} else {
			del_mapfiles(tmp_name);
			(void) rename_map(bkup_name, map_name);
				logprintf(
		"Rename error:  check that old %s is still intact.\n",
			    map_name);
			*pushstat = YPPUSH_FILE;
			return (FALSE);
		}
	}

	return (TRUE);
}

/*
 * This tries to get the order number out of the local version of the map.
 * If the attempt fails for any version, the function will return "0"
 */
unsigned
get_local_version(name)
	char *name;
{
	datum key;
	datum val;
	char number[11];
	DBM *db;

	if (!check_map_existence(name)) {
		return (0);
	}
	if (debug) fprintf(stderr,
		"ypxfr: Map does exist, checking version now.\n");

	if ((db = dbm_open(name, 0, 0)) == 0) {
		return (0);
	}

	key.dptr = yp_last_modified;
	key.dsize = yp_last_modified_sz;
	val = dbm_fetch(db, key);
	if (!val.dptr) {	/* Check to see if dptr is NULL */
		return (0);
	}
	if (val.dsize == 0 || val.dsize > 10) {
		return (0);
	}
	/* Now save this value while we have it available */
	(void) memmove(number, val.dptr, val.dsize);
	number[val.dsize] = '\0';

	/*
	 * Now check to see if it is 'secure'. If we haven't already
	 * determined that it is secure in get_private_recs() then we check
	 * the local map here.
	 */
	if (!secure_map) {
		key.dptr = yp_secure;
		key.dsize = yp_secure_sz;
		val = dbm_fetch(db, key);
		secure_map = (val.dptr != NULL);
	}

	/*
	 * Now check to see if interdomain requests are made of the local
	 * map. Keep the value around if they are.
	 */
	if (!interdomain_map) {
		key.dptr = yp_interdomain;
		key.dsize = yp_interdomain_sz;
		val = dbm_fetch(db, key);
		if (interdomain_map = (val.dptr != NULL)) {
			interdomain_value = (char *)malloc(val.dsize+1);
			(void) memmove(interdomain_value, val.dptr, val.dsize);
			*(interdomain_value+val.dsize) = '\0';
			interdomain_sz = val.dsize;
		}
	}

	/* finish up */
	(void) dbm_close_status(db);

	return ((unsigned) atoi(number));
}

/*
 * This constructs a file name for a map, minus its dbm_dir
 * or dbm_pag extensions
 */
void
mkfilename(ppath)
	char *ppath;
{

	if ((strlen(domain) + strlen(map) + strlen(ypdbpath) + 3)
	    > (MAXNAMLEN + 1)) {
		logprintf("Map name string too long.\n");
	}

	(void) strcpy(ppath, ypdbpath);
	(void) strcat(ppath, "/");
	(void) strcat(ppath, domain);
	(void) strcat(ppath, "/");
	(void) strcat(ppath, map);
}

/*
 * This returns a temporary name for a map transfer minus its dbm_dir or
 * dbm_pag extensions.
 */
void
mk_tmpname(prefix, xfr_name)
	char *prefix;
	char *xfr_name;
{
	char xfr_anumber[10];
	long xfr_number;

	if (!xfr_name) {
		return;
	}

	xfr_number = getpid();
	(void) sprintf(xfr_anumber, "%d", xfr_number);

	(void) strcpy(xfr_name, ypdbpath);
	(void) strcat(xfr_name, "/");
	(void) strcat(xfr_name, domain);
	(void) strcat(xfr_name, "/");
	(void) strcat(xfr_name, prefix);
	(void) strcat(xfr_name, map);
	(void) strcat(xfr_name, ".");
	(void) strcat(xfr_name, xfr_anumber);
}

/*
 * This deletes the .pag and .dir files which implement a map.
 *
 * Note:  No error checking is done here for a garbage input file name or for
 * failed unlink operations.
 */
void
del_mapfiles(basename)
	char *basename;
{
	char dbfilename[MAXNAMLEN + 1];

	if (!basename) {
		return;
	}

	strcpy(dbfilename, basename);
	strcat(dbfilename, dbm_pag);
	unlink(dbfilename);
	strcpy(dbfilename, basename);
	strcat(dbfilename, dbm_dir);
	unlink(dbfilename);
}

/*
 * This checks to see if the source map files exist, then renames them to the
 * target names.  This is a boolean function.  The file names from.pag and
 * from.dir will be changed to to.pag and to.dir in the success case.
 *
 * Note:  If the second of the two renames fails, yprename_map will try to
 * un-rename the first pair, and leave the world in the state it was on entry.
 * This might fail, too, though...
 */
bool
rename_map(from, to)
	char *from;
	char *to;
{
	char fromfile[MAXNAMLEN + 1];
	char tofile[MAXNAMLEN + 1];
	char savefile[MAXNAMLEN + 1];

	if (!from || !to) {
		return (FALSE);
	}

	if (!check_map_existence(from)) {
		return (FALSE);
	}

	(void) strcpy(fromfile, from);
	(void) strcat(fromfile, dbm_pag);
	(void) strcpy(tofile, to);
	(void) strcat(tofile, dbm_pag);

	if (rename(fromfile, tofile)) {
		logprintf("Can't mv %s to %s.\n", fromfile,
		    tofile);
		return (FALSE);
	}

	(void) strcpy(savefile, tofile);
	(void) strcpy(fromfile, from);
	(void) strcat(fromfile, dbm_dir);
	(void) strcpy(tofile, to);
	(void) strcat(tofile, dbm_dir);

	if (rename(fromfile, tofile)) {
		logprintf("Can't mv %s to %s.\n", fromfile,
		    tofile);
		(void) strcpy(fromfile, from);
		(void) strcat(fromfile, dbm_pag);
		(void) strcpy(tofile, to);
		(void) strcat(tofile, dbm_pag);

		if (rename(tofile, fromfile)) {
			logprintf(
			    "Can't recover from rename failure.\n");
			return (FALSE);
		}

		return (FALSE);
	}

	if (!secure_map) {
		chmod(savefile, 0644);
		chmod(tofile, 0644);
	}

	return (TRUE);
}

/*
 * This performs an existence check on the dbm data base files <pname>.pag and
 * <pname>.dir.
 */
bool
check_map_existence(pname)
	char *pname;
{
	char dbfile[MAXNAMLEN + 1];
	struct stat filestat;
	int len;

	if (debug) fprintf(stderr,
		"ypxfr: Checking the existence of '%s'\n", pname);
	if (!pname || ((len = strlen(pname)) == 0) ||
	    (len + 5) > (MAXNAMLEN + 1)) {
		return (FALSE);
	}

	errno = 0;
	(void) strcpy(dbfile, pname);
	(void) strcat(dbfile, dbm_dir);

	if (debug) fprintf(stderr,
		"ypxfr: First file is '%s'\n", dbfile);
	if (stat(dbfile, &filestat) != -1) {
		(void) strcpy(dbfile, pname);
		(void) strcat(dbfile, dbm_pag);

		if (stat(dbfile, &filestat) != -1) {
			return (TRUE);
		} else {

			if (errno != ENOENT) {
				logprintf(
				    "Stat error on map file %s.\n",
				    dbfile);
			}

			return (FALSE);
		}

	} else {

		if (errno != ENOENT) {
			logprintf(
			    "Stat error on map file %s.\n",
			    dbfile);
		}

		return (FALSE);
	}
}

/*
 * This creates <pname>.dir and <pname>.pag
 */
bool
new_mapfiles(pname)
	char *pname;
{
	char dbfile[MAXNAMLEN + 1];
	int f;
	int len;

	if (!pname || ((len = strlen(pname)) == 0) ||
	    (len + 5) > (MAXNAMLEN + 1)) {
		return (FALSE);
	}

	errno = 0;
	(void) strcpy(dbfile, pname);
	(void) strcat(dbfile, dbm_dir);

	if ((f = open(dbfile, (O_WRONLY | O_CREAT | O_TRUNC), 0600)) >= 0) {
		(void) close(f);
		(void) strcpy(dbfile, pname);
		(void) strcat(dbfile, dbm_pag);

		if ((f = open(dbfile, (O_WRONLY | O_CREAT | O_TRUNC),
		    0600)) >= 0) {
			(void) close(f);
			return (TRUE);
		} else {
			return (FALSE);
		}

	} else {
		return (FALSE);
	}
}

count_callback(status)
	int status;
{
	if (status != YP_TRUE) {

		if (status != YP_NOMORE) {
			logprintf(
			    "Error from ypserv on %s (ypall_callback) = %s.\n",
			    master, yperr_string(ypprot_err(status)));
		}

		return (TRUE);
	}

	entry_count++;
	return (FALSE);
}

/*
 * This counts the entries in the dbm file after the transfer to
 * make sure that the dbm file was built correctly.
 * Returns TRUE if everything is OK, FALSE if they mismatch.
 */
count_mismatch(pname, oldcount)
	char *pname;
	int oldcount;
{
	datum key;
	DBM *db;
# ifdef REALLY_PARANOID
	struct ypall_callback cbinfo;
	struct ypreq_nokey allreq;
	enum clnt_stat s;
	struct dom_binding domb;
	datum value;
# endif REALLY_PARANOID

	entry_count = 0;
	db = dbm_open(pname, 0, 0);
	if (db){
	    for (key = dbm_firstkey(db);
				key.dptr != NULL; key = dbm_nextkey(db))
		entry_count++;
	    dbm_close_status(db);
	}

	if (oldcount != entry_count) {
	logprintf(
		    "*** Count mismatch in dbm file %s: old=%d, new=%d ***\n",
		    map, oldcount, entry_count);
	return (FALSE);
	}

# ifdef REALLY_PARANOID

	if (!domb.dom_client = __yp_clnt_create_rsvdport(master, YPPROG,
						    master_prog_vers,
						    "tcp", 0, 0)) {
		clnt_pcreateerror("ypxfr (mismatch) - TCP channel "
				  "create failure");
		return (FALSE);
	}

	if (master_prog_vers == YPVERS) {
		int tmpstat;

		allreq.domain = source;
		allreq.map = map;
		cbinfo.foreach = count_callback;
		tmpstat = 0;
		cbinfo.data = (char *) &tmpstat;

		entry_count = 0;
		s = clnt_call(domb.dom_client, YPPROC_ALL, xdr_ypreq_nokey,
		    &allreq, xdr_ypall, &cbinfo, tcp_timeout);

		if (tmpstat == 0) {
			if (s == RPC_SUCCESS) {
			} else {
				clnt_perror(domb.dom_client,
		"ypxfr (get_map/all) - RPC clnt_call (TCP) failure");
		    return (FALSE);
			}

		} else {
		    return (FALSE);
		}

	} else {
	    logprintf("Wrong version number!\n");
	    return (FALSE);
	}
	clnt_destroy(domb.dom_client);
	close(domb.dom_socket);
	entry_count += 2;			/* add in YP_entries */
	if (oldcount != entry_count) {
		logprintf(
	"*** Count mismatch after enumerate %s: old=%d, new=%d ***\n",
		    map, oldcount, entry_count);
		return (FALSE);
	}
# endif REALLY_PARANOID

	return (TRUE);
}

/*
 * This sets up a TCP connection to the master server, and either gets
 * ypall_callback to do all the work of writing it to the local dbm file
 * (if the ypserv is current version), or does it itself for an old ypserv.
 */
bool
get_map(pname, pushstat)
	char *pname;
	int *pushstat;
{
	struct dom_binding domb;
	enum clnt_stat s;
	struct ypreq_nokey allreq;
	struct ypall_callback cbinfo;
	bool retval = FALSE;
	int tmpstat;
	int	recvsiz = 24 * 1024;
	struct netconfig *nconf;
	int fd = RPC_ANYFD;
	struct netbuf *svcaddr;

	svcaddr = (struct netbuf *)calloc(1, sizeof (struct netbuf));
	if (! svcaddr)
		return (FALSE);
	svcaddr->maxlen = 32;
	svcaddr->len = 32;
	svcaddr->buf = (char *) malloc(32);

	if ((nconf = getnetconfigent("tcp")) == NULL) {
		logprintf("ypxfr: tcp transport not supported\n");
		return (FALSE);
	}
	if (rpcb_getaddr(YPPROG, master_prog_vers, nconf, svcaddr, master)
		== FALSE) {
		logprintf("ypxfr: couldnot get %s address\n", master);
		return (FALSE);
	}
	if ((domb.dom_client = clnt_tli_create(fd, nconf, svcaddr,
		YPPROG, master_prog_vers, 0, recvsiz)) == (CLIENT *) NULL) {
		clnt_pcreateerror(
			"ypxfr (get_map) - TCP channel create failure");
		*pushstat = YPPUSH_RPC;
		return (FALSE);
	}

	entry_count = 0;
	if (master_prog_vers == YPVERS) {
		allreq.domain = source;
		allreq.map = map;
		cbinfo.foreach = ypall_callback;
		tmpstat = 0;
		cbinfo.data = (char *) &tmpstat;

		s = clnt_call(domb.dom_client, YPPROC_ALL,
			(xdrproc_t)xdr_ypreq_nokey,
		    (char *)&allreq, (xdrproc_t)xdr_ypall, (char *)&cbinfo,
		    tcp_timeout);

		if (tmpstat == 0) {

			if (s == RPC_SUCCESS) {
				retval = TRUE;
			} else {
				clnt_perror(domb.dom_client,
			"ypxfr (get_map/all) - RPC clnt_call (TCP) failure");
				*pushstat = YPPUSH_RPC;
			}

		} else {
			*pushstat = tmpstat;
		}

	} else
		retval = FALSE; /* barf again at YPOLDVERS */
cleanup:
	clnt_destroy(domb.dom_client);
	return (retval);
}

/*
 * This sticks each key-value pair into the current map.  It returns FALSE as
 * long as it wants to keep getting called back, and TRUE on error conditions
 * and "No more k-v pairs".
 */
int
ypall_callback(status, key, kl, val, vl, pushstat)
	int status;
	char *key;
	int kl;
	char *val;
	int vl;
	int *pushstat;
{
	datum keydat;
	datum valdat;
	datum test;

	if (status != YP_TRUE) {

		if (status != YP_NOMORE) {
			logprintf(
			    "Error from ypserv on %s (ypall_callback) = %s.\n",
			    master, yperr_string(ypprot_err(status)));
			*pushstat = map_yperr_to_pusherr(status);
		}

		return (TRUE);
	}

	keydat.dptr = key;
	keydat.dsize = kl;
	valdat.dptr = val;
	valdat.dsize = vl;
	entry_count++;
/* way too many fetches */

# ifdef PARANOID
	test = dbm_fetch(db, keydat);
	if (test.dptr != NULL) {
		logprintf("Duplicate key %s in map %s\n", key, map);
		*pushstat  = YPPUSH_DBM;
		return (TRUE);
	}
# endif PARANOID
	if (dbm_store(db, keydat, valdat, 0) < 0) {
		logprintf(
		    "Can't do dbm store into temp map %s.\n", map);
		*pushstat  = YPPUSH_DBM;
		return (TRUE);
	}
# ifdef PARANOID
	test = dbm_fetch(db, keydat);
	if (test.dptr == NULL) {
		logprintf("Key %s was not inserted into dbm file %s\n",
			key, map);
		*pushstat  = YPPUSH_DBM;
		return (TRUE);
	}
# endif PARANOID

	if (dbm_error(db)) {
		logprintf("Key %s dbm_error raised in file %s\n",
			key, map);
		*pushstat  = YPPUSH_DBM;
		return (TRUE);
	}
	return (FALSE);
}

/*
 * This maps a YP_xxxx error code into a YPPUSH_xxxx error code
 */
int
map_yperr_to_pusherr(yperr)
	int yperr;
{
	int reason;

	switch (yperr) {

	case YP_NOMORE:
		reason = YPPUSH_SUCC;
		break;

	case YP_NOMAP:
		reason = YPPUSH_NOMAP;
		break;

	case YP_NODOM:
		reason = YPPUSH_NODOM;
		break;

	case YP_NOKEY:
		reason = YPPUSH_YPERR;
		break;

	case YP_BADARGS:
		reason = YPPUSH_BADARGS;
		break;

	case YP_BADDB:
		reason = YPPUSH_YPERR;
		break;

	default:
		reason = YPPUSH_XFRERR;
		break;
	}

	return (reason);
}

/*
 * This writes the last-modified and master entries into the new dbm file
 */
bool
add_private_entries(pname)
	char *pname;
{
	datum key;
	datum val;

	if (!fake_master_version) {
		key.dptr = yp_last_modified;
		key.dsize = yp_last_modified_sz;
		val.dptr = master_ascii_version;
		val.dsize = strlen(master_ascii_version);

		if (dbm_store(db, key, val, 1) < 0) {
			logprintf(
			    "Can't do dbm store into temp map %s.\n",
			    pname);
			return (FALSE);
		}
		entry_count++;
	}

	if (master_name) {
		key.dptr = yp_master_name;
		key.dsize = yp_master_name_sz;
		val.dptr = master_name;
		val.dsize = strlen(master_name);
		if (dbm_store(db, key, val, 1) < 0) {
			logprintf(
			    "Can't do dbm store into temp map %s.\n",
			    pname);
			return (FALSE);
		}
		entry_count++;
	}

	if (interdomain_map) {
		key.dptr = yp_interdomain;
		key.dsize = yp_interdomain_sz;
		val.dptr = interdomain_value;
		val.dsize = interdomain_sz;
		if (dbm_store(db, key, val, 1) < 0) {
			logprintf(
			    "Can't do dbm store into temp map %s.\n",
			    pname);
			return (FALSE);
		}
		entry_count++;
	}

	if (secure_map) {
		key.dptr = yp_secure;
		key.dsize = yp_secure_sz;
		val.dptr = yp_secure;
		val.dsize = yp_secure_sz;
		if (dbm_store(db, key, val, 1) < 0) {
			logprintf(
			    "Can't do dbm store into temp map %s.\n",
			    pname);
			return (FALSE);
		}
		entry_count++;
	}

	return (TRUE);
}


/*
 * This sends a YPPROC_CLEAR message to the local ypserv process.
 */
bool
send_ypclear(pushstat)
	int *pushstat;
{
	struct dom_binding domb;
	char local_host_name[256];
	unsigned int progvers;
	int status;

	if (gethostname(local_host_name, 256)) {
		logprintf("Can't get local machine name.\n");
		*pushstat = YPPUSH_RSRC;
		return (FALSE);
	}

	if (!bind_to_server(local_host_name, &domb,
	    &progvers, &status)) {
		*pushstat = YPPUSH_CLEAR;
		return (FALSE);
	}

	if ((enum clnt_stat) clnt_call(domb.dom_client,
	    YPPROC_CLEAR, xdr_void, 0, xdr_void, 0,
	    udp_timeout) != RPC_SUCCESS) {
		logprintf(
		"Can't send ypclear message to ypserv on the local machine.\n");
		xfr_exit(YPPUSH_CLEAR);
	}

	return (TRUE);
}

/*
 * This decides if send_callback has to get called, and does the process exit.
 */
void
xfr_exit(status)
	int status;
{
	if (callback) {
		send_callback(&status);
	}

	if (status == YPPUSH_SUCC) {
#ifdef TREEPUSH
		if (treepush){
		if (debug)
		execlp("./yppush", "yppush", "-T", map, 0);
		execlp("/usr/etc/yp/yppush", "yppush", "-T", map, 0);
		perror("yppush");
		}
#endif
		exit(0);
	} else {
		exit(1);
	}
}

/*
 * This sets up a UDP connection to the yppush process which contacted our
 * parent ypserv, and sends him a status on the requested transfer.
 */
void
send_callback(status)
	int *status;
{
	struct yppushresp_xfr resp;
	struct dom_binding domb;

	resp.transid = (unsigned long) atoi(tid);
	resp.status = (unsigned long) *status;

	udp_timeout.tv_sec = CALLTIMEOUT;

	if ((domb.dom_client = __yp_clnt_create_rsvdport(pushhost,
						    (ulong_t) atoi(proto),
						    YPPUSHVERS,
						    "udp", 0, 0)) == NULL) {
		*status = YPPUSH_RPC;
		return;
	}

	if ((enum clnt_stat) clnt_call(domb.dom_client,
	    YPPUSHPROC_XFRRESP, (xdrproc_t)xdr_yppushresp_xfr,
	    (char *) &resp, xdr_void, 0,
	    udp_timeout) != RPC_SUCCESS) {
		*status = YPPUSH_RPC;
		return;
	}
}
