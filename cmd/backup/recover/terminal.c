#ident	"@(#)terminal.c 1.11 94/08/10"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "recover.h"
#include <signal.h>
#ifdef USG
#include <termios.h>

static struct winsize win;
#else
#include <sys/ioccom.h>
#include <sys/ttycom.h>

static struct ttysize tty;
#endif

static int tty_lines, tty_cols;

static char *termbuffer;
static char *pager;
static int pipeout;
FILE *outfp;

#ifdef __STDC__
static void get_termsize(void);
static void open_pager(void);
#else
static void get_termsize();
static void open_pager();
#endif

static void
#ifdef __STDC__
get_termsize(void)
#else
get_termsize()
#endif
{
	int fd;

	if (!isatty(fileno(stdout)))
		return;

	if ((fd = open("/dev/tty", O_RDWR)) == -1) {
		perror("open: /dev/tty");
		return;
	}
#ifdef USG
	(void) ioctl(fd, TIOCGWINSZ, &win);
	(void) close(fd);
	if (win.ws_row == 0 && win.ws_col == 0) {
		win.ws_row = 24;
		win.ws_col = 80;
	}
	tty_lines = win.ws_row;
	tty_cols = win.ws_col;
#else
	if (ioctl(fd, TIOCGSIZE, &tty) == -1)
		perror("TIOCGSIZE");
	(void) close(fd);
	if (tty.ts_lines == 0 && tty.ts_cols == 0) {
		tty.ts_lines = 24;
		tty.ts_cols = 80;
	}
	tty_lines = tty.ts_lines;
	tty_cols = tty.ts_cols;
#endif
	if (termbuffer)
		free(termbuffer);
	termbuffer = (char *)malloc((unsigned)(MAXPATHLEN*tty_lines));
	if (termbuffer == NULL)
		panic(gettext("out of memory\n"));

	/*
	 * act like there is one fewer line since recover will re-write
	 * its prompt at the end of command output.  If our command wrote
	 * exactly `screensize' lines of output, we would lose the top
	 * line when the prompt is re-written...
	 */
#ifdef USG
	win.ws_row--;
#else
	tty.ts_lines--;
#endif
	tty_lines--;

}

#ifdef __STDC__
get_termwidth(void)
#else
get_termwidth()
#endif
{
	return (tty_cols);
}

void
#ifdef __STDC__
term_init(void)
#else
term_init()
#endif
{
	struct sigvec winvec;

	winvec.sv_handler = get_termsize;
#ifdef USG
	winvec.sa_flags = SA_RESTART;
	(void) sigemptyset(&winvec.sa_mask);
#else
	winvec.sa_flags = 0;
	winvec.sv_mask = 0;
#endif
	(void) sigvec(SIGWINCH, &winvec, (struct sigvec *)NULL);
	get_termsize();
}

static int nlines, linechars, buflines, nchars, ttyout;

void
#ifdef __STDC__
term_start_output(void)
#else
term_start_output()
#endif
{
	nlines = nchars = buflines = linechars = 0;
	ttyout = isatty(fileno(outfp));
	if (outfp == stdout && ttyout && tty_lines == 0)
		get_termsize();
}
void
#ifdef __STDC__
term_finish_output(void)
#else
term_finish_output()
#endif
{
	if (buflines)
		(void) fputs(termbuffer, outfp);
}

void
term_putc(c)
	int c;
{
	if (outfp != stdout || !ttyout) {
		(void) putc(c, outfp);
	} else if (nlines >= tty_lines) {
		if (buflines) {
			open_pager();
			(void) fputs(termbuffer, outfp);
			buflines = 0;
		}
		(void) putc(c, outfp);
	} else if (c == '\n') {
		termbuffer[nchars] = c;
		nlines++;
		buflines++;
		termbuffer[++nchars] = '\0';
		linechars = 0;
	} else {
		termbuffer[nchars] = c;
		termbuffer[++nchars] = '\0';
		if ((++linechars % tty_cols) == 0) {
			nlines++;
			buflines++;
		}
	}
}

void
term_putline(s)
	char *s;
{
	register char *p;

	if (outfp != stdout || !ttyout) {
		(void) fputs(s, outfp);
	} else if (nlines >= tty_lines) {
		if (buflines) {
			open_pager();
			(void) fputs(termbuffer, outfp);
			buflines = 0;
		}
		(void) fputs(s, outfp);
	} else {
		for (p = s; *p; p++) {
			termbuffer[nchars++] = *p;
			if (*p == '\n') {
				buflines++;
				nlines++;
				linechars = 0;
			} else if ((++linechars % tty_cols) == 0) {
				nlines++;
				buflines++;
			}
		}
		termbuffer[nchars] = '\0';
	}
}

static void
#ifdef __STDC__
open_pager(void)
#else
open_pager()
#endif
{
	if (pager == NULL) {
		pager = (char *)getenv("PAGER");
		if (pager == NULL)
			pager = "/usr/bin/more";
	}
	if (outfp == stdout && ttyout) {
		if ((outfp = popen(pager, "w")) == NULL) {
			(void) fprintf(stderr,
				gettext("Cannot open pipe to %s\n"), pager);
			outfp = stdout;
		} else {
			pipeout = 1;
		}
	}
}

void
#ifdef __STDC__
close_output(void)
#else
close_output()
#endif
{
	if (outfp == stdout) {
		(void) fflush(stdout);
	} else if (pipeout) {
		(void) pclose(outfp);
	} else {
		(void) fclose(outfp);
	}
}
