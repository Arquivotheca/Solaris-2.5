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
#pragma ident	"@(#)debug.c	1.18	95/02/24 SMI"

#include	<sys/types.h>
#include	<sys/stat.h>
#include	<sys/param.h>
#include	<stdio.h>
#include	<fcntl.h>
#include	<stdarg.h>
#include	<dlfcn.h>
#include	<unistd.h>
#include	<errno.h>
#include	<string.h>
#include	<thread.h>
#include	"debug.h"
#include	"_rtld.h"
#include	"_elf.h"

/*
 * Its possible that dbg_setup may be called more than once.
 */
#ifndef	DEBUG

int
dbg_setup(const char * options)
{
	eprintf(ERR_WARNING, "debugging is not enabled; "
	    "recompile with -DDEBUG");
	return (0);
}

#else

static const char
	* Dbg_library =	DBG_LIBRARY,
	* Dbg_setup =	DBG_SETUP,
	* Errmsg_updl =	"debugging disabled; unable to process debug library";

int
dbg_setup(const char * options)
{
	Rt_map *	lmp, * nlmp;
	Sym * 		sym;
	int (*		fptr)();
	int		error;

	/*
	 * If we're running secure only allow debugging if ld.so.1 itself
	 * is owned by root and has its mode setuid.  Fail silently.
	 */
	if (rtld_flags & RT_FL_SECURE) {
		struct stat	status;

		if (stat(NAME(lml_rtld.lm_head), &status) == 0) {
			if ((status.st_uid != 0) ||
			    (!(status.st_mode & S_ISUID)))
				return (0);
		}
	}
	/*
	 * Open the linker debugging library
	 */
	if ((lmp = load_so(&lml_rtld, (char *)Dbg_library,
	    lml_rtld.lm_head)) == 0) {
		eprintf(ERR_WARNING, Errmsg_updl);
		return (0);
	}
	PERMIT(lmp) = perm_set(PERMIT(lmp), PERMIT(lml_rtld.lm_head));
	if (analyze_so(&lml_rtld, lmp, RTLD_NOW, 0) == 0) {
		eprintf(ERR_WARNING, Errmsg_updl);
		return (0);
	}
	if (relocate_so(lmp, RTLD_NOW) == 0) {
		eprintf(ERR_WARNING, Errmsg_updl);
		return (0);
	}

	/*
	 * Obtain the Dbg_setup() symbol
	 */
	if ((sym = LM_LOOKUP_SYM(lmp)((char *)Dbg_setup, PERMIT(lmp), 0,
	    lmp, &nlmp, (LKUP_DEFT | LKUP_FIRST))) == 0) {
		eprintf(ERR_WARNING, Errmsg_updl);
		return (0);
	}

	/*
	 * Call the debugging setup routine to initialize the mask and
	 * debug function array.
	 */
	fptr = (int (*)())((int)sym->st_value + (int)ADDR(nlmp));
	error = (* fptr)(options);

	if (error) {
		Rt_map *	rlmp = lml_rtld.lm_head;
		Rel *		_reladd, * reladd = (Rel *)JMPREL(rlmp);

		/*
		 * Loop through ld.so.1's plt relocations and bind all debugging
		 * functions.  This prevents possible recursion and prevents the
		 * the user from having to see the debugging bindings when they
		 * are trying to investigate their own bindings.
		 */
		for (_reladd = reladd + (PLTRELSZ(rlmp) / sizeof (Rel));
		    reladd < _reladd; reladd++) {
			unsigned long	addr;
			Sym *		sym;
			char *		name;

			sym = (Sym *)((unsigned long)SYMTAB(rlmp) +
				(ELF_R_SYM(reladd->r_info) * SYMENT(rlmp)));
			name = (char *)(STRTAB(rlmp) + sym->st_name);

			addr = reladd->r_offset;
			addr += ADDR(rlmp);

			/*
			 * Fine definition for symbol.
			 */
			if ((sym = LM_LOOKUP_SYM(lmp)(name, PERMIT(lmp), 0, lmp,
			    &nlmp, (LKUP_DEFT | LKUP_FIRST))) != 0) {
				unsigned long	value = sym->st_value;

				if (sym->st_shndx != SHN_ABS)
					value += ADDR(nlmp);

				/*
				 * Perform the necessary relocation (either a
				 * plt or got update).
				 */
				elf_plt_write((unsigned long *)addr,
				    (unsigned long *)value);
			}
		}
	}
	return (error);
}

/* VARARGS1 */
void
dbg_print(const char * format, ...)
{
	va_list			args;
	int			len;
	static char *		buffer;
	static int		buf_len;
	static int		fd = 0;
	static dev_t		dev;
	static ino_t		ino;
	struct stat		status;
	static int		thread_id = -1;

	/*
	 * The first time here.
	 */
	if (fd == 0) {
		/*
		 * Allocate a buffer for the diagnostic message to be built in.
		 * Initialize the buffer with the process id.
		 */
		if ((buffer = (char *)malloc(ERRSIZE)) == 0) {
			dbg_mask = 0;
			return;
		}
		(void) sprintf(buffer, "%5.5d: ", getpid());

		if (dbg_file) {
			/*
			 * If an LD_DEBUG_OUTPUT file was specified then we need
			 * to direct all diagnostics to the specified file.  Add
			 * the process id as a file suffix so that multiple
			 * processes that inherit the same debugging environment
			 * variable don't fight over the same file.
			 */
			char 	file[MAXPATHLEN];

			(void) sprintf(file, "%s.%5.5d", dbg_file, getpid());
			if ((fd = open(file, (O_RDWR | O_CREAT), 0666)) == -1) {
				eprintf(ERR_WARNING, Errmsg_cofl, file, errno);
				dbg_mask = 0;
				return;
			}
		} else {
			/*
			 * The default is to direct debugging to the stderr.
			 */
			fd = 2;
		}

		/*
		 * Initialize the dev/inode pair to enable us to determine if
		 * the debugging file descriptor is still available once the
		 * application has been entered.
		 */
		(void) fstat(fd, &status);
		dev = status.st_dev;
		ino = status.st_ino;
	}

	/*
	 * If we're in the application make sure the debugging file descriptor
	 * is still available (ie, the user hasn't closed and/or reused the
	 * same descriptor).
	 */
	if (rtld_flags & RT_FL_APPLIC) {
		if ((fstat(fd, &status) == -1) || (status.st_dev != dev) ||
		    (status.st_ino != ino)) {
			if (dbg_file) {
				/*
				 * If the user specified output file has been
				 * disconnected try and reconnect to it.
				 */
				char 	file[MAXPATHLEN];

				(void) sprintf(file, "%s.%5.5d", dbg_file,
				    getpid());
				if ((fd = open(file, (O_RDWR | O_APPEND),
				    0)) == -1) {
					dbg_mask = 0;
					return;
				}
				(void) fstat(fd, &status);
				dev = status.st_dev;
				ino = status.st_ino;
			} else {
				/*
				 * If stderr has been stolen from us simply
				 * turn debugging off.
				 */
				dbg_mask = 0;
				return;
			}
		}
	}


	if (rtld_flags & RT_FL_THREADS) {
		int _thread_id =	thr_self();

		if (_thread_id != thread_id) {
			(void) sprintf(&buffer[6], "%d: ", _thread_id);
			buf_len = strlen(buffer);
			thread_id = _thread_id;
		}
	} else {
		/*
		 * If threads had previously been enabled then we
		 * need to reset the buffer to its non-threaded state.
		 */
		if (thread_id != -1) {
			(void) sprintf(buffer, "%5.5d: ", getpid());
			thread_id = -1;
		}
		buf_len = 7;
	}
	/*
	 * Format the message and print it.
	 */
	va_start(args, format);
	len = doprf(format, args, &buffer[buf_len]);
	len += buf_len;
	buffer[len++] = '\n';
	(void) write(fd, buffer, len);
	va_end(args);
}

#endif
