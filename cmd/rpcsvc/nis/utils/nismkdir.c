#pragma ident	"@(#)nismkdir.c	1.25	95/07/21 SMI"

/*
 * nismkdir.c
 *
 * nis+ dir create utility
 *
 * Copyright (c) 1988-1992 Sun Microsystems Inc
 * All Rights Reserved.
 */

#include <stdio.h>
#include <string.h>
#include <rpc/rpc.h>
#include <rpcsvc/nis.h>
#include <netdb.h>
#ifndef TDRPC
#include <netdir.h>
#include <netconfig.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <rpc/key_prot.h>

#define	MAX_REPLICA 64
char *defstr = 0;

extern int 	optind;
extern char	*optarg;

extern nis_object nis_default_obj;


/*
 * get_server()
 *
 * This function constructs a local server description of the current
 * server and returns it as a nis_server structure. This is then added
 * to the list of servers who serve this directory.
 */
#ifdef TDRPC
nis_server *
get_server(host)
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
			exit(1);
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
				"Please use fully qualified host name.\n");
			exit(1);
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
nis_server *
get_server(host)
	char	*host;
{
	static endpoint  	myaddr[512];
	static nis_server 	myself;
	char			hname[256];
	int			num_ep = 0, i;
	char			*uaddr;
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
		gethostname(hname, 256);
		hs.h_host = hname;
	}
	hs.h_serv = "rpcbind";
	nch = setnetconfig();
	while (nc = getnetconfig(nch)) {
		if (! netdir_getbyname(nc, &hs, &addrs)) {
			for (i = 0; i < addrs->n_cnt; i++, num_ep++) {
				myaddr[num_ep].uaddr =
					taddr2uaddr(nc, &(addrs->n_addrs[i]));
				myaddr[num_ep].family =
					    strdup(nc->nc_protofmly);
				myaddr[num_ep].proto =
					    strdup(nc->nc_proto);
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
		if (strchr(hostnetname, '.') == NULL) {
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
			exit(1);
		}
	}

	myself.name = (host) ? strdup(hostnetname) : strdup(nis_local_host());
	myself.ep.ep_len = num_ep;
	myself.ep.ep_val = &myaddr[0];

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
#endif


make_directory(char *name)
{
	nis_result *res, *ares;
	nis_object *obj;
	char *p, lname[NIS_MAXNAMELEN], *dname;
	nis_error s;

	/*
	 * Break name into leaf and domain components.
	 */
	if ((p = nis_leaf_of(name)) == 0) {
		nis_perror(NIS_BADNAME, name);
		exit(1);
	}
	strcpy(lname, p);
	dname = nis_domain_of(name);

	/*
	 * Get the parent directory object.
	 */
	res = nis_lookup(dname, MASTER_ONLY);
	if (res->status != NIS_SUCCESS) {
		nis_perror(res->status, dname);
		exit(1);
	}

	if (!nis_defaults_init(defstr))
		exit(1);

	/*
	 * Turn the parent directory object into the
	 * sub-directory object.  If we cared about memory
	 * leaks, we would save pointers to the fields that
	 * are being overwritten, and restore them and free
	 * the parent object when we are done.
	 */
	obj = &(NIS_RES_OBJECT(res)[0]);
	if (obj->zo_data.zo_type != DIRECTORY_OBJ) {
		fprintf(stderr, "%s: not a directory\n", dname);
		exit(1);
	}
	obj->zo_owner = nis_default_obj.zo_owner;
	obj->zo_group = nis_default_obj.zo_group;
	obj->zo_access = nis_default_obj.zo_access;
	obj->zo_ttl = nis_default_obj.zo_ttl;
	obj->DI_data.do_name = name;

	/*
	 * Make the directory and add it to the namespace.
	 */
	ares = nis_add(name, obj);
	if (ares->status != NIS_SUCCESS) {
		nis_perror(ares->status, "can't add directory");
		exit(1);
	} else {
		s = nis_mkdir(name,
			&(obj->DI_data.do_servers.do_servers_val[0]));
		if (s != NIS_SUCCESS) {
			(void) nis_remove(name, 0);
			nis_perror(s, "can't make directory");
			exit(1);
		}
	}
	nis_freeresult(ares);
	/*
	 * do not free res because it contains pointers to structures
	 * it did not allocate.
	 */
}

make_directory_master(char *name, char *host)
{
	nis_result *res, *ares, *mres;
	nis_object *obj, dobj;
	nis_error s;
	nis_server *serv, sserv, *newserv;
	int nserv, i;

	/*
	 * Does the directory already exist?
	 */
	res = nis_lookup(name, MASTER_ONLY);
	if (res->status == NIS_SUCCESS) {
		obj = &(NIS_RES_OBJECT(res)[0]);
		if (obj->zo_data.zo_type != DIRECTORY_OBJ) {
			fprintf(stderr, "%s: not a directory\n", name);
			exit(1);
		}
		nserv = obj->DI_data.do_servers.do_servers_len;

		newserv = get_server(host);

		if (nis_dir_cmp(
			obj->DI_data.do_servers.do_servers_val[0].name,
				newserv->name) == SAME_NAME) {
			fprintf(stderr,
				"%s is already master for %s!\n",
				host, name);
			exit(1);
		}

		/*
		 * Add master to list of servers and demote current
		 * master to the role of a slave.
		 */
		for (i = 1; i < nserv; i++)
			if (nis_dir_cmp(
			obj->DI_data.do_servers.do_servers_val[i].name,
					newserv->name) == SAME_NAME)
				break;
		if (i < nserv) {
			sserv =
			    obj->DI_data.do_servers.do_servers_val[i];
			obj->DI_data.do_servers.do_servers_val[i] =
			    obj->DI_data.do_servers.do_servers_val[0];
			obj->DI_data.do_servers.do_servers_val[0] =
								sserv;
		} else {
			if ((serv = (nis_server*)malloc(
				(nserv + 1)*sizeof (nis_server))) ==
								0) {
				nis_perror(NIS_NOMEMORY,
						"can't add master");
				exit(1);
			}

			for (i = 0; i < nserv; i++)
				serv[i+1] =
			    obj->DI_data.do_servers.do_servers_val[i];
			serv[0] = *newserv;
			obj->DI_data.do_servers.do_servers_len++;
			obj->DI_data.do_servers.do_servers_val = serv;
		}

		mres = nis_modify(name, obj);
		if (mres->status != NIS_SUCCESS) {
			nis_perror(mres->status, "can't add master");
			exit(1);
		}
		nis_freeresult(mres);
		/*
		 * do not free res because it contains pointers to
		 * structures it did not allocate.
		 */
	} else {
		if (!nis_defaults_init(defstr))
			exit(1);

		/*
		 * Construct the directory object.
		 */
		dobj = nis_default_obj;
		dobj.zo_data.zo_type = DIRECTORY_OBJ;
		dobj.DI_data.do_name = name;
		dobj.DI_data.do_type = NIS;
		dobj.DI_data.do_ttl = nis_default_obj.zo_ttl;
		dobj.DI_data.do_servers.do_servers_len = 1;
		dobj.DI_data.do_servers.do_servers_val =
				get_server(host);
		dobj.DI_data.do_armask.do_armask_len = 0;
		dobj.DI_data.do_armask.do_armask_val = 0;

		/*
		 * Make the directory and add it to the namespace.
		 */
		ares = nis_add(name, &dobj);
		if (ares->status != NIS_SUCCESS) {
			nis_perror(ares->status, "can't add directory");
			exit(1);
		} else {
			s = nis_mkdir(name,
				&(dobj.DI_data.do_servers.do_servers_val[0]));
			if (s != NIS_SUCCESS) {
				(void) nis_remove(name, 0);
				nis_perror(s, "can't make directory");
				exit(1);
			}
		}
		nis_freeresult(ares);
	}
}

make_directory_replica(char *name, char *host)
{
	nis_result *res, *mres;
	nis_object *obj;
	nis_server *serv, *newserv;
	int nserv, i;

	/*
	 * Get the directory object.
	 */
	res = nis_lookup(name, MASTER_ONLY);
	if (res->status != NIS_SUCCESS) {
		nis_perror(res->status, name);
		exit(1);
	}
	obj = &(NIS_RES_OBJECT(res)[0]);
	if (obj->zo_data.zo_type != DIRECTORY_OBJ) {
		fprintf(stderr, "%s: not a directory\n", name);
		exit(1);
	}
	nserv = obj->DI_data.do_servers.do_servers_len;

	newserv = get_server(host);

	for (i = 0; i < nserv; i++)
		if (nis_dir_cmp(
			obj->DI_data.do_servers.do_servers_val[i].name,
				newserv->name) == SAME_NAME)
			break;
	if (i < nserv) {
		fprintf(stderr, "%s already serves %s!\n",
			host, name);
		exit(1);
	}

	/*
	 * Add slave to the list of servers.
	 */
	if ((serv = (nis_server*)malloc(
			(nserv + 1)*sizeof (nis_server))) == 0) {
		nis_perror(NIS_NOMEMORY, "can't add slave");
		exit(1);
	}
	for (i = 0; i < nserv; i++)
		serv[i] = obj->DI_data.do_servers.do_servers_val[i];
	serv[i] = *newserv;
	obj->DI_data.do_servers.do_servers_len++;
	obj->DI_data.do_servers.do_servers_val = serv;

	mres = nis_modify(name, obj);
	if (mres->status != NIS_SUCCESS) {
		nis_perror(mres->status, "can't add slave");
		exit(1);
	}
	nis_freeresult(mres);
	/*
	 * do not free res because it contains pointers to structures
	 * it did not allocate.
	 */
}

void
usage()
{
	fprintf(stderr,
	"usage: nismkdir [-D defaults] [-m hostname] [-s hostname] dirname\n");
	exit(1);
}

main(argc, argv)
	int argc;
	char *argv[];
{
	int c;
	int i;
	char *host;
	char *name;
	int update_master = 0;
	char *replicas[MAX_REPLICA];
	int nreplicas = 0;

	while ((c = getopt(argc, argv, "D:m:s:")) != -1) {
		switch (c) {
		case 'D':
			defstr = optarg;
			break;
		case 'm':
			if (update_master) {
				fprintf(stderr,
					"only one master can be specified\n");
				exit(1);
			}
			update_master = 1;
			host = optarg;
			break;
		case 's':
			if (nreplicas >= MAX_REPLICA) {
				fprintf(stderr, "too many replicas\n");
				exit(1);
			}
			replicas[nreplicas++] = optarg;
			break;
		default:
			usage();
		}
	}

	if (argc - optind != 1)
		usage();

	name = argv[optind];
	if (name[strlen(name)-1] != '.') {
		fprintf(stderr, "dirname must be fully qualified.\n");
		exit(1);
	}

	/*
	 *  If no master or replica flag, just create directory.
	 *  The directory will have the same replication as its parent
	 *  directory.
	 */
	if (! update_master && nreplicas == 0)
		make_directory(name);
	else {
		if (update_master)
			make_directory_master(name, host);

		for (i = 0; i < nreplicas; i++)
			make_directory_replica(name, replicas[i]);
	}

	exit(0);
}
