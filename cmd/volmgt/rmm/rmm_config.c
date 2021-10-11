/*
 * Copyright (c) 1992 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)rmm_config.c	1.11	94/11/22 SMI"


#include	<stdio.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<string.h>
#include	<errno.h>
#include	<locale.h>
#include	<libintl.h>
#include	<rmmount.h>
#include	<libgen.h>
#include	<sys/types.h>
#include	<rpc/types.h>
#include	<sys/param.h>
#include	<sys/stat.h>

#include	"rmm_int.h"


static void	conf_ident(int, char **, u_int);
static void	conf_action(int, char **, u_int);
#ifdef	NOT_USED
static void	conf_mount(int, char **, u_int);
#endif
static void	conf_share(int, char **, u_int);


#define	IDENT_MEDARG	3
#define	FS_IDENT_PATH	"/usr/lib/fs"


static struct cmds {
	char	*name;
	void	(*func)(int, char **, u_int);
} cmd_list[] = {
	{ "ident", conf_ident },
	{ "action", conf_action },
#ifdef	NOT_USED
	{ "mount", conf_mount },
#endif
	{ "share", conf_share },
	{ 0, 0}
};


static struct mount_args	*alloc_ma(char *symname, int ln);
static void			share_args(struct mount_args *, int,
				    char **, int *, int);

#ifdef	NOT_USED

static void			cache_args(struct mount_args *, int,
				    char **, int *, int);

struct arg_names {
	char	*an_name;
	u_int	an_flag;
	void	(*an_argproc)(struct mount_args *, int, char **, int *, int);
};

static arg_names  arg_names[] = {
	{ "cache", 	MA_CACHE, 	cache_args },
	{ "suid", 	MA_SUID, 	0 },
	{ "share", 	MA_SHARE, 	share_args },
	{ "auto", 	MA_AUTO, 	0 },
	{ "netwide", 	MA_NETWIDE,	0 },
	{ "readonly", 	MA_READONLY,	0 },
	{ "ro", 	MA_READONLY,	0 },
	{ 0, 0, 0 },
};

#endif	/* NOT_USED */


struct mount_args	**mount_args = NULL;
static int		mount_arg_index = 0;	/* mount arg index */


void
config_read()
{
	extern void	makeargv(int *, char **, char *);
	struct	cmds	*cmd;
	FILE		*cfp;
	char		buf[BUFSIZ];
	char		*wholeline = 0;
	char		*av[MAX_ARGC];
	int		ac;
	int		found;
	u_int		lineno = 0;
	size_t		len;
	size_t		linelen = 0;



	if ((cfp = fopen(rmm_config, "r")) == NULL) {
		(void) fprintf(stderr, gettext("%s(%d): %s; %s\n"),
		    prog_name, prog_pid, rmm_config, strerror(errno));
		exit(0);
	}

	while (fgets(buf, BUFSIZ, cfp) != NULL) {

		lineno++;

		/* skip comment lines (starting with #) and blanks */
		if ((buf[0] == '#') || (buf[0] == '\n')) {
			continue;
		}

		len = strlen(buf);

		if (buf[len-2] == '\\') {
			if (wholeline == NULL) {
				buf[len-2] = NULLC;
				wholeline = strdup(buf);
				linelen = len-2;
				continue;
			} else {
				buf[len-2] = NULLC;
				len -= 2;
				wholeline = (char *)realloc(wholeline,
				    linelen+len+1);
				(void) strcpy(&wholeline[linelen], buf);
				linelen += len;
				continue;
			}
		} else {
			if (wholeline == NULL) {
				/* just a one liner */
				wholeline = buf;
			} else {
				wholeline = (char *)realloc(wholeline,
				    linelen+len+1);
				(void) strcpy(&wholeline[linelen], buf);
				linelen += len;
			}
		}

		/* make a nice argc, argv thing for the commands */
		makeargv(&ac, av, wholeline);

		found = 0;
		for (cmd = cmd_list; cmd->name; cmd++) {
			if (strcmp(cmd->name, av[0]) == 0) {
				(*cmd->func)(ac, av, lineno);
				found++;
			}
		}
		if (!found) {
			(void) fprintf(stderr, gettext(
			    "%s(%d): %s; unknown directive %s, line %d\n"),
			    prog_name, prog_pid, rmm_config, av[0], lineno);
		}
		if (wholeline != buf) {
			free(wholeline);
		}
		wholeline = NULL;
		linelen = 0;
	}
	(void) fclose(cfp);
}


/*
 * argv[0] = "action"
 * argv[1] = <optional_flag>
 * argv[next] = <media>
 * argv[next] = <dso>
 * [argv[next] = <action_arg[N]>]
 */
static void
conf_action(int argc, char **argv, u_int ln)
{
	int		nextarg;
	static int	ali;
	int		i;
	int		j;


	if (argc < 3) {
		(void) fprintf(stderr,
		    gettext("%s(%d): %s line %d: insufficient args\n"),
		    prog_name, prog_pid, rmm_config, ln);
		return;
	}

	if (action_list == NULL) {
		action_list = (struct action_list **)calloc(MAX_ACTIONS,
		    sizeof (struct action_list *));
		ali = 0;
	}

	if (ali == MAX_ACTIONS) {
		(void) fprintf(stderr, gettext(
		    "%s(%d): %s line %d: maximum actions (%d) exceeded\n"),
		    prog_name, prog_pid, rmm_config, ln, MAX_ACTIONS);
		return;
	}

	action_list[ali] = (struct action_list *)calloc(1,
	    sizeof (struct action_list));

	nextarg = 1;
	action_list[ali]->a_flag = 0;

	/*
	 * or in the bits for the flags.
	 */
	if (strcmp(argv[1], "-premount") == 0) {
		nextarg++;
		action_list[ali]->a_flag |= A_PREMOUNT;
	}

	action_list[ali]->a_media = strdup(argv[nextarg++]);
	/*
	 * Here, we just remember the name.  We won't actually
	 * load in the dso until we're sure that we need to
	 * call the function.
	 */
	action_list[ali]->a_dsoname = strdup(argv[nextarg++]);

	action_list[ali]->a_argc = argc - nextarg+2;
	action_list[ali]->a_argv = (char **)calloc(action_list[ali]->a_argc,
	    sizeof (char *));

	action_list[ali]->a_argv[0] = action_list[ali]->a_dsoname;
	for (i = nextarg, j = 1; i < argc; i++, j++) {
		action_list[ali]->a_argv[j] = strdup(argv[i]);
	}

	ali++;	/* next one... */
}


/*
 * argv[0] = "ident"
 * argv[1] = <fstype>
 * argv[2] = <dsoname>
 * argv[3] = <media>
 * [argv[n] = <media>]
 */
static void
conf_ident(int argc, char **argv, u_int ln)
{
	static int	ili;
	int		i;
	int		j;
	char		namebuf[MAXNAMELEN+1];


	if (argc < 3) {
		(void) fprintf(stderr,
		    gettext("%s(%d): %s line %d: insufficient args\n"),
		    prog_name, prog_pid, rmm_config, ln);
		return;
	}

	if (ident_list == NULL) {
		ident_list = (struct ident_list **)calloc(MAX_IDENTS,
		    sizeof (struct ident_list *));
		ili = 0;
	}

	if (ili == MAX_IDENTS) {
		(void) fprintf(stderr, gettext(
		    "%s(%d): line %d: %s maximum idents (%d) exceeded\n"),
		    prog_name, prog_pid, ln, rmm_config, MAX_IDENTS);
		return;
	}

	ident_list[ili] = (struct ident_list *)calloc(1,
	    sizeof (struct ident_list));
	ident_list[ili]->i_type = strdup(argv[1]);
	(void) sprintf(namebuf, "%s/%s/%s", FS_IDENT_PATH, argv[1], argv[2]);
	ident_list[ili]->i_dsoname = strdup(namebuf);
	ident_list[ili]->i_media = (char **)calloc(argc - IDENT_MEDARG+1,
	    sizeof (char *));

	for (i = IDENT_MEDARG, j = 0; i < argc; i++, j++) {
		ident_list[ili]->i_media[j] = strdup(argv[i]);
	}

	ili++;
}


#ifdef	NOT_USED

/*
 * argv[0] = "mount"
 * argv[1] = <symdev>
 * argv[2] = <option>
 */
void
conf_mount(int argc, char **argv, u_int ln)
{
	int			i;
	int			j;
	struct mount_args	*ma;
	u_int			oldflags;


	if (argc < 3) {
		(void) fprintf(stderr,
		    gettext("%s(%d): %s line %d: insufficient args\n"),
		    prog_name, prog_pid, rmm_config, ln);
		return;
	}

	if ((ma = alloc_ma(argv[1], ln)) == NULL) {
		return;
	}

	for (i = 2; i < argc; i++) {

		oldflags = ma->ma_flags;

		for (j = 0; arg_names[j].an_name; j++) {

			if (strncmp(argv[i], arg_names[j].an_name,
			    strlen(arg_names[j].an_name)) == 0) {

				ma->ma_flags |= arg_names[j].an_flag;

				if (arg_names[j].an_argproc != NULL) {
					(*arg_names[j].an_argproc)(ma,
					    argc, argv, &i, ln);
				}
				break;
			}
		}

		if (ma->ma_flags == oldflags) {
			(void) fprintf(stderr, gettext(
			    "%s(%d): line %d: %s unknown option '%s'\n"),
			    prog_name, prog_pid, ln, rmm_config, argv[i]);

		}
	}
}

#endif	/* NOT_USED */


static void
conf_share(int argc, char **argv, u_int ln)
{
	int			i;
	struct mount_args	*ma;


	if (argc < 2) {
		(void) fprintf(stderr,
		    gettext("%s(%d): %s line %d: insufficient args\n"),
		    prog_name, prog_pid, rmm_config, ln);
		return;
	}

	if ((ma = alloc_ma(argv[1], ln)) == NULL) {
		return;
	}

	/*
	 * this is a bit of a wack to make a separate share command work
	 * as opposed to implement it as a mount "option".  The mount
	 * option stuff is currently stuck in psarc.
	 */
	ma->ma_flags |= MA_SHARE;
	i = 1;
	share_args(ma, argc, argv, &i, ln);
}


#ifdef	NOT_USED

/*ARGSUSED*/
static void
cache_args(struct mount_args *ma, int ac, char **av, int *index, int ln)
{
	char	buf[BUFSIZ];
	int	i;


	/*
	 * start the buffer off right.
	 */
	buf[0] = NULLC;

	for (i = (*index)+1; i < ac; ) {
		if (av[i][0] != '-') {
			goto out;
		}
		switch (av[i][1]) {
		case 'o':
			(void) strcat(buf, av[i+1]);
			i += 2;
			break;
		default:
			goto out;
		}

	}
out:
	*index = i - 1;
	ma->ma_cacheflags = strdup(buf);
}

#endif	/* NOT_USED */


/*ARGSUSED*/
static void
share_args(struct mount_args *ma, int ac, char **av, int *index, int ln)
{
	char	buf[BUFSIZ];
	int	i;


	/*
	 * start the buffer off right.
	 */
	buf[0] = NULLC;

	for (i = (*index)+1; i < ac; ) {
		if (av[i][0] != '-') {
			goto out;
		}
		switch (av[i][1]) {
		case 'F':
			(void) strcat(buf, " -F ");
			(void) strcat(buf, av[i+1]);
			i += 2;
			break;
		case 'o':
			(void) strcat(buf, " -o ");
			(void) strcat(buf, av[i+1]);
			i += 2;
			break;
		case 'd':
			strcat(buf, " -d ");
			strcat(buf, av[i+1]);
			i += 2;
			break;
		default:
			goto out;
		}

	}
out:
	*index = i - 1;
	ma->ma_shareflags = strdup(buf);
}


static struct mount_args *
alloc_ma(char *symname, int ln)
{
	char			*re_symname;
	struct mount_args	*ma;
	int			i;


	if (mount_args == NULL) {
		mount_args = (struct mount_args **)calloc(MAX_MOUNTS,
		    sizeof (struct mount_args *));
		mount_arg_index = 0;
	}

	if (mount_arg_index == MAX_MOUNTS) {
		(void) fprintf(stderr, gettext(
		    "%s(%d): line %d: %s maximum mounts (%d) exceeded\n"),
		    prog_name, prog_pid, ln, rmm_config, MAX_MOUNTS);
		return (NULL);
	}
	/*
	 * See if we already have a mount_args for this symbolic name.
	 */
	re_symname = sh_to_regex(symname);
	for (ma = NULL, i = 0; i < mount_arg_index; i++) {
		if (strcmp(re_symname, mount_args[i]->ma_namere) == 0) {
			ma = mount_args[i];
			break;
		}
	}

	/*
	 * If we don't, allocate one.
	 */
	if (ma == NULL) {
		mount_args[mount_arg_index] = (struct mount_args *)calloc(1,
		    sizeof (struct mount_args));
		ma = mount_args[mount_arg_index];
		/* convert to a useful regular expression */
		ma->ma_namere = re_symname;
		ma->ma_namerecmp = regcmp(ma->ma_namere, 0);
		mount_arg_index++;
	}

	return (ma);
}
