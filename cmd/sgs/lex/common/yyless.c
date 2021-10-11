/*	Copyright (c) 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#pragma ident	"@(#)yyless.c	6.5	95/02/11 SMI"

#include <sys/euc.h>
#include <stdlib.h>
#include <widec.h>
#include <limits.h>

#ifndef JLSLEX
#define	CHR    char
#define	YYTEXT yytext
#define	YYLENG yyleng
#define	YYINPUT yyinput
#define	YYUNPUT yyunput
#define	YYOUTPUT yyoutput
#endif

#ifdef WOPTION
#define	CHR    wchar_t
#define	YYTEXT yytext
#define	YYLENG yyleng
#define	YYINPUT yyinput
#define	YYUNPUT yyunput
#define	YYOUTPUT yyoutput
#endif

#ifdef EOPTION
#define	CHR    wchar_t
#define	YYTEXT yywtext
#define	YYLENG yywleng
#define	YYINPUT yywinput
#define	YYUNPUT yywunput
#define	YYOUTPUT yywoutput
#endif

#if defined(__cplusplus) || defined(__STDC__)
/* XCU4: type of yyless() changes to int */
int
yyless(int x)
#else
yyless(x)
#endif
{
	extern CHR YYTEXT[];
	register CHR *lastch, *ptr;
	extern int YYLENG;
	extern int yyprevious;
#ifdef EOPTION
	extern char yytext[];
	extern int yyleng;
#endif
	lastch = YYTEXT+YYLENG;
	if (x >= 0 && x <= YYLENG)
		ptr = x + YYTEXT;
	else
	/*
	 * The cast on the next line papers over an unconscionable nonportable
	 * glitch to allow the caller to hand the function a pointer instead of
	 * an integer and hope that it gets figured out properly.  But it's
	 * that way on all systems.
	 */
		ptr = (CHR *) x;
	while (lastch > ptr)
		YYUNPUT(*--lastch);
	*lastch = 0;
	if (ptr > YYTEXT)
		yyprevious = *--lastch;
	YYLENG = ptr-YYTEXT;
#ifdef EOPTION
	yyleng = wcstombs(yytext, YYTEXT, YYLENG*MB_LEN_MAX);
#endif
	return (0);
}
