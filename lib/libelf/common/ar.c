/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)ar.c	1.9	95/05/10 SMI" 	/* SVr4.0 1.6	*/

/*LINTLIBRARY*/

#include "syn.h"
#include <ar.h>
#include <stdlib.h>
#include <memory.h>

#ifdef	MMAP_IS_AVAIL
#	include	<sys/mman.h>
#endif

#include "libelf.h"
#include "decl.h"
#include "error.h"
#include "member.h"


#define MANGLE	'\177'


/* Archive processing
 *	When processing an archive member, two things can happen
 *	that are a little tricky.
 *
 * Sliding
 *	Archive members are only 2-byte aligned within the file.  To reuse
 *	the file's memory image, the library slides an archive member into
 *	its header to align the bytes.  This means the header must be
 *	disposable.
 *
 * Header reuse
 *	Because the library can trample the header, it must be preserved to
 *	avoid restrictions on archive member reuse.  That is, if the member
 *	header changes, the library may see garbage the next time it looks
 *	at the header.  After extracting the original header, the library
 *	appends it to the parents `ed_memlist' list, thus future lookups first
 *	check this list to determine if a member has previously been processed
 *	and whether sliding occured.
 */


/* Size check
 *	If the header is too small, the following generates a negative
 *	subscript for x.x and fails to compile.
 */

struct	x
{
	char	x[sizeof(struct ar_hdr) - 3 * sizeof(Elf32) - 1];
};


static unsigned long	number		_((char *, char *, int));

static const char	fmag[] = ARFMAG;


static unsigned long
number(p, end, base)
	register char	*p;
	register char	*end;
	int		base;
{
	register unsigned	c;
	register unsigned long	n = 0;

	while (p < end)
	{
		if ((c = *p - '0') >= base)
		{
			while (*p++ == ' ')
				if (p >= end)
					return n;
			return 0;
		}
		n *= base;
		n += c;
		++p;
	}
	return n;
}


/* Convert ar_hdr to Member
 *	Converts ascii file representation to the binary memory values.
 */

Member *
_elf_armem(elf, file, fsz)
	Elf			*elf;
	register char		*file;	/* file version */
	size_t			fsz;
{
	register struct ar_hdr	*f = (struct ar_hdr *)file;
	register Member		*m;
	register Memlist	*l, * ol;
	register Memident	*i;

	if (fsz < sizeof(*f))
	{
		_elf_err = EFMT_ARHDRSZ;
		return 0;
	}

	/*	Determine in this member has already been processed
	 */
	for (l = elf->ed_memlist, ol = l; l; ol = l, l = l->m_next)
		for (i = (Memident *)(l + 1); i < l->m_free; i++)
			if (i->m_offset == file)
				return i->m_member;

	if (f->ar_fmag[0] != fmag[0] || f->ar_fmag[1] != fmag[1])
	{
		_elf_err = EFMT_ARFMAG;
		return 0;
	}

	/*	Allocate a new member structure and assign it to the next free
	 *	free memlist ident.
	 */
	if ((m = (Member *)malloc(sizeof(*m))) == 0)
	{
		_elf_err = EMEM_ARMEM;
		return 0;
	}
	if ((elf->ed_memlist == 0 ) || (ol->m_free == ol->m_end)) {
		if ((l = (Memlist *)malloc(sizeof(Memlist) +
			(sizeof(Memident) * MEMIDENTNO))) == 0)
		{
			_elf_err = EMEM_ARMEM;
			return 0;
		}
		l->m_next = 0;
		l->m_free = (Memident *)(l + 1);
		l->m_end = (Memident *)((int)l->m_free +
			(sizeof(Memident) * MEMIDENTNO));

		if (elf->ed_memlist == 0)
			elf->ed_memlist = l;
		else
			ol->m_next = l;
		ol = l;
	}
	ol->m_free->m_offset = file;
	ol->m_free->m_member = m;
	ol->m_free++;

	m->m_err = 0;
	(void)memcpy(m->m_name, f->ar_name, ARSZ(ar_name));
	m->m_name[ARSZ(ar_name)] = '\0';
	m->m_hdr.ar_name = m->m_name;
	(void)memcpy(m->m_raw, f->ar_name, ARSZ(ar_name));
	m->m_raw[ARSZ(ar_name)] = '\0';
	m->m_hdr.ar_rawname = m->m_raw;
	m->m_slide = 0;

	/*	Classify file name.
	 *	If a name error occurs, delay until getarhdr().
	 */

	if (f->ar_name[0] != '/')	/* regular name */
	{
		register char	*p;

		p = &m->m_name[sizeof(m->m_name)];
		while (*--p != '/')
			if (p <= m->m_name)
				break;
		*p = '\0';
	}
	else if (f->ar_name[1] >= '0' && f->ar_name[1] <= '9')	/* strtab */
	{
		register unsigned long	j;

		j = number(&f->ar_name[1], &f->ar_name[ARSZ(ar_name)], 10);
		if (j < elf->ed_arstrsz)
			m->m_hdr.ar_name = elf->ed_arstr + j;
		else
		{
			m->m_hdr.ar_name = 0;
			m->m_err = EFMT_ARSTRNM;
		}
	}
	else if (f->ar_name[1] == ' ')				/* "/" */
		m->m_name[1] = '\0';
	else if (f->ar_name[1] == '/' && f->ar_name[2] == ' ')	/* "//" */
		m->m_name[2] = '\0';
	else							/* "/?" */
	{
		m->m_hdr.ar_name = 0;
		m->m_err = EFMT_ARUNKNM;
	}

	m->m_hdr.ar_date = number(f->ar_date, &f->ar_date[ARSZ(ar_date)], 10);
	m->m_hdr.ar_uid = number(f->ar_uid, &f->ar_uid[ARSZ(ar_uid)], 10);
	m->m_hdr.ar_gid = number(f->ar_gid, &f->ar_gid[ARSZ(ar_gid)], 10);
	m->m_hdr.ar_mode = number(f->ar_mode, &f->ar_mode[ARSZ(ar_mode)], 8);
	m->m_hdr.ar_size = number(f->ar_size, &f->ar_size[ARSZ(ar_size)], 10);
	return m;
}


/* Initial archive processing
 *	An archive may have two special members.
 *	A symbol table, named /, must be first if it is present.
 *	A string table, named //, must precede all "normal" members.
 *
 *	This code "peeks" at headers but doesn't change them.
 *	Later processing wants original headers.
 *
 *	String table is converted, changing '/' name terminators
 *	to nulls.  The last byte in the string table, which should
 *	be '\n', is set to nil, guaranteeing null termination.  That
 *	byte should be '\n', but this code doesn't check.
 *
 *	The symbol table conversion is delayed until needed.
 */

void
_elf_arinit(elf)
	Elf			*elf;
{
	char			*base = elf->ed_ident;
	register char		*end = base + elf->ed_fsz;
	register struct ar_hdr	*a;
	register char		*hdr = base + SARMAG;
	register char		*mem, *nmem;
	int			j, k;
	size_t			sz = SARMAG;

	elf->ed_status = ES_COOKED;
	elf->ed_nextoff = SARMAG;
	for (j = 0; j < 2; ++j)		/* 2 special members */
	{
		unsigned long	n;

		if (end - hdr < sizeof(*a)
		|| _elf_vm(elf, (size_t)(hdr - elf->ed_ident), sizeof(*a)) != OK_YES)
			return;
		a = (struct ar_hdr *)hdr;
		mem = (char *)a + sizeof(*a);
		n = number(a->ar_size, &a->ar_size[ARSZ(ar_size)], 10);
		if (end - mem < n || a->ar_name[0] != '/' || (sz = n) != n)
			return;
		hdr = mem + sz;
		if (a->ar_name[1] == ' ')
		{
			elf->ed_arsym = mem;
			elf->ed_arsymsz = sz;
			elf->ed_arsymoff = (char *)a - base;
		}
		else if (a->ar_name[1] == '/' && a->ar_name[2] == ' ')
		{
			if (_elf_vm(elf, (size_t)(mem - elf->ed_ident), sz) != OK_YES)
				return;
#ifdef	MMAP_IS_AVAIL
			if (elf->ed_vm == 0) {
				if ((nmem = malloc(sz)) == 0) {
					_elf_err = EMEM_ARSTR;
					return;
				}
				(void) memcpy(nmem, mem, sz);
				elf->ed_myflags |= EDF_ASTRALLOC;
				mem = nmem;
			}
#endif	MMAP_IS_AVAIL
			elf->ed_arstr = mem;
			elf->ed_arstrsz = sz;
			elf->ed_arstroff = (char *)a - base;
			for (k = 0; k < sz; k++) {
				if (*mem == '/')
					*mem = '\0';
				++mem;
			}
			*(mem - 1) = '\0';
		}
		else
			return;
		hdr += sz & 1;
	}
}
