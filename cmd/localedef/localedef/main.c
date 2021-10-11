/*
 * Copyright (c) 1993, by Sun Microsystems, Inc.
 */
#ident	"@(#)main.c	1.27	95/08/21 SMI"

/*
 *  Locale Definition File Controller, main routine
 *	localdef [-c] [-B] [-l run_locale] [-f charmap]
 *		[-i sourcefile] localename
 */
#include "../head/_localedef.h"
#include "../head/charmap.h"
#include "y.tab.h"
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <sys/wait.h>

#define	OPTSTRING	":cveF:f:i:Bl:"
#define	TMP_FNAME	"/tmp/_localedef_XXXXXX"
#define	TMP_DIR		"/tmp/_localedef_dir_XXXXXX"
#define	TMP_BUF		1024*2
#define	DEF_CHARMAP	"/usr/lib/localedef/charmap/Portable.cmap"
#define	LONG_DEFAULT	"_LOCALEDEF_LONG_DEFAULT_"

#define	CMD_CHARMAP	"/usr/lib/localedef/xsh4_charmap"
#define	CMD_CHRTBL	"/usr/lib/localedef/xsh4_chrtbl"
#define	CMD_COLLATE	"/usr/lib/localedef/xsh4_collate"
#define	CMD_TIME	"/usr/lib/localedef/xsh4_time"
#define	CMD_NUMERIC	"/usr/lib/localedef/xsh4_numeric"
#define	CMD_MONETARY	"/usr/lib/localedef/xsh4_montbl"
#define	CMD_MESSAGE	"/usr/lib/localedef/xsh4_message"

#define	DIR_LOCALEDEF	"LOCALEDEF"
#define	C_CHARMAP	"xsh4_charmap"
#define	C_CHRTBL	"xsh4_chrtbl"
#define	C_COLLATE	"xsh4_collate"
#define	C_TIME		"xsh4_time"
#define	C_NUMERIC	"xsh4_numeric"
#define	C_MONETARY	"xsh4_montbl"
#define	C_MESSAGE	"xsh4_message"

int	lineno = 0;
char	*program;
char	*input_fname;
char	*localename;
char	*olocalename;
char 	*runtime_locale = "";
char	charmap_fname[TMP_BUF];
char	charobj_fname[TMP_BUF];
extern  int errno;
static	char *tmp_dir;
static	char *target_dir;
static  int move_files = 0;

int		lc_ctype = 0;
int		lc_collate = 0;
int		lc_time = 0;
int		lc_numeric = 0;
int		lc_monetary = 0;
int		lc_message = 0;

int		i_flag = 0;		/* -i input_fname */
int		f_flag = 0;		/* -f charmap */
int		c_flag = 0;		/* -c option */
int		e_flag = 0;		/* -e option */
int		v_flag = 0;		/* -v option */
int		lf_flag = 0;		/* -F charobj */
int		B_flag = 0;		/* -B */
int		l_flag = 0;		/* -l flag */
int		errorcnt = 0;

char	*cmd_charmap = CMD_CHARMAP;
char	*cmd_chrtbl = CMD_CHRTBL;
char	*cmd_collate = CMD_COLLATE;
char	*cmd_time = CMD_TIME;
char	*cmd_numeric = CMD_NUMERIC;
char	*cmd_monetary = CMD_MONETARY;
char	*cmd_message = CMD_MESSAGE;

FILE	*input_file;

static int		postprocess(int);
static char		*get_stdin();
static void		setupcmds();
static void		usage();
static char		*setup_locale(char *);
static int		move_locales(char *, char *);
static void		remove_tmp_dir(char *);
static void		skip_to(FILE *, char *);

extern int		yyparse(void);

#ifdef VSC_DEBUG
FILE *debug_fp;
#endif

int
main(int argc, char **argv)
{
	int		c;
	int		ret;

	extern int	optind, opterr, optopt;
	extern char	*optarg;

	opterr = 0;
	program = argv[0];

	(void) setlocale(LC_ALL, "");
#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN	"SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	/*
	 * Argument handling.
	 */
	while ((c = getopt(argc, argv, OPTSTRING)) != -1) {
		switch (c) {
		case 'B':
			B_flag++;
			break;
		case 'l':
			l_flag++;
			runtime_locale = optarg;
			break;
		case 'c':
			c_flag++;
			break;
		case 'e':
			e_flag++;
			break;
		case 'v':
			v_flag++;
			break;
		case 'F':
			if (f_flag) {
				(void) fprintf(stderr, gettext(
					"-F and -f are mutually exclusive.\n"));
				exit(300);
			} else {
				lf_flag++;
				(void) strcpy(charobj_fname, optarg);
			}
			break;
		case 'f':
			if (lf_flag) {
				(void) fprintf(stderr, gettext(
					"-F and -f are mutually exclusive.\n"));
				exit(301);
			} else {
				f_flag++;
				(void) strcpy(charmap_fname, optarg);
			}
			break;
		case 'i':
			i_flag++;
			input_fname = optarg;
			break;
		case ':':
			(void) fprintf(stderr, gettext(
				"%s: option '%c' requires an argument.\n"),
				program, optopt);
			errorcnt++;
			break;
		case '?':
			(void) fprintf(stderr, gettext(
				"%s: illegal option '%c'.\n"),
				program, optopt);
			errorcnt++;
			break;
		}
	}

	if (errorcnt != 0 || optind == argc) {
		usage();
	}
	olocalename = localename = argv[optind];

	/*
	 * Setup locale name
	 */
	localename = setup_locale(localename);
	if (localename == NULL)
		exit(400);
#ifdef VSC_DEBUG
	{
		unlink("/tmp/VSC_DEBUG");
		debug_fp = fopen("/tmp/VSC_DEBUG", "a");
		if (debug_fp == NULL) {
			(void) fprintf(stderr, "Could not open debug_fp\n");
		} else {
			(void) fprintf(debug_fp,
			"olocalename = '%s'\n", olocalename);
			(void) fprintf(debug_fp,
			"localename = '%s'\n", localename);
		}
	}
#endif

	/*
	 * Initialise
	 */
	if (i_flag == 0) {
		input_fname = get_stdin();
	}

	if ((input_file = fopen(input_fname, "r")) == NULL) {
		(void) fprintf(stderr, gettext("%s: Can't open %s.\n"),
			program, input_fname);
		exit(500);
	}

	if (e_flag != 0) {
		setupcmds();
	}

	if (lf_flag) {
		if (access(charobj_fname, R_OK) != 0) {
			(void) fprintf(stderr, gettext(
				"%s: Can't read %s.\n"),
				program, charobj_fname);
			if (!i_flag) {
				(void) unlink(input_fname);
			}
			exit(501);
		}
	} else if (f_flag) {
		char	*command;
		int	stat;
		int	com_len;

		if (access(charmap_fname, R_OK) != 0) {
			(void) fprintf(stderr, gettext(
				"%s: Can't read %s.\n"),
				program, charmap_fname);
			if (!i_flag) {
				(void) unlink(input_fname);
			}
			exit(502);
		}
		(void) strcpy(charobj_fname, localename);
		(void) strcat(charobj_fname, CHARMAP_TRAIL);

		com_len = strlen(cmd_charmap)+strlen(charmap_fname)+
			strlen(charobj_fname)+128;
		command = malloc(com_len + 1);
		if (command == NULL) {
			(void) fprintf(stderr, gettext(
			"localedef: Could not allocate memory.\n"));
			exit(503);
		}
		(void) sprintf(command, "%s -i %s -o %s",
			cmd_charmap, charmap_fname, charobj_fname);
#ifdef DDEBUG
		(void) fprintf(stderr, "\tDEBUG: COMMAND = '%s'\n", command);
#endif
		stat = system(command);
		free(command);

		if (stat != 0) {
			(void) fprintf(stderr, gettext(
				"localedef: charmap was not generated.\n"));
			if (!i_flag) {
				(void) unlink(input_fname);
			}
			exit(504);
		}
	} else {
		/*
		 * Use Portable character mapping file.
		 */
		(void) strcpy(charobj_fname, DEF_CHARMAP);
		lf_flag++;
		if (access(charobj_fname, R_OK) != 0) {
			(void) fprintf(stderr, gettext(
				"%s: Can't read %s.\n"),
				program, charobj_fname);
			if (!i_flag) {
				(void) unlink(input_fname);
			}
			exit(505);
		}
	}

	/*
	 * Skip forward to the LC_CTYPE portion of the localedef source file
	 * so it gets processed first.
	 */

	skip_to(input_file, "LC_CTYPE");

	/*
	 * Parse Input File
	 */
	ret = yyparse();

	/*
	 * Post processing
	 */
	if (i_flag == 0) {
		(void) unlink(input_fname);
	}
	c = postprocess(ret);
	if (c == ERROR)
		exit(600);

	/*
	 * Standard says the error code has to be larger
	 * 3 for these cases.
	 */
	if (ret != 0)
		ret = 1000 + ret;
	exit(ret);
}

/*
 * usage function
 */
static void
usage()
{
	(void) fprintf(stderr, gettext(
		"usage: %s [-e] [-c] [-f charmap | -F charmap_obj]"
		" [-i sourcefile] localename\n"),
		program);
	exit(300);
}


static char *
get_stdin()
{
	FILE	*fp;
	char	*name;
	char	buf[1024];

	name = (char *)mktemp(TMP_FNAME);

	if (name == NULL) {
		(void) fprintf(stderr, gettext(
			"localedef: Could not create temporary file.\n"));
		exit(1100);
	}

	fp = fopen(name, "wb");

	if (fp == NULL) {
		(void) fprintf(stderr, gettext(
			"localedef: Could not create temporary file.\n"));
		exit(1100);
	}

	while (gets(buf) != NULL) {
		(void) fprintf(fp, "%s\n", buf);
	}

	(void) fclose(fp);
	return (name);
}

static void
setupcmds()
{
	char	*dir;
	char	buf[TMP_BUF + 1];

	if ((dir = getenv(DIR_LOCALEDEF)) == NULL) {
		return;
	}

	(void) strcpy(buf, dir);
	(void) strcat(buf, "/");
	(void) strcat(buf, C_CHARMAP);
	cmd_charmap = strdup(buf);
	if (cmd_charmap == NULL) {
		goto error_out;
		/* NOTREACHED */
	}

	(void) strcpy(buf, dir);
	(void) strcat(buf, "/");
	(void) strcat(buf, C_CHRTBL);
	cmd_chrtbl = strdup(buf);
	if (cmd_chrtbl == NULL) {
		goto error_out;
		/* NOTREACHED */
	}

	(void) strcpy(buf, dir);
	(void) strcat(buf, "/");
	(void) strcat(buf, C_COLLATE);
	cmd_collate = strdup(buf);
	if (cmd_collate == NULL) {
		goto error_out;
		/* NOTREACHED */
	}

	(void) strcpy(buf, dir);
	(void) strcat(buf, "/");
	(void) strcat(buf, C_TIME);
	cmd_time = strdup(buf);
	if (cmd_time == NULL) {
		goto error_out;
		/* NOTREACHED */
	}

	(void) strcpy(buf, dir);
	(void) strcat(buf, "/");
	(void) strcat(buf, C_NUMERIC);
	cmd_numeric = strdup(buf);
	if (cmd_numeric == NULL) {
		goto error_out;
		/* NOTREACHED */
	}

	(void) strcpy(buf, dir);
	(void) strcat(buf, "/");
	(void) strcat(buf, C_MONETARY);
	cmd_monetary = strdup(buf);
	if (cmd_monetary == NULL) {
		goto error_out;
		/* NOTREACHED */
	}

	(void) strcpy(buf, dir);
	(void) strcat(buf, "/");
	(void) strcat(buf, C_MESSAGE);
	cmd_message = strdup(buf);
	if (cmd_message == NULL) {
		goto error_out;
		/* NOTREACHED */
	}

	return;

error_out:
	(void) fprintf(stderr, gettext(
		"localedef: could not allocate memory.\n"));
	exit(1000);
}

/*
 * execute
 */
int
execute(int type, char *in_put, int skip_lines, char *out)
{
	int	stat;
	int 	com_len;
	char	*command;
	static void	create_command();

	com_len = strlen(in_put) + strlen(out) + 128;
	command = (char *)malloc(com_len+1);
	if (command == NULL) {
		(void) fprintf(stderr, gettext(
		"localedef: Could not allocate memory.\n"));
		return (-1);
	}
	create_command(command, type, in_put, skip_lines, out);

#ifdef DDEBUG
	(void) fprintf(stderr, "\tDEBUG: COMMAND = '%s'\n", command);
#endif
	stat = system(command);
	if (stat != -1)
		stat = (WIFEXITED(stat)) ? WEXITSTATUS(stat) : -1;
	free(command);

#ifdef DEBUG
	(void) fprintf(stderr,
	"\tDEBUG: status returned from COMMAND is %d\n", stat);
#endif
	return (stat);
}

static void
create_command(char *s, int type, char *input, int skip, char *outf)
{
#define	OPTIONAL_OPTIONS	128
	char	*com;
	char	out[TMP_BUF];
	char	options[OPTIONAL_OPTIONS+1];

	(void) strcpy(out, outf);
	(void) strcpy(options, " ");
	switch (type) {
	case CHARMAP_LINE:
		com = cmd_charmap;
		(void) strcat(out, CHARMAP_TRAIL);
		break;
	case LC_CTYPE_LINE:
		com =  cmd_chrtbl;
		(void) strcat(out, CHRTBL_TRAIL);
		break;
	case LC_COLLATE_LINE:
		com = cmd_collate;
		if (B_flag != 0)
			(void) strcat(options, "-B ");
		if (l_flag != 0) {
			(void) strcat(options, "-l ");
			(void) strcat(options, runtime_locale);
		}
		(void) strcat(out, COLLATE_TRAIL);
		break;
	case LC_TIME_LINE:
		com = cmd_time;
		(void) strcat(out, TIME_TRAIL);
		break;
	case LC_NUMERIC_LINE:
		com = cmd_numeric;
		(void) strcat(out, NUMERIC_TRAIL);
		break;
	case LC_MONETARY_LINE:
		com = cmd_monetary;
		(void) strcat(out, MONETARY_TRAIL);
		break;
	case LC_MESSAGE_LINE:
		com = cmd_message;
		(void) strcat(out, MESSAGE_TRAIL);
		break;
	default:
		(void) fprintf(stderr,
			"%s: internal error. create_command\n",
			program);
		exit(1000);
	}
	if (type == CHARMAP_LINE || (f_flag == 0 && lf_flag == 0)) {
		(void) sprintf(s, "%s %s -i %s -s %d -o %s",
			com, options, input, skip - 1, out);
	} else {
		(void) sprintf(s, "%s %s -f %s -i %s -s %d -o %s",
			com, options, charobj_fname, input, skip - 1, out);
	}
}

/*
 * set up locale name
 */
static char *
setup_locale(char *name)
{
	char *p1;
	char *p2;
	int ret_val;
	int dir_cnt = 0;
	struct stat sbuf;
	char leaf_name[TMP_BUF];

#define	MAX_LOCALE_NAME	1000
	/*
	 * If the length of name is
	 * small enough to generate locale file names,
	 * then just return.
	 *
	 * 1000 chosen so 23 bytes can be used for extentions.
	 */
	if ((int)strlen(name) < MAX_LOCALE_NAME)
		return (name);

	/*
	 * Get the leaf locale name.
	 */
	move_files++;
	p2 = p1 = name;
	while (*p1 != 0) {
		if (*p1 == '/') {
			dir_cnt++;
			p2 = p1;
		}
		p1++;
	}
	if (dir_cnt == 0 && ((int)strlen(name) > MAX_LOCALE_NAME)) {
		/*
		 * This was a long dir/file name.
		 * Give up and use fixed name.
		 */
		p1 = strdup(LONG_DEFAULT);
		if (p1 == NULL) {
			(void) fprintf(stderr, gettext(
			"localedef: could not allocate memory.\n"));
			return (NULL);
		}
		return (p1);
	}
	/*
	 * Get the leaf name into leaf_name[]
	 */
	p1 = leaf_name;
	if (dir_cnt != 0) {
		char *last;
		/*
		 * there are directories in between.
		 */
		target_dir = strdup(name);
		if (target_dir == NULL) {
			(void) fprintf(stderr, gettext(
			"localedef: could not allocate memory.\n"));
			return (NULL);
		}
		last = target_dir + strlen(target_dir);
		while (*last != '/')
			last--;
		*(++last) = 0;
		++p2;
	} else {
		/*
		 * current directory
		 */
		target_dir = getcwd(NULL, TMP_BUF);
		if (target_dir == NULL) {
			(void) fprintf(stderr, gettext(
		"localedef: Could not get the current working directory.\n"));
			return (NULL);
		}
	}

	/*
	 * Check the validity of 'target_dir'
	 */
	if (stat(target_dir, &sbuf) == -1) {
		/*
		 * target_dir does not exist ?
		 */
		(void) fprintf(stderr, gettext(
		"localedef: %s is not a directory.\n"),
		target_dir);
		return (NULL);
	} else {
		if ((sbuf.st_mode & S_IFMT) != S_IFDIR) {
			(void) fprintf(stderr, gettext(
			"localedef: %s is not a directory.\n"),
			target_dir);
			return (NULL);
		}
	}

	while (*p2 != 0)
		*p1++ = *p2++;
	*p1 = 0;
	if (strlen(leaf_name) == 0) {
		/*
		 * This means that the localename specified
		 * ends with '/'. This implementation rejects it.
		 */
		(void) fprintf(stderr, gettext(
		"localedef: illegal name '%s' specified.\n"),
		name);
		return (NULL);
	}

	/*
	 * create a temporary directory
	 */
	p1 = (char *)mktemp(TMP_DIR);
	if (p1 == NULL) {
		(void) fprintf(stderr, gettext(
			"localedef: Could not create temporary file.\n"));
		exit(1100);
	}
	tmp_dir = strdup(p1);
	if (tmp_dir == NULL) {
		(void) fprintf(stderr, gettext(
		"localedef: could not allocate memory.\n"));
		return (NULL);
	}

	/*
	 * create a command line
	 */
	p1 = malloc(strlen("/bin/mkdir ") + strlen(tmp_dir) + 1);
	if (p1 == NULL) {
		(void) fprintf(stderr, gettext(
		"localedef: could not allocate memory.\n"));
		return (NULL);
	}
	(void) sprintf(p1, "/bin/mkdir %s", tmp_dir);
	ret_val = system(p1);
	if (ret_val != 0) {
		(void) fprintf(stderr, gettext(
		"localedef: could not create temporary directory.\n"));
		return (NULL);
	}
	free(p1);

	/*
	 * create locale name
	 */
	p1 = malloc(strlen(tmp_dir) + strlen("/") + strlen(leaf_name) + 1);
	if (p1 == NULL) {
		(void) fprintf(stderr, gettext(
		"localedef: could not allocate memory.\n"));
		return (NULL);
	}
	(void) strcpy(p1, tmp_dir);
	(void) strcat(p1, "/");
	(void) strcat(p1, leaf_name);
	return (p1);
}


static int
postprocess(int val)
{
	if (val != 0) {
		(void) fprintf(stderr, gettext(
			"localedef: there were categories "
			"which were not generated.\n"));
	} else {
		int fd;
		char buf[128];
		int ret;

		/*
		 * Was it a long locale name ?
		 */
		if (move_files != 0) {
			char *cwd;
			int i;

			cwd = getcwd(NULL, TMP_BUF+1);
			if (cwd == NULL)
				return (ERROR);
			i = chdir(target_dir);
			if (i == -1)
				return (ERROR);
			ret = move_locales(".", tmp_dir);
#ifdef VSC_DEBUG
fprintf(debug_fp, "move_locales() returned %d\n", ret);
fprintf(debug_fp, "len = %d, target = '%s'\n", strlen(target_dir), target_dir);
fprintf(debug_fp, "len = %d, tmp_dir = '%s'\n", strlen(tmp_dir), tmp_dir);
#endif

			remove_tmp_dir(tmp_dir);
			if (ret != 0)
				return (ERROR);
			(void) chdir(cwd);
			free(cwd);
		}


		fd = creat(olocalename, 0777);
		if (fd == -1)
			return (ERROR);
		(void) strcpy(buf, "The locale files for the following "
			"categories were generated:\n");
		(void) write(fd, buf, strlen(buf));
		if (lc_ctype != 0)
			(void) write(fd, "\tLC_CTYPE\n",
				strlen("\tLC_CTYPE\n"));
		if (lc_collate != 0)
			(void) write(fd, "\tLC_COLLATE\n",
				strlen("\tLC_COLLATE\n"));
		if (lc_time != 0)
			(void) write(fd, "\tLC_TIME\n",
				strlen("\tLC_TIME\n"));
		if (lc_numeric != 0)
			(void) write(fd, "\tLC_NUMERIC\n",
				strlen("\tLC_NUMERIC\n"));
		if (lc_monetary != 0)
			(void) write(fd, "\tLC_MONETARY\n",
				strlen("\tLC_MONETARY\n"));
		if (lc_message != 0)
			(void) write(fd, "\tLC_MESSAGE\n",
				strlen("\tLC_MESSAGE\n"));
		(void) close(fd);
	}
	return (0);
}

static int
move_locales(char *t, char *f)
{
#define	MAX_NUM_FILES	20
	int num_files = 0;
	DIR *dir;
	struct dirent *dp;
	char *files[MAX_NUM_FILES];
	char *p;
	char *com;
	int len = 0;
	int i;

	/*
	 * Read the temporary directory.
	 */
	dir = opendir(f);
	if (dir == 0) {
		(void) fprintf(stderr, gettext(
		"localedef: internal error. opendir()\n"));
		return (-1);
	}
	for (;;) {
		if (num_files == MAX_NUM_FILES)
			continue;
		dp = readdir(dir);
		if (dp == NULL)
			break;
		if ((strcmp(dp->d_name, ".") == 0) ||
			(strcmp(dp->d_name, "..") == 0))
			continue;
		p = malloc(strlen(tmp_dir) +
			strlen(dp->d_name) + 1 + 1);
		if (p == NULL) {
			(void) fprintf(stderr, gettext(
			"localedef: malloc error\n"));
			return (-2);
		}
		(void) strcpy(p, tmp_dir);
		(void) strcat(p, "/");
		(void) strcat(p, dp->d_name);
		files[num_files++] = p;
	}
	/*
	 * generate command line
	 */
	for (i = 0; i < num_files; i++)
		len += strlen(files[i]);
	len += strlen("/bin/cp");
	len += MAX_NUM_FILES + 2;
	len += strlen(t);
	com = malloc(len);
	if (com == NULL) {
		(void) fprintf(stderr, gettext(
		"localedef: malloc error\n"));
		return (-3);
	}
	(void) strcpy(com, "/bin/cp ");
	for (i = 0; i < num_files; i++) {
		(void) strcat(com, files[i]);
		(void) strcat(com, " ");
	}
	(void) strcat(com, t);
	i = system(com);
	if (i != 0) {
		(void) fprintf(stderr, gettext(
		"localedef: could not copy files from tmp directory.\n"));
		(void) closedir(dir);
		return (-4);
	}
	free(com);
	for (i = 0; i < num_files; i++)
		free(files[i]);
	(void) closedir(dir);
	return (0);
}

static void
remove_tmp_dir(char *t)
{
	char com[TMP_BUF];
	(void) sprintf(com, "/bin/rm -fr %s", t);
	(void) system(com);
}

/*
 * skip forward in the localedef file to the indicated, s, section
 */

static void
skip_to(FILE *fp, char *s)
{
	int	string_length;
	char	buf[LINE_MAX];
	char	*bufptr;


	string_length = strlen(s);

	while (fgets(buf, LINE_MAX, fp) != NULL) {
		lineno++;
		for (bufptr = buf; *bufptr != NULL; bufptr++)
			if (!isspace(*bufptr))
				break;
		if (strncmp(bufptr, s, string_length) == 0) {
			if (fseek(fp, (long) -(strlen(buf)), SEEK_CUR) == -1) {
				fprintf(stderr, gettext("localedef: Internal \
error.  skip_to():fseek failed\n"));
				exit(506);
			} else {
				lineno--;
				return;
			}
		}
	}

	/*
	 * If you got here then you didn't find the section.
	 * That's ok.  Just continue processing.
	 */

	(void) rewind(fp);
	lineno = 0;
}
