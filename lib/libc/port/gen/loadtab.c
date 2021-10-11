/*	This module was created for NLS on Dec 18 '87		*/ /* NLS */

/*
 * loadtab - set character table for multi-byte characters
 */

#ident	"@(#)loadtab.c	1.7	92/09/05 SMI"

#include "synonyms.h"
#include "_libc_gettext.h"
#include <stdlib.h>
#include <locale.h>
#include "_locale.h"
#include <sys/fcntl.h>
#include <widec.h>
#include <wctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sys/mman.h>
#include <thread.h>
#include <synch.h>
#include <mtlib.h>

#define	SZ	524

extern	int	_lflag;
extern	struct _wctype *_wcptr[];
void *malloc();

static void adjustwcptr(void *);

int
_loadtab()
/* 
 * _loadtab() loads /usr/lib/locale/${LC_CTYPE}/LC_CTYPE/ctype into
 * _ctype_[] (for single byte codeset) and malloced area (for multibyte
 * locale).
 */
{
	register int fd;
	int ret = -1;
	struct stat fstat;
	static caddr_t ptr= (caddr_t) -1;	/* mmaped object */
	int i;
	static unsigned lsize = 0;
	char *locale;
	char ctypefname[sizeof("/usr/lib/locale/ctype")+LC_NAMELEN];
	
	locale = _cur_locale[LC_CTYPE];
	_lflag = 1;
	strcpy(ctypefname, _fullocale(locale, "LC_CTYPE"));
	strcat(ctypefname, "/ctype");
	if ((fd = open(ctypefname, O_RDONLY)) == -1)
	{
		_lflag = 0;
		fprintf(stderr, _libc_gettext("loadtab: unable to open %s\n"),
			ctypefname);
		exit(1);
	}else if (stat(ctypefname, &fstat) == 0){

		if (fstat.st_size > SZ)
		{
			if (ptr != (caddr_t)-1)
			{
				/* The object is alerady mmap'ed */
				munmap(ptr, lsize);
				ptr = (caddr_t) -1;
			}
			lsize = fstat.st_size;
			if ((ptr = 
			     mmap((caddr_t)0, lsize, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0)) 
			     != (caddr_t) -1)
			{
				/*
				 * The first SZ bytes contains ctype table.
				 * Skip them.
				 */
				adjustwcptr(ptr+SZ);
				ret = 0;
			}
		}
	}
	(void) close(fd);
	return (ret);
}

static void
adjustwcptr(void *base)
/* Adjust wcptr[] by offsetting the real address of wcptr. */
{
	int i;

	for (i=0; i<3; i++)
	{
		_wcptr[i] = (struct _wctype *)
			((int)base + (int)((sizeof (struct _wctype)) * i));
		if (_wcptr[i]->index != 0)
			_wcptr[i]->index = (unsigned char *)
				((int)base + (int)_wcptr[i]->index);
		if (_wcptr[i]->type != 0)
			_wcptr[i]->type = (unsigned *)
				((int)base + (int)_wcptr[i]->type);
		if (_wcptr[i]->code != 0)
			_wcptr[i]->code = (wchar_t *)
				((int)base + (int)_wcptr[i]->code);
	}
}
