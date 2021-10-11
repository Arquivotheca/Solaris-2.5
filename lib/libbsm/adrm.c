#ifndef lint
static char sccsid[] = "@(#)adrm.c 1.3 93/01/29 SMI";
#endif

/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

/*
 * Adr memory based translations
 */

#include <stdio.h>
#include <sys/types.h>
#include <bsm/audit.h>
#include <bsm/audit_record.h>

void
#ifdef __STDC__
adrm_start(adr_t *adr, char *p)
#else
adrm_start(adr, p)
	adr_t *adr;
	char *p;
#endif
{
	adr->adr_stream = p;
	adr->adr_now = p;
}

/*
 * adrm_char - pull out characters
 */
void
#ifdef __STDC__
adrm_char(adr_t *adr, char *cp, int count)
#else
adrm_char(adr, cp, count)
	adr_t *adr;
	char *cp;
	int count;
#endif
{
	while (count--)
		*cp++ = *adr->adr_now++;
}

/*
 * adrm_short - pull out shorts
 */
void
#ifdef __STDC__
adrm_short(adr_t *adr, short *sp, int count)
#else
adrm_short(adr, sp, count)
	adr_t *adr;
	short *sp;
	int count;
#endif
{

	while (count--) {
		*sp = *adr->adr_now++ << 8;
		*sp++ += ((short)*adr->adr_now++) & 0x00ff;
	}
}

/*
 * adrm_long - pull out long
 */
void
#ifdef __STDC__
adrm_long(adr_t *adr, long *lp, int count)
#else
adrm_long(adr, lp, count)
	adr_t *adr;
	long *lp;
	int count;
#endif
{
	int i;

	for (; count--; lp++) {
		*lp = 0;
		for (i = 0; i < 4; i++) {
			*lp <<= 8;
			*lp += ((long)*adr->adr_now++) & 0x000000ff;
		}
	}
}

void
#ifdef __STDC__
adrm_string(adr_t *adr, char *p)
#else
adrm_string(adr, p)
	adr_t *adr;
	char *p;
#endif
{
	short c;

	adrm_short(adr, &c, 1);
	adrm_char(adr, p, c);
}

void
#ifdef __STDC__
adrm_int(adr_t *adr, int *cp, int count)
#else
adrm_int(adr, cp, count)
	adr_t *adr;
	int *cp;
	int count;
#endif
{
	adrm_long(adr, (long *)cp, count);
}

void
#ifdef __STDC__
adrm_u_int(adr_t *adr, u_int *cp, int count)
#else
adrm_u_int(adr, cp, count)
	adr_t *adr;
	u_int *cp;
	int count;
#endif
{
	adrm_long(adr, (long *)cp, count);
}

void
#ifdef __STDC__
adrm_opaque(adr_t *adr, char *p)
#else
adrm_opaque(adr, p)
	adr_t *adr;
	char *p;
#endif
{
	adrm_string(adr, p);
}

void
#ifdef __STDC__
adrm_u_char(adr_t *adr, u_char *cp, int count)
#else
adrm_u_char(adr, cp, count)
	adr_t *adr;
	u_char *cp;
	int count;
#endif
{
	adrm_char(adr, (char *)cp, count);
}

void
#ifdef __STDC__
adrm_u_long(adr_t *adr, u_long *lp, int count)
#else
adrm_u_long(adr, lp, count)
	adr_t *adr;
	u_long *lp;
	int count;
#endif
{
	adrm_long(adr, (long *)lp, count);
}

void
#ifdef __STDC__
adrm_u_short(adr_t *adr, u_short *sp, int count)
#else
adrm_u_short(adr, sp, count)
	adr_t *adr;
	u_short *sp;
	int count;
#endif
{
	adrm_short(adr, (short *)sp, count);
}
