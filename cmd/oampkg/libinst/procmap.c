/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/
/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

/*LINTLIBRARY*/
#ident	"@(#)procmap.c	1.25	94/11/22 SMI"	/* SVr4.0 1.9.1.1	*/

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <pkgstrct.h>
#include <locale.h>
#include <libintl.h>
#include "pkglib.h"
#include "install.h"
#include "libinst.h"

#define	ERR_MEMORY	"memory allocation failure, errno=%d"
#define	ERR_DUPPATH	"duplicate pathname <%s>"

extern char	*errstr;

extern void	quit(int exitval);

/* libpkg/gpkgmap */
extern int	getmapmode();

#define	DIRMALLOC	128
#define	EPTMALLOC	512

static struct cfent **eptlist;
static struct cfextra **extlist;

static int	eptnum;
static int	errflg;
static int	nparts;

static void	procinit(char *space);
static int	procassign(struct cfent *ept, char **server_local,
		    char **client_local, char **server_path,
		    char **client_path, char **map_path, int mapflag,
		    int nc);

static int	ckdup(struct cfent *ept1, struct cfent *ept2);
static int	sortentry(int index);
static int	sortentry_x(int index);

static void
procinit(char *space)
{
	errflg = nparts = eptnum = 0;

	if (space)
		free(space);
	if (eptlist)
		free(eptlist);
	if (extlist)
		free(extlist);

	/*
	 * initialize dynamic memory used to store
	 * path information which is read in
	 */
	(void) pathdup((char *)0);
}

/*
 * This function assigns appropriate values based upon the pkgmap entry
 * in the cfent structure.
 */
static int
procassign(struct cfent *ept, char **server_local, char **client_local,
    char **server_path, char **client_path, char **map_path, int mapflag,
    int nc)
{
	int	path_duped = 0;
	int	local_duped = 0;
	char	source[PATH_MAX+1];

	if (nc >= 0 && ept->ftype != 'i')
		if ((ept->pkg_class_idx = cl_idx(ept->pkg_class)) == -1)
			return (1);

	if (ept->volno > nparts)
		nparts++;

	/*
	 * Generate local (delivered source) paths for files
	 * which need them so that the install routine will know
	 * where to get the file from the package. Note that we
	 * do not resolve path environment variables here since
	 * they won't be resolved in the reloc directory.
	 */
	if ((mapflag > 1) && strchr("fve", ept->ftype)) {
		if (ept->ainfo.local == NULL) {
			source[0] = '~';
			(void) strcpy(&source[1], ept->path);
			ept->ainfo.local = pathdup(source);
			*server_local = ept->ainfo.local;
			*client_local = ept->ainfo.local;

			local_duped = 1;
		}
	}

	/*
	 * Evaluate the destination path based upon available
	 * environment, then produce a client-relative and
	 * server-relative canonized path.
	 */
	if (mapflag && (ept->ftype != 'i')) {
		mappath(getmapmode(), ept->path); /* evaluate variables */
		canonize(ept->path);	/* Fix path as necessary. */

		eval_path(server_path,
		    client_path,
		    map_path,
		    ept->path);
		path_duped = 1;	/* eval_path dup's it */
		ept->path = *server_path;	/* default */
	}

	/*
	 * Deal with source for hard and soft links.
	 */
	if (strchr("sl", ept->ftype)) {
		if (mapflag) {
			mappath(getmapmode(), ept->ainfo.local);
			if (!RELATIVE(ept->ainfo.local)) {
				canonize(ept->ainfo.local);

				/* check for hard link */
				if (ept->ftype == 'l') {
					eval_path(
					    server_local,
					    client_local,
					    NULL,
					    ept->ainfo.local);
					local_duped = 1;

					/* Default to server. */
					ept->ainfo.local = *server_local;
				}
			}
		}
	}

	/*
	 * For the paths (both source and target) were too mundane to
	 * have been copied into dup space yet, do that.
	 */
	if (!path_duped) {
		*server_path = pathdup(ept->path);
		*client_path = *server_path;
		ept->path = *server_path;

		path_duped = 1;
	}
	if (ept->ainfo.local != NULL)
		if (!local_duped) {
			*server_local = pathdup(ept->ainfo.local);
			ept->ainfo.local = *server_local;
			*client_local = ept->ainfo.local;

		local_duped = 1;
	}

	return (0);
}

/*
 * This function reads the pkgmap (or any file similarly formatted) and
 * returns a pointer to a list of struct cfent representing the contents
 * of that file.
 */
struct cfent **
procmap(FILE *fp, int mapflag, char *ir)
{
	struct	cfent *ept;
	int	i;
	int	n;
	int	nc;
	static char *server_local, *client_local;
	static char *server_path, *client_path, *map_path;
	static struct cfent *space;

	procinit((char *)space);

	space = (struct cfent *) calloc(EPTMALLOC,
		(unsigned) sizeof (struct cfent));
	if (space == NULL) {
		progerr(gettext(ERR_MEMORY), errno);
		quit(99);
	}

	nc = cl_getn();
	for (;;) {
		ept = &space[eptnum];

		n = gpkgmap(ept, fp);
		if (n == 0)
			break; /* no more entries in pkgmap */
		else if (n < 0) {
			progerr(gettext("bad entry read in pkgmap"));
			logerr(gettext("pathname=%s"),
			    (ept->path && *ept->path) ? ept->path : "Unknown");
			logerr(gettext("problem=%s"),
			    (errstr && *errstr) ? errstr : "Unknown");
			return (NULL);
		}

		if (procassign(ept, &server_local, &client_local,
		    &server_path, &client_path, &map_path,
		    mapflag, nc))
			continue;


		if ((++eptnum % EPTMALLOC) == 0) {
			space = (struct cfent *) realloc(space,
			    (unsigned) (sizeof (struct cfent) *
			    (eptnum+EPTMALLOC)));
			if (space == NULL) {
				progerr(gettext(ERR_MEMORY), errno);
				return (NULL);
			}
		}
	}

	/* setup a pointer array to point to malloc'd entries space */
	eptlist = (struct cfent **) calloc(eptnum+1, sizeof (struct cfent *));
	if (eptlist == NULL) {
		progerr(gettext(ERR_MEMORY), errno);
		quit(99);
	}
	for (i = 0; i < eptnum; i++)
		eptlist[i] = &space[i];

	(void) sortentry(-1);
	for (i = 0; i < eptnum; /* void */) {
		if (!sortentry(i))
			i++;
	}
	return (errflg ? NULL : eptlist);
}

/*
 * This function reads the pkgmap (or any file similarly formatted) and
 * returns a pointer to a list of struct cfextra (each of which
 * contains a struct cfent) representing the contents of that file.
 */
struct cfextra **
procmap_x(FILE *fp, int mapflag, char *ir)
{
	struct	cfextra *ext;
	struct	cfent *ept;
	int	i;
	int	n;
	int	nc;
	static struct cfextra *space;

	procinit((char *)space);

	space = (struct cfextra *) calloc(EPTMALLOC,
		(unsigned) sizeof (struct cfextra));
	if (space == NULL) {
		progerr(gettext(ERR_MEMORY), errno);
		quit(99);
	}

	nc = cl_getn();
	for (;;) {
		ext = &space[eptnum];
		ept = &(ext->cf_ent);

		/*
		 * Fill in a cfent structure in a very preliminary fashion.
		 * ept->path and ept->ainfo.local point to static memory
		 * areas of size PATH_MAX. These are manipulated and
		 * then provided their own allocations later in this function.
		 */
		n = gpkgmap(ept, fp);

		if (n == 0)
			break; /* no more entries in pkgmap */
		else if (n < 0) {
			progerr(gettext("bad entry read in pkgmap"));
			logerr(gettext("pathname=%s"),
			    (ept->path && *ept->path) ? ept->path : "Unknown");
			logerr(gettext("problem=%s"),
			    (errstr && *errstr) ? errstr : "Unknown");
			return (NULL);
		}

		if (procassign(ept,
		    &(ext->server_local),
		    &(ext->client_local),
		    &(ext->server_path),
		    &(ext->client_path),
		    &(ext->map_path),
		    mapflag, nc))
			continue;

		ext->fsys_value = BADFSYS;	/* No file system data yet */

		/* Allocate new space for the list if necessary */
		if ((++eptnum % EPTMALLOC) == 0) {
			space = (struct cfextra *) realloc(space,
			    (unsigned) (sizeof (struct cfextra) *
			    (eptnum+EPTMALLOC)));
			if (space == NULL) {
				progerr(gettext(ERR_MEMORY), errno);
				return (NULL);
			}
		}
	}

	/* setup a pointer array to point to malloc'd entries space */
	extlist = (struct cfextra **) calloc(eptnum+1,
	    sizeof (struct cfextra *));
	if (extlist == NULL) {
		progerr(gettext(ERR_MEMORY), errno);
		quit(99);
	}
	for (i = 0; i < eptnum; i++)
		extlist[i] = &space[i];

	(void) sortentry_x(-1);
	for (i = 0; i < eptnum; /* void */) {
		if (!sortentry_x(i))
			i++;
	}
	return (errflg ? NULL : extlist);
}

static int
sortentry(int index)
{
	struct cfextra *ext;
	struct cfent *ept;
	static int last = 0;
	int	i, n, j;
	int	upper, lower;

	if (index == 0)
		return (0);
	else if (index < 0) {
		last = 0;
		return (0);
	}

	ept = eptlist[index];
	/* quick comparison optimization for pre-sorted arrays */
	if (strcmp(ept->path, eptlist[index-1]->path) > 0) {
		/* do nothing */
		last = index-1;
		return (0);
	}

	lower = 0;
	upper = index-1;
	i = last;
	do {
		n = strcmp(ept->path, eptlist[i]->path);
		if (n == 0) {
			if (ckdup(ept, eptlist[i])) {
				progerr(gettext(ERR_DUPPATH), ept->path);
				errflg++;
			}
			/* remove the entry at index */
			while (index < eptnum) {
				eptlist[index] = eptlist[index+1];
				index++;
			}
			eptnum--;
			return (1);
		} else if (n < 0) {
			/* move down array */
			upper = i;
			i = lower + (upper-lower)/2;
		} else {
			/* move up array */
			lower = i+1;
			i = upper - (upper-lower)/2;
		}
	} while (upper != lower);
	last = i = upper;

	/* expand to insert at i */
	for (j = index; j > i; j--)
		eptlist[j] = eptlist[j-1];

	eptlist[i] = ept;
	return (0);
}

static int
sortentry_x(int index)
{
	struct cfextra *ext;
	struct cfent *ept;
	static int last = 0;
	int	i, n, j;
	int	upper, lower;

	if (index == 0)
		return (0);
	else if (index < 0) {
		last = 0;
		return (0);
	}

	ext = extlist[index];
	ept = &(ext->cf_ent);

	/* quick comparison optimization for pre-sorted arrays */
	if (strcmp(ept->path, extlist[index-1]->cf_ent.path) > 0) {
		/* do nothing */
		last = index-1;
		return (0);
	}

	lower = 0;
	upper = index-1;
	i = last;
	do {
		n = strcmp(ept->path, extlist[i]->cf_ent.path);
		if (n == 0) {
			if (ckdup(ept, &(extlist[i]->cf_ent))) {
				progerr(gettext(ERR_DUPPATH), ept->path);
				errflg++;
			}
			/* remove the entry at index */
			while (index < eptnum) {
				extlist[index] = extlist[index+1];
				index++;
			}
			eptnum--;
			return (1);
		} else if (n < 0) {
			/* move down array */
			upper = i;
			i = lower + (upper-lower)/2;
		} else {
			/* move up array */
			lower = i+1;
			i = upper - (upper-lower)/2;
		}
	} while (upper != lower);
	last = i = upper;

	/* expand to insert at i */
	for (j = index; j > i; j--)
		extlist[j] = extlist[j-1];

	extlist[i] = ext;
	return (0);
}

static int
ckdup(struct cfent *ept1, struct cfent *ept2)
{
	/* ept2 will be modified to contain "merged" entries */

	if (!strchr("?dx", ept1->ftype))
		return (1);

	if (!strchr("?dx", ept2->ftype))
		return (1);

	if (ept2->ainfo.mode == BADMODE)
		ept2->ainfo.mode = ept1->ainfo.mode;
	if ((ept1->ainfo.mode != ept2->ainfo.mode) &&
	    (ept1->ainfo.mode != BADMODE))
		return (1);

	if (strcmp(ept2->ainfo.owner, "?") == 0)
		(void) strcpy(ept2->ainfo.owner, ept1->ainfo.owner);
	if (strcmp(ept1->ainfo.owner, ept2->ainfo.owner) &&
	    strcmp(ept1->ainfo.owner, "?"))
		return (1);

	if (strcmp(ept2->ainfo.group, "?") == 0)
		(void) strcpy(ept2->ainfo.group, ept1->ainfo.group);
	if (strcmp(ept1->ainfo.group, ept2->ainfo.group) &&
	    strcmp(ept1->ainfo.group, "?"))
		return (1);

	return (0);
}
