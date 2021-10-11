/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)libs.c	1.12	94/07/18 SMI"

/*
 * Library processing
 */
#include	<stdio.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<string.h>
#include	<memory.h>
#include	<limits.h>
#include	"paths.h"
#include	"debug.h"
#include	"_ld.h"

/*
 * Function to handle -YL and -YU substitutions in LIBPATH.  It's probably
 * very unlikely that the link-editor will ever see this, as any use of these
 * options is normally processed by the compiler driver first and the finished
 * -YP string is sent to us.  The fact that these two options are not even
 * documented anymore makes it even more unlikely this processing will occur.
 */
static char *
compat_YL_YU(char * path, int index)
{
	if (index == YLDIR) {
		if (Llibdir) {
			/*
			 * User supplied "-YL,libdir", this is the pathname that
			 * corresponds for compatibility to -YL (as defined in
			 * sgs/include/paths.h)
			 */
			DBG_CALL(Dbg_libs_ylu(Llibdir, path, index));
			return (Llibdir);
		}
	} else if (index == YUDIR) {
		if (Ulibdir) {
			/*
			 * User supplied "-YU,libdir", this is the pathname that
			 * corresponds for compatibility to -YU (as defined in
			 * sgs/include/paths.h)
			 */
			DBG_CALL(Dbg_libs_ylu(Ulibdir, path, index));
			return (Ulibdir);
		}
	}
	return (path);
}

static char *
process_lib_path(List * list, char * path, Boolean subsflag)
{
	int		i;
	char *		cp;
	Boolean		seenflg = FALSE;
	char *		dot = ".";

	for (i = YLDIR; i; i++) {
		cp = strpbrk(path, ";:");
		if (cp == NULL) {
			if (*path == '\0') {
				if (seenflg)
					if (list_append(list, subsflag ?
					    compat_YL_YU(dot, i) : dot) == 0)
						return ((char *)S_ERROR);
			} else
				if (list_append(list, subsflag ?
				    compat_YL_YU(path, i) : path) == 0)
					return ((char *)S_ERROR);
			return (cp);
		}

		if (*cp == ':') {
			*cp = '\0';
			if (cp == path) {
				if (list_append(list, subsflag ?
				    compat_YL_YU(dot, i) : dot) == 0)
					return ((char *)S_ERROR);
			} else {
				if (list_append(list, subsflag ?
				    compat_YL_YU(path, i) : path) == 0)
					return ((char *)S_ERROR);
			}
			path = cp + 1;
			seenflg = TRUE;
			continue;
		}

		/* case ";" */

		if (cp != path) {
			if (list_append(list, subsflag ?
			    compat_YL_YU(path, i) : path) == 0)
				return ((char *)S_ERROR);
		} else {
			if (seenflg)
				if (list_append(list, subsflag ?
				    compat_YL_YU(dot, i) : dot) == 0)
					return ((char *)S_ERROR);
		}
		return (cp);
	}
	/* NOTREACHED */
}

/*
 * adds the indicated path to those to be searched for libraries.
 */
int
add_libdir(Ofl_desc * ofl, const char * path)
{
	if (insert_lib == NULL) {
		if (list_prepend(&ofl->ofl_ulibdirs, path) == 0)
			return (S_ERROR);
		insert_lib = ofl->ofl_ulibdirs.head;
	} else
		if ((insert_lib = list_insert(&ofl->ofl_ulibdirs, path,
		    insert_lib)) == 0)
			return (S_ERROR);

	/*
	 * As -l and -L options can be interspersed, print the library
	 * search paths each time a new path is added.
	 */
	DBG_CALL(Dbg_libs_update(&ofl->ofl_ulibdirs, &ofl->ofl_dlibdirs));
	return (1);
}

/*
 * Process a required library.  Combine the directory and filename, check the
 * resultant path size, and then append either a `.so' or `.a' suffix and try
 * opening the associated pathname.
 */
static Ifl_desc *
find_lib_name(const char * dir, const char * file, int flen, Ofl_desc * ofl)
{
	int		dlen, plen, fd;
	char *		_path, path[PATH_MAX];
	const char *	_dir = dir;

	/*
	 * Determine the sizes of the directory and filename to insure we dont't
	 * exceed our buffer.
	 */
	if ((dlen = strlen(dir)) == 0) {
		_dir = ".";
		dlen = 1;
	}
	plen = dlen + flen;
	if ((plen + 4) > PATH_MAX) {
		eprintf(ERR_FATAL, "%s/lib%s: pathname too long", _dir, file);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}

	/*
	 * Build the pathname (up to the shared object/archive suffix).
	 */
	(void) strcpy(path, _dir);
	(void) strcpy((path + dlen), file);
	dlen++;

	/*
	 * If we are in dynamic mode try and open the associated shared object.
	 */
	if (ofl->ofl_flags & FLG_OF_DYNLIBS) {
		(void) strcpy((path + plen), ".so");
		DBG_CALL(Dbg_libs_l(file, path));
		if ((fd = open(path, O_RDONLY)) != -1) {

			if ((_path = (char *)malloc(strlen(path) + 1)) == 0)
				return ((Ifl_desc *)S_ERROR);
			(void) strcpy(_path, path);

			return ((Ifl_desc *)process_open(_path, dlen, fd,
			    ofl, FLG_IF_NEEDED));
		}
	}

	/*
	 * If we are not in dynamic mode, or a shared object could not be
	 * located, try and open the associated archive.
	 */
	(void) strcpy((path + plen), ".a");
	DBG_CALL(Dbg_libs_l(file, path));
	if ((fd = open(path, O_RDONLY)) != -1) {

		if ((_path = (char *)malloc(strlen(path) + 1)) == 0)
			return ((Ifl_desc *)S_ERROR);
		(void) strcpy(_path, path);

		return ((Ifl_desc *)process_open(_path, dlen, fd, ofl,
		    FLG_IF_NEEDED));
	}

	return (0);
}

/*
 * Take the abbreviated name of a library file (from -lfoo) and searches for the
 * library.  The search path rules are:
 *
 *	o	use any user supplied paths, i.e. LD_LIBRARY_PATH and -L, then
 *
 *	o	use the default directories, i.e. LIBPATH or -YP.
 *
 * If we are in dynamic mode and -Bstatic is not in effect, first look for a
 * shared object with full name: path/libfoo.so; then [or else] look for an
 * archive with name: path/libfoo.a.  If no file is found, it's a fatal error,
 * otherwise process the file appropriately depending on its type.
 */
int
find_library(const char * name, Ofl_desc * ofl)
{
	Listnode *	lnp;
	char *		path;
	int		flen;
	Ifl_desc *	ifl = 0;
	char		file[PATH_MAX];

	/*
	 * Create the library name from the supplied abbreviated name  (the
	 * worst case name would result in "./lib{very-long-name}.so\0")
	 */
	if ((flen = strlen(name)) > (PATH_MAX - 9)) {
		eprintf(ERR_FATAL, "lib%s: library name too long", name);
		ofl->ofl_flags |= FLG_OF_FATAL;
		return (0);
	}
	(void) strcpy(file, "/lib");
	(void) strcat(file, name);
	flen += 4;

	/*
	 * Search for this file in any user defined directories.
	 */
	for (LIST_TRAVERSE(&ofl->ofl_ulibdirs, lnp, path)) {
		if ((ifl = find_lib_name(path, file, flen, ofl)) == 0)
			continue;
		return ((int)ifl);
	}

	/*
	 * Finally try the default library search directories.
	 */
	for (LIST_TRAVERSE(&ofl->ofl_dlibdirs, lnp, path)) {
		if ((ifl = find_lib_name(path, file, flen, ofl)) == 0)
			continue;
		return ((int)ifl);
	}

	/*
	 * If we've got this far we haven't found a shared object or archive.
	 */
	eprintf(ERR_FATAL, "library -l%s: not found", name);
	ofl->ofl_flags |= FLG_OF_FATAL;
	return (0);
}

/*
 * Inspect the LD_LIBRARY_PATH variable (if the -i options has not been
 * specified), and set up the directory list from which to search for
 * libraries.  From the man page:
 *
 *	LD_LIBRARY_PATH=dirlist1;dirlist2
 * and
 *	ld ... -Lpath1 ... -Lpathn ...
 *
 * results in a search order of:
 *
 *	dirlist1 path1 ... pathn dirlist2 LIBPATH
 *
 * If LD_LIBRARY_PATH has no `;' specified, the pathname(s) supplied are
 * all taken as dirlist2.
 */
int
lib_setup(Ofl_desc * ofl)
{
	char *	path;
	char *	cp;

	if (!(ofl->ofl_flags & FLG_OF_IGNENV) &&
	    ((cp = getenv("LD_LIBRARY_PATH")) != NULL)) {
		if ((path = (char *)malloc(strlen(cp) + 1)) == 0)
			return (S_ERROR);
		(void) strcpy(path, cp);
		DBG_CALL(Dbg_libs_path(path));

		/*
		 * Process the first path string (anything up to a null or
		 * a `;');
		 */
		path = process_lib_path(&ofl->ofl_ulibdirs, path, FALSE);

		/*
		 * If a `;' was seen then initialize the insert flag to the
		 * tail of this list.  This is where any -L paths will be
		 * added (otherwise -L paths are prepended to this list).
		 * Continue to process the remaining path string.
		 */
		if (path) {
			insert_lib = ofl->ofl_ulibdirs.tail;
			*path = '\0';
			++path;
			cp = process_lib_path(&ofl->ofl_ulibdirs, path, FALSE);
			if (cp == (char *)S_ERROR)
				return (S_ERROR);
			else if (cp)
				eprintf(ERR_WARNING,
				    "LD_LIBRARY_PATH malformed");
		}
	}
	/*
	 * Add the default LIBPATH or any -YP supplied path.
	 */
	DBG_CALL(Dbg_libs_yp(Plibpath));
	cp = process_lib_path(&ofl->ofl_dlibdirs, Plibpath, TRUE);
	if (cp == (char *)S_ERROR)
		return (S_ERROR);
	else if (cp) {
		eprintf(ERR_FATAL, "-YP library path malformed");
		return (S_ERROR);
	}
	DBG_CALL(Dbg_libs_init(&ofl->ofl_ulibdirs, &ofl->ofl_dlibdirs));
	return (1);
}
