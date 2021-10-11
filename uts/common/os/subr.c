/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)subr.c	1.32	95/10/03 SMI"	/* SVr4.0 1.44	*/

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/vmparam.h>
#include <sys/systm.h>
#include <sys/cred.h>
#include <sys/user.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <sys/tuneable.h>
#include <sys/errno.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

/*
 * Routine which sets a user error; placed in
 * illegal entries in the bdevsw and cdevsw tables.
 */

int
nodev()
{
	return (curthread->t_lwp ?
	    ttolwp(curthread)->lwp_error = ENXIO : ENXIO);
}

/*
 * Null routine; placed in insignificant entries
 * in the bdevsw and cdevsw tables.
 */

int
nulldev()
{
	return (0);
}

/*
 * Generate an unused major device number.
 */
int
getudev()
{
	static int next = 0;
	int ret;
	extern kmutex_t udevlock;

	/*
	 * As above, ensure that we start allocating major numbers
	 * above the 'devcnt' count.  The only limit we
	 * place on the number is that it should be a legal
	 * SVR4 major number (and not L_MAXMAJ which happens
	 * to be equal to devcnt in the current system)
	 */
	mutex_enter(&udevlock);
	if (next == 0)
		next = devcnt;
	if (next < (1 << L_BITSMAJOR) && next >= devcnt)
		ret = next++;
	else
		ret = -1;
	mutex_exit(&udevlock);
	return (ret);
}

/*
 * C-library string functions.  Assembler versions of others are in
 * ml/string.s.
 */

/*
 * Copy s2 to s1, truncating or null-padding to always copy n bytes.
 * Return s1.
 */

char *
strncpy(s1, s2, n)
	register char *s1;
	register const char *s2;
	register size_t n;
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

/*
 * Return the ptr in sp at which the character c last
 * appears; NULL if not found
 */

#ifndef	NULL
#define	NULL 0
#endif	NULL

char *
strrchr(sp, c)
	register const char *sp;
	register int c;
{
	register const char *r = NULL;

	do {
		if (*sp == c)
			r = sp;
	} while (*sp++);
	return ((char *)r);
}

/*
 * Like strrchr(), except
 * (a) it takes a maximum length for the string to be searched, and
 * (b) if the string ends with a null, it is not considered part of
 *     the string.
 */

char *
strnrchr(const char *sp, int c, size_t n)
{
	const char *r = NULL;

	while (n-- > 0 && *sp) {
		if (*sp == c)
			r = sp;
		sp++;
	}
	return ((char *)r);
}

/*
 * Compare two byte streams.  Returns 0 if they're identical, 1
 * if they're not.
 */
int
bcmp(s1, s2, len)
	register char *s1, *s2;
	register size_t len;
{
	while (len--)
		if (*s1++ != *s2++)
			return (1);
	return (0);
}

int
memlow()
{
	return (freemem <= tune.t_gpgslo);
}

/* takes a numeric char, yields an int */
#define	CTOI(c)		((c) & 0xf)
/* takes an int, yields an int */
#define	TEN_TIMES(n)	(((n) << 3) + ((n) << 1))

/*
 * Returns the integer value of the string of decimal numeric
 * chars beginning at **str.
 * Does no overflow checking.
 * Note: updates *str to point at the last character examined.
 */
int
stoi(str)
	register char	**str;
{
	register char	*p = *str;
	register int	n;
	register int	c;

	for (n = 0; (c = *p) >= '0' && c <= '9'; p++) {
		n = TEN_TIMES(n) + CTOI(c);
	}
	*str = p;
	return (n);
}

/*
 * Simple-minded conversion of a long into a null-terminated character
 * string.  Caller must ensure there's enough space to hold the result.
 */
void
numtos(num, s)
	u_long num;
	char *s;
{
	register int i = 0;
	register u_long mm = 1000000000;
	int t;

	if (num < 10) {
		*s++ = num + '0';
		*s = '\0';
	} else while (mm) {
		t = num / mm;
		if (i || t) {
			i++;
			*s++ = t + '0';
			num -= t * mm;
		}
		mm = mm / 10;
	}
	*s = '\0';
}

int
rlimit(resource, softlimit, hardlimit)
	int resource;
	rlim_t softlimit, hardlimit;
{
	struct proc *p = ttoproc(curthread);

	if (softlimit > hardlimit)
		return (EINVAL);

	if (hardlimit > u.u_rlimit[resource].rlim_max &&
	    !suser(CRED()))
		return (EPERM);

	switch (resource) {
	case RLIMIT_DATA:
		if (softlimit > (rlim_t)DSIZE_LIMIT)
			softlimit = (rlim_t)DSIZE_LIMIT;
		if (hardlimit > (rlim_t)DSIZE_LIMIT)
			hardlimit = (rlim_t)DSIZE_LIMIT;
		break;

	case RLIMIT_STACK:
		if (softlimit > SSIZE_LIMIT)
			softlimit = SSIZE_LIMIT;
		if (hardlimit > SSIZE_LIMIT)
			hardlimit = SSIZE_LIMIT;
		break;
	}

	/*
	 * Prevent multiple threads from updating the
	 * rlimit members in a random order.
	 */
	mutex_enter(&p->p_lock);
	u.u_rlimit[resource].rlim_cur = softlimit;
	u.u_rlimit[resource].rlim_max = hardlimit;
	mutex_exit(&p->p_lock);

	return (0);
}

int
min(a, b)
	int a, b;
{

	return (a < b ? a : b);
}

int
max(a, b)
	int a, b;
{

	return (a > b ? a : b);
}

u_int
umin(a, b)
	u_int a, b;
{

	return (a < b ? a : b);
}

u_int
umax(a, b)
	u_int a, b;
{

	return (a > b ? a : b);
}

/*
 * XXX - strictly speaking, the following routines should
 * go in common/ml/string.c or something.
 */

/*
 * Note:  strlen() is implemented in assembly language for performance.
 */

/*
 * Compare strings:  s1>s2: >0  s1==s2: 0  s1<s2: <0
 */
strcmp(s1, s2)
	register const char *s1;
	register const char *s2;
{

	while (*s1 == *s2++)
		if (*s1++ == '\0')
			return (0);
	return (*s1 - *--s2);
}

/*
 * Compare strings (at most n bytes): return *s1-*s2 for the last
 * characters in s1 and s2 which were compared.
 */
int
strncmp(s1, s2, n)
	register const char *s1;
	register const char *s2;
	register size_t n;
{
	if (s1 == s2)
		return (0);
	n++;
	while (--n != 0 && *s1 == *s2++)
		if (*s1++ == '\0')
			return (0);
	return ((n == 0) ? 0 : *s1 - *--s2);
}

/*
 * Return bit position of least significant bit set in mask,
 * starting numbering from 1.
 */
ffs(mask)
	register long mask;
{
	register int i;

	if (mask == 0)
		return (0);
	for (i = 1; i <= NBBY * sizeof (mask); i++) {
		if (mask & 1)
			return (i);
		mask >>= 1;
	}
	return (0);
}

/* Concatenate string s2 to string s1 */
char *
strcat(s1, s2)
	char *s1;
	const char *s2;
{
	register char *s = s1;

	while (*s)
		s++;		/* find the end of string s1 */

	strcpy(s, s2);
	return (s1);
}
/*
 * Return the ptr in sp at which the character c first
 * appears; NULL if not found
 */

char *
strchr(sp, c)
	register const char *sp;
	register int c;
{

	do {
		if (*sp == (char)c)
			return ((char *)sp);
	} while (*sp++);
	return (NULL);
}

/*
 * Convert hex string to u_int. Assumes no leading "0x".
 */
u_int
atou(s)
	char *s;
{
	u_int val = 0;
	u_int digit;

	while (*s) {
		if (*s >= '0' && *s <= '9')
			digit = *s++ - '0';
		else if (*s >= 'a' && *s <= 'f')
			digit = *s++ - 'a' + 10;
		else if (*s >= 'A' && *s <= 'F')
			digit = *s++ - 'A' + 10;
		else
			break;
		val = (val * 16) + digit;
	}
	return (val);
}

/*
 * Tables to convert a single byte to/from binary-coded decimal (BCD).
 */
u_char byte_to_bcd[256] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
};

u_char bcd_to_byte[256] = {		/* CSTYLED */
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  0,  0,  0,  0,  0,  0,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 19,  0,  0,  0,  0,  0,  0,
	20, 21, 22, 23, 24, 25, 26, 27, 28, 29,  0,  0,  0,  0,  0,  0,
	30, 31, 32, 33, 34, 35, 36, 37, 38, 39,  0,  0,  0,  0,  0,  0,
	40, 41, 42, 43, 44, 45, 46, 47, 48, 49,  0,  0,  0,  0,  0,  0,
	50, 51, 52, 53, 54, 55, 56, 57, 58, 59,  0,  0,  0,  0,  0,  0,
	60, 61, 62, 63, 64, 65, 66, 67, 68, 69,  0,  0,  0,  0,  0,  0,
	70, 71, 72, 73, 74, 75, 76, 77, 78, 79,  0,  0,  0,  0,  0,  0,
	80, 81, 82, 83, 84, 85, 86, 87, 88, 89,  0,  0,  0,  0,  0,  0,
	90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
};
