/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)io.c	1.10	95/03/31 SMI"	/* SVr4.0 1.10.2.1	*/
/*
 * UNIX shell
 */

#include	"defs.h"
#include	"dup.h"
#include	<fcntl.h>
#include	<sys/types.h>
#include	<sys/stat.h>

short topfd;

/* ========	input output and file copying ======== */

initf(fd)
int	fd;
{
	register struct fileblk *f = standin;

	f->fdes = fd;
	f->fsiz = ((flags & oneflg) == 0 ? BUFFERSIZE : 1);
	f->fnxt = f->fend = f->fbuf;
	f->feval = 0;
	f->flin = 1;
	f->feof = FALSE;
}

estabf(s)
register unsigned char *s;
{
	register struct fileblk *f;

	(f = standin)->fdes = -1;
	f->fend = length(s) + (f->fnxt = s);
	f->flin = 1;
	return (f->feof = (s == 0));
}

push(af)
struct fileblk *af;
{
	register struct fileblk *f;

	(f = af)->fstak = standin;
	f->feof = 0;
	f->feval = 0;
	standin = f;
}

pop()
{
	register struct fileblk *f;

	if ((f = standin)->fstak)
	{
		if (f->fdes >= 0)
			close(f->fdes);
		standin = f->fstak;
		return (TRUE);
	}else
		return (FALSE);
}

struct tempblk *tmpfptr;

pushtemp(fd, tb)
	int fd;
	struct tempblk *tb;
{
	tb->fdes = fd;
	tb->fstak = tmpfptr;
	tmpfptr = tb;
}

poptemp()
{
	if (tmpfptr){
		close(tmpfptr->fdes);
		tmpfptr = tmpfptr->fstak;
		return (TRUE);
	}else
		return (FALSE);
}

chkpipe(pv)
int	*pv;
{
	if (pipe(pv) < 0 || pv[INPIPE] < 0 || pv[OTPIPE] < 0)
		error(piperr);
}

chkopen(idf)
unsigned char *idf;
{
	register int	rc;

	if ((rc = open((char *)idf, 0)) < 0)
		failed(idf, badopen);
	else
		return (rc);
}

/*
 * Make f2 be a synonym (including the close-on-exec flag) for f1, which is
 * then closed.  If f2 is descriptor 0, modify the global ioset variable
 * accordingly.
 */
rename(f1, f2)
register int	f1, f2;
{
#ifdef RES
	if (f1 != f2)
	{
		dup(f1 | DUPFLG, f2);
		close(f1);
		if (f2 == 0)
			ioset |= 1;
	}
#else
	int	fs;

	if (f1 != f2)
	{
		fs = fcntl(f2, 1, 0);
		close(f2);
		fcntl(f1, 0, f2);
		close(f1);
		if (fs == 1)
			fcntl(f2, 2, 1);
		if (f2 == 0)
			ioset |= 1;
	}
#endif
}

create(s)
unsigned char *s;
{
	register int	rc;

	if ((rc = creat((char *)s, 0666)) < 0)
		failed(s, badcreate);
	else
		return (rc);
}

fexists(s)
unsigned char *s;
{
	register int	rc;
	struct stat	stat_buf;

	if ((rc = stat((char *)s, &stat_buf)) < 0)
		return (0);
	else
		return (1);
}

tmpfil(tb)
	struct tempblk *tb;
{
	int fd;

	/* make sure tmp file does not already exist. */
	do {
		itos(serial++);
		movstr(numbuf, tmpname);
	} while (fexists(tmpout));

	fd = create(tmpout);
	pushtemp(fd, tb);
	return (fd);
}

/*
 * set by trim
 */
extern BOOL		nosubst;
#define			CPYSIZ		512

copy(ioparg)
struct ionod	*ioparg;
{
	register unsigned char	*cline;
	register unsigned char	*clinep;
	register struct ionod	*iop;
	unsigned char	c;
	unsigned char	*ends;
	unsigned char	*start;
	int		fd;
	int		i;
	int		stripflg;


	if (iop = ioparg)
	{
		struct tempblk tb;
		copy(iop->iolst);
		ends = mactrim(iop->ioname);
		stripflg = iop->iofile & IOSTRIP;
		if (nosubst)
			iop->iofile &= ~IODOC;
		fd = tmpfil(&tb);

		if (fndef)
			iop->ioname = make(tmpout);
		else
			iop->ioname = cpystak(tmpout);

		iop->iolst = iotemp;
		iotemp = iop;

		cline = clinep = start = locstak();
		if (stripflg)
		{
			iop->iofile &= ~IOSTRIP;
			while (*ends == '\t')
				ends++;
		}
		for (;;)
		{
			chkpr();
			if (nosubst)
			{
				c = readc();
				if (stripflg)
					while (c == '\t')
						c = readc();

				while (!eolchar(c))
				{
					if (clinep >= brkend)
						growstak(clinep);
					*clinep++ = c;
					c = readc();
				}
			}else{
				c = nextc();
				if (stripflg)
					while (c == '\t')
						c = nextc();

				while (!eolchar(c))
				{
					if (clinep >= brkend)
						growstak(clinep);
					*clinep++ = c;
					if (c == '\\')
					{
						if (clinep >= brkend)
							growstak(clinep);
						*clinep++ = readc();
					}
					c = nextc();
				}
			}

			if (clinep >= brkend)
				growstak(clinep);
			*clinep = 0;
			if (eof || eq(cline, ends))
			{
				if ((i = cline - start) > 0)
					write(fd, start, i);
				break;
			}else{
				if (clinep >= brkend)
					growstak(clinep);
				*clinep++ = NL;
			}

			if ((i = clinep - start) < CPYSIZ)
				cline = clinep;
			else
			{
				write(fd, start, i);
				cline = clinep = start;
			}
		}

		poptemp();	/*
				 * pushed in tmpfil -- bug fix for problem
				 * deleting in-line scripts
				 */
	}
}


link_iodocs(i)
	struct ionod	*i;
{
	while (i)
	{
		free(i->iolink);

		/* make sure tmp file does not already exist. */
		do {
			itos(serial++);
			movstr(numbuf, tmpname);
		} while (fexists(tmpout));

		i->iolink = make(tmpout);
		link(i->ioname, i->iolink);
		i = i->iolst;
	}
}


swap_iodoc_nm(i)
	struct ionod	*i;
{
	while (i)
	{
		free(i->ioname);
		i->ioname = i->iolink;
		i->iolink = 0;

		i = i->iolst;
	}
}


savefd(fd)
	int fd;
{
	register int	f;

	f = fcntl(fd, F_DUPFD, 10);
	return (f);
}


restore(last)
	register int	last;
{
	register int 	i;
	register int	dupfd;

	for (i = topfd - 1; i >= last; i--)
	{
		if ((dupfd = fdmap[i].dup_fd) > 0)
			rename(dupfd, fdmap[i].org_fd);
		else
			close(fdmap[i].org_fd);
	}
	topfd = last;
}
