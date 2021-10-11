/*
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)support.c	1.3	95/06/20 SMI"

/* LINTLIBRARY */

#include	"_debug.h"

void
Dbg_support_req(const char * define, int flag)
{
	const char *	str;

	if (DBG_NOTCLASS(DBG_SUPPORT))
		return;

	if (flag == DBG_SUP_ENVIRON)
		str = (const char *)"supplied via SGS_SUPPORT";
	else if (flag == DBG_SUP_CMDLINE)
		str = (const char *)"supplied via -S";
	else
		str = (const char *)"default";

	dbg_print(Str_empty);
	dbg_print("support object request=%s  (%s)", define, str);
}

void
Dbg_support_load(const char * obj, const char * func)
{
	if (DBG_NOTCLASS(DBG_SUPPORT))
		return;

	dbg_print("  support object=%s:  provides routine %s", obj, func);
}

void
Dbg_support_action(const char * obj, const char * func, int flag,
    const char * name)
{
	const char *	str;

	if (DBG_NOTCLASS(DBG_SUPPORT))
		return;
	if (DBG_NOTDETAIL())
		return;

	if (flag == DBG_SUP_START)
		str = (const char *)"output file";
	else if (flag == DBG_SUP_FILE)
		str = (const char *)"input file";
	else if (flag == DBG_SUP_SECTION)
		str = (const char *)"input section";

	if (flag == DBG_SUP_ATEXIT)
		dbg_print("  calling routine=%s (%s)", func, obj);
	else
		dbg_print("  calling routine=%s (%s)  %s=%s", func, obj,
		    str, name);
}
