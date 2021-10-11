#ident	"@(#)split.c 1.6 93/10/27"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*
 * convert line to fields in place
 * note that leading and trailing split
 * characters are discarded
 */
#include "structs.h"

#define	MAXCHARS	256		/* 256 ascii characters */
static char	splitseps[MAXCHARS];	/* automatically zeroed */

void
split(line, seps)
	char	*line;
	char	*seps;		/* list of separators */
{
	unsigned char	*p;
	char	*pout;

	if (maxsplitfields == 0) {
		/*LINTED [alignment ok]*/
		splitfields = (char **) checkalloc(GROW * sizeof (char *));
		maxsplitfields = GROW;
	}
	for (p = (unsigned char *) seps; *p; p++) /* set fast check list */
		splitseps[*p] = 1;
	nsplitfields = 0;
	pout = line;
	p = (unsigned char *) pout;
	while (*p) {
		for (; splitseps[*p]; p++)	/* elim seps */
			/* empty */
			;
		if (*p == 0)
			break;	/* seps were trailing */
		while (nsplitfields >= maxsplitfields) {
			/* dynamic growth */
			maxsplitfields += GROW;
			splitfields =
				(char **) checkrealloc((char *) splitfields,
				/*LINTED [alignment ok]*/
				maxsplitfields * sizeof (char *));
		}
		splitfields[nsplitfields++] = pout;
		do {
			*pout++ = *p++;
		} while (*p && !splitseps[*p]);
		if (*p)		/* move over cuz pout might pounce on p */
			p++;
		*pout++ = 0;
	}
	/* undo fast check list for next time */
	for (p = (unsigned char *) seps; *p; p++)
		splitseps[*p] = 0;
}
