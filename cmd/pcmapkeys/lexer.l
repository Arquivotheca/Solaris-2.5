%{

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993 Sun Microsystems, Inc.
 * All Rights Reserved.
 */
#pragma ident "@(#)lexer.l	1.3	94/01/23 SMI"

#include "y.tab.h"

extern YYSTYPE yylval;

%}

%%
\n			return NLINE;		 /*newline */
"input:"        	return INPUT;
"output:"       	return OUTPUT;
"toggle:"		return TOGGLE;
"dead:"			return DEAD;
"compose:"      	return COMPOSE;
"scancodes:"    	return SCANS;
"SHIFT"			return SHFT;
"CAPS"  		return CAPS;
"NUM"   		return NUM;
"|C"    		return CONTROL;
"|N"    		return ESCN;
"|O"    		return ESCO;
"|["    		return ESCL;
"XK_"[0-9A-Za-z_]*	return NOP;
-			return NOP;

[ \t]+ 			;
"#".*$          	;
"/*".*"*/"      	;

"F"[1-9][0-9]* {
		yylval.u_char = (unsigned char) atoi(yytext+1);
		return FKEY;
	}

[1-9][0-9]* {
		yylval.u_char = (unsigned char) atoi(yytext);
		return BYTE;
	}

0[0-7]+	{
		yylval.u_char = (unsigned char) strtol(yytext, NULL, 0);
		return BYTE;
	}

0[xX][0-9A-Fa-f]+ {
		yylval.u_char = (unsigned char) strtol(yytext, NULL, 0);
		return BYTE;
	}

\'\\[0-3][0-7][0-7]\' {
		yylval.u_char = (unsigned char) strtol(yytext+2, NULL, 0);
		return BYTE;
	}

\'\\[Xx][0-9A-Fa-f][0-9A-Fa-f]\' {
		yytext[1] = '0';
		yylval.u_char = (unsigned char) strtol(yytext+1, NULL, 0);
		return BYTE;
	}

\'[A-Za-z]\' {
		yylval.u_char = yytext[1];
		return BYTE;
	}

\'\^[A-Z]\' {
		yylval.u_char = (yytext[2] - '@');
		return BYTE;
	}

\'\^[a-z]\' {
		yylval.u_char = (yytext[2] - 'a' +1);
		return BYTE;
	}

\".*\"	{
		strcpy(yylval.u_strct.string, yytext + 1);
		yylval.u_strct.value  = yyleng - 2;
		return STRING;
	}

\'\^.\'	{
		yylval.u_char = (yytext[2] - '@');
		return BYTE;
	}

\'.\'	{
		yylval.u_char = yytext[1]; 
		return BYTE;
	}

\'\\n\'	{
		yylval.u_char = '\n';
		return BYTE;
	}

\'\\t\'	{
		yylval.u_char = '\t';
		return BYTE;
	}

\'\\b\'	{
		yylval.u_char = '\b';
		return BYTE;
	}

\'\\r\' {
		yylval.u_char = '\r';
		return BYTE;
	}

.	{
		yylval.u_char = yytext[1];
		return BYTE;
	}

%%
