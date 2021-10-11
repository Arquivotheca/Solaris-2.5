/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)args.c	1.37	95/06/20 SMI"

/*
 * Processes the command line flags.  The options recognized by ld are the
 * following:
 *
 *	OPTION			MEANING
 *
 *	-a { -dn only } make the output file executable
 *
 *	-b { -dy only }	turn off special handling for PIC/non-PIC relocations
 *
 *	-dy		dynamic mode: build a dynamically linked executable or
 *			a shared object - build a dynamic structure in the
 *			output file and make that file's symbols available for
 *			run-time linking
 *
 *	-dn		static mode: build a statically linked executable or a
 *			relocatable object file
 *
 *	-e name		make name the new entry point (e_entry)
 *
 *	-F name		specify that this files symbol table is acting as a
 *			filter on the symbol table of the shared object name
 *
 *	-f name		specify that this files symbol table is acting as a
 *			auxiliary filter on the symbol table of the shared
 *			object name
 *
 *	-h name { -dy -G only }
 *			make name the output filename in the dynamic structure
 *
 *	-i		ignore any LD_LIBRARY_PATH setting.
 *
 *	-I name		make name the interpreter pathname written into the
 *			program execution header of the output file
 *
 *	-lx		search for libx.[so|a] using search directories
 *
 *	-m		generate a memory map and show multiply defined symbols
 *
 *	-o name		use name as the output filename
 *
 *	-r { -dn only }	retain relocation in the output file (ie. produce a
 *			relocatable object file)
 *
 *	-R path		specify a library search path for use at run time.
 *
 *	-s		strip the debugging sections and their associated
 *			relocations from the output file
 *
 *	-t		turnoff warnings about multiply-defined symbols that
 *			are not the same size
 *
 *	-u name		make name an undefined entry in the ld symbol table
 *
 *	-z text { -dy only }
 *			issue a fatal error if any text relocations remain
 *
 *	-z defs | nodefs
 *			issue a fatal error | don't, if undefined symbols remain
 *
 *	-z muldefs 	multiply defined symbols are allowable
 *
 *	-z noversion	don't record versioning sections
 *
 *	-B reduce	reduce symbols if possible
 *
 *	-B static	in searching for libx, choose libx.a
 *
 *	-B dynamic { -dy only }
 *			in searching for libx, choose libx.so
 *
 *	-B symbolic { -dy -G }
 *			shared object symbol resolution flag ...
 *
 *	#ifdef	DEBUG
 *	-D option1,option2,...
 *			turn on debugging for each indicated option
 *	#endif
 *
 *	-G { -dy }	produce a shared object
 *
 *	-L path		prepend path to library search path
 *
 *	-M name		read a mapfile (if name is a dir, read all files in dir)
 *
 *	-Q[y|n]		add|do not add ld version to comment section of output
 *			file
 *
 *	-S name		dlopen(name) a support library
 *
 * 	-V		print ld version to stderr
 *
 *	-YP path	change LIBPATH to path
 *
 *	-YL path	change YLDIR (1st) part of LIBPATH to path
 *			(undocumented)
 *
 *	-YU path	change YUDIR (2nd) part of LIBPATH to path
 *			(undocumented)
 */
#include	<stdio.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	<string.h>
#include	<errno.h>
#include	"debug.h"
#include	"_ld.h"

static const char	/* CSTYLED */
	* Options     = "abd:e:f:h:il:mo:rstu:z:B:D:F:GI:L:M:Q:R:S:VY:",
	* Errmsg_bade = "building a dynamic executable",
	* Errmsg_oain =	"option %s and %s are incompatible",
	* Errmsg_oamo =	"option %s appears more than once, "
			"first setting taken",
	* Errmsg_ohia =	"option %s has illegal argument `%s'",
	* Errval_zdef = "-zdefs/nodefs";

/*
 * Define a set of local argument flags, the settings of these will be
 * verified in check_flags() and lead to the appropriate output file flags
 * being initialized.
 */
typedef	enum {
	SET_UNKNOWN = -1,
	SET_FALSE = 0,
	SET_TRUE = 1
} Setstate;

static Setstate	dflag	= SET_UNKNOWN;
static Setstate	zdflag	= SET_UNKNOWN;
static Setstate	Qflag	= SET_UNKNOWN;

static Boolean	aflag	= FALSE;
static Boolean	bflag	= FALSE;
static Boolean	files	= FALSE;
static Boolean	rflag	= FALSE;
static Boolean	sflag	= FALSE;
static Boolean	zmflag	= FALSE;
static Boolean	ztflag	= FALSE;
static Boolean	zvflag	= FALSE;
static Boolean	Bsflag	= FALSE;
static Boolean	Blflag	= FALSE;
static Boolean	Brflag	= FALSE;
static Boolean	Gflag	= FALSE;
static Boolean	Vflag	= FALSE;

/*
 * Prepend environment string as a series of options to the argv array.
 */
int
prepend_argv(char * ld_options, int * argcp, char *** argvp)
{
	int	nargc;			/* New argc */
	char **	nargv;			/* New argv */
	char *	arg, * string;
	int	count;

	/*
	 * Get rid of leading white space, and make sure the string has size.
	 */
	while (isspace(*ld_options))
		ld_options++;
	if (*ld_options == '\0')
		return (1);

	nargc = 0;
	arg = string = ld_options;
	/*
	 * Walk the environment string counting any arguments that are
	 * separated by white space.
	 */
	while (*string != '\0') {
		if (isspace(*string)) {
			nargc++;
			while (isspace(*string))
				string++;
			arg = string;
		} else
			string++;
	}
	if (arg != string)
		nargc++;

	/*
	 * Allocate a new argv array big enough to hold the new options
	 * from the environment string and the old argv options.
	 */
	if ((nargv = (char **)calloc(nargc + *argcp, sizeof (char *))) == 0)
		return (S_ERROR);

	/*
	 * Initialize first element of new argv array to be the first
	 * element of the old argv array (ie. calling programs name).
	 * Then add the new args obtained from the environment.
	 */
	nargv[0] = (*argvp)[0];
	nargc = 0;
	arg = string = ld_options;
	while (*string != '\0') {
		if (isspace(*string)) {
			nargc++;
			*string++ = '\0';
			nargv[nargc] = arg;
			while (isspace(*string))
				string++;
			arg = string;
		} else
			string++;
	}
	if (arg != string) {
		nargc++;
		nargv[nargc] = arg;
	}

	/*
	 * Now add the original argv array (skipping argv[0]) to the end of
	 * the new argv array, and overwrite the old argc and argv.
	 */
	for (count = 1; count < *argcp; count++) {
		nargc++;
		nargv[nargc] = (*argvp)[count];
	}
	*argcp = ++nargc;
	*argvp = nargv;

	return (1);
}

/*
 * print usage message to stderr - 2 modes, summary message only,
 * and full usage message
 */
static void
usage_mesg(Boolean detail)
{
	(void) fprintf(stderr, "usage: %sld [-%s] file(s)\n",
	    (const char *)SGS, Options);

	if (detail == FALSE)
	    return;

	(void) fprintf(stderr,
	    "\t[-a]\t\tcreate an absolute file\n"
	    "\t[-b]\t\tdo not do special PIC relocations in a.out\n"
	    "\t[-d y|n]\toperate in dynamic|static mode\n"
	    "\t[-e sym]\tuse `sym' as entry point address\n"
	    "\t[-f name]\tspecify library for which this file is an auxiliary\n"
	    "\t\t\tfilter\n"
	    "\t[-h name]\tuse `name' as internal shared object identifier\n"
	    "\t[-i]\t\tignore LD_LIBRARY_PATH setting\n"
	    "\t[-l x]\t\tsearch for libx.so or libx.a\n"
	    "\t[-m]\t\tprint memory map\n"
	    "\t[-o outfile]\tname the output file `outfile'\n"
	    "\t[-r]\t\tcreate a relocatable object\n"
	    "\t[-s]\t\tstrip any symbol and debugging information\n"
	    "\t[-t]\t\tdo not warn of multiply defined symbols of\n"
	    "\t\t\tdifferent sizes\n"
	    "\t[-u sym]\tcreate an undefined symbol `sym'\n"
	    "\t[-z defs|nodefs]\n"
	    "\t\t\tdisallow|allow undefined symbols\n"
	    "\t[-z muldefs]\tallow multiply defined symbols\n"
	    "\t[-z noversion]\tdon't record any version sections\n"
	    "\t[-z text]\tdisallow output relocations against text\n"
	    "\t[-B local]\treduce unqualified global symbols to local\n"
	    "\t[-B dynamic|static]\n"
	    "\t\t\tsearch for shared libraries|archives\n"
	    "\t[-B reduce]\tprocess symbol reductions\n"
	    "\t[-B symbolic]\tbind external references to definitions when\n"
	    "\t\t\tcreating shared objects\n"
	    "\t[-D options]\tprint diagnostic messages\n"
	    "\t[-F name]\tspecify library for which this file is a filter\n"
	    "\t[-G]\t\tcreate a shared object\n"
	    "\t[-I interp]\tuse `interp' as path name of interpreter\n"
	    "\t[-L path]\tsearch for libraries in directory `path'\n"
	    "\t[-M mapfile]\tuse processing directives contained in `mapfile'\n"
	    "\t[-Q y|n]\tdo|do not place version information in output file\n"
	    "\t[-R path]\tspecify a library search path to be used at run "
	    "time\n"
	    "\t[-S name]\tspecify a link-edit support library\n"
	    "\t[-V]\t\tprint version information\n"
	    "\t[-Y P,dirlist]\tuse `dirlist' as a default path when "
	    "searching\n\t\t\tfor libraries\n");
}

/*
 * Checks the command line option flags for consistency.
 */
static int
check_flags(Ofl_desc * ofl, int argc)
{
	Word	flags = 0;

	if (Plibpath && (Llibdir || Ulibdir)) {
		eprintf(ERR_FATAL,
		    "option -YP and -Y%c may not be specified concurrently",
		    Llibdir ? 'L' : 'U');
		flags |= FLG_OF_FATAL;
	}

	if (rflag) {
		if (dflag == SET_TRUE) {
			eprintf(ERR_FATAL, Errmsg_oain,
				(const char *) "-dy", (const char *) "-r");
			flags |= FLG_OF_FATAL;
		}
		dflag = SET_FALSE;
	}

	if (zdflag == SET_TRUE)
		flags |= FLG_OF_NOUNDEF;

	if (zmflag)
		flags |= FLG_OF_MULDEFS;

	if (sflag)
		flags |= FLG_OF_STRIP;

	if (Qflag == SET_TRUE)
		flags |= FLG_OF_ADDVERS;

	if (Blflag)
		flags |= FLG_OF_AUTOLCL;

	if (Brflag)
		flags |= FLG_OF_PROCRED;

	if (dflag != SET_FALSE) {
		/*
		 * -Bdynamic on by default, setting is rechecked as input
		 * files are processed.
		 */
		flags |= (FLG_OF_DYNAMIC | FLG_OF_DYNLIBS);

		if (aflag) {
			eprintf(ERR_FATAL, Errmsg_oain,
				(const char *) "-dy", (const char *) "-a");
			flags |= FLG_OF_FATAL;
		}

		if (zvflag)
			flags |= FLG_OF_NOVERSEC;

		if (bflag)
			flags |= FLG_OF_BFLAG;

		if (ztflag)
			flags |= FLG_OF_PURETXT;

		if (!Gflag) {
			/*
			 * Dynamically linked executable.
			 */
			flags |= FLG_OF_EXEC;

			if (zdflag != SET_FALSE)
				flags |= FLG_OF_NOUNDEF;

			if (Bsflag) {
				eprintf(ERR_FATAL, Errmsg_oain, (const char *)
					"-Bsymbolic", Errmsg_bade);
				flags |= FLG_OF_FATAL;
			}
			if (ofl->ofl_soname) {
				eprintf(ERR_FATAL, Errmsg_oain, (const char *)
					"-h", Errmsg_bade);
				flags |= FLG_OF_FATAL;
			}
			if (ofl->ofl_filter) {
				if (ofl->ofl_flags & FLG_OF_AUX) {
					eprintf(ERR_FATAL, Errmsg_oain,
						(const char *) "-f",
						Errmsg_bade);
				} else {
					eprintf(ERR_FATAL, Errmsg_oain,
						(const char *) "-F",
						Errmsg_bade);
				}
				flags |= FLG_OF_FATAL;
			}
		} else {
			/*
			 * Shared library.
			 */
			flags |= FLG_OF_SHAROBJ;

			if (Bsflag)
				flags |= FLG_OF_SYMBOLIC;
		}
	} else {
		flags |= FLG_OF_STATIC;

		if (bflag) {
			eprintf(ERR_FATAL, Errmsg_oain,
			    (const char *) "-dn", (const char *) "-b");
			flags |= FLG_OF_FATAL;
		}
		if (ofl->ofl_soname) {
			eprintf(ERR_FATAL, Errmsg_oain,
			    (const char *) "-dn", (const char *) "-h");
			flags |= FLG_OF_FATAL;
		}
		if (ofl->ofl_filter) {
			if (ofl->ofl_flags & FLG_OF_AUX) {
				eprintf(ERR_FATAL, Errmsg_oain,
				    (const char *) "-dn", (const char *) "-f");
			} else {
				eprintf(ERR_FATAL, Errmsg_oain,
				    (const char *) "-dn", (const char *) "-F");
			}
			flags |= FLG_OF_FATAL;
		}
		if (ztflag) {
			eprintf(ERR_FATAL, Errmsg_oain,
			    (const char *) "-dn", (const char *) "-ztext");
			flags |= FLG_OF_FATAL;
		}
		if (Gflag) {
			eprintf(ERR_FATAL, Errmsg_oain,
			    (const char *) "-dn", (const char *) "-G");
			flags |= FLG_OF_FATAL;
		}
		if (aflag && rflag) {
			eprintf(ERR_FATAL, Errmsg_oain,
			    (const char *) "-a", (const char *) "-r");
			flags |= FLG_OF_FATAL;
		}

		if (rflag) {
			/*
			 * Relocatable object.
			 */
			flags |= FLG_OF_RELOBJ;

			/*
			 * we can only strip the symbol table and string table
			 * if no output relocations will refer to them
			 */
			if (sflag) {
				eprintf(ERR_WARNING,
					"option -r and -s both set; only "
					"debugging information stripped");
			}
			if (ofl->ofl_interp) {
				eprintf(ERR_FATAL, Errmsg_oain,
				    (const char *) "-r", (const char *) "-I");
				flags |= FLG_OF_FATAL;
			}
		} else {
			/*
			 * Static executable.
			 */
			flags |= FLG_OF_EXEC;

			if (zdflag != SET_FALSE)
				flags |= FLG_OF_NOUNDEF;

		}
	}
	if (!files) {
		if (Vflag && (argc == 2))
			exit(EXIT_SUCCESS);
		else {
			eprintf(ERR_FATAL, "no files on input command line");
			exit(EXIT_FAILURE);
		}
	}
	ofl->ofl_flags |= flags;

	/*
	 * If the user didn't supply an output file name supply a default.
	 */
	if (ofl->ofl_name == NULL)
		ofl->ofl_name = "a.out";

	/*
	 * We set the entrance criteria after all input argument processing as
	 * it is only at this point we're sure what the output image will be
	 * (static or dynamic).
	 */
	if (ent_setup(ofl) == S_ERROR)
		return (S_ERROR);

	/*
	 * Process any mapfiles after establishing the entrance criteria as
	 * the user may be redefining or adding sections/segments.
	 */
	if (ofl->ofl_maps.head) {
		Listnode *	lnp;
		const char *	name;

		for (LIST_TRAVERSE(&ofl->ofl_maps, lnp, name))
			if (map_parse(name, ofl) == S_ERROR)
				return (S_ERROR);

		if (ofl->ofl_flags & FLG_OF_SEGSORT)
			return (sort_seg_list(ofl));
	}
	return (1);
}

/*
 *
 * Pass 1 -- process_flags: collects all options and sets flags
 */
int
process_flags(Ofl_desc * ofl, int argc, char ** argv)
{
	int	error = 0;	/* Collect all argument errors before exit */
	int	c;		/* character returned by getopt */
	char *	str;

	if (argc < 2) {
		usage_mesg(FALSE);
		return (S_ERROR);
	}

getmore:
	while ((c = getopt(argc, argv, Options)) != -1) {
		DBG_CALL(Dbg_args_flags((optind - 1), c));

		switch (c) {

		case 'a':
			aflag = TRUE;
			break;

		case 'b':
			bflag = TRUE;
			break;

		case 'd':
			if ((optarg[0] == 'n') && (optarg[1] == '\0')) {
				if (dflag != SET_UNKNOWN)
					eprintf(ERR_WARNING, Errmsg_oamo,
					    (const char *) "-d");
				else
					dflag = SET_FALSE;
			} else if ((optarg[0] == 'y') && (optarg[1] == '\0')) {
				if (dflag != SET_UNKNOWN)
					eprintf(ERR_WARNING, Errmsg_oamo,
					    (const char *) "-d");
				else
					dflag = SET_TRUE;
			} else {
				eprintf(ERR_FATAL, Errmsg_ohia,
					(const char *) "-d", optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			break;

		case 'e':
			if (ofl->ofl_entry)
				eprintf(ERR_WARNING, Errmsg_oamo,
					(const char *) "-e");
			else
				ofl->ofl_entry = (void *)optarg;
			break;

		case 'f':
			if (ofl->ofl_filter) {
				eprintf(ERR_WARNING, Errmsg_oamo,
					(const char *) "-f");
			} else {
				ofl->ofl_filter = (const char *)optarg;
				ofl->ofl_flags |= FLG_OF_AUX;
			}
			break;

		case 'F':
			if (ofl->ofl_filter)
				eprintf(ERR_WARNING, Errmsg_oamo,
					(const char *) "-F");
			else
				ofl->ofl_filter = (const char *)optarg;
			break;

		case 'h':
			if (ofl->ofl_soname)
				eprintf(ERR_WARNING, Errmsg_oamo,
					(const char *) "-h");
			else
				ofl->ofl_soname = (const char *)optarg;
			break;

		case 'i':
			ofl->ofl_flags |= FLG_OF_IGNENV;
			break;

		case 'I':
			if (ofl->ofl_interp)
				eprintf(ERR_WARNING, Errmsg_oamo,
					(const char *) "-I");
			else
				ofl->ofl_interp = (const char *)optarg;
			break;

		case 'l':
			files = TRUE;
			break;

		case 'm':
			ofl->ofl_flags |= FLG_OF_GENMAP;
			break;

		case 'o':
			if (ofl->ofl_name)
				eprintf(ERR_WARNING, Errmsg_oamo,
					(const char *) "-o");
			else
				ofl->ofl_name = (const char *)optarg;
			break;
		case 'r':
			rflag = TRUE;
			break;

		case 'R':
			/*
			 * Multiple instances of this option may occur.  Each
			 * additional instance is effectively concatenated to
			 * the previous separated by a colon.
			 */
			if (ofl->ofl_rpath) {
				if ((str =
				    (char *)malloc(strlen(ofl->ofl_rpath) +
				    strlen(optarg) + 2)) == 0)
					return (S_ERROR);
				(void) strcpy(str, ofl->ofl_rpath);
				(void) strcat(str, ":");
				(void) strcat(str, optarg);
				(void) free((void *)ofl->ofl_rpath);
			} else {
				if ((str =
				    (char *)malloc(strlen(optarg) + 1)) == 0)
					return (S_ERROR);
				(void) strcpy(str, optarg);
			}
			ofl->ofl_rpath = (const char *)str;
			break;

		case 's':
			sflag = TRUE;
			break;

		case 't':
			ofl->ofl_flags |= FLG_OF_NOWARN;
			break;

		case 'u':
			break;

		case 'z':
			if (strncmp(optarg, "defs", 4) == 0) {
				if (zdflag != SET_UNKNOWN)
					eprintf(ERR_WARNING, Errmsg_oamo,
					    Errval_zdef);
				else
					zdflag = SET_TRUE;
			} else if (strncmp(optarg, "nodefs", 6) == 0) {
				if (zdflag != SET_UNKNOWN)
					eprintf(ERR_WARNING, Errmsg_oamo,
					    Errval_zdef);
				else
					zdflag = SET_FALSE;
			} else if (strncmp(optarg, "noversion", 9) == 0) {
				zvflag = TRUE;
			} else if (strncmp(optarg, "text", 4) == 0) {
				ztflag = TRUE;
			} else if (strncmp(optarg, "muldefs", 7) == 0) {
				zmflag = TRUE;
			} else {
				eprintf(ERR_FATAL, Errmsg_ohia,
					(const char *) "-z", optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			break;

		case 'D':
			/*
			 * If we have not yet read any input files go ahead
			 * and process any debugging options (this allows any
			 * argument processing, entrance criteria and library
			 * initialization to be displayed).  Otherwise, if an
			 * input file has been seen, skip interpretation until
			 * process_files (this allows debugging to be turned
			 * on and off around individual groups of files).
			 */
			if (!files)
				dbg_mask = dbg_setup(optarg);
			break;

		case 'B':
			if (strcmp(optarg, "symbolic") == 0)
				Bsflag = TRUE;
			else if (strcmp(optarg, "reduce") == 0)
				Brflag = TRUE;
			else if (strcmp(optarg, "local") == 0)
				Blflag = TRUE;
			else if (strcmp(optarg, "dynamic") &&
			    strcmp(optarg, "static")) {
				eprintf(ERR_FATAL, Errmsg_ohia,
					(const char *) "-B", optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			break;

		case 'G':
			Gflag = TRUE;
			break;

		case 'L':
			break;

		case 'M':
			if (list_append(&(ofl->ofl_maps), optarg) == 0)
				return (S_ERROR);
			break;

		case 'Q':
			if ((optarg[0] == 'n') && (optarg[1] == '\0')) {
				if (Qflag != SET_UNKNOWN)
					eprintf(ERR_WARNING, Errmsg_oamo,
					    (const char *) "-Q");
				else
					Qflag = SET_FALSE;
			} else if ((optarg[0] == 'y') && (optarg[1] == '\0')) {
				if (Qflag != SET_UNKNOWN)
					eprintf(ERR_WARNING, Errmsg_oamo,
					    (const char *) "-Q");
				else
					Qflag = SET_TRUE;
			} else {
				eprintf(ERR_FATAL, Errmsg_ohia,
					(const char *) "-Q", optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			break;

		case 'S':
			if (list_append(&lib_support, optarg) == 0)
				return (S_ERROR);
			break;

		case 'V':
			if (!Vflag)
				(void) fprintf(stderr,
				    "%sld: %s %s\n", (const char *)SGS,
				    (const char *)SGU_PKG,
				    (const char *)SGU_REL);
			Vflag = TRUE;
			break;

		case 'Y':
			if (strncmp(optarg, "L,", 2) == 0) {
				if (Llibdir)
					eprintf(ERR_WARNING, Errmsg_oamo,
						(const char *) "-YL");
				else
					Llibdir = optarg + 2;
			} else if (strncmp(optarg, "U,", 2) == 0) {
				if (Ulibdir)
					eprintf(ERR_WARNING, Errmsg_oamo,
						(const char *) "-YU");
				else
					Ulibdir = optarg + 2;
			} else if (strncmp(optarg, "P,", 2) == 0) {
				if (Plibpath)
					eprintf(ERR_WARNING, Errmsg_oamo,
						(const char *) "-YP");
				else
					Plibpath = optarg + 2;
			} else {
				eprintf(ERR_FATAL, Errmsg_ohia,
					(const char *) "-Y", optarg);
				ofl->ofl_flags |= FLG_OF_FATAL;
			}
			break;

		case '?':
			error++;
			break;

		default:
			break;
		}		/* END: switch (c) */
	}
	for (; optind < argc; optind++) {

		/*
		 * If we detect some more options return to getopt().
		 * Checking argv[optind][1] against null prevents a forever
		 * loop if an unadorned `-' argument is passed to us.
		 */
		if (argv[optind][0] == '-')
			if (argv[optind][1] == '\0')
				continue;
			else
				goto getmore;
		files = TRUE;
	}

	/*
	 * Having parsed everything, did we have any errors.
	 */
	if (error) {
		usage_mesg(TRUE);
		return (S_ERROR);
	}

	return (check_flags(ofl, argc));
}

/*
 * Pass 2 -- process_files: skips the flags collected in pass 1 and processes
 * files.
 */
int
process_files(Ofl_desc * ofl, int argc, char ** argv)
{
	int	c;

	optind = 1;		/* reinitialize optind */
getmore:
	while ((c = getopt(argc, argv, Options)) != -1) {
		DBG_CALL(Dbg_args_flags((optind - 1), c));
		switch (c) {
			case 'l':
				if (find_library(optarg, ofl) == S_ERROR)
					return (S_ERROR);
				break;
			case 'B':
				if (strcmp(optarg, "dynamic") == 0) {
					if (ofl->ofl_flags & FLG_OF_DYNAMIC)
						ofl->ofl_flags |=
							FLG_OF_DYNLIBS;
					else {
						eprintf(ERR_FATAL, Errmsg_oain,
						    (const char *) "-dn",
						    (const char *) "-Bdynamic");
						ofl->ofl_flags |= FLG_OF_FATAL;
					}
				} else if (strcmp(optarg, "static") == 0)
					ofl->ofl_flags &= ~FLG_OF_DYNLIBS;
				break;
			case 'L':
				if (add_libdir(ofl, optarg) == S_ERROR)
					return (S_ERROR);
				break;
			case 'D':
				dbg_mask = dbg_setup(optarg);
				break;
			case 'u':
				if (sym_add_u(optarg, ofl) ==
				    (Sym_desc *)S_ERROR)
					return (S_ERROR);
				break;
			default:
				break;
			}
		}
	for (; optind < argc; optind++) {
		int	fd;

		/*
		 * If we detect some more options return to getopt().
		 * Checking argv[optind][1] against null prevents a forever
		 * loop if an unadorned `-' argument is passed to us.
		 */
		if (argv[optind][0] == '-')
			if (argv[optind][1] == '\0')
				continue;
			else
				goto getmore;

		files = TRUE;
		if ((fd = open(argv[optind], O_RDONLY)) == -1) {
			eprintf(ERR_FATAL, Errmsg_file, argv[optind],
			    (const char *)"open", errno);
			ofl->ofl_flags |= FLG_OF_FATAL;
			continue;
		}

		DBG_CALL(Dbg_args_files(optind, argv[optind]));

		if (process_open(argv[optind], 0, fd, ofl,
		    (FLG_IF_CMDLINE | FLG_IF_NEEDED)) == (Ifl_desc *)S_ERROR)
			return (S_ERROR);
	}

	/*
	 * If we've had some form of fatal error while processing the command
	 * line files we might as well return now.
	 */
	if (ofl->ofl_flags & FLG_OF_FATAL)
		return (1);

	/*
	 * If any version definitions have been established, either via input
	 * from a mapfile or from the input relocatable objects, make sure any
	 * version dependencies are satisfied, and version symbols created.
	 */
	if (ofl->ofl_verdesc.head)
		if (vers_check_defs(ofl) == S_ERROR)
			return (S_ERROR);

	/*
	 * Now that all command line files have been processed see if there are
	 * any additional `needed' shared object dependencies.
	 */
	if (ofl->ofl_soneed.head)
		if (finish_libs(ofl) == S_ERROR)
			return (S_ERROR);

	/*
	 * If segment ordering was specified (using mapfile) verify things
	 * are ok.
	 */
	if (ofl->ofl_flags & FLG_OF_SEGORDER)
		ent_check(ofl);

	return (1);
}
