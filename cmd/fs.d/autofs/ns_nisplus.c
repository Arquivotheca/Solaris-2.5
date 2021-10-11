/*
 *	ns_nisplus.c
 *
 *	Copyright (c) 1988-1993 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)ns_nisplus.c	1.18	95/10/13 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <ctype.h>
#include <nsswitch.h>
#include <sys/param.h>
#include <sys/types.h>
#include <rpc/rpc.h>
#include <rpcsvc/nfs_prot.h>
#include <rpcsvc/nis.h>
#include "automount.h"

#define	KEY		0
#define	CONTENTS	1
#define	MASK_SIZE	1024

/*
 * The following macro is for making the values returned
 * from name services switch compatible. Earlier when a
 * name service returned 0 it meant it could not find
 * the requested stuff and a ret val of > 0 implied
 * success. This is opposite of what switch expects
 */
nis_result *__nis_list_localcb(nis_name, u_long,
		int (*)(nis_name, nis_object *, void *), void *);

extern void dirinit(char *, char *, char *, int);
extern void pr_msg(const char *, ...);

static int mastermap_callback(char *, nis_object *, void *);
static int directmap_callback(char *, nis_object *, void *);
static int nisplus_err(int);
static int nisplus_match(char *, char *, char *, char **, int *);

static char *nisplus_subdomain = NULL;

struct cbdata {
	char *ptr1;
	char *ptr2;
};

void
init_nisplus()
{
	nisplus_subdomain = "org_dir";
}

getmapent_nisplus(key, map, ml)
	char *key;
	char *map;
	struct mapline *ml;
{
	char *nis_line = NULL;
	char *lp;
	int nis_len, len;
	int nserr;

	nserr = nisplus_match(map, "key", key, &nis_line, &nis_len);
	if (nserr) {
		if (nserr == __NSW_NOTFOUND) {
			/* Try the default entry "*" */
			if ((nserr = nisplus_match(map, "key", "*", &nis_line,
						    &nis_len)))
				goto done;
		} else
			goto done;
	}

	/*
	 * at this point we are sure that nisplus_match
	 * succeeded so massage the entry by
	 * 1. ignoring # and beyond
	 * 2. trim the trailing whitespace
	 */
	if (lp = strchr(nis_line, '#'))
		*lp = '\0';
	len = strlen(nis_line);
	if (len == 0) {
		nserr = __NSW_NOTFOUND;
		goto done;
	}
	lp = &nis_line[len - 1];
	while (lp > nis_line && isspace(*lp))
		*lp-- = '\0';
	if (lp == nis_line) {
		nserr = __NSW_NOTFOUND;
		goto done;
	}
	(void) strcpy(ml->linebuf, nis_line);
	unquote(ml->linebuf, ml->lineqbuf);
	nserr = __NSW_SUCCESS;
done:
	if (nis_line)
		free((char *) nis_line);

	return (nserr);
}

getnetmask_nisplus(netname, mask)
	char *netname;
	char **mask;
{
	int outsize;

	return (nisplus_match("netmasks", "addr", netname,  mask, &outsize));
}

loadmaster_nisplus(mapname, defopts)
	char *mapname;
	char *defopts;
{
	char indexedname[NIS_MAXNAMELEN];
	nis_result *res = NULL;
	int err;

	if (nisplus_subdomain == NULL)
		return (__NSW_UNAVAIL);

	(void) strcpy(indexedname, mapname);
	if (strchr(mapname, '.') == NULL) {
		(void) strcat(indexedname, ".");
		(void) strcat(indexedname, nisplus_subdomain);
	}

	res = __nis_list_localcb(indexedname,
			EXPAND_NAME | FOLLOW_LINKS | FOLLOW_PATH |
			HARD_LOOKUP | ALL_RESULTS,
			mastermap_callback, (void *) defopts);
	if (res == NULL)
		return (__NSW_UNAVAIL);

	if (res->status != NIS_CBRESULTS) {
		if (verbose)
			syslog(LOG_ERR, "nisplus can't list map, %s: %s",
				mapname, nis_sperror(res->status,
						    "nis_list failed"));
		err = res->status;
		nis_freeresult(res);

		return (nisplus_err(err));
	}

	nis_freeresult(res);
	return (__NSW_SUCCESS);
}

loaddirect_nisplus(nsmap, localmap, opts)
	char *nsmap, *localmap, *opts;
{
	char indexedname[NIS_MAXNAMELEN];
	struct cbdata direct_cbdata;
	nis_result *res = NULL;
	int err;

	if (nisplus_subdomain == NULL)
		return (__NSW_UNAVAIL);

	(void) strcpy(indexedname, nsmap);
	if (strchr(nsmap, '.') == NULL) {
		(void) strcat(indexedname, ".");
		(void) strcat(indexedname, nisplus_subdomain);
	}
	direct_cbdata.ptr1 = opts;
	direct_cbdata.ptr2 = localmap;
	res = __nis_list_localcb(indexedname,
			EXPAND_NAME | FOLLOW_LINKS | FOLLOW_PATH |
			HARD_LOOKUP | ALL_RESULTS,
			directmap_callback, (void *)&direct_cbdata);
	if (res == NULL)
		return (__NSW_UNAVAIL);

	if (res->status != NIS_CBRESULTS) {
		if (verbose)
			syslog(LOG_ERR, "nisplus can't list map, %s: %s",
				nsmap, nis_sperror(res->status,
						"nis_list failed"));
		err = res->status;
		nis_freeresult(res);

		return (nisplus_err(err));
	}

	nis_freeresult(res);
	return (__NSW_SUCCESS);
}

static int
nisplus_err(err)
	int err;
{
	switch (err) {

	case NIS_SUCCESS:
	case NIS_S_SUCCESS:
		return (__NSW_SUCCESS);

	case NIS_NOTFOUND:
	case NIS_S_NOTFOUND:
		return (__NSW_NOTFOUND);

	case NIS_TRYAGAIN:
		return (__NSW_TRYAGAIN);

	default:
		return (__NSW_UNAVAIL);
	}
}


/*
 * The first param is not used, but it is reqd
 * because this function is called by nisplus
 * library functions
 */
/* ARGSUSED */
static int
mastermap_callback(tab, ent, udata)
	char *tab;
	nis_object *ent;
	void *udata;
{
	char *key, *contents, *pmap, *opts;
	char dir[256], map[256], qbuff[256];
	int  key_len, contents_len;
	register entry_col *ec = ent->EN_data.en_cols.en_cols_val;
	char *defopts = (char *)udata;

	key_len = ec[KEY].ec_value.ec_value_len;
	contents_len = ec[CONTENTS].ec_value.ec_value_len;

	if (key_len >= 256 || contents_len >= 256)
		return (0);
	if (key_len < 2 || contents_len < 2)
		return (0);

	key = ec[KEY].ec_value.ec_value_val;
	contents = ec[CONTENTS].ec_value.ec_value_val;
	while (isspace(*contents))
		contents++;
	if (contents == NULL)
		return (0);
	if (isspace(*key) || *key == '#')
		return (0);
	(void) strncpy(dir, key, key_len);
	dir[key_len] = '\0';
	if (macro_expand("", dir, qbuff, sizeof (dir))) {
		syslog(LOG_ERR,
		    "%s in nisplus master map: entry too long (max %d chars)",
		    dir, sizeof (dir) - 1);
		return (0);
	}
	(void) strncpy(map, contents, contents_len);
	map[contents_len] = '\0';
	if (macro_expand("", map, qbuff, sizeof (map))) {
		syslog(LOG_ERR,
		    "%s in nisplus master map: entry too long (max %d chars)",
		    map, sizeof (map) - 1);
		return (0);
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
	/*
	 * Check for no embedded blanks.
	 */
	if (strcspn(opts, " 	") == strlen(opts)) {
		dirinit(dir, pmap, opts, 0);
	} else
pr_msg("Warning: invalid entry for %s in nisplus map %s ignored.\n",
		    dir, ent->zo_name);
	return (0);
}

/*
 * The first param is not used, but it is reqd
 * because this function is called by nisplus
 * library functions
 */
/* ARGSUSED */
static int
directmap_callback(tab, ent, udata)
	char *tab;
	nis_object *ent;
	void *udata;
{
	char *key;
	char dir[256];
	int  key_len;
	struct cbdata *temp = (struct cbdata *)udata;
	char *opts = temp->ptr1;
	char *localmap = temp->ptr2;
	register entry_col *ec = ent->EN_data.en_cols.en_cols_val;

	key_len = ec[KEY].ec_value.ec_value_len;
	if (key_len >= 100 || key_len < 2)
		return (0);

	key = ec[KEY].ec_value.ec_value_val;
	if (isspace(*key) || *key == '#')
		return (0);
	(void) strncpy(dir, key, key_len);
	dir[key_len] = '\0';

	dirinit(dir, localmap, opts, 1);

	return (0);
}

static int
nisplus_match(map, colm_name, key, nis_line, nis_len)
	char *map, *colm_name, *key;
	char **nis_line;
	int  *nis_len;
{
	nis_result *res = NULL;
	int err;
	entry_col *ent;
	char indexedname[NIS_MAXNAMELEN];

	if (nisplus_subdomain == NULL)
		return (__NSW_UNAVAIL);

	if (*map != '[')
		(void) sprintf(indexedname, "[%s=%s],%s",
				colm_name, key, map);
	else
		(void) strcpy(indexedname, map);

	if (strchr(map, '.') == NULL) {
		(void) strcat(indexedname, ".");
		(void) strcat(indexedname, nisplus_subdomain);
	}

	res = nis_list(indexedname,
			USE_DGRAM | EXPAND_NAME | FOLLOW_LINKS |
			FOLLOW_PATH | ALL_RESULTS,
			NULL, NULL);
	if (res == NULL)
		return (__NSW_UNAVAIL);

	if (res->status != NIS_SUCCESS && res->status != NIS_S_SUCCESS) {
		if (verbose && res->status != NIS_NOTFOUND)
			syslog(LOG_ERR, "nisplus can't list map, %s: %s", map,
				nis_sperror(res->status, "nis_list failed"));
		err = res->status;
		nis_freeresult(res);

		return (nisplus_err(err));
	}

	ent = res->objects.objects_val->EN_data.en_cols.en_cols_val;

	if (ent == NULL ||
	    ent[KEY].ec_value.ec_value_val == NULL ||
	    strcmp(ent[KEY].ec_value.ec_value_val, key) != 0) {
		nis_freeresult(res);
		return (__NSW_NOTFOUND);
	}

	*nis_len = ent[CONTENTS].ec_value.ec_value_len +
		ent[KEY].ec_value.ec_value_len;
	/*
	 * so check for the length; it should be less than LINESZ
	 */
	if ((*nis_len + 2) > LINESZ) {
		syslog(LOG_ERR, "nisplus map %s, entry for %s"
			" is too long %d chars (max %d)",
		    map, key, (*nis_len + 2), LINESZ);
		nis_freeresult(res);
		return (__NSW_UNAVAIL);
	}
	*nis_line = (char *)malloc(*nis_len + 2);
	if (*nis_line == NULL) {
		syslog(LOG_ERR, "nisplus_match: malloc failed");
		nis_freeresult(res);
		return (__NSW_UNAVAIL);
	}

	(void) sprintf(*nis_line, "%s", ent[CONTENTS].ec_value.ec_value_val);
	nis_freeresult(res);

	return (__NSW_SUCCESS);
}
