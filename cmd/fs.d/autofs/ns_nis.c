/*
 *	ns_nis.c
 *
 *	Copyright (c) 1988-1993 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)ns_nis.c	1.14	95/08/24 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <ctype.h>
#include <nsswitch.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/systeminfo.h>
#include <rpc/rpc.h>
#include <rpcsvc/nfs_prot.h>
#include <rpcsvc/ypclnt.h>
#include "automount.h"

#define	KEY		0
#define	CONTENTS	1
#define	MASK_SIZE	1024

extern void pr_msg(const char *, ...);
extern void dirinit(char *, char *, char *, int);
extern void unquote(char *, char *);

/*
 * Hidden rpc function
 */
extern int __nis_reset_state();

static int replace_undscr_by_dot(char *);
static int nis_err(int);

static char nis_mydomain[64];

void
init_nis()
{
	(void) sysinfo(SI_SRPC_DOMAIN, nis_mydomain, sizeof (nis_mydomain));
	(void) __nis_reset_state();	/* XXX temporary hack for csh bug */
}

getmapent_nis(key, map, ml)
	char *key, *map;
	struct mapline *ml;
{
	char *nisline = NULL;
	char *my_map = NULL;
	char *lp, *lq;
	int nislen, len;
	int nserr;

	nserr = yp_match(nis_mydomain, map, key, strlen(key),
						&nisline, &nislen);
	if (nserr == YPERR_MAP) {
		my_map = strdup(map);
		if (my_map == NULL) {
			syslog(LOG_ERR,
				"getmapent_nis: memory alloc failed: %m");
			return (__NSW_UNAVAIL);
		}
		if (replace_undscr_by_dot(my_map))
			nserr = yp_match(nis_mydomain, my_map, key,
					strlen(key), &nisline, &nislen);
	}

	if (nserr) {
		if (nserr == YPERR_KEY) {
			/*
			 * Try the default entry "*"
			 */
			if (my_map == NULL)
				nserr = yp_match(nis_mydomain, map, "*", 1,
						&nisline, &nislen);
			else
				nserr = yp_match(nis_mydomain, my_map, "*", 1,
						&nisline, &nislen);
		} else {
			if (verbose)
				syslog(LOG_ERR, "%s: %s",
					map, yperr_string(nserr));
			nserr = 1;
		}
	}
	if (my_map != NULL)
		free(my_map);

	nserr = nis_err(nserr);
	if (nserr)
		goto done;

	/*
	 * at this point we are sure that yp_match succeeded
	 * so massage the entry by
	 * 1. ignoring # and beyond
	 * 2. trim the trailing whitespace
	 */
	if (lp = strchr(nisline, '#'))
		*lp = '\0';
	len = strlen(nisline);
	if (len == 0) {
		nserr = __NSW_NOTFOUND;
		goto done;
	}
	lp = &nisline[len - 1];
	while (lp > nisline && isspace(*lp))
		*lp-- = '\0';
	if (lp == nisline) {
		nserr = __NSW_NOTFOUND;
		goto done;
	}
	(void) strcpy(ml->linebuf, nisline);
	lp = ml->linebuf;
	lq = ml->lineqbuf;
	unquote(lp, lq);
	/* now we have the correct line */

	nserr = __NSW_SUCCESS;
done:
	if (nisline)
		free((char *) nisline);
	return (nserr);

}


getnetmask_nis(netname, mask)
	char *netname;
	char **mask;
{
	int outsize, nserr;

	nserr = yp_match(nis_mydomain, "netmasks.byaddr",
			netname, strlen(netname), mask,
			&outsize);
	if (nserr == __NSW_SUCCESS)
		return (__NSW_SUCCESS);

	return (nis_err(nserr));
}

loadmaster_nis(mapname, defopts)
	char *mapname, *defopts;
{
	int first, err;
	char *key, *nkey, *val;
	int kl, nkl, vl;
	char dir[256], map[256], qbuff[256];
	char *pmap, *opts, *my_mapname;
	int count = 0;


	first = 1;
	key  = NULL; kl  = 0;
	nkey = NULL; nkl = 0;
	val  = NULL; vl  = 0;

	/*
	 * need a private copy of mapname, because we may change
	 * the underscores by dots. We however do not want the
	 * orignal to be changed, as we may want to use the
	 * original name in some other name service
	 */
	my_mapname = strdup(mapname);
	if (my_mapname == NULL) {
		syslog(LOG_ERR, "loadmaster_yp: memory alloc failed: %m");
		/* not the name svc's fault but ... */
		return (__NSW_UNAVAIL);
	}
	for (;;) {
		if (first) {
			first = 0;
			err = yp_first(nis_mydomain, my_mapname,
				&nkey, &nkl, &val, &vl);

			if ((err == YPERR_MAP) &&
			    (replace_undscr_by_dot(my_mapname)))
				err = yp_first(nis_mydomain, my_mapname,
					&nkey, &nkl, &val, &vl);

			if ((err == YPERR_DOMAIN) || (err == YPERR_YPBIND)) {
				syslog(LOG_ERR,
					"can't read nis map %s: %s - retrying",
					my_mapname, yperr_string(err));
				while ((err == YPERR_DOMAIN) ||
					(err == YPERR_YPBIND)) {
					(void) sleep(20);
					err = yp_first(nis_mydomain, my_mapname,
						&nkey, &nkl, &val, &vl);
				}
				syslog(LOG_ERR,
					"nis map %s: read OK.", my_mapname);
			}
		} else {
			err = yp_next(nis_mydomain, my_mapname, key, kl,
				&nkey, &nkl, &val, &vl);
		}
		if (err) {
			if (err != YPERR_NOMORE && err != YPERR_MAP)
				if (verbose)
					syslog(LOG_ERR, "%s: %s",
					my_mapname, yperr_string(err));
			break;
		}
		if (key)
			free(key);
		key = nkey;
		kl = nkl;


		if (kl >= 256 || vl >= 256)
			break;
		if (kl < 2 || vl < 1)
			break;
		if (isspace(*key) || *key == '#')
			break;
		(void) strncpy(dir, key, kl);
		dir[kl] = '\0';
		if (macro_expand("", dir, qbuff, sizeof (dir))) {
			syslog(LOG_ERR,
			    "%s in NIS map %s: entry too long (max %d chars)",
			    dir, my_mapname, sizeof (dir) - 1);
			break;
		}
		(void) strncpy(map, val, vl);
		map[vl] = '\0';
		if (macro_expand(dir, map, qbuff, sizeof (map))) {
			syslog(LOG_ERR,
			    "%s in NIS map %s: entry too long (max %d chars)",
			    map, my_mapname, sizeof (map) - 1);
			break;
		}
		pmap = map;
		while (*pmap && isspace(*pmap))
			pmap++;		/* skip blanks in front of map */
		opts = pmap;
		while (*opts && !isspace(*opts))
			opts++;
		if (*opts) {
			*opts++ = '\0';
			while (*opts && isspace(*opts))
				opts++;
			if (*opts == '-')
				opts++;
			else
				opts = defopts;
		}
		free(val);

		/*
		 * Check for no embedded blanks.
		 */
		if (strcspn(opts, " 	") == strlen(opts)) {
			dirinit(dir, pmap, opts, 0);
			count++;
		} else {
pr_msg("Warning: invalid entry for %s in NIS map %s ignored.\n", dir, mapname);
		}

	}
	if (my_mapname)
		free(my_mapname);

	/*
	 * In the context of a master map, if no entry is
	 * found, it is like NOTFOUND
	 */
	if (count > 0 && err == YPERR_NOMORE)
		return (__NSW_SUCCESS);
	else {
		if (err)
			return (nis_err(err));
		else
			/*
			 * This case will happen if map is empty
			 *  or none of the entries is valid
			 */
			return (__NSW_NOTFOUND);
	}
}

loaddirect_nis(nismap, localmap, opts)
	char *nismap, *localmap, *opts;
{
	int first, err, count;
	char *key, *nkey, *val, *my_nismap;
	int kl, nkl, vl;
	char dir[100];

	first = 1;
	key  = NULL; kl  = 0;
	nkey = NULL; nkl = 0;
	val  = NULL; vl  = 0;
	count = 0;
	my_nismap = NULL;

	my_nismap = strdup(nismap);
	if (my_nismap == NULL) {
		syslog(LOG_ERR, "loadmaster_yp: memory alloc failed: %m");
		return (__NSW_UNAVAIL);
	}
	for (;;) {
		if (first) {
			first = 0;
			err = yp_first(nis_mydomain, my_nismap, &nkey, &nkl,
					&val, &vl);

			if ((err == YPERR_MAP) &&
			    (replace_undscr_by_dot(my_nismap)))
				err = yp_first(nis_mydomain, my_nismap,
						&nkey, &nkl, &val, &vl);

			if ((err == YPERR_DOMAIN) || (err == YPERR_YPBIND)) {
				syslog(LOG_ERR,
					"can't read nis map %s: %s - retrying",
					my_nismap, yperr_string(err));
				while ((err == YPERR_DOMAIN) ||
					(err == YPERR_YPBIND)) {
					(void) sleep(20);
					err = yp_first(nis_mydomain, my_nismap,
						&nkey, &nkl, &val, &vl);
				}
				syslog(LOG_ERR,
					"nis map %s: read OK.", my_nismap);
			}
		} else {
			err = yp_next(nis_mydomain, my_nismap, key, kl,
					&nkey, &nkl, &val, &vl);
		}
		if (err) {
			if (err != YPERR_NOMORE && err != YPERR_MAP)
				syslog(LOG_ERR, "%s: %s",
					my_nismap, yperr_string(err));
			break;
		}
		if (key)
			free(key);
		key = nkey;
		kl = nkl;

		if (kl < 2 || kl >= 100)
			continue;
		if (isspace(*key) || *key == '#')
			continue;
		(void) strncpy(dir, key, kl);
		dir[kl] = '\0';

		dirinit(dir, localmap, opts, 1);
		count++;
		free(val);
	}

	if (my_nismap)
		free(my_nismap);

	if (count > 0 && err == YPERR_NOMORE)
			return (__NSW_SUCCESS);
	else
		return (nis_err(err));

}

static int
replace_undscr_by_dot(map)
	char *map;
{
	int ret_val = 0;

	while (*map) {
		if (*map == '_') {
			ret_val = 1;
			*map = '.';
		}
		map++;
	}
	return (ret_val);
}

static int
nis_err(err)
	int err;
{
	switch (err) {
	case 0:
		return (__NSW_SUCCESS);
	case YPERR_KEY:
		return (__NSW_NOTFOUND);
	case YPERR_MAP:
		return (__NSW_UNAVAIL);
	default:
		return (__NSW_UNAVAIL);
	}
}
