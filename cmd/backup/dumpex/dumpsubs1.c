#ident	"@(#)dumpsubs1.c 1.37 94/08/10"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include <config.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <signal.h>
#include <errno.h>
#include <curses.h>
#include <sys/termios.h>
#include <sys/types.h>
#include <sys/stat.h>

extern int expert;

#ifdef __STDC__
extern void die2(const char *format, ...);
static void showdevices(int);
static void showkeeps(int);
static void showmailees(int);
static void showcrons();
static void makeit(char *);
static int k_input1(char *, int);
static int k_input2(char *, int);
static int k_input3(char *, int);
static int k_input4(char *, int);
#else
#endif

void
newtapelibraryname(void)
{
	char	in[LINEWID];
	inputborder();
	inputshow(1, gettext(
	    "Please enter new tape library name (return=retain)"));
	for (;;) {
		inputshow(2, "> ");	/* Combine with checkget */
		checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0')
			return;
		if (!filenamecheck(in, MAXLIBLEN))
			break;
		inputshow(3, gettext(
		    "Max %d chars; use only letters/numbers (%s)"),
			MAXLIBLEN, in);

	}
	cf_tapelib = strdup(in);
	changedtop = 1;
}

void
newrdevuser(void)
{
	char	in[LINEWID];
	inputborder();
	inputshow(1, gettext(
	    "Please enter new remote dump user (return=discard)"));
	inputshow(2, "> ");	/* Combine with checkget */
	checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
	cf_rdevuser = strdup(in);
	changedtop = 1;
}

void
newdumplibraryname(void)
{
	char	in[LINEWID];
	inputborder();
	inputshow(1, gettext(
	    "Please enter new dump library machine (return=retain)"));
	for (;;) {
		inputshow(2, "> ");
		checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0')
			return;
		if (index(in, ' ') != NULL) {
			inputshow(3, gettext(
			    "No blanks allowed in machine name (`%s')"), in);
			continue;
		}
		break;
	}
	cf_dumplib = strdup(in);
	changedtop = 1;
}

void
newtapesup(void)
{
	char	in[LINEWID];
	inputborder();
	inputshow(1, gettext(
	    "Please enter the number of tapes to preallocate:"));
	for (;;) {
		inputshow(2, "> ");	/* Combine with checkget */
		checkget(LINEWID, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0')
			return;
		if (atoi(in) < 0) {
			inputshow(3, gettext(
		    "Number of preallocated tapes must be non-negative"));
			continue;
		}
		break;
	}
	cf_tapesup = atoi(in);
	changedtop = 1;
}

void
newblockfac(void)
{
	char	in[LINEWID];
	inputborder();
	inputshow(1, gettext("Please enter the blocking factor:"));
	inputshow(2, "> ");	/* Combine with checkget */
	for (;;) {
		checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0')
			return;
		if (atoi(in) < 0) {
			inputshow(3, gettext(
			    "Blocking factor must be non-negative"));
			continue;
		}
		break;
	}
	cf_blockfac = atoi(in);
	changedtop = 1;
}

/*
 * new devices
 */
void
newdevices(void)
{
	int	c;
	int	devstart;
	char	in[LINEWID];
	char	*add = gettext("a = add");
	char	*delete = gettext("d = delete");
	char	*edit = gettext("e = edit");
	char	*quit = gettext("q = quit (back to main menu)");
	char	*help = gettext("? = help");
	int	i;

	devstart = 0;
	for (;;) {
		erase();
		(void) mvprintw(2, 5, gettext(
			"----- Tape Device Editor -- %s -----"), filename);
		showdevices(devstart);
		(void) mvprintw(20, 2, "  %s  %s  %s  %s  %s",
			add, delete, edit, quit, help);
		move(20, 1);
		c = zgetch();
		if (c == '\014')
			clear();
		else if (c == quit[0] || c == '\n' || c == '\r') {
			if (ncf_dumpdevs == 0) {
				(void) mvprintw(24, 3, gettext(
			"Cannot return without at least one tape device"));
				continue;
			}
			return;
		} else if (c == '-' || c == '' || c == '') {
			devstart = devstart - MAXDEVICEDISPLAY + 1;
			if (devstart < 0)
				devstart = 0;
		} else if (c == '+' || c == ' ' || c == '' || c == '') {
			devstart = devstart + MAXDEVICEDISPLAY - 1;
			if (devstart >= ncf_dumpdevs)
				devstart = ncf_dumpdevs - 1;
		} else if (c == add[0]) {
			inputborder();
			inputshow(1, gettext(
				"Add after which device (0 for beginning):"));
			for (;;) {
				int	n;
				inputshow(2, "> ");
				checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
				if (in[0] == '\0')
					break;
				n = atoi(in);
				if (n >= 0 && n <= ncf_dumpdevs) {
					inputshow(1, gettext(
				    "Name of device to add at slot %d:"),
						n + 1);
					inputclear(3);
					inputshow(2, "> ");
					checkget(LINEWID - 1,
						INBASE + 2, INPUTCOL, in);
					if (in[0] == '\0')
						break;
					while (ncf_dumpdevs >= maxcf_dumpdevs) {
						/* dynamic growth */
						maxcf_dumpdevs += GROW;
						cf_dumpdevs = (char **)
						    checkrealloc((char *)
							cf_dumpdevs,
							maxcf_dumpdevs *
						/*LINTED [alignment ok]*/
							sizeof (char *));
					}
					for (i = ncf_dumpdevs - 1; i >= n; i--)
						cf_dumpdevs[i + 1] =
							cf_dumpdevs[i];
					cf_dumpdevs[n] = strdup(in);
					changedbottom = 1;
					ncf_dumpdevs++;
					break;
				}
				inputshow(3, gettext(
				    "Choose a number from 0 through %d"),
					ncf_dumpdevs);
				continue;
			}
		} else if (c == delete[0]) {
			inputborder();
			inputshow(1, gettext(
			    "Please enter the number of the device to delete"));
			for (;;) {
				int	n;
				inputshow(2, "> "); /* Combine with checkget */
				checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
				if (in[0] == '\0')
					break;
				n = atoi(in);
				if (n > 0 && n <= ncf_dumpdevs) {
					for (i = n - 1;
					    i + 1 < ncf_dumpdevs; i++)
						cf_dumpdevs[i] =
							cf_dumpdevs[i + 1];
					ncf_dumpdevs--;
					changedbottom = 1;
					break;
				}
				inputshow(3, gettext(
				    "Choose a number from 1 through %d"),
					ncf_dumpdevs);
				continue;
			}
		} else if (c == edit[0]) {
			inputborder();
			inputshow(1, gettext(
			    "Please enter the number of the device to edit:"));
			for (;;) {
				int	n;
				inputshow(2, "> ");
				checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
				if (in[0] == '\0')
					break;
				n = atoi(in);
				if (n > 0 && n <= ncf_dumpdevs) {
					inputshow(1, gettext(
				"Name of device to replace at slot %d:"),
						n);
					inputclear(3);
					inputshow(2, "> ");
					checkget(LINEWID - 1,
						INBASE + 2, INPUTCOL, in);
					if (in[0] == '\0')
						break;
					cf_dumpdevs[n - 1] = strdup(in);
					changedbottom = 1;
					break;
				}
				inputshow(3, gettext(
					"Choose a number from 1 through %d"),
					ncf_dumpdevs);
				continue;
			}
		} else if (c == help[0])
			scr_help(HELP_DEVS, expert);
		else
			(void) fprintf(stderr, gettext("\007"));
	}
	/* NOTREACHED */
}

static void
showdevices(start)
{
	int	i;
	int	line;
	if (start == 1)
		(void) mvprintw(2, 10, gettext(
			"1 device above these (use '-' to display)"));
	else if (start)
		(void) mvprintw(2, 10, gettext(
		    "%d devices above these (use '-' to display)"),
			start);
	line = 4;
	for (i = start; i < start + MAXDEVICEDISPLAY && i < ncf_dumpdevs; i++)
		(void) mvprintw(line++, 3, "%3d  %s", i + 1, cf_dumpdevs[i]);
	if (i < ncf_dumpdevs) {
		if (ncf_dumpdevs - i == 1)
			(void) mvprintw(line, 10, gettext(
			    "1 device below these (use '+' to display)"));
		else
			(void) mvprintw(line, 10, gettext(
			    "%d devices below these (use '+' to display)"),
				ncf_dumpdevs - i);
	}
}

/*
 * new keeps
 */
void
newkeeps(void)
{
	char	c;
	int	keepstart;
	char	in[LINEWID];
	int	i;
	char	*add = gettext("a = add");
	char	*delete = gettext("d = delete");
	char	*edit = gettext("e = edit");
	char	*quit = gettext("q = quit (back to main menu)");
	char	*help = gettext("? = help");

	keepstart = 0;
	for (;;) {
		erase();
		(void) mvprintw(2, 5, gettext(
		    "----- Dump Tape Expiration Editor -- %s -----"),
			filename);
		showkeeps(keepstart);
		(void) mvprintw(20, 2, "  %s  %s  %s  %s  %s",
			add, delete, edit, quit, help);
		move(20, 1);
		c = zgetch();
		if (c == '\014')
			clear();
		else if (c == quit[0] || c == '\n' || c == '\r')
			return;
		else if (c == '-' || c == '' || c == '') {
			keepstart = keepstart - MAXDEVICEDISPLAY + 1;
			if (keepstart < 0)
				keepstart = 0;
		} else if (c == '+' || c == ' ' || c == '' || c == '') {
			keepstart = keepstart + MAXDEVICEDISPLAY - 1;
			if (keepstart >= ncf_keep)
				keepstart = ncf_keep - 1;
		} else if (c == add[0]) {
			inputborder();
			while (ncf_keep >= maxcf_keep) { /* dynamic growth */
				maxcf_keep += GROW;
				cf_keep = (struct keep_f *)
					checkrealloc((char *) cf_keep,
					/*LINTED [alignment ok]*/
					maxcf_keep * sizeof (struct keep_f));
			}
			cf_keep[ncf_keep].k_level =
				k_input2(gettext("Level"), '0');
			cf_keep[ncf_keep].k_multiple =
				k_input1(gettext("Multiple"), 1);
			cf_keep[ncf_keep].k_days =
				k_input1(gettext("Days"), 60);
			cf_keep[ncf_keep].k_minavail =
				k_input1(gettext("On shelf"), 0);
			ncf_keep++;
			changedtop = 1;
		} else if (c == delete[0]) {
			inputborder();
			inputshow(1, gettext(
	    "Please enter the Id of the tape expiration setting to delete"));
			for (;;) {
				int	n;
				inputshow(2, "> "); /* Combine with checkget */
				checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
				if (in[0] == '\0')
					break;
				n = atoi(in) - 1;
				if (n >= 0 && n < ncf_keep) {
					for (i = n; i + 1 < ncf_keep; i++)
						cf_keep[i] = cf_keep[i + 1];
					ncf_keep--;
					changedtop = 1;
					break;
				}
				inputshow(3, gettext(
				    "Choose a number from 1 through %d"),
					ncf_keep);
			}
			continue;
		} else if (c == edit[0]) {
			inputborder();
			inputshow(1, gettext(
			    "Id of which tape expiration setting to edit:"));
			for (;;) {
				int	n;
				inputshow(2, "> ");
				checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
				if (in[0] == '\0')
					break;
				n = atoi(in) - 1;
				if (n >= 0 && n < ncf_keep) {
					inputclear(3);
					cf_keep[n].k_level =
						k_input2(gettext("Level"),
						cf_keep[n].k_level);
					inputclear(3);
					cf_keep[n].k_multiple =
						k_input1(gettext("Multiple"),
						cf_keep[n].k_multiple);
					inputclear(3);
					cf_keep[n].k_days =
						k_input1(gettext("Days"),
						cf_keep[n].k_days);
					inputclear(3);
					cf_keep[n].k_minavail =
						k_input1(gettext("On shelf"),
						cf_keep[n].k_minavail);
					changedtop = 1;
					break;
				}
				inputshow(3, gettext(
				    "Choose a number from 1 through %d"),
					ncf_keep);
				continue;
			}
			continue;
		} else if (c == help[0])
			scr_help(HELP_KEEP, expert);
		else
			(void) fprintf(stderr, gettext("\007"));
	}
	/* NOTREACHED */
}

static void
showkeeps(start)
{
	int	i;
	int	line;

	if (start == 1)
		(void) mvprintw(2, 10, gettext(
	    "1 tape expiration setting above these (use '-' to display)"));
	else
		(void) mvprintw(2, 10, gettext(
	    "%d tape expiration settings above these (use '-' to display)"),
				start);
	line = 3;
	(void) mvprintw(line++, 3, gettext(
		"Id  Level    Multiple    Days   Onshelf"));
	for (i = start; i < start + MAXDEVICEDISPLAY && i < ncf_keep; i++)
		(void) mvprintw(line++, 3,
		    "%2d%5c%12d%9d%9d", i + 1,
			cf_keep[i].k_level,
			cf_keep[i].k_multiple,
			cf_keep[i].k_days,
			cf_keep[i].k_minavail);
	if (i < ncf_keep) {
		if (ncf_keep - i == 1)
			(void) mvprintw(line, 10, gettext(
		"1 tape expiration setting below these (use '+' to display)"));
		else
			(void) mvprintw(line, 10, gettext(
		"%d tape expiration settings below these (use '+' to display)"),
				ncf_keep - i);
	}
}

/*
 * mailees
 */
void
newmailees(void)
{
	char	c;
	int	mailstart;
	char	in[LINEWID];
	int	i;
	char	*add = gettext("a = add");
	char	*delete = gettext("d = delete");
	char	*edit = gettext("e = edit");
	char	*quit = gettext("q = quit (back to main menu)");
	char	*help = gettext("? = help");

	mailstart = 0;
	for (;;) {
		erase();
		(void) mvprintw(2, 5, gettext(
			"----- Dump Mail Recipient Editor -- %s -----"),
			filename);
		showmailees(mailstart);
		(void) mvprintw(20, 2, "  %s  %s  %s  %s  %s",
			add, delete, edit, quit, help);
		move(20, 1);
		c = zgetch();
		if (c == '\014')
			clear();
		else if (c == quit[0] || c == '\n' || c == '\r')
			return;
		else if (c == '-' || c == '' || c == '') {
			mailstart = mailstart - MAXDEVICEDISPLAY + 1;
			if (mailstart < 0)
				mailstart = 0;
		} else if (c == '+' || c == ' ' || c == '' || c == '') {
			mailstart = mailstart + MAXDEVICEDISPLAY - 1;
			if (mailstart >= ncf_notifypeople)
				mailstart = ncf_notifypeople - 1;
		} else if (c == add[0]) {
			inputborder();
			inputshow(1, gettext(
		"Add after which mail recipient (0 for beginning):"));
			for (;;) {
				int	n;
				inputshow(2, "> ");
				checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
				if (in[0] == '\0')
					break;
				n = atoi(in);
				if (n >= 0 && n <= ncf_notifypeople) {
					inputshow(1, gettext(
				    "Mail recipient to add at slot %d:"),
						n + 1);
					inputclear(3);
					inputshow(2, "> ");
					checkget(LINEWID - 1,
						INBASE + 2, INPUTCOL, in);
					if (in[0] == '\0')
						break;
					changedtop = 1;
					if (cf_cron.c_enable)
						changedcron = 1;
					while (ncf_notifypeople >=
					    maxcf_notifypeople) {
						/* dynamic growth */
						maxcf_notifypeople += GROW;
						cf_notifypeople = (char **)
						    checkrealloc((char *)
							cf_notifypeople,
							maxcf_notifypeople *
						/*LINTED [alignment ok]*/
							    sizeof (char *));
					}
					for (i = ncf_notifypeople - 1;
					    i >= n; i--)
						cf_notifypeople[i + 1] =
							cf_notifypeople[i];
					cf_notifypeople[n] = strdup(in);
					ncf_notifypeople++;
					break;
				}
				inputshow(3, gettext(
				    "Choose a number from 0 through %d"),
					ncf_notifypeople);
				continue;
			}
			continue;
		} else if (c == delete[0]) {
			inputborder();
			inputshow(1, gettext(
		"Please enter the number of the mail recipient to delete"));
			for (;;) {
				int	n;
				inputshow(2, "> "); /* Combine with checkget */
				checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
				if (in[0] == '\0')
					break;
				n = atoi(in);
				if (n > 0 && n <= ncf_notifypeople) {
					for (i = n - 1;
					    i + 1 < ncf_notifypeople; i++)
						cf_notifypeople[i] =
							cf_notifypeople[i + 1];
					ncf_notifypeople--;
					changedtop = 1;
					if (cf_cron.c_enable)
						changedcron = 1;
					break;
				}
				inputshow(3, gettext(
				    "Choose a number from 1 through %d"),
					ncf_notifypeople);
				continue;
			}
			continue;
		} else if (c == edit[0]) {
			inputborder();
			inputshow(1, gettext(
		"Please enter the number of the mail recipient to edit:"));
			for (;;) {
				int	n;

				inputshow(2, "> ");
				checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
				if (in[0] == '\0')
					break;
				n = atoi(in);
				if (n > 0 && n <= ncf_notifypeople) {
					inputshow(1, gettext(
				"Mail recipient to replace at slot %d:"),
						n);
					inputclear(3);
					inputshow(2, "> ");
					checkget(LINEWID - 1,
						INBASE + 2, INPUTCOL, in);
					if (in[0] == '\0')
						break;
					cf_notifypeople[n - 1] = strdup(in);
					changedtop = 1;
					if (cf_cron.c_enable)
						changedcron = 1;
					break;
				}
				inputshow(3, gettext(
				    "Choose a number from 1 through %d"),
					ncf_notifypeople);
				continue;
			}
			continue;
		} else if (c == help[0])
			scr_help(HELP_MAIL, expert);
		else
			(void) fprintf(stderr, gettext("\007"));
	}
	/* NOTREACHED */
}

static void
showmailees(start)
{
	int	i;
	int	line;
	if (start == 1)
		(void) mvprintw(2, 10, gettext(
			"1 mail recipient above these (use '-' to display)"));
	else if (start)
		(void) mvprintw(2, 10, gettext(
			"%d mail recipients above these (use '-' to display)"),
			start);
	line = 4;
	for (i = start;
	    i < start + MAXDEVICEDISPLAY && i < ncf_notifypeople; i++)
		(void) mvprintw(line++, 3,
			"%3d  %s", i + 1, cf_notifypeople[i]);
	if (i < ncf_notifypeople) {
		if (ncf_notifypeople - i == 1)
			(void) mvprintw(line, 10, gettext(
		    "1 mail recipient below these (use '+' to display)"));
		else
			(void) mvprintw(line, 10, gettext(
		    "%d mail recipients below these (use '+' to display)"),
			ncf_notifypeople - i);
	}
}

/* When done with curses */
void
cleanup(void)
{
	move(LINES - 1, 0);
	/* refresh();	this seems to break cmdtool */
	nocurses();
}

struct termios termios;		/* to learn kill character */

int
zgetch(void)
{
	WINDOW *confirm;
	int	i;
	int	c;
	char	ch;

	char	stars[SCREENWID + 1];

	for (;;) {
		refresh();
		c = getch();
		if (c == termios.c_cc[VSUSP]) {
			(void) kill(getpid(), SIGTSTP);
			continue;
		}
		if (c != termios.c_cc[VINTR])	/* control-c or whatever */
			return (c);
		if (changedtop == 0 && changedbottom == 0 && changedcron == 0) {
			cleanup();
			exit(0);
		}
		confirm = newwin(0, 0, 0, 0);
		for (i = 0; i < SCREENWID; i++)
			stars[i] = '*';
		stars[i] = '\0';
		(void) mvwprintw(confirm, 10, 0, "%s", stars);
		(void) mvwprintw(confirm, 11, 0, "%s", stars);
		(void) mvwprintw(confirm, 12, 0, "%s", stars);
		(void) mvwprintw(confirm, 11, 1, gettext(
	" Do you really wish to exit and discard your changes:   "));
		(void) wrefresh(confirm);
		c = getch();
		ch = (char) c;
		if (strncasecmp(&ch, gettext("yes"), 1) == 0) {
			cleanup();
			exit(0);
		}
		touchwin(stdscr);
		delwin(confirm);
		(void) wrefresh(stdscr);
	}
}

/* start curses */
void
setup(void)
{
	if (ioctl(0, TCGETS, &termios) == -1)
		die(gettext("Cannot get terminal attributes\n"));
	curseson = 1;
	(void) initscr();
	raw();
	noecho();
}

void
newdumpsets(void)
{
	char	in[LINEWID];
	int	n;

	inputborder();
	inputshow(1, gettext(
		"How many dumpsets (1 through %d):"), MAXDUMPSETS - 1);
	if (ndumpsets > 1)
		inputshow(3, gettext(
		"Fewer dumpsets can cause loss of some configuration data"));
	for (;;) {
		inputshow(2, "> ");	/* Combine with checkget */
		checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0')
			return;
		n = atoi(in);
		if (n >= 1 && n < MAXDUMPSETS)
			break;
		inputshow(3,
		    gettext("Please choose a number from 1 through %d"),
			MAXDUMPSETS);
	}
	changedbottom = 1;
	while (n < ndumpsets)
		cf_tapeset[ndumpsets--] = 0;
	/* fall through */
	if (n == ndumpsets)
		return;
	/* add new dumpsets: */
	while (ndumpsets < n) {	/* dumpsets start at cf_tapeset[1] */
		cf_tapeset[++ndumpsets] = (struct tapeset_f *)
			/*LINTED [alignment ok]*/
			checkcalloc(sizeof (struct tapeset_f));
		cf_tapeset[ndumpsets]->ts_devlist = cf_tapeset[1]->ts_devlist;
	}
}

void
newtmpdir(void)
{
	char	in[LINEWID];
	struct stat stat_buf;
	inputborder();
	inputshow(1, gettext("Please enter the temp directory:"));
	inputshow(2, "> ");	/* Combine with checkget */
	for (;;) {
		checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0')
			return;
		if (index(in, ' ') != NULL) {
			inputshow(3, gettext(
			    "No blanks allowed in temp directory (`%s')"), in);
			continue;
		}
		if (in[0] != '/') {
			inputshow(3, gettext(
			    "Temp directory must be an absolute path (`%s')"),
			    in);
			continue;
		}
		if (stat(in, &stat_buf) != 0) {
			inputshow(3, gettext(
			    "Cannot stat `%s', %s"), in, strerror(errno));
			continue;
		}
		if ((stat_buf.st_mode & S_IFDIR) == 0) {
			inputshow(3, gettext(
			    "`%s' is not a directory"), in);
			continue;
		}
		break;
	}
	tmpdir = strdup(in);
	changedtop = 1;
}

void
newcron(void)
{
	char	c;
	char	in[LINEWID], temp[MAXLINELEN];
	int	i;
	char	*edit = gettext("e = edit day");
	char	*quit = gettext("q = quit (back to main menu)");
	char	*help = gettext("? = help");
	char	*automatic_toggle = gettext("a = toggle automatic execution");
	char	*dumpex_time = gettext("d = dump time");
	char	*tape_time = gettext("t = tape reminder time");
	char	*day_arr[7];

	day_arr[Mon] = gettext("Monday");
	day_arr[Tue] = gettext("Tuesday");
	day_arr[Wed] = gettext("Wednesday");
	day_arr[Thu] = gettext("Thursday");
	day_arr[Fri] = gettext("Friday");
	day_arr[Sat] = gettext("Saturday");
	day_arr[Sun] = gettext("Sunday");
	for (;;) {
		erase();
		(void) mvprintw(2, 5, gettext(
		    "----- Dump Scheduling Editor -- %s -----"),
			filename);
		showcrons();
		(void) mvprintw(20, 2, "  %s  %s  %s", edit, quit, help);
		(void) mvprintw(21, 2, "  %s  %s  %s",
			automatic_toggle, dumpex_time, tape_time);
		move(20, 1);
		c = zgetch();
		if (c == '\014')
			clear();
		else if (c == quit[0] || c == '\n' || c == '\r')
			return;
		else if (c == dumpex_time[0]) {
			inputborder();
			cf_cron.c_dtime = k_input4(gettext(
		    "Please enter the 4-digit 24-hour time to start dumps"),
				cf_cron.c_dtime);
			changedcron = 1;
		} else if (c == tape_time[0]) {
			inputborder();
			cf_cron.c_ttime = k_input4(gettext(
	    "Please enter the 4-digit 24-hour time to send a tape reminder"),
				cf_cron.c_ttime);
			changedcron = 1;
		} else if (c == automatic_toggle[0]) {
			changedcron = 1;
			cf_cron.c_enable = !cf_cron.c_enable;
		} else if (c == edit[0]) {
			inputborder();
			inputshow(1, gettext(
				"Id of which day to edit:"));
			for (;;) {
				int	n;
				inputshow(2, "> ");
				checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
				if (in[0] == '\0')
					break;
				n = atoi(in) - 1;
				if (n >= 0 && n < 7) {
					inputclear(3);
					(void) sprintf(temp, gettext(
					    "Enable backups at %04d on %s?"),
						cf_cron.c_dtime,
						day_arr[n]);
					cf_cron.c_ena[n] = k_input3(temp,
						cf_cron.c_ena[n]);
					if (cf_cron.c_ena[n]) {
						inputclear(3);
						(void) sprintf(temp, gettext(
					    "Switch to a new tape every %s?"),
							day_arr[n]);
						cf_cron.c_new[n] =
							k_input3(temp,
							cf_cron.c_new[n]);
					} else {
						cf_cron.c_new[n] = 0;
					}
					changedcron = 1;
					break;
				}
				inputshow(3, gettext(
				    "Choose a number from 1 through %d"), 7);
				continue;
			}
			continue;
		} else if (c == help[0])
			scr_help(HELP_SCHED, expert);
		else
			(void) fprintf(stderr, gettext("\007"));
	}
	/* NOTREACHED */
}

static void
showcrons(void)
{
	int	i;
	int	line;
	struct tm *timeptr;
	time_t	now;
	char	timebuf[20];
	char	*s;
	char	*proto = gettext("%2d%5s%8s%12s");

#define	yn(flag)	((flag) ? gettext("x") : gettext(" "))
	line = 4;
	(void) mvprintw(line++, 3, gettext(
		"Id  Day    Enable?    New Tape?"));
	(void) mvprintw(line++, 3, proto, 1, gettext("Mon"),
		yn(cf_cron.c_ena[Mon]), yn(cf_cron.c_new[Mon]));
	(void) mvprintw(line++, 3, proto, 2, gettext("Tue"),
		yn(cf_cron.c_ena[Tue]), yn(cf_cron.c_new[Tue]));
	(void) mvprintw(line++, 3, proto, 3, gettext("Wed"),
		yn(cf_cron.c_ena[Wed]), yn(cf_cron.c_new[Wed]));
	(void) mvprintw(line++, 3, proto, 4, gettext("Thu"),
		yn(cf_cron.c_ena[Thu]), yn(cf_cron.c_new[Thu]));
	(void) mvprintw(line++, 3, proto, 5, gettext("Fri"),
		yn(cf_cron.c_ena[Fri]), yn(cf_cron.c_new[Fri]));
	(void) mvprintw(line++, 3, proto, 6, gettext("Sat"),
		yn(cf_cron.c_ena[Sat]), yn(cf_cron.c_new[Sat]));
	(void) mvprintw(line++, 3, proto, 7, gettext("Sun"),
		yn(cf_cron.c_ena[Sun]), yn(cf_cron.c_new[Sun]));

	line += 2;
	(void) mvprintw(line++, 3, gettext(
		"a. Automatic Dump Execution: %-4s  (toggle)"),
		cf_cron.c_enable ? gettext("Yes") : gettext("No"));

	/* crud to compute am/pm internationally */
	now = time((time_t *) 0);
	timeptr = localtime(&now);
	timeptr->tm_min = cf_cron.c_dtime % 100;
	timeptr->tm_hour = cf_cron.c_dtime / 100;
	/*
	 * The format string is broken into two halfs, which the compiler
	 * puts back together.  This is done because sccs mucks up the string.
	 */
	(void) strftime(timebuf, sizeof (timebuf), "%I:%M" "%p", timeptr);
	for (s = timebuf; *s; s++)
		if (isupper(*s))
			*s = tolower(*s);
	(void) mvprintw(line++, 3, gettext(
		"d. Dump Execution Time:      %04d  (%s)  "),
		cf_cron.c_dtime, timebuf[0] == '0' ? timebuf + 1 : timebuf);
	timeptr->tm_min = cf_cron.c_ttime % 100;
	timeptr->tm_hour = cf_cron.c_ttime / 100;
	(void) strftime(timebuf, sizeof (timebuf), "%I:%M" "%p", timeptr);
	for (s = timebuf; *s; s++)
		if (isupper(*s))
			*s = tolower(*s);
	(void) mvprintw(line++, 3, gettext(
		"t. Tape Reminder Time:       %04d  (%s)  "),
		cf_cron.c_ttime, timebuf[0] == '0' ? timebuf + 1 : timebuf);
}

void
dumped_quit(void)
{
	char	*new = checkalloc(strlen(filename) + 5);
	char	*bak = checkalloc(strlen(filename) + 5);

	if (changedbottom || changedtop || changedcron) {
		inputborder();
		if (k_input3(gettext(
		    "Save changes before exiting? (y/n)"), 1) == 0)
			return;

		(void) sprintf(new, "%s.new", filename);
		(void) sprintf(bak, "%s.bak", filename);
		makeit(new);
		if (rename(filename, bak) == -1)
			die2(gettext(
		    "Rename from %s to %s failed.  Your output is in %s."),
				filename, bak, new);
		if (rename(new, filename) == -1)
			die2(gettext(
		    "Rename from %s to %s failed.  Your output is in %s."),
				new, filename, new);
	}
}

static void
makeit(file)
	char	*file;
{
	int	i;
	FILE	*out = fopen(file, "w");
	char	*warn = "# NEVER EDIT THIS FILE BY HAND.  USE dumped(1M).\n\n";
	char	*lwarn = gettext(warn);

	if (out == NULL && strcmp(file, "/tmp/dumped.out") != 0)
		die2(gettext("Sorry -- cannot write your file"));
	if (out == NULL) {
		makeit("/tmp/dumped.out");
		die2(gettext(
		    "Cannot write `%s' so output is in /tmp/dumped.out\n"));
	}
	inputshow(3, gettext("Saving configuration file `%s'..."), filename);
	refresh();
	(void) fprintf(out, "%s\n", configfilesecurity);
	(void) fprintf(out,
		"# NEVER EDIT THIS FILE BY HAND.  USE dumped(1M).\n\n");
	if (strcmp(warn, lwarn) != 0)
		(void) fprintf(out, gettext(
		    "# NEVER EDIT THIS FILE BY HAND.  USE dumped(1M).\n\n"));

	(void) fprintf(out, "tapelib   %s\n", cf_tapelib);
	if (cf_dumplib && cf_dumplib[0])
		(void) fprintf(out, "dumpmach  %s\n", cf_dumplib);
	(void) fprintf(out, "dumpdevs ");
	for (i = 0; i < ncf_dumpdevs; i++)
		(void) fprintf(out, " %s", cf_dumpdevs[i]);
	(void) fprintf(out, "\n");
	(void) fprintf(out, "block     %d\n", cf_blockfac);
	(void) fprintf(out, "tapesup   %d\n", cf_tapesup);
	if (ncf_notifypeople) {
		(void) fprintf(out, "notify   ");
		for (i = 0; i < ncf_notifypeople; i++)
			(void) fprintf(out, " %s", cf_notifypeople[i]);
		(void) fprintf(out, "\n");
	}
	if (cf_rdevuser && cf_rdevuser[0])
		(void) fprintf(out, "rdevuser  %s\n", cf_rdevuser);
	if (cf_longplay)
		(void) fprintf(out, "longplay\n");
	(void) fprintf(out, "cron      %d %d %d",
		cf_cron.c_enable, cf_cron.c_dtime, cf_cron.c_ttime);
	for (i = 0; i < 7; i++)
		(void) fprintf(out, " %d %d", cf_cron.c_ena[i],
			cf_cron.c_new[i]);
	(void) fprintf(out, "\n");
	(void) fprintf(out, "tmpdir   %s\n", tmpdir);
	(void) fprintf(out, "\n");

	(void) fprintf(out, "#    level   multiple   days   min available\n");

	for (i = 0; i < ncf_keep; i++)
		(void) fprintf(out, "keep    %c%9d%9d%10d\n",
			cf_keep[i].k_level,
			cf_keep[i].k_multiple,
			cf_keep[i].k_days,
			cf_keep[i].k_minavail);

	(void) fprintf(out, "\nmastercycle %05.5d\n", cf_mastercycle);

	if (changedbottom) {
		for (i = 1; i <= ndumpsets; i++) {
			int	j;

			if (cf_tapeset[i] == NULL)
				continue;
			(void) fprintf(out, "\nset %d\n", i);
			for (j = 0; j < nfilesystems; j++)
				if (addedfs)
					(void) fprintf(out,
					    "fullcycle %05.5d +%-25s %s\n",
						fs[j]->dc_fullcycle,
						fs[j]->dc_filesys + 1,
						fs[j]->dc_dumplevels);
				else
					(void) fprintf(out,
					    "fullcycle %05.5d %-26s %s\n",
						fs[j]->dc_fullcycle,
						fs[j]->dc_filesys,
						fs[j]->dc_dumplevels);
		}
	} else
		for (i = 1; i <= ndumpsets; i++) {
			struct devcycle_f *d;
			if (cf_tapeset[i] == NULL)
				continue;
			(void) fprintf(out, "\nset %d\n", i);
			for (d = cf_tapeset[i]->ts_devlist; d; d = d->dc_next)
				(void) fprintf(out,
				    "fullcycle %05.5d %-26s %s\n",
					d->dc_fullcycle,
					d->dc_filesys,
					d->dc_dumplevels);
		}
	if (fclose(out) == EOF)
		die2(gettext(
	    "Warning: configuration file `%s' was not closed cleanly\n"),
			file);
	sleep(1);

	if (changedcron) {
		FILE *crontab, *tmpf;
		char line[MAXLINELEN], prog[MAXLINELEN];
		char tmp_file[MAXPATHLEN];
		int nenable, hour, minute, ret, oldcron;
#define	NMATCHES	8
		char matches[NMATCHES][MAXLINELEN];

		/*
		 * Read current crontab file to a temporary file.
		 * Remove existing lines for this configuration.
		 * Add new lines for this cron specification.
		 * Load the new crontab file back from temporary file.
		 */
		inputshow(3, gettext(
	    "Updating crontab information...          \b\b\b\b\b\b\b\b\b\b"));
		refresh();
		/* become root for real... */
		(void) setuid(0);
		(void) tmpnam(tmp_file);
		/*
		 * Make sure the crontab command is executable by root.
		 */
		if (access("/usr/bin/crontab", X_OK) != 0) {
			(void) printf(gettext(
		"Cannot read current crontab file; crontab not updated.\n"));
			goto out;
		}
		/*
		 * Execute "crontab -l" to see if a current crontab exists
		 * for root.
		 */
		if ((crontab = popen(
"sh -c '( /usr/bin/crontab -l ) > /dev/null 2>&1;echo ==$?'", "r")) == NULL)
		{
			(void) printf(gettext(
		"Cannot read current crontab file; crontab not updated.\n"));
			goto out;
		}
		if (fscanf(crontab, "==%d", &ret) != 1) {
			(void) printf(gettext(
		"Cannot read current crontab file; crontab not updated.\n"));
			goto out;
		}
		oldcron = (ret == 0);
		pclose(crontab);
		crontab = NULL;
		if (oldcron) {
			if ((crontab = popen("/usr/bin/crontab -l", "r")) ==
			    NULL || (tmpf = fopen(tmp_file, "w+")) == NULL) {
				(void) printf(gettext(
		"Cannot read current crontab file; crontab not updated.\n"));
				goto out;
			}
		} else {
			if ((tmpf = fopen(tmp_file, "w+")) == NULL) {
				(void) printf(gettext(
		"Cannot read current crontab file; crontab not updated.\n"));
				goto out;
			}
		}

		/* setup the matches array */
		(void) sprintf(matches[0], "%s/%s %s ", gethsmpath(sbindir),
			"dumpex", filename);
		(void) sprintf(matches[1], "%s/%s %s\n", gethsmpath(sbindir),
			"dumpex", filename);
		(void) sprintf(matches[2], "%s/%s -s %s ", gethsmpath(sbindir),
			"dumpex", filename);
		(void) sprintf(matches[3], "%s/%s -s %s\n", gethsmpath(sbindir),
			"dumpex", filename);
		(void) sprintf(matches[4], "%s/%s -n %s ", gethsmpath(sbindir),
			"dumpex", filename);
		(void) sprintf(matches[5], "%s/%s -n %s\n", gethsmpath(sbindir),
			"dumpex", filename);
		(void) sprintf(matches[6], "%s/%s -n -s %s ",
			gethsmpath(sbindir), "dumpex", filename);
		(void) sprintf(matches[7], "%s/%s -n -s %s\n",
			gethsmpath(sbindir), "dumpex", filename);

		/*
		 * make a copy of the current crontab file, removing existing
		 * dumpex entries for this configuration.
		 */
		if (oldcron) {
			while (fgets(line, sizeof (line), crontab) != NULL) {
				for (i = 0; i < NMATCHES; i++)
					if (strstr(line, matches[i]) != NULL)
						break;		/* strip it */
				if (i == NMATCHES && fputs(line, tmpf) == EOF) {
					(void) printf(gettext(
		"Cannot write temporary crontab file; crontab not updated.\n"));
					goto out;
				}
			}
			(void) pclose(crontab);
			crontab = NULL;
		}

		/* see if we have anything to do... */
		for (nenable = i = 0; i < 7; i++)
			if (cf_cron.c_ena[i] != 0)
				nenable++;
		if (cf_cron.c_enable == 0 || nenable == 0)
			goto writeit;

		/* add the new entries into the temp crontab file */
		/* first, a header */
		(void) fprintf(tmpf, gettext(
			"# %s/%s %s lines: only change with dumped(1M).\n"),
			gethsmpath(sbindir), "dumpex", filename);
		hour = cf_cron.c_dtime / 100;
		minute = cf_cron.c_dtime % 100;
		for (i = 0; i < 7; i++) {
			int j;

			if (cf_cron.c_ena[i] == 0)
				continue;
			(void) fprintf(tmpf,
		    "%02.2d %02.2d * * %d %s/%s %s%s >/dev/null 2>&1\n",
				minute, hour, (i == 6) ? 0 : i + 1,
				gethsmpath(sbindir), "dumpex",
				cf_cron.c_new[i] ? "-s " : "", filename);
			if (ncf_notifypeople <= 0)
				continue;
			(void) fprintf(tmpf,
	    "%02.2d %02.2d * * %d %s/%s -n %s%s | /usr/bin/mailx -s '%s %s/%s'",
				cf_cron.c_ttime % 100, cf_cron.c_ttime / 100,
				(cf_cron.c_ttime > cf_cron.c_dtime) ? i :
				((i == 6) ? 0 : i + 1), gethsmpath(sbindir),
				"dumpex", cf_cron.c_new[i] ? "-s " : "",
				filename, gettext("Tape reminder for"),
				hostname, filename);
			for (j = 0; j < ncf_notifypeople; j++) {
				(void) fprintf(tmpf, " %s", cf_notifypeople[j]);
			}
			(void) fprintf(tmpf, " >/dev/null 2>&1\n");
		}
		/* end with a trailer */
		(void) fprintf(tmpf, gettext("# End of %s/%s %s lines.\n"),
			gethsmpath(sbindir), "dumpex", filename);

writeit:
		/* write out the new crontab file */
		(void) fflush(tmpf);
		(void) fclose(tmpf);
		tmpf = NULL;
		(void) sprintf(prog, "/usr/bin/crontab < %s", tmp_file);
		if (system(prog) != 0) {
			(void) printf(gettext(
		    "Cannot load new crontab file; crontab not changed.\n"));
		}

out:
		/* clean up */
		if (crontab)
			(void) pclose(crontab);
		if (tmpf)
			(void) fclose(tmpf);
		(void) unlink(tmp_file);
		sleep(1);
	}
}

/* get a number, with a default if return pressed */
static int
k_input1(mesg, deflt)
	char	*mesg;
	int	deflt;
{
	char	in[LINEWID];

	inputshow(1, "%s [%d]", mesg, deflt);
	inputshow(2, "> ");
	checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
	return (in[0] == '\0' ? deflt : atoi(in));
}

/* get a character, with a default if return pressed */
static int
k_input2(mesg, deflt)
	char	*mesg;
	int	deflt;
{
	char	in[LINEWID];

	inputshow(1, "%s [%c]", mesg, deflt);
	inputshow(2, "> ");
	checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
	return (in[0] == '\0' ? deflt : in[0]);
}

/* get a yes/no response, with a default if return pressed */
int
k_input3(char *mesg, int deflt)
{
	char	in[LINEWID];
	char	*yes = gettext("yes");
	char	*no = gettext("no");

	for (;;) {
		inputshow(1, "%s [%s]", mesg,
			deflt ? gettext("y") : gettext("n"));
		inputshow(2, "> ");
		checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0')
			return (deflt);
		if (strncasecmp(in, yes, 1) == 0)
			return (1);
		if (strncasecmp(in, no, 1) == 0)
			return (0);
		inputshow(3, gettext(
			"Please respond with either \"%s\" or \"%s\"."),
			yes, no);
	}
}

/* get a 4-digit, 24-hour format time value, with a default if return pressed */
int
k_input4(char *mesg, int deflt)
{
	char	in[LINEWID];
	char	*s;
	int	intime, hour, minute;

	for (;;) {
		inputshow(1, "%s [%04d]", mesg, deflt);
		inputshow(2, "> ");
		checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0')
			return (deflt);
		for (s = in; *s; s++) {
			if (!isdigit(*s)) {
				inputshow(3, gettext(
				    "Please enter only digits.             "));
				break;
			}
		}
		if (*s)
			continue;
		intime = atoi(in);
		hour = intime / 100;
		minute = intime % 100;
		if (hour < 0 || hour >= HOURSPERDAY) {
			inputshow(3, gettext(
				"Hours range from 0 through %d."),
				HOURSPERDAY - 1);
			continue;
		}
		if (minute < 0 || minute >= MINUTESPERHOUR) {
			inputshow(3, gettext(
				"Minutes range from 0 through %d."),
				MINUTESPERHOUR - 1);
			continue;
		}
		return (intime);
	}
}
