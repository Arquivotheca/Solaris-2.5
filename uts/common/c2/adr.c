/* @(#)adr.c 2.2 91/06/05 SMI; SunOS CMW */
/* @(#)adr.c 4.2.1.2 91/05/08 SMI; BSM Module */
/*
 * Adr memory based encoding
 */

#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/systm.h>
#include <sys/mutex.h>
#include <sys/thread.h>
#include <c2/audit.h>
#include <c2/audit_kernel.h>
#include <c2/audit_record.h>

void
adr_start(adr, p)
	adr_t *adr;
	char *p;
{
	adr->adr_stream = p;
	adr->adr_now = p;
}

int
adr_count(adr)
	adr_t *adr;
{
	return (((int)adr->adr_now) - ((int)adr->adr_stream));
}


/*
 * adr_char - pull out characters
 */
void
adr_char(adr, cp, count)
	adr_t *adr;
	char *cp;
	int count;
{
	while (count-- > 0)
		*adr->adr_now++ = *cp++;
}

/*
 * adr_short - pull out shorts
 */
void
adr_short(adr, sp, count)
	adr_t *adr;
	short *sp;
	int count;
{

	for (; count-- > 0; sp++) {
		*adr->adr_now++ = (char)((*sp >> (int) 8) & 0x00ff);
		*adr->adr_now++ = (char)(*sp & 0x00ff);
	}
}

/*
 * adr_long - pull out long
 */
void
adr_long(adr, lp, count)
	adr_t *adr;
	long *lp;
	int count;
{
	int i;		/* index for counting */
	long l;		/* value for shifting */

	for (; count-- > 0; lp++) {
		for (i = 0, l = *lp; i < 4; i++) {
			*adr->adr_now++ = (char)((l & (int) 0xff000000) >>
				(int) 24);
			l <<= (int) 8;
		}
	}
}

char *
adr_getchar(adr, cp)
	adr_t	*adr;
	char	*cp;
{
	char	*old;

	old = adr->adr_now;
	*cp = *adr->adr_now++;
	return (old);
}

char *
adr_getshort(adr, sp)
	adr_t	*adr;
	short	*sp;
{
	char	*old;

	old = adr->adr_now;
	*sp = *adr->adr_now++;
	*sp >>= (int) 8;
	*sp = *adr->adr_now++;
	*sp >>= (int) 8;
	return (old);
}


char *
adr_getlong(adr, lp)
	adr_t	*adr;
	long	*lp;
{
	char	*old;
	int	i;

	old = adr->adr_now;
	for (i = 0; i < 4; i++) {
		*lp = *adr->adr_now++;
		*lp >>= (int) 8;
	}
	return (old);
}
