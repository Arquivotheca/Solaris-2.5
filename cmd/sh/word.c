/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)word.c	1.12	95/03/01 SMI"	/* SVr4.0 1.11.2.2	*/
/*
 * UNIX shell
 */

#include	"defs.h"
#include	"sym.h"
#include	<errno.h>

/*
 * _mbftowc() is not officially supported but is there in libw.a.
 */
#ifdef __STDC__
int _mbftowc(char *, wchar_t *, int (*)(), int *);
#else
int _mbftowc();
#endif /* __STDC__ */

static 	int readb();

/* ========	character handling for command lines	======== */


word()
{
	register unsigned char	c, d;
	struct argnod	*arg = (struct argnod *)locstak();
	register unsigned char	*argp = arg->argval;
	unsigned char	*oldargp;
	int		alpha = 1;

	wdnum = 0;
	wdset = 0;

	while (1)
	{
		while (c = nextc(), space(c))		/* skipc() */
			;

		if (c == COMCHAR)
		{
			while ((c = readc()) != NL && c != EOF);
			peekc = c;
		}
		else
		{
			break;	/* out of comment - white space loop */
		}
	}
	if (!eofmeta(c))
	{
		do
		{
			if (c == LITERAL)
			{
				oldargp = argp;
				while ((c = readc()) && c != LITERAL){
					/*
					 * quote each character within
					 * single quotes
					 */
					unsigned char *pc = readw(c);
					if (argp >= brkend)
						growstak(argp);
					*argp++='\\';
				/* Pick up rest of multibyte character */
					if (c == NL)
						chkpr();
					while (c = *pc++) {
						if (argp >= brkend)
							growstak(argp);
						*argp++ = c;
					}
				}
				if (argp == oldargp) { /* null argument - '' */
				/*
				 * Word will be represented by quoted null
				 * in macro.c if necessary
				 */
					if (argp >= brkend)
						growstak(argp);
					*argp++ = '"';
					if (argp >= brkend)
						growstak(argp);
					*argp++ = '"';
				}
			}
			else
			{
				if (argp >= brkend)
					growstak(argp);
				*argp++ = (c);
				if (c == '\\') {
					if (argp >= brkend)
						growstak(argp);
					*argp++ = readc();
				}
				if (c == '=')
					wdset |= alpha;
				if (!alphanum(c))
					alpha = 0;
				if (qotchar(c))
				{
					d = c;
					for (;;)
					{
						if (argp >= brkend)
							growstak(argp);
						if ((*argp++ = (c = nextc()))
						    == 0 ||
						    c == d)
							break;
						if (c == NL)
							chkpr();
						/*
						 * don't interpret quoted
						 * characters
						 */
						if (c == '\\') {
							if (argp >= brkend)
								growstak(argp);
							*argp++ = readc();
						}
					}
				}
			}
		} while ((c = nextc(), !eofmeta(c)));
		argp = endstak(argp);
		if (!letter(arg->argval[0]))
			wdset = 0;

		peekn = c | MARK;
		if (arg->argval[1] == 0 &&
		    (d = arg->argval[0], digit(d)) &&
		    (c == '>' || c == '<'))
		{
			word();
			wdnum = d - '0';
		}else{ /* check for reserved words */
			if (reserv == FALSE ||
			    (wdval = syslook(arg->argval,
					reserved, no_reserved)) == 0) {
				wdval = 0;
			}
			/* set arg for reserved words too */
			wdarg = arg;
		}
	}else if (dipchar(c)){
		if ((d = nextc()) == c)
		{
			wdval = c | SYMREP;
			if (c == '<')
			{
				if ((d = nextc()) == '-')
					wdnum |= IOSTRIP;
				else
					peekn = d | MARK;
			}
		}
		else
		{
			peekn = d | MARK;
			wdval = c;
		}
	}
	else
	{
		if ((wdval = c) == EOF)
			wdval = EOFSYM;
		if (iopend && eolchar(c))
		{
			struct ionod *tmp_iopend;
			tmp_iopend = iopend;
			iopend = 0;
			copy(tmp_iopend);
		}
	}
	reserv = FALSE;
	return (wdval);
}

unsigned char skipc()
{
	register unsigned char c;

	while (c = nextc(), space(c))
		;
	return (c);
}

unsigned char nextc()
{
	register unsigned char	c, d;

retry:
	if ((d = readc()) == ESCAPE)
	{
		if ((c = readc()) == NL)
		{
			chkpr();
			goto retry;
		}
		peekc = c | MARK;
	}
	return (d);
}

unsigned char *readw(d)
unsigned char d;
{
	static unsigned char c[MULTI_BYTE_MAX + 1];
	int length;
	wchar_t l;
	if (isascii(d)) {
		c[0] = d;
		c[1] = '\0';
		return (c);
	}
	peekc = d; /* will not overwrite peekc = '\\' */
	length = _mbftowc((char *)c, &l, (int (*)())readc, &peekc);
	if (length < 0)
		length = -length;
	c[length] = '\0';
	return (c);
}

unsigned char readc()
{
	unsigned register char	c;
	register int	len;
	register struct fileblk *f;

	if (peekn)
	{
		c = peekn;
		peekn = 0;
		return (c);
	}
	if (peekc)
	{
		c = peekc;
		peekc = 0;
		return (c);
	}
	f = standin;
retry:
	if (f->fnxt != f->fend)
	{
		if ((c = *f->fnxt++) == 0)
		{
			if (f->feval)
			{
				if (estabf(*f->feval++))
					c = EOF;
				else
					c = SP;
			}
			else
				goto retry;	/* = c = readc(); */
		}
		if (flags & readpr && standin->fstak == 0)
			prc(c);
		if (c == NL)
			f->flin++;
	}else if (f->feof || f->fdes < 0){
		c = EOF;
		f->feof++;
	}else if ((len = readb()) <= 0){
		if (f->fdes != input || !isatty(input)) {
			close(f->fdes);
			f->fdes = -1;
		}
		f->feof++;
		c = EOF;
	}
	else
	{
		f->fend = (f->fnxt = f->fbuf) + len;
		goto retry;
	}
	return (c);
}

static
readb()
{
	register struct fileblk *f = standin;
	register int	len;

	do
	{
		if (trapnote & SIGSET)
		{
			newline();
			sigchk();
		}else if ((trapnote & TRAPSET) && (rwait > 0)){
			newline();
			chktrap();
			clearup();
		} else if ((trapnote & TRAPSET) && (errno == EINTR))
			chktrap();
		errno = 0;
	} while ((len = read(f->fdes, f->fbuf, f->fsiz)) < 0 && trapnote);
	return (len);
}
