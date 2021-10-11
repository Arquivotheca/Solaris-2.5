#ident	"@(#)read_string.c	1.1	94/11/10 SMI"

#include <string.h>
#include <sys/types.h>

/*
 * Read a string, null-terminated, from addr into buf.
 * Return strlen(buf), always < size.
 */

#define	MYSIZE	40

ssize_t
read_string(int fd, char *buf, size_t size, off_t addr)
{
	register int nbyte;
	register ssize_t leng = 0;
	char string[MYSIZE+1];

	if (size < 2)
		return (-1);
	size--;

	*buf = '\0';
	string[MYSIZE] = '\0';
	for (nbyte = MYSIZE; nbyte == MYSIZE && leng < size; addr += MYSIZE) {
		if ((nbyte = pread(fd, string, MYSIZE, addr)) <= 0) {
			buf[leng] = '\0';
			return (leng? leng : -1);
		}
		if ((nbyte = strlen(string)) > 0) {
			if (leng + nbyte > size)
				nbyte = size - leng;
			(void) strncpy(buf+leng, string, nbyte);
			leng += nbyte;
		}
	}
	buf[leng] = '\0';
	return (leng);
}
