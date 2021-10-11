/*
 *	makedescred.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)makedescred.c	1.23	95/01/31 SMI"

/*
 * makedescred.c
 *
 * Make a "AUTH_DES" credential. This is the old secure rpc credentials from
 * SunOS 4.0 and Vanilla System V release 4.0.
 */


#include <stdio.h>
#include <pwd.h>
#ifndef TDRPC
#include <shadow.h>
#include <nsswitch.h>
#else
#include <fcntl.h>
#endif
#include <netdb.h>
#include <rpcsvc/nis.h>
#include <rpc/key_prot.h>
#include "nisaddcred.h"

extern char *getpass();
extern char *crypt();

static int force = 1;   /*  Eventually, this will be an option */

static const char *OPSYS = "unix";
#define	OPSYS_LEN 4
#define	ROOTKEY_FILE "/etc/.rootkey"

#ifndef TDRPC
/* ************************ switch functions *************************** */

/*	NSW_NOTSUCCESS  NSW_NOTFOUND   NSW_UNAVAIL    NSW_TRYAGAIN */
#define	DEF_ACTION {__NSW_RETURN, __NSW_RETURN, __NSW_CONTINUE, __NSW_CONTINUE}

static struct __nsw_lookup lookup_files = {"files", DEF_ACTION, NULL, NULL},
		lookup_nis = {"nis", DEF_ACTION, NULL, &lookup_files};
static struct __nsw_switchconfig publickey_default =
			{0, "publickey", 2, &lookup_nis};


char *
switch_policy_str(struct __nsw_switchconfig *conf)
{
	struct __nsw_lookup *look;
	static char policy[256];
	int previous = 0;

	memset((char *)policy, 0, 256);

	for (look = conf->lookups; look; look = look->next) {
		if (previous)
			strcat(policy, " ");
		strcat(policy, look->service_name);
		previous = 1;
	}

	return (policy);
}

int
no_switch_policy(struct __nsw_switchconfig *conf)
{
	return (conf == NULL || conf->lookups == NULL);
}

is_switch_policy(struct __nsw_switchconfig *conf, char *target)
{
	return (conf && conf->lookups &&
		strcmp(conf->lookups->service_name, target) == 0 &&
		conf->lookups->next == NULL);
}


int
check_switch_policy(char *policy, char *target_service,
		    struct __nsw_switchconfig *default_conf,
		    char *head_msg, char *tail_msg)
{
	struct __nsw_switchconfig *conf;
	enum __nsw_parse_err perr;
	int policy_correct = 1;

	conf = __nsw_getconfig(policy, &perr);
	if (no_switch_policy(conf)) {
		if (!is_switch_policy(default_conf, target_service)) {
			fprintf(stderr,
				"\n%s\n There is no publickey entry in %s.\n",
				head_msg, __NSW_CONFIG_FILE);
			fprintf(stderr,
			" The default publickey policy is \"publickey: %s\".\n",
			switch_policy_str(default_conf));
			policy_correct = 0;
		}
	} else if (!is_switch_policy(conf, target_service)) {
		fprintf(stderr,
		"\n%s\n The publickey entry in %s is \"publickey: %s\".\n",
			head_msg, __NSW_CONFIG_FILE,
			switch_policy_str(conf));
		policy_correct = 0;
	}
	/* should we exit ? */
	if (policy_correct == 0)
		fprintf(stderr,
		" It should be \"publickey: %s\"%s.\n\n",
			target_service, tail_msg);
	if (conf)
		__nsw_freeconfig(conf);

	return (policy_correct);
}
#else
#define	check_switch_policy(a, b, c, d, e)
#endif

/* ******************************************************************** */
/* Check that data to be entered makes sense */
static int
sanity_checks(char *nis_princ, char *netname, char *domain)
{
	char		netdomainaux[MAXHOSTNAMELEN+1];
	char		*princdomain, *netdomain;
	int		len;

	/* Sanity check 0. Do we have a nis+ principal name to work with? */
	if (nis_princ == NULL) {
		fprintf(stderr,
			"%s: you must create a \"local\" credential first.\n",
			program_name);
		fprintf(stderr,
			"rerun this command as : %s local \n", program_name);
		return (0);
	}

	/* Sanity check 0.5.  NIS+ principal names must be dotted. */
	len = strlen(nis_princ);
	if (nis_princ[len-1] != '.') {
		fprintf(stderr,
		"%s: invalid principal name: '%s' (forgot ending dot?).\n",
			program_name, nis_princ);
		return (0);
	}

	/* Sanity check 1.  We only deal with one type of netnames. */
	if (strncmp(netname, OPSYS, OPSYS_LEN) != 0) {
		fprintf(stderr, "%s: unrecognized netname type: '%s'.\n",
			program_name, netname);
		return (0);
	}

	/* Sanity check 2.  Should only add DES cred in home domain. */
	princdomain = nis_domain_of(nis_princ);
	if (strcasecmp(princdomain, domain) != 0) {
		fprintf(stderr,
"%s: domain of principal '%s' does not match destination domain '%s'.\n",
			program_name, nis_princ, domain);
		fprintf(stderr,
	"Should only add DES credential of principal in its home domain\n");
		return (0);
	}

	/*
	 * Sanity check 3:  Make sure netname's domain same as principal's
	 * and don't have extraneous dot at the end.
	 */
	netdomain = (char *)strchr(netname, '@');
	if (! netdomain) {
		fprintf(stderr, "%s: invalid netname, missing @: '%s'. \n",
			program_name, netname);
		return (0);
	}
	if (netname[strlen(netname)-1] == '.') {
		fprintf(stderr, "%s: invalid netname, trailing dot: '%s'. \n",
			program_name, netname);
		return (0);
	}
	netdomain++; /* skip '@' */
	strcpy(netdomainaux, netdomain);
	if (netdomainaux[strlen(netdomainaux)-1] != '.')
		strcat(netdomainaux, ".");

	if (strcasecmp(princdomain, netdomainaux) != 0) {
		fprintf(stderr,
	"%s: domain of netname %s should be same as that of principal %s\n",
			program_name, netname, nis_princ);
		return (0);
	}

	/* Check publickey policy and warn user if it is not NIS+ */
	check_switch_policy("publickey", "nisplus", &publickey_default,
			    "WARNING:", " when using NIS+");

	/* Another principal owns same credentials? (exits if that happens) */
	(void) auth_exists(nis_princ, netname, "DES", domain);

	return (1); /* all passed */
}

/* ***************************** keylogin stuff *************************** */
int
keylogin(char *netname, char *secret)
{
	struct key_netstarg netst;

	netst.st_pub_key[0] = 0;
	memcpy(netst.st_priv_key, secret, HEXKEYBYTES);
	netst.st_netname = netname;

#ifdef NFS_AUTH
	nra.authtype = AUTH_DES;	/* only revoke DES creds */
	nra.uid = getuid();		/* use the real uid */
	if (_nfssys(NFS_REVAUTH, &nra) < 0) {
		perror("Warning: NFS credentials not destroyed");
		err = 1;
	}
#endif NFS_AUTH


	/* do actual key login */
	if (key_setnet(&netst) < 0) {
		fprintf(stderr, "Could not set %s's secret key\n", netname);
		fprintf(stderr, "May be the keyserv is down?\n");
		return (0);
	}

	return (1);
}

/* write unencrypted secret key into root key file */
void
write_rootkey(char *secret)
{
	char sbuf[HEXKEYBYTES+2];
	int fd, len;

	strcpy(sbuf, secret);
	strcat(sbuf, "\n");
	len = strlen(sbuf);
	sbuf[len] = '\0';
	unlink(ROOTKEY_FILE);
	if ((fd = open(ROOTKEY_FILE, O_WRONLY+O_CREAT, 0600)) != -1) {
		write(fd, sbuf, len+1);
		close(fd);
		fprintf(stderr, "Wrote secret key into %s\n",
			ROOTKEY_FILE);
	} else {
		fprintf(stderr, "Could not open %s for update\n",
			ROOTKEY_FILE);
	}
}


static
char *
get_password(uid_t uid, int same_host, char *target_host, char *domain)
{
	static char	password[256];
	char		prompt[256];
	char		*encrypted_password, *login_password = NULL, *pass;
	struct passwd	*pw;
	int passwords_matched = 0;
	struct passwd	*domain_getpwuid();
	struct spwd	*domain_getspnam();

#ifndef TDRPC
	struct spwd *spw;

	/* ignore password checking when the -l option is used */
	if (nispasswd[0] != '\0')
		return (nispasswd);

	if (uid == 0) {
		if (same_host) {
			/*
			 *  The root user is never in the NIS+
			 *  data base.  Get it locally.
			 */
			pw = getpwuid(0);
			if (! pw) {
				fprintf(stderr,
			"%s: unable to locate password record for uid %d\n",
					program_name, uid);
				return (0);
			}
			spw = getspnam(pw->pw_name);
			if (!spw) {
				fprintf(stderr,
			"%s: unable to locate password record for uid 0\n",
					program_name);
				return (0);
			}
			login_password = spw->sp_pwdp;
		}
	} else {
		pw = domain_getpwuid(domain, uid);
		if (pw) {
			/* get password from shadow */
			spw = domain_getspnam(domain, pw->pw_name);
			if (spw) {
				login_password = spw->sp_pwdp;
			}
		} else {
			return (0);
		}
	}
#else
	/* ignore password checking when the -l option is used */
	if (nispasswd[0] != '\0')
		return (nispasswd);

	pw = getpwuid(uid);
	if (! pw) {
		fprintf(stderr,
			"%s: unable to locate password record for uid %d\n",
				program_name, uid);
			return (0);
	}
	if (strlen(pw->pw_passwd) > 1)
		login_password = pw->pw_passwd;
#endif

	if ((uid == my_uid) && ((uid != 0) || same_host))
		sprintf(prompt, "Enter login password:");
	else if (uid == 0) {
		sprintf(prompt, "Enter %s's root login password:",
			target_host);
	} else
		sprintf(prompt, "Enter %s's login password:",
			pw->pw_name);
	pass = getpass(prompt);
	if (strlen(pass) == 0) {
		(void) fprintf(stderr, "%s: Password unchanged.\n",
								program_name);
		return (0);
	}
	strcpy(password, pass);


	/* Verify that password supplied matches login password */
	if (login_password && (strlen(login_password) != 0)) {
		encrypted_password = crypt(password, login_password);
		if (strcmp(encrypted_password, login_password) == 0)
			passwords_matched = 1;
		else {
			fprintf(stderr,
			"%s: %s: password differs from login password.\n",
				program_name, force? "WARNING" : "ERROR");
			if (!force)
				return (0);
		}
	}

	/* Check for mis-typed password */
	if (!passwords_matched) {
		pass = getpass("Retype password:");
		if (strcmp(password, pass) != 0) {
			(void) fprintf(stderr, "%s: password incorrect.\n",
								program_name);
			return (0);
		}
	}

	return (password);
}


/*
 *	Definitions of the credential table.
 *
 * Column	Name			Contents
 * ------	----			--------
 *   0		cname			nis principal name
 *   1		auth_type		DES
 *   2		auth_name		netname
 *   3		public_auth_data	public key
 *   4		private_auth_data	encrypted secret key with checksum
*/

/*
 * Function for building DES credentials.
 *
 * The domain may be the local domain or some remote domain.
 * 'domain' should be the same as the domain found in netname,
 * which should be the home domain of nis+ principal.
 */

int
make_des_cred(nis_princ, netname, domain)
	char	*nis_princ;	/* NIS+ principal name 		*/
	char	*netname;	/* AUTH_DES netname 	*/
	char	*domain;	/* Domain name			*/
{
	nis_object	*obj = init_entry();
	char 		*pass;
	uid_t		uid;
	char 		public[HEXKEYBYTES + 1];
	char		secret[HEXKEYBYTES + 1];
	char		crypt1[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];
	char		target_host[MAXHOSTNAMELEN+1];
	int		same_host = 0;
	int		status, len, addition;

	if (nis_princ == NULL)
		nis_princ = default_principal(domain);

	if (sanity_checks(nis_princ, netname, domain) == 0)
		return (0);

	addition = (cred_exists(nis_princ, "DES", domain) == NIS_NOTFOUND);

	/* Get password with which to encrypt secret key. */
	(void) printf("%s key pair for %s (%s).\n",
			addition? "Adding" : "Updating", netname, nis_princ);

	/* Extract user/host information from netname */
	if (! isdigit(netname[OPSYS_LEN+1])) {
		uid = 0;  /* root */
		netname2host(netname, target_host, MAXHOSTNAMELEN);
		len = strlen(my_host)-1;   /* ignore trailing dot in my_host */
		if (len == strlen(target_host) &&
		    strncasecmp(target_host, my_host, len) == 0)
			same_host = 1;
	} else {
		uid = (uid_t)atoi(netname+OPSYS_LEN+1);
	}

	pass = get_password(uid, same_host, target_host, domain);
	if (pass == 0)
		return (0);

	/* Encrypt secret key */
	(void) __gen_dhkeys(public, secret, pass);
	memcpy(crypt1, secret, HEXKEYBYTES);
	memcpy(crypt1 + HEXKEYBYTES, secret, KEYCHECKSUMSIZE);
	crypt1[HEXKEYBYTES + KEYCHECKSUMSIZE] = 0;
	xencrypt(crypt1, pass);


	/* Now we have a key pair, build up the cred entry */
	ENTRY_VAL(obj, 0) = nis_princ;
	ENTRY_LEN(obj, 0) = strlen(nis_princ) + 1;

	ENTRY_VAL(obj, 1) = "DES";
	ENTRY_LEN(obj, 1) = 4;

	ENTRY_VAL(obj, 2) = netname;
	ENTRY_LEN(obj, 2) = strlen(netname) + 1;

	ENTRY_VAL(obj, 3) = public;
	ENTRY_LEN(obj, 3) = strlen(public) + 1;
#ifdef OLD_MODE
	strcat(ENTRY_VAL(obj, 3), ":");
	ENTRY_LEN(obj, 3)++;
#endif

	ENTRY_VAL(obj, 4) = crypt1;
	ENTRY_LEN(obj, 4) = strlen(crypt1) + 1;

	if (addition) {
		obj->zo_owner = nis_princ;
		obj->zo_group = my_group;
		obj->zo_domain = domain;
		/* owner: r, group: rmcd */
		obj->zo_access = ((NIS_READ_ACC<<16)|
				(NIS_READ_ACC|NIS_MODIFY_ACC|NIS_CREATE_ACC|
					NIS_DESTROY_ACC)<<8);
		status = add_cred_obj(obj, domain);
	} else {
		obj->EN_data.en_cols.en_cols_val[3].ec_flags |= EN_MODIFIED;
		obj->EN_data.en_cols.en_cols_val[4].ec_flags |= EN_MODIFIED;
		status = modify_cred_obj(obj, domain);
	}


	/* attempt keylogin if appropriate */
	if (status) {
		if ((uid == my_uid) && ((uid != 0) || same_host))
			keylogin(netname, secret);
		if ((uid == 0) && same_host)
			write_rootkey(secret);
	}
	return (status);
}


char *
get_des_cred(domain)
char *domain;
{
	int		uid, status;
	static char netname[MAXNETNAMELEN+1];

	uid = my_uid;

	if (uid == 0)
		status = host2netname(netname, (char *)NULL, domain);
	else {
		/* generate netname using uid and domain information. */
		int len;
		len = strlen(domain);
		if ((len + OPSYS_LEN + 3 + MAXIPRINT) > MAXNETNAMELEN) {
			printf("Domain name too long: %s\n", domain);
			goto not_found;
		}
		(void) sprintf(netname, "%s.%d@%s", OPSYS, uid, domain);
		len = strlen(netname);
		if (netname[len-1] == '.')
			netname[len-1] = '\0';

		status = 1;
	}

	if (status == 1) {
		printf("DES principal name : %s\n", netname);
		return (netname);
	}

not_found:
	printf("DES principal name for %d not found\n", uid);
	return (NULL);
}
