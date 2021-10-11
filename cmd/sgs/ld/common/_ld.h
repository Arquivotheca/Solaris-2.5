/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1991 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)_ld.h	1.15	95/06/20 SMI"

/*
 * Global include file for ld library.
 */
#ifndef	_LD_DOT_H
#define	_LD_DOT_H

#include	<stdlib.h>
#include	<libelf.h>
#include	"libld.h"

#define	DEBUG

#define	SYM_NBKTS	1009

/*
 * Data declarations.
 */
extern const char *	Fini_usym;
extern const char *	Init_usym;
extern const char *	Libv_usym;

extern const char *	Errmsg_file;
extern const char *	Errmsg_eupd;

extern char *		Plibpath;
extern char *		Llibdir;
extern char *		Ulibdir;
extern Listnode *	insert_lib;

extern List		lib_support;

/*
 * Function Declarations
 */
extern int		add_libdir(Ofl_desc *, const char *);
extern int		dbg_setup(const char *);
extern void		ent_check(Ofl_desc *);
extern int		find_library(const char *, Ofl_desc *);
extern int		lib_setup(Ofl_desc *);
extern int		init(Ofl_desc *);
extern void		ldexit(void);
extern void		ldmap_out(Ofl_desc *);
extern int		map_parse(const char *, Ofl_desc *);
extern void		map_segorder_check(Ofl_desc *);
extern int		prepend_argv(char *, int *, char ***);
extern int		process_flags(Ofl_desc *, int, char **);
extern int		process_files(Ofl_desc *, int, char **);
extern int		sort_seg_list(Ofl_desc *);

/*
 * The following prevent us from having to include ctype.h which defines these
 * functions as macros which reference the __ctype[] array.  Go through .plt's
 * to get to these functions in libc rather than have every invocation of ld
 * have to suffer the R_SPARC_COPY overhead of the __ctype[] array.
 */
extern int		isascii(int);
extern int		isdigit(int);
extern int		isprint(int);
extern int		isspace(int);
extern int		tolower(int);

#endif
