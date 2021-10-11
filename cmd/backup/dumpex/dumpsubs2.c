#ident	"@(#)dumpsubs2.c 1.40 93/02/25"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <errno.h>
#include <curses.h>
#include <ulimit.h>
#include <netdb.h>
#include <netinet/in.h>

extern int expert;

#ifdef __STDC__
static void sronlcr(int, int);
static char *getarblevels(void);
static void showonefs(int);
static void showfs(int);
static void doedit(int);
static void advance(int);
static void right(int);
static void left(int);
static void newlevel(int);
static int figuresublen(void);
static int dumplen(char *);
static void fillin_dumplevels(int, struct devcycle_f *);
static void insert_fs(int, struct devcycle_f *);
static void addfilesys(void);
static void probefs_cb(char *);
static void do_new_filesys(int, char *, int, char *, int);
#else
#endif

static void
sronlcr(int fd, int flag)
{
	struct termios termios[1];

	(void) ioctl(fd, TCGETS, termios);
	if (flag)
		termios->c_oflag |= ONLCR;
	else
		termios->c_oflag &= ~ONLCR;
	(void) ioctl(fd, TCSETSW, termios);
}

static char *
getarblevels(void)
{
	char	in[LINEWID];
	char	*p;
	char	*out;

	inputshow(1, gettext("Please enter the dump levels:"));
	for (;;) {
		inputshow(2, "> ");
		checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0')
			return (0);
		for (p = in; *p; p++) {
			if (*p == 'X')
				*p = 'x';
			if (index("01234567890x", *p) == NULL)
				goto bad1;
		}
		out = checkalloc(strlen(in) + 2);
		changedbottom = 1;
		(void) sprintf(out, ">%s", in);
		return (out);
bad1:
		inputshow(3, gettext("Only digits and `x' are allowed"));
	}
}

struct devcycle_f *fs[1000];	/* XXX - Bad form */
static int maxfslen;

void
newfilesys(void)
{
	char	c;
	int	fsstart;
	char	in[LINEWID];
	int	i;
	int	j;
	char	*add = gettext("a = add");
	char	*delete = gettext("d = delete");
	char	*edit = gettext("e = edit");
	char	*quit = gettext("q = quit (back to main menu)");
	char	*help = gettext("? = help");
	char	*stagger = gettext("s = stagger");
	char	*unstagger = gettext("u = unstagger");
	char	*align = gettext("> = align '>' pointers");

	fsstart = 0;
	for (;;) {
		erase();
		(void) mvprintw(2, 5, gettext(
		    "----- Dump File System Editor -- %s -----"), filename);
		showfs(fsstart);
		(void) mvprintw(20, 2, "  %s  %s  %s  %s  %s",
			add, delete, edit, quit, help);
		(void) mvprintw(21, 2, "  %s  %s  %s",
			stagger, unstagger, align);
		move(20, 1);
		c = zgetch();
		if (c == '')
			clear();
		else if (c == quit[0] || c == '\n' || c == '\r') {
			if (nfilesystems == 0) {
				(void) mvprintw(22, 1, gettext(
			"Cannot return without at least one file system"));
				(void) mvprintw(23, 1, gettext(
					"Press `return' to continue"));
				(void) zgetch();
				continue;
			}
			return;
		} else if (c == '-' || c == '\004' || c == '\002') {
			fsstart = fsstart - MAXDEVICEDISPLAY + 1;
			if (fsstart < 0)
				fsstart = 0;
		} else if (c == '+' || c == ' ' || c == '\006' || c == '\025') {
			fsstart = fsstart + MAXDEVICEDISPLAY - 1;
			if (fsstart >= nfilesystems)
				fsstart = nfilesystems - 1;
		} else if (c == add[0])
			addfilesys();
		else if (c == delete[0]) {
			inputborder();
			inputshow(1, gettext(
		"Please enter the number of the file system to delete"));
			for (;;) {
				int	n;
				inputshow(2, "> "); /* Combine with checkget */
				checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
				if (in[0] == '\0')
					break;
				n = atoi(in);
				if (n > 0 && n <= nfilesystems) {
					for (i = n - 1;
					    i + 1 < nfilesystems; i++)
						fs[i] = fs[i + 1];
					fs[nfilesystems - 1] = 0;
					nfilesystems--;
					addedfs = changedbottom = 1;
					break;
				}
				inputshow(3, gettext(
				    "Choose a number from 1 through %d"),
					nfilesystems);
				continue;
			}
		} else if (c == unstagger[0]) {
			changedbottom = 1;
			for (i = 0; i < nfilesystems; i++) {
				if (index(fs[i]->dc_dumplevels, '0') == NULL) {
					(void) mvprintw(22, 1, gettext(
				"Cannot unstagger: %s has no level 0 dump"),
						&fs[i]->dc_filesys[1]);
					(void) mvprintw(23, 1, gettext(
						"Press `return' to continue"));
					(void) zgetch();
					continue;
				}
				while (!(fs[i]->dc_dumplevels[0] == '0' ||
				    (fs[i]->dc_dumplevels[0] == '>' &&
				    fs[i]->dc_dumplevels[1] == '0')))
					left(i);
			}
		} else if (c == '>') {
			changedbottom = 1;
			for (i = 0; i < nfilesystems; i++) {
				char	*p, *q1, *q2;
				q2 = q1 = checkalloc(
					strlen(fs[i]->dc_dumplevels) + 2);
				*q1++ = '>';
				for (p = fs[i]->dc_dumplevels; *p; p++)
					if (*p != '>')
						*q1++ = *p;
				*q1++ = '\0';
				free(fs[i]->dc_dumplevels);
				fs[i]->dc_dumplevels = q2;
			}
		} else if (c == stagger[0]) {
			changedbottom = 1;
			for (i = 0; i < nfilesystems; i++) {
				if (index(fs[i]->dc_dumplevels, '0') == NULL) {
					(void) mvprintw(22, 1, gettext(
				"Cannot restagger: %s has no level 0 dump"),
						&fs[i]->dc_filesys[1]);
					(void) mvprintw(23, 1, gettext(
						"Press `return' to continue"));
					(void) zgetch();
					continue;
				}
				while (!(fs[i]->dc_dumplevels[0] == '0' ||
				    (fs[i]->dc_dumplevels[0] == '>' &&
				    fs[i]->dc_dumplevels[1] == '0')))
					left(i);
				for (j = 0; j < i; j++) {
					right(i);
				}
			}
		} else if (c == edit[0]) {
			inputborder();
			inputshow(1, gettext("Edit which file system entry:"));
			for (;;) {
				int	n;
				inputshow(2, "> ");
				checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
				if (in[0] == '\0')
					break;
				n = atoi(in);
				if (n > 0 && n <= nfilesystems) {
					doedit(n - 1);
					break;
				}
				inputshow(3, gettext(
				    "Choose a number from 1 through %d"),
					nfilesystems);
			}
		} else if (c == help[0])
			scr_help(HELP_FS, expert);
		else
			(void) fprintf(stderr, gettext("\007"));
	}
	/* NOTREACHED */
}

#define	FSLIM		30
#define	DUMPLIM		65
#define	BLANKWID	3

static void
showfs(int start)
{
	int	i;
	int	line;
	int	maxdumplen;
	int	t;
	maxfslen = 10;
	for (i = 0; i < nfilesystems; i++) {
		int	t;
		if ((t = strlen(&fs[i]->dc_filesys[1])) > maxfslen)
			maxfslen = t;
	}
	maxdumplen = 0;
	if (maxfslen > FSLIM)
		maxfslen = FSLIM;
	for (i = start; i < start + MAXDEVICEDISPLAY && i < nfilesystems; i++)
		if ((t = strlen(fs[i]->dc_dumplevels)) > maxdumplen)
			maxdumplen = t;
	if (maxdumplen + maxfslen > DUMPLIM)
		maxdumplen = DUMPLIM - BLANKWID - maxfslen;
	if (start)
		(void) mvprintw(2, 10, gettext(
		    "%d file systems above these (use '-' to display)"),
			start);

	line = 4;
	for (i = start; i < start + MAXDEVICEDISPLAY && i < nfilesystems; i++) {
		if ((int)strlen(&fs[i]->dc_filesys[1]) > maxfslen)
			(void) mvprintw(line++, 3,
			    "%3d  %-*.*s... %-*.*s",
				i + 1, maxfslen - BLANKWID, maxfslen - BLANKWID,
				&fs[i]->dc_filesys[1], maxdumplen, maxdumplen,
				fs[i]->dc_dumplevels);
		else
			(void) mvprintw(line++, 3,
			    "%3d  %-*.*s %-*.*s",
				i + 1, maxfslen, maxfslen,
				&fs[i]->dc_filesys[1],
				maxdumplen, maxdumplen, fs[i]->dc_dumplevels);
		if ((int)strlen(fs[i]->dc_dumplevels) > maxdumplen)
			(void) printw("...");
	}
	if (i < nfilesystems) {
		if (nfilesystems - i == 1)
			(void) mvprintw(line, 10, gettext(
			    "1 file system below these (use '+' to display)"));
		else
			(void) mvprintw(line, 10, gettext(
			    "%d file systems below these (use '+' to display)"),
				nfilesystems - i);
	}
}


#define	CHARSPERLINE	60

static void
showonefs(int n)
{
	int	i;
	int	len;
	int	line;

	erase();
	(void) mvprintw(1, 5, gettext(
	    "----- Dump Single File System Editor -- %s -----"),
		filename);
	line = 2;
	(void) mvprintw(line, 1, gettext("FS: "));
	for (i = 1, len = strlen(&fs[n]->dc_filesys[i]);
	    i <= len; i += CHARSPERLINE)
		(void) mvprintw(line++, 6, "%-*.*s", CHARSPERLINE, CHARSPERLINE,
			&fs[n]->dc_filesys[i]);
	line = 12;
	(void) mvprintw(line, 1, gettext("Levels: "));
	for (len = strlen(fs[n]->dc_dumplevels), i = 0;
	    i < len; i += CHARSPERLINE)
		(void) mvprintw(line++, 10, "%-*.*s",
			CHARSPERLINE, CHARSPERLINE,
			&fs[n]->dc_dumplevels[i]);
}

static void
doedit(int n)
{
	char	c;
	char	in[SCREENWID];
	char	*newfs = gettext("f = new file system");
	char	*newlevelmsg = gettext("l = new dumplevels");
	char	*rotate = gettext("< = rotate levels");
	char	*carat = gettext("> = advance '>' pointer");
	char	*quit = gettext("q = quit (back to previous menu)");
	char	*help = gettext("? = help");

	for (;;) {
		showonefs(n);
		(void) mvprintw(20, 1, "  %s  %s  %s",
			newfs, newlevelmsg, quit);
		(void) mvprintw(21, 1, "  %s  %s  %s", rotate, carat, help);
		move(20, 1);
		c = zgetch();
		if (c == quit[0] || c == '\n' || c == '\r')
			return;
		else if (c == '')
			clear();
		else if (c == newfs[0]) {
			inputborder();
			inputshow(1, gettext("New file system:"));
			inputshow(2, "> ");
			checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
			if (in[0] == '\0')
				break;
			fs[n]->dc_filesys = checkalloc(strlen(in) + 2);
			changedbottom = 1;
			(void) sprintf(fs[n]->dc_filesys, "-%s", in);
		} else if (c == rotate[0]) { /* shift left (circular) */
			left(n);
			changedbottom = 1;
		} else if (c == carat[0]) { /* slide pointer right */
			advance(n);
			changedbottom = 1;
		} else if (c == newlevelmsg[0])
			newlevel(n);
		else if (c == help[0])
			scr_help(HELP_FS_EDIT, expert);
		else
			(void) fprintf(stderr, gettext("\007"));
	}
}

/*
 * This advances the pointer exactly one dump level to the right, wrapping
 * to the beginning if necessary.
 */
static void
advance(int n)
{
	char *p;

	for (p = fs[n]->dc_dumplevels; *p && *p != '>'; p++)
		/* empty */;
	if (*p == '\0')			/* should not happen */
		return;

	if (*(p + 2) != '\0') {
		/* easy case: move the carat right one */
		*p = *(p + 1);
		*(p + 1) = '>';
	} else {
		/* harder: carat goes at the beginning; big shift */
		while (p != fs[n]->dc_dumplevels) {
			*p = *(p - 1);
			p--;
		}
		*p = '>';
	}
}

/*
 * This does a left-circular shift of all the dump levels.
 */
static void
right(int n)
{
	char	*p;
	char	t;
	int	len;

	len = strlen(fs[n]->dc_dumplevels);
	p = &fs[n]->dc_dumplevels[--len];
	for (t = *p; len--; p--)
		*p = *(p - 1);
	*p = t;
	len = strlen(fs[n]->dc_dumplevels);
	if (fs[n]->dc_dumplevels[len - 1] == '>')
		right(n);
}

/*
 * This does a left-circular shift of all the dump levels.
 */
static void
left(int n)
{
	char	*p;
	char	t;
	int	len;

	p = fs[n]->dc_dumplevels;
	for (t = *p++; *p; p++)
		*(p - 1) = *p;
	*(p - 1) = t;
	len = strlen(fs[n]->dc_dumplevels);
	if (fs[n]->dc_dumplevels[len - 1] == '>')
		left(n);
}

/*
 * Move the pointer to the first level-0
 * in the sequence
 */
static char *
copy_dumplevels(char *levels)
{
	char *dumplevels, *p, *q;
	int sawzero;

	/* if no level-0, just return a copy */
	if (index(levels, '0') == NULL)
		return (strdup(levels));

	p = dumplevels = (char *) malloc(strlen(levels) + 2);
	for (sawzero = 0, q = levels; *q; q++) {
		if (*q == '>')
			continue;
		if (sawzero == 0 && *q == '0') {
			sawzero++;
			*p++ = '>';
		}
		*p++ = *q;
	}
	*p = '\0';
	return (dumplevels);
}

static void
newlevel(int n)
{
	int	i;
	char	*p;
	int	x, y;
	int	len, sublen;
	int	calclen, calcsublen;
	char	in[SCREENWID];

	inputborder();
	inputshow(1,
	    "1=05555 55555...  2=09999 59999...  3=0xxxx xxxxx...  4=%s",
		gettext("other"));
	for (;;) {
		inputshow(2, "> ");
		checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0')
			return;
		i = atoi(in);
		if (i >= LEV_FULL_INCR && i <= LEV_ANY)
			break;
		inputshow(3, gettext("Please choose 1, 2, 3, or 4"));
	}
	inputclear(3);
	if (i == LEV_ANY) {
		char	*p;
		p = getarblevels();
		if (p != 0)
			fs[n]->dc_dumplevels = p;
		return;
	}
	calcsublen = calclen = strlen(fs[n]->dc_dumplevels);
	if (index(fs[n]->dc_dumplevels, '>'))
		calcsublen--, calclen--;
	inputclear(3);
	for (;;) {
		inputshow(1,
			gettext("Please enter the length of this sequence:"));
		inputshow(2, "[%d] > ", calclen);
		getyx(stdscr, y, x);
		checkget(LINEWID - 1, INBASE + 2, x, in);
		if (in[0] == '\0')
			len = calclen;
		else
			len = atoi(in);
		if (len > 0)
			break;
		inputshow(3, gettext("Please enter a length greater than 0"));
	}
	inputclear(3);
	if (i == LEV_FULL_INCRx2 && len > 1) {
		calcsublen = figuresublen();
		for (;;) {
			inputshow(1, gettext(
			    "Please enter the sub-length of this sequence:"));
			inputshow(2, "[%d] > ", calcsublen);
			getyx(stdscr, y, x);
			checkget(LINEWID - 8, INBASE + 2, x, in);
				/* -8 for default */
			if (in[0] == '\0')
				sublen = calcsublen;
			else
				sublen = atoi(in);
			if (sublen < 1 || sublen > len) {
				inputshow(3, gettext(
			"Please enter sublength between 1 and %d inclusive"),
					len);
				continue;
			}
			if (len > 0 && (len % sublen) == 0)
				break;
			inputshow(3, gettext(
	"Please enter a length greater than 0 that is a submultiple of %d"),
				len);
		}
	} else
		sublen = 1;
	changedbottom = 1;
	p = genlevelstring(i, 0, len, sublen);
	fs[n]->dc_dumplevels = checkalloc(strlen(p) + 2);
	(void) sprintf(fs[n]->dc_dumplevels, ">%s", p);
#ifdef lint
	x = y;
#endif
}

static int
figuresublen(void)
{
	char	*p1;
	char	*p2;
	char	*work;
	int	calclen;

	if (fs[0] == 0 || fs[0]->dc_dumplevels == 0)
		return (10);
	work = checkalloc(strlen(fs[0]->dc_dumplevels));
	for (p1 = fs[0]->dc_dumplevels, p2 = work; *p1; p1++)
		if (*p1 != '>')
			*p2++ = *p1;
	*p2++ = '\0';

	if (index(work, '9') == NULL) {
		calclen = dumplen(work);
		if (calclen > 2 && (calclen % 2) == 0)
			calclen /= 2;
		return (calclen);
	}
	/* find 0: */
	p1 = index(work, '0');
	p2 = index(work, '5');
	if (p1 && p2) {
		p1 = p1 < p2 ? p1 : p2;
		for (p2 = p1 + 1; *p2 && *p2 != '0' && *p2 != '5'; p2++);
		if (p2 != p1)
			return (p2 - p1);
	}
	return (dumplen(work));
}

static int
dumplen(char *p)
{
	int	len;
	for (len = 0; *p; p++)
		if (*p != '>')
			len++;
	return (len);
}

static void
fillin_dumplevels(int dumptype, struct devcycle_f *dp)
{
	int	sublen, calclen;
	char	*p;

	if (nfilesystems)
		sublen = calclen = dumplen(fs[0]->dc_dumplevels);
	else
		sublen = calclen = 10;
	if (dumptype == LEV_FULL_INCRx2)
		sublen = figuresublen();
	p = genlevelstring(dumptype, 0, calclen, sublen);
	dp->dc_dumplevels = checkalloc(strlen(p) + 2);
	(void) sprintf(dp->dc_dumplevels, ">%s", p);
}

/* insert after entry n, 0=front */
static void
insert_fs(int n, struct devcycle_f *dp)
{
	int	i;
	addedfs = changedbottom = 1;
	if (n)
		fs[n - 1]->dc_next = dp;
	else
		cf_tapeset[1]->ts_devlist = dp;
	dp->dc_next = fs[n];
	for (i = nfilesystems - 1; i >= n; i--)
		fs[i + 1] = fs[i];
	fs[n] = dp;
	nfilesystems++;
}

/* need to be static since probefs_cb uses them */
static int dumpslot, dumplevelchoice;
static char *dumplevels;

static void
addfilesys(void)
{
	char	in[MAXLINELEN];
	int	i;
	char	*filesyschoice = 0;

	inputborder();
	inputshow(1, gettext(
	    "Add after which file system (0 for beginning) [%d]:"),
		nfilesystems);
	for (;;) {
		inputshow(2, "> ");
		checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0')
			dumpslot = nfilesystems;
		else
			dumpslot = atoi(in);
		if (dumpslot < 0 || dumpslot > nfilesystems) {
			inputshow(3, gettext(
			    "Choose a number from 0 through %d"),
				nfilesystems);
			continue;
		}
		break;
	}
inputfilename:;
	inputshow(1, gettext(
	    "File system(s) to add at slot %d (or +machine):"),
		dumpslot + 1);
	inputshow(2, "> ");
	checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
	if (in[0] == '\0')
		return;
	filesyschoice = strdup(in);
	inputclear(3);
	inputshow(1, "1=05555...  2=09999 59999...  3=0xxxx...  4=%s %s",
		gettext("other"),
		nfilesystems > 0 ? gettext(" return=[like slot 1]") : "");
	for (;;) {
		inputshow(2, "> ");
		checkget(LINEWID - 1, INBASE + 2, INPUTCOL, in);
		if (in[0] == '\0') {
			if (nfilesystems > 0) {
				dumplevelchoice = 0;
				break;
			}
		} else if (index("1234", in[0]) != NULL) {
			dumplevelchoice = atoi(in);
			break;
		}
		inputshow(3, gettext("Please choose 1, 2, 3, or 4%s"),
		    nfilesystems ? gettext("; or `return' for default") : "");
	}
	if (filesyschoice[0] != '+') {
		/* normal, single file case: */
		if (dumplevelchoice == LEV_ANY)
			if ((dumplevels = getarblevels()) == NULL)
				return;
		if (dumplevelchoice == 0) {	/* like slot 1 */
			dumplevels = copy_dumplevels(fs[0]->dc_dumplevels);
			dumplevelchoice = LEV_ANY;
		}
		/* must split filesyschoice for multiple fs's: */
		split(filesyschoice, " \t,");
		for (i = 0; i < nsplitfields; i++) {
			if (splitfields[i] == '\0')
				continue;
			do_new_filesys(0, splitfields[i],
				dumplevelchoice, dumplevels, dumpslot++);
		}
		return;
	}
	/* Only possibility is: interrogate-a-machine: */
	if (dumplevelchoice == LEV_ANY) {
		if ((dumplevels = getarblevels()) == NULL)
			return;
	} else {
		if (dumplevelchoice == 0) {
			dumplevels = copy_dumplevels(fs[0]->dc_dumplevels);
			dumplevelchoice = LEV_ANY;
		}
	}

	/* Position at the bottom, and clear the rest */
	move(INBASE + 1, 0);
	clrtobot();
	refresh();
	sronlcr(0, TRUE);

	probefs(&filesyschoice[1], hostname, probefs_cb);

	(void) printf("\nPress any key to continue: ");
	(void) fflush(stdout);
	(void) zgetch();
	sronlcr(0, FALSE);
	clearok(stdscr, TRUE);
}

void
probefs_cb(char *fs)
{
	(void) printf("Adding: %s\n", fs);
	(void) fflush(stdout);
	do_new_filesys(0, fs, dumplevelchoice, dumplevels, dumpslot++);
}

static void
do_new_filesys(
	int	fullcycle,		/* fullcycle number */
	char	*filesys,		/* file system */
	int	dumplevelchoice,	/* LEV_* */
	char	*dumplevels,		/* levels if LEV_ANY chosen */
	int	n)			/* where to insert in insert_fs */
{
	struct devcycle_f *dp =
		/*LINTED [alignment ok]*/
		(struct devcycle_f *) checkcalloc(sizeof (struct devcycle_f));
	char	temp[MAXLINELEN];

	dp->dc_fullcycle = fullcycle;
	(void) sprintf(temp, "-%s", filesys);
	dp->dc_filesys = strdup(temp);
	if (dumplevelchoice == LEV_ANY)
		dp->dc_dumplevels = strdup(dumplevels);
	else
		fillin_dumplevels(dumplevelchoice, dp);
	insert_fs(n, dp);
}
