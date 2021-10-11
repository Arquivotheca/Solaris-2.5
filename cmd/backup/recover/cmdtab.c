#ident	"@(#)cmdtab.c 1.6 91/12/20"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "recover.h"
#include "cmds.h"

/*
 * recognize command names and allow the shortest possible abbreviation
 * for each.
 */

#define	TABLESIZE 26	/* lower case 'a' through lower case 'z' */
#define	MAXCMDS	5	/* max # cmds that start with the same letter */

static struct allcmds {
	int nentries;		/* # of cmds starting with this letter */
	int maxlen;		/* length of longest one */
	struct cmdinfo acmd[MAXCMDS];	/* each individual command */
} cmdtab[TABLESIZE] = {

	{2, 7,
	    {
		{3, "add", CMD_ADD, 0, -1, REDIR_INPUT},
		{4, "addname", CMD_ADDNAME, 2, 2, 0}
	    }
	},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},

	{1, 2, {1, "cd", CMD_CD, 0, 1, 0}},

	{1, 6, {1, "delete", CMD_DELETE, 0, -1, REDIR_INPUT}},

	{1, 7, {1, "extract", CMD_EXTRACT, 0, 0, 0}},

	{2, 11,
	    {
		{2, "fastrecover", CMD_FASTRECOVER, 1, 1, 0},
		{2, "find", CMD_FASTFIND, 1, 1, REDIR_OUTPUT},
	    }
	},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},

	{1, 4, {1, "help", CMD_HELP, 0, 1, REDIR_OUTPUT}},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},

	{5, 4,
	    {
		{2, "ls", CMD_LS, 0, -1, REDIR_INPUT|REDIR_OUTPUT},
		{2, "ll", CMD_LL, 0, -1, REDIR_INPUT|REDIR_OUTPUT},
		{2, "lcd", CMD_LCD, 0, 1, 0},
		{2, "list", CMD_LIST, 0, 0, REDIR_OUTPUT},
		{2, "lpwd", CMD_LPWD, 0, 0, 0},
	    }
	},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},

	{1, 6, {1, "notify", CMD_NOTIFY, 1, 1, 0}},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},

	{1, 3, {1, "pwd", CMD_PWD, 0, 0, 0}},

	{1, 4, {1, "quit", CMD_QUIT, 0, 0, 0}},

	{1, 8, {1, "rrestore", CMD_RRESTORE, 0, -1, 0}},

	{5, 12,
	    {
		{4, "setdate", CMD_SETDATE, 0, -1, 0},
		{4, "sethost", CMD_SETHOST, 1, 1, 0},
		{4, "setmode", CMD_SETMODE, 0, 1, 0},
		{5, "showdump", CMD_SHOWDUMP, 0, -1, REDIR_OUTPUT},
		{5, "showsettings", CMD_SHOWSET, 0, 0, 0},
	    }
	},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},

	{1, 8, {1, "versions", CMD_VERSIONS, 0, -1, REDIR_INPUT|REDIR_OUTPUT}},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},

	{1, 8, {1, "xrestore", CMD_XRESTORE, 0, -1, 0}},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},

	{0, 0, {0, "", CMD_INVAL, 0, 0, 0}},
};

static struct cmdinfo ambiguous_command = {
	0, "", CMD_AMBIGUOUS, 0, 0, 0
};

static struct cmdinfo invalid_command = {
	0, "", CMD_INVAL, 0, 0, 0
};

static struct cmdinfo null_command = {
	0, "", CMD_NULL, 0, 0, 0
};

struct cmdinfo *
parsecmd(string)
	char *string;
{
	int idx, len;
	register int i;
	struct cmdinfo *rval;

	if (*string == '\0')
		return (&null_command);
	idx = *string - 'a';
	if (idx < 0 || idx >= TABLESIZE)
		return (&invalid_command);

	len = strlen(string);
	if (len > cmdtab[idx].maxlen)
		return (&invalid_command);

	rval = &invalid_command;
	for (i = 0; i < cmdtab[idx].nentries; i++) {
		if (strncmp(string, cmdtab[idx].acmd[i].name, len) == 0) {
			if (len < cmdtab[idx].acmd[i].minlen) {
				rval = &ambiguous_command;
			} else {
				rval =  &cmdtab[idx].acmd[i];
			}
			break;
		}
	}

	return (rval);
}

void
#ifdef __STDC__
usage(void)
#else
usage()
#endif
{
	register int i, j, col2;
	static int halfscreen = 39;
	char termbuf[50];
	static int init;
	static char *umsgs[24];		/* sigh */
	static int ncmds = 24;

	if (!init) {
		umsgs[0] = gettext("add [path ...] [<file]");
		umsgs[1] = gettext("addname path newname");
		umsgs[2] = gettext("cd [directory]");
		umsgs[3] = gettext("delete [path ...] [< file]");
		umsgs[4] = gettext("extract");
		umsgs[5] = gettext("fastrecover filesystem");
		umsgs[6] = gettext("find component [> file]");
		umsgs[7] = gettext("help [cmd] [> file]");
		umsgs[8] = gettext("lcd [directory]");
		umsgs[9] = gettext("list [> file]");
		umsgs[10] = gettext("ll [path ...] [> file] [< file]");
		umsgs[11] = gettext("lpwd");
		umsgs[12] = gettext("ls [path ...] [> file] [< file]");
		umsgs[13] = gettext("notify none | number | all");
		umsgs[14] = gettext("pwd");
		umsgs[15] = gettext("quit");
		umsgs[16] = gettext("rrestore [path ...]");
		umsgs[17] = gettext("setdate [date-spec]");
		umsgs[18] = gettext("sethost hostname");
		umsgs[19] = gettext("setmode [translucent]");
		umsgs[20] = gettext("showdump [path ...] [> file]");
		umsgs[21] = gettext("showsettings");
		umsgs[22] = gettext("versions [path ...] [> file] [< file]");
		umsgs[23] = gettext("xrestore [path ...]");
		init++;
	}

	col2 = (ncmds / 2) + (ncmds % 2);

	term_start_output();
	term_putline(gettext("valid commands are:\n\n"));
	for (i = 0, j = col2; i < col2; i++, j++) {
		(void) sprintf(termbuf, "%-*s", halfscreen, umsgs[i]);
		term_putline(termbuf);
		if (j < ncmds) {
			(void) sprintf(termbuf, "%-*s", halfscreen, umsgs[j]);
			term_putline(termbuf);
		}
		term_putc('\n');
	}
	term_finish_output();
}
