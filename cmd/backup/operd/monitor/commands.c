/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ident	"@(#)commands.c 1.0 91/01/28 SMI"

#ident	"@(#)commands.c 1.19 92/04/06"

#include "defs.h"
#include <stdio.h>
#include <locale.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curses.h>

static char	shorthelp[] = "?";

#ifdef __STDC__
static void docmd(void);
static void cmd_server(void);
static void cmd_erase(void);
static void cmd_filter(void);
#else
static void docmd();
static void cmd_erase();
static void cmd_filter();
static void cmd_server();
#endif

struct command {
	char	*c_name;		/* command name */
	void	(*c_func)(void);	/* function */
};

/*
 * Command indicies -- make sure these
 * match the ordering below
 */
#define	SERVER		0
#define	SHORTHELP	1
#define	HELP		2
#define	QUIT		3
#define	ERASE		4
#define	FILTER		5

static struct command localcmds[] = {
	{
		(char *)0,
		cmd_server
	},
	{
		shorthelp,
		scr_help
	},
	{
		(char *)0,
		scr_help
	},
	{
		(char *)0,
		cmd_exit
	},
	{
		(char *)0,
		cmd_erase
	},
	{
		(char *)0,
		cmd_filter
	}
};
static int ncmds = sizeof (localcmds) / sizeof (struct command);

/*
 * Initialize command name pointers.  This must be
 * called before issuing the first prompt.  It exists
 * solely to enable internationalization.
 */
void
#ifdef __STDC__
cmd_init(void)
#else
cmd_init()
#endif
{
	/*
	 * XGETTEXT:  The following five text strings constitute
	 * the command set of opermon.  The set may be localized,
	 * but for best results each command should translate to
	 * a single word.
	 */
	localcmds[SERVER].c_name = gettext("server");
	localcmds[HELP].c_name = gettext("help");
	localcmds[QUIT].c_name = gettext("quit");
	localcmds[ERASE].c_name = gettext("erase");
	localcmds[FILTER].c_name = gettext("filter");
}

void
cmd_dispatch(cmd)
	cmd_t	cmd;
{
	if (doinghelp) {
		runhelp(cmd);
		return;
	}

	current_status = NULL;

	switch (cmd) {
	case '\002':	/* control-B */
	case '\025':	/* control-U */
	case '-':
		suspend = 0;
		scr_prev();
		break;
	case '\004':	/* control-D */
	case '\006':	/* control-F */
	case '+':
		suspend = 0;
		scr_next();
		break;
	case '\014':	/* control-L */
		scr_redraw(1);
		break;
	case '?':
		suspend = 0;
		scr_help();
		break;
	case ' ':
		(void) time(&suspend);
		suspend += HOLDSCREEN;
		status(1, gettext("Screen updates suspended"));
		break;
	case '\032':
		/* suspend */
		break;
	case ':':
		scr_redraw(0);
		docmd();
		break;
	default:
		if (istag((tag_t)cmd)) {
			scr_redraw(0);
			(void) msg_reply((char)cmd);
			suspend = 0;
		} else
			bell();
		break;
	}
	mainprompt = 1;
	resetprompt();	/* reset to main prompt */
	scr_redraw(0);
}

void
#ifdef __STDC__
bell(void)
#else
bell()
#endif
{
	/* XGETTEXT:  Translate this to whatever produces a beep */
	(void) fputs(gettext(""), stderr);
	current_status = NULL;
}

static void
#ifdef __STDC__
docmd(void)
#else
docmd()
#endif
{
	struct command *cmdp;
	char cmdbuf[256];
	char *cp = cmdbuf;
	int	cmdlen;
	int	match;
	int	start;
	int	end;
	int	c;
	register int i;

	start = end = 0;
	cmdlen = 0;
	*cp = 0;
	mainprompt = 0;
	prompt(gettext("Command> "));
	current_input = cmdbuf;
	while ((c = getch()) != ERR && cmdlen < sizeof (cmdbuf)-1 && !end) {
		match = 0;
		if (isspace(c)) {
			if (start)
				end++;
			if (c == '\n') {
				erasecmds();
				break;
			}
			continue;
		} else if (c == erasech) {
			if (cp > cmdbuf) {
				backspace();
				cmdlen--;
				if (--cp == cmdbuf)
					start = 0;
			}
			continue;
		} else if (c == killch) {
			if (cp > cmdbuf) {
				linekill(strlen(cmdbuf));
				cp = cmdbuf;
			}
			start = 0;
			cmdlen = 0;
			*cp = 0;
			continue;
		} else {
			if (!start)
				start++;
			scr_echo(c);
			*cp++ = (char)c;
			*cp = 0;
			cmdlen++;
		}
		for (i = 0; i < ncmds; i++) {
			if (strncmp(localcmds[i].c_name, cmdbuf,
			    cmdlen) == 0) {
				if (match++)
					break;
				cmdp = &localcmds[i];
			}
		}
		if (match == 1) {
			current_input = NULL;
			prompt("%s%s ", current_prompt, cmdp->c_name);
			(*cmdp->c_func)();
			break;
		} else if (!match) {
			status(1, gettext("unrecognized command '%s'"), cmdbuf);
			break;
		}
	}
	current_input = NULL;
}

static void
#ifdef __STDC__
cmd_server(void)
#else
cmd_server()
#endif
{
	char buf[BCHOSTNAMELEN];
	char *nlp;

	suspend = 0;
	buf[0] = '\0';
	current_input = buf;
	cgets(buf, sizeof (buf));
	current_input = NULL;
	if (buf[0] != '\n' && buf[0] != '\0') {
		nlp = strchr(buf, '\n');
		if (nlp)
			*nlp = '\0';
		msg_reset();
		if (connected)
			(void) oper_logout(opserver);
		if (setopserver(buf) >= 0 &&
		    oper_login(buf, 0) == OPERMSG_SUCCESS) {
			(void) strncpy(opserver, buf, BCHOSTNAMELEN);
			status(1, gettext(
			    "Login succeeded: now connected to server '%s'"),
				opserver);
			connected = opserver;
		} else {
			bell();
			if (oper_login(opserver, 0) != OPERMSG_SUCCESS) {
				status(1, gettext(
			"Login failed: not connected to any server"));
				connected = (char *)0;
			} else
				status(1, gettext(
				    "Login failed: reconnected to server '%s'"),
					opserver);
		}
	} else
		status(1, gettext("Aborted '%s' command"),
			localcmds[SERVER].c_name);
}

static void
#ifdef __STDC__
cmd_erase(void)
#else
cmd_erase()
#endif
{
	struct msg_cache *mc = NULL;
	extern	int tagall;
	char	name[2];
	time_t	h;

	h = scr_hold(0);
	if (!top) {
		bell();
		status(1, gettext("No messages to erase!"));
		scr_release(h);
		return;
	}
	mainprompt = 0;
	tagall++;
	scr_redraw(0);
	prompt(gettext("%serase which message? "), current_prompt);
	name[0] = '\0';
	current_input = name;
	cgets(name, 2);
	current_input = NULL;
	if (name[0] == '\0' || name[0] == '\n') {
		status(1, gettext("Aborted '%s' command"),
			localcmds[ERASE].c_name);
		tagall = 0;
		scr_release((time_t)0);		/* 0 releases user hold */
		return;
	}
	mc = getmsgbytag(STRTOTAG(name));
	tagall = 0;
	scr_release((time_t)0);
	if (mc != NULL) {
		mc->mc_status |= EXPIRED;	/* hide it, but don't rm it */
		scr_topdown(0);
	} else {
		bell();
		status(1, gettext("That message does not exist"));
	}
}

void
#ifdef __STDC__
cmd_exit(void)
#else
cmd_exit()
#endif
{
	current_status = NULL;
	Exit(0);
}

static void
#ifdef __STDC__
cmd_filter(void)
#else
cmd_filter()
#endif
{
	static char filter[256];
	char	*errmsg, *nlp, buf[256];

	prompt("%s/", current_prompt);
	buf[0] = '\0';
	current_input = buf;
	cgets(buf, sizeof (buf));
	current_input = NULL;
	if (buf[0] != '\n' && buf[0] != '\0') {
		nlp = strrchr(buf, '\n');
		if (nlp)
			*nlp = '\0';
		errmsg = mreg_comp(buf);
		if (errmsg == NULL) {
			(void) strcpy(filter, buf);
			current_filter = filter;
			scr_bottomup(1);
		} else
			status(1, errmsg);
	} else if (buf[0] == '\n' && current_filter) {
		status(1, gettext("Filter '%s' removed"), current_filter);
		current_filter = NULL;
		scr_bottomup(1);
	} else
		status(1, gettext("Filter status unchanged"));
}
