#ident	"@(#)checkget.c 1.6 91/12/20"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include	"structs.h"
#include	<curses.h>
#include	<ctype.h>
#ifdef USG
#include	<termios.h>
#else
#include	<sgttyb.h>
#endif

static int killch;		/* line kill character */
static int erasech;		/* character erase */
static int werasech;		/* word erase */

static void
setblank(ly, lx, xstandout, len)
{
	move(ly, lx);
	if (xstandout)
		standout();
	while (len--)
		addch(' ');
	standend();
}

static void
erasechars(n)
	int	n;		/* erase this many characters */
{
	int	y, x;		/* row and column coordinates */
	if (n) {
		getyx(stdscr, y, x);
		setblank(y, x - n, 0, n);
		move(y, x - n);
	}
}

/* thanks to Mike Locke */
void
checkget(linelen, ly, lx, buf)
	int	linelen, ly, lx;
	char	*buf;		/* where to put the input */
{
	int	j;
	int	c;
	char    line[MAXLINELEN];
	int	ncharsin;

#ifdef USG
	struct termios tty;

	if (tcgetattr(1, &tty) < 0)
		die(gettext("cannot get terminal attributes\n"));
	werasech = (int) tty.c_cc[VWERASE];
#else
	struct ltchars tty2;

	if (ioctl(1, TIOCGLTC, (char *) &tty2) == -1)
		die(gettext("cannot get terminal attributes\n"));
	werasech = tty2.t_werasc;
#endif
	killch = killchar();
	erasech = erasechar();

	ncharsin = 0;
	setblank(ly, lx, 0, linelen);
	move(ly, lx);
	for (;;) {
		c = zgetch();
		if (c == erasech) {	/* erase this character? */
			if (ncharsin) {	/* anything to erase? */
				erasechars(1);
				ncharsin--;
			} else
				(void) printf(gettext("\007"));
			continue;
		}
		if (c == killch) {	/* line kill */
			if (ncharsin) {
				erasechars(ncharsin);
				ncharsin = 0;
			}
			continue;
		}
		if (c == werasech) {	/* word erase */
			int	nerase = 0;
			while (ncharsin &&
			    (line[ncharsin - 1] == ' ' ||
			    line[ncharsin - 1] == '\t')) {
				ncharsin--;
				nerase++;
			}
			while (ncharsin && (line[ncharsin - 1] != ' ' &&
					    line[ncharsin - 1] != '\t')) {
				ncharsin--;
				nerase++;
			}
			erasechars(nerase);
			continue;
		}
		switch (c) {
		case '\014':	/* Refresh */
			clearok(stdscr, 1);
			break;
		case '\n':
		case '\r':	/* we're done now! */
			/* copy to buffer */
			for (j = 0; j < linelen && j < ncharsin; j++)
				buf[j] = line[j];
			buf[j] = '\0';
			return;
		default:
			if (!isprint(c) || ncharsin == linelen) {
				(void) printf(gettext("\007"));
				break;
			}
			line[ncharsin++] = c;
			addch(c);
			break;
		}
	}
}
