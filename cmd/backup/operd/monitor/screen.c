/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ident	"@(#)screen.c 1.0 91/01/28 SMI"

#ident	"@(#)screen.c 1.30 92/07/20"

#include "defs.h"
#include "objects.h"
#include <stdio.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <curses.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>

#define	MINROWS		10
#define	MINCOLS		60
#define	DEFROWS		24
#define	DEFCOLS		80
#define	MAXCOLS		256

#define	CMDPCT		20		/* % of total screen for commands */

static int	scrinit;		/* =1 if scr_config() called */
static int	screen_lines = 24;
static int	screen_cols = DEFCOLS;

int	killch;				/* line kill character */
int	erasech;			/* backspace character */

/*
 * "tags" is initialized via a call
 * to gettext() in scr_config().
 */
static u_char *tags;
int	tagall;				/* tag all displayable messages */

#define	OVERHEAD	4		/* top, 2 for queue status, bottom */
#define	CMD_LINES	3
#define	MSGWIN_TOP	2

static WINDOW	*cmdwin;

static WINDOW	*msgwin;
static int	msg_lines;
static int	msg_indent = 3;		/* <tag><disp><space> */

static int clocklen = 16;		/* length of clock/date string */

#define	INTERVAL	5

time_t	current_time;
time_t	screen_hold;			/* keep expire from updating screen */
time_t	suspend;			/* user-specified hold screen */

static struct itimerval itv;		/* for interval timer */

#ifdef __STDC__
static void ontstp(int);
static void onalrm(int);
static void retag(tag_t	*, tag_t *);
static void dorefresh(WINDOW *, int);
static char *lctime(time_t *);
#else
static void ontstp();
static void onalrm();
static void retag();
static void dorefresh();
static char *lctime();
#endif

/*ARGSUSED*/
static void
ontstp(sig)
	int	sig;
{
#ifndef USG
	SGTTY	tty;

	tty = _tty;
#else
	(void) def_prog_mode();
#endif
	mvcur(0, screen_cols - 1, screen_lines - 1, 0);
	(void) endwin();
	(void) fflush(stdout);
	(void) signal(SIGTSTP, SIG_DFL);
#ifndef USG
	(void) sigsetmask(sigblock(0) &~ sigmask(SIGTSTP));
#else
	(void) sigrelse(SIGTSTP);
#endif
	(void) kill(0, SIGTSTP);
#ifdef USG
	(void) sighold(SIGTSTP);
#else
	(void) sigblock(sigmask(SIGTSTP));
#endif
	if (!doinghelp && connected) {
		(void) oper_logout(opserver);
		if (oper_login(opserver, 0) == OPERMSG_SUCCESS)
			status(1, gettext(
			    "Login succeeded: now connected to server '%s'."),
				opserver);
		else {
			bell();
			status(1, gettext(
			    "Login failed: not connected to any server"));
			connected = (char *)0;
		}
	}
#ifndef USG
	_tty = tty;
	(void) stty(_tty_ch, &_tty);
#else
	(void) reset_prog_mode();
	(void) sigrelse(SIGTSTP);
#endif
	(void) signal(SIGTSTP, ontstp);
	scr_redraw(1);
}

/*ARGSUSED*/
static void
onalrm(sig)
	int	sig;
{
	static int cnt;
	int	x, y;

	(void) time(&current_time);
	if (suspend && (current_time > suspend)) {
		suspend = 0;
		status(1, gettext("Resuming message tag updates"));
	}
	getyx(curscr, y, x);		/* may be interrupting output */
	scr_clock(&objects[OBJ_CLOCK]);
	move(y, x);			/* restore position */
	if (!doinghelp)
		dorefresh(stdscr, TRUE);
	if (++cnt > 60/INTERVAL) {
		expire_all();		/* mark/rm expired messages */
		if (screen_hold || suspend)
			cnt = 55;	/* try removes again */
		else
			cnt = 0;
	}
}

void
#ifdef __STDC__
scr_config(void)
#else
scr_config()
#endif
{
	struct winsize size;
	struct sigvec sv;

	(void) signal(SIGTERM, (void (*)(int))cmd_exit);
	(void) signal(SIGHUP, (void (*)(int))cmd_exit);
	(void) signal(SIGINT, (void (*)(int))cmd_exit);
	(void) signal(SIGKILL, (void (*)(int))cmd_exit);

	(void) initscr();
	(void) cbreak();
	(void) noecho();

	scrinit++;

	(void) signal(SIGTSTP, ontstp);

	killch = (u_char)killchar();
	erasech = (u_char)erasechar();
	/*
	 * XGETTEXT:  The following string contains the
	 * single-character tags by which users select
	 * messages for response or erasure.
	 */
	tags = (u_char *)gettext(
		"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");

	if (ioctl(1, TIOCGWINSZ, &size) < 0) {
		status(1, gettext(
		    "cannot get screen dimensions -- assuming size of %dx%d"),
			screen_cols, screen_lines);
	} else {
		/*
		 * don't know why this happens...
		 */
		if ((int)size.ws_row == 0)
			screen_lines = DEFROWS;
		else
			screen_lines = (int)size.ws_row;
		if ((int)size.ws_col == 0)
			screen_cols = DEFCOLS;
		else
			screen_cols = (int)size.ws_col;
	}
	if (screen_lines < MINROWS || screen_cols < MINCOLS) {
		status(0, gettext(
		    "%s requires screen dimensions of at least %dx%d"),
			progname, MINROWS, MINCOLS);
		Exit(-1);
	}

	if (screen_cols > MAXCOLS)
		screen_cols = MAXCOLS;

	helpinit(screen_lines, screen_cols);

	cmdwin = subwin(stdscr, CMD_LINES, screen_cols,
	    screen_lines - (CMD_LINES + 1), 0);
	if (cmdwin == NULL) {
		status(0, gettext("cannot initialize command sub-window"));
		Exit(-1);
	}
	scrollok(cmdwin, FALSE);
	clearok(cmdwin, FALSE);

	msg_lines = screen_lines - OVERHEAD - CMD_LINES;
	msgwin = subwin(stdscr, msg_lines, screen_cols, MSGWIN_TOP, 0);
	if (msgwin == NULL) {
		status(0, gettext("cannot initialize message sub-window"));
		Exit(-1);
	}
	scrollok(msgwin, FALSE);
	clearok(msgwin, FALSE);

	leaveok(stdscr, FALSE);
	clearok(stdscr, FALSE);

	objects[OBJ_SERVER].obj_name = gettext("Server:");

	clocklen = strlen(lctime(&current_time));
	objects[OBJ_CLOCK].obj_x = screen_cols - clocklen;

	objects[OBJ_PROMPT].obj_y = screen_lines - (CMD_LINES + 1);
	objects[OBJ_INPUT].obj_y = objects[OBJ_PROMPT].obj_y;
	objects[OBJ_BELOW].obj_y = objects[OBJ_PROMPT].obj_y - 1;
	objects[OBJ_STATUS].obj_y = objects[OBJ_PROMPT].obj_y + 1;
	objects[OBJ_FILTER].obj_y = screen_lines - 1;
	mainprompt = 1;
	resetprompt();
	/*
	 * Install alarm handler and turn things on.
	 */
#ifdef USG
	(void) sigemptyset(&sv.sa_mask);
#else
	sv.sv_mask = 0;
#endif
	sv.sv_flags = SA_RESTART;
	sv.sv_handler = onalrm;
	(void) sigvec(SIGALRM, &sv, (struct sigvec *)0);

	(void) time(&current_time);
	itv.it_interval.tv_sec = itv.it_value.tv_sec = INTERVAL;
	itv.it_interval.tv_usec = itv.it_value.tv_usec = 0;
	if (setitimer(ITIMER_REAL, &itv, (struct itimerval *)0) < 0) {
		status(0, "setitimer: %s", strerror(errno));
		Exit(-1);
	}
}

void
#ifdef __STDC__
scr_cleanup(void)
#else
scr_cleanup()
#endif
{
	if (!scrinit)
		return;
	(void) signal(SIGALRM, SIG_IGN);
	nocbreak();
	echo();
	dorefresh(stdscr, TRUE);
	mvcur(0, screen_cols - 1, screen_lines - 1, 0);
	endwin();
	(void) printf("\n");
	(void) fflush(stdout);
	scrinit = 0;
}

void
scr_redraw(doclear)
	int	doclear;		/* clear-screen first */
{
	register struct object *objp;
	register struct msg_cache *mc;
	register u_char *tp = tags;
	register int y;
	time_t	h;

	if (doinghelp)
		return;
	h = scr_hold(0);
	if (doclear)
		wclear(stdscr);
	if (top) {
		y = 0;		/* XXX within msgwin, not stdscr */
		for (mc = top; mc != bottom->mc_nextrcvd;
		    mc = mc->mc_nextrcvd) {
			if ((mc->mc_status & DISPLAY) == 0)
				continue;
			if (mc->mc_status & NEEDREPLY || tagall)
				mc->mc_tag = (tag_t)(*tp++);
			else
				mc->mc_tag = NOTAG;
			scr_msg(mc, y);
			y += mc->mc_nlines;
		}
		wclrtobot(msgwin);
	} else
		werase(msgwin);
#ifdef USG
	/*
	 * XXX I don't know why I need this, but
	 * things don't work without it.  Maybe
	 * a 5.0 curses bug? [sas]
	 */
	if (!doclear)
		dorefresh(msgwin, FALSE);
#endif
	for (objp = objects; objp < &objects[nobjects]; objp++)
		if (objp->obj_func)
			(*objp->obj_func)(objp);
	scr_release(h);
	dorefresh(stdscr, TRUE);
	doupdate();
}

void
scr_reverse(mc)
	struct msg_cache *mc;
{
	mc->mc_status ^= REVERSE;
	scr_redraw(0);
}

/*
 * Adjust display params (top and bottom pointers)
 */
void
scr_adjust(mc)
	struct msg_cache *mc;
{
	register struct msg_cache *new;

	if (top == mc) {
		for (new = top->mc_prevrcvd;
		    new != &timeorder; new = new->mc_prevrcvd)
			if (msg_filter(new))
				break;
		if (new == &timeorder || new->mc_nlines > top->mc_nlines)
			for (new = top->mc_nextrcvd;
			    new != &timeorder; new = new->mc_nextrcvd)
				if (msg_filter(new))
					break;
		top = (new == &timeorder) ? NULL : new;
	}
	if (bottom == mc) {
		for (new = bottom->mc_nextrcvd;
		    new != &timeorder; new = new->mc_nextrcvd)
			if (msg_filter(new))
				break;
		if (new == &timeorder || new->mc_nlines > bottom->mc_nlines)
			for (new = bottom->mc_prevrcvd;
			    new != &timeorder; new = new->mc_prevrcvd)
				if (msg_filter(new))
					break;
		bottom = (new == &timeorder) ? NULL : new;
	}
}

/*
 * Determine what cached messages will fit
 * on the screen given a pointer to the first
 * message to be displayed (set this pointer
 * if necessary) and ajust the display accordingly.
 * Sets the pointer to the last displayed message
 * and the number of messages above and below
 * the display.
 */
void
scr_topdown(reset)
	int	reset;
{
	register struct msg_cache *mc;
	int linesused = 0;
	time_t	h;

	h = scr_hold(0);
	msgs_above = msgs_below = 0;
	if (!top || reset) {
		werase(msgwin);
		for (mc = timeorder.mc_nextrcvd;
		    mc != &timeorder; mc = mc->mc_nextrcvd) {
			if (msg_filter(mc))
				break;
			mc->mc_status &= ~DISPLAY;
		}
		if (mc != &timeorder)
			top = mc;
	} else if (!suspend && msg_filter(top) == 0)
		scr_adjust(top);
	if (top) {
		for (mc = timeorder.mc_nextrcvd;
		    mc != top; mc = mc->mc_nextrcvd) {
			if (msg_filter(mc))
				msgs_above++;
			mc->mc_status &= ~DISPLAY;
		}
		for (mc = top; mc != &timeorder &&
		    (mc->mc_nlines+linesused) <= msg_lines;
		    mc = mc->mc_nextrcvd) {
			if (msg_filter(mc)) {
				linesused += mc->mc_nlines;
				mc->mc_status |= DISPLAY;
				bottom = mc;
			} else
				mc->mc_status &= ~DISPLAY;
		}
		for (mc = bottom->mc_nextrcvd;
		    mc != &timeorder; mc = mc->mc_nextrcvd) {
			if (msg_filter(mc))
				msgs_below++;
			mc->mc_status &= ~DISPLAY;
		}
	}
	scr_release(h);
}

/*
 * Like above, but fill given a pointer to
 * the last displayed message (establish
 * that pointer if necessary).
 */
void
scr_bottomup(reset)
	int	reset;
{
	register struct msg_cache *mc;
	int linesused = 0;
	int adjust = suspend ? 0 : 1;
	time_t	h;

	h = scr_hold(0);
	msgs_above = msgs_below = 0;
	if (!bottom || reset) {
		werase(msgwin);
		for (mc = timeorder.mc_prevrcvd;
		    mc != &timeorder; mc = mc->mc_prevrcvd) {
			if (msg_filter(mc))
				break;
			mc->mc_status &= ~DISPLAY;
		}
		if (mc != &timeorder)
			bottom = mc;
	} else if (adjust && msg_filter(bottom) == 0)
		scr_adjust(bottom);
	if (bottom) {
		for (mc = timeorder.mc_prevrcvd;
		    mc != bottom; mc = mc->mc_prevrcvd) {
			if (msg_filter(mc))
				msgs_below++;
			mc->mc_status &= ~DISPLAY;
		}
		if (top == NULL)
			adjust++;
		for (mc = bottom; mc != &timeorder &&
		    (mc->mc_nlines+linesused) <= msg_lines;
		    mc = mc->mc_prevrcvd) {
			if (msg_filter(mc)) {
				linesused += mc->mc_nlines;
				mc->mc_status |= DISPLAY;
				if (adjust)
					top = mc;
			} else
				mc->mc_status &= ~DISPLAY;
		}
		for (mc = top->mc_prevrcvd;
		    mc != &timeorder; mc = mc->mc_prevrcvd) {
			if (msg_filter(mc))
				msgs_above++;
			mc->mc_status &= ~DISPLAY;
		}
	}
	scr_release(h);
}

/*
 * Scroll forward, displaying more
 * recently received messages.
 */
void
#ifdef __STDC__
scr_next(void)
#else
scr_next()
#endif
{
	register struct msg_cache *mc;
	int linesused = 0;
	time_t	h;

	h = scr_hold(0);
	if (!top || !bottom || bottom->mc_nextrcvd == &timeorder) {
		scr_release(h);
		bell();
		return;
	}
	for (mc = bottom->mc_nextrcvd; mc != &timeorder &&
	    (mc->mc_nlines+linesused) <= msg_lines;
	    mc = mc->mc_nextrcvd) {
		if (msg_filter(mc)) {
			linesused += mc->mc_nlines;
			mc->mc_status |= DISPLAY;
			bottom = mc;
		} else
			mc->mc_status &= ~DISPLAY;
	}
	scr_bottomup(0);
	scr_release(h);
}

/*
 * Scroll backward, displaying less
 * recently received messages.
 */
void
#ifdef __STDC__
scr_prev(void)
#else
scr_prev()
#endif
{
	register struct msg_cache *mc;
	int linesused = 0;
	time_t	h;

	h = scr_hold(0);
	if (!top || !bottom || top->mc_prevrcvd == &timeorder) {
		scr_release(h);
		bell();
		return;
	}
	for (mc = top->mc_prevrcvd; mc != &timeorder &&
	    (mc->mc_nlines+linesused) <= msg_lines;
	    mc = mc->mc_prevrcvd) {
		if (msg_filter(mc)) {
			linesused += mc->mc_nlines;
			mc->mc_status |= DISPLAY;
			top = mc;
		} else
			mc->mc_status &= ~DISPLAY;
	}
	scr_topdown(0);
	scr_release(h);
}

/*
 * The externally callable function that
 * adds a message to the bottom of the
 * message display area -- the message is
 * assumed to have already passed through
 * msg_filter.
 */
void
scr_add(new)
	struct msg_cache *new;
{
	register struct msg_cache *mc = new;
	time_t	h;

	msg_format(new, screen_cols, msg_indent);
	if (msg_filter(new) == 0)
		return;
	h = scr_hold(0);
	if (bottom) {
		/*
		 * If any displayable messages exist in the
		 * cache, are more recently received than
		 * the message we just added  but are not
		 * displayed, don't display the new message.
		 */
		for (mc = bottom->mc_nextrcvd;
		    mc != new && mc != &timeorder; mc = mc->mc_nextrcvd)
			if (msg_filter(mc))
				break;
	}
	scr_release(h);
	if (mc == new) {
		bottom = new;
		new->mc_status |= DISPLAY;
		if (suspend)
			scr_topdown(0);
		else
			scr_bottomup(1);
	} else
		msgs_below++;
	resetprompt();
	scr_redraw(0);
}

time_t
scr_hold(duration)
	int	duration;
{
	time_t	old = screen_hold;

	if (duration) {			/* user lock */
		(void) time(&screen_hold);
		screen_hold += duration;
	} else
		screen_hold = ~0;	/* program lock */
	return (old);
}

void
scr_release(hold)
	time_t	hold;
{
	screen_hold = hold;
}

/*
 * Screen object rendereing functions
 */
static void
scr_server(objp)
	struct object *objp;
{
	/*LINTED [alignment ok]*/
	char	*str = *(char **)(objp->obj_val);
	char	*server;

	if ((server = malloc(screen_cols)) == NULL) {
		fprintf(stderr, gettext("%s: malloc failed\n"), progname);
		Exit(1);
	}

	move(objp->obj_y, objp->obj_x);
	if (str && *str) {
		printw("%s ", objp->obj_name);
		(void) strncpy(server, str,
		    screen_cols - strlen(objp->obj_name) - clocklen - 2);
	} else
		(void) strcpy(server, gettext("Not connected"));
	printw("%s", server);
	free(server);
	clrtoeol();
}

static void
scr_qstatus(objp)
	struct object *objp;
{
	/*LINTED [alignment ok]*/
	int	n = *(int *)(objp->obj_val);

	move(objp->obj_y, objp->obj_x);
	if (n != 0) {
		if (objp->obj_val == (char *)&msgs_above) {
			/*
			 * XGETTEXT:  The following four messages should
			 * each fit on a single [60-column] line.
			 */
			if (n > 1)
				printw(gettext(
			    "(-) %d messages above the current display"), n);
			else
				printw(gettext(
			    "(-) %d message above the current display"), n);
		} else {
			if (n > 1)
				printw(gettext(
			    "(+) %d messages below the current display"), n);
			else
				printw(gettext(
			    "(+) %d message below the current display"), n);
		}
	}
	clrtoeol();
}

static void
scr_string(objp)
	struct object *objp;
{
	/*LINTED [alignment ok]*/
	char	*str = *(char **)(objp->obj_val);

	if (str == NULL)
		return;
	move(objp->obj_y, objp->obj_x);
	if (objp->obj_name)
		printw("%s ", objp->obj_name);
	printw("%s", str);
	clrtoeol();
}

static void
scr_clock(objp)
	struct object *objp;
{
	/*LINTED [alignment ok]*/
	char	*date = lctime((time_t *)objp->obj_val);

	move(objp->obj_y, objp->obj_x);
	printw("%s", date);
}

static void
scr_filter(objp)
	struct object *objp;
{
	/*LINTED [alignment ok]*/
	char	*str = *(char **)(objp->obj_val);

	move(objp->obj_y, objp->obj_x);
	if (str == NULL)
		printw(gettext(
		    "No currently active filter: displaying all messages"));
	else
		printw(gettext("Filter: '%s'"), str);
	clrtoeol();
}

static void
scr_msg(mc, y)
	struct msg_cache *mc;
	int	y;
{
	msg_t	*msgp = &mc->mc_msg;
	char	msgbuf[MAXCOLS+1];
	register char *cp;
	int	x, indent;
	time_t	h;

	h = scr_hold(0);
	wmove(msgwin, y, 0);
	if (mc->mc_status & REVERSE) {
#ifndef USG
		register int i;
#endif
		wstandout(msgwin);
#ifdef USG
		touchline(msgwin, y, mc->mc_nlines);
#else
		for (i = y; i < (y + mc->mc_nlines); i++)
			touchline(msgwin, i, 0, screen_cols-1);
#endif
	}
	wprintw(msgwin, "%c", (u_char)mc->mc_tag);
	if (mc->mc_status & NEEDREPLY) {
		char	disp;

		if (mc->mc_status & GOTACK)
			disp = '+';
		else if (mc->mc_status & GOTNACK)
			disp = '-';
		else if (mc->mc_status & SENTREPLY)
			disp = '?';
		else
			disp = '*';
		wprintw(msgwin, "%c ", (u_char)disp);
	} else
		wprintw(msgwin, "  ");
#ifdef lint
	x = 0;
#endif
	for (cp = msgp->msg_data, indent = msg_indent;
	    cp < &msgp->msg_data[msgp->msg_len];
	    cp += x, indent = TABCOLS) {
		x = screen_cols - indent;
		wmove(msgwin, y++, indent);
		(void) strncpy(msgbuf, cp, x);
		msgbuf[x] = '\0';
		if (waddstr(msgwin, msgbuf) != ERR)
			wclrtoeol(msgwin);
	}
	if (mc->mc_status & REVERSE) {
		wstandend(msgwin);
	}
	scr_release(h);
}

void
scr_echo(c)
	int	c;
{
	if (doinghelp)
		return;
	if (cmdwin) {
		waddch(cmdwin, c);
		dorefresh(cmdwin, TRUE);
	}
}

#include <stdarg.h>

static char promptstr[1024];

/*VARARGS*/
void
prompt(const char *fmt, ...)
{
	va_list args;

	if (doinghelp || !cmdwin)
		return;
#ifdef USG
	va_start(args, fmt);
#else
	va_start(args);
#endif
	(void) vsprintf(promptstr, fmt, args);
	objects[OBJ_INPUT].obj_x = strlen(promptstr);
	current_prompt = promptstr;
	wmove(cmdwin, 0, 0);
	if (wprintw(cmdwin, "%s", promptstr) != ERR)
		wclrtoeol(cmdwin);
	dorefresh(cmdwin, TRUE);
	va_end(args);
}

/*
 * Reset to and construct main prompt
 */
void
#ifdef __STDC__
resetprompt(void)
#else
resetprompt()
#endif
{
	static char tail[3] = "- ";
	tag_t tag1, tag2;
	register char *cp;

	if (!mainprompt)
		return;
	retag(&tag1, &tag2);
	(void) sprintf(promptstr, gettext("Type "));
	cp = &promptstr[strlen(promptstr)];
	if (msgs_above > 0) {
		(void) strcpy(cp, gettext("- "));
		cp += 2;
	}
	if (msgs_below > 0) {
		(void) strcpy(cp, gettext("+ "));
		cp += 2;
	}
	(void) strcpy(cp, "? : ^L ");
	cp = &promptstr[strlen(promptstr)];
	if (tag1 != NOTAG) {
		(void) sprintf(cp,
		    gettext("<space> or %c"), (u_char)tag1);
		if (tag1 != tag2) {
			tail[1] = (char)tag2;
			(void) strcat(promptstr, tail);
		}
	} else
		(void) sprintf(cp, gettext("or <space> "));
	current_prompt = promptstr;
}

/*VARARGS*/
void
status(int print, const char *fmt, ...)
{
	va_list args;
	static char buf[1024];

#ifdef USG
	va_start(args, fmt);
#else
	va_start(args);
#endif
	(void) vsprintf(buf, fmt, args);
	current_status = buf;
	if (cmdwin && print && !doinghelp) {
		wmove(cmdwin, 1, 0);
		if (wprintw(cmdwin, "%s", buf) != ERR)
			wclrtobot(cmdwin);
		dorefresh(cmdwin, TRUE);
	}
	va_end(args);
}

/*
 * Get a line of input.  Like fgets,
 * cgets retains the trailing newline
 * character.
 */
void
cgets(buf, len)
	char	*buf;
	int	len;
{
	register char *bp = buf;
	register int c;
	int x, y, linex, liney;

	getyx(cmdwin, liney, linex);
	x = linex;
	y = liney;
	while (--len > 0 && (c = getch()) != ERR) {
		if (c == erasech) {
			if (bp == buf)
				continue;
			if (--x < 0) {
				x = screen_cols - 1;
				y--;
			}
			wmove(cmdwin, y, x);
			*bp = '\0';
			--bp;
			++len;
			wclrtoeol(cmdwin);
		} else if (c == killch) {
			*buf = '\0';
			wmove(cmdwin, liney, linex);
			wclrtoeol(cmdwin);
			return;
		} else if (c == '\014') {
			++len;		/* adjust count */
			scr_redraw(1);
			wmove(cmdwin, y, x);
			continue;
		} else {
			waddch(cmdwin, c);
			*bp++ = (char)c;
			if (c == '\n' || c == '\r')
				break;
			*bp = '\0';
			if (++x > (screen_cols - 1)) {
				x = 0;
				y++;
			}
		}
		dorefresh(cmdwin, TRUE);
	}
	*bp = '\0';
}

/*
 * Move backwards one character
 */
void
#ifdef __STDC__
backspace(void)
#else
backspace()
#endif
{
	int	x, y;

	getyx(cmdwin, y, x);
	wmove(cmdwin, y, x-1);
	wclrtoeol(cmdwin);
	dorefresh(cmdwin, TRUE);
}

/*
 * Kill an entire line
 */
void
linekill(len)
	int	len;	/* number of cols used */
{
	int	x, y;

	getyx(cmdwin, y, x);
	wmove(cmdwin, y, x - len);
	wclrtoeol(cmdwin);
	dorefresh(cmdwin, TRUE);
}

void
#ifdef __STDC__
erasecmds(void)
#else
erasecmds()
#endif
{
	werase(cmdwin);
	dorefresh(cmdwin, TRUE);
}

istag(tag)
	tag_t	tag;
{
	register u_char *tp;

	for (tp = tags; *tp; tp++)
		if ((tag_t)(*tp) == tag)
			return (1);
	return (0);
}

static void
retag(tag1, tag2)
	tag_t	*tag1;	/* RETURN */
	tag_t	*tag2;	/* RETURN */
{
	register struct msg_cache *mc;
	register u_char *tp = tags;
	time_t	h;

	*tag1 = NOTAG;
	*tag2 = NOTAG;
	h = scr_hold(0);
	if (top) {
		mc = top;
		for (mc = top; mc != bottom->mc_nextrcvd;
		    mc = mc->mc_nextrcvd) {
			if (!msg_filter(mc))
				continue;
			if (mc->mc_status & NEEDREPLY || tagall) {
				if (tagall ||
				    (mc->mc_status & (GOTACK|GOTNACK)) == 0) {
					if (*tag1 == NOTAG)
						*tag1 = (tag_t)(*tp);
					*tag2 = (tag_t)(*tp);
				}
				mc->mc_tag = (tag_t)(*tp++);
			} else
				mc->mc_tag = NOTAG;
		}
	}
	scr_release(h);
}

/*
 * Version of wrefresh() that blocks alarm
 * interrupts during screen refreshes.
 */
static void
dorefresh(win, update)
	WINDOW	*win;
	bool	update;		/* for USG, doupdate if TRUE */
{
#ifndef USG
	int	omask;

	omask = sigblock(sigmask(SIGALRM));
	(void) wrefresh(win);
	(void) sigsetmask(omask);
#else
	(void) sighold(SIGALRM);
	(void) wnoutrefresh(win);
	if (update == TRUE)
		doupdate();
	(void) sigrelse(SIGALRM);
#endif
}

static char *
lctime(timep)
	time_t	*timep;
{
	static char buf[256];
	struct tm *tm;

	tm = localtime(timep);
	(void) strftime(buf, sizeof (buf), gettext(CLOCKFMT), tm);
	return (buf);
}
