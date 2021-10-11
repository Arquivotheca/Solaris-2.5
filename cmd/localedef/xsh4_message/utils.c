/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)utils.c	1.10	95/07/13 SMI"

#include "message.h"
#include "extern.h"
#include <libgen.h>
#include <unistd.h>

#define	TRAILER	".msg"
#define	CMD		"/bin/mkmsgs -o"

static char		out_list[T_NUM_STRINGS][MAX_BYTES + 1];
extern int		k_count[];

extern int		expand_sym_string(unsigned char *, unsigned char *);

#define	DEFAULT_YESSTR	"yes\n"
#define	DEFAULT_NOSTR	"no\n"
#define	DEFAULT_YESEXPR	"yes\n"
#define	DEFAULT_NOEXPR	"no\n"

/*
 *  Supporting routines.
 */
int
output(int fd, char *out)
{
	int	i;
	char	*out_msgfile;
	char	*cmdline;
	unsigned char	buf[MAX_BYTES + 1];

	if (k_count[T_YES_EXP] == 0) {
		(void) strcpy((char *)out_list[T_YES_EXP], DEFAULT_YESEXPR);
	}
	if (k_count[T_NO_EXP] == 0) {
		(void) strcpy((char *)out_list[T_NO_EXP], DEFAULT_NOEXPR);
	}
	if (k_count[T_YES_STR] == 0) {
		(void) strcpy((char *)out_list[T_YES_STR], DEFAULT_YESSTR);
	}
	if (k_count[T_NO_STR] == 0) {
		(void) strcpy((char *)out_list[T_NO_STR], DEFAULT_NOSTR);
	}
	for (i = 0; i < T_NUM_STRINGS; i++) {
		int	len;

		len = expand_sym_string(buf, (unsigned char *)out_list[i]);
		if (len == ERROR)
			return (-1);
		len = strlen((char *)buf);
		if (write(fd, (char *)buf, len) != len)
			return (-1);
	}
	out_msgfile = malloc(strlen(out)+strlen(TRAILER)+1);
	if (out_msgfile == NULL) {
		fprintf(stderr, gettext(
		"xsh4_message: Could not allocate memory.\n"));
		return (-1);
	}
	(void) strcpy(out_msgfile, out);
	(void) strcat(out_msgfile, TRAILER);

	cmdline = malloc(strlen(CMD)+strlen(out)+strlen(out_msgfile)+10);
	if (cmdline == NULL) {
		fprintf(stderr, gettext(
		"xsh4_message: Could not allocate memory.\n"));
		return (-1);
	}
	(void) sprintf(cmdline, "%s %s %s", CMD, out, out_msgfile);
	(void) system(cmdline);
	free(cmdline);
	free(out_msgfile);
	return (0);
}

int
set_table(struct keyword *k, encoded_val *en)
{
	int		i;
	char	*l;

	for (i = 0; i < en->length; i++) {
		out_list[k->ktype][i] = en->bytes[i];
	}
	out_list[k->ktype][i++] = '\n';
	out_list[k->ktype][i] = 0;

	if (k->ktype == T_YES_EXP || k->ktype == T_NO_EXP) {
		l = regcmp((char *)out_list[k->ktype], (char *)0);
		if (l == NULL) {
#ifdef DEBUG
			char	*x;
			x = strchr((char *)out_list[k->ktype], '\n');
			*x = 0;
			(void) printf("REGCMP ERROR for '%s'.\n",
				(char *)out_list[k->ktype]);
#endif
			errorcnt++;
			return (-1);
		}
		free(l);
	}
	return (0);
}
