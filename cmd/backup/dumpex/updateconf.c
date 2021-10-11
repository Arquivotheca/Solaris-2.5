#ident	"@(#)updateconf.c 1.12 93/04/28"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#include "structs.h"
#include "dumpex.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

extern int outofband;

#ifdef __STDC__
static void outputline(int);
#else
static void outputline();
#endif

void
incrmastercycle(void)
{
	char	*p;
	char	out[MAXLINELEN];

	/* find digits */
	for (p = inlines[cf_mastercycleline].if_text;
	    *p && !isdigit((u_char)*p); p++)
		/* empty */
		;
	if (p == NULL)
		die(gettext("%s: Internal consistency error (%d)\n"),
			"incrmastercycle", 1);
	cf_mastercycle++;
	if (!outofband) {
		(void) sprintf(out, "%05.5d", cf_mastercycle);
		(void) strncpy(p, out, TAPENUMLEN);
	}
}

void
markdone(filesystem, linenumber, fullcycleptr)
	char	*filesystem;
	int	linenumber;	/* where to fix it */
	int	*fullcycleptr;	/* pointer to fullcycle (return val) */
{
	char	*p, *q;
	char	out[MAXLINELEN];
	int	i;
	char	level;
	extern char dumplevel;

	/* mark the + sign: */
	for (p = inlines[linenumber].if_text; *p && *p != '-' &&
	    *p != '*'; p++)
		/* empty */;
	if (*p == NULL)
		die(gettext("%s: Internal consistency error (%d)\n"),
			"markdone", 1);
	*p = '+';
	filesystem[0] = '+';	/* internal */

	/* move the > pointer */
	if (!outofband) {
		for (p = inlines[linenumber].if_text; *p && *p != '>'; p++)
			/* empty */;
		if (*p == NULL)
			die(gettext("%s: Internal consistency error (%d)\n"),
				"markdone", 2);
		if (dumplevel == '\0' || dumplevel == *(p + 1)) {
			/*
			 * "normal" case:
			 * no -l option specified, or it was the next level
			 * to do for this file system anyway...
			 */
			level = *(p + 1);
			if (*(p + 2) != '\n') {
				/* easy case: move the carat right one */
				*p = *(p + 1);
				*(p + 1) = '>';
			} else {
				while (!isspace((u_char)(*(p - 1)))) {
					*p = *(p - 1);
					p--;
				}
				*p = '>';
			}
		} else {
			/*
			 * "odd" case:
			 * -l option specifed; move pointer if possible
			 */
			level = dumplevel;
			for (q = p + 1; *q != '>'; q++) {
				if (*q == '\n') {
					/*
					 * move backward until whitespace
					 * is found
					 */
					for (q--; !isspace((u_char)*q); q--)
						/* empty */;
					continue;
				}
				if (*q == dumplevel) {
					if (p < q) {
						if (*(q + 1) == '\n') {
							/*
							 * If at end of the
							 * line, search for
							 * beginning, then
							 * process as usual.
							 */
							for (q = p; !isspace(
							    (u_char)*q); q--)
								/* empty */;
							goto q_gt_p;
						}
						while (p < q) {
							*p = *(p + 1);
							*(p + 1) = '>';
							p++;
						}
					} else {
q_gt_p:
						while (p > (q + 1)) {
							*p = *(p - 1);
							*(p - 1) = '>';
							p--;
						}
					}
					break;
				}
			}
		}
	}

	if (level == '0') {	/* then increment full cycle number */
		(void) sprintf(out, "%05.5d", ++*fullcycleptr);
		/* find digits */
		for (p = inlines[linenumber].if_text;
		    *p && !isdigit((u_char)*p); p++)
			/* empty */;
		if (p == NULL)
			die(gettext("%s: Internal consistency error (%d)\n"),
				"markdone", 3);
		for (q = out, i = 0; i < TAPENUMLEN; i++)
			*p++ = *q++;
	}
	outputline(linenumber);
}

void
markfail(linenumber, filesystem)
	int	linenumber;	/* which line in file needs fixing */
	char	*filesystem;	/* filesystem place to update */
{
	char	*p;

	/* mark the * sign: */
	for (p = inlines[linenumber].if_text;
	    *p && *p != '-' && *p != '*'; p++)
		/* empty */;
	if (*p == NULL)
		die(gettext("%s: Internal consistency error (%d)\n"),
			"markfail", 1);
	*p = '*';
	filesystem[0] = '*';	/* internal */
	outputline(linenumber);
}

void
markundone(d)
	struct devcycle_f *d;
{
	char	*p;
	for (p = inlines[d->dc_linenumber].if_text; *p && *p != '+' &&
	    *p != '*'; p++)
		/* empty */;
	if (*p == NULL)
		die(gettext("%s: Internal consistency error (%d)\n"),
			"markundone", 1);
	*p = '-';
	d->dc_filesys[0] = '-';
}

static void
outputline(linenumber)
	int	linenumber;
{
	if (fseek(infid, (long) inlines[linenumber].if_fileoffset, 0) == -1)
		die(gettext("%s failed (%d)\n"), "fseek", 27);
	if (fputs(inlines[linenumber].if_text, infid) == EOF)
		die(gettext("%s failed (%d)\n"), "fputs", 10);
	if (fflush(infid) == EOF)
		die(gettext("%s failed (%d)\n"), "fflush", 4);
}

void
outputfile(name)
	char	*name;
{
	FILE	*outfid;
	int	i;

	outfid = fopen(name, "w");
	if (outfid == NULL)
		die(gettext("Cannot open temporary file `%s'\n"), name);
	for (i = 0; i < ninlines; i++)
		if (fputs(inlines[i].if_text, outfid) == EOF)
			die(gettext("%s failed (%d)\n"), "fputs", 11);
	if (fclose(outfid) == EOF)
		die(gettext("%s failed (%d)\n"), "fclose", 19);
}
