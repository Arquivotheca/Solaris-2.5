/*
 * Copyright 1994 (c) by Sun Microsystems Inc.
 */

#ident	"@(#)strsubr.c	1.5	94/11/29 SMI" /* from SunOS 4.1 */

#include <sys/types.h>
#include <sys/mkdev.h>

/*
 * Miscellaneous routines used by the standalones.
 */

int
strlen(register char *s)
{
	register int n;

	n = 0;
	while (*s++)
		n++;
	return (n);
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

char *
strcpy(register char *s1, register char *s2)
{
	register char *os1;

	os1 = s1;
	while (*s1++ = *s2++)
		;
	return (os1);
}

/*
 * Compare strings:  s1>s2: >0  s1==s2: 0  s1<s2: <0
 */
int
strcmp(register char *s1, register char *s2)
{
	while (*s1 == *s2++)
		if (*s1++ == '\0')
			return (0);
	return (*s1 - *--s2);
}

int
strncmp(register char *s1, register char *s2, register int n)
{
	while (--n >= 0 && *s1 == *s2++)
		if (*s1++ == '\0')
			return (0);
	return (n<0 ? 0 : *s1 - *--s2);
}

/*
 * Copy s2 to s1, truncating or null-padding to always
 * copy n bytes.  Return s1.
 */
char *
strncpy(register char *s1, register char *s2, register size_t n)
{
	register char *os1 = s1;

	n++;
	while (--n != 0 && (*s1++ = *s2++) != '\0')
		;
	if (n != 0)
		while (--n != 0)
			*s1++ = '\0';
	return (os1);
}

int
bcmp(register char *s1, register char *s2, register int len)
{
	while (len--)
		if (*s1++ != *s2++)
			return (1);
	return (0);
}


/*
 * Return the ptr in sp at which the character c last
 * appears, or NULL if not found.
 */
char *
strrchr(register char *sp, char c)
{
	register char *r;

	r = '\0';
	do {
		if (*sp == c)
			r = sp;
	} while (*sp++);
	return (r);
}

/*
 * Return the ptr in sp at which the character c first appears;
 * NULL if not found
 */
char *
strchr(register const char *sp, register int c)
{
	do {
		if (*sp == (char) c)
			return ((char *)sp);
	} while (*sp++);
	return (0);
}

int
bzero(register char *p, register int n)
{
	register char zeero = 0;

	while (n > 0)
		*p++ = zeero, n--;	/* Avoid clr for 68000, still... */
}

int
bcopy(register char *src, register char *dest, register int count)
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
