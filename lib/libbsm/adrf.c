#ifndef lint
static char sccsid[] = "@(#)adrf.c 1.10 93/07/29 SMI";
#endif

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <sys/types.h>
#include <bsm/audit.h>
#include <bsm/libbsm.h>
#include <bsm/audit_record.h>

void
#ifdef __STDC__
adrf_start(adr_t *adr, FILE *fp)
#else
adrf_start(adr, fp)
	adr_t *adr;
	FILE *fp;
#endif
{
	adr->adr_stream = (char *)fp;
	adr->adr_now = (char *)0;
}

/*
 * adrf_char - pull out characters
 */
int
#ifdef __STDC__
adrf_char(adr_t *adr, char *cp, int count)
#else
adrf_char(adr, cp, count)
	adr_t *adr;
	char *cp;
	int count;
#endif
{
	int c;	/* read character in here */

	while (count--) {
		if ((c = fgetc((FILE *)adr->adr_stream)) == EOF)
			return (-1);
		*cp++ = c;
	}
	return (0);
}

/*
 * adrf_short - pull out shorts
 */
int
#ifdef __STDC__
adrf_short(adr_t *adr, short *sp, int count)
#else
adrf_short(adr, sp, count)
	adr_t *adr;
	short *sp;
	int count;
#endif
{
	int c;	/* read character in here */

	while (count--) {
		if ((c = fgetc((FILE *)adr->adr_stream)) == EOF)
			return (-1);
		*sp = c << 8;
		if ((c = fgetc((FILE *)adr->adr_stream)) == EOF)
			return (-1);
		*sp++ |= c & 0x00ff;
	}
	return (0);
}

/*
 * adrf_long - pull out long
 */
int
#ifdef __STDC__
adrf_long(adr_t *adr, long *lp, int count)
#else
adrf_long(adr, lp, count)
	adr_t *adr;
	long *lp;
	int count;
#endif
{
	int i;
	int c;	/* read character in here */

	for (; count--; lp++) {
		*lp = 0;
		for (i = 0; i < 4; i++) {
			if ((c = fgetc((FILE *)adr->adr_stream)) == EOF)
				return (-1);
			*lp <<= 8;
			*lp |= c & 0x000000ff;
		}
	}
	return (0);
}

int
#ifdef __STDC__
adrf_string(adr_t *adr, char *p)
#else
adrf_string(adr, p)
	adr_t *adr;
	char *p;
#endif
{
	short c;

	if (adrf_short(adr, &c, 1) != 0)
		return (-1);
	return (adrf_char(adr, p, c));
}

int
#ifdef __STDC__
adrf_int(adr_t *adr, int *cp, int count)
#else
adrf_int(adr, cp, count)
	adr_t *adr;
	int *cp;
	int count;
#endif
{
	return (adrf_long(adr, (long *)cp, count));
}

int
#ifdef __STDC__
adrf_u_int(adr_t *adr, u_int *cp, int count)
#else
adrf_u_int(adr, cp, count)
	adr_t *adr;
	u_int *cp;
	int count;
#endif
{
	return (adrf_long(adr, (long *)cp, count));
}

int
#ifdef __STDC__
adrf_opaque(adr_t *adr, char *p)
#else
adrf_opaque(adr, p)
	adr_t *adr;
	char *p;
#endif
{
	return (adrf_string(adr, p));
}

int
#ifdef __STDC__
adrf_u_char(adr_t *adr, u_char *cp, int count)
#else
adrf_u_char(adr, cp, count)
	adr_t *adr;
	u_char *cp;
	int count;
#endif
{
	return (adrf_char(adr, (char *)cp, count));
}

int
#ifdef __STDC__
adrf_u_long(adr_t *adr, u_long *lp, int count)
#else
adrf_u_long(adr, lp, count)
	adr_t *adr;
	u_long *lp;
	int count;
#endif
{
	return (adrf_long(adr, (long *)lp, count));
}

int
#ifdef __STDC__
adrf_u_short(adr_t *adr, u_short *sp, int count)
#else
adrf_u_short(adr, sp, count)
	adr_t *adr;
	u_short *sp;
	int count;
#endif
{
	return (adrf_short(adr, (short *)sp, count));
}

int
#ifdef __STDC__
adrf_peek(adr_t *adr)
#else
adrf_peek(adr)
	adr_t *adr;
#endif
{
	return (ungetc(fgetc((FILE *)adr->adr_stream),
		(FILE *)adr->adr_stream));
}
