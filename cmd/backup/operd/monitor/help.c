/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ident	"@(#)help.c 1.0 91/12/13 SMI"

#ident	"@(#)help.c 1.5 92/07/27"

#include "defs.h"
#include <locale.h>
#include <curses.h>
#include <string.h>

#define	MAXHELP		11

int	doinghelp;		/* indicates in help:  no screen updates */

static WINDOW	*helpwin;
static int	help_lines;

static char *helptext[MAXHELP];	/* help text, initialized below */
static char **hsection;		/* pointer to current help section */
static char *hp;		/* pointer to current help text */
static int y;			/* current screen line */
static int nextbreak;		/* "y" of next prompt break */
static int lastscreen;		/* on last screen-full */

#ifdef __STDC__
static void helpmsg(char *);
static void helplines(void);
#else
static void helpmsg();
static void helplines();
#endif

void
helpinit(rows, columns)
	int	rows;
	int	columns;
{
	help_lines = rows;
	helpwin = newwin(rows, columns, 0, 0);
	if (helpwin == (WINDOW *)0) {
		status(0, gettext("cannot initialize help window"));
		Exit(-1);
	}
	scrollok(helpwin, TRUE);
#ifdef USG
	idlok(helpwin, TRUE);		/* for smooth scrolling */
#endif

	/*
	 * XGETTEXT:  Because the target string of a "msgstr"
	 * directive must be less then 2048 characters in length,
	 * the help text is divided into sections.  Each help
	 * section consists of one or more lines of text that
	 * should be formatted the way you wish them to appear
	 * on the screen.  The default/native text is formatted
	 * for a screen size of 60 [or more] columns.
	 *
	 */
	helptext[0] = gettext(
		"\nThe opermon program provides the user three modes of\n\
interaction: (1) screen manipulation, (2) command mode,\n\
and (3) message reply.\n");

	helptext[1] = gettext(
		"\n(1) Screen Manipulation:\n\n\
A command of the form ^X is typed by pressing the Control\n\
key at the same time as the designated character.  Paging\n\
forward displays more recently received messages; paging\n\
backward displays older messages.  Pressing the Space Bar\n\
prevents the retagging of messages for 10 seconds or until\n\
you issue another screen command (except redraw).\n");

	helptext[2] = gettext(
		"\n    function        key             function        key\n\
    --------        ---             --------        ---\n\
    help            ?               redraw screen   ^L\n\
    page forward    +               page backward   -\n\
    page forward    ^D              page backward   ^U\n\
    page forward    ^F              page backward   ^B\n\
    hold screen     space-bar\n");

	helptext[3] = gettext(
		"\n(2) Command Mode:\n\n\
You enter command mode by typing a colon (`:') followed\n\
by the first character of a command name.  The first\n\
character of the command is a ``hot key'' that changes\n\
your prompt.  Depending on the command, you may be\n\
required to enter additional arguments after the prompt.\n");

	helptext[4] = gettext(
		"\n  command abbrev  argument              function\n\
  ------- ------ -----------   ---------------------------\n\
  server    s    server_name   retrieve specified messages\n\
  erase     e    message_tag   removes specified message\n\
  help      h    none          display help message\n\
  help      ?    none          display help message\n\
  quit      q    none          exits the monitor\n\
  filter    f    reg_ex/none   sets or removes filter\n");

	helptext[5] = gettext(
		"\nExamples (monitor output shown as [...]):\n\
    Connecting to a server (operator daemon):\n\
        s[erver] wilma\n\
        s[erver] fred.skunkworks.com\n\
    Erasing a message from your monitor's display:\n\
        e[rase which message? (a-b)] b\n\
    Filtering messages not containing the given expression:\n\
        f[ilter] dump\n\
        f[ilter] wilma\n\
    Removing the filter (displaying all messages):\n\
        f[ilter] <Return>\n");

	helptext[6] = gettext(
		"\n(3) Message Replies:\n\n\
Messages preceded by a ``tag'' letter from the set a-z or\n\
A-Z require a response.  The character following this tag\n\
indicates the message's disposition.  You may reply to any\n\
tagged message followed by either a question mark (?) or\n\
an asterisk (*).  Possible disposition characters and their\n\
meanings are:\n");

	helptext[7] = gettext(
		"\n disp                      definition\n\
 -----   --------------------------------------------------\n\
  (*)    You have not yet responded to this message\n\
  (?)    Your response has not yet been acknowledged\n\
  (+)    Your response was accepted (it was the first the\n\
              requesting application received)\n\
  (-)    Your response was rejected because it was not the \n\
              first the requesting application received,\n\
              or because you are not authorized to respond.\n");

	helptext[8] = gettext(
	    "\nTo respond to a message, type its alphabetical ``tag'' (a-z\n\
or A-Z).  After you enter the tag, the monitor highlights\n\
the selected message and displays a prompt that looks like:\n\n\
        program[process-ID]@host>\n\n\
Enter the text of the response you wish to send.  While\n\
entering the response, you may use the Delete key to\n\
erase text and the Control-U key to cancel the response and\n\
de-select the message.  When opermon receives your reply\n\
it updates the status of the target message.\n");

	helptext[9] = gettext(
		"\nFor more detailed information, consult your\n\
Online: Backup 2.0 Administration Guide.\n");

	helptext[10] = (char *)0;
}

static void
helpmsg(prompt)
	char	*prompt;
{
	wmove(helpwin, help_lines-1, 0);
	wclrtoeol(helpwin);
	wstandout(helpwin);
	waddstr(helpwin, prompt);
	wstandend(helpwin);
/*	dorefresh(helpwin, TRUE); */
	wrefresh(helpwin);
}

static void
#ifdef __STDC__
helplines(void)
#else
helplines()
#endif
{
	char	*nl;

	while (hp && *hp && y != nextbreak) {
		wmove(helpwin, y++, 0);
		nl = strchr(hp, '\n');
		if (nl != (char *)0) {
			waddnstr(helpwin, hp, (nl + 1) - hp);
			hp = nl + 1;
		} else {
			waddstr(helpwin, hp);
			hp = (char *)0;
		}
		wclrtoeol(helpwin);
		/*
		 * get next help section if done
		 * with this one
		 */
		if ((hp == (char *)0 || *hp == '\0') && *hsection != (char *)0)
			hp = *++hsection;
	}
}

void
runhelp(cmd)
	cmd_t	cmd;
{
	/* XGETTEXT:  "quit" matches "'q' exit help" */
	char	*quit = gettext("quit");

	(void) tcflush(fileno(stdin), 0);
	wmove(helpwin, help_lines-1, 0);
	wclrtoeol(helpwin);

	if (lastscreen) {
		doinghelp = 0;
		scr_redraw(1);
		return;
	}

	if (cmd == '\n')		/* next line */
		nextbreak++;
	else if (cmd == ' ') 		/* next screen */
		nextbreak += (help_lines-1);
	else if (cmd == quit[0]) {	/* return to monitor */
		doinghelp = 0;
		scr_redraw(1);
		return;
	}

	helplines();			/* display some help text */

	if (hp == (char *)0 || *hp == '\0') {
		/* XGETTEXT:  The following text should fit in 60 columns */
		helpmsg(gettext("Press any key to return to monitor "));
		lastscreen = 1;
	} else
		/* XGETTEXT:  The following text should fit in 60 columns */
		helpmsg(gettext(
	"'Return' next line, 'Space Bar' next page, 'q' exit help "));
}

void
#ifdef __STDC__
scr_help(void)
#else
scr_help()
#endif
{
	doinghelp++;
	wclear(helpwin);
	nextbreak = help_lines-1;
	lastscreen = 0;
	y = 0;

	/*
	 * Find out if localized help text is available.
	 * Note that the text "help section" appears in
	 * msgid strings in the catalog and thus should
	 * not be wrapped by gettext().
	 */
	hsection = helptext;
	hp = *hsection;

	helplines();			/* display some help text */

	if (hp == (char *)0 || *hp == '\0') {
		helpmsg(gettext("Press any key to return to monitor "));
		lastscreen = 1;
	} else
		helpmsg(gettext(
	"'Return' next line, 'Space Bar' next page, 'q' exit help "));
}
