/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code
 *
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)ldd.c	1.18	95/07/27 SMI"

/*
 * Print the list of shared objects required by a dynamic executable or shared
 * object.
 *
 * usage is: ldd [-d | -r] [-f] [-s] [-v] file(s)
 *
 * ldd opens the file and verifies the information in the elf header.
 * If the file is a dynamic executable, we set up some environment variables
 * and exec(2) the file.  If the file is a shared object, we preload the
 * file with a dynamic executable stub. The runtime linker (ld.so.1) actually
 * provides the diagnostic output, according to the environment variables set.
 *
 * If neither -d nor -r is specified, we set only LD_TRACE_LOADED_OBJECTS_[AE].
 * The runtime linker will print the pathnames of all dynamic objects it
 * loads, and then exit.  Note that we distiguish between ELF and AOUT objects
 * when setting this environment variable - AOUT executables cause the mapping
 * of sbcp, the dependencies of which the user isn't interested in.
 *
 * If -d or -r is specified, we also set LD_WARN=1; the runtime linker will
 * perform its normal relocations and issue warning messages for unresolved
 * references. It will then exit.
 * If -r is specified, we set LD_BIND_NOW=1, so that the runtime linker
 * will perform all relocations, otherwise (under -d) the runtime linker
 * will not perform PLT (function) type relocations.
 *
 * If -f is specified, we will run ldd as root on executables that have
 * an unsercure runtime linker that does not live under the "/usr/lib"
 * directory.  By default we will not let this happen.
 *
 * If -s is specified we also set the LD_TRACE_SEARCH_PATH=1, thus enabling
 * the runtime linker to indicate the search algorithm used.
 *
 * If -v is specified we also set the LD_VERBOSE=1, thus enabling the runtime
 * linker to indicate all object dependencies (not just the first object
 * loaded) together with any versionig requirements.
 */
#include	<fcntl.h>
#include	<stdio.h>
#include	<string.h>
#include	<libelf.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<wait.h>
#include	"machdep.h"
#include	"paths.h"
#include	"a.out.h"

static int	elf_check(int, char *, char *, Elf *, int);
static int	aout_check(int, char *, char *, int, int);
static int	run(int, char *, char *, char *);

static const char
	* Errmsg_usag = "usage: %s [-d | -r] [-f] [-s] [-v] file(s)\n",
	* Errmsg_cate = "%s: can't add to environment, putenv(3C) failed\n",
	* Errmsg_fnds = "%s: %s: file is not a dynamic executable or "
			"shared object\n",
	* Errmsg_uouf =	"%s: %s: unsupported or unknown file type\n",
	* Errmsg_efai =	"%s: %s: execution failed ",
	* Errmsg_euid = "%s: %s: file has insecure interpreter %s\n",
	* Errmsg_auid = "%s: %s: insecure a.out file\n",
	* Errmsg_mall =	"%s: malloc failed\n",
	* Lddstub =	"/usr/lib/lddstub";

/*
 * The following size definitions provide for allocating space for the string,
 * or the string position at which any modifications to the variable will occur.
 */
#define	LD_PRELOAD_SIZE		11
#define	LD_LOAD_SIZE		27
#define	LD_PATH_SIZE		23
#define	LD_BIND_SIZE		13
#define	LD_VERB_SIZE		12
#define	LD_WARN_SIZE		9

static const char
	* prefile =	"",
	* preload =	"LD_PRELOAD=",
	* prestr =	"LD_PRELOAD=";

static char
	* bind =	"LD_BIND_NOW= ",
	* load_elf =	"LD_TRACE_LOADED_OBJECTS_E= ",
	* load_aout =	"LD_TRACE_LOADED_OBJECTS_A= ",
	* path =	"LD_TRACE_SEARCH_PATHS= ",
	* verb =	"LD_VERBOSE= ",
	* warn =	"LD_WARN= ",
	* load;


main(int argc, char ** argv)
{
	char *	fname, * cname = argv[0];

	Elf *	elf;
	int	dflag = 0, fflag = 0, rflag = 0, sflag = 0, vflag = 0;
	int	nfile, var, error = 0;

	/*
	 * verify command line syntax and process arguments
	 */
	opterr = 0;				/* disable getopt error mesg */
	while ((var = getopt(argc, argv, "dfrsv")) != EOF) {
		switch (var) {
		case 'd' :			/* perform data relocations */
			dflag = 1;
			if (rflag)
				error++;
			break;
		case 'f' :
			fflag = 1;
			break;
		case 'r' :			/* perform all relocations */
			rflag = 1;
			if (dflag)
				error++;
			break;
		case 's' :			/* enable search path output */
			sflag = 1;
			break;
		case 'v' :			/* enable verbose output */
			vflag = 1;
			break;
		default :
			error++;
			break;
		}
		if (error)
			break;
	}
	if (error) {
		(void) fprintf(stderr, Errmsg_usag, cname);
		exit(1);
	}

	/*
	 * Determine if LD_PRELOAD is already set, if so we'll continue to
	 * analyze each object with this setting.
	 */
	if ((fname = getenv("LD_PRELOAD")) != 0) {
		prefile = fname;
		if ((fname = (char *)malloc(strlen(prefile) +
		    LD_PRELOAD_SIZE + 1)) == 0) {
			(void) fprintf(stderr, Errmsg_mall, cname);
			exit(1);
		}
		(void) sprintf(fname, "%s%s", preload, prefile);
		prestr = fname;
	}

	/*
	 * Set the appropriate relocation environment variables (Note unsetting
	 * the environment variables is done just in case the user already
	 * has these in their environment ... sort of thing the test folks
	 * would do :-)
	 */
	warn[LD_WARN_SIZE - 1] = (dflag || rflag) ? '1' : '\0';
	bind[LD_BIND_SIZE - 1] = (rflag) ? '1' : '\0';
	path[LD_PATH_SIZE - 1] = (sflag) ? '1' : '\0';
	verb[LD_VERB_SIZE - 1] = (vflag) ? '1' : '\0';

	if ((putenv(warn) != 0) || (putenv(bind) != 0) || (putenv(path) != 0) ||
	    (putenv(verb) != 0)) {
		(void) fprintf(stderr, Errmsg_cate, cname);
		exit(1);
	}

	/*
	 * coordinate libelf's version information
	 */
	if (elf_version(EV_CURRENT) == EV_NONE) {
		(void) fprintf(stderr, "%s: libelf is out of date\n", cname);
		exit(1);
	}

	/*
	 * Loop through remaining arguments.  Note that from here on there
	 * are no exit conditions so that we can process a list of files,
	 * any error condition is retained for a final exit status.
	 */
	nfile = argc - optind;
	for (; optind < argc; optind++) {
		fname = argv[optind];
		/*
		 * Open file (do this before checking access so that we can
		 * provide the user with better diagnostics).
		 */
		if ((var = open(fname, O_RDONLY)) == -1) {
			(void) fprintf(stderr, "%s: ", cname);
			perror(fname);
			error = 1;
			continue;
		}

		/*
		 * Get the files elf descriptor and process it as an elf or
		 * a.out (4.x) file.
		 */
		elf = elf_begin(var, ELF_C_READ, (Elf *)0);
		switch (elf_kind(elf)) {
		case ELF_K_AR :
			(void) fprintf(stderr, Errmsg_fnds, cname, fname);
			error = 1;
			break;
		case ELF_K_COFF:
			(void) fprintf(stderr, Errmsg_uouf, cname, fname);
			error = 1;
			break;
		case ELF_K_ELF:
			if (elf_check(nfile, fname, cname, elf, fflag) != NULL)
				error = 1;
			break;
		default:
			/*
			 * This is either an unknown file or an aout format
			 */
			if (aout_check(nfile, fname, cname, var, fflag) != NULL)
				error = 1;
			break;
		}
		(void) elf_end(elf);
		(void) close(var);
	}
	return (error);
}

static int
elf_check(int nfile, char * fname, char * cname, Elf * elf, int fflag)
{
	Ehdr *	ehdr;
	Phdr *	phdr;
	int	dynamic, cnt;

	/*
	 * verify information in file header
	 */
	if ((ehdr = elf_getehdr(elf)) == (Ehdr *)0) {
		(void) fprintf(stderr, "%s: %s: can't read ELF header\n",
			cname, fname);
		return (1);
	}

	/*
	 * check class and encoding
	 */
	if (ehdr->e_ident[EI_CLASS] != M_CLASS ||
	    ehdr->e_ident[EI_DATA] != M_DATA) {
		(void) fprintf(stderr, "%s: %s: has wrong class or data "
			"encoding\n", cname, fname);
		return (1);
	}

	/*
	 * check type
	 */
	if ((ehdr->e_type != ET_EXEC) && (ehdr->e_type != ET_DYN)) {
		(void) fprintf(stderr, "%s: %s: bad magic number\n",
			cname, fname);
		return (1);
	}
	if (ehdr->e_machine != M_MACH) {
		if (ehdr->e_machine != M_MACHPLUS) {
			(void) fprintf(stderr, "%s: %s: wrong machine type\n",
				cname, fname);
			return (1);
		}
		if ((ehdr->e_flags & M_FLAGSPLUS) == 0) {
			(void) fprintf(stderr, "%s: %s: wrong machine flags\n",
				cname, fname);
			return (1);
		}
	}

	/*
	 * Check that the file is executable.  Dynamic executables must be
	 * executable to be exec'ed.  Shared objects need not be executable to
	 * be mapped with a dynamic executable, however, by convention they're
	 * supposed to be executable.
	 */
	if (access(fname, X_OK) != 0) {
		if (ehdr->e_type == ET_EXEC) {
			(void) fprintf(stderr, "%s: %s: is not executable\n",
				cname, fname);
			return (1);
		}
		(void) fprintf(stderr, "warning: %s: %s: is not executable\n",
			cname, fname);
	}

	/*
	 * read program header and check for dynamic section and
	 * interpreter
	 */
	if ((phdr = elf_getphdr(elf)) == (Phdr *)0) {
		(void) fprintf(stderr, "%s: %s: can't read program header\n",
			cname, fname);
		return (1);
	}

	for (dynamic = 0, cnt = 0; cnt < (int)ehdr->e_phnum; cnt++) {
		if (phdr->p_type == PT_DYNAMIC) {
			dynamic = 1;
			break;
		}

		/*
		 * If fflag is not set, and euid == root, and the interpreter
		 * does not live under /usr/lib or /etc/lib then don't allow
		 * ldd to execute the image.  This prevents someone creating a
		 * `trojan horse' by substituting their own interpreter that
		 * could preform privileged operations when ldd is against it.
		 */
		if (!fflag && (phdr->p_type == PT_INTERP) && (geteuid() == 0)) {
			/*
			 * Does the interpreter live under a trusted directory.
			 */
			char * interpreter = (char *)ehdr + phdr->p_offset;

			if ((strncmp(interpreter, LIBDIR, LIBDIRLEN) != 0) &&
			    (strncmp(interpreter, ETCDIR, ETCDIRLEN) != 0)) {
				(void) fprintf(stderr, Errmsg_euid,
					cname, fname, interpreter);
				return (1);
			}
		}
		phdr = (Phdr *)((unsigned long)phdr + ehdr->e_phentsize);
	}
	if (!dynamic) {
		(void) fprintf(stderr, Errmsg_fnds, cname, fname);
		return (1);
	}

	load = load_elf;

	/*
	 * Run the required program.
	 */
	if (ehdr->e_type == ET_DYN)
		return (run(nfile, cname, fname, (char *)Lddstub));
	else
		return (run(nfile, cname, fname, fname));
}

static int
aout_check(int nfile, char * fname, char * cname, int fd, int fflag)
{
	struct exec	aout;

	if (lseek(fd, 0, SEEK_SET) != 0) {
		(void) fprintf(stderr, "%s: %s: can't lseek\n", cname, fname);
		return (1);
	}
	if (read(fd, (char *)&aout, sizeof (struct exec)) !=
	    sizeof (struct exec)) {
		(void) fprintf(stderr, "%s: \n", cname);
		perror(fname);
		return (1);
	}
	if (aout.a_machtype != M_SPARC) {
		(void) fprintf(stderr, Errmsg_uouf, cname, fname);
		return (1);
	}
	if (N_BADMAG(aout) || !aout.a_dynamic) {
		(void) fprintf(stderr, Errmsg_fnds, cname, fname);
		return (1);
	}
	if (!fflag && (geteuid() == 0)) {
		(void) fprintf(stderr, Errmsg_auid, cname, fname);
		return (1);
	}

	/*
	 * Run the required program.
	 */
	if ((aout.a_magic == ZMAGIC) &&
	    (aout.a_entry <= sizeof (struct exec))) {
		load = load_elf;
		return (run(nfile, cname, fname, (char *)Lddstub));
	} else {
		load = load_aout;
		return (run(nfile, cname, fname, fname));
	}
}


/*
 * Run the required program, setting the preload and trace environment
 * variables accordingly.
 */
static int
run(int nfile, char * cname, char * fname, char * ename)
{
	char *		str;
	char		ndx;
	int		pid, status;
	const char *	format = "%s./%s %s";

	if (fname != ename) {
		for (str = fname; *str; str++)
			if (*str == '/') {
				format = (const char *)"%s%s %s";
				break;
		}
		if ((str = (char *)malloc(strlen(fname) +
		    strlen(prefile) + LD_PRELOAD_SIZE + 4)) == 0) {
			(void) fprintf(stderr, Errmsg_mall, cname);
			exit(1);
		}

		/*
		 * When using ldd(1) to analyze a shared object we preload the
		 * shared object with lddstub.  Any additional preload
		 * requirements are added after the object being analyzed, this
		 * allows us to skip the first object but produce diagnostics
		 * for each other preloaded object.
		 */
		(void) sprintf(str, format, preload, fname, prefile);
		ndx = '2';

		if (putenv(str) != 0) {
			(void) fprintf(stderr, Errmsg_cate, cname);
			return (1);
		}
	} else
		ndx = '1';

	if ((pid = fork()) == -1) {
		(void) fprintf(stderr, "%s: ", cname);
		perror(fname);
		return (1);
	}

	if (pid) {				/* parent */
		while (wait(&status) != pid)
			;
		if (WIFSIGNALED(status)) {
			(void) fprintf(stderr, Errmsg_efai, cname, fname);
			(void) fprintf(stderr, "due to signal %d %s\n",
				(WSIGMASK & status), ((status & WCOREFLG) ?
				(const char *)"(core dumped)" : ""));
			status = 1;
		} else if (WHIBYTE(status)) {
			(void) fprintf(stderr, Errmsg_efai, cname, fname);
			(void) fprintf(stderr, "with exit status %d\n",
				WHIBYTE(status));
			status = 1;
		}
	} else {				/* child */
		load[LD_LOAD_SIZE - 1] = ndx;
		if (putenv(load) != 0) {
			(void) fprintf(stderr, Errmsg_cate, cname);
			return (1);
		}

		if (nfile > 1)
			(void) printf("%s:\n", fname);
		(void) fflush(stdout);
		if ((execl(ename, ename, (char *)0)) == -1) {
			(void) fprintf(stderr, Errmsg_efai, cname, fname);
			perror(ename);
			_exit(0);
			/* NOTREACHED */
		}
	}

	/*
	 * If there is more than one filename to process make sure the
	 * preload environment variable is reset (this makes sure we remove
	 * any preloading that had been established to process a shared object).
	 */
	if ((nfile > 1) && (fname != ename)) {
		if (putenv((char *)prestr) != 0) {
			(void) fprintf(stderr, Errmsg_cate, cname);
			return (1);
		}
		free(str);
	}
	return (status);
}
