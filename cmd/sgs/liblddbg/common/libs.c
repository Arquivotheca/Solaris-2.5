/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)libs.c	1.12	94/02/28 SMI"

/* LINTLIBRARY */

#include	"paths.h"
#include	"_debug.h"
#include	"libld.h"

static void
Dbg_lib_dir_print(List * libdir)
{
	Listnode *	lnp;
	char *   	cp;

	for (LIST_TRAVERSE(libdir, lnp, cp)) {
		dbg_print("  %s", cp);
	}
}

void
Dbg_libs_init(List * ulibdir, List * dlibdir)
{
	if (DBG_NOTCLASS(DBG_LIBS))
		return;

	dbg_print("Library Search Paths (initial)");
	Dbg_lib_dir_print(ulibdir);
	Dbg_lib_dir_print(dlibdir);
}

void
Dbg_libs_l(const char * name, const char * path)
{
	if (DBG_NOTCLASS(DBG_LIBS))
		return;
	if (DBG_NOTDETAIL())
		return;

	/*
	 * The file name is passed to us as "/libfoo".
	 */
	dbg_print("find lib=-l%s; path=%s", &name[4], path);
}

void
Dbg_libs_path(const char * path)
{
	if (path == (const char *)0)
		return;
	if (DBG_NOTCLASS(DBG_LIBS))
		return;

	dbg_print(" search path=%s  (LD_LIBRARY_PATH)", path);
}

void
Dbg_libs_req(Sdf_desc * sdf, const char * name)
{
	if (DBG_NOTCLASS(DBG_LIBS))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print("find lib=%s; path=%s (required by %s)",
		sdf->sdf_name, name, sdf->sdf_rfile);
}

void
Dbg_libs_update(List * ulibdir, List * dlibdir)
{
	if (DBG_NOTCLASS(DBG_LIBS))
		return;

	dbg_print("Library Search Paths (-L updated)");
	Dbg_lib_dir_print(ulibdir);
	Dbg_lib_dir_print(dlibdir);
}

void
Dbg_libs_yp(const char * path)
{
	if (DBG_NOTCLASS(DBG_LIBS))
		return;

	dbg_print(" search path=%s  (LIBPATH or -YP)", path);
}

void
Dbg_libs_ylu(const char * path, const char * orig, int index)
{
	if (DBG_NOTCLASS(DBG_LIBS))
		return;

	dbg_print(" search path=%s  replaces  path=%s  (-Y%c)", path, orig,
		(index == YLDIR) ? 'L' : 'U');
}

void
Dbg_libs_rpath(const char * name, const char * path)
{
	if (DBG_NOTCLASS(DBG_LIBS))
		return;

	dbg_print(" search path=%s  (RPATH from file %s)", path, name);
}

void
Dbg_libs_dpath(const char * path)
{
	if (DBG_NOTCLASS(DBG_LIBS))
		return;

	dbg_print(" search path=%s  (default)", path);
}

void
Dbg_libs_find(const char * name)
{
	if (DBG_NOTCLASS(DBG_LIBS))
		return;

	dbg_print(Str_empty);
	dbg_print("find library=%s; searching", name);
}

void
Dbg_libs_found(const char * path)
{
	if (DBG_NOTCLASS(DBG_LIBS))
		return;

	dbg_print(" trying path=%s", path);
}

void
Dbg_libs_ignore(const char * path)
{
	if (DBG_NOTCLASS(DBG_LIBS))
		return;

	dbg_print(" ignore path=%s  (insecure directory name)", path);
}
