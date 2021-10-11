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
#pragma ident	"@(#)util.c	1.7	94/06/29 SMI"

/*
 * Utility functions
 */

#include	<stdarg.h>
#include	<unistd.h>
#include	<stdio.h>
#include	<signal.h>
#include	<dlfcn.h>
#include	<errno.h>
#include	"_ld.h"
#include	"debug.h"

static Ofl_desc *	Ofl;
static int (*		_malloc)();
static int (*		_calloc)();
static int (*		_realloc)();
static const char *	nmalloc = "malloc";
static const char *	ncalloc = "calloc";
static const char *	nrealloc = "realloc";

static const char
	* Errmsg_allo = "%s (0x%x) failed: errno=%d",
	* Errmsg_dlsy = "dlsym of `%s' failed: %s";

/*
 * Exit after cleaning up
 */
void
ldexit()
{
	(void) signal(SIGINT, SIG_IGN);
	(void) signal(SIGQUIT, SIG_DFL);
	(void) signal(SIGTERM, SIG_IGN);
	(void) signal(SIGHUP, SIG_DFL);

	/*
	 * If we have created an output file remove it.
	 */
	if (Ofl->ofl_fd > 0)
		(void) unlink(Ofl->ofl_name);
	ld_atexit(EXIT_FAILURE);
	exit(EXIT_FAILURE);
	/* NOTREACHED */
}

/*
 * Print a message to stdout
 */
/* VARARGS2 */
void
eprintf(Error error, const char * format, ...)
{
	int		elferr;		/* to hold libelf error code */
	va_list		args;

	static const char * strings[] = {
		"",		"warning: ",	"fatal: ",	"elf error: "
	};

	if (error > ERR_NONE)
		(void) fputs("ld: ", stderr);
	(void) fputs(strings[error], stderr);

	va_start(args, format);
	(void) vfprintf(stderr, format, args);
	if (error == ERR_ELF)
		if ((elferr = elf_errno()) != 0)
			(void) fprintf(stderr, ": %s ", elf_errmsg(elferr));
	(void) fprintf(stderr, "\n");
	(void) fflush(stderr);
	va_end(args);
}

/*
 * Trap signals so as to call ldexit(), and initialize allocator symbols.
 */
int
init(Ofl_desc * ofl)
{
	/*
	 * Initialize the output file descriptor address for use in the
	 * signal handler routine.
	 */
	Ofl = ofl;

	if (signal(SIGINT, (void (*)(int)) ldexit) == SIG_IGN)
		(void) signal(SIGINT, SIG_IGN);
	if (signal(SIGHUP, (void (*)(int)) ldexit) == SIG_IGN)
		(void) signal(SIGHUP, SIG_IGN);
	if (signal(SIGQUIT, (void (*)(int)) ldexit) == SIG_IGN)
		(void) signal(SIGQUIT, SIG_IGN);

	/*
	 * Obtain the real allocation routines.
	 */
	if ((_malloc = (int (*)())dlsym(RTLD_NEXT, nmalloc)) == NULL) {
		eprintf(ERR_FATAL, Errmsg_dlsy, malloc, dlerror());
		return (S_ERROR);
	}
	if ((_calloc = (int (*)())dlsym(RTLD_NEXT, ncalloc)) == NULL) {
		eprintf(ERR_FATAL, Errmsg_dlsy, calloc, dlerror());
		return (S_ERROR);
	}
	if ((_realloc = (int (*)())dlsym(RTLD_NEXT, nrealloc)) == NULL) {
		eprintf(ERR_FATAL, Errmsg_dlsy, realloc, dlerror());
		return (S_ERROR);
	}
	return (1);
}

/*
 * Define local memory allocators so that we can centralize error messages.
 */
void *
malloc(size_t size)
{
	void *	error;
	if ((error = (void *)(* _malloc)(size)) == 0)
		eprintf(ERR_FATAL, Errmsg_allo, nmalloc, size, errno);
	return ((void *)error);
}

void *
calloc(size_t size, size_t num)
{
	void *	error;
	if ((error = (void *)(* _calloc)(size, num)) == 0)
		eprintf(ERR_FATAL, Errmsg_allo, ncalloc, size, errno);
	return (error);
}

void *
realloc(void * addr, size_t size)
{
	void *	error;
	if ((error = (void *)(* _realloc)(addr, size)) == 0)
		eprintf(ERR_FATAL, Errmsg_allo, nrealloc, size, errno);
	return (error);
}
