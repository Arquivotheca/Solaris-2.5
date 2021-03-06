/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#pragma	ident	"@(#)kobj_subr.c	1.2	94/10/21 SMI"

#include <sys/types.h>

/*
 * Standalone copies of some basic routines.
 */

int
strcmp(register const char *s1, register const char *s2)
{
	if (s1 == s2)
		return (0);
	while (*s1 == *s2++)
		if (*s1++ == '\0')
			return (0);
	return (*s1 - s2[-1]);
}

size_t
strlen(register const char *s)
{
	register const char *s0 = s + 1;

	while (*s++ != '\0')
		;
	return (s - s0);
}

char *
strcpy(register char *s1, register const char *s2)
{
	char *os1 = s1;

	while (*s1++ = *s2++)
		;
	return (os1);
}

char *
strcat(register char *s1, register const char *s2)
{
	char *os1 = s1;

	while (*s1++)
		;
	--s1;
	while (*s1++ = *s2++)
		;
	return (os1);
}

void
bzero(register caddr_t p, size_t count)
{
	register char zero = 0;

	while (count != 0)
		*p++ = zero, count--;	/* Avoid clr for 68000, still... */
}

void
bcopy(register caddr_t src, register caddr_t dest, size_t count)
{
	if (src < dest && (src + count) > dest) {
		/* overlap copy */
		while (--count != -1)
			*(dest + count) = *(src + count);
	} else {
		while (--count != -1)
			*dest++ = *src++;
	}
}
