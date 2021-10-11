#pragma ident	"@(#)labeltostring.c	1.2	92/07/20 SMI"  /* c2 secure */

#include <sys/label.h> 

char *
labeltostring(part, value, verbose)
int part;
blabel_t *value;
int verbose;
{
	char *string;

	string = (char *)malloc(sizeof(char));
	strcpy(string, "");
	return (string);
}

labelfromstring(part, label_string, value)
int part;
char *label_string;
blabel_t *value;
{
	bzero(value, sizeof(value));
}
