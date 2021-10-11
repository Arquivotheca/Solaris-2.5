/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)passwd.c	1.19	93/09/28 SMI"	/* SVr4.0 1.4.3.9	*/

/*	Copyright (c) 1987, 1988 Microsoft Corporation	*/
/*	  All Rights Reserved	*/

/*	This Module contains Proprietary Information of Microsoft  */
/*	Corporation and should be treated as Confidential.	   */
/*
 * passwd is a program whose sole purpose is to manage
 * the password file, map, or table. It allows system administrator
 * to add, change and display password attributes.
 * Non privileged user can change password or display
 * password attributes which corresponds to their login name.
 */

#include <stdio.h>
#include <pwd.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <locale.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <security/ia_appl.h>
#include <rpcsvc/nis.h>

/*
 * flags indicate password attributes to be modified
 */

#define	LFLAG 0x001		/* lock user's password  */
#define	DFLAG 0x002		/* delete user's  password */
#define	MFLAG 0x004		/* set max field -- # of days passwd is valid */
#define	NFLAG 0x008		/* set min field -- # of days between */
				/* password changes */
#define	SFLAG 0x010		/* display password attributes */
#define	FFLAG 0x020		/* expire  user's password */
#define	AFLAG 0x040		/* display password attributes for all users */
#define	SAFLAG (SFLAG|AFLAG)	/* display password attributes for all users */
#define	WFLAG 0x100		/* warn user to change passwd */
#define	OFLAG 0x200		/* domain name */
#define	EFLAG 0x400		/* change shell */
#define	GFLAG 0x800		/* change gecos information */
#define	HFLAG 0x1000		/* change home directory */
#define	NONAGEFLAG	(EFLAG | GFLAG | HFLAG)
#define	AGEFLAG	(LFLAG | FFLAG | MFLAG | NFLAG | WFLAG)


/*
 * exit code
 */

#define	SUCCESS	0	/* succeeded */
#define	NOPERM	1	/* No permission */
#define	BADOPT	2	/* Invalid combination of option */
#define	FMERR	3	/* File/table manipulation error */
#define	FATAL	4	/* Old file/table can not be recovered */
#define	FBUSY	5	/* Lock file/table busy */
#define	BADSYN	6	/* Incorrect syntax */
#define	BADAGE	7	/* Aging is disabled  */

/*
 * define error messages
 */
#define	MSG_NP	"Permission denied"
#define	MSG_BS	"Invalid combination of options"
#define	MSG_FE	"Unexpected failure. Password file/table unchanged."
#define	MSG_FF	"Unexpected failure. Password file/table missing."
#define	MSG_FB	"Password file/table busy. Try again later."
#define	MSG_NV  "Invalid argument to option"
#define	MSG_AD	"Password aging is disabled"

/*
 * return code from ckarg() routine
 */
#define	FAIL 		-1

/*
 *  defind password file name
 */
#define	PASSWD 			"/etc/passwd"

#ifdef DEBUG
#define	dprintf1	printf
#else
#define	dprintf1(w, x)
#endif

extern int	optind;

static int		retval = SUCCESS;
static uid_t		uid;
static char		*prognamep;
static long		maxdate;	/* password aging information */
static int		passwd_conv();
static void		dummy_conv();
static struct ia_conv	ia_conv = {passwd_conv, passwd_conv, dummy_conv, NULL};
static void		*iah;		/* Authentication handle */
static int		repository = R_DEFAULT;
static nis_name		nisdomain = NULL;
static char		*cmd;		/* nispasswd, yppasswd or passwd */

/*
 * Function Declarations
 */
extern	int		ia_get_authtokattr();
extern	int		ia_set_authtokattr();
extern	int		ia_chauthtok();
extern	int		ia_set_item();
extern	int		ia_start();
extern	int		ia_end();
extern	void		free_resp();
extern	void		audit_passwd_init_id();
extern	void		audit_passwd_sorf();
extern	void		audit_passwd_attributes_sorf();
extern	nis_name	nis_local_directory();

static	char		*pw_attr_match();
static	void		display_attr();
static	int		get_namelist();
static	void		pw_setup_setattr();
static	char		**get_authtokattr();
static	void		passwd_exit();
static	void		rusage();
static	int		ckuid();
static	int		ckarg();

/*
 * main():
 *	The main routine will call ckarg() to parse the command line
 *	arguments and call the appropriate functions to perform the
 *	tasks specified by the arguments. It allows system
 * 	administrator to add, change and display password attributes.
 * 	Non privileged user can change password or display
 * 	password attributes which corresponds to their login name.
 */

void
main(argc, argv)
	int argc;
	char *argv[];
{

	int			flag;
	char			**namelist;
	int			num_user;
	int			i;
	char			*usrname;
	char			**getattr;
	char			*setattr[MAX_NUM_ATTR];
	struct ia_status	ia_status;

	/*
	 * Determine command invoked (nispasswd, yppasswd, or passwd)
	 */

	if (cmd = strrchr(argv[0], '/'))
		++cmd;
	else
		cmd = argv[0];

	if (strcmp(cmd, "nispasswd") == 0) {
		repository = R_NISPLUS | R_OPWCMD;
		nisdomain = nis_local_directory();
	} else if (strcmp(cmd, "yppasswd") == 0)
		repository = R_NIS | R_OPWCMD;
	else
		repository = R_DEFAULT;	/* can't determine yet */

	/* initialization for variables, set locale and textdomain  */
	i = 0;
	flag = 0;
	prognamep = argv[0];

	uid = getuid();		/* get the user id */
	(void) setlocale(LC_ALL, "");

#if !defined(TEXT_DOMAIN)	/* Should be defined by cc -D */
#define	TEXT_DOMAIN "SYS_TEST"	/* Use this only if it weren't */
#endif
	(void) textdomain(TEXT_DOMAIN);

	/*
	 * ckarg() parses the arguments. In case of an error,
	 * it sets the retval and returns FAIL (-1).
	 */

	flag = ckarg(argc, argv, setattr);
	dprintf1("flag is %0x\n", flag);
	if (flag == FAIL)
		exit(retval);

	/* Save away uid, gid, euid, egid, pid, and auditinfo. */
	audit_passwd_init_id();

	argc -= optind;

	if (argc < 1) {
		if ((usrname = getlogin()) == NULL) {
			struct passwd *pass = getpwuid(getuid());
			if (pass != NULL)
				usrname = pass->pw_name;
			else {
				rusage();
				exit(NOPERM);
			}
		} else if (flag == 0) {
			/*
			 * If flag is zero, change passwd.
			 * Otherwise, it will display or
			 * modify password aging attributes
			 */
			(void) fprintf(stderr, "%s:  %s %s\n",
				prognamep, "Changing password for", usrname);
		}
	} else
		usrname = argv[optind];

	if (ia_start(prognamep, usrname, NULL, NULL, &ia_conv,
	    &iah) != IA_SUCCESS)
		passwd_exit(IA_NOPERM);	/* exit properly according */
					/* by calling passwd_exit() */


	/* switch on flag */
	switch (flag) {

	case SAFLAG:		/* display password attributes by root */
		/* make sure only one repository may be specified */

		/*
		 * For nis+, backend will go thru the passwd table and display
		 * all passwd attributes for all users. It can't just look at
		 * local passwd file.
		 * It should not reach here for nis because SAFLAG is not
		 * supported for nis.
		 */
		if (IS_NISPLUS(repository)) {
			getattr = get_authtokattr(NULL);
			passwd_exit(IA_SUCCESS);
		}

		/* this is really an assertion that nis should not occur */
		if (IS_NIS(repository)) {
			rusage();
			passwd_exit(IA_FATAL);
		}

		retval = get_namelist(&namelist, &num_user);
		if (retval != SUCCESS)
			(void) passwd_exit(retval);

		if (num_user == 0) {
			(void) fprintf(stderr, "%s: %s\n",
					prognamep, gettext(MSG_FF));
			passwd_exit(IA_FATAL);
		}
		i = 0;
		while (namelist[i] != NULL) {
			getattr = get_authtokattr(namelist[i]);
			(void) display_attr(namelist[i], getattr);
			free(namelist[i]);
			while (*getattr != NULL) {
				free(*getattr);
				getattr++;
			}
			i++;
		}
		(void) free(namelist);
		passwd_exit(IA_SUCCESS);
		break;		/* NOT REACHED */

	case SFLAG:		/* display password attributes by user */
		getattr = get_authtokattr(usrname);
		(void) display_attr(usrname, getattr);
		while (*getattr != NULL) {
			free(*getattr);
			getattr++;
		}
		passwd_exit(IA_SUCCESS);
		break;		/* NOT REACHED */


	case 0:			/* changing user password */
		dprintf1("call ia_chauthtok() repository=%d \n", repository);
		retval = ia_chauthtok(iah, &ia_status, repository, nisdomain);
		audit_passwd_sorf(retval);
		(void) passwd_exit(retval);
		break;		/* NOT REACHED */


	default:		/* changing user password attributes */
		retval = ia_set_authtokattr(iah, &setattr[0], &ia_status,
		    repository, nisdomain);
		for (i = 0; setattr[i] != NULL; i++)
			free(setattr[i]);
		audit_passwd_attributes_sorf(retval);
		(void) passwd_exit(retval);

	}


}

/*
 * ckarg():
 *	This function parses and verifies the
 * 	arguments.  It takes three parameters:
 * 	argc => # of arguments
 * 	argv => pointer to an argument
 * 	setattr => pointer to a character array
 * 	In case of an error it prints the appropriate error
 * 	message, sets the retval and returns FAIL(-1).
 *	In case of success it will return an integer flag to indicate
 *	whether the caller wants to change password, or to display/set
 *	password attributes.  When setting password attributes is
 *	requested, the charater array pointed by "setattr" will be
 *	filled with ATTRIBUTE=VALUE pairs after the call.  Those
 *	pairs are set according to the input arguments' value
 */

static int
ckarg(argc, argv, setattr)
	int argc;
	char **argv;
	char *setattr[];
{
	extern char	*optarg;
	char		*char_p;
	register int	opt;
	register int	k;
	register int	flag;
	register int	entry_n, entry_x;
	char		*tmp;

	flag = 0;
	k = 0;
	entry_n = entry_x = -1;
	while ((opt = getopt(argc, argv, "r:aldefghsx:n:w:D:")) != EOF) {
		switch (opt) {

		case 'r':
			/* repository: this option should be specified first */
			if (IS_OPWCMD(repository)) {
				(void) fprintf(stderr, gettext(
		"can't invoke nispasswd or yppasswd with -r option\n"));
				rusage();
				exit(BADSYN);
			}
			if (repository != R_DEFAULT) {
				(void) fprintf(stderr, gettext(
			"Repository is already defined or specified.\n"));
				rusage();
				exit(BADSYN);
			}
			if (strcmp(optarg, "nisplus") == 0) {
				repository = R_NISPLUS;
				nisdomain = nis_local_directory();
				dprintf1("domain is %s\n", nisdomain);
			} else if (strcmp(optarg, "nis") == 0)
				repository = R_NIS;
			else if (strcmp(optarg, "files") == 0)
				repository = R_FILES;
			else {
				(void) fprintf(stderr,
				    gettext("invalid repository\n"));
				rusage();
				exit(BADSYN);
			}
			break;

		case 'd':
			/* if no repository the default for -d is files */
			if (repository == R_DEFAULT)
				repository = R_FILES;

			/* if invoked from nispasswd, it means "display" */
			if (IS_OPWCMD(repository) && IS_NISPLUS(repository)) {
				/* map to new flag -s */
				flag |= SFLAG;
				break;
			}

			/* delete the password 				*/
			/* Only privileged process can execute this	*/
			if (IS_FILES(repository) == FALSE) {
				(void) fprintf(stderr, gettext(
				    "-d only applies to files repository\n"));
				rusage();	/* exit */
				exit(BADSYN);
			}

			if (ckuid() != SUCCESS)
				return (FAIL);
			if (flag & (LFLAG|SAFLAG|DFLAG)) {
				rusage();
				retval = BADOPT;
				return (FAIL);
			}
			flag |= DFLAG;
			pw_setup_setattr(setattr, k++, "AUTHTOK_DEL=", "1");
			break;

		case 'l':
			/* lock the password 				*/

			/* if no repository the default for -l is files */
			if (repository == R_DEFAULT)
				repository = R_FILES;

			if (IS_FILES(repository) == FALSE &&
			    IS_NISPLUS(repository) == FALSE) {
				(void) fprintf(stderr, gettext(
			"-l only applies to files or nisplus repository\n"));
				rusage();	/* exit */
				exit(BADOPT);
			}

			/* Only privileged process can execute this	*/
			if (IS_FILES(repository) && (ckuid() != SUCCESS))
				return (FAIL);
			if (flag & (DFLAG|SAFLAG|LFLAG|NONAGEFLAG)) {
				rusage();	/* exit */
				retval = BADOPT;
				return (FAIL);
			}
			flag |= LFLAG;
			pw_setup_setattr(setattr, k++, "AUTHTOK_LK=", "1");
			break;

		case 'x':

			/* if no repository the default for -x is files */
			if (repository == R_DEFAULT)
				repository = R_FILES;

			/* set the max date 				*/
			if (IS_FILES(repository) == FALSE &&
			    IS_NISPLUS(repository) == FALSE) {
				(void) fprintf(stderr, gettext(
			"-x only applies to files or nisplus repository\n"));
				rusage();	/* exit */
				exit(BADSYN);
			}

			/* Only privileged process can execute this	*/
			if (IS_FILES(repository) && (ckuid() != SUCCESS))
				return (FAIL);
			if (flag & (SAFLAG|MFLAG|NONAGEFLAG)) {
				(void) fprintf(stderr, "%s\n", gettext(MSG_BS));
				retval = BADOPT;
				return (FAIL);
			}
			flag |= MFLAG;
			if ((int)strlen(optarg)  <= 0 ||
			    (maxdate = strtol(optarg, &char_p, 10)) < -1 ||
			    *char_p != '\0') {
				(void) fprintf(stderr, "%s: %s -x\n",
					prognamep, gettext(MSG_NV));
				retval = BADSYN;
				return (FAIL);
			}
			entry_x = k;
			pw_setup_setattr(setattr, k++,
					"AUTHTOK_MAXAGE=", optarg);
			break;

		case 'n':

			/* if no repository the default for -n is files */
			if (repository == R_DEFAULT)
				repository = R_FILES;

			/* set the min date 				*/
			if (IS_FILES(repository) == FALSE &&
			    IS_NISPLUS(repository) == FALSE) {
				(void) fprintf(stderr, gettext(
			"-n only applies to files or nisplus repository\n"));
				rusage();	/* exit */
				exit(BADSYN);
			}

			/* Only privileged process can execute this	*/
			if (IS_FILES(repository) && (ckuid() != SUCCESS))
				return (FAIL);
			if (flag & (SAFLAG|NFLAG|NONAGEFLAG)) {
				(void) fprintf(stderr, "%s\n", gettext(MSG_BS));
				retval = BADOPT;
				return (FAIL);
			}
			flag |= NFLAG;
			if ((int)strlen(optarg)  <= 0 ||
			    (strtol(optarg, &char_p, 10)) < 0 ||
			    *char_p != '\0') {
				(void) fprintf(stderr, "%s: %s -n\n",
					prognamep, gettext(MSG_NV));
				retval = BADSYN;
				return (FAIL);
			}
			entry_n = k;
			pw_setup_setattr(setattr, k++,
					"AUTHTOK_MINAGE=", optarg);
			break;

		case 'w':

			/* if no repository the default for -w is files */
			if (repository == R_DEFAULT)
				repository = R_FILES;

			/* set the warning field 			*/
			if (IS_FILES(repository) == FALSE &&
			    IS_NISPLUS(repository) == FALSE) {
				(void) fprintf(stderr, gettext(
			"-w only applies to files or nisplus repository\n"));
				rusage();	/* exit */
				exit(BADSYN);
			}

			/* Only privileged process can execute this	*/
			if (IS_FILES(repository) && (ckuid() != SUCCESS))
				return (FAIL);
			if (flag & (SAFLAG|WFLAG|NONAGEFLAG)) {
				(void) fprintf(stderr, "%s\n", gettext(MSG_BS));
				retval = BADOPT;
				return (FAIL);
			}
			flag |= WFLAG;
			if ((int)strlen(optarg)  <= 0 ||
			    (strtol(optarg, &char_p, 10)) < 0 ||
			    *char_p != '\0') {
				(void) fprintf(stderr, "%s: %s -w\n",
					prognamep, gettext(MSG_NV));
				retval = BADSYN;
				return (FAIL);
			}
			pw_setup_setattr(setattr, k++,
					"AUTHTOK_WARNDATE=", optarg);
			break;

		case 's':

			/* if no repository the default for -s is files */
			if (repository == R_DEFAULT)
				repository = R_FILES;

			/* if invoked from nispasswd, it means "change shell" */
			if (IS_OPWCMD(repository) && IS_NISPLUS(repository)) {
				if (flag & (EFLAG|SAFLAG|AGEFLAG)) {
					(void) fprintf(stderr, "%s\n",
					    gettext(MSG_BS));
					retval = BADOPT;
					return (FAIL);
				}
				flag |= EFLAG;
				/* set attr */
				/* handle prompting in backend */
				pw_setup_setattr(setattr, k++,
				    "AUTHTOK_SHELL=", "1");
				break;
			}

			/* display password attributes */
			if (IS_FILES(repository) == FALSE &&
			    IS_NISPLUS(repository) == FALSE) {
				(void) fprintf(stderr, gettext(
			"-s only applies to files or nisplus repository\n"));
				rusage();	/* exit */
				exit(BADSYN);
			}

			if (flag && (flag != AFLAG)) {
				(void) fprintf(stderr, "%s\n", gettext(MSG_BS));
				retval = BADOPT;
				return (FAIL);
			}
			flag |= SFLAG;
			break;

		case 'a':

			/* if no repository the default for -a is files */
			if (repository == R_DEFAULT)
				repository = R_FILES;

			/* if invoked from nispasswd, it means "display all" */
			if (IS_OPWCMD(repository) && IS_NISPLUS(repository)) {
				if (flag) {
					(void) fprintf(stderr, "%s\n",
					    gettext(MSG_BS));
					retval = BADOPT;
					return (FAIL);
				}
				flag |= SAFLAG;
				break;
			}

			/* display password attributes 			*/
			if (IS_FILES(repository) == FALSE &&
			    IS_NISPLUS(repository) == FALSE) {
				(void) fprintf(stderr, gettext(
			"-a only applies to files or nisplus repository\n"));
				rusage();	/* exit */
				exit(BADSYN);
			}

			/* Only privileged process can execute this	*/
			if (IS_FILES(repository) && (ckuid() != SUCCESS))
				return (FAIL);
			if (flag && (flag != SFLAG)) {
				(void) fprintf(stderr, "%s\n", gettext(MSG_BS));
				retval = BADOPT;
				return (FAIL);
			}
			flag |= AFLAG;
			break;

		case 'f':

			/* if no repository the default for -f is files */
			if (repository == R_DEFAULT)
				repository = R_FILES;

			/* expire password attributes 			*/
			if (IS_FILES(repository) == FALSE &&
			    IS_NISPLUS(repository) == FALSE) {
				(void) fprintf(stderr, gettext(
			"-f only applies to files or nisplus repository\n"));
				rusage();	/* exit */
				exit(BADSYN);
			}

			/* Only privileged process can execute this	*/
			if (IS_FILES(repository) && (ckuid() != SUCCESS))
				return (FAIL);
			if (flag & (SAFLAG|FFLAG|NONAGEFLAG)) {
				(void) fprintf(stderr, "%s\n", MSG_BS);
				retval = BADOPT;
				return (FAIL);
			}
			flag |= FFLAG;
			pw_setup_setattr(setattr, k++, "AUTHTOK_EXP=", "1");
			break;

		case 'D':
			if (IS_NISPLUS(repository) == FALSE) {
				(void) fprintf(stderr, gettext(
				    "-D only applies to nisplus repository\n"));
				rusage();	/* exit */
				exit(BADSYN);
			}

			if (flag & AFLAG) {
				(void) fprintf(stderr, "%s\n", gettext(MSG_BS));
				retval = BADOPT;
				return (FAIL);
			}
			/* It is cleaner not to set this flag */
			/* flag |= OFLAG; */

			/* get domain from optarg */
			/* domain is passed to backend using ia_start() */
			nisdomain = optarg;
			break;

		case 'e':

			/* if no repository the default for -e is files */
			if (repository == R_DEFAULT)
				repository = R_FILES;

			/* change login shell */
			if (IS_OPWCMD(repository) && IS_NIS(repository)) {
				(void) fprintf(stderr, gettext(
				    "-e doesn't apply to yppasswd \n"));
				rusage();	/* exit */
				exit(BADSYN);
			}


			/* Only privileged process can execute this	*/
			if (IS_FILES(repository) && (ckuid() != SUCCESS))
				return (FAIL);
			if (flag & (EFLAG|SAFLAG|AGEFLAG)) {
				(void) fprintf(stderr, "%s\n", gettext(MSG_BS));
				retval = BADOPT;
				return (FAIL);
			}
			flag |= EFLAG;
			/* set attr */
			/* handle prompting in backend */
			pw_setup_setattr(setattr, k++, "AUTHTOK_SHELL=", "1");
			break;

		case 'g':

			/* if no repository the default for -g is files */
			if (repository == R_DEFAULT)
				repository = R_FILES;

			/* change gecos information */
			if (IS_OPWCMD(repository) && IS_NIS(repository)) {
				(void) fprintf(stderr, gettext(
				    "-g doesn't apply to yppasswd \n"));
				rusage();	/* exit */
				exit(BADSYN);
			}


			/* Only privileged process can execute this	*/
			if (IS_FILES(repository) && (ckuid() != SUCCESS))
				return (FAIL);
			if (flag & (GFLAG|SAFLAG|AGEFLAG)) {
				(void) fprintf(stderr, "%s\n", gettext(MSG_BS));
				retval = BADOPT;
				return (FAIL);
			}
			flag |= GFLAG;
			pw_setup_setattr(setattr, k++, "AUTHTOK_GECOS=", "1");
			break;

		case 'h':

			/* if no repository the default for -h is files */
			if (repository == R_DEFAULT)
				repository = R_FILES;

			/* change home dir */
			if (IS_OPWCMD(repository) && IS_NIS(repository)) {
				(void) fprintf(stderr, gettext(
				    "-h doesn't apply to yppasswd \n"));
				rusage();	/* exit */
				exit(BADSYN);
			}


			/* Only privileged process can execute this	*/
			if (IS_FILES(repository) && (ckuid() != SUCCESS))
				return (FAIL);
			if (flag & (HFLAG|SAFLAG|AGEFLAG)) {
				(void) fprintf(stderr, "%s\n", gettext(MSG_BS));
				retval = BADOPT;
				return (FAIL);
			}
			flag |= HFLAG;
			pw_setup_setattr(setattr, k++, "AUTHTOK_HOMEDIR=", "1");
			break;

		case '?':
			rusage();
			retval = BADSYN;
			return (FAIL);
		}
	}

	/* terminate the ATTRIBUTE=VALUE pairs array with NULL */
	(void) pw_setup_setattr(setattr, k++, NULL, NULL);

	argc -= optind;
	if (argc > 1) {
		rusage();
		retval = BADSYN;
		return (FAIL);
	}

	/*
	 * If option -n appears before -x, exchange them in the table because
	 * field max must be set before setting field min.
	 */
	if ((entry_n >= 0 && entry_x >= 0) && (entry_n < entry_x)) {
		tmp = setattr[entry_n];
		setattr[entry_n] = setattr[entry_x];
		setattr[entry_x] = tmp;
	}



	/* If no options are specified or only the show option */
	/* is specified, return because no option error checking */
	/* is needed */
	if (!flag || (flag == SFLAG))
		return (flag);

	/* AFLAG must be used with SFLAG */
	if (flag == AFLAG) {
		rusage();
		retval = BADSYN;
		return (FAIL);
	}

	if (flag != SAFLAG && argc < 1) {
		/*
		 * user name is not specified (argc<1), it can't be
		 * aging info update.
		 */
		if (!(flag & NONAGEFLAG)) {
			rusage();
			retval = BADSYN;
			return (FAIL);
		}
	}

	/* user name(s) may not be specified when SAFLAG is used. */
	if (flag == SAFLAG && argc >= 1) {
		rusage();
		retval = BADSYN;
		return (FAIL);
	}

	/*
	 * If aging is being turned off (maxdate == -1), mindate may not
	 * be specified.
	 */
	if ((maxdate == -1) && (flag & NFLAG)) {
		(void) fprintf(stderr, "%s: %s -x\n",
				prognamep, gettext(MSG_NV));
		retval = BADSYN;
		return (FAIL);
	}

	return (flag);
}

/*
 *
 * ckuid():
 *	This function returns SUCCESS if the caller is root, else
 *	it returns NOPERM.
 *
 */

static int
ckuid()
{
	if (uid != 0) {
		(void) fprintf(stderr, "%s: %s\n", prognamep, gettext(MSG_NP));
		return (retval = NOPERM);
	}
	return (SUCCESS);
}

/*
 * get_authtokattr():
 *	This function sets user name in PAM buffer pointed by the
 *	authentication handle "iah" first, then calls
 *	ia_get_authtokattr() to get the values of the authentication
 *	token attributes associated with the user specified by
 *	"username".  Upon success, it will return a pointer which
 *	points to a character array which stores the user's
 *	authentication token ATTRIBUTE=VALUE pairs.  Else, it will
 *	call passwd_exit() to exit properly.
 *
 */

char **
get_authtokattr(username)
	char *username;
{
	char			**get_attr;
	struct	ia_status	ia_status;
	int 			retcode;

	/* nis+: if username is NULL, it gets all users */
	if (ia_set_item(iah, IA_USER, username) != IA_SUCCESS)
		passwd_exit(IA_NOPERM);

	retcode = ia_get_authtokattr(iah, &get_attr, &ia_status, repository,
	    nisdomain);
	if (retcode != IA_SUCCESS)
			passwd_exit(retcode);
	return (get_attr);
}


/*
 *
 * display_attr():
 *	This function prints out the password attributes of a user
 *	onto standand output.
 *
 */

void
display_attr(usrname, getattr)
	char *usrname;
	char **getattr;
{
	char 		*value;
	long		lstchg;
	struct tm	*tmp;

	(void) fprintf(stdout, "%s  ", usrname);

	if ((value = pw_attr_match("AUTHTOK_STATUS", getattr)) != NULL)
		(void) fprintf(stdout, "%s  ", value);

	if ((value = pw_attr_match("AUTHTOK_LASTCHANGE", getattr)) != NULL) {
		lstchg = atoi(value);
		if (lstchg == 0)
			(void) strcpy(value, "00/00/00  ");
		else {
			tmp = gmtime(&lstchg);
			(void) sprintf(value, "%.2d/%.2d/%.2d  ",
			(tmp->tm_mon + 1), tmp->tm_mday, tmp->tm_year);
		}

		(void) fprintf(stdout, "%s  ", value);
	}

	if ((value = pw_attr_match("AUTHTOK_MINAGE", getattr)) != NULL)
		(void) fprintf(stdout, "%s  ", value);

	if ((value = pw_attr_match("AUTHTOK_MAXAGE", getattr)) != NULL)
		(void) fprintf(stdout, "%s  ", value);

	if ((value = pw_attr_match("AUTHTOK_WARNDATE", getattr)) != NULL)
		(void) fprintf(stdout, "%s  ", value);

	(void) fprintf(stdout, "\n");
}

/*
 *
 * get_namelist():
 *	This function gets a list of user names on the system from
 *	the /etc/passwd file.
 *
 */

int
get_namelist(namelist_p, num_user)
	char ***namelist_p;
	int *num_user;
{
	FILE		*pwfp;
	struct passwd	*pwd;
	int		max_user;
	int		nuser;
	char	**nl;

	nuser = 0;
	errno = 0;
	pwd = NULL;

	if ((pwfp = fopen(PASSWD, "r")) == NULL)
		return (IA_FATAL);

	/*
	 * find out the actual number of entries in the PASSWD file
	 */
	max_user = 1;			/* need one slot for terminator NULL */
	while ((pwd = fgetpwent(pwfp)) != NULL)
		max_user++;

	/*
	 *	reset the file stream pointer
	 */
	rewind(pwfp);

	nl = (char **)calloc(max_user, (sizeof (char *)));
	if (nl == NULL) {
		(void) fclose(pwfp);
		return (IA_NOPERM);
	}

	while ((pwd = fgetpwent(pwfp)) != NULL) {
		nl[nuser] = (char *)malloc(strlen(pwd->pw_name) + 1);
		if (nl[nuser] == NULL) {
			(void) fclose(pwfp);
			return (IA_NOPERM);
		}
		(void) strcpy((char *)nl[nuser], pwd->pw_name);
		nuser++;
	}

	nl[nuser] = NULL;
	*num_user = nuser;
	*namelist_p = nl;
	(void) fclose(pwfp);
	return (SUCCESS);
}

/*
 *
 * passwd_exit():
 *	This function will call exit() with appropriate exit code
 *	according to the input "retcode" value, which is supposed
 *	to be a return value from an "ia_..()" function call.
 *	It also calls ia_end() to clean-up buffers before exit.
 *
 */

void
passwd_exit(retcode)
	int	retcode;
{
	int	exitcode = 0;

	if (iah)
		ia_end(iah);

	switch (retcode) {
	case IA_SUCCESS:
			exitcode = SUCCESS;
			break;
	case IA_NOPERM:
			(void) fprintf(stderr, "%s\n", gettext(MSG_NP));
			exitcode = NOPERM;
			break;
	case IA_FMERR:
			exitcode = FMERR;
			break;
	case IA_FATAL:
			exitcode = FATAL;
			break;
	case IA_FBUSY:
			exitcode = EBUSY;
			break;
	case IA_BADAGE:
			(void) fprintf(stderr, "%s\n", gettext(MSG_AD));
			exitcode = BADAGE;
			break;
	case IA_CONV_FAILURE:
			exitcode = NOPERM;
			break;
	default:
			exitcode = NOPERM;
			break;			/* Since we don't want to */
						/* have new exit code for */
						/* passwd now. we will just */
						/* use NOPERM instead. */
	}
	exit(exitcode);
}

/*
 *
 * passwd_conv():
 *	This is the conv (conversation) function called from
 *	a PAM authentication scheme to print error messages
 *	or garner information from the user.
 *
 */

static int
passwd_conv(conv_id, num_msg, msg, response, appdata_ptr)
	int conv_id;
	int num_msg;
	struct ia_message **msg;
	struct ia_response **response;
	void *appdata_ptr;
{
	struct ia_message	*m;
	struct ia_response	*r;
	char 			*temp;
	int			k;

	if (num_msg <= 0)
		return (IA_CONV_FAILURE);

	*response = (struct ia_response *)calloc(num_msg,
						sizeof (struct ia_response));
	if (*response == NULL)
		return (IA_CONV_FAILURE);

	(void) memset(*response, 0, sizeof (struct ia_response));

	k = num_msg;
	m = *msg;
	r = *response;
	while (k--) {

		switch (m->msg_style) {

		case IA_PROMPT_ECHO_OFF:
			temp = getpass(m->msg);
			if (temp != NULL) {
				r->resp = (char *)malloc(strlen(temp)+1);
				if (r->resp == NULL) {
					free_resp(num_msg, *response);
					*response = NULL;
					return (IA_CONV_FAILURE);
				}
				(void) strcpy(r->resp, temp);
				r->resp_len  = strlen(r->resp);
			}

			m++;
			r++;
			break;

		case IA_PROMPT_ECHO_ON:
			if (m->msg != NULL) {
				(void) fputs(m->msg, stdout);
			}
			r->resp = (char *)malloc(MAX_RESP_SIZE);
			if (r->resp == NULL) {
				free_resp(num_msg, *response);
				*response = NULL;
				return (IA_CONV_FAILURE);
			}
			if (fgets(r->resp, MAX_RESP_SIZE, stdin) != NULL)
				r->resp_len  = strlen(r->resp);
			m++;
			r++;
			break;

		case IA_ERROR_MSG:
			if (m->msg != NULL) {
				(void) fputs(m->msg, stderr);
			}
			m++;
			r++;
			break;
		case IA_TEXTINFO:
			if (m->msg != NULL) {
				(void) fputs(m->msg, stdout);
			}
			m++;
			r++;
			break;

		default:
			break;
		}
	}
	return (IA_SUCCESS);
}

static void
dummy_conv()
{
}


/*
 * 		Utilities Functions
 */


/*
 *	s1 is either name, or name=value
 *	s2 is an array of name=value pairs
 *	if name in s1 match the name in a pair stored in s2,
 *	then return value of the matched pair, else NULL
 */

static char *
pw_attr_match(s1, s2)
	register char *s1;
	register char **s2;
{
	char *s3;
	char *s1_save;

	s1_save = s1;
	while (*s2 != NULL) {
		s3 = *s2;
		while (*s1 == *s3++)
			if (*s1++ == '=')
				return (s3);
		if (*s1 == '\0' && *(s3-1) == '=')
			return (s3);
		s2++;
		s1 = s1_save;

	}
	return (NULL);
}

void
pw_setup_setattr(setattr, k, attr, value)
	char *setattr[];
	int k;
	char attr[];
	char value[];
{

	if (attr != NULL) {
		setattr[k] = (char *)malloc(strlen(attr) + strlen(value) + 1);
		if (setattr[k] == NULL)
			return;
		(void) strcpy(setattr[k], attr);
		(void) strcat(setattr[k], value);
	} else
		setattr[k] = NULL;
}

void
rusage()
{
	if (IS_OPWCMD(repository)) {
		(void) fprintf(stderr, gettext(
"yppasswd and nispasswd have been replaced by the new passwd command.\n"));
		(void) fprintf(stderr,
		    gettext("The usage is displayed below.\n"));
		(void) fprintf(stderr, gettext(
	"To continue using yppasswd/nispasswd, please refer to man pages.\n"));
	}
	(void) fprintf(stderr, gettext("usage:\n"));
	(void) fprintf(stderr, gettext(
	    "\tpasswd [-r files | -r nis | -r nisplus] [name]\n"));
	(void) fprintf(stderr, gettext(
	    "\tpasswd [-r files] [-egh] [name]\n"));
	(void) fprintf(stderr, gettext(
	    "\tpasswd [-r files] -sa\n"));
	(void) fprintf(stderr, gettext(
	    "\tpasswd [-r files] -s [name]\n"));
	(void) fprintf(stderr, gettext(
	"\tpasswd [-r files] [-d|-l] [-f] [-n min] [-w warn] [-x max] name\n"));
	(void) fprintf(stderr, gettext(
	    "\tpasswd -r nis [-egh] [name]\n"));
	(void) fprintf(stderr, gettext(
	    "\tpasswd -r nisplus [-egh] [-D domainname] [name]\n"));
	(void) fprintf(stderr, gettext(
	    "\tpasswd -r nisplus -sa\n"));
	(void) fprintf(stderr, gettext(
	    "\tpasswd -r nisplus [-D domainname] -s [name]\n"));
	(void) fprintf(stderr, gettext(
	    "\tpasswd -r nisplus [-l] [-f] [-n min] [-w warn]"));
	(void) fprintf(stderr, gettext(" [-x max] [-D domainname] name\n"));
}
