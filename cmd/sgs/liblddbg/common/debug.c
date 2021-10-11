/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)debug.c	1.15	95/02/01 SMI"

/* LINTLIBRARY */

#include	<stdio.h>
#include	<string.h>
#include	<stdlib.h>
#include	<stdarg.h>
#include	"_debug.h"

int		_Dbg_mask;
static int	_Dbg_count = 0;

const char
	* Str_again =	"(again)",
	* Str_empty =	"",
	* Str_entered =	"entered",
	* Str_null =	"(null)";

/*
 * Debugging initialization and processing.  The options structure defines
 * a set of option strings that can be specified using the -D flag or from an
 * environment variable.  For each option, a class is enabled in the _Dbg_mask
 * bit mask.
 */
static DBG_options _Dbg_options[] = {
	{"all",		DBG_ALL},
	{"args",	DBG_ARGS},
	{"bindings",	DBG_BINDINGS},
	{"detail",	DBG_DETAIL},
	{"entry",	DBG_ENTRY},
	{"files",	DBG_FILES},
	{"help",	DBG_HELP},
	{"libs",	DBG_LIBS},
	{"map",		DBG_MAP},
	{"reloc",	DBG_RELOC},
	{"sections",	DBG_SECTIONS},
	{"segments",	DBG_SEGMENTS},
	{"support",	DBG_SUPPORT},
	{"symbols",	DBG_SYMBOLS},
	{"versions",	DBG_VERSIONS},
	{NULL,		NULL},
};

static const char *
	Errmsg_urdo = "warning: unrecognized debug option `%s' (try help)";

/*
 * Provide a debugging usage message
 */
static void
_Dbg_usage()
{
	dbg_print(Str_empty);
	dbg_print("\t\t For debugging the run-time linking of an application:");
	dbg_print("\t\t\tLD_DEBUG=option1,option2  prog");
	dbg_print("\t\t enables diagnostics to the stderr.  The additional "
		"option:");
	dbg_print("\t\t\tLD_DEBUG_OUTPUT=file");
	dbg_print("\t\t redirects the diagnostics to an output file created "
		"using");
	dbg_print("\t\t the specified name and the process id as a suffix.  "
		"All");
	dbg_print("\t\t diagnostics are prepended with the process id.");
	dbg_print(Str_empty);
	dbg_print("\t\t For debugging the link-editing of an application:");
	dbg_print("\t\t\tLD_OPTIONS=-Doption1,option2 cc -o prog ...");
	dbg_print("\t\t or,");
	dbg_print("\t\t\tld -Doption1,option2 -o prog ...");
	dbg_print("\t\t where placement of -D on the command line is "
		"significant,");
	dbg_print("\t\t and options can be switched off by prepending "
		"with `!'.");
	dbg_print(Str_empty);
	dbg_print(Str_empty);
	dbg_print("args\t display input argument processing");
	dbg_print("bindings  display symbol binding; detail flag shows "
		"absolute:relative");
	dbg_print("\t\t addresses");
	dbg_print("detail\t provide more information in conjunction with "
		"other options");
	dbg_print("entry\t display entrance criteria descriptors");
	dbg_print("files\t display input file processing "
		"(files and libraries)");
	dbg_print("help\t display this help message");
	dbg_print("libs\t display library search paths; detail flag shows "
		"actual");
	dbg_print("\t\t library lookup (-l) processing");
	dbg_print("map\t display map file processing");
	dbg_print("reloc\t display relocation processing");
	dbg_print("sections  display input section processing");
	dbg_print("segments  display available output segments and "
		"address/offset");
	dbg_print("\t\t processing; detail flag shows associated sections");
	dbg_print("support\t display support library processing");
	dbg_print("symbols\t display symbol table processing;");
	dbg_print("\t\t detail flag shows resolution and linker table "
		"addition");
	dbg_print("versions\t display version processing");
}

/*
 * Validate and enable the appropriate debugging classes.
 */
int
Dbg_setup(const char * string)
{
	char *		name, * _name;	/* Temporary buffer in which to */
					/* perform strtok() operations. */
	DBG_opts 	opts;		/* Ptr to cycle thru _Dbg_options[]. */
	const char *	comma = ",";

	if ((_name = (char *)malloc(strlen(string) + 1)) == 0)
		return (0);
	(void) strcpy(_name, string);

	/*
	 * The option should be of the form "-Dopt,opt,opt,...".  Separate the
	 * pieces and build up the appropriate mask, unrecognized options are
	 * flagged.
	 */
	if ((name = strtok(_name, comma)) != NULL) {
		Boolean		found, set;
		do {
			found = FALSE;
			set = TRUE;
			if (name[0] == '!') {
				set = FALSE;
				name++;
			}
			for (opts = _Dbg_options; opts->o_name != NULL;
				opts++) {
				if (strcmp(name, opts->o_name) == 0) {
					if (set == TRUE)
						_Dbg_mask |= opts->o_mask;
					else
						_Dbg_mask &= ~(opts->o_mask);
					found = TRUE;
					break;
				}
			}
			if (found == FALSE)
				dbg_print(Errmsg_urdo, name);
		} while ((name = strtok(NULL, comma)) != NULL);
	}
	(void) free(_name);

	/*
	 * If the debug help option was specified dump a usage message.  If
	 * this is the only debug option cause the caller to exit.
	 */
	if ((_Dbg_mask & DBG_HELP) && !_Dbg_count) {
		_Dbg_usage();
		if (_Dbg_mask == DBG_HELP)
			exit(0);
	}

	_Dbg_count++;

	return (_Dbg_mask);
}
