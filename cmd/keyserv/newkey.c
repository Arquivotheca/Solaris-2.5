/*
 *	newkey.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)newkey.c	1.19	94/10/25 SMI"

/*
 * Administrative tool to add a new user to the publickey database
 */
#include <stdio.h>
#include <stdlib.h>
#include <rpc/rpc.h>
#include <rpc/key_prot.h>
#include <rpcsvc/ypclnt.h>
#include <sys/wait.h>
#include <netdb.h>
#include <pwd.h>
#include <shadow.h>
#include <crypt.h>
#include <string.h>
#include <sys/resource.h>
#include <netdir.h>
#include <rpcsvc/nis.h>

#define	MAXMAPNAMELEN 256

#define	PK_FILES	1
#define	PK_YP		2
#define	PK_NISPLUS	3

extern	int optind;
extern	char *optarg;
extern	char *get_nisplus_principal();
extern	int __getnetnamebyuid();

char	program_name[256];
static	char	*get_password();
static	char *basename();
static	char SHELL[] = "/bin/sh";
static	char YPDBPATH[] = "/var/yp";
static	char PKMAP[] = "publickey.byname";
static	char UPDATEFILE[] = "updaters";
static	char PKFILE[] = "/etc/publickey";

main(argc, argv)
	int argc;
	char *argv[];
{
	char	name[MAXNETNAMELEN + 1];
	char	public[HEXKEYBYTES + 1];
	char	secret[HEXKEYBYTES + 1];
	char	crypt1[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];
	int	status, pk_database;
	char	*pass, *target_host = NULL,
		*username = NULL, *pk_service = NULL;
	struct passwd	*pw;
	NCONF_HANDLE	*nc_handle;
	struct	netconfig *nconf;
	struct	nd_hostserv service;
	struct	nd_addrlist *addrs;
	bool_t	validhost;
	uid_t	uid;
	int	c;
	char	*nprinc = NULL;  /* nisplus principal name */
	char	host_pname[NIS_MAXNAMELEN];

	strcpy(program_name, argv[0]);
	while ((c = getopt(argc, argv, "s:u:h:")) != -1) {
		switch (c) {
		case 's':
			if (pk_service == NULL)
				pk_service = optarg;
			else
				usage();
			break;
		case 'u':
			if (username || target_host)
				usage();
			username = optarg;
			break;
		case 'h':
			if (username || target_host)
				usage();
			target_host = optarg;
			break;
		default:
			usage();
		}
	}

	if (optind < argc || (username == 0 && target_host == 0)) {
		usage();
	}

	if ((pk_database = get_pk_source(pk_service)) == 0)
		usage();

	if (geteuid() != 0) {
		(void) fprintf(stderr, "Must be superuser to run %s\n",
				program_name);
		exit(1);
	}

	if (username) {
		pw = getpwnam(username);
		if (pw == NULL) {
			(void) fprintf(stderr, "%s: unknown user: '%s'\n",
				program_name, username);
			exit(1);
		}
		uid = pw->pw_uid;
		if (uid == 0) {
			if (! getnetname(name)) {
				(void) fprintf(stderr,
			"%s: could not get the equivalent netname for %s\n",
				program_name, username);
				usage();
			}
			if (pk_database == PK_NISPLUS)
				target_host = nis_local_host();
			else {
				if (gethostname(host_pname, NIS_MAXNAMELEN)
					< 0) {
					(void) fprintf(stderr,
				"%s: could not get the hostname for %s\n",
					program_name, username);
					usage();
				}
				target_host = host_pname;
			}
		}
		if (__getnetnamebyuid(name, uid) == 0) {
			(void) fprintf(stderr,
			"%s: could not get the equivalent netname for %s\n",
				program_name, username);
			usage();
		}
		if (pk_database == PK_NISPLUS)
			nprinc = get_nisplus_principal(nis_local_directory(),
					uid);
	} else {
		/* -h hostname option */
		service.h_host = target_host;
		service.h_serv = NULL;
		validhost = FALSE;
		/* verify if this is a valid hostname */
		nc_handle = setnetconfig();
		if (nc_handle == NULL) {
			/* fails to open netconfig file */
			(void) fprintf(stderr,
				"%s: failed in routine setnetconfig()\n",
				program_name);
			exit(2);
		}
		while (nconf = getnetconfig(nc_handle)) {
			/* check to see if hostname exists for this transport */
			if ((netdir_getbyname(nconf, &service, &addrs) == 0) &&
			    (addrs->n_cnt != 0)) {
				/* at least one valid address */
				validhost = TRUE;
				break;
			}
		}
		endnetconfig(nc_handle);
		if (!validhost) {
			(void) fprintf(stderr, "%s: unknown host: %s\n",
				program_name, target_host);
			exit(1);
		}
		(void) host2netname(name, target_host, (char *)NULL);
		if (pk_database == PK_NISPLUS) {
			if (target_host[strlen(target_host) - 1] != '.') {
				sprintf(host_pname, "%s.%s",
					target_host, nis_local_directory());
				nprinc = host_pname;
			} else
				nprinc = target_host;
		}
		uid = 0;
	}

	(void) fprintf(stdout, "Adding new key for %s.\n", name);
	pass = get_password(uid, target_host);

	if (pass == NULL)
		exit(1);

	(void) __gen_dhkeys(public, secret, pass);

	memcpy(crypt1, secret, HEXKEYBYTES);
	memcpy(crypt1 + HEXKEYBYTES, secret, KEYCHECKSUMSIZE);
	crypt1[HEXKEYBYTES + KEYCHECKSUMSIZE] = 0;
	xencrypt(crypt1, pass);

	if (status = setpublicmap(name, public, crypt1, pk_database, nprinc)) {
		switch (pk_database) {
		case PK_YP:
			(void) fprintf(stderr,
				"%s: unable to update NIS database (%u): %s\n",
				program_name, status,
				yperr_string(status));
			break;
		case PK_FILES:
			(void) fprintf(stderr,
			"%s: hence, unable to update publickey database\n",
				program_name);
			break;
		case PK_NISPLUS:
			(void) fprintf(stderr,
				"%s: unable to update nisplus database\n",
				program_name);
			break;
		default:
			(void) fprintf(stderr,
				"%s: could not update unknown database: %d\n",
				program_name, pk_database);
		}
		exit(1);
	}
	exit(0);
	/* NOTREACHED */
}

/*
 * Set the entry in the public key file
 */
setpublicmap(name, public, secret, database, nis_princ)
	int database;
	char *name;
	char *public;
	char *secret;
	nis_name nis_princ;
{
	char pkent[HEXKEYBYTES + HEXKEYBYTES + KEYCHECKSUMSIZE + 2];
	char *domain = NULL;
	char *master = NULL;
	char hostname[MAXHOSTNAMELEN+1];

	(void) sprintf(pkent, "%s:%s", public, secret);
	switch (database) {
	case PK_YP:
		/* check that we're on the master server */
		(void) yp_get_default_domain(&domain);
		if (yp_master(domain, PKMAP, &master) != 0) {
			(void) fprintf(stderr,
			"%s: cannot find master of NIS publickey database\n",
				program_name);
			exit(1);
		}
		if (gethostname(hostname, MAXHOSTNAMELEN) < 0) {
			(void) fprintf(stderr,
				"%s: cannot find my own host name\n",
				program_name);
			exit(1);
		}
		if (strcmp(master, hostname) != 0) {
			(void) fprintf(stderr,
			"%s: can only be used on NIS master machine '%s'\n",
				program_name, master);
			exit(1);
		}

		if (chdir(YPDBPATH) < 0) {
			(void) fprintf(stderr, "%s: cannot chdir to %s",
			program_name, YPDBPATH);
		}
		(void) fprintf(stdout,
			"Please wait for the database to get updated ...\n");
		return (mapupdate(name, PKMAP, YPOP_STORE, pkent));
	case PK_FILES:
		return (localupdate(name, PKFILE, YPOP_STORE, pkent));
	case PK_NISPLUS:
		return (nisplus_update(name, public, secret, nis_princ));
	default:
		break;
	}
	return (1);
}

usage()
{
	(void) fprintf(stderr,
		"usage:\t%s -u username [-s nisplus | nis | files]\n",
		program_name);
	(void) fprintf(stderr,
		"\t%s -h hostname [-s nisplus | nis | files]\n",
		program_name);
	exit(1);
}

static char *
get_password(uid, target_host)
uid_t	uid;
char	*target_host;
{
	static	char	password[256];
	char		prompt[256];
	char		*encrypted_password,
			*login_password = NULL,
			*pass = NULL;
	struct	passwd	*pw;
	struct	spwd	*spw;
	int passwords_matched = 0;

	pw = getpwuid(uid);
	if (! pw) {
		(void) fprintf(stderr,
		"%s: unable to locate password record for uid %d\n",
			program_name, uid);
		return (0);
	}
	spw = getspnam(pw->pw_name);
	if (spw)
		login_password = spw->sp_pwdp;

	if (! login_password || (strlen(login_password) == 0)) {
		(void) fprintf(stderr,
		"%s: unable to locate shadow password record for %s\n",
			program_name, pw->pw_name);
		return (0);
	}

	if (uid == 0) {
		(void) sprintf(prompt, "Enter %s's root login password:",
			target_host);
	} else
		(void) sprintf(prompt, "Enter %s's login password:",
			pw->pw_name);

	pass = getpass(prompt);
	if (pass && strlen(pass) == 0) {
		(void) fprintf(stderr, "%s: Password unchanged.\n",
			program_name);
		return (0);
	}
	strcpy(password, pass);

	/* Verify that password supplied matches login password */
	encrypted_password = crypt(password, login_password);
	if (strcmp(encrypted_password, login_password) == 0)
		passwords_matched = 1;
	else {
		(void) fprintf(stderr,
			"%s: ERROR, password differs from login password.\n",
			program_name);
		return (0);
	}

	/* Check for mis-typed password */
	if (! passwords_matched) {
		pass = getpass("Please retype password:");
		if (pass && (strcmp(password, pass) != 0)) {
			(void) fprintf(stderr,
				"%s: password incorrect.\n", program_name);
			return (0);
		}
	}
	return (password);
}
