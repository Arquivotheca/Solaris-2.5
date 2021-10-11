#ident	"@(#)devlinks.c	1.15	95/01/30 SMI"
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/dditypes.h>
#include <locale.h>

#include <sac.h>
#include <libdevinfo.h>

#include "devlinkhdr.h"

const char *table_file = TABLE_FILENAME;

const char *devdir_pfx;

struct devdef ddef;

struct miscdev misc[MAX_MISC];

const struct miscdev **extra;

const struct miscdev userdef = {0}; /* Dummy entry for extra array */

#define OBPLSFILE "/etc/.obp_devices"
FILE *obpfp;

boolean_t debugging = B_FALSE;	/* -d debugging flag */

/*
 * addmnode -- add link node to internal structure.
 *
 * This adds information about a possible link to an internal structure.
 * This routine can be called from two places:
 *	get_dev_entries, which finds what links already exist.  In this case
 *		the links are marked as 'dangling'.
 *	get_devfs_entries, which finds what links *should* exist.  Often the
 *		corresponding link will already exist, and will be marked as
 *		'valid'.
 */
void
addmnode(const char *devnm,
	 const char *devfsnm,	/* Devfs name */
	 boolean_t in_dev)	/* This entry found in /dev */
{
    int m_no = -1;

#ifdef DEBUG
    printf("Adding %s link from %s to %s\n", (in_dev ? "DEV" : "DEVFS"),
	   devnm, devfsnm);
#endif


    for (m_no = 0; m_no < MAX_MISC; m_no++) {
	if (misc[m_no].unit == 0 || strcmp(misc[m_no].unit, devnm) == 0) {
	    break;
	}
    }

    if (m_no >= MAX_MISC) {
	wmessage("devlinks: Too many %s devices - device %s%s ignored\n",
		 ddef.dfs.type, ddef.devdir, devnm);
	return;
    }

    /*
     * Don`t process more than ONE devfs entry; clones, with
     * multiple hardware, will have more than one identical entries
     * bugid 1104276
     */

    if (misc[m_no].state == LN_MISSING && in_dev == B_FALSE)  {
	    return;
    }

    if (misc[m_no].unit == NULL)
	misc[m_no].unit = s_strdup(devnm);

    if (misc[m_no].devfsnm) {
	if (strcmp(misc[m_no].devfsnm, devfsnm) != 0) {
	    /*
	     * Assume devfs entry (done second) has precedence
	     */
	    free(misc[m_no].devfsnm);
	    misc[m_no].devfsnm = s_strdup(devfsnm);
	    misc[m_no].state = LN_INVALID;
	}
	else
	    misc[m_no].state = LN_VALID;
    }
    else {
	misc[m_no].devfsnm = s_strdup(devfsnm);
	misc[m_no].state = (in_dev) ? LN_DANGLING : LN_MISSING;
    }

    misc[m_no].extra = -1;	/* Mark as not yet in 'extra' structure */

    return;
}

/*
 * Find existing dev entries matching a particular name-spec
 */

/*
 * check to see if a particular name matches a format.
 * If the format contains a '\N', the output parameter counter is set
 * to reflect the value of the first \N matched
 */
static boolean_t
fmt_matchdevnm(const char *fmt, const char *name, int * const counter)
{
    const char *cp = name;
    char sbuf[64];
    char *sp;
    int minval;
    int num;
    boolean_t counter_seen = B_FALSE;

    for (;*fmt != NULL; fmt++) {
	switch (*fmt) {
	case '\\':
	    fmt++;
	    switch (*fmt) {
	    case '\0':
		return(B_FALSE);
	    case '\\':
		if (*cp++ != '\\')
		    return(B_FALSE);
		break;
	    case 'N':
		fmt++;
		if (!isdigit(*fmt))
		    return(B_FALSE);
		minval = *fmt - '0';

		sp = sbuf;
		while (isdigit(*cp))
		    *sp++ = *cp++;
		if (sp == sbuf)
		    return(B_FALSE);

		*sp = '\0';
		if (!counter_seen) {
		    num = atol(sbuf);
		    if (num < minval)
			return(B_FALSE);

		    *counter = num;
		    counter_seen = B_TRUE;
		}
		break;

	    default:
		return(B_FALSE);
	    }
	    fmt++;
	    break;

	default:
	    if (*cp++ != *fmt++)
		return(B_FALSE);
	}
    }

    if (*cp == '\0')
        return(B_TRUE);
    else
	return(B_FALSE);
}

void
get_dev_entries()
{
    CACHE_DIR *dp;
    const CACHE_ENT *entp;
    int i;

    /*
     * Search a directory for special names
     */
    dp = cache_opendir(ddef.devdir);

    if (dp == NULL) {
	fmessage(1, "devlinks: Could not open directory '%s': %s\n",
		 ddef.devdir, strerror(errno));
	return;
    }

    while ((entp = cache_readdir(dp)) != NULL) {
	/*
	 * Ignore entries that are not symbolic links -- those with their
	 * linkto fields null
	 */
	if (entp->linkto == NULL)
	    continue;

	if (ddef.devspec[0] != '\0')
	    if (!fmt_matchdevnm(ddef.devspec, entp->name, &i))
		continue;

	/*
	 * Add new entry to device node list
	 */
	addmnode(entp->name, entp->linkto, B_TRUE);
    }

    cache_closedir(dp);
    return;
}

static int
devfsmatch(const char *string)
{
    register int i;
    register const char *sp;

    if (ddef.dfs.name && strcmp(ddef.dfs.name, getdevname(string)) != 0) {
	return 1;
    }

    for (i=0; i<MAX_DEVFS_COMPMATCH && ddef.dfs.addr[i].pat; i++) {
	if ((sp =  getdevaddr(string, ddef.dfs.addr[i].no)) == NULL ||
	    strcmp(ddef.dfs.addr[i].pat, sp) != 0)
	    return 1;
    }


    for (i=0; i<MAX_DEVFS_COMPMATCH && ddef.dfs.minor[i].pat; i++) {
	if ((sp =  getdevminor(string, ddef.dfs.minor[i].no)) == NULL ||
	    strcmp(ddef.dfs.minor[i].pat, sp) != 0)
	    return 1;
    }

    return(0);
}

/*
 * do special fomatting.
 * special format sequences start with '\'.
 *
 * Current sequences allowed are:
 *	\D	- 'device type' field of devfs name (before the '@')
 *	\An	- Address (after the @, before the :).  If comma-separeated,
 *		  the values of the subfields are in \A1, \A2; the whole is in
 *		  \A0.
 *	\Mn	- minor (after the :).  If comma-separeated, the values of the
 *		  subfields are in \M1, \M2; the whole is in \M0.
 */

static char *
fmt_derived_devnm(
	  const char *fmt,
	  const char *devnm)
{
    static char fmtbuf[PATH_MAX+1];
    char *cp = fmtbuf;
    const char *subp;

    for (;*fmt != NULL; fmt++) {
	switch (*fmt) {
	case '\\':
	    fmt++;
	    switch (*fmt) {
	    case '\0':
		return(NULL);
	    case 'D':
		strcpy(cp, getdevname(devnm));
		cp += strlen(cp);
		break;
	    case 'A':
		fmt++;
		subp = getdevaddr(devnm, *fmt-'0');
		if (subp == NULL) {
		    return NULL;
		}

		strcpy(cp, subp);
		cp += strlen(subp);
		break;

	    case 'M':
		fmt++;
		subp = getdevminor(devnm, *fmt-'0');
		if (subp == NULL) {
		    return NULL;
		}

		strcpy(cp, subp);
		cp += strlen(subp);
		break;

	    default:
		return(NULL);
	    }
	    break;

	default:
	    *cp++ = *fmt;
	}
    }
    *cp = '\0';

    return(fmtbuf);
}
/*
 * do special counter fomatting.
 * special format sequences start with '\'.
 *
 * Current sequences allowed are:
 *	\Nn	- a sequential numeric value, unique, starting at 'n'.
 *		  This is special, in that it implies that currently-existing
 *		  nodes are searched to find an already-existing value for N.
 *		  If none is found, the counter is incremented.
 */

static char *
fmt_counter_devnm(
		  const char *fmt, /* link-name format */
		  const char *dir, /* directory in which links exist */
		  const char *link_val,	/* value in link */
		  int * const counter /* Counter value */
		  )
{
    static char fmtbuf[PATH_MAX+1];
    static char after_counter[64];
    char *cp = fmtbuf;
    const char *rp;

    int count_start;
    boolean_t counter_seen = B_FALSE;

    for (;*fmt != NULL; fmt++) {
	switch (*fmt) {
	case '\\':
	    fmt++;
	    switch (*fmt) {
	    case 'N':
		if (counter_seen)
		    return(NULL);
		fmt++;
		if (!isdigit(*fmt)) {
		    return NULL;
		}
		count_start = *fmt - '0';

		if (count_start < *counter)
		    count_start = *counter;

		*cp = '\0';
		counter_seen = B_TRUE;
		cp = after_counter;

		break;

	    default:
		return(NULL);
	    }
	    break;

	default:
	    *cp++ = *fmt;
	}
    }
    *cp = '\0';

    /* Now handle counter-search */

    if (counter_seen && *counter < 0) {
	CACHE_DIR *dp;
	const CACHE_ENT *entp;
	int num = 0;
	int num_length;
	int ucnt = 0;
	static int num_used[MAX_MISC];
	int i, j, k;

	/* Open link directory */
	dp = cache_opendir(dir);

	if (dp == NULL) {
	    fmessage(1, "devlinks: Could not open directory '%s': %s\n",
		     dir, strerror(errno));
	    return(NULL);
	}

	while ((entp = cache_readdir(dp)) != NULL) {
	    /*
	     * Ignore entries that are not symbolic links -- those with their
	     * linkto fields null
	     */
	    if (entp->linkto == NULL)
		continue;

	    if (strncmp(entp->name, fmtbuf, strlen(fmtbuf)) != 0)
		continue;

	    if (strlen(after_counter) != 0) {
		if (strcmp(entp->name + strlen(entp->name) - strlen(after_counter),
			   after_counter) != 0)
		    continue;
	    }

	    num = 0;
	    num_length = (strlen(entp->name) - strlen(after_counter)
			   - strlen(fmtbuf));

	    if (num_length <= 0)
		continue;

	    rp = entp->name + strlen(fmtbuf);

	    for (i = 0; i < num_length; i++) {
		if (!isdigit(rp[i]))
		    break;
		num = num * 10 + (rp[i] - '0');
	    }

	    if (rp < (entp->name + strlen(entp->name) - strlen(after_counter)))
		continue;	/* Failed the 'isdigit' test */


	    /* Have valid name; check for match */

	    if (strcmp(link_val, entp->linkto) != 0) {
		num_used[ucnt++] = num;
		continue;
	    }

	    /* Found it! */

	    strcat(fmtbuf, entp->name + strlen(fmtbuf));
	    *counter = num;

	    cache_closedir(dp);
	    return(fmtbuf);
	}

	cache_closedir(dp);

	/*
	 * Not found, so use lowest unused greater than counter
	 */
	j = count_start;
	do {
	    k = j;
	    for (i=0; i< ucnt; i++) {
		if (num_used[i] == k)
		    j++;
	    }
	} while (k != j);

	*counter =j;

	sprintf(fmtbuf+strlen(fmtbuf), "%d%s", j, after_counter);
    }
    else if (counter_seen) {
	sprintf(fmtbuf+strlen(fmtbuf), "%d%s", *counter, after_counter);
    }

    return(fmtbuf);
}


#define N_FSCACHE 20

struct devfscache dfsch[N_FSCACHE];

int devcounter;

/*
 * devfs_entry -- routine called when a matching devfs entry is found
 *
 * This routine is called by devfs_find() when a matching devfs entry is found.
 * It is passwd the name of the devfs entry.
 */
void
devfs_entry(const char *devfsnm)
{
    const char *cp, *tp;
    char dev_unit[PATH_MAX];
    char devfs_fnm[PATH_MAX];

    /*
     * Check entry against devfspfx to check for match
     */
    cp = strrchr(devfsnm, '/');

    if (cp == NULL)
	cp = devfsnm;
    else
	cp++;			/* Skip the '/' */

    if (devfsmatch(cp) != 0)
	return;

    sprintf(devfs_fnm, "%s%s", ddef.pathto_devfs, devfsnm);

    switch (ddef.devpat_type) {
    case PAT_ABSOLUTE:
	strcpy(dev_unit, ddef.devspec);
	break;
    case PAT_DERIVED:
	if ((tp = fmt_derived_devnm(ddef.devspec, devfsnm)) == NULL) {
	    wmessage("devlinks: Pattern '%s' cannot be used with device '%s'\n",
		     ddef.devspec, devfsnm);
	    return;
	}
	strcpy(dev_unit, tp);
	break;
    case PAT_COUNTER:
	strcpy(dev_unit, fmt_counter_devnm(ddef.devspec, ddef.devdir,
					   devfs_fnm, &devcounter));
	devcounter++;
	break;
    default:
	assert(B_FALSE);
    }

    addmnode(dev_unit, devfs_fnm, B_FALSE);
}

/*ARGSUSED*/
void
devfs_cache_entry(const char *devfsnm, const char *devtype,
    const dev_info_t *dip, struct ddi_minor_data *dmip,
    struct ddi_minor_data *dmap)
{
    struct devfscache *chp, *curchp;
    int i;


    if (debugging) {
	fprintf(stderr, "'%s' entry: %s\n", devtype, devfsnm); /**/
    }

    if (obpfp != NULL) {
	    fprintf(obpfp, "%s	%s\n", devtype, devfsnm);
    }


    chp = s_malloc(sizeof(*chp));

    chp->name = s_strdup(devfsnm);
    chp->next = NULL;

    for (i=0; i < N_FSCACHE; i++) {
	if (dfsch[i].name == NULL)
	    break;
	if (strcmp(dfsch[i].name, devtype) == 0) {
	    for (curchp = &dfsch[i]; curchp->next != NULL; curchp = curchp->next);

	    curchp->next = chp;
	    curchp = chp;
	    return;
	}
    }

    if (i >= N_FSCACHE)
	fmessage(4, "devlinks: Too many different device types -- internal error\n");

    dfsch[i].name = s_strdup(devtype);
    dfsch[i].next = chp;
}

void
get_devfs_entries(void)
{
    static int cacheread = B_FALSE;

    int i;
    struct devfscache *chp;

    devcounter = -1;

    if (!cacheread) {
	devfs_find(NULL, devfs_cache_entry, 0);
	cacheread = B_TRUE;
    }
	 

    for (i=0; i < N_FSCACHE; i++) {
	if (dfsch[i].name == NULL)
	    break;
	if (strcmp(dfsch[i].name, ddef.dfs.type) == 0) {
	    for (chp = dfsch[i].next; chp != NULL; chp = chp->next)
		devfs_entry(chp->name);

	    return;
	}
    }
}

void
remove_links(void)
{
    int i;
    char devnbuf[PATH_MAX+1];

    for (i=0; i<MAX_MISC; i++) {
	if (misc[i].devfsnm == NULL)
	    break;

	/*
	 * You would expect invalid as well as dangling links to be removed
	 * here; with the directory-caching, it is more efficient to do
	 * the unlink and relink in a single operation, so LN_INVALID
	 * nodes are not explicitly removed, but simply overwritten.
	 */
	if (misc[i].state != LN_DANGLING)
	    continue;

	sprintf(devnbuf, "%s%s", ddef.devdir, misc[i].unit);

	if (debugging) {
	    fprintf(stderr, "deleting symlink %s\n", devnbuf);
	    continue;
	}

	if (cache_unlink(ddef.devdir, devnbuf) != 0) {
	    wmessage("devlinks: Could not remove symlink '%s': %s\n",
		     devnbuf, strerror(errno));
	}
    }
}

void
add_links(void)
{
    int i;

    for (i=0; i<MAX_MISC; i++) {
	if (misc[i].devfsnm == NULL)
	    break;

	if (misc[i].state != LN_MISSING)
	    continue;

	if (debugging) {
	    fprintf(stderr, "adding link %s%s ==> %s\n", ddef.devdir,
		    misc[i].unit, misc[i].devfsnm);
	    continue;
	}

	if (cache_symlink(misc[i].devfsnm, ddef.devdir, misc[i].unit) != 0) {
	    wmessage("devlinks: Could not create symlink '%s': %s\n",
		     misc[i].unit, strerror(errno));
	}

    }
	
}

/*
 * Extra link stuff, for counted extra links
 */

static struct miscdev *
find_entry(const char *name)
{
    int i;

    for (i=0; i<MAX_MISC; i++) {
	if (misc[i].unit && strcmp(misc[i].unit, name) == 0)
	    return (&misc[i]);
    }

    return(NULL);
}

/*
 * get_extra_dev:
 * It is assumed that all ''extra' links are counted sequences of links
 * to normal links.  Therefore to find which links are in use, we need to
 * search for already-existing links.
 */
void
get_extra_dev(void)
{
    CACHE_DIR *dp;
    const CACHE_ENT *entp;
    struct miscdev *mp;
    int i;

    /*
     * Search a directory for special names
     */
    dp = cache_opendir(ddef.extra_linkdir);

    if (dp == NULL) {
	fmessage(1, "devlinks: Could not open directory '%s': %s\n", ddef.extra_linkdir,
		 strerror(errno));
	return;
    }

    while ((entp = cache_readdir(dp)) != NULL) {
	if (fmt_matchdevnm(entp->name, ddef.extra_linkspec, &i) == B_FALSE)
	    continue;

	/*
	 * Now find if link points to existing dev device.
	 * If it is dangling, ignore it;
	 * if it points to incorrect device, ignore it;
	 * If it points to valid device, set extra[i] pointer.
	 */
	if (strncmp(entp->linkto, ddef.extra_linkdir,
		    strlen(ddef.extra_linkdir)) == 0) {
	    /* XXX should check devnm here as well */
	    if ((mp = find_entry(entp->linkto+strlen(ddef.extra_linkdir)))
		!= NULL) {
		extra[i] = mp;
		mp->extra = i;	/* self-referential */
	    }
	}
	else
	    extra[i] = &userdef;
    }
    cache_closedir(dp);
}

void
remove_extra_links(void)
{
}

static const char *
get_pathto(const char *fpath, const char *tpath)
{
    const char *otpath = tpath;
    static char obuf[PATH_MAX+1];
    char *op = obuf;

    while (*fpath == *tpath && *fpath != '\0')
	fpath++, tpath++;

    /* Count directories to go up */
    while (*fpath != '\0') {
	if (*fpath == '/') {
	    strcpy(op, "../");
	    op += 3;
	}
	fpath++;
    }

    /* Now tag on directories to go down */
    while (tpath != otpath && *(tpath-1) != '/')
	tpath--;

    strcpy(op, tpath);

    return obuf;
}

void
add_extra_links(void)
{
    int i, j;
    char *name;
    char linktobuf[PATH_MAX];

    j = -1;

    for (i=0; i<MAX_MISC; i++) {
	if (misc[i].devfsnm == NULL)
	    break;

	if (misc[i].extra < 0) {
	    /* get next free number */
	    for (; extra[j] != NULL; j++);

	    /* prepare strings */
	    sprintf(linktobuf, "%s%s", get_pathto(ddef.extra_linkdir, ddef.devdir),
		    misc[i].unit);

	    /* Now format link name */
	    name = fmt_counter_devnm(ddef.extra_linkspec, ddef.extra_linkdir,
				     linktobuf, &j);

	    /* Do it! */

	    if (debugging) {
		fprintf(stderr, "adding extra link %s%s ==> %s\n",
			ddef.extra_linkdir, name, linktobuf);
		continue;
	    }

	    if (cache_symlink(linktobuf, ddef.extra_linkdir, name) != 0) {
		wmessage("devlinks: Could not create symlink '%s': %s\n",
			 name, strerror(errno));
	    }

	    misc[i].extra = j;

	    j++;
	}
    }
}

boolean_t
parse_dfs_spec(char *sbuf)
{
    char *vp, *ep;
    int i;
    unsigned int subno;
    int	found_type = 0;
    int	found_name = 0;

    /*
     * First tidy up old values
     */
    if (ddef.dfs.type) {
	free (ddef.dfs.type);
	ddef.dfs.type = NULL;
    }

    if (ddef.dfs.name) {
	free (ddef.dfs.name);
	ddef.dfs.name = NULL;
    }

    for (i=0; i<MAX_DEVFS_COMPMATCH && ddef.dfs.addr[i].pat; i++) {
	free (ddef.dfs.addr[i].pat);
	ddef.dfs.addr[i].pat = NULL;
    }

    for (i=0; i<MAX_DEVFS_COMPMATCH && ddef.dfs.minor[i].pat; i++) {
	free (ddef.dfs.minor[i].pat);
	ddef.dfs.minor[i].pat = NULL;
    }

    while (sbuf != NULL && *sbuf != '\0') {
	if ((ep = strchr(sbuf, ';')) != NULL)
	    *ep++ = '\0';

	if ((vp = strchr(sbuf, '=')) == NULL)
	    return(B_FALSE);

	*vp++ = '\0';

	if (strcmp(sbuf, CFS_TYPE) == 0) {
	    ddef.dfs.type = s_strdup(vp);
	    found_type++;

	} else if (strcmp(sbuf, CFS_NAME) == 0) {
	    ddef.dfs.name = s_strdup(vp);
	    found_name++;

	} else if (strncmp(sbuf, CFS_ADDR, strlen(CFS_ADDR)) == 0) {
	    if (sbuf[strlen(CFS_ADDR)] == '\0') {
		subno = 0;
	    }
	    else if (isdigit(sbuf[strlen(CFS_ADDR)])) {
		subno = strtoul(&sbuf[strlen(CFS_ADDR)], NULL, 10);
	    }
	    else
		return(B_FALSE);

	    i=0;
	    while (i < MAX_DEVFS_COMPMATCH) {
		if (ddef.dfs.addr[i].pat == NULL) {
		    /* Free slot, so fill it */
		    ddef.dfs.addr[i].no = subno;
		    ddef.dfs.addr[i].pat = s_strdup(vp);
		    break;
		}
		if (ddef.dfs.addr[i].no == subno)
		    return(B_FALSE);
		i++;
	    }
	    if (i >= MAX_DEVFS_COMPMATCH)
		    return(B_FALSE);

	}

	else if (strncmp(sbuf, CFS_MINOR, strlen(CFS_MINOR)) == 0) {
	    if (sbuf[strlen(CFS_MINOR)] == '\0') {
		subno = 0;
	    }
	    else if (isdigit(sbuf[strlen(CFS_MINOR)])) {
		subno = strtoul(&sbuf[strlen(CFS_MINOR)], NULL, 10);
	    }
	    else
		return(B_FALSE);

	    i=0;
	    while (i < MAX_DEVFS_COMPMATCH) {
		if (ddef.dfs.minor[i].pat == NULL) {
		    /* Free slot, so fill it */
		    ddef.dfs.minor[i].no = subno;
		    ddef.dfs.minor[i].pat = s_strdup(vp);
		    break;
		}
		if (ddef.dfs.minor[i].no == subno)
		    return(B_FALSE);
		i++;
	    }
	    if (i >= MAX_DEVFS_COMPMATCH)
		    return(B_FALSE);

	}


	else
	    return(B_FALSE);

	sbuf = ep;
    }

    /*
     * The type field is required, but we cannot have more than one name.
     */
    if ((found_type != 1) || (found_name > 1))
		return (B_FALSE);
    else
		return (B_TRUE);

}

pattype_t
find_pattern_type(const char *pattern)
{
    pattype_t rc = PAT_ABSOLUTE;

    while ((pattern = strchr(pattern, '\\')) != NULL) {
	pattern++;

	switch (*pattern) {
	case 'A':
	case 'M':
	    if (!isdigit(*(pattern+1)))
		return (PAT_INVALID);
	    /* FALLTHRU */
	case 'D':
	    if (rc == PAT_COUNTER)
		return (PAT_INVALID);
	    rc = PAT_DERIVED;
	    break;
	case 'N':
	    if (!isdigit(*(pattern+1)))
		return (PAT_INVALID);
	    if (rc == PAT_DERIVED || rc == PAT_COUNTER)
		return(PAT_INVALID);
	    rc = PAT_COUNTER;
	    break;
	case '\\':
	    break;
	default:
	    return(PAT_INVALID);
	}

	pattern++;
    }

    return(rc);
}

static FILE *table_fp = NULL;

boolean_t
get_misctbl_entry()
{
    char *cp, *sp, *tmp;
    int i;
    char lnbuf[1024];

    static unsigned int lncnt = 0;

    while ((tmp = fgets(lnbuf, sizeof(lnbuf), table_fp)) != NULL) {
	
	lncnt++;

	i = strlen(lnbuf);
	if (lnbuf[i-1] == '\n')
	    lnbuf[i-1] = '\0';
	else if (i == sizeof(lnbuf)-1) {
	    wmessage("devlinks: Line %d too long in configuration file %s \
-- should be less than %d characters\n",
		     lncnt, table_file, sizeof(lnbuf)-1);
	    while ((i = getc(table_fp)) != '\n' && i != EOF);
	    continue;
	}

	if (lnbuf[0] == '\0' || lnbuf[0] == '#')
	    continue;		/* Ignore comments and blank lines */

	/*
	 * Now parse into fields, with CF_SEPCHAR as separator;
	 */
	if ((cp = strchr(lnbuf, CF_SEPCHAR)) == NULL) {
	    wmessage("devlinks: Line %d in configuration file incorrect -- ignoring\n",
		     lncnt);
	    continue;
	}
	*cp = '\0';

	/* Parse devfs spec */
	if (parse_dfs_spec(lnbuf) == B_FALSE) {
	    wmessage("devlinks: Line %d in configuration file incorrect -- ignoring\n",
		     lncnt);
	    continue;
	}

	sp = cp+1;

	if ((cp = strchr(sp, CF_SEPCHAR)) != NULL) {
	    *cp = '\0';
	    strcpy(ddef.devpat, sp);

	    sp = cp+1;

	    if ((cp = strchr(sp, CF_SEPCHAR)) != NULL) {
		wmessage("devlinks: Line %d in configuration file has too many fields -- ignoring\n",
			 lncnt);
		continue;
	    }
	    strcpy(ddef.extra_linkpat, sp);
	}
	else {
	    strcpy(ddef.devpat, sp);

	    ddef.extra_linkpat[0] = '\0';
	}

	/* Check devpat for correctness -- set flags appropriately */
	if ((ddef.devpat_type = find_pattern_type(ddef.devpat)) == PAT_INVALID) {
	    wmessage("devlinks: Line %d in configuration file: devpat field '%s' \
incorrect, ignoring\n", lncnt, ddef.devpat);
	    continue;
	}

	if (ddef.extra_linkpat[0] != '\0' && 
	    find_pattern_type(ddef.extra_linkpat) != PAT_COUNTER) {
	    wmessage("devlinks: Line %d in configuration file: extra link field '%s' \
incorrect, ignoring line\n", lncnt, ddef.extra_linkpat);
	    continue;
	}

	break;
    }
    if (tmp == NULL)
	return (B_FALSE);

    strcpy(ddef.devdir, devdir_pfx);
    if ((cp = strrchr(ddef.devpat, '/')) == NULL) {
	ddef.devspec = ddef.devpat;
    }
    else {
	strncat(ddef.devdir, ddef.devpat, (cp - ddef.devpat + 1));
	if (! debugging)
	    create_dirs(ddef.devdir); /* Make it exist */
	ddef.devspec = cp+1;
    }

    if (ddef.extra_linkpat[0]) {
	strcpy(ddef.extra_linkdir, devdir_pfx);
	if ((cp = strrchr(ddef.extra_linkpat, '/')) == NULL) {
	    ddef.extra_linkspec = ddef.extra_linkpat;
	}
	else {
	    strncat(ddef.extra_linkdir, ddef.extra_linkpat,
		    (cp - ddef.extra_linkpat + 1));
	    if (! debugging)
		create_dirs(ddef.extra_linkdir); /* Make it exist */
	    ddef.extra_linkspec = cp+1;
	}
    }

    ddef.pathto_devfs[0] = '\0';
    cp = ddef.devpat;

    while ((cp = strchr(cp, '/')) != 0) {
	strcat(ddef.pathto_devfs, "../");
	cp++;			/* Skip the '/' */
    }

    strcat(ddef.pathto_devfs, "../devices/");

    ddef.devcheck = B_FALSE;	/* XXX */

    return B_TRUE;
}

void
free_globs()
{
    int i;

    for (i=0; i<MAX_MISC; i++) {
	if (misc[i].devfsnm) {
	    free(misc[i].devfsnm);
	    misc[i].devfsnm = NULL;
	}
	else
	    break;

	if (misc[i].unit) {
	    free(misc[i].unit);
	    misc[i].unit = NULL;
	}
	misc[i].state = 0;
    }

}

main(int argc, char **argv)
{
    extern int optind;
    char *rootdir = "";
    char *obpls_file;
    int c;

    (void) setlocale(LC_ALL, "");
    (void) textdomain(TEXT_DOMAIN);

    while ((c = getopt(argc, argv, "dr:t:")) != EOF)
	switch (c) {
	case 'r':
	    rootdir = optarg;
	    break;
	case 'd':
	    debugging = B_TRUE;
	    break;
	case 't':
	    table_file = optarg;
	    break;
	case '?':
	    fmessage(1, "Usage: devlinks [-r root_directory] [-t table-file]\n");
	}

    if (optind < argc)
	fmessage(1, "Usage: devlinks [-r root_directory] [-t table-file]\n");

    /*
     * open table file
     */
    if ((table_fp = fopen(table_file, "r")) == NULL) {
	fmessage(2, "devlinks: Could not open '%s': %s\n",
		 table_file, strerror(errno));
    }

    /*
     * Set address of dev directory.
     */
    devdir_pfx = s_malloc(strlen(rootdir) + sizeof("/dev/"));
    sprintf((char *)devdir_pfx, "%s%s", rootdir, "/dev/");
				/* Explicitly override const attribute */


    if (! debugging) {
	create_dirs(devdir_pfx);	/* Try to make sure it exists */

	/*
	 * try to open device type storage file.  Failure to do this
	 * is not a fatal error; the program will work correctly in all
	 * other respects.
	 */
	obpls_file = s_malloc(strlen(rootdir) + sizeof(OBPLSFILE));
	sprintf(obpls_file, "%s%s", rootdir, OBPLSFILE);
	obpfp = fopen(obpls_file, "w");
    }

    while (get_misctbl_entry()) {
	/*
	 * Start building list of matching devices by looking through /dev
	 *
	 * This is only useful for a "PAT_COUNTER" entry; for "PAT_DIRECT"
	 * there is probably only one link, and for "PAT_DERIVED" entries it
	 * is impossible to deduce the actual derived node names without
	 * knowing the devinfo nodes.
	 *
	 * It this proves a problem an extra pattern will have to be added to
	 * catch the derived nodes
	 *
	 */
	if (ddef.devpat_type == PAT_COUNTER || 
	    ddef.devpat_type == PAT_ABSOLUTE)
	    get_dev_entries();

	/*
	 * Now add to this real device configuration from /devfs
	 */
	get_devfs_entries();

	/*
	 * Delete unwanted or incorrect nodes
	 */
	if (ddef.devcheck)
	    remove_links();

	/*
	 * Make new links
	 */
	add_links();

	if (ddef.extra_linkpat[0] != '\0') {
	    extra = s_calloc(MAX_MISC, sizeof(*extra));
	    get_extra_dev();

	    remove_extra_links();

	    add_extra_links();

	    free(extra);
	}
	    
	    
	/*
	 *
	 */
	free_globs();
    }

    if (obpfp != NULL) {
	fclose(obpfp);
	(void) chmod(obpls_file, 0644);
    }

    return(0);
}
