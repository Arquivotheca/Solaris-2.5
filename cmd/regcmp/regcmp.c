/*	Copyright (c) 1988 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)regcmp.c	1.9	93/09/08 SMI"	/* SVr4.0 1.6.1.1	*/

#include <stdio.h>
#include <locale.h>
#include <libgen.h>
FILE *fopen();
FILE *iobuf;
int gotflg;
char ofile[64];
char a1[1024];
char a2[64];
int c;

main(argc,argv) char **argv;
{
	register char *name, *str, *v;
	char *bp, *cp, *sv;
	char *message;
	int j,k,cflg = 0;

	(void)setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);
	if (argc > 1 && *argv[1] == '-') {
		cflg++;
		++argv;
		argc--;
	}
	else cflg = 0;
	while(--argc) {
		++argv;
		bp = *argv;
		if ((iobuf=fopen(*argv,"r")) == NULL) {
			message = gettext("can not open ");
			write(2,message,strlen(message));
			write(2,*argv,size(*argv));
			write(2,"\n",1);
			continue;
		}
		cp = ofile;
		while(*++bp)
			if(*bp == '/') *bp = '\0';
		while(*--bp == '\0');
		while(*bp != '\0' && bp > *argv) bp--;
		while (*bp == 0)
			bp++;
		while(*cp++ = *bp++);
		cp--; *cp++ = '.';
		if(cflg) *cp++ = 'c';
		else *cp++ = 'i';
		*cp = '\0';
		close(1);
		if (creat(ofile,0644)<0)  {
			message = gettext("can not create .i file\n");
			write(2,message,strlen(message));
			exit(1);
		}
		gotflg = 0;
	   while(1) {
		str = a1;
		name = a2;
		if (!gotflg)
			while(((c=getc(iobuf)) == '\n') || (c == ' '));
		else
			gotflg = 0;
		if(c==EOF) break;
		*name++ = c;
		while(((*name++ = c = getc(iobuf)) != ' ') && (c != EOF) && (c != '\n'));
		*--name = '\0';
		while(((c=getc(iobuf)) == ' ') || (c == '\n'));
		if(c != '"') {
			if (c==EOF) {
				message = gettext("unexpected eof\n"); 
				write(2, message, strlen(message));
				exit(1);
			}
			message = gettext("missing initial quote for ");
			write (2, message, strlen(message));
			write(2,a2,size(a2));
			message = gettext(" : remainder of line ignored\n");
			write(2,message, strlen(message));
			while((c=getc(iobuf)) != '\n');
			continue;
		}
	keeponl:
		while(gotflg || (c=getc(iobuf)) != EOF) {
		gotflg = 0;
		switch(c) {
		case '"':
			break;
		case '\\':
			switch(c=getc(iobuf)) {
			case 't':
				*str++ = '\011';
				continue;
			case 'n':
				*str++ = '\012';
				continue;
			case 'r':
				*str++ = '\015';
				continue;
			case 'b':
				*str++ = '\010';
				continue;
			case '\\':
				*str++ = '\\';
				continue;
			default:
				if (c<='7' && c>='0') 
						*str++ = getnm(c);
				else *str++ = c;
				continue;
			}
		default:
			*str++ = c;
		}
		if (c=='"') break;
		}
		if (c==EOF) {
			message = gettext("unexpected eof\n");
			write(2, message, strlen(message));
			exit(1);
		}
		while(((c=getc(iobuf)) == '\n') || (c == ' '));
		if (c=='"') goto keeponl;
		else {
			gotflg++;
		}
		*str = '\0';
		if(!(sv=v=regcmp(a1,0))) {
			message = gettext("fail: ");
			write(2, message, strlen(message));
			write(2,a2,size(a2));
			write(2,"\n",1);
			continue;
		}
		printf("/* \"%s\" */\n",a1);
		printf("char %s[] = {\n",a2);
		while(__i_size > 0) {
			for(k=0;k<12;k++)
				if(__i_size-- > 0) printf("0%o,",*v++);
			printf("\n");
		}
		printf("0};\n");
		free(sv);
	   }
	   fclose(iobuf);
	}
	exit(0);
}
size(p) char *p;
{
	register i;
	register char *q;

	i = 0;
	q = p;
	while(*q++) i++;
	return(i);
}
getnm(j) char j;
{
	register int i;
	register int k;
	i = j - '0';
	k = 1;
	while( ++k < 4 && (c=getc(iobuf)) >= '0' && c <= '7') 
		i = (i*8+(c-'0'));
	if (k >= 4)
		c = getc(iobuf);
	gotflg++;
	return(i);
}
