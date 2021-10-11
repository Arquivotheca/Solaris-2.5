/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/


#ident	"@(#)ypserv_map.c	1.5	92/07/14 SMI"        /* SVr4.0 1.5   */

/*+++++++++++++++++++++++++++++++++++++++++++++++++++++++++
*	PROPRIETARY NOTICE (Combined)
*
* This source code is unpublished proprietary information
* constituting, or derived under license from AT&T's UNIX(r) System V.
* In addition, portions of such source code were derived from Berkeley
* 4.3 BSD under license from the Regents of the University of
* California.
*
*
*
*	Copyright Notice 
*
* Notice of copyright on this source code product does not indicate 
*  publication.
*
*	(c) 1986,1987,1988,1989,1990  Sun Microsystems, Inc
*	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
*          All rights reserved.
*/ 
#ifndef lint
static	char sccsid[] = "@(#)ypserv_map.c 1.10 88/02/08 Copyr 1985 Sun Micro";
#endif

#include <dirent.h>
#include "ypsym.h"
#include "ypdefs.h"
USE_YP_MASTER_NAME
USE_YP_LAST_MODIFIED
USE_YPDBPATH
USE_DBM

#include <ctype.h>
static char current_map[sizeof (ypdbpath) + YPMAXDOMAIN + YPMAXMAP + 3];
DBM *cur_fdb; /* will be passwd back up by ypset_current_map */
static enum { UNKNOWN, SECURE, PUBLIC } current_map_access;
static char map_owner[MAX_MASTER_NAME + 1];

extern unsigned int strlen();
extern int strcmp();
extern unsigned int ypcheck_domain();

/*
 * This performs an existence check on the dbm data base files <name>.pag and
 * <name>.dir.  pname is a ptr to the filename.  This should be an absolute
 * path.
 * Returns TRUE if the map exists and is accessable; else FALSE.
 *
 * Note:  The file name should be a "base" form, without a file "extension" of
 * .dir or .pag appended.  See ypmkfilename for a function which will generate
 * the name correctly.  Errors in the stat call will be reported at this level,
 * however, the non-existence of a file is not considered an error, and so will
 * not be reported.
 */
bool
ypcheck_map_existence(pname)
	char *pname;
{
	char dbfile[MAXNAMLEN + 1];
	struct stat filestat;
	int len;

	if (!pname || ((len = (int)strlen(pname)) == 0) ||
	    (len + sizeof (dbm_pag)) > (MAXNAMLEN + 1) ) {
		return(FALSE);
	}
		
	errno = 0;
	(void) strcpy(dbfile, pname);
	(void) strcat(dbfile, dbm_dir);

	if (stat(dbfile, &filestat) != -1) {
		(void) strcpy(dbfile, pname);
		(void) strcat(dbfile, dbm_pag);

		if (stat(dbfile, &filestat) != -1) {
			return(TRUE);
		} else {

			if (errno != ENOENT) {
				(void) fprintf(stderr,
				    "ypserv:  Stat error on map file %s.\n",
				    dbfile);
			}

			return(FALSE);
		}

	} else {

		if (errno != ENOENT) {
			(void) fprintf(stderr,
			    "ypserv:  Stat error on map file %s.\n",
			    dbfile);
		}

		return(FALSE);
	}

}

/*
 * The retrieves the order number of a named map from the order number datum
 * in the map data base.  
 */
bool
ypget_map_order(map, domain, order)
	char *map;
	char *domain;
	unsigned *order;
{
	datum key;
	datum val;
	char toconvert[MAX_ASCII_ORDER_NUMBER_LENGTH + 1];
	unsigned error;
	DBM *fdb;

	if ((fdb = ypset_current_map(map, domain, &error)) != NULL) {
		key.dptr = yp_last_modified;
		key.dsize = yp_last_modified_sz;
		val = dbm_fetch(fdb, key);

		if (val.dptr != (char *) NULL) {

			if (val.dsize > MAX_ASCII_ORDER_NUMBER_LENGTH) {
				return(FALSE);
			}

			/*
			 * This is getting recopied here because val.dptr
			 * points to static memory owned by the dbm package,
			 * and we have no idea whether numeric characters
			 * follow the order number characters, nor whether
			 * the mess is null-terminated at all.
			 */

			memcpy(toconvert, val.dptr, val.dsize);
			toconvert[val.dsize] = '\0';
			*order = (unsigned long) atol(toconvert);
			return(TRUE);
		} else {
		    return(FALSE);
		}
		    
	} else {
		return(FALSE);
	}
}

/*
 * The retrieves the master server name of a named map from the master datum
 * in the map data base.  
 */
bool
ypget_map_master(map, domain, owner)
	char *map;
	char *domain;
	char **owner;
{
	datum key;
	datum val;
	unsigned error;
	DBM *fdb;

	if ((fdb = ypset_current_map(map, domain, &error)) != NULL) {
		key.dptr = yp_master_name;
		key.dsize = yp_master_name_sz;
		val = dbm_fetch(fdb, key);

		if (val.dptr != (char *) NULL) {

			if (val.dsize > MAX_MASTER_NAME) {
				return(FALSE);
			}

			/*
			 * This is getting recopied here because val.dptr
			 * points to static memory owned by the dbm package.
			 */
			memcpy(map_owner, val.dptr, val.dsize);
			map_owner[val.dsize] = '\0';
			*owner = map_owner;
			return(TRUE);
		} else {
		    return(FALSE);
		}
		    
	} else {
		return(FALSE);
	}
}

/*
 * This makes a map into the current map, and calls dbminit on that map 
 * and returns the DBM pointer to the map. Procedures called by
 * ypserv dispatch routine would use this pointer for successive
 * ndbm operations.  Returns an YP_xxxx error code in error if FALSE.  
 */
DBM *
ypset_current_map(map, domain, error)
	char *map;
	char *domain;
	unsigned *error;
{
	char mapname[sizeof (current_map)];
	int lenm, lend;

	if (!map || ((lenm = (int)strlen(map)) == 0) || (lenm > YPMAXMAP) ||
	    !domain || ((lend = (int)strlen(domain)) == 0) || 
	    (lend > YPMAXDOMAIN)) {
		*error = YP_BADARGS;
		return(FALSE);
	}

	ypmkfilename(domain, map, mapname);

	if ((strcmp(mapname, current_map) == 0) && (cur_fdb)) {
		return(cur_fdb);
	}

	ypclr_current_map();
	current_map_access = UNKNOWN;

	if ((cur_fdb = dbm_open(mapname, O_RDWR, 0644)) != NULL) {
		(void) strcpy(current_map, mapname);
		return(cur_fdb);
	}

	ypclr_current_map();
	
	if (ypcheck_domain(domain)) {

		if (ypcheck_map_existence(mapname)) {
			*error = YP_BADDB;
		} else {
			*error = YP_NOMAP;
		}
		
	} else {
		*error = YP_NODOM;
	}

	return(NULL);
}

/*
 * This checks to see if there is a current map, and, if there is, does a
 * dbmclose on it and sets the current map name and its DBM ptr to null. 
 */
void
ypclr_current_map()
{
	if (cur_fdb != NULL) {
		(void) dbm_close(cur_fdb);
		cur_fdb = NULL;
	}
	current_map[0] = '\0';
}
