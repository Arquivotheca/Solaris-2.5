/*
 *	nis_misc_proc.c
 *
 *	Copyright (c) 1988-1994 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_misc_proc.c	1.3	94/03/31 SMI"

/*
 * This contains miscellaneous functions moved from commands to the library.
 */

#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>
#include <rpc/auth.h>
#include <rpc/auth_sys.h>
#include <rpc/auth_des.h>
#include <rpc/key_prot.h>
#ifndef TDRPC
#include <netdir.h>
#include <netconfig.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>

extern void *calloc();

/*
 * Returns the NIS principal name of the person making the request
 * XXX This is set up to use Secure RPC only at the moment, it should
 * be possible for any authentication scheme to be incorporated if it
 * has a "full name" that we can return as the principal name.
 */
static const nis_name nobody = "nobody";

static NIS_HASH_TABLE credtbl;
struct creditem {
	NIS_HASH_ITEM	item;
	char	pname[1024];
};

static void
add_cred_item(netname, pname)
char	*netname;
char	*pname;
{
	struct creditem *foo = NULL, *old = NULL;

	old = (struct creditem *)nis_find_item(netname, &credtbl);
	if (old != NULL)
		return;

	foo = (struct creditem *)calloc(1, sizeof (struct creditem));
	foo->item.name = strdup(netname);
	(void) strcpy(foo->pname, pname);
	nis_insert_item((NIS_HASH_ITEM *)foo, &credtbl);
}

static bool_t
find_cred_item(netname, pname)
char	*netname;
char	*pname;
{
	struct creditem	*old = NULL;

	old = (struct creditem *) nis_find_item(netname, &credtbl);
	if (old == NULL)
		return (FALSE);
	(void) strcpy(pname, old->pname);
	return (TRUE);
}

void
__nis_auth2princ(name, flavor, auth, verbose)
char		*name;
int		flavor;
caddr_t 	auth;
int		verbose;
{
	struct authsys_parms	*au;
	struct authdes_cred	*ad;
	char			*rmtdomain;
	char			srch[2048]; /* search criteria */
	nis_result		*res;

	(void) strcpy(name, nobody); /* default is "nobody" */
	if (flavor == AUTH_NONE) {
		if (verbose) {
			syslog(LOG_INFO,
		    "__nis_auth2princ: flavor = NONE: returning '%s'", nobody);
		}
		return;
	} else if (flavor == AUTH_SYS) { /* XXX ifdef this for 4.1 */
		au = (struct authsys_parms *)(auth);
		rmtdomain = nis_domain_of(au->aup_machname);
		if (au->aup_uid == 0) {
			(void) sprintf(name, "%s", au->aup_machname);
			if (! rmtdomain)
				(void) strcat(name, nis_local_directory());
			if (name[strlen(name) - 1] != '.')
				(void) strcat(name, ".");
			if (verbose) {
				syslog(LOG_INFO,
		    "__nis_auth2princ: flavor = SYS: returning '%s'", name);
			}
			return;
		}
		(void) sprintf(srch,
		    "[auth_name=\"%ld\", auth_type=LOCAL], cred.org_dir.%s",
				au->aup_uid, (*rmtdomain == '.') ?
				(char *) nis_local_directory() : rmtdomain);
		if (srch[strlen(srch) - 1] != '.') {
			(void) strcat(srch, ".");
		}
	} else if (flavor == AUTH_DES) {
		ad = (struct authdes_cred *)(auth);
		if (find_cred_item(ad->adc_fullname.name, name)) {
			if (verbose)
				syslog(LOG_INFO,
		"__nis_auth2princ: flavor = DES: returning from cache '%s'",
					name);
			return;
		}
		rmtdomain = strchr(ad->adc_fullname.name, '@');
		if (rmtdomain) {
			rmtdomain++;
			(void) sprintf(srch,
			    "[auth_name=%s, auth_type=DES], cred.org_dir.%s",
					ad->adc_fullname.name, rmtdomain);
			if (srch[strlen(srch) - 1] != '.') {
				(void) strcat(srch, ".");
			}
		} else {
			if (verbose) {
				syslog(LOG_INFO,
			    "__nis_auth2princ: flavor = DES: returning '%s'",
					nobody);
			}
			return;
		}
	} else {
		syslog(LOG_WARNING,
		"__nis_auth2princ: flavor = %d(unknown): returning '%s'",
							flavor, nobody);
		return;
	}
	if (verbose)
		syslog(LOG_INFO,
			"__nis_auth2princ: calling list with name '%s'",
							name);
	res = nis_list(srch, NO_AUTHINFO+USE_DGRAM+FOLLOW_LINKS, NULL, NULL);
	if (res->status != NIS_SUCCESS) {
		if (verbose)
			syslog(LOG_INFO,
				"__nis_auth2princ: error doing nis_list: %s",
						nis_sperrno(res->status));
	} else {
		(void) strncpy(name, ENTRY_VAL(res->objects.objects_val, 0),
				1024);
		if (flavor == AUTH_DES)
			add_cred_item(ad->adc_fullname.name, name);
	}

	nis_freeresult(res);
	if (verbose)
		syslog(LOG_INFO,
		"__nis_auth2princ: flavor = %s: returning : '%s'",
			flavor == AUTH_SYS? "SYS" : "DES", name);
}

/*
 * This function returns true if the given principal has the right to
 * do the requested function on the given object. It could be a define
 * if that would save time. At the moment it is a function.
 * NOTE: It recursively calls NIS by doing the lookup on the group if
 * the conditional gets that far.
 *
 * N.B. If the principal passed is 'null' then we're recursing and don't
 * need to check it. (we always let ourselves look at the data)
 */
bool_t
__nis_ck_perms(right, mask, obj, pr, level)
unsigned long	right;	/* The Access right we desire 		*/
unsigned long	mask;	/* The mask for World/Group/Owner 	*/
nis_object	*obj;	/* A pointer to the object		*/
nis_name	pr;	/* Principal making the request		*/
int		level;	/* security level server is running at	*/
{
	if ((level == 0) || (*pr == 0))
		return (TRUE);

	return (NOBODY(mask, right) ||
		(WORLD(mask, right) && (strcmp(pr, "nobody") != 0)) ||
		(OWNER(mask, right) &&
			(nis_dir_cmp(pr, obj->zo_owner) == SAME_NAME)) ||
		(GROUP(mask, right) &&
			(strlen(obj->zo_group) > (size_t)(1)) &&
			__do_ismember(pr, obj->zo_group, nis_lookup)));
}

/*
 * is 'host' the master server for "org_dir."'domain' ?
 */
bool_t
__nis_ismaster(host, domain)
char	*host;
char	*domain;
{
	nis_server	**srvs;		/* servers that serve 'domain' */
	nis_server	*master_srv;
	char		buf[NIS_MAXNAMELEN];
	bool_t		res;

	if (domain == NULL) {
		syslog(LOG_ERR, "__nis_ismaster(): null domain");
		return (FALSE);
	}
	/* strlen(".org_dir") + null + "." = 10 */
	if ((strlen(domain) + 10) > (size_t) NIS_MAXNAMELEN)
		return (FALSE);

	(void) sprintf(buf, "org_dir.%s", domain);
	if (buf[strlen(buf) - 1] != '.')
		(void) strcat(buf, ".");

	srvs = nis_getservlist(buf);
	if (srvs == NULL) {
		/* can't find any of the servers that serve this domain */
		/* something is very wrong ! */
		syslog(LOG_ERR,
			"cannot get a list of servers that serve '%s'",
			buf);
		return (FALSE);
	}
	master_srv = srvs[0];	/* the first one is always the master */

	if (strcasecmp(host, master_srv->name) == 0)
		res = TRUE;
	else
		res = FALSE;

	/* done with server list */
	(void) nis_freeservlist(srvs);

	return (res);
}

/*
 * check if this principal is the owner of the table
 * or is a member of the table group owner
 */
bool_t
__nis_isadmin(princ, table, domain)
char	*princ;
char	*table;
char	*domain;
{
	char	buf[NIS_MAXNAMELEN];
	struct	nis_result	*res;
	struct	nis_object	*obj;
	bool_t	ans = FALSE;

	if ((princ == NULL || *princ == '\0') ||
		(table == NULL || *table == '\0') ||
		(domain == NULL || *domain == '\0'))
		return (FALSE);

	/* strlen(".org_dir.") + null + "." = 11 */
	if ((strlen(table) + strlen(domain) + 11) >
			(size_t) NIS_MAXNAMELEN) {
		syslog(LOG_ERR, "__nis_isadmin: buffer too small");
		return (FALSE);
	}
	(void) sprintf(buf, "%s.org_dir.%s", table, domain);
	if (buf[strlen(buf) - 1] != '.')
		(void) strcat(buf, ".");

	/* get the table object */
	res = nis_lookup(buf, FOLLOW_LINKS);
	if (res->status != NIS_SUCCESS) {
		syslog(LOG_ERR,
			"__nis_isadmin: could not lookup '%s' table",
			table);
		return (FALSE);
	}
	obj = NIS_RES_OBJECT(res);
	if (obj->zo_data.zo_type != TABLE_OBJ) {
		syslog(LOG_ERR, "__nis_isadmin: not a table object");
		return (FALSE);
	}
	if ((strcasecmp(obj->zo_owner, princ) == 0) ||
		((obj->zo_group) && (*(obj->zo_group)) &&
			nis_ismember(princ, obj->zo_group)))
		ans = TRUE;

	(void) nis_freeresult(res);
	return (ans);
}

#define	NIS_NOHOSTNAME	48
#define	NIS_NOHOSTADDR	49


/*
 * This function constructs a server description of the host
 * given (or the local host) and returns it as a nis_server
 * structure.
 * Returns NULL on error, and sets the errcode.
 */
#ifdef TDRPC
nis_server *
__nis_host2nis_server(host, addpubkey, errcode)
char	*host;		/* host name */
bool_t	addpubkey;	/* add pub key info */
int	*errcode;	/* error code */
{
	static char		hostname[NIS_MAXNAMELEN + 1];
	struct hostent		*he;
	struct in_addr		*ha;
	static endpoint		addr[2];
	static char		uaddr[32];
	u_long			a1, a2, a3, a4, p1, p2;
	struct sockaddr_in	addr;
	static nis_server	hostinfo;
	char			*dir;
	char			hostnetname[MAXNETNAMELEN + 1];
	char			pubkey[HEXKEYBYTES + 1];

	if (! host) {
		if (gethostname(hostname, NIS_MAXNAMELEN) != 0) {
			*errcode = NIS_NOHOSTNAME;
			return (NULL);
		}
	} else
		(void) strcpy(hostname, host);

	he = gethostbyname(hostname);
	if (! he) {
		*errcode = NIS_NOHOSTADDR;
		return (NULL);
	}
	ha = (struct in_addr *)(he->h_addr_list[0]);
	addr.sin_addr = *ha;

	a1 = (addr.sin_addr.s_addr >> 24) & 0xff;
	a2 = (addr.sin_addr.s_addr >> 16) & 0xff;
	a3 = (addr.sin_addr.s_addr >>  8) & 0xff;
	a4 = (addr.sin_addr.s_addr) & 0xff;
	p1 = 0;
	p2 = 111;
	(void) sprintf(uaddr, "%d.%d.%d.%d.%d.%d", a1, a2, a3, a4, p1, p2);
	addr[0].uaddr = &uaddr[0];
	addr[0].family = "INET";
	addr[0].proto = "TCP";
	addr[1].uaddr = &uaddr[0];
	addr[1].family = "INET";
	addr[1].proto = "UDP";

	/* fully qualify the host given */
	if (strchr(hostname, '.') == 0) {
		(void) strcat(hostname, ".");
		dir = nis_local_directory();
		if (*dir != '.')
			(void) strcat(hostname, dir);
	}
	hostinfo.name = strdup(hostname);
	hostinfo.ep.ep_len = 2;
	hostinfo.ep.ep_val = addr;

	if (addpubkey &&
		host2netname(hostnetname, hostname, NULL) &&
		getpublickey(hostnetname, pubkey)) {

		hostinfo.key_type = NIS_PK_DH;
		hostinfo.pkey.n_len = strlen(pubkey) + 1;
		hostinfo.pkey.n_bytes = strdup(pubkey);
	} else {
		hostinfo.key_type = NIS_PK_NONE;
		hostinfo.pkey.n_len = 0;
		hostinfo.pkey.n_bytes = NULL;
	}
	return (&hostinfo);
}
#else
nis_server *
__nis_host2nis_server(host, addpubkey, errcode)
char	*host;		/* host name */
bool_t	addpubkey;	/* add pub key info */
int	*errcode;	/* error code */
{
	static char		hostname[NIS_MAXNAMELEN + 1];
	static endpoint		addr[20];
	static nis_server	hostinfo;
	int			num_ep = 0, i;
	struct netconfig	*nc;
	void			*nch;
	struct nd_hostserv	hs;
	struct nd_addrlist	*addrs;
	char			*dir;
	char			hostnetname[MAXNETNAMELEN + 1];
	char			pubkey[HEXKEYBYTES + 1];
	extern int		gethostname(), getpublickey();

	if (! host) {
		if (gethostname(hostname, NIS_MAXNAMELEN) != 0) {
			*errcode = NIS_NOHOSTNAME;
			return (NULL);
		}
	} else
		(void) strcpy(hostname, host);
	hs.h_host = hostname;
	hs.h_serv = "rpcbind";
	nch = setnetconfig();
	while (nc = getnetconfig(nch)) {
		if (! netdir_getbyname(nc, &hs, &addrs)) {
			for (i = 0; i < addrs->n_cnt; i++, num_ep++) {
				addr[num_ep].uaddr =
				taddr2uaddr(nc, &(addrs->n_addrs[i]));
				addr[num_ep].family =
					strdup(nc->nc_protofmly);
				addr[num_ep].proto =
					strdup(nc->nc_proto);
			}
			netdir_free((char *)addrs, ND_ADDRLIST);
		}
	}
	endnetconfig(nch);

	if (! num_ep) {
		*errcode = NIS_NOHOSTADDR;
		return (NULL);
	}
	/* fully qualify the host given */
	if (strchr(hostname, '.') == 0) {
		(void) strcat(hostname, ".");
		dir = nis_local_directory();
		if (*dir != '.')
			(void) strcat(hostname, dir);
	}
	hostinfo.name = strdup(hostname);
	hostinfo.ep.ep_len = num_ep;
	hostinfo.ep.ep_val = &addr[0];

	if (addpubkey &&
		host2netname(hostnetname, hostname, NULL) &&
		getpublickey(hostnetname, pubkey)) {

		hostinfo.key_type = NIS_PK_DH;
		hostinfo.pkey.n_len = strlen(pubkey) + 1;
		hostinfo.pkey.n_bytes = strdup(pubkey);
	} else {
		hostinfo.key_type = NIS_PK_NONE;
		hostinfo.pkey.n_len = 0;
		hostinfo.pkey.n_bytes = NULL;
	}
	return (&hostinfo);
}
#endif
