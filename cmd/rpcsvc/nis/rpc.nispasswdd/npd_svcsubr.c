/*
 *	npd_svcsubr.c
 *	Contains the sub-routines required by the server.
 *
 *	Copyright (c) 1994 Sun Microsystems, Inc.
 *	All Rights Reserved.
 */

#pragma ident	"@(#)npd_svcsubr.c	1.6	95/02/28 SMI"

#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <memory.h>
#include <shadow.h>
#include <crypt.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpc/key_prot.h>
#include <rpc/des_crypt.h>
#include <mp.h>
#include <rpcsvc/nis.h>
#include <rpcsvc/nispasswd.h>

extern int verbose;
extern bool_t debug;
extern bool_t generatekeys;
extern int xdecrypt();
extern int xencrypt();

/*
 * get the password information for this user
 * the result should be freed using nis_freeresult()
 */
struct nis_result *
nis_getpwdent(user, domain)
char		*user;
char		*domain;
{
	char	buf[NIS_MAXNAMELEN];

	if ((user == NULL || *user == '\0') ||
		(domain == NULL || *domain == '\0'))
		return (NULL);

	/* strlen("[name=],passwd.org_dir.") + null + "." = 25 */
	if ((25 + strlen(user) + strlen(domain)) >
					(size_t) NIS_MAXNAMELEN)
		return (NULL);

	(void) sprintf(buf, "[name=%s],passwd.org_dir.%s", user, domain);
	if (buf[strlen(buf) - 1] != '.')
		(void) strcat(buf, ".");

	return (nis_list(buf, USE_DGRAM+MASTER_ONLY, NULL, NULL));
}

/*
 * get the credential information for this user
 * the result should be freed using nis_freeresult()
 */
static struct nis_result *
nis_getcredent(principal, domain, auth_type)
char	*principal;
char	*domain;
char	*auth_type;
{
	char	buf[NIS_MAXNAMELEN];

	if ((principal == NULL || *principal == '\0') ||
		(domain == NULL || *domain == '\0') || auth_type == NULL)
		return (NULL);

	/* strlen("[cname=,auth_type=],cred.org_dir.") + null + "." = 35 */
	if ((35 + strlen(principal) + strlen(domain)) >
				(size_t) NIS_MAXNAMELEN)
		return (NULL);

	(void) sprintf(buf, "[cname=%s,auth_type=%s],cred.org_dir.%s",
			principal, auth_type, domain);

	if (buf[strlen(buf) - 1] != '.')
		(void) strcat(buf, ".");

	return (nis_list(buf, USE_DGRAM+MASTER_ONLY, NULL, NULL));
}

static bool_t
__npd_fill_spwd(obj, sp)
struct nis_object *obj;
struct	spwd	*sp;
{
	long	val;
	char	*p = NULL, *ageinfo = NULL;
	char	*ep, *end;

	/* defaults */
	sp->sp_lstchg = -1;
	sp->sp_min = -1;
	sp->sp_max = -1;
	sp->sp_warn = -1;
	sp->sp_inact = -1;
	sp->sp_expire = -1;
	sp->sp_flag = 0;

	/* shadow information is in column 7 */
	if ((p = ENTRY_VAL(obj, 7)) == NULL)
		return (FALSE);
	if ((ageinfo = strdup(p)) == NULL)
		return (FALSE);

	p = ageinfo;
	end = ageinfo + ENTRY_LEN(obj, 7);

	/* format is: lstchg:min:max:warn:inact:expire:flag */

	val = strtol(p, &ep, 10);	/* base = 10 */
	if (*ep != ':' || ep >= end) {
		(void) free(ageinfo);
		return (FALSE);
	}
	if (ep != p)
		sp->sp_lstchg = val;
	p = ep + 1;

	val = strtol(p, &ep, 10);
	if (*ep != ':' || ep >= end) {
		(void) free(ageinfo);
		return (FALSE);
	}
	if (ep != p)
		sp->sp_min = val;
	p = ep + 1;

	val = strtol(p, &ep, 10);
	if (*ep != ':' || ep >= end) {
		(void) free(ageinfo);
		return (FALSE);
	}
	if (ep != p)
		sp->sp_max = val;
	p = ep + 1;

	val = strtol(p, &ep, 10);
	if (*ep != ':' || ep >= end) {
		(void) free(ageinfo);
		return (FALSE);
	}
	if (ep != p)
		sp->sp_warn = val;
	p = ep + 1;

	val = strtol(p, &ep, 10);
	if (*ep != ':' || ep >= end) {
		(void) free(ageinfo);
		return (FALSE);
	}
	if (ep != p)
		sp->sp_inact = val;
	p = ep + 1;

	val = strtol(p, &ep, 10);
	if (*ep != ':' || ep >= end) {
		(void) free(ageinfo);
		return (FALSE);
	}
	if (ep != p)
		sp->sp_expire = val;
	p = ep + 1;

	val = strtol(p, &ep, 10);
	if (*ep != ':' || ep >= end) {
		(void) free(ageinfo);
		return (FALSE);
	}
	if (ep != p)
		sp->sp_flag = val;

	(void) free(ageinfo);
	return (TRUE);
}
/*
 * checks if the password has aged sufficiently
 */
bool_t
__npd_has_aged(obj, res)
struct nis_object *obj;
int	*res;
{
	struct	spwd	sp;
	long	now;

	if (__npd_fill_spwd(obj, &sp) == TRUE) {

		now = DAY_NOW;
		if (sp.sp_lstchg != 0 && sp.sp_lstchg <= now) {
			/* password changed before or just now */
			if (now < (sp.sp_lstchg + sp.sp_min)) {
				*res = NPD_NOTAGED;
				return (FALSE);
			} else
				return (TRUE);
		}
	}
	*res = NPD_NOSHDWINFO;
	return (FALSE);
}

/*
 * authenticate the admin by decrypting their secret key with
 * the passwd they sent across.
 */
bool_t
__authenticate_admin(prin, pass, authflav)
char	*prin;
char	*pass;
char	*authflav;
{
	char	*d;
	struct	nis_result	*cres;
	char	*sp, secret[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];

	if ((prin == NULL || *prin == '\0') ||
		(pass == NULL || *pass == '\0') || authflav == NULL)
		return (FALSE);

	d = strchr(prin, '.');
	if (d == NULL)
		d = nis_local_directory();
	else
		d++;

	cres = nis_getcredent(prin, d, authflav);
	if (cres != NULL && cres->status == NIS_SUCCESS) {
		sp = ENTRY_VAL(NIS_RES_OBJECT(cres), 4);
		if (sp != NULL) {
			memcpy(secret, sp,
				ENTRY_LEN(NIS_RES_OBJECT(cres), 4));
			if ((xdecrypt(secret, pass) != 0) &&
				(memcmp(secret, &(secret[HEXKEYBYTES]),
					KEYCHECKSUMSIZE) == 0)) {
				memset(secret, 0, HEXKEYBYTES + 1);
				(void) nis_freeresult(cres);
				return (TRUE);
			}
		}
	}
	if (cres != NULL)
		(void) nis_freeresult(cres);
	return (FALSE);
}

/*
 * build a netname given a nis+ principal name
 */
bool_t
__npd_prin2netname(prin, netname)
char	*prin;
char	netname[MAXNETNAMELEN];
{
	nis_result	*pass_res;
	nis_object	*pobj;
	char		name[NIS_MAXNAMELEN];
	char		*domain;
	uid_t		uid;


	if (prin == NULL || *prin == '\0')
		return (FALSE);
	if (strlen(prin) > (size_t) NIS_MAXNAMELEN)
		return (FALSE);
	(void) sprintf(name, "%s", prin);

	/* get the domain name */
	if (name[strlen(name) - 1] == '.')
		name[strlen(name) - 1] = '\0';
	else
		return (FALSE);
	domain = strchr(name, '.');
	if (domain == NULL)
		return (FALSE);
	else
		*domain++ = '\0';

	/* nis_getpwdent() will fully qualify the domain */
	pass_res = nis_getpwdent(name, domain);

	if (pass_res == NULL)
		return (FALSE);

	switch (pass_res->status) {
	case NIS_SUCCESS:
		pobj = NIS_RES_OBJECT(pass_res);
		uid = atol(ENTRY_VAL(pobj, 2));
		(void) nis_freeresult(pass_res);
		/* we cannot simply call user2netname() ! */
		if ((strlen(domain) + 7 + 11) > (size_t) MAXNETNAMELEN)
			return (FALSE);
		(void) sprintf(netname, "unix.%d@%s", uid, domain);
		return (TRUE);

	case NIS_NOTFOUND:
		/*
		 * assumption: hosts DO NOT have their passwords
		 * stored in NIS+ ==> not a user but a host
		 */
		if ((strlen(domain) + 7 + strlen(name)) >
						(size_t) MAXNETNAMELEN)
			return (FALSE);
		(void) sprintf(netname, "unix.%s@%s", name, domain);
		(void) nis_freeresult(pass_res);
		return (TRUE);

	default:
		syslog(LOG_ERR, "no passwd entry found for %s", prin);
		(void) nis_freeresult(pass_res);
		return (FALSE);
	}
}

/*
 * make a new cred entry and add it
 */
bool_t
__npd_addcredent(prin, domain, authflav, newpass)
char	*prin;		/* principal name */
char	*domain;	/* domain name */
char	*authflav;	/* auth flavor */
char	*newpass;	/* new passwd used to encrypt secret key */
{
	char	public[HEXKEYBYTES + 1];
	char	secret[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];
	char	xsecret[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];
	nis_object	obj;
	entry_col	ecol[5];
	struct nis_result *mod_res;
	char	buf[NIS_MAXNAMELEN];
	char	nisdomain[NIS_MAXNAMELEN];
	char	netname[MAXNETNAMELEN];
	int	status;

	/* check args */
	if ((prin == NULL || *prin == '\0') ||
		(domain == NULL || *domain == '\0') || authflav == NULL)
		return (FALSE);

	if (strlen(domain) > (size_t) NIS_MAXNAMELEN)
		return (FALSE);
	(void) sprintf(nisdomain, "%s", domain);
	if (nisdomain[strlen(buf) - 1] != '.')
		(void) strcat(buf, ".");

	/* build netname from principal name */
	if (__npd_prin2netname(prin, netname) == FALSE)
		return (FALSE);

	/* initialize object */
	memset((char *)&obj, 0, sizeof (obj));
	memset((char *)ecol, 0, sizeof (ecol));

	obj.zo_name = "cred";
	obj.zo_ttl = 43200;
	obj.zo_data.zo_type = ENTRY_OBJ;
	obj.zo_owner = prin;
	obj.zo_group = nis_local_group();
	obj.zo_domain = nisdomain;
			/* owner: r; group: rmcd */
	obj.zo_access = (NIS_READ_ACC<<16)|
			((NIS_READ_ACC|NIS_MODIFY_ACC|
			NIS_CREATE_ACC|NIS_DESTROY_ACC)<<8);

	if (strcasecmp(authflav, "DES") == 0) {
		/* generate key-pair */
		(void) __gen_dhkeys(public, secret, newpass);
		(void) memcpy(xsecret, secret, HEXKEYBYTES);
		(void) memcpy(xsecret + HEXKEYBYTES, secret, KEYCHECKSUMSIZE);
		xsecret[HEXKEYBYTES + KEYCHECKSUMSIZE] = 0;
		if (xencrypt(xsecret, newpass) == 0)
			return (FALSE);

		/* build cred entry */
		ecol[0].ec_value.ec_value_val = prin;
		ecol[0].ec_value.ec_value_len = strlen(prin) + 1;

		ecol[1].ec_value.ec_value_val = "DES";
		ecol[1].ec_value.ec_value_len = 4;

		ecol[2].ec_value.ec_value_val = netname;
		ecol[2].ec_value.ec_value_len = strlen(netname) + 1;

		ecol[3].ec_value.ec_value_val = public;
		ecol[3].ec_value.ec_value_len = strlen(public) + 1;

		ecol[4].ec_value.ec_value_val = xsecret;
		ecol[4].ec_value.ec_value_len = strlen(xsecret) + 1;
		ecol[4].ec_flags |= EN_CRYPT;
	}
	obj.EN_data.en_type = "cred_tbl";
	obj.EN_data.en_cols.en_cols_val = ecol;
	obj.EN_data.en_cols.en_cols_len = 5;

	/* strlen("cred.org_dir.") + null = 14 */
	if ((strlen(nisdomain) + 14) > (size_t) NIS_MAXNAMELEN)
		return (FALSE);
	(void) sprintf(buf, "cred.org_dir.%s", (char *)&nisdomain[0]);

	if (debug == TRUE)
		(void) nis_print_object(&obj);

	mod_res = nis_add_entry(buf, &obj, 0);
	status = mod_res->status;
	(void) nis_freeresult(mod_res);
	switch (status) {
	case NIS_SUCCESS:
		return (TRUE);

	case NIS_PERMISSION:
		if (verbose == TRUE)
			syslog(LOG_ERR,
			"permission denied to add a %s cred entry for %s",
			authflav, prin);
		return (FALSE);

	default:
		if (verbose == TRUE)
			syslog(LOG_ERR,
			"error creating %s cred for %s, NIS+ error: %s",
			authflav, prin, nis_sperrno(status));
		return (FALSE);
	}
}

/*
 * reencrypt the secret key (or generate a new key-pair)
 * and update the cred table.
 */
int
__npd_upd_cred(user, domain, authflav, oldpass, newpass, err)
char	*user;		/* user name */
char	*domain;	/* domainname */
char	*authflav;
char	*oldpass;
char	*newpass;
int	*err;
{
	struct nis_result *cred_res, *mod_res;
	nis_object *cobj, *eobj;
	char	prin[NIS_MAXNAMELEN];
	char	secret[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];
	char	xsecret[HEXKEYBYTES + KEYCHECKSUMSIZE + 1];
	char	public[HEXKEYBYTES + 1];
	entry_col	ecol[5];
	char	buf[NIS_MAXNAMELEN];
	int	status;

	if ((user == NULL || *user == '\0') ||
		(domain == NULL || *domain == '\0') || authflav == NULL) {
		*err = NPD_KEYNOTREENC;
		return (NIS_SYSTEMERROR);
	}
	/* "." + "." + null = 3 */
	if ((strlen(user) + strlen(domain) + 3) > (size_t) NIS_MAXNAMELEN) {
		*err = NPD_KEYNOTREENC;
		return (NIS_SYSTEMERROR);
	}
	(void) sprintf(prin, "%s.%s", user, domain);
	if (prin[strlen(prin) - 1] != '.')
		(void) strcat(prin, ".");

	cred_res = nis_getcredent(prin, domain, authflav);
	if (cred_res == NULL) {
		*err = NPD_KEYNOTREENC;
		return (NIS_SYSTEMERROR);
	}
	status = cred_res->status;
	switch (status) {
	case NIS_SUCCESS:
		if (verbose == TRUE)
			syslog(LOG_ERR, "found a %s cred entry for %s",
				authflav, prin);
		cobj = NIS_RES_OBJECT(cred_res);
		if (cobj == NULL) {
			*err = NPD_KEYNOTREENC;
			break;
		}
		/* attempt to reencrypt secret key */
		memcpy(secret, ENTRY_VAL(cobj, 4), ENTRY_LEN(cobj, 4));

		if ((xdecrypt(secret, oldpass) == 0) ||
			(memcmp(secret, &(secret[HEXKEYBYTES]),
					KEYCHECKSUMSIZE) != 0))
			if (generatekeys == TRUE) {
				(void) __gen_dhkeys(public, secret, newpass);
				*err = NPD_KEYSUPDATED;
			} else {
				*err = NPD_KEYNOTREENC;
				break;
			}
		memcpy(xsecret, secret, HEXKEYBYTES);
		memcpy(xsecret + HEXKEYBYTES, secret, KEYCHECKSUMSIZE);
		xsecret[HEXKEYBYTES + KEYCHECKSUMSIZE] = 0;
		if (xencrypt(xsecret, newpass) == 0) {
			*err = NPD_KEYNOTREENC;
			break;
		}

		memset((char *)ecol, 0, sizeof (ecol));

		if (generatekeys == TRUE) {	/* public key changed too */
			ecol[3].ec_value.ec_value_val = public;
			ecol[3].ec_value.ec_value_len = strlen(public) + 1;
			ecol[3].ec_flags = EN_MODIFIED;
		}
		ecol[4].ec_value.ec_value_val = xsecret;
		ecol[4].ec_value.ec_value_len = strlen(xsecret) + 1;
		ecol[4].ec_flags = EN_MODIFIED|EN_CRYPT;

		/* strlen("[cname=,auth_type=],cred.") + null + "." = 27 */
		if ((strlen(prin) + strlen(cobj->zo_domain) + strlen(authflav)
			+ 27) > (size_t) NIS_MAXNAMELEN) {
			*err = NPD_KEYNOTREENC;
			break;
		}
		(void) sprintf(buf, "[cname=%s,auth_type=%s],cred.%s",
			prin, authflav, cobj->zo_domain);
		if (buf[strlen(buf) - 1] != '.')
			(void) strcat(buf, ".");

		eobj = nis_clone_object(cobj, NULL);
		if (eobj == NULL) {
			*err = NPD_KEYNOTREENC;
			break;
		}
		eobj->EN_data.en_cols.en_cols_val = ecol;
		eobj->EN_data.en_cols.en_cols_len = 5;

		mod_res = nis_modify_entry(buf, eobj, 0);
		status = mod_res->status;
		if (status != NIS_SUCCESS)
			*err = NPD_KEYNOTREENC;

		/* set ecol to null so that we can free eobj */
		eobj->EN_data.en_cols.en_cols_val = NULL;
		eobj->EN_data.en_cols.en_cols_len = 0;
		(void) nis_destroy_object(eobj);
		(void) nis_freeresult(mod_res);
		break;

	case NIS_NOTFOUND:
		if (verbose == TRUE)
			syslog(LOG_ERR, "no %s cred for %s", authflav, prin);
		if ((generatekeys == TRUE) &&
		    (__npd_addcredent(prin, domain, authflav, newpass)
			== TRUE))
			*err = NPD_KEYSUPDATED;
		else
			*err = NPD_KEYNOTREENC;
		break;
	default:
		*err = NPD_KEYNOTREENC;
		syslog(LOG_ERR,
			"NIS+ error (%d) getting cred entry for %s",
			status, prin);
		break;
	}
	(void) nis_freeresult(cred_res);
	return (status);
}

/*
 * encrypt new passwd
 */
char *
__npd_encryptpass(pass)
char	*pass;
{
	char	saltc[2];
	long	salt;
	int	i, c;

	if (pass == NULL || *pass == '\0')
		return (NULL);

	(void) time((time_t *)&salt);
	salt += (long) getpid();

	saltc[0] = salt & 077;
	saltc[1] = (salt >> 6) & 077;
	for (i = 0; i < 2; i++) {
		c = saltc[i] + '.';
		if (c > '9') c += 7;
		if (c > 'Z') c += 6;
		saltc[i] = c;
	}
	return (crypt(pass, saltc));
}


/*
 * find the passwd object for this user from the list
 * of dirs given. If object is found in more than one
 * place return an error, otherwise clone the object.
 * Note, the object should be freed using nis_destroy_object().
 */
bool_t
__npd_find_obj(user, dirlist, obj)
char	*user;
char	*dirlist;
nis_object **obj;	/* returned */
{
	char	*tmplist;
	char	*curdir, *end = NULL;
	char	buf[NIS_MAXNAMELEN];
	nis_result	*tmpres;
	nis_object	*tmpobj = NULL;

	if ((user == NULL || *user == '\0') ||
		(dirlist == NULL || *dirlist == '\0'))
		return (FALSE);

	*obj = NULL;
	if ((tmplist = strdup(dirlist)) == NULL) {
		syslog(LOG_CRIT, "out of memory");
		return (FALSE);
	}
	for (curdir = tmplist; curdir != NULL; curdir = end) {
		end = strchr(curdir, ' ');
		if (end != NULL)
			*end++ = NULL;
		if (strncasecmp(curdir, "org_dir", 7) != 0)
			continue;	/* not an org_dir */

		/* strlen("[name=],passwd." + null + "." = 17 */
		if ((strlen(user) + 17 + strlen(curdir)) >
				(size_t) NIS_MAXNAMELEN) {
			(void) free(tmplist);
			return (FALSE);
		}

		(void) sprintf(buf, "[name=%s],passwd.%s", user, curdir);
		if (buf[strlen(buf) - 1] != '.')
			(void) strcat(buf, ".");
		tmpres = nis_list(buf, USE_DGRAM+MASTER_ONLY, NULL, NULL);
		switch (tmpres->status) {
		case NIS_NOTFOUND:	/* skip */
			(void) nis_freeresult(tmpres);
			continue;

		case NIS_SUCCESS:
			if (NIS_RES_NUMOBJ(tmpres) != 1) {
				/* should only have one entry */
				(void) nis_freeresult(tmpres);
				(void) free(tmplist);
				if (tmpobj != NULL)
					(void) nis_destroy_object(tmpobj);
				return (FALSE);
			}
			if (tmpobj != NULL) {
				/* found in more than one dir */
				(void) nis_destroy_object(tmpobj);
				(void) nis_freeresult(tmpres);
				(void) free(tmplist);
				*obj = NULL;
				return (FALSE);
			}
			tmpobj = nis_clone_object(NIS_RES_OBJECT(tmpres), NULL);
			if (tmpobj == NULL) {
				syslog(LOG_CRIT, "out of memory");
				(void) nis_freeresult(tmpres);
				(void) free(tmplist);
				return (FALSE);
			}
			(void) nis_freeresult(tmpres);
			continue;

		default:
			/* some NIS+ error - quit processing */
			curdir = NULL;
			break;
		}
	}
	(void) nis_freeresult(tmpres);
	(void) free(tmplist);
	if (tmpobj == NULL)	/* no object found */
		return (FALSE);
	*obj = tmpobj;
	return (TRUE);
}

/*
 * go thru' the list of dirs and see if the host is a
 * master server for any 'org_dir'.
 */
bool_t
__npd_am_master(host, dirlist)
char	*host;
char	*dirlist;
{
	char	*tmplist;
	char	*curdir, *end = NULL;
	nis_server	**srvs;

	if ((host == NULL || *host == '\0') ||
		(dirlist == NULL || *dirlist == '\0'))
		return (FALSE);

	if ((tmplist = strdup(dirlist)) == NULL) {
		syslog(LOG_CRIT, "out of memory");
		return (FALSE);
	}
	for (curdir = tmplist; curdir != NULL; curdir = end) {
		end = strchr(curdir, ' ');
		if (end != NULL)
			*end++ = NULL;
		if (strncasecmp(curdir, "org_dir", 7) != 0)
			continue;	/* not an org_dir */

		srvs = nis_getservlist(curdir);
		if (srvs == NULL) {
			syslog(LOG_ERR,
		"cannot get a list of servers that serve '%s'", curdir);
			(void) free(tmplist);
			return (FALSE);
		}
		if (strcasecmp(host, srvs[0]->name) == 0) {
			(void) free(tmplist);
			(void) nis_freeservlist(srvs);
			return (TRUE);
		}
		(void) nis_freeservlist(srvs);
	}
	(void) free(tmplist);
	return (FALSE);
}


/*
 * check whether this principal has permission to
 * add/delete/modify an entry object
 */
bool_t
__npd_can_do(right, obj, prin, column)
unsigned long	right;		/* access right seeked */
nis_object *obj;		/* entry object */
char	*prin;			/* principal seeking access */
int	column;			/* column being modified */
{
	nis_result	*res;
	nis_object	*tobj;
	table_col	*tc;
	char		buf[NIS_MAXNAMELEN];
	int		mod_ok;

	/* strlen("passwd." + null + "." = 9 */
	if ((9 + strlen(obj->zo_domain)) > (size_t) NIS_MAXNAMELEN) {
		return (FALSE);
	}
	(void) sprintf(buf, "passwd.%s", obj->zo_domain);
	if (buf[strlen(buf) - 1] != '.')
		(void) strcat(buf, ".");

	res = nis_lookup(buf, MASTER_ONLY);

	if (res->status == NIS_SUCCESS) {
		tobj = NIS_RES_OBJECT(res);
		if (__type_of(tobj) != TABLE_OBJ) {
			(void) nis_freeresult(res);
			return (FALSE);
		}
	} else {
		(void) nis_freeresult(res);
		return (FALSE);
	}

	/* check the permission on the table */
	mod_ok = __nis_ck_perms(right, tobj->zo_access, tobj, prin, 1);
	if (mod_ok == TRUE) {
		(void) nis_freeresult(res);
		return (TRUE);
	}

	/* check the permission on the obj */
	mod_ok = __nis_ck_perms(right, obj->zo_access, obj, prin, 1);
	if (mod_ok == TRUE) {
		(void) nis_freeresult(res);
		return (TRUE);
	}

	if (column > tobj->TA_data.ta_maxcol) {
		/* invalid column */
		(void) nis_freeresult(res);
		return (FALSE);
	}
	tc = tobj->TA_data.ta_cols.ta_cols_val;	/* table columns */

	/* check the permission on column being modified */
	mod_ok = __nis_ck_perms(right, tc[column].tc_rights, obj, prin, 1);

	(void) nis_freeresult(res);
	return (mod_ok);
}

void
__npd_gen_rval(randval)
unsigned long *randval;
{
	int		i, shift;
	struct timeval	tv;
	unsigned int	seed = 0;

	for (i = 0; i < 1024; i++) {
		(void) gettimeofday(&tv, NULL);
		shift = i % 8 * sizeof (int);
		seed ^= (tv.tv_usec << shift) | (tv.tv_usec >> (32 - shift));
	}
	(void) srandom(seed);
	*randval = (u_long) random();
}
