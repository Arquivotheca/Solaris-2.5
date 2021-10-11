#ifndef lint
static char sccsid[] = "@(#)adr.c 1.8 93/01/29 SMI";
#endif

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

/*
 * Adr memory based encoding
 */

#include <sys/types.h>
#include <bsm/audit.h>
#include <bsm/libbsm.h>
#include <bsm/audit_record.h>

void
#ifdef __STDC__
adr_start(adr_t *adr, char *p)
#else
adr_start(adr, p)
	adr_t *adr;
	char *p;
#endif
{
	adr->adr_stream = p;
	adr->adr_now = p;
}

int
#ifdef __STDC__
adr_count(adr_t *adr)
#else
adr_count(adr)
	adr_t *adr;
#endif
{
	return (((int)adr->adr_now) - ((int)adr->adr_stream));
}


/*
 * adr_char - pull out characters
 */
void
#ifdef __STDC__
adr_char(adr_t *adr, char *cp, int count)
#else
adr_char(adr, cp, count)
	adr_t *adr;
	char *cp;
	int count;
#endif
{
	while (count-- > 0)
		*adr->adr_now++ = *cp++;
}

/*
 * adr_short - pull out shorts
 */
void
#ifdef __STDC__
adr_short(adr_t *adr, short *sp, int count)
#else
adr_short(adr, sp, count)
	adr_t *adr;
	short *sp;
	int count;
#endif
{

	for (; count-- > 0; sp++) {
		*adr->adr_now++ = (char)((*sp >> 8) & 0x00ff);
		*adr->adr_now++ = (char)(*sp & 0x00ff);
	}
}

/*
 * adr_long - pull out long
 */
void
#ifdef __STDC__
adr_long(adr_t *adr, long *lp, int count)
#else
adr_long(adr, lp, count)
	adr_t *adr;
	long *lp;
	int count;
#endif
{
	int i;		/* index for counting */
	long l;		/* value for shifting */

	for (; count-- > 0; lp++) {
		for (i = 0, l = *lp; i < 4; i++) {
			*adr->adr_now++ =
				(char)((unsigned)(l & 0xff000000) >> 24);
			l <<= 8;
		}
	}
}
