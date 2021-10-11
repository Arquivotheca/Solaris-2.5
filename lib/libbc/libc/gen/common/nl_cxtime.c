/*	@(#)nl_cxtime.c 1.2 90/03/30 SMI*/


#include <stdio.h>
#include <time.h>

#define TBUFSIZE 128
char _tbuf[TBUFSIZE];

char *
nl_cxtime(clk, fmt)
	struct tm *clk;
	char *fmt;
{
	char *nl_ascxtime();
	return (nl_ascxtime(localtime(clk), fmt));
}

char *
nl_ascxtime(tmptr, fmt) 
	struct tm *tmptr;
	char *fmt;
{
	return (strftime (_tbuf, TBUFSIZE, fmt ? fmt : "%H:%M:%S", tmptr) ?
			 _tbuf : asctime(tmptr));
}
