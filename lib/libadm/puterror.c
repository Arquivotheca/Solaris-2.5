
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*LINTLIBRARY*/
#ident	"@(#)puterror.c	1.4	92/07/14 SMI"       /* SVr4.0 1.1 */

#include <stdio.h>
#include <string.h>

extern int	ckwidth;
extern int	ckindent;
extern int	ckquit;
extern int	puttext();
extern void	*calloc(),
		free();

#define DEFMSG	"ERROR: "
#define MS	sizeof(DEFMSG)
#define INVINP	"invalid input"

void
puterror(fp, defmesg, error)
FILE *fp;
char *defmesg, *error;
{
	char	*tmp;
	int	n;
	
	if(error == NULL) {
		/* use default message since no error was provided */
		n = (defmesg ?  strlen(defmesg) : strlen (INVINP));
		tmp = calloc(MS+n+1, sizeof(char));
		(void) strcpy(tmp, DEFMSG);
		(void) strcat(tmp, defmesg ? defmesg : INVINP); 

	} else if(defmesg != NULL) {
		n = strlen(error);
		if(error[0] == '~') {
			/* prepend default message */
			tmp = calloc(MS+n+strlen(defmesg)+2, sizeof(char));
			(void) strcpy(tmp, DEFMSG);
			(void) strcat(tmp, defmesg);
			(void) strcat(tmp, "\n");
			(void) strcat(tmp, ++error);
		} else if(n && (error[n-1] == '~')) {
			/* append default message */
			tmp = calloc(MS+n+strlen(defmesg)+2, sizeof(char));
			(void) strcpy(tmp, DEFMSG);
			(void) strcat(tmp, error);
			tmp[MS-1+n-1] = '\0';	/* first -1 'cuz sizeof(DEFMSG)
						    includes terminator... */
			(void) strcat(tmp, "\n");
			(void) strcat(tmp, defmesg);
		} else {
			tmp = calloc(MS+n+1, sizeof(char));
			(void) strcpy(tmp, DEFMSG);
			(void) strcat(tmp, error);
		}
	} else {
		n = strlen(error);
		tmp = calloc(MS+n+1, sizeof(char));
		(void) strcpy(tmp, DEFMSG);
		(void) strcat(tmp, error);
	}
	(void) puttext(fp, tmp, ckindent, ckwidth);
	(void) fputc('\n', fp);
	free(tmp);
}
