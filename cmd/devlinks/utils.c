#ident	"@(#)utils.c	1.4	93/09/16 SMI"
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <libgen.h>
#include <errno.h>
#include <locale.h>

#include "utilhdr.h"

void
wmessage(const char *message, ...)
{
    va_list ap;

    va_start(ap, message);

    (void)vfprintf(stderr, gettext(message), ap);

    return;
}

void
fmessage(const int rc, const char *message, ...)
{
    va_list ap;

    va_start(ap, message);

    (void)vfprintf(stderr, gettext(message), ap);

    (void)exit(rc);
}

/*
 * Safe alloc routines -- fail with a fatal error message if anything
 * goes wrong.
 */
void *
s_calloc(const size_t nelem, const size_t elsize)
{
    void *rp;

    rp = calloc(nelem, elsize);

    if (rp == NULL)
	fmessage(1, "Cannot calloc %d bytes\n", nelem * elsize);

    return(rp);
}

void *
s_malloc(const size_t size)
{
    void *rp;

    rp = malloc(size);

    if (rp == NULL)
	fmessage(1, "Cannot malloc %d bytes\n", size);

    return(rp);
}

void *
s_realloc(void *optr, const size_t size)
{
    void *rp;

    rp = realloc(optr, size);

    if (rp == NULL)
	fmessage(1, "Cannot realloc %d bytes\n", size);

    return(rp);
}

char *
s_strdup(const char *s1)
{
    char *s2;

    s2 = strdup(s1);

    if (s2 == NULL)
	fmessage(1, "Cannot alloc %d bytes for dup\n", strlen(s1)+1);

    return(s2);
}

/*
 * Check and create if needed a dirctory path.  Much like stanard libgen routine
 * (which it uses), but does not return status, and uses default permissions.
 */
void
create_dirs(const char *dirp)
{
    /*
     * This routine contains extra code to work around a bug in mkdirp
     * causing it to write one beyond the end of a malloc'd string
     * in the input sting ends in a '/'.  This junk can be removed when mkdirp
     * is fixed.
     */
    char *cp;

    if (dirp != NULL && *dirp != '\0' && dirp[strlen(dirp)-1] == '/') {
	cp = s_strdup(dirp);
	cp[strlen(cp)-1] = '\0';
	(void)mkdirp(cp, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
	free(cp);
	return;
    }

    /*
     * If this proves slow, can try statting directory before doing mkdirp().
     */
    (void)mkdirp(dirp, S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH);
}

/*
 * routines to break apart a devfs name.
 *
 * All these routines work only on the last devfs name component in the path
 * given to them.
 *
 * All return pointers to a static area.  The next call to the same routine will
 * overwrite the area.
 */

const char *
getdevname(const char *devnm)
{
    static char nbuf[256];

    const char *cp;
    char *np = nbuf;

    cp = strrchr(devnm, '/');	/* Look for last element */

    if (cp == NULL)
	cp = devnm;
    else
	cp++;

    while (*cp != '\0' && *cp != '@' && *cp != ':') {
	*np++ = *cp++;
    }

    *np = '\0';

    return(nbuf);
}


const char *
getdevaddr(const char *devnm, const unsigned int subno)
{
    static char nbuf[256];
    const char *subfp;

    const char *cp;
    char *np = nbuf;

    cp = strrchr(devnm, '/');	/* Look for last element */

    if (cp == NULL)
	cp = devnm;
    else
	cp++;

    if ((cp = strchr(cp, '@')) == NULL)
	    return(NULL);

    cp++;

    while (*cp != '\0' && *cp != ':')
	*np++ = *cp++;

    *np = '\0';

    if (subno != 0) {

	if ((subfp = substring(nbuf, DS_SSSEP, subno)) == NULL)
	    return NULL;

	strcpy(nbuf, subfp);	/* So another substing call doesn't stop on this */
    }

    return nbuf;
}


const char *
getdevminor(const char *devnm, const unsigned int subno)
{
    static char nbuf[256];
    const char *subfp;

    const char *cp;
    char *np = nbuf;

    cp = strrchr(devnm, '/');	/* Look for last element */

    if (cp == NULL)
	cp = devnm;
    else
	cp++;

    if ((cp = strchr(cp, ':')) == NULL)
	    return(NULL);

    cp++;

    while (*cp != '\0')
	    *np++ = *cp++;

    *np = '\0';

    if (subno != 0) {

	if ((subfp = substring(nbuf, DS_SSSEP, subno)) == NULL)
	    return NULL;

	strcpy(nbuf, subfp);	/* So another substing call doesn't stop on this */
    }

    return nbuf;
}

/*
 * Return the 'ss_no' substring of a string, where the substrings
 * are delimited by 'sep_char'.  The first substring is substring 1;
 * substring 0 is the entire string.
 */
const char *substring(const char *string, const char sep_char, const int ss_no)
{
    static char buf[256];
    char *cp = buf;
    int s_index = 1;

    if (string == NULL)
	return(NULL);

    if (ss_no == 0)
	return(string);

    while (*string) {
	if (*string == sep_char) {
	    if (++s_index > ss_no)
		break;
	}
	else if (s_index == ss_no)
	    *cp++ = *string;

	string++;
    }
    if (s_index >= ss_no) {
	*cp = '\0';
	return(buf);
    }
    else
	return(NULL);
}


/*
 * Directory cacheing stuff
 */
#define CACHE_S_CLOSED 0
#define CACHE_S_ROPEN 1
#define CACHE_S_COPEN 2


struct cache_hdr {
    CACHE_DIR *dir;
    struct cache_hdr *next;
};

typedef struct cache_hdr CACHE_HDR;

static CACHE_HDR *cache = NULL;

CACHE_DIR *
cache_opendir(const char *dirname)
{
    CACHE_HDR *dp, *ldp = NULL;
    CACHE_DIR *dirp;

    if (*dirname == '\0')	/* assume zero-legth is current dir */
	dirname = "./";

    for (dp = cache; dp != NULL; dp = dp->next) {
	if (strcmp(dirname, dp->dir->dirname) == 0) {
	    dirp = dp->dir;
	    if (dirp->state != CACHE_S_CLOSED) {
		fmessage(1, "Can't re-open already open directory '%s'\n",
			 dirname);
		/*NOTREACHED*/
	    }

	    dirp->current = dirp->head;
	    dirp->state = CACHE_S_COPEN;
	    return (dirp);
	}
	ldp = dp;
    }

    dp = s_calloc(sizeof(*dp), 1);

    dirp = s_calloc(sizeof(*dirp), 1);

    dp->dir = dirp;

    dirp->dp = opendir(dirname);

    if (dirp->dp == NULL) {
	free(dirp);
	free(dp);
	return NULL;
    }

    dirp->dirname = s_strdup(dirname);

    dirp->state = CACHE_S_ROPEN;

    if (ldp == NULL)
	cache = dp;
    else
	ldp->next = dp;

    return dirp;
	
}

static CACHE_IENT *
cache_enter(CACHE_DIR *dp, const char *name, const char *link)
{
    CACHE_IENT *ientp;

    /*
     * Cache the new entry
     */
    ientp = s_calloc(sizeof(*ientp), 1);

    ientp->ent.name = s_strdup(name);
    ientp->ent.linkto = (link == NULL ? link : s_strdup(link));

    if (dp->current != NULL)
	dp->current->next = ientp;
    else
	dp->head = ientp;

    dp->current = ientp;

    return(ientp);
}


static const CACHE_ENT *
cache_readnextlink(CACHE_DIR *dp)
{
    struct dirent *dirp;
    int rc, linksize;
    struct stat sb;
    CACHE_IENT *ientp;
    char namebuf[PATH_MAX+1];
    char linkbuf[PATH_MAX+1];

    while ((dirp = readdir(dp->dp)) != NULL) {
	if (strcmp(dirp->d_name, ".") == 0 || strcmp(dirp->d_name, "..") == 0)
	    continue;

	sprintf(namebuf, "%s%s", dp->dirname, dirp->d_name);

	if ((rc = lstat(namebuf, &sb)) < 0) {
	    wmessage ("Cannot stat %s\n", namebuf);
	    continue;
	}

	switch(sb.st_mode & S_IFMT) {
	case S_IFLNK:
	    linksize = readlink(namebuf,linkbuf, PATH_MAX);

	    if (linksize <= 0) {
		wmessage("Could not read symbolic link %s\n", namebuf);
		continue;
	    }

	    linkbuf[linksize] = '\0';

	    /*
	     * Cache the new entry
	     */
	    ientp = cache_enter(dp, dirp->d_name, linkbuf);

	    return(&ientp->ent);

	    /*NOTREACHED*/

	default:
	    (void)cache_enter(dp, dirp->d_name, NULL);

	    continue;
	}
    }
	
    return(NULL);
}

const CACHE_ENT *
cache_readdir(CACHE_DIR *dp)
{
    CACHE_ENT *rp;

    if (dp->state == CACHE_S_ROPEN)
	return (cache_readnextlink(dp));
    else if (dp->state != CACHE_S_COPEN) {
	fmessage(1, "cache_readdir: directory not open\n");
	/*NOTREACHED*/
    }

    for(; dp->current != NULL; dp->current = dp->current->next)
	if (dp->current->ent.linkto != NULL) {
	    rp = &dp->current->ent;
	    dp->current = dp->current->next;
	    return(rp);
	}

    return(NULL);
}

void
cache_closedir(CACHE_DIR *dp)
{
    if (dp->state == CACHE_S_ROPEN) {
	/* Fill rest of cache */
	while (cache_readdir(dp) != NULL);

	closedir(dp->dp);
	dp->dp = NULL;
    }

    dp->state = CACHE_S_CLOSED;
}

int
cache_unlink(const char *dirname, const char *filename)
{
    int rc;
    CACHE_HDR *dp;
    char namebuf[PATH_MAX+1];

    /*
     * first do it
     */
    sprintf(namebuf, "%s%s", dirname, filename);

    if ((rc = unlink(namebuf)) != 0) {
	return(rc);
    }

    if (*dirname == '\0')
	dirname = "./";

    /*
     * Now tidy up cache (if needed)
     */
    for (dp = cache; dp != NULL; dp = dp->next) {
	if (strcmp(dirname, dp->dir->dirname) == 0) {
	    /*
	     * found cached directory, now find entry
	     */
	    CACHE_IENT *ientp, *lp = NULL;

	    for (ientp = dp->dir->head; ientp != NULL; ientp = ientp->next)
		if (strcmp(ientp->ent.name, filename) == 0) {
		    if (dp->dir->current == ientp)
			dp->dir->current = ientp->next;

		    if (lp == NULL) {
			dp->dir->head = ientp->next;
		    }
		    else
			lp->next = ientp->next;

		    free((char *)ientp->ent.name);
		    if(ientp->ent.linkto)
			free((char *)ientp->ent.linkto);
		    free(ientp);

		    return(rc);
		}
	}
    }

    return(rc);
}


int
cache_symlink(const char *linkdata,
	      const char *dirname,
	      const char *filename)
{
    int rc;
    CACHE_HDR *dp;
    CACHE_IENT *ientp;
    CACHE_IENT *lp = NULL;

    char namebuf[PATH_MAX+1];

    sprintf(namebuf, "%s%s", dirname, filename);

    if (*dirname == '\0')
	dirname = "./";

    /*
     * Search cache for entry
     */
    for (dp = cache; dp != NULL; dp = dp->next) {
	if (strcmp(dirname, dp->dir->dirname) == 0)
	    break;
    }

    if (dp == NULL) {
	/*
	 * Take this opportunity to cache the directory
	 */
	CACHE_DIR *dirp;

	if ((dirp=cache_opendir(dirname)) == NULL)
	    return(errno);

	while (cache_readdir(dirp));

	cache_closedir(dirp);

	for (dp = cache; dp != NULL; dp = dp->next) {
	    if (strcmp(dirname, dp->dir->dirname) == 0)
		break;
	}

	if (dp == NULL)
	    fmessage(2, "Directory cacheing error\n");
    }

	
    /*
     * found cached directory, now find entry (if there)
     */
    for (ientp = dp->dir->head; ientp != NULL; ientp = ientp->next) {
	if (strcmp(ientp->ent.name, filename) == 0) {
	    /*
	     * Found it!  Check if it is already correct
	     */
	    if (ientp->ent.linkto && strcmp(ientp->ent.linkto, linkdata) == 0)
		return(0);

	    /*
	     * Not correct, so need to update it, removing old data
	     */
	    if ((rc = unlink(namebuf)) != 0 ||
		(rc = symlink(linkdata, namebuf)) != 0) {
		return(rc);
	    }

	    if (ientp->ent.linkto)
		free((char *)ientp->ent.linkto);

	    ientp->ent.linkto = s_strdup(linkdata);

	    return(rc);
	}
    }

    /*
     * Not found, so add new entry at tail
     */
    if ((rc = symlink(linkdata, namebuf)) != 0) {
	return(rc);
    }

    /* Easiest to insert at head, so do it there. */
    ientp = s_calloc(sizeof(*ientp), 1);

    ientp->ent.name = s_strdup(filename);
    ientp->ent.linkto = s_strdup(linkdata);
    ientp->next = dp->dir->head;

    dp->dir->head = ientp;

    return(rc);
}
