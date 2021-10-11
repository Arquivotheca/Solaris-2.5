/*	@(#)help.c 1.5 92/07/27	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */
/*
 * help message text.  This comes from the manual.  It would be nice
 * if we had some way of keeping the two in sync...
 */
#include "recover.h"
#include "cmds.h"

static char *findhelp;
static char *lshelp;
static char *llhelp;
static char *cdhelp;
static char *pwdhelp;
static char *versionshelp;
static char *sethosthelp;
static char *setdatehelp;
static char *addhelp;
static char *lcdhelp;
static char *lpwdhelp;
static char *listhelp;
static char *extracthelp;
static char *deletehelp;
static char *addnamehelp;
static char *showdumphelp;
static char *xrestorehelp;
static char *rrestorehelp;
static char *showsethelp;
static char *notifyhelp;
static char *fastrecoverhelp;
static char *setmodehelp;
static char *helphelp;
static char *quithelp;

static void
inithelp(void)
{
	findhelp = gettext(
"The find command recursively descends the database\n\
directory hierarchy and displays the pathnames in which\n\
the given filename component occurs.  The find command\n\
in recover is equivalent to the UNIX find(1) program\n\
when it is used at the command line with the following\n\
options:\n\
   # find . -name component -print\n\
The find command has the following syntax:\n\
   find component [ > file ]\n\
The find command accepts file names possibly containing\n\
the meta characters *, ?, [, and ] as the component that\n\
you wish to find.  When find is invoked, it does not\n\
follow symbolic links to other files or directories;\n\
it applies the selection criteria to the symbolic links\n\
themselves, as if they were ordinary files.\n");

	lshelp = gettext(
"The ls command lists files in the current database directory\n\
or the specified path.  The listing format is similar to the\n\
ls (1v) command with the -aC options.  Entries currently\n\
marked for extraction are prefixed with an asterisk (*).\n\
The ls command behaves like the UNIX ls as it reports if\n\
a file or directory is not found.  When you ls a symbolic\n\
link that points to a directory, the files in the pointed-to\n\
directory are displayed.  The ls command has the following\n\
syntax:\n\
   ls [ path ... ] [ > file ]\n\
The ls command accepts a path argument of either files or\n\
directories.  If path is a directory, the contents are\n\
displayed.  If path is a file, only information about\n\
that file name is displayed.\n");

	llhelp = gettext(
"The ll command lists files in the current database directory\n\
or the specified path.  The listing format is similar to the\n\
UNIX ls command with the -lags options.  However the ll\n\
output does not include the link count because the database\n\
does not keep this information.  Entries currently marked\n\
for extraction are prefixed with an asterisk (*).  The ll\n\
command behaves like the UNIX ls as it reports if a file\n\
or directory is not found.  When you ll a file that is a\n\
symbolic link, information about the link itself is\n\
displayed.  The ll command has the following syntax:\n\
   ll [ path ... ] [ > file ] [ < file ]\n\
The ll command accepts a path argument of either files or\n\
directories.  If path is a directory, the contents are\n\
displayed.  If path is a file, only that file is displayed.\n\
Input from a file that contains a list of file names can\n\
be directed into ll, and output from ll can be directed\n\
into a file.   If a file is directed into the ll command\n\
(< file), ll produces a line of output for each entry in\n\
the file.\n");

	cdhelp = gettext(
"The cd command changes the current directory in the database\n\
to the designated directory.  When the cd command is invoked,\n\
the prompt changes to reflect the current working directory\n\
in the database.  The cd command has the following syntax:\n\
   cd [ directory ]\n\
Like the UNIX cd(1) command, the recover cd command accepts\n\
only one argument, directory.  If supplied, directory becomes\n\
the current working directory in the database.  If directory\n\
is not specified, cd returns you to your login directory,\n\
or to the root directory if your login directory does not\n\
exist in the database.  If directory does not exist,\n\
the message: ``directory: No such file or directory''\n\
is returned.\n");

	pwdhelp = gettext(
"The pwd command prints the pathname of the working directory\n\
inside the database.  The pwd command has the following\n\
syntax:\n\
   pwd\n\
The pwd command prints the pathname of the working (current)\n\
directory.\n");

	versionshelp = gettext(
"The versions command displays all versions of the specified\n\
file or directory that are currently identified in the\n\
database.  The files or directories must have been dumped\n\
using the dump update database flag (U) or have been added\n\
to the database using the dumpdm(1M) command.  The listing\n\
format is the same as that used by the ll command.  With\n\
the appropriate use of the setdate command, any one of the\n\
file's versions may be added to the extraction list. \n\
Entries currently marked for extraction are prefixed with an\n\
asterisk (*).  Like the ll command, when versions is invoked\n\
on a file that is a symbolic link, information about the link\n\
itself is displayed.  Note that the versions command always\n\
displays all versions of the specified files or directories,\n\
regardless of the current date and mode settings.\n\
\n\
Note that versions of mount points reports the version of the\n\
mount point when dumped from root (i.e., the directory) and\n\
when dumped as the file system (i.e., the file system).  Use\n\
showdump to find dump instances of a mount point at a given\n\
point in time.\n\
\n\
The versions command has the following syntax:\n\
   versions [ path ...] [ > file ] [ < file ]\n\
The versions command displays all versions of the current\n\
directory or the specified path that are available in the\n\
database.  If path is a directory, the directory information\n\
is displayed in the ls -ld format (i.e., the contents of the\n\
directory are not shown).  If path is a file, only that file\n\
is displayed.  The listing format is similar to ls -lags.\n\
Only a file generated by the find command can be directed\n\
into the versions command (< file).  If the file is directed\n\
into versions, it shows the versions of the file names\n\
listed in the input file.\n");

	sethosthelp = gettext(
"The sethost command allows viewing Online: Backup database entries\n\
for a specified host.  By default, recover displays the\n\
database entries for the machine on which recover is\n\
invoked.  When traversing a database associated with\n\
another host, the pathnames will reflect the file tree\n\
as it existed on that host.  Changing to another host's\n\
database does not disturb an existing extraction list.  A\n\
single extract command may recover files from more than one\n\
database.\n\
\n\
For security reasons, the sethost command can only be used\n\
by the super user on the machine that hosts the database\n\
(i.e., the machine where the rpc.dumpdbd daemon runs).\n\
\n\
The sethost command has the following syntax:\n\
   sethost hostname\n\
The sethost command changes to the Online: Backup database for the\n\
hostname specified.\n");

	setdatehelp = gettext(
"The setdate command changes recover's notion of ``the\n\
current date.''  When the date is changed using setdate,\n\
subsequent commands act on the version of a given file\n\
that was dumped most recently before the specified date.\n\
The resolution of the file system image depends on how\n\
often the file system was dumped, on the current ``lookup\n\
mode'' setting, and on how far back the Online: Backup database\n\
information goes.  The setdate command has the following\n\
syntax:\n\
   setdate [ date-spec ]\n\
The date-spec can be specified with key words such as\n\
``now,'' ``yesterday,'' ``last week,'' ``last month,''\n\
``last thursday,'' ``last year,'' ``3 days ago,'' etc.\n\
setdate also accepts such date specifications as ``2/7/90\n\
12pm,'' ``2 a.m. 12/5/89,'' and ``21 feb 1990.'' If no\n\
date specification is passed, setdate sets the date to\n\
the present (i.e., ``now'').   Date specifications in\n\
the future are not allowed; when specified, the date\n\
setting remains unchanged.\n");

	addhelp = gettext(
"Adds the current directory, or the named files to the list of\n\
files to be extracted.  The name of the extracted file is the\n\
same as the name of the file at the time of the dump. The path\n\
for the extracted file is the same as the path of the dumped file\n\
with the current directory prepended.  The add command has the\n\
following syntax:\n\
    add [ path ... ] [ < file ]\n\
add accepts full pathnames or file names relative to the current\n\
directory as arguments.  Only a file generated by the find\n\
command can be directed into the add command (< file).\n");

	lcdhelp = gettext(
"The lcd command changes the current working directory on the\n\
local machine (not the working directory inside the database).\n\
The local current working directory is used by recover to build\n\
the pathname of a file that is added to the extraction list,\n\
using either the add or addname commands.\n\
The lcd command has the following syntax:\n\
   lcd [ directory ]\n\
The lcd command accepts a directory name as an argument.  If\n\
directory is specified, the current working directory changes\n\
to that directory.  The C Shell tilde (~) expansion is\n\
supported (for the home directory).  If directory is not\n\
specified, lcd changes to the user's home directory.\n");

	lpwdhelp = gettext(
"The lpwd command displays the local working directory on\n\
the system (this may not be the working directory inside\n\
the database).  This is the directory to which files will\n\
be recovered when the extract command is invoked.  The\n\
local working directory is set using the lcd command.\n\
The lpwd command has the following syntax:\n\
   lpwd\n\
The lpwd command does not accept any arguments.\n");

	listhelp = gettext(
"The list command displays the full pathname of files that\n\
are currently selected to be restored (using either add or\n\
addname).  The pathnames displayed are the paths where the\n\
files will be restored when the extract command is invoked.\n\
The list command has the following syntax:\n\
   list [ > file ]\n\
The output from the list command can be directed to a file or\n\
displayed on the screen.\n");

	extracthelp = gettext(
"The extract command reads from the backup tapes all the\n\
files that have been added to the extraction list (using\n\
either the add or addname subcommands).  The recover\n\
program queries the user before the extraction begins\n\
to verify the magnitude of the recovery request.\n\
\n\
When tape mounts are required, the system administrator\n\
is prompted to mount the necessary tape(s) via the\n\
opermon(1M) program (see Chapter 4, ``Operator Monitor'').\n\
The interactive user of recover will see the tape mount\n\
request via recover.  Either party may answer the mount\n\
request.  Unless you have access to backup tapes and tape\n\
drives, you should wait for your system administrator to\n\
answer the mount request.\n\
\n\
The recover program waits until the extraction completes,\n\
thus the prompt does not return until the recovery completes.\n\
When the recover successfully completes, the extraction list\n\
is automatically cleared.  If there was an error or an\n\
interruption during the recovery, the extraction list is\n\
retained. The extract command has the following syntax:\n\
   extract\n\
The extract command does not accept any arguments.\n");

	deletehelp = gettext(
"The delete command removes files or directories from the\n\
extraction list.  The delete command has the following syntax:\n\
   delete [ path  ...  ] [ < file ]\n\
The delete command accepts file or directory names (path)\n\
as arguments.  If path is a directory, all its files and\n\
subdirectories are removed from the extraction list.  If\n\
path is not specified, the files in the current working\n\
directory are deleted.  Only a file generated by the find\n\
command can be directed into the delete command (< file).\n\
If the file is directed into delete, the files named in\n\
the input file are deleted from the extraction list.\n");

	addnamehelp = gettext(
"The addname command adds the specified file or directory\n\
(path) to the extraction list.  This command is useful for\n\
adding a file or directory to a different destination and\n\
under a different file or directory name (newname).\n\
\n\
You can not restore the same version of a file more than\n\
one time.  Also, you can not restore two files to the same\n\
file name during a recover session.  The addname command\n\
has the following syntax:\n\
   addname path newname\n\
addname accepts a database file (either fully qualified or\n\
relative to the current database directory) and a newname.\n\
If newname is not fully qualified (i.e., does not begin with\n\
a /) the local working directory is prepended.  A different\n\
name for the file or directory can also be specified.\n\
\n\
A potential use of the addname command is to restore\n\
multiple versions of a file to distinct names.\n");

	showdumphelp = gettext(
"The showdump command displays the most recent dump information,\n\
relative to the current date setting, for the specified path.\n\
If no path is specified, the current directory in the database\n\
is used.  The setdate command can be used to view descriptions\n\
of other dumps of the same file system, when showdump is\n\
invoked.  The showdump command has the following syntax:\n\
   showdump [ path ]\n\
The showdump command displays the dump information for the\n\
specified path.  If the path is a file system mount point,\n\
the output describes its most recent level 0 dump as well\n\
as any subsequent incremental dumps prior to the current\n\
date setting.  If the path is not a mount point, the output\n\
describes the most recent dump which contains the file.\n\
If path is not specified, the current working directory\n\
(of the database) is used.  The dump description includes\n\
the dump date, dump level, the dumped file system, the\n\
dumped device, etc.\n");

	xrestorehelp = gettext(
"The xrestore command performs the same function as the\n\
restore program when invoked with the -x option.\n\
xrestore is also similar to the rrestore command as both\n\
restore an entire set of dumps.\n\
\n\
The showdump command should be used with the setdate\n\
command  to verify the dumps to be recovered before using\n\
this command.  The files are recovered to the target\n\
directory that is set using the lcd command.\n\
\n\
When invoked, xrestore restores a series of dumps.  The\n\
most recent level 0 dump prior to the current date setting\n\
is restored, followed by any subsequent incrementals prior\n\
to the current date setting.  For instance, the level 0\n\
dump is recovered first, followed by the level 5, and the\n\
level 9 (providing this is the way the dumps were performed\n\
by the administrator).\n\
\n\
The major difference between xrestore and rrestore is\n\
that the the rrestore command might delete files during\n\
restoration in an effort to restore a file system to its\n\
exact previous state, while the xrestore command will not.\n\
\n\
For security reasons, the user must be super user to invoke\n\
the xrestore command and the full xrestore can only be\n\
performed on the host from which the dump was performed.\n\
The recover program waits while the xrestore command\n\
runs, i.e., the user can not enter any recover commands\n\
until the xrestore completes.  The xrestore request can\n\
be interrupted by entering a Control-C.\n\
\n\
The xrestore command has the following syntax:\n\
   xrestore [ path ...]\n\
The xrestore command restores full and incremental dumps\n\
of the file system mounted at path or the file system\n\
mounted on the directory named by the pwd command.  The\n\
entire dump is extracted, in the same way as invoking\n\
restore with the x option and no filename argument.  The\n\
owner, modification time, and mode are restored (if\n\
possible).  If no path argument is given, dumps of the\n\
file system mounted at the current database directory\n\
(i.e., as shown by the pwd subcommand) are restored.\n\
The dumps to be recovered are chosen based on the current\n\
date setting established by the setdate command.\n");

	rrestorehelp = gettext(
"The rrestore command restores one or more full dumps.\n\
This is the same function as the restore program when\n\
invoked with the -r option.  rrestore is similar to the\n\
xrestore command as it restores an entire set of dumps.\n\
\n\
When invoked, rrestore restores a series of dumps.  The\n\
level 0, followed by any incrementals that fall within\n\
the current date setting are restored.  For instance, the\n\
level 0 dump is recovered first, followed by the level\n\
5, and the level 9 (providing this is the way the dumps\n\
were set up by the administrator).\n\
\n\
The showdump command should be used with the setdate\n\
command to check the dumps to be recovered before using.\n\
this command.  The files are recovered to the target\n\
directory shown by the lcd command.\n\
\n\
For security reasons, the user must be super user to\n\
invoke the rrestore command and the full rrestore can\n\
only be performed on the host from which the dump was\n\
performed.  The full rrestore requests will run to\n\
completion and the user will not see a new recover\n\
prompt until all relevant dumps have been restored.\n\
The user will see tape mount prompts, but can not enter\n\
other recover commands.  The rrestore request can be\n\
interrupted by entering a Control-C.\n\
\n\
The major difference between rrestore and xrestore is\n\
that the rrestore command might delete files during\n\
restoration, while the xrestore command will not.\n\
\n\
The rrestore command has the following syntax:\n\
   rrestore [ path ... ]\n\
The rrestore command restores one or more dumps in the\n\
current working directory (as reported by the lpwd\n\
command).  If path is not specified, dumps of the file\n\
system mounted at the current database directory (as\n\
displayed by the pwd command) will be restored.  If a\n\
path is specified, dumps of the file system mounted at\n\
the specified path are restored.  This option should\n\
only be used to restore a complete dump tape onto a\n\
clear file system.  rrestore performs the level 0\n\
restore and all incrementals automatically.\n");

	showsethelp = gettext(
"The showsettings command displays the current values\n\
established by the setdate, sethost, and setmode\n\
commands. The showsettings command has the following\n\
syntax:\n\
   showsettings\n\
Prints the current date, host, and mode settings.\n");

	notifyhelp = gettext(
"The notify command sets the type of notification desired\n\
when files and directories are extracted from dump tapes.\n\
notify supports three modes of notification: none, number,\n\
and all.  The default action is none.  The notification\n\
is written to stderr.  Note that this only controls\n\
informational messages (i.e., specifying notify none does\n\
not turn off error messages). The notify command has the\n\
following syntax:\n\
\n\
   notify none\n\
The notify command, by default, performs no notification\n\
to the user about individual file restorations.  This is\n\
the none option.\n\
\n\
   notify number\n\
When a number is specified, the user sees a message for\n\
every number of files restored.\n\
\n\
   notify all\n\
all specifies that the user wishes to be notified when\n\
each individual file restoration completes.\n");

	fastrecoverhelp = gettext(
"The fastrecover command improves the performance of a\n\
recover (extract, xrestore, or rrestore) command by delaying\n\
the writes of meta data in a specified file system.  You\n\
must be root to invoke this command.\n\
\n\
The benefit of the fastrecover command is that it\n\
increases the restore speed by as much as four-times\n\
the current restore rate under SunOS 4.1 restore.\n\
However, if the system crashes during the restore\n\
operation, the file system may be left very corrupted.\n\
fsck(8) might not be able to repair the corruption.\n\
Sun recommends this command only be used on an empty file\n\
system, where it would be easy to invoke the newfs(8)\n\
command and re-invoke the restore operation.\n\
\n\
If recover exits and leaves the file system in ``fast mode,''\n\
the fastfs(8) utility can be used to put the file system back\n\
in ``safe mode.''  This may occur if the recover command is\n\
killed.\n\
\n\
The fastrecover command has the following syntax:\n\
   fastrecover path\n\
The fastrecover command accepts a single path as an\n\
argument.  path can be any path within the target file\n\
system.\n");

	setmodehelp = gettext(
"The setmode command is used to modify lookup operations\n\
in the database.  setmode has two modes of operation,\n\
translucent and opaque.  opaque, the default behavior,\n\
offers a view of a given file system that is bound\n\
by the most recent level 0 dump prior to the current\n\
date setting.  In translucent mode, the most recently\n\
dumped version of any file in the database is always\n\
visible.  The setmode command has the following\n\
syntax:\n\
   setmode [ opaque | translucent ]\n\
The setmode command is used for lookup operations in the\n\
database.  If translucent is specified, subsequent lookups\n\
are conducted in translucent mode.  By default, lookups\n\
are conducted in opaque mode.\n");

	helphelp = gettext(
"The help command displays a list of the recover commands\n\
when help is entered without an argument.  The name of a\n\
command can be added as an argument to help, and a\n\
description of the command will be displayed.\n\
The help command has the following syntax:\n\
  help [ command ] [ > file ]\n\
help accepts the name of a recover subcommand as an\n\
argument.  If no command name is specified, help displays\n\
the list of recover commands.\n");

	quithelp = gettext(
"The quit command exits the recover program.  When\n\
exiting recover, a query will be given if the extraction\n\
list contains the names of files or directories that were\n\
selected for extraction, but not yet recovered.  The quit\n\
command has the following syntax:\n\
  quit\n\
The quit command exits recover.\n");
}

void
printhelp(cmd)
	char *cmd;
{
	struct cmdinfo *cp;
	char *helptxt;
	static int init;

	if (cmd == NULL) {
		usage();
		return;
	}

	if (!init) {
		init++;
		inithelp();
	}

	cp = parsecmd(cmd);
	switch (cp->id) {
	case CMD_LS:
		helptxt = lshelp;
		break;
	case CMD_LL:
		helptxt = llhelp;
		break;
	case CMD_CD:
		helptxt = cdhelp;
		break;
	case CMD_PWD:
		helptxt = pwdhelp;
		break;
	case CMD_VERSIONS:
		helptxt = versionshelp;
		break;
	case CMD_SETHOST:
		helptxt = sethosthelp;
		break;
	case CMD_SETDATE:
		helptxt = setdatehelp;
		break;
	case CMD_ADD:
		helptxt = addhelp;
		break;
	case CMD_LCD:
		helptxt = lcdhelp;
		break;
	case CMD_LPWD:
		helptxt = lpwdhelp;
		break;
	case CMD_LIST:
		helptxt = listhelp;
		break;
	case CMD_EXTRACT:
		helptxt = extracthelp;
		break;
	case CMD_DELETE:
		helptxt = deletehelp;
		break;
	case CMD_ADDNAME:
		helptxt = addnamehelp;
		break;
	case CMD_SHOWDUMP:
		helptxt = showdumphelp;
		break;
	case CMD_XRESTORE:
		helptxt = xrestorehelp;
		break;
	case CMD_RRESTORE:
		helptxt = rrestorehelp;
		break;
	case CMD_SHOWSET:
		helptxt = showsethelp;
		break;
	case CMD_NOTIFY:
		helptxt = notifyhelp;
		break;
	case CMD_FASTRECOVER:
		helptxt = fastrecoverhelp;
		break;
	case CMD_FASTFIND:
		helptxt = findhelp;
		break;
	case CMD_SETMODE:
		helptxt = setmodehelp;
		break;
	case CMD_HELP:
		helptxt = helphelp;
		break;
	case CMD_QUIT:
		helptxt = quithelp;
		break;
	case CMD_AMBIGUOUS:
		(void) fprintf(stderr,
		    gettext("%s is an ambiguous command abbreviation\n"), cmd);
		return;
	case CMD_INVAL:
		(void) fprintf(stderr,
		    gettext("%s is an invalid command\n"), cmd);
		return;
	default:
		(void) fprintf(stderr, gettext("no help for `%s'\n"), cmd);
		return;
	}

	term_start_output();
	term_putline(helptxt);
/*
	while (*helptxt) {
		term_putline(*helptxt);
		term_putc('\n');
		helptxt++;
	}
*/
	term_finish_output();
}
