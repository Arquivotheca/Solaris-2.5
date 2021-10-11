
#ident	"@(#)t_stdio.c 1.3 93/04/28"

/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <limits.h>
#include <fcntl.h>
#include <string.h>
#include <rmt.h>
#include <sys/vnode.h>
#ifdef USG
#include <sys/fs/ufs_inode.h>
#else
#include <ufs/inode.h>
#endif
#include <protocols/dumprestore.h>

int t_stdio_remote = 0;

/*
 * Tape buffered IO package for dumpdm.
 *
 * This package exists because the standard stdio routines in 5.0
 * cannot be convinced to read large blocks via setvbuf() as in 4.x
 * This causes reads from some tape devices to fail. If stdio is
 * changed in future releases, this file can be deleted and the
 * t_ prefix removed from the corresponding calls in dumpdm.
 * This package only provides what dumpdm needs (for example, t_fopen
 * only supports read-only and assumes that t_setvbuf *will* be called).
 */


#ifdef just_for_looks
typedef struct	/* needs to be binary-compatible with old versions */
{
	int		_cnt;	/* number of available characters in buffer */
	unsigned char	*_ptr;	/* next character from/to here in buffer */
	unsigned char	*_base;	/* the buffer */
	unsigned char	_flag;	/* the state of the stream */
	unsigned char	_file;	/* UNIX System file descriptor */
} FILE;
#endif

/*
 * This local structure is used to hold buffer management variables.
 * I keep the FILE struct for compatibility with the stdio routines,
 * but I want more variables to keep this implementation straight forward.
 * When the file is t_fopened, I squirrel away a pointer to one of these
 * objects in the FILE _ptr field. Ugly - but the whole reason for
 * these routines is ugly.
 */

typedef struct bm
{
	size_t		b_size;	/* size of the attached buffer */
	unsigned char	*b_ptr; /* pointer to next valid data byte */
} BM;


FILE *
t_fopen(const char *name, const char *type)
{
	char *colon;
	FILE *iop;
	BM *bmp;
	int fd;

	if (type[0] != 'r' || type[1] != (char)0) {
		/*
		 * add code if you need more than read-only.
		 */
		return (NULL);
	}

	colon = strchr(name, ':');

	if (colon == NULL) {
		if ((fd = open(name, O_RDONLY)) < 0)
			return (NULL);
	} else {
		char *path, host[256];

		(void) strcpy(host, name);
		host[colon - name] = '\0';
		path = ++colon;
		if ((rmthost((char *)name, HIGHDENSITYTREC) == 0) ||
		    ((fd = rmtopen(path, O_RDONLY)) < 0))
			return (NULL);

		t_stdio_remote = 1;
	}

	if (fd > UCHAR_MAX) {
		(void) close(fd);
		return (NULL);
	}

	if ((iop = (FILE *) malloc(sizeof (FILE))) == NULL) {
		(void) close(fd);
		return (NULL);
	}

	if ((bmp = (BM *) malloc(sizeof (BM))) == NULL) {
		(void) close(fd);
		free((char *)iop);
		return (NULL);
	}
	iop->_file = fd;
	iop->_base = 0;
	iop->_ptr = (unsigned char *)bmp;
	iop->_flag = 0;
	/*
	 * Note that there is no buffer associated with this stream.
	 * Relies on setvbuf() for the buffer.
	 */
	return (iop);
}

int
t_setvbuf(FILE *iop, char *abuf, int type, size_t size)
{
	BM *bmp;
	/*
	 * Only supports full buffering.
	 */
	if (type != _IOFBF)
		return (EOF);

	iop->_base = (unsigned char *)abuf;
	bmp = (BM *)iop->_ptr;
	bmp->b_size = size;
	bmp->b_ptr = iop->_base;
	iop->_cnt = 0;
	return (0);
}

int
t_fclose(FILE *iop)
{
	/*
	 * No flushing since we are open read-only.
	 */
	iop->_base = 0; /* defensive */
	if (t_stdio_remote)
		(void) rmtclose();
	else
		(void) close(iop->_file);
	free((char *)iop->_ptr);
	free((char *)iop);
	return (0);
}

int
t_fileno(FILE *iop)
{
	return ((int)iop->_file);
}

void
t_rewind(FILE *iop)
{
	BM *bmp;

	(void) lseek(t_fileno(iop), 0L, 0);
	bmp = (BM *)iop->_ptr;
	bmp->b_ptr = iop->_base;
	iop->_cnt = 0;
	iop->_flag &= ~_IOEOF;
}

int
t_feof(FILE *iop)
{
	return (iop->_flag & _IOEOF);
}

size_t
t_fread(void *ptr, size_t size, size_t count, FILE *iop)
{
	size_t want, remain;
	BM *bmp;
	ssize_t n;
	unsigned char *cptr = (unsigned char *)ptr;

	if (iop->_base == 0 || size == 0 || count == 0)
		return (0);

	want = remain = size * count;
	bmp = (BM *)iop->_ptr;

	if (want <= iop->_cnt) {
		/*
		 * Satisfy the request from the buffer.
		 */
		memcpy((void *)cptr, (void *)bmp->b_ptr, want);
		bmp->b_ptr += want;
		iop->_cnt -= want;
		return (count);
	}

	if (iop->_cnt > 0) {
		/*
		 * Empty the buffer and then read some more.
		 */
		memcpy((void *)cptr, (void *)bmp->b_ptr, iop->_cnt);
		bmp->b_ptr += iop->_cnt;
		remain -= iop->_cnt;
		cptr += iop->_cnt;
		iop->_cnt = 0;
	}

	while (remain > 0) {
		/*
		 * Attempt to read chunks the size of the buffer passed
		 * in from t_setvbuf().
		 */
		if (t_stdio_remote)
			n = rmtread((char *)iop->_base, bmp->b_size);
		else
			n = read(iop->_file, iop->_base, bmp->b_size);
		if (n < 0) {
			/*
			 * return what has already been read.
			 */
			return ((want - remain)/size);
		}
		if (n == 0) {
			/*
			 * Let t_feof succeed.
			 */
			iop->_flag = _IOEOF;
			/*
			 * return what has already been read.
			 */
			return ((want - remain)/size);
		}
		/*
		 * Some amount of data was read. Copy the data to
		 * caller and see if we are done.
		 */
		if (n >= remain) {
			memcpy((void *)cptr, (void *)iop->_base, remain);
			bmp->b_ptr = iop->_base+remain;
			iop->_cnt = n-remain;
			return (count);
		} else {
			memcpy((void *)cptr, (void *)iop->_base, n);
			iop->_cnt = 0;
			remain -= n;
			cptr += n;
		}
	}
}
