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
#pragma ident	"@(#)globals.c	1.28	95/05/15 SMI"

#include	<signal.h>
#include	<dlfcn.h>
#include	<synch.h>
#include	"_rtld.h"
#include	"paths.h"

/*
 * Declarations of global variables used in ld.so.
 */
int		bind_mode =	RTLD_LAZY;
rwlock_t	bindlock =	DEFAULTRWLOCK;
rwlock_t	malloclock =	DEFAULTRWLOCK;
rwlock_t	printlock =	DEFAULTRWLOCK;
rwlock_t	boundlock =	DEFAULTRWLOCK;
mutex_t *	profilelock =	0;

/*
 * global variable which holds the current version of the
 * ld_concurrency interface that was passed in via libthread
 */
int		lc_version = 0;

Lm_list		lml_main =	{ 0, 0 };	/* the `main's link map list */
Lm_list		lml_rtld =	{ 0, 0 };	/* rtld's link map list */

struct r_debug r_debug = {
	1,				/* version no. */
	0,
	(unsigned long)r_debug_state,
	RT_CONSISTENT,
	0
};

const char *	pr_name = "(unknown)";	/* Initialize the process name */
					/*	incase exec() is called with */
					/*	a null argv[0] specified. */
const char *	rt_name;		/* the run time linkers name */
char *		lasterr = (char *)0;	/* string describing last error */
					/*	cleared by each dlerror() */
Interp *	interp = 0;		/* ELF interpreter info */
Fmap *		fmap = 0;		/* Initial file mapping info */
List		preload = { 0, 0 };	/* LD_PRELOAD objects */
const char *	envdirs = 0;		/* LD_LIBRARY_PATH and its */
Pnode *		envlist = 0;		/*	associated Pnode list */
size_t		syspagsz = 0;		/* system page size */
unsigned long	flags = 0;		/* machine specific file flags */
int		tracing = 0;		/* tracing loaded objects? */
Rel_copy *	copies = 0;		/* copy relocation records */
char *		platform = 0;		/* platform name from AT_SUN_PLATFORM */
int		rtld_flags = 0;		/* status flags for RTLD */

const char *	ldso_path = LDSO_PATH;
const char *	ldso_name = LDSO_NAME;

#ifdef DEBUG
int		dbg_mask;		/* debugging classes */
const char * 	dbg_file = 0;		/* debugging directed to file */
#endif


/*
 * Global error message diagnostics (routed via eprintf()).  These are normally
 * provided when both an elf and a.out file required the same message.
 */
const char
	* Errmsg_cmdz = "can't map /dev/zero: errno=%d",
	* Errmsg_cmfl = "%s: can't map file: errno=%d",
	* Errmsg_cmsg = "%s: can't map segment: errno=%d",
	* Errmsg_cofl = "%s: can't open file: errno=%d",
	* Errmsg_cotf = "%s: corrupt or truncated file",
	* Errmsg_csps = "%s: can't set protections on segment: errno=%d",
	* Errmsg_rupp =	"relocation error: unable to process PLT entry "
			"at 0x%x 0x%x: ",
	* Errmsg_rupr =	"unidentifiable procedure reference",
	* Errmsg_rbeo =	"bad entry offset",
	* Errmsg_rirt =	"relocation error: invalid type %d at 0x%x: "
			"referenced in %s",
	* Errmsg_rsnf =	"relocation error: symbol not found: %s: "
			"referenced in %s",
	* Errmsg_unft =	"%s: unknown file type";

/*
 * Internal error messages.  Here we don't format any arguments but simply get
 * the string out as soon as possible.
 */
const char
	* Intmsg_relo = "ld.so.1: internal: invalid relocation type "
			"(non relative/jmp relocation)\n";

/*
 * ldd(1) influenced message diagnostics (routed via printf()).
 */
const char
	* Lddmsg_fndl =	"\n   find library=%s; required by %s\n",
	* Lddmsg_lflp =	"\t%s\n",
	* Lddmsg_lequ =	"\t%s =>\t %s\n",
	* Lddmsg_rsnf =	"\tsymbol not found: %s\t\t(%s)\n";
