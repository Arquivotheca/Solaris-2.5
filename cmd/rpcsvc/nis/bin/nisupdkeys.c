/*
 *	nisupdkeys.c
 *
 *	Copyright (c) 1988-1995 Sun Microsystems, Inc.
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nisupdkeys.c	1.23	95/02/17 SMI"

/*
 * nisupdkeys.c
 *
 * This function will read the list of servers from a directory object,
 * update them with the proper public keys and write the directory object
 * back.
 *
 */

#include <stdio.h>
#include <netdb.h>
#ifndef TDRPC
#include <netdir.h>
#include <netconfig.h>
#else
#include <sys/socket.h>
#endif
#include <netinet/in.h>
#include <rpc/key_prot.h>
#include <rpcsvc/nis.h>
#include <string.h>
#include <stdlib.h>

extern int optind;
extern char *optarg;

extern char *inet_ntoa();
extern int gethostname(char *name, int namelen);

/*
 * This piece of code copied from nismkdir.c file
 *
 * This function constructs a local server description of the current
 * server and returns it as a nis_server structure.
 */
#ifdef TDRPC
static nis_server *
host_2_nis_server(host)
	char	*host;
{
	static char		uaddr[32], hname[1024];
	static endpoint  	myaddr[2];
	static nis_server 	myself;
	struct sockaddr_in	addr;
	struct in_addr		*ha;
	u_long			a1, a2, a3, a4, p1, p2;
	char			*dir, hostnetname[1024];
	struct hostent		*he;
	char			pknetname[MAXNETNAMELEN];
	char			pkey[HEXKEYBYTES+1];

	if (host) {
		he = gethostbyname(host);
		if (! he) {
			fprintf(stderr,
			"Couldn't locate address information for %s.\n", host);
			return (0);
		}
		ha = (struct in_addr *)(he->h_addr_list[0]);
		addr.sin_addr = *ha;
		strcpy(hostnetname, he->h_name);
		/*
		 * append local domain if name is unqualified
		 */
		if (strchr(hostnetname, '.') == 0) {
			strcat(hostnetname, ".");
			dir = nis_local_directory();
			if (*dir != '.')
				strcat(hostnetname, dir);
		} else if (hostnetname[strlen(hostnetname)-1] != '.') {
			fprintf(stderr,
				"Please use a fully qualified host name.\n");
			return (0);
		}
	} else
		get_myaddress(&addr);

	a1 = (addr.sin_addr.s_addr >> 24) & 0xff;
	a2 = (addr.sin_addr.s_addr >> 16) & 0xff;
	a3 = (addr.sin_addr.s_addr >>  8) & 0xff;
	a4 = (addr.sin_addr.s_addr) & 0xff;
	p1 = 0;
	p2 = 111;
	sprintf(uaddr, "%d.%d.%d.%d.%d.%d", a1, a2, a3, a4, p1, p2);
	myaddr[0].uaddr = &uaddr[0];
	myaddr[0].family = "INET";
	myaddr[0].proto = "TCP";
	myaddr[1].uaddr = &uaddr[0];
	myaddr[1].family = "INET";
	myaddr[1].proto = "UDP";

	myself.name = (host) ? strdup(hostnetname) : strdup(nis_local_host());
	myself.ep.ep_len = 2;
	myself.ep.ep_val = myaddr;

	if (host2netname(pknetname, myself.name, NULL) &&
	    getpublickey(pknetname, pkey)) {
		myself.key_type = NIS_PK_DH;
		myself.pkey.n_len = strlen(pkey)+1;
		myself.pkey.n_bytes = (char *)strdup(pkey);
	} else {
		myself.key_type = NIS_PK_NONE;
		myself.pkey.n_bytes = NULL;
		myself.pkey.n_len = 0;
	}

	return (&myself);
}

#else

static nis_server *
host_2_nis_server(host)
	char	*host;
{
	endpoint		myaddr[32], *tmpaddr;
	nis_server		*myself;
	char			hname[256];
	int			num_ep = 0, i;
	struct netconfig	*nc;
	void			*nch;
	struct nd_hostserv	hs;
	struct nd_addrlist	*addrs;
	char			hostnetname[NIS_MAXPATH];
	struct hostent		*he;
	char			pknetname[MAXNETNAMELEN];
	char			pkey[HEXKEYBYTES+1];

	if (host)
		hs.h_host = host;
	else {
		(void) gethostname(hname, 256);
		hs.h_host = hname;
	}
	hs.h_serv = "rpcbind";
	if (!(nch = setnetconfig()))
		return (0);

	while (nc = getnetconfig(nch)) {
		if (! netdir_getbyname(nc, &hs, &addrs)) {
			for (i = 0; i < addrs->n_cnt && num_ep < 32;
				i++, num_ep++) {
				myaddr[num_ep].uaddr =
					taddr2uaddr(nc, &(addrs->n_addrs[i]));
				myaddr[num_ep].family =
					    strdup(nc->nc_protofmly);
				myaddr[num_ep].proto =
					    strdup(nc->nc_proto);
				if (!myaddr[num_ep].uaddr) {
					endnetconfig(nch);
					return (0);
				}
			}
			netdir_free((char *)addrs, ND_ADDRLIST);
		}
	}
	endnetconfig(nch);

	if (host) {
/*
 * bug 1183848
 * fully qualify the name here, as gethostbyname() cannot be relied upon
 * to do so.  We assume that if the name we were passed had any "."'s
 * that it was intended to be fully qualified (and add a final "." if
 * needed).  Otherwise, it's in the local domain, so we append
 * nis_local_directory().  There is no unambiguous way to fully qualify
 * a name such as "sofa.ssi", given the variety of possible returns from
 * gethostbyname(), so we do not accept such names.
 */
		strcpy(hostnetname, host);
		if (strchr(hostnetname, '.') == 0) {
			char *localDir = nis_local_directory();
			if (*localDir != '.')
				strcat(hostnetname, ".");
			strcat(hostnetname, localDir);
		}
		if (hostnetname[strlen(hostnetname)-1] != '.')
			strcat(hostnetname, ".");

		he = gethostbyname(hostnetname);
		if (! he) {
			fprintf(stderr,
			    "Couldn't locate address information for '%s'.  ",
			    hostnetname);
			fprintf(stderr,
			    "Please use local or fully qualified host name.\n");
			return (0);
		}
	}

	if ((myself = (nis_server *)malloc(sizeof (nis_server))) == NULL) {
		fprintf(stderr, "Could not allocate memory\n");
		return (0);
	}
	myself->name = (host) ? strdup(hostnetname) : strdup(nis_local_host());
	myself->ep.ep_len = num_ep;
	if ((tmpaddr = (endpoint *)malloc(sizeof (endpoint) * num_ep))
		== NULL) {
		free(myself->name);
		free(myself);
		fprintf(stderr, "Could not allocate memory\n");
		return (0);
	}
	for (i = 0; i < num_ep; i++)
		tmpaddr[i] = myaddr[i];
	myself->ep.ep_val = tmpaddr;

	if (host2netname(pknetname, myself->name, NULL) &&
	    getpublickey(pknetname, pkey)) {
		myself->key_type = NIS_PK_DH;
		myself->pkey.n_len = strlen(pkey)+1;
		myself->pkey.n_bytes = (char *)strdup(pkey);
	} else {
		myself->key_type = NIS_PK_NONE;
		myself->pkey.n_bytes = NULL;
		myself->pkey.n_len = 0;
	}

	return (myself);
}
#endif

static
int
match_host(char *host, char *target)
{
	int len = strlen(host);

	if (strncasecmp(host, target, len) == 0 &&
	    (target[len] == '.' || target[len] == '\0'))
		return (1);

	return (0);
}


void
usage(cmd)
	char	*cmd;
{
	fprintf(stderr, "usage: %s [-C | -a] [-H host] [directory]\n", cmd);
	fprintf(stderr, "usage: %s -s [-C | -a] [-H host]\n", cmd);
	exit(1);
}

int
clearkeydata(h, ns, srvs)
	char		*h;	/* host to act on 	*/
	int		ns;	/* number of servers	*/
	nis_server	*srvs;	/* array of servers	*/
{
	int		i, is_modified;

	for (i = 0, is_modified = 0; i < ns; i++) {
		if (h && !match_host(h, srvs[i].name))
			continue;
		if (srvs[i].key_type != NIS_PK_NONE) {
			printf("\tClearing server %s's key\n", srvs[i].name);
			srvs[i].key_type = NIS_PK_NONE;
			srvs[i].pkey.n_bytes = 0;
			srvs[i].pkey.n_len = 0;
			is_modified++;
		} else {
			printf("\tNo keys exist for the server %s\n",
				srvs[i].name);
		}
	}
	return (is_modified);
}

int
updkeydata(h, ns, srvs)
	char		*h;	/* host to act on 	*/
	int		ns;	/* number of servers	*/
	nis_server	*srvs;	/* array of servers	*/
{
	int		i, is_modified;
	char		netname[MAXNETNAMELEN];
	char		pkey[HEXKEYBYTES+1];

	for (i = 0, is_modified = 0; i < ns; i++) {
		if (h && !match_host(h, srvs[i].name))
			continue;
		if (! host2netname(netname, srvs[i].name, NULL)) {
			fprintf(stderr, "\tERROR: No netname for %s\n",
				srvs[i].name);
			continue;
		}
#ifdef DEBUG
		printf("\tFetching Public key for server %s ...\n",
			srvs[i].name);
		printf("\tnetname = '%s'\n", netname);
#endif

		/*
		 * XXX: should get the new publickey from the master so that
		 * publickey it gets is not the same stale one.  Currently
		 * no interface to do this.  So for this operation to succeed
		 * the new publickeys should have propagated to all replicas.
		 */
		if (! getpublickey(netname, pkey)) {
			if (srvs[i].key_type == NIS_PK_DH) {
				srvs[i].key_type = NIS_PK_NONE;
				srvs[i].pkey.n_bytes = NULL;
				srvs[i].pkey.n_len = 0;
				is_modified++;
			} else if (srvs[i].key_type != NIS_PK_NONE) {
				fprintf(stderr, "\tERROR: Unknown key type!\n");
			}
			continue;
		} else {
			if ((srvs[i].key_type == NIS_PK_NONE) ||
			    ((srvs[i].key_type == NIS_PK_DH) &&
				(strcmp(srvs[i].pkey.n_bytes, pkey) != 0))) {
				if (srvs[i].key_type == NIS_PK_NONE)
					printf(
				"\tInstalling %s's public key in this object\n",
								srvs[i].name);
				else
					printf(
				"\tUpdating %s's public key in this object\n",
								srvs[i].name);
				printf("\tPublic key : %s\n", pkey);
				srvs[i].pkey.n_bytes = (char *)strdup(pkey);
				srvs[i].pkey.n_len = strlen(pkey)+1;
				srvs[i].key_type = NIS_PK_DH;
				is_modified++;
			} else
				printf("\tServer %s's public key unchanged.\n",
							srvs[i].name);
		}
	}
	return (is_modified);
}

/*
 * updaddrdata()
 *
 * For each server in the list, update its address information to be
 * current. If h is non-null only update information for that host.
 */
int
updaddrdata(h, ns, srvs)
	char		*h;	/* host to act on 	*/
	int		ns;	/* number of servers	*/
	nis_server	*srvs;	/* array of servers	*/
{
	register int	i, j, k;
	endpoint	*eps;	/* endpoints	*/
	int		nep;	/* num of eps	*/

	struct netconfig	*nc;	/* netconfig structure	*/
	void			*nch;	/* netconfig structure handle	*/
	struct nd_hostserv	hs;	/* netconfig database hostserv */
	struct nd_addrlist	*addrs; /* netconfig database addr list	*/

	/* XXX only update TCP/IP addresses at present */
	for (i = 0; i < ns; i++) {
		if (h && !match_host(h, srvs[i].name))
			continue;
		eps = srvs[i].ep.ep_val;
		nep = srvs[i].ep.ep_len;

		for (j = 0; j < nep; j++) {
		    free(eps[j].uaddr);
		    free(eps[j].family);
		    free(eps[j].proto);
		}

		/* setup params for netdir_getbyname() */
		hs.h_host = srvs[i].name;
		hs.h_serv = "rpcbind";

		/* count how many server entries we need */
		j = 0, nch = setnetconfig();
		while (nc = getnetconfig(nch)) {
			if (!netdir_getbyname(nc, &hs, &addrs)) {
				j += addrs->n_cnt;
				netdir_free((char *)addrs, ND_ADDRLIST);
			}
		}
		endnetconfig(nch);

		/* got server count and allocate space */
		srvs[i].ep.ep_len = nep = j;
		if (!(srvs[i].ep.ep_val = eps =
			(endpoint*)malloc(nep*sizeof (struct endpoint)))) {
				return (0);
		}

		/* fill in new server address info */
		j = 0, nch = setnetconfig();

		/* keep going if we still have more interfaces */
		while (nc = getnetconfig(nch)) {
		    if (!netdir_getbyname(nc, &hs, &addrs)) {
			for (k = 0; k < addrs->n_cnt; k++) {
			    eps[j].uaddr  =
				taddr2uaddr(nc, &(addrs->n_addrs[k]));
			    eps[j].family = strdup(nc->nc_protofmly);
			    eps[j].proto  = strdup(nc->nc_proto);

			    /* if any of these returned NULL, bail */
			    if (!(eps[j].uaddr && eps[j].family &&
				eps[j].proto)) {
				    netdir_free((char *)addrs, ND_ADDRLIST);
				    endnetconfig(nch);
				    return (0);
			    }
			    j++;
			}
			netdir_free((char *)addrs, ND_ADDRLIST);
		    }
		}
		endnetconfig(nch);	/* free(3C)'s NC data structs	*/
	}
	return (1);
}

#define	UPD_KEYS	0
#define	CLR_KEYS	1
#define	UPD_ADDR	2

main(argc, argv)
	int	argc;
	char	*argv[];
{
	char		dname[NIS_MAXNAMELEN];
	char		*server = NULL;
	nis_server	*srvlist;
	char		*dirlist[NIS_MAXREPLICAS], **curdir;
	int		ns, is_modified;
	nis_object	*obj;
	nis_result	*res, *mres;
	int		c;
	int		op = UPD_KEYS;
	int		i = 0;
	bool_t		hostupdate = FALSE;
	char		hostname[1024];

	while ((c = getopt(argc, argv, "CsaH:")) != -1) {
		switch (c) {
			case 'C' :
				op = CLR_KEYS;
				break;
			case 'a' :
				op = UPD_ADDR;
				break;
			case 'H' :
				server = optarg;
				break;
			case 's' :
				hostupdate = TRUE;
				break;
			case '?' :
			default :
				fprintf(stderr, "Unrecognized option.\n");
				usage(argv[0]);
				break;
		}
	}

	if (server)
		strcpy(hostname, server);
	else
		(void) gethostname(hostname, 256);

	if (hostupdate == TRUE) {
		/*
		 * get the list of directories served by this server and
		 * update all those directory objects.
		 */
		nis_server	*nisserver;
		nis_tag		tags, *tagres;
		nis_error	status;
		char		*t, *dirname, tmpbuf[1024];

		if (argc > optind) {
			fprintf(stderr,
				"No directories allowed with -s option\n");
			usage(argv[0]);
		}
		if ((nisserver = host_2_nis_server(hostname)) == NULL)
			exit(1);

		/* Get a list of directories served by this server */
		tags.tag_type = TAG_DIRLIST;
		tags.tag_val = "";
		status = nis_stats(nisserver, &tags, 1, &tagres);
		if (status != NIS_SUCCESS) {
			fprintf(stderr,
		    "nisupdkeys: Error talking to host %s, error was %s\n",
				hostname, nis_sperrno(status));
			exit(1);
		}
		if ((strcmp(tagres->tag_val, "<Unknown Statistic>") == 0) ||
			(strcasecmp(tagres->tag_val, "<error>") == 0) ||
			(strcmp(tagres->tag_val, " ") == 0)) {
			fprintf(stderr,
	"Attributes for the server %s cannot be updated by \"nisupdkeys -s\"\n",
				hostname);
			fprintf(stderr,
		"Instead, use the following for all directories served by %s\n",
				hostname);
			fprintf(stderr, "\t%s [-a|-C] -H %s dir_name ... \n",
				argv[0], hostname);
			exit(1);
		}

		strcpy(tmpbuf, tagres->tag_val);
		dirname = tmpbuf;
		while (t = strchr(dirname, ' ')) {
			*t++ = NULL;
			dirlist[i++] = dirname;
			dirname = t;
		}
		dirlist[i++] = dirname;
	} else {
		while ((argc - optind) > 0)
			dirlist[i++] = argv[optind++];
		if (i == 0) {
			dirlist[0] = nis_local_directory();
			i++;
		}
	}
	dirlist[i] = NULL;

	res = NULL;
	for (curdir = dirlist; *curdir; curdir++) {
		is_modified = 0;

		/* if res != NULL its been used before */
		if (res)
			nis_freeresult(res);

		printf("Updating directory object %s ...\n", *curdir);
		res = nis_lookup(*curdir, MASTER_ONLY+EXPAND_NAME);
		if (res->status != NIS_SUCCESS) {
			fprintf(stderr,
				"\tERROR: Unable to retrieve object.\n");
			nis_perror(res->status, *curdir);
			continue;
		}
		obj = res->objects.objects_val;
		sprintf(dname, "%s.%s", obj->zo_name, obj->zo_domain);
		if (__type_of(obj) != DIRECTORY_OBJ) {
			fprintf(stderr, "\tERROR: %s is not a directory.\n",
				dname);
			continue;
		}
		ns = obj->DI_data.do_servers.do_servers_len;
		srvlist = obj->DI_data.do_servers.do_servers_val;
		/* if a specific host has been specified */
		if (server) {
			for (i = 0; i < ns; ++i) {
				if (match_host(server, srvlist[i].name))
					break;
			}
			if (i == ns) {
				fprintf(stderr,
			"\tERROR: Host %s does not serve directory %s\n",
					    server, *curdir);
				fprintf(stderr,
				"\tDirectory %s is not being modified\n",
					 *curdir);
				continue;
			}
		}
		switch (op) {
			case CLR_KEYS :
				is_modified = clearkeydata(server, ns, srvlist);
				break;
			case UPD_KEYS :
				is_modified = updkeydata(server, ns, srvlist);
				break;
			case UPD_ADDR :
				is_modified = updaddrdata(server, ns, srvlist);
				break;
			default:
				/* should not have happened */
				exit(1);
		}
		if (is_modified) {
			mres = nis_modify(dname, obj);
			if (mres->status != NIS_SUCCESS) {
				fprintf(stderr,
			"\tERROR: Unable to modify directory object %s\n",
					*curdir);
				nis_perror(mres->status, dname);
			}
			nis_freeresult(mres);
		}
	}
	if (res)
		nis_freeresult(res);
	return (0);
}
