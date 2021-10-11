/*
 *	chkey.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)chkey.c	1.18	94/10/25 SMI"

/*
 * Command to change one's public key in the public key database
 */
#include <stdio.h>
#include <stdlib.h>
#include <rpc/rpc.h>
#include <rpc/key_prot.h>
#include <rpcsvc/ypclnt.h>
#include <pwd.h>
#include <shadow.h>
#include <crypt.h>
#include <string.h>
#include <rpcsvc/nis.h>

#define	PK_FILES	1
#define	PK_YP		2
#define	PK_NISPLUS	3

char	program_name[256];
extern	int optind;
extern	char *optarg;
extern	char *get_nisplus_principal();
extern	int	__getnetnamebyuid();
static	int getpasswd();

static char PKMAP[] = "publickey.byname";
static char PKFILE[] = "/etc/publickey";
#define	MAXHOSTNAMELEN	256

int	reencrypt_only = 0;

main(argc, argv)
	int argc;
	char **argv;
{
	char	netname[MAXNETNAMELEN + 1];
	char	public[HEXKEYBYTES + 1];
	char	secret[HEXKEYBYTES + 1];
	char	crypt1[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];
	char	newpass[9]; /* as per getpass() man page */
	int	status;
	uid_t	uid;
	int	force = 0;
	int	pk_database = 0;
	char	*pk_service = NULL;
	int	c;

	strcpy(program_name, argv[0]);
	while ((c = getopt(argc, argv, "fps:")) != -1) {
		switch (c) {
		case 'f':
			/*
			 * temporarily supported,
			 * not documented as of s1093.
			 */
			force = 1;
			break;
		case 'p':
			reencrypt_only = 1;
			break;
		case 's':
			if (pk_service == NULL)
				pk_service = optarg;
			else
				usage();
			break;
		default:
			usage();
		}
	}

	if (optind < argc)
		usage();

	if ((pk_database = get_pk_source(pk_service)) == 0)
		usage();

	/*
	 * note: we're using getuid() and not geteuid() as
	 * chkey is a root set uid program.
	 */
	if (__getnetnamebyuid(netname, uid = getuid()) == 0) {
		(void) fprintf(stderr,
			"%s: cannot generate netname for uid %d\n",
				program_name, uid);
		exit(1);
	}

	if (reencrypt_only)
		(void) fprintf(stdout,
			"Reencrypting key for '%s'.\n", netname);
	else
		(void) fprintf(stdout,
			"Generating new key for '%s'.\n", netname);

	if (! getpasswd(uid, netname, force, newpass, secret))
		exit(1);
	/* at this point geteuid() == uid */

	if (reencrypt_only) {
		if (! getpublickey(netname, public)) {
			(void) fprintf(stderr,
				"%s: cannot get public key for %s.\n",
				program_name, netname);
			exit(1);
		}
	} else
		(void) __gen_dhkeys(public, secret, newpass);

	memcpy(crypt1, secret, HEXKEYBYTES);
	memcpy(crypt1 + HEXKEYBYTES, secret, KEYCHECKSUMSIZE);
	crypt1[HEXKEYBYTES + KEYCHECKSUMSIZE] = 0;
	xencrypt(crypt1, newpass);

	status = setpublicmap(netname, public, crypt1, pk_database);

	if (status) {
		switch (pk_database) {
		case PK_YP:
			(void) fprintf(stderr,
				"%s: unable to update NIS database (%u): %s\n",
				program_name, status, yperr_string(status));
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
				"could not update; database %d unknown\n",
				pk_database);
		}
		exit(1);
	}

	if (uid == 0 && ! reencrypt_only) {
		/*
		 * Root users store their key in /etc/$ROOTKEY so
		 * that they can auto reboot without having to be
		 * around to type a password.
		 */
		write_rootkey(secret);
	}
	if (! reencrypt_only)
		keylogin(netname, secret);
	exit(0);
}

usage()
{
	(void) fprintf(stderr,
		"usage: %s [-p] [-s nisplus | nis | files] \n",
		program_name);
	exit(1);
}


/*
 * Set the entry in the public key file
 */
setpublicmap(netname, public, secret, database)
	char *netname;
	char *public;
	char *secret;
	int database;
{
	char pkent[1024];
	char *master;
	char *domain = NULL;
	nis_name nis_princ;

	(void) sprintf(pkent, "%s:%s", public, secret);
	switch (database) {
	case PK_YP:
		(void) yp_get_default_domain(&domain);
		if (yp_master(domain, PKMAP, &master) != 0) {
			(void) fprintf(stderr,
			"%s: cannot find master of NIS publickey database\n",
				program_name);
			exit(1);
		}
		(void) fprintf(stdout,
			"Sending key change request to %s ...\n", master);
		return (yp_update(domain, PKMAP, YPOP_STORE,
				netname, strlen(netname), pkent,
				strlen(pkent)));
	case PK_FILES:
		if (geteuid() != 0) {
			(void) fprintf(stderr,
		"%s: non-root users cannot change their key-pair in %s\n",
				program_name, PKFILE);
			exit(1);
		}
		return (localupdate(netname, PKFILE, YPOP_STORE, pkent));
	case PK_NISPLUS:
		nis_princ = get_nisplus_principal(nis_local_directory(),
				geteuid());
		return (nisplus_update(netname, public, secret, nis_princ));
	default:
		break;
	}
	return (1);
}

/*
 * populate 'newpass' and 'secret' and pass back
 * force will be only supported for a while
 * 	-- it is NOT documented as of s1093
 */
static int
getpasswd(uid, netname, force, newpass, secret)
uid_t	uid;
char *netname;
char	*newpass;
char	*secret;
{
	char	*rpcpass = NULL, *x_rpcpass = NULL;
	char	*l_pass = NULL, *x_lpass = NULL;
	struct	passwd	*pw;
	struct	spwd	*spw;
	char	*login_passwd = NULL;
	char	prompt[256];


	/*
	 * get the shadow passwd from the repository
	 */
	if ((pw = getpwuid(uid)) == 0) {
		(void) fprintf(stderr,
			"%s: unable to locate passwd entry for uid %d\n",
			program_name, uid);
		return (0);
	}
	if ((spw = getspnam(pw->pw_name)) == 0) {
		(void) fprintf(stderr,
			"%s: unable to locate shadow passwd for uid %d\n",
			program_name, uid);
		return (0);
	}
	login_passwd = spw->sp_pwdp;

	/*
	 * now set effective uid to users uid
	 */
	seteuid(uid);

	/*
	 * get the old encryption passwd - this may or may not be
	 * their old login passwd
	 */
	(void) sprintf(prompt,
		"Please enter the Secure-RPC password for %s:",
		pw->pw_name);
	rpcpass = getpass(prompt);
	if (rpcpass == NULL) {
		(void) fprintf(stderr,
			"%s: key-pair unchanged for %s.\n",
			program_name, pw->pw_name);
		return (0);
	}

	/*
	 * get the secret key from the key respository
	 */
	if (! getsecretkey(netname, secret, rpcpass)) {
		(void) fprintf(stderr,
			"%s: could not get secret key for '%s'\n",
			program_name, netname);
		return (0);
	}
	/*
	 * check if it is zero -> if it is then ask for the passwd again.
	 * then attempt to get the secret again
	 */
	if (secret[0] == 0) {
		(void) sprintf(prompt,
			"Try again. Enter the Secure-RPC password for %s:",
			pw->pw_name);
		rpcpass = getpass(prompt);
		if (rpcpass == NULL) {
			(void) fprintf(stderr,
				"%s: key-pair unchanged for %s.\n",
				program_name, pw->pw_name);
			return (0);
		}
		if (! getsecretkey(netname, secret, rpcpass)) {
			(void) fprintf(stderr,
				"%s: could not get secret key for '%s'\n",
				program_name, netname);
			return (0);
		}
		if (secret[0] == 0) {
			(void) fprintf(stderr,
				"%s: Unable to decrypt secret key for %s.\n",
				program_name, netname);
			return (0);
		}
	}
	/*
	 * check if 'user' has a key cached with keyserv.
	 * if there is no key cached, then keylogin the user.
	 * if uid == 0, might aswell write it to /etc/$ROOTKEY,
	 * assuming that if there is a /etc/$ROOTKEY then the
	 * roots' secret key should already be cached.
	 */
	if (! key_secretkey_is_set()) {
		keylogin(netname, secret);
		if ((uid == 0) && (reencrypt_only))
			write_rootkey(secret);
	}

	if (force) {
		/* simply get the new passwd - no checks */
		(void) sprintf(prompt, "Please enter New password:");
		l_pass = getpass(prompt);
		if (l_pass && (strlen(l_pass) != 0)) {
			strcpy(newpass, l_pass);
			return (1);
		}
		(void) fprintf(stderr,
			"%s: key-pair unchanged for %s.\n",
			program_name, pw->pw_name);
		return (0);
	}
	/*
	 * check if the secure-rpc passwd given above matches the
	 * the unix login passwd
	 */
	if (login_passwd && (strlen(login_passwd) != 0)) {
		/* NOTE: 1st 2 chars of an encrypted passwd = salt */
		x_rpcpass = crypt(rpcpass, login_passwd);
		if (strcmp(x_rpcpass, login_passwd) != 0) {
			/*
			 * the passwds don't match, get the unix
			 * login passwd
			 */
			(void) sprintf(prompt,
				"Please enter the login password for %s:",
				pw->pw_name);
			l_pass = getpass(prompt);
			if (l_pass && (strlen(l_pass) != 0)) {
				x_lpass = crypt(l_pass, login_passwd);
				if (strcmp(x_lpass, login_passwd) != 0) {
					/* try again ... */
					(void) sprintf(prompt,
			"Try again. Please enter the login password for %s:",
						pw->pw_name);
					l_pass = getpass(prompt);
					if (l_pass && (strlen(l_pass) != 0)) {
						x_lpass = crypt(l_pass,
							login_passwd);
						if (strcmp(x_lpass, l_pass)
								!= 0) {
							(void) fprintf(stderr,
								"Sorry.\n");
							return (0);
						}
						/* passwds match */
						strcpy(newpass, l_pass);
					} else {
						(void) fprintf(stderr,
					"%s: key-pair unchanged for %s.\n",
					program_name, pw->pw_name);
						return (0);
					}
				}
				/* passwds match */
				strcpy(newpass, l_pass);
			} else {
				/* need a passwd */
				(void) sprintf(prompt,
		"Need a password. Please enter the login password for %s:",
						pw->pw_name);
				l_pass = getpass(prompt);
				if (l_pass && (strlen(l_pass) != 0)) {
					x_lpass = crypt(l_pass, login_passwd);
					if (strcmp(x_lpass, l_pass) != 0) {
						(void) fprintf(stderr,
							"Sorry.\n");
						return (0);
					}
					/* passwds match */
					strcpy(newpass, l_pass);
				} else {
					(void) fprintf(stderr,
					"%s: key-pair unchanged for %s.\n",
					program_name, pw->pw_name);
					return (0);
				}
			}
		}
		/* rpc and login passwds match */
		strcpy(newpass, rpcpass);
	} else { 	/* no login passwd entry */
		(void) fprintf(stderr,
		"%s: no passwd found for %s in the shadow passwd entry.\n",
			program_name, pw->pw_name);
		return (0);
	}
	return (1);
}
