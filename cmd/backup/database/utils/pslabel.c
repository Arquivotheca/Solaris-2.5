#ident	"@(#)pslabel.c 1.9 94/08/10"

/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <libintl.h>
#include "defs.h"

#define	PSLABELPROLOG "\
%! postscript tape insert\n\
/width 270 def /height 270 def /spine 45 def /lb 4 def /titlesize 13\n\
def /maxtoc 6 def /mintoc 3 def /titlefont /Helvetica-Bold findfont def\n\
/darkfont /Courier-Bold findfont def /textfont /Courier findfont def /b\n\
(zirak!zigil) def /startoflabels { initgraphics clippath pathbbox exch\n\
4 1 roll add /paperheight exch def add /paperwidth exch def pathbbox\n\
newpath exch 4 1 roll exch sub height div cvi /rows exch def sub width\n\
div cvi /cols exch def pc } bind def /pc { gsave 0.5 setgray paperwidth\n\
width cols mul sub cols 1 add div dup width add paperwidth\n\
currentlinewidth sub { dup 0 moveto paperheight lineto stroke } for\n\
paperwidth width cols mul sub cols 1 add div width add dup paperwidth {\n\
dup 0 moveto paperheight lineto stroke } for paperheight height rows\n\
mul sub rows 1 add div dup height add paperheight currentlinewidth sub\n\
{ dup 0 exch moveto paperwidth exch lineto stroke } for paperheight\n\
height rows mul sub rows 1 add div height add dup paperheight { dup 0\n\
exch moveto paperwidth exch lineto stroke } for grestore } bind def\n\
/drawlabel { gsave exch /right exch def dup paperwidth width cols mul\n\
sub cols 1 add div dup width add 3 -1 roll rows div cvi mul add exch\n\
paperheight height rows mul sub rows 1 add div height add exch rows mod\n\
1 add mul moveto currentpoint /yorg exch def /xorg exch def xorg yorg\n\
moveto width 0 rlineto 0 height -1 mul rlineto width -1 mul 0 rlineto\n\
closepath gsave stroke grestore clip newpath gsave spine 0 ne { [7 3] 0\n\
setdash xorg yorg spine sub moveto width 0 rlineto stroke xorg yorg\n\
spine 2 mul sub moveto width 0 rlineto stroke } if grestore gsave yorg\n\
spine 2 mul titlesize add lb add dup 3 1 roll sub xorg exch moveto\n\
width 0 rlineto height exch sub -1 mul 0 exch rlineto width -1 mul 0\n\
rlineto closepath clip dup length width lb sub exch div cvi dup maxtoc\n\
gt { pop maxtoc } if dup mintoc lt { pop mintoc } if /tsize exch def\n\
xorg yorg moveto tsize height -1 mul lb add rmoveto 90 rotate\n\
currentpoint /tmargin exch def /lmargin exch def /bmargin tmargin width\n\
sub lb add currentlinewidth add tsize add def /rmargin lmargin def\n\
textfont tsize scalefont setfont { dup b eq { darkfont tsize scalefont\n\
setfont pop } { show currentpoint exch dup rmargin gt { /rmargin exch\n\
def } { pop } ifelse bmargin lt { /rmargin rmargin lb add def /lmargin\n\
rmargin def lmargin tmargin moveto } { currentpoint lmargin exch moveto\n\
pop 0 tsize -1 mul rmoveto } ifelse textfont tsize scalefont setfont }\n\
ifelse } forall grestore /left exch def titlefont titlesize scalefont\n\
setfont spine 0 ne { gsave xorg lb add right stringwidth pop add yorg\n\
spine 3 mul 2 div sub titlesize 2 div add moveto gsave -1 -1 scale\n\
right show grestore xorg width add lb sub yorg spine 3 mul 2 div sub\n\
titlesize 2 div add moveto gsave -1 -1 scale left show grestore xorg lb\n\
add right stringwidth pop add yorg spine 2 div sub titlesize 2 div add\n\
moveto gsave -1 -1 scale right show grestore xorg width add lb sub yorg\n\
spine 2 div sub titlesize 2 div add moveto gsave -1 -1 scale left show\n\
grestore grestore } if xorg yorg moveto width lb sub titlesize spine 2\n\
mul add -1 mul rmoveto right stringwidth exch -1 mul exch rmoveto right\n\
show xorg yorg moveto lb titlesize spine 2 mul add -1 mul rmoveto left\n\
show grestore } bind def /seq 0 def /label { seq drawlabel /seq seq 1\n\
add rows cols mul mod def seq 0 eq { showpage pc } if } bind def\n\
/endoflabels { seq rows cols mul mod 0 ne { showpage } if } bind def\n\
%% uncomment next line for 4mm DAT format\n\
%/width 208 def /height 222 def /spine 34 def\n\
%% uncomment next line for QIC format\n\
%/width 440 def /height 280 def /spine 0 def\n"

#define	SPACE " \t"

struct labelstruct labeltypes[] = {
	{"8mm", 270, 270, 45},
	{"dat", 208, 222, 34},
	{"qic", 440, 280,  0},
	{NULL, 0, 0, 0}
};

pslabel(in, lt)
FILE *in;
struct labelstruct *lt;
{
	char buffy[BUFSIZ], line[BUFSIZ], tapename[BUFSIZ] = "";
	char *tname, *off, *lev, *mount, *tstr;
	time_t secs;

	fputs(PSLABELPROLOG, stdout);

	/* any modifications (e.g. 4mm DAT tapes) go here */
	if (lt)
		printf("/width %d def /height %d def /spine %d def\n",
		    lt->width, lt->height, lt->spine);

	puts("\nstartoflabels");

	while (fgets(buffy, sizeof (buffy), in)) {
		strcpy(line, buffy);
		if (((tname = strtok(buffy, SPACE)) == NULL) ||
		    ((off = strtok(NULL, SPACE)) == NULL) ||
		    ((lev = strtok(NULL, SPACE)) == NULL) ||
		    ((mount = strtok(NULL, SPACE)) == NULL) ||
		    (strtok(NULL, SPACE) == NULL) ||
		    ((tstr = strtok(NULL, SPACE)) == NULL)) {
			fprintf(stderr, gettext(
			"warning: unrecognized line (error message?)\n> %s\n"),
			    line);
			continue;
		}
		if (strchr(lev, 'E'))
			continue;

		if (strcmp(tname, tapename)) {
			if (tapename[0]) {
				(void) cftime(line, "%x", &secs);
				printf("] (%s) label\n", line);
			}
			printf("\n(%s) [\n", tname);
			puts(gettext("  b (off lv   m  d filesys)"));
			strcpy(tapename, tname);
		}

		secs = (time_t)atoi(tstr);

		cftime(line, "%b %d", &secs);
		printf("    (%3s %2s %s %s)\n", off, lev, line, mount);
	}
	if (tapename[0]) {
		(void) cftime(line, "%x", &secs);
		printf("] (%s) label\n\nendoflabels\n", line);
	}
}
