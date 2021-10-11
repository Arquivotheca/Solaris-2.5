/*
 *	ns_generic.c
 *
 *	Copyright (c) 1988-1993 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)ns_generic.c	1.5	94/10/27 SMI"

#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <nsswitch.h>
#include <sys/param.h>
#include <rpc/rpc.h>
#include <rpcsvc/nfs_prot.h>
#include "automount.h"

/*
 * Each name service is represented by a ns_info structure.
 */
struct ns_info {
	char	*ns_name;		/* service name */
	void	(*ns_init)();		/* initialization routine */
	int	(*ns_getmapent)();	/* get map entry given key */
	int	(*ns_getnetmask)();	/* get netmask for net */
	int	(*ns_loadmaster)();	/* load master map */
	int	(*ns_loaddirect)();	/* load direct map */
};

extern void init_files();
extern int  getmapent_files();
extern int  getnetmask_files();
extern int  loadmaster_files();
extern int  loaddirect_files();

extern void init_nisplus();
extern int  getmapent_nisplus();
extern int  getnetmask_nisplus();
extern int  loadmaster_nisplus();
extern int  loaddirect_nisplus();

extern void init_nis();
extern int  getmapent_nis();
extern int  getnetmask_nis();
extern int  loadmaster_nis();
extern int  loaddirect_nis();

static struct ns_info ns_info[] = {

	"files",   init_files,  getmapent_files,
	getnetmask_files, loadmaster_files, loaddirect_files,

	"nisplus", init_nisplus, getmapent_nisplus,
	getnetmask_nisplus, loadmaster_nisplus, loaddirect_nisplus,

	"nis",	   init_nis,	getmapent_nis,
	getnetmask_nis,   loadmaster_nis,   loaddirect_nis,

	NULL, NULL, NULL, NULL, NULL, NULL
};

static struct ns_info *get_next_ns(struct __nsw_lookup **, int);

mutex_t ns_setup_lock;

void
ns_setup()
{
	struct ns_info *nsp;

	mutex_init(&ns_setup_lock, 0, NULL);

	for (nsp = ns_info; nsp->ns_name; nsp++)
		nsp->ns_init();
}

static struct ns_info *
get_next_ns(curr_ns, curr_nserr)
	struct __nsw_lookup **curr_ns;
	int curr_nserr;
{
	static struct __nsw_switchconfig *conf = NULL;
	enum __nsw_parse_err pserr;
	struct __nsw_lookup *lkp;
	struct ns_info *nsp;

	mutex_lock(&ns_setup_lock);
	if (conf == NULL) {
		conf = __nsw_getconfig("automount", &pserr);
		if (conf == NULL)
			return (NULL);
	}
	mutex_unlock(&ns_setup_lock);

	if (*curr_ns == NULL)
		/* first time */
		lkp = conf->lookups;
	else {
		lkp = *curr_ns;
		if (__NSW_ACTION(lkp, curr_nserr) == __NSW_RETURN)
			return (NULL);
		lkp = lkp->next;
	}

	for (; lkp; lkp = lkp->next) {
		for (nsp = ns_info; nsp->ns_name; nsp++) {
			if (strcmp(lkp->service_name, nsp->ns_name) == 0) {
				*curr_ns = lkp;
				return (nsp);
			}
		}
		/*
		 * Note: if we get here then we've found
		 * an unsupported name service.
		 */
	}

	return (NULL);
}

getmapent(key, mapname, ml)
	char *key, *mapname;
	struct mapline *ml;
{
	struct __nsw_lookup *curr_ns = NULL;
	int ns_err = __NSW_SUCCESS;
	struct ns_info *nsp;

	if (strcmp(mapname, "-hosts") == 0) {
		(void) strcpy(ml->linebuf, "-hosts");
		return (__NSW_SUCCESS);
	}

	if (*mapname == '/') 		/* must be a file */
		return (getmapent_files(key, mapname, ml));

	while ((nsp = get_next_ns(&curr_ns, ns_err)) != NULL) {
		ns_err = nsp->ns_getmapent(key, mapname, ml);
		if (ns_err == __NSW_SUCCESS)
			return (__NSW_SUCCESS);
	}

	return (__NSW_UNAVAIL);
}

/*
 * XXX This routine should be in libnsl
 * and be supported by the ns switch.
 */
getnetmask_byaddr(netname, mask)
	char *netname, **mask;
{
	struct __nsw_lookup *curr_ns = NULL;
	int ns_err = __NSW_SUCCESS;
	struct ns_info *nsp;

	while ((nsp = get_next_ns(&curr_ns, ns_err)) != NULL) {
		ns_err = nsp->ns_getnetmask(netname, mask);
		if (ns_err == __NSW_SUCCESS)
			return (__NSW_SUCCESS);
	}

	return (__NSW_UNAVAIL);
}

int
loadmaster_map(mapname, defopts)
	char *mapname, *defopts;
{
	struct __nsw_lookup *curr_ns = NULL;
	int ns_err = __NSW_SUCCESS;
	struct ns_info *nsp;

	if (*mapname == '/')		/* must be a file */
		return (loadmaster_files(mapname, defopts));

	while ((nsp = get_next_ns(&curr_ns, ns_err)) != NULL) {
		ns_err = nsp->ns_loadmaster(mapname, defopts);
		if (ns_err == __NSW_SUCCESS)
			return (__NSW_SUCCESS);
	}

	return (__NSW_UNAVAIL);
}

loaddirect_map(mapname, localmap, defopts)
	char *mapname, *localmap, *defopts;
{
	struct __nsw_lookup *curr_ns = NULL;
	int ns_err = __NSW_SUCCESS;
	struct ns_info *nsp;

	if (*mapname == '/')		/* must be a file */
		return (loaddirect_files(mapname, localmap, defopts));

	while ((nsp = get_next_ns(&curr_ns, ns_err)) != NULL) {
		ns_err = nsp->ns_loaddirect(mapname, localmap, defopts);
		if (ns_err == __NSW_SUCCESS)
			return (__NSW_SUCCESS);
	}

	return (__NSW_UNAVAIL);
}
