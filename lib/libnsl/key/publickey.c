/*
 * ==== hack-attack:  possibly MT-safe but definitely not MT-hot.
 * ==== turn this into a real switch frontend and backends
 *
 * Well, at least the API doesn't involve pointers-to-static.
 */

#ident	"@(#)publickey.c	1.18	94/10/19 SMI"  /*  SVr4 1.2 */

/*
 * Copyright (c) 1986-1992 by Sun Microsystems Inc.
 */

/*
 * publickey.c
 *
 *
 * Public and Private (secret) key lookup routines. These functions
 * are used by the secure RPC auth_des flavor to get the public and
 * private keys for secure RPC principals. Originally designed to
 * talk only to YP, AT&T modified them to talk to files, and now
 * they can also talk to NIS+. The policy for these lookups is now
 * defined in terms of the nameservice switch as follows :
 *	publickey: nis files
 *
 * Note :
 * 1.  NIS+ combines the netid.byname and publickey.byname maps
 *	into a single NIS+ table named cred.org_dir
 * 2.  To use NIS+, the publickey needs to be
 *	publickey: nisplus
 *	(or have nisplus as the first entry).
 *	The nsswitch.conf file should be updated after running nisinit
 *	to reflect this.
 */
#include "../rpc/rpc_mt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <rpc/trace.h>
#include <pwd.h>
#include "nsswitch.h"
#include <rpc/rpc.h>
#include <rpc/key_prot.h>
#include <rpcsvc/nis.h>
#include <rpcsvc/ypclnt.h>

static const char *PKTABLE = "cred.org_dir";
static const char *PKMAP = "publickey.byname";
static const char *PKFILE = "/etc/publickey";
#define	PKTABLE_LEN 12
#define	WORKBUFSIZE 1024

extern int xdecrypt();

/*
 * default publickey policy:
 *	publickey: nis [NOTFOUND = return] files
 */


/*	NSW_NOTSUCCESS  NSW_NOTFOUND   NSW_UNAVAIL    NSW_TRYAGAIN */
#define	DEF_ACTION {__NSW_RETURN, __NSW_RETURN, __NSW_CONTINUE, __NSW_CONTINUE}

static struct __nsw_lookup lookup_files = {"files", DEF_ACTION, NULL, NULL},
		lookup_nis = {"nis", DEF_ACTION, NULL, &lookup_files};
static struct __nsw_switchconfig publickey_default =
			{0, "publickey", 2, &lookup_nis};

#ifndef NUL
#define	NUL '\0'
#endif

extern mutex_t serialize_pkey;

/*
 * extract_secret()
 *
 * This generic function will extract the private key
 * from a string using the given password. Note that
 * it uses the DES based function xdecrypt()
 */
static int
extract_secret(raw, private, passwd)
	char	*raw;
	char	*private;
	char	*passwd;
{
	char	buf[WORKBUFSIZE]; /* private buffer to work from */
	char	*p;

	trace1(TR_extract_secret, 0);
	if (! passwd || ! raw || ! private) {
		if (private)
			*private = NUL;
		trace1(TR_extract_secret, 1);
		return (0);
	}

	strcpy(buf, raw);
	p = strchr(buf, ':');
	if (p) {
		*p = 0;
	}
	if (! xdecrypt(buf, passwd)) {
		private[0] = 0;
		trace1(TR_extract_secret, 1);
		return (1);
	}
	if (memcmp(buf, &(buf[HEXKEYBYTES]), KEYCHECKSUMSIZE) != 0) {
		private[0] = 0;
		trace1(TR_extract_secret, 1);
		return (1);
	}

	buf[HEXKEYBYTES] = NUL;
	strcpy(private, buf);
	trace1(TR_extract_secret, 1);
	return (1);
}

/*
 * These functions are the "backends" for the switch for public keys. They
 * get both the public and private keys from each of the supported name
 * services (nis, nisplus, files). They are passed the appropriate parameters
 * and return 0 if unsuccessful with *errp set, or 1 when they got just the
 * public key and 3 when they got both the public and private keys.
 *
 *
 * getkey_nis()
 *
 * Internal implementation of getpublickey() using NIS (aka Yellow Pages,
 * aka YP).
 *
 * NOTE : *** this function returns nsswitch codes and _not_ the
 * value returned by getpublickey.
 */
static int
getkeys_nis(errp, netname, pkey, skey, passwd)
	int  *errp;
	char *netname;
	char *pkey;
	char *skey;
	char *passwd;
{
	char 	*domain;
	char	*keyval = NULL;
	int	keylen, err, r = 0;
	char	*p;

	trace1(TR_getkeys_nis, 0);

	p = strchr(netname, '@');
	if (! p) {
		*errp = __NSW_UNAVAIL;
		trace1(TR_getkeys_nis, 1);
		return (0);
	}

	domain = ++p;
	err = yp_match(domain, (char *)PKMAP, netname, strlen(netname),
			&keyval, &keylen);
	switch (err) {
	case YPERR_KEY :
		if (keyval)
			free(keyval);
		*errp = __NSW_NOTFOUND;
		trace1(TR_getkeys_nis, 1);
		return (0);
	default :
		if (keyval)
			free(keyval);
		*errp = __NSW_UNAVAIL;
		trace1(TR_getkeys_nis, 1);
		return (0);
	case 0:
		break;
	}

	p = strchr(keyval, ':');
	if (p == NULL) {
		free(keyval);
		*errp = __NSW_NOTFOUND;
		trace1(TR_getkeys_nis, 1);
		return (0);
	}
	*p = 0;
	if (pkey)
		(void) strcpy(pkey, keyval);
	r = 1;
	p++;
	if (skey && extract_secret(p, skey, passwd))
		r |= 2;
	free(keyval);
	*errp = __NSW_SUCCESS;
	trace1(TR_getkeys_nis, 1);
	return (r);
}

/*
 * getkey_files()
 *
 * The files version of getpublickey. This function attempts to
 * get the publickey from the file PKFILE .
 *
 * This function defines the format of the /etc/publickey file to
 * be :
 *	netname <whitespace> publickey:privatekey
 *
 * NOTE : *** this function returns nsswitch codes and _not_ the
 * value returned by getpublickey.
 */

static int
getkeys_files(errp, netname, pkey, skey, passwd)
	int	*errp;
	char	*netname;
	char	*pkey;
	char	*skey;
	char	*passwd;
{
	register char *mkey;
	register char *mval;
	char buf[WORKBUFSIZE];
	int	r = 0;
	char *res;
	FILE *fd;
	char *p;

	trace1(TR_getkeys_files, 0);

	fd = fopen(PKFILE, "r");
	if (fd == (FILE *) 0) {
		*errp = __NSW_UNAVAIL;
		trace1(TR_getkeys_files, 1);
		return (0);
	}

	/* Search through the file linearly :-( */
	while ((res = fgets(buf, WORKBUFSIZE, fd)) != NULL) {

		if ((res[0] == '#') || (res[0] == '\n'))
			continue;
		else {
			mkey = strtok(buf, "\t ");
			if (mkey == NULL) {
				syslog(LOG_INFO,
				"getpublickey: Bad record in %s for %s",
							PKFILE, netname);
				continue;
			}
			mval = strtok((char *)NULL, " \t#\n");
			if (mval == NULL) {
				syslog(LOG_INFO,
				"getpublickey: Bad record in %s for %s",
							PKFILE, netname);
				continue;
			}
			/* NOTE : Case insensitive compare. */
			if (strcasecmp(mkey, netname) == 0) {
				p = strchr(mval, ':');
				if (p == NULL) {
					syslog(LOG_INFO,
				"getpublickey: Bad record in %s for %s",
							PKFILE, netname);
					continue;
				}

				*p = 0;
				if (pkey)
					(void) strcpy(pkey, mval);
				r = 1;
				p++;
				if (skey && extract_secret(p, skey, passwd))
					r |= 2;
				fclose(fd);
				*errp = __NSW_SUCCESS;
				trace1(TR_getkeys_files, 1);
				return (r);
			}
		}
	}

	fclose(fd);
	*errp = __NSW_NOTFOUND;
	trace1(TR_getkeys_files, 1);
	return (0);
}

/*
 * getkeys_nisplus()
 *
 * This one fetches the key pair from NIS+.
 */
static int
getkeys_nisplus(err, netname, pkey, skey, passwd)
	int 	*err;
	char	*netname;
	char	*pkey;
	char	*skey;
	char	*passwd;
{
	nis_result	*res;
	int		r = 0;
	char		*domain, *p;
	char		buf[NIS_MAXNAMELEN+1];
	int len;

	trace1(TR_getkeys_nisplus, 0);

	domain = strchr(netname, '@');
	if (! domain) {
		*err = __NSW_UNAVAIL;
		trace1(TR_getkeys_nisplus, 1);
		return (0);
	}
	domain++;

	if ((strlen(netname)+PKTABLE_LEN+strlen(domain)+32) >
		(size_t) NIS_MAXNAMELEN) {
		*err = __NSW_UNAVAIL;
		trace1(TR_getkeys_nisplus, 1);
		return (0);
	}

	/*
	 * Cred table has following format for DES entries:
	 * cname   auth_type auth_name public  private
	 * ----------------------------------------------------------
	 * nisname	DES	netname	pubkey	prikey
	 */
	sprintf(buf, "[auth_name=\"%s\",auth_type=DES],%s.%s",
		netname, PKTABLE, domain);
	if (buf[strlen(buf)-1] != '.')
	strcat(buf, ".");

	/*
	 * If we want a publickey, then we need the connection to be
	 * authenticated; otherwise, since the secretkey is encrypted
	 * it can be gotten from an unauthenticated connection.
	 * Furthermore, the latter needs to be the case if
	 * the bootstrapping process is to work.  More specifically,
	 * when you're doing a keylogin, you need to get your secretkey
	 * and you *cannot* use an authenticated handle to get it because
	 * you don't have your secretkey yet.
	 */
	if (pkey)
		res = nis_list(buf,
				USE_DGRAM+NO_AUTHINFO+FOLLOW_LINKS+FOLLOW_PATH,
				NULL, NULL);
	else
		res = nis_list(buf,
				USE_DGRAM+NO_AUTHINFO+FOLLOW_LINKS+FOLLOW_PATH,
				NULL, NULL);
	switch (res->status) {
	case NIS_SUCCESS:
	case NIS_S_SUCCESS:
		break;
	case NIS_NOTFOUND:
	case NIS_PARTIAL:
	case NIS_NOSUCHNAME:
	case NIS_NOSUCHTABLE:
		nis_freeresult(res);
		*err = __NSW_NOTFOUND;
		trace1(TR_getkeys_nisplus, 1);
		return (0);
	case NIS_S_NOTFOUND:
	case NIS_TRYAGAIN:
		syslog(LOG_ERR, "getkeys: (nis+ key lookup): %s\n",
			nis_sperrno(res->status));
		nis_freeresult(res);
		*err = __NSW_TRYAGAIN;
		trace1(TR_getkeys_nisplus, 1);
		return (0);
	default:
		*err = __NSW_UNAVAIL;
		syslog(LOG_ERR,
			"getkeys: (nis+ key lookup): %s\n",
			nis_sperrno(res->status));
		nis_freeresult(res);
		trace1(TR_getkeys_nisplus, 1);
		return (0);
	}

	if (pkey) {
		len = ENTRY_LEN(res->objects.objects_val, 3);
		strncpy(pkey, ENTRY_VAL(res->objects.objects_val, 3), len);
		/*
		 * len has included the terminating null.
		 */

		/*
		 * XXX
		 * This is only for backward compatibility with the old cred
		 * table format.  The new one does not have a ':'.
		 */
		p = strchr(pkey, ':');
		if (p)
			*p = NUL;
	}
	r = 1; /* At least public key was found; always true at this point */

	if (skey && extract_secret(ENTRY_VAL(res->objects.objects_val, 4),
				skey, passwd))
		r |= 2;

	nis_freeresult(res);
	*err = __NSW_SUCCESS;
	trace1(TR_getkeys_nisplus, 1);
	return (r);
}

/*
 * getpublickey(netname, key)
 *
 * This is the actual exported interface for this function.
 */

int
getpublickey(netname, pkey)
	const char	*netname;
	char		*pkey;
{
	int	needfree = 1, res, err;
	struct __nsw_switchconfig *conf;
	struct __nsw_lookup *look;
	enum __nsw_parse_err perr;

	trace1(TR_getpublickey, 0);
	if (! netname || ! pkey) {
		trace1(TR_getpublickey, 1);
		return (0);
	}

	mutex_lock(&serialize_pkey);

	conf = __nsw_getconfig("publickey", &perr);
	if (! conf) {
		conf = &publickey_default;
		needfree = 0;
	}
	for (look = conf->lookups; look; look = look->next) {
		if (strcmp(look->service_name, "nisplus") == 0)
			res = getkeys_nisplus(&err, (char *) netname, pkey,
					(char *) NULL, (char *) NULL);
		else if (strcmp(look->service_name, "nis") == 0)
			res = getkeys_nis(&err, (char *) netname, pkey,
					(char *) NULL, (char *) NULL);
		else if (strcmp(look->service_name, "files") == 0)
			res = getkeys_files(&err, (char *) netname, pkey,
					(char *) NULL, (char *) NULL);
		else {
			syslog(LOG_INFO, "Unknown publickey nameservice '%s'",
						look->service_name);
			err = __NSW_UNAVAIL;
		}
		switch (look->actions[err]) {
		case __NSW_CONTINUE :
			continue;
		case __NSW_RETURN :
			if (needfree)
				__nsw_freeconfig(conf);
			mutex_unlock(&serialize_pkey);
			trace1(TR_getpublickey, 1);
			return ((res & 1) != 0);
		default :
			syslog(LOG_INFO, "Unknown action for nameservice %s",
					look->service_name);
		}
	}

	if (needfree)
		__nsw_freeconfig(conf);
	mutex_unlock(&serialize_pkey);
	trace1(TR_getpublickey, 1);
	return (0);
}

int
getsecretkey(netname, skey, passwd)
	const char	*netname;
	char		*skey;
	const char	*passwd;
{
	int	needfree = 1, res, err;
	struct __nsw_switchconfig *conf;
	struct __nsw_lookup *look;
	enum __nsw_parse_err perr;

	trace1(TR_getsecretkey, 0);
	if (! netname || !skey) {
		trace1(TR_getsecretkey, 1);
		return (0);
	}

	mutex_lock(&serialize_pkey);

	conf = __nsw_getconfig("publickey", &perr);

	if (! conf) {
		conf = &publickey_default;
		needfree = 0;
	}

	for (look = conf->lookups; look; look = look->next) {
		if (strcmp(look->service_name, "nisplus") == 0)
			res = getkeys_nisplus(&err, (char *) netname,
					(char *) NULL, skey, (char *) passwd);
		else if (strcmp(look->service_name, "nis") == 0)
			res = getkeys_nis(&err, (char *) netname,
					(char *) NULL, skey, (char *) passwd);
		else if (strcmp(look->service_name, "files") == 0)
			res = getkeys_files(&err, (char *) netname,
					(char *) NULL, skey, (char *) passwd);
		else {
			syslog(LOG_INFO, "Unknown publickey nameservice '%s'",
						look->service_name);
			err = __NSW_UNAVAIL;
		}
		switch (look->actions[err]) {
		case __NSW_CONTINUE :
			continue;
		case __NSW_RETURN :
			if (needfree)
				__nsw_freeconfig(conf);
			mutex_unlock(&serialize_pkey);
			trace1(TR_getsecretkey, 1);
			return ((res & 2) != 0);
		default :
			syslog(LOG_INFO, "Unknown action for nameservice %s",
					look->service_name);
		}
	}
	if (needfree)
		__nsw_freeconfig(conf);
	mutex_unlock(&serialize_pkey);
	trace1(TR_getsecretkey, 1);
	return (0);
}
