/*
 *      auditconfig.c
 *      
 *      Copyright (c) 1991, 1992, Sun Microsystems, Inc.
 *      All Rights Reserved.
 */

#pragma ident	"@(#)auditconfig.c	1.8	92/11/01 SMI"

#if !defined(lint) && defined(SCCSIDS)
static char *sccsid = "@(#)auditconfig.c 1.8 92/11/01 SMI;";
static char *cmw_sccsid = "@(#)auditconfig.c 2.4 92/08/10 SMI; SunOS CMW";
#endif

/*
 * auditconfig - set and display audit parameters
 */

#include <locale.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/param.h>
#include <stdio.h>
#include <varargs.h>
#include <string.h>
#include <nlist.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/mkdev.h>
#include <sys/param.h>
#include <pwd.h>

#include <bsm/audit.h>
#include <bsm/audit_record.h>
#include <bsm/libbsm.h>

#if !defined(TEXT_DOMAIN)
#define TEXT_DOMAIN "SUNW_BSM_AUDITCONFIG"
#endif

#define AC_ARG_AUDIT			0
#define AC_ARG_CHKCONF			1
#define AC_ARG_CONF			2
#define AC_ARG_GETASID			3	/* same as GETSID */
#define AC_ARG_GETAUDIT			4
#define AC_ARG_GETAUID			5
#define AC_ARG_GETCAR			6
#define AC_ARG_GETCLASS			7	/* same as GETESTATE */
#define AC_ARG_GETCOND			8
#define AC_ARG_GETCWD			9
#define AC_ARG_GETESTATE		10
#define AC_ARG_GETKERNSTATE		11
#define AC_ARG_GETKMASK			12	/* same as GETKERNSTATE */
#define AC_ARG_GETPINFO			13	/* new */  
#define AC_ARG_GETPOLICY		14
#define AC_ARG_GETQBUFSZ		15
#define AC_ARG_GETQCTRL			16
#define AC_ARG_GETQDELAY		17
#define AC_ARG_GETQHIWATER		18
#define AC_ARG_GETQLOWATER		19
#define AC_ARG_GETSID			20
#define AC_ARG_GETSTAT			21
#define AC_ARG_GETTERMID		22
#define AC_ARG_GETUSERAUDIT		23	/* only CMW syscall w/out */
#define AC_ARG_LSEVENT			24
#define AC_ARG_LSPOLICY			25
#define AC_ARG_SETASID			26	/* new */  
#define AC_ARG_SETAUDIT			27
#define AC_ARG_SETAUID			28
#define AC_ARG_SETCLASS			29	/* same as SETESTATE */
#define AC_ARG_SETCOND			30
#define AC_ARG_SETESTATE		31
#define AC_ARG_SETKERNSTATE		32
#define AC_ARG_SETKMASK			33	/* same as SETKERNSTATE */
#define AC_ARG_SETPMASK			34	/* new */  
#define AC_ARG_SETSMASK			35	/* new */  
#define AC_ARG_SETSTAT			36
#define AC_ARG_SETPOLICY		37
#define AC_ARG_SETQBUFSZ		38
#define AC_ARG_SETQCTRL			39
#define AC_ARG_SETQDELAY		40
#define AC_ARG_SETQHIWATER		41
#define AC_ARG_SETQLOWATER		42
#define AC_ARG_SETTERMID		43	/* new */
#define AC_ARG_SETUMASK			44	/* new */
#define AC_ARG_SETUSERAUDIT		45
#define AC_ARG_GETFSIZE			46	/* new */
#define AC_ARG_SETFSIZE			47	/* new */

#define AC_KERN_EVENT 		0
#define AC_USER_EVENT 		1

#define NONE(s) (!strlen(s) ? "none" : s)

#ifdef SunOS_CMW
#define ALL_POLICIES   (AUDIT_AHLT|\
			AUDIT_ARGE|\
			AUDIT_ARGV|\
			AUDIT_CNT|\
			AUDIT_GROUP|\
			AUDIT_PASSWD|\
			AUDIT_USER|\
			AUDIT_WINDATA|\
		        AUDIT_SEQ|\
			AUDIT_TRAIL|\
			AUDIT_PATH)
#else /* !SunOS_CMW */
#define ALL_POLICIES   (AUDIT_AHLT|\
			AUDIT_ARGE|\
			AUDIT_ARGV|\
			AUDIT_CNT|\
			AUDIT_GROUP|\
			AUDIT_PASSWD|\
			AUDIT_WINDATA|\
		        AUDIT_SEQ|\
			AUDIT_TRAIL|\
			AUDIT_PATH)
#endif /* SunOS_CMW */

#define NO_POLICIES  (0)

#define ONEK 1024

/* This should be defined in <string.h>, but it is not */
extern int strncasecmp();

/* 
 * remove this after the audit.h is fixed
 */

struct arg_entry {
	char *arg_str;
	char *arg_opts;
	int auditconfig_cmd;
}; 

struct policy_entry {
	char *policy_str;
	u_long policy_mask;
	char *policy_desc;
};

static struct arg_entry arg_table[] = {
	{ "-audit",       "event sorf retval string",   AC_ARG_AUDIT},
	{ "-chkconf",     "",                           AC_ARG_CHKCONF},
	{ "-conf",        "",                           AC_ARG_CONF},
	{ "-getasid",     "",                           AC_ARG_GETASID},
	{ "-getaudit",    "",                           AC_ARG_GETAUDIT},
	{ "-getauid",     "",                           AC_ARG_GETAUID},
	{ "-getcar",      "",                           AC_ARG_GETCAR},
	{ "-getclass",    "",                           AC_ARG_GETCLASS},
	{ "-getcond",     "",                           AC_ARG_GETCOND},
	{ "-getcwd",      "",                           AC_ARG_GETCWD},
	{ "-getestate",   "event",                      AC_ARG_GETESTATE},
	{ "-getfsize",	  "",				AC_ARG_GETFSIZE},
	{ "-getkernstate","",                           AC_ARG_GETKERNSTATE},
	{ "-getkmask",    "",                           AC_ARG_GETKMASK},
	{ "-getpinfo",    "",                           AC_ARG_GETPINFO},
	{ "-getpolicy",   "",                           AC_ARG_GETPOLICY},
	{ "-getqbufsz",   "",                           AC_ARG_GETQBUFSZ},
	{ "-getqctrl",    "",                           AC_ARG_GETQCTRL},
	{ "-getqdelay",   "",                           AC_ARG_GETQDELAY},
	{ "-getqhiwater", "",                           AC_ARG_GETQHIWATER},
	{ "-getqlowater", "",                           AC_ARG_GETQLOWATER},
	{ "-getsid",      "",                           AC_ARG_GETSID},
	{ "-getstat",     "",                           AC_ARG_GETSTAT},
	{ "-gettermid",   "",                           AC_ARG_GETTERMID},
	{ "-gettid",      "",                           AC_ARG_GETTERMID},
	{ "-getuseraudit","user",                       AC_ARG_GETUSERAUDIT},
	{ "-lsevent",     "",                           AC_ARG_LSEVENT},
	{ "-lspolicy",    "",                           AC_ARG_LSPOLICY},
	{ "-setasid",     "asid [cmd]",                 AC_ARG_SETASID},
	{ "-setaudit",    "auid audit_flags termid sid [cmd]",AC_ARG_SETAUDIT},
	{ "-setauid",     "auid [cmd]",                 AC_ARG_SETAUID},
	{ "-setclass",    "event audit_flags",          AC_ARG_SETCLASS},
	{ "-setcond",     "condition",                  AC_ARG_SETCOND},
	{ "-setestate",   "event audit_flags",          AC_ARG_SETESTATE},
	{ "-setfsize",	  "filesize",			AC_ARG_SETFSIZE},
	{ "-setkernstate","audit_flags",                AC_ARG_SETKERNSTATE},
	{ "-setkmask",    "audit_flags",                AC_ARG_SETKMASK},
	{ "-setpmask",    "pid audit_flags [cmd]",      AC_ARG_SETPMASK},
	{ "-setpolicy",   "policy_flags",               AC_ARG_SETPOLICY},
	{ "-setqbufsz",   "bufsz",                      AC_ARG_SETQBUFSZ},
	{ "-setqctrl",    "hiwater lowater bufsz delay",AC_ARG_SETQCTRL},
	{ "-setqdelay",   "delay",                      AC_ARG_SETQDELAY},
	{ "-setqhiwater", "hiwater",                    AC_ARG_SETQHIWATER},
	{ "-setqlowater", "lowater",                    AC_ARG_SETQLOWATER},
	{ "-setsmask",    "asid audit_flags",           AC_ARG_SETSMASK},
	{ "-setstat",     "",                           AC_ARG_SETSTAT},
	{ "-settid",      "tid [cmd]",                  AC_ARG_SETTERMID},
	{ "-setumask",    "user audit_flags",           AC_ARG_SETUMASK},
	{ "-setuseraudit","user audit_flags",           AC_ARG_SETUSERAUDIT}
	};

#define ARG_TBL_SZ (sizeof(arg_table)/sizeof(struct arg_entry))

static struct arg_entry arg2_table[] = {
	{ "-chkconf",     "",                           AC_ARG_CHKCONF},
	{ "-conf",        "",                           AC_ARG_CONF},
	{ "-getcond",     "",                           AC_ARG_GETCOND},
	{ "-setcond",     "condition",                  AC_ARG_SETCOND},
	{ "-getclass",    "event",                      AC_ARG_GETCLASS},
	{ "-setclass",    "event audit_flags",          AC_ARG_SETCLASS},
	{ "-lsevent",     "",                           AC_ARG_LSEVENT},
	{ "-lspolicy",    "",                           AC_ARG_LSPOLICY},
	{ "-getpolicy",   "",                           AC_ARG_GETPOLICY},
	{ "-setpolicy",   "policy_flags",               AC_ARG_SETPOLICY},
	{ "-getstat",     "",                           AC_ARG_GETSTAT},
	{ "-getpinfo",    "pid",                        AC_ARG_GETPINFO},
	{ "-setpmask",    "pid audit_flags",            AC_ARG_SETPMASK},
	{ "-setsmask",    "asid audit_flags",           AC_ARG_SETSMASK},
	{ "-setumask",    "user audit_flags",           AC_ARG_SETUMASK},
	{ "-getfsize",	  "",				AC_ARG_GETFSIZE},
	{ "-setfsize",	  "filesize",			AC_ARG_SETFSIZE}
	};

#define ARG2_TBL_SZ (sizeof(arg2_table)/sizeof(struct arg_entry))

static struct policy_entry policy_table[] = {
	{ "arge",   AUDIT_ARGE,	  "include exec envronment args in audit recs" },
	{ "argv",   AUDIT_ARGV,	  "include exec args in audit recs" },
	{ "cnt",    AUDIT_CNT,    "when no more space, drop recs and keep a count" },
	{ "group",  AUDIT_GROUP,  "include supplementary groups in audit recs" },
	{ "seq",    AUDIT_SEQ,    "include a sequence number in audit recs" },
	{ "trail",  AUDIT_TRAIL,  "include trailer tokens in audit recs" },
	{ "path",   AUDIT_PATH,    "allow multiple paths per event" },
#ifdef SunOS_CMW
	{ "ahlt",   AUDIT_AHLT,   "halt machine if we can't record an async event"},
	{ "0",      NO_POLICIES,  "no policies" },
	{ "zero",   NO_POLICIES,  "no policies" },
	{ "passwd", AUDIT_PASSWD, "include clear text passwords in audit recs" },
	{ "user",   AUDIT_USER,   "make audituser(2) unprivileged" },
	{ "windata",AUDIT_WINDATA,"include inter-window data move data in audit recs" },
#endif /* SunOS_CMW */
	{ "all",    ALL_POLICIES, "all policies"},
	{ "none",   NO_POLICIES,  "no policies" }};

#define POLICY_TBL_SZ (sizeof(policy_table)/sizeof(struct policy_entry))

static char *progname;

static au_event_ent_t *egetauevnam();
static au_event_ent_t *egetauevnum();
static char *strtolower();
static int arg_ent_compare();
static int cond2str();
static int policy2str();
static int str2cond();
static int str2policy();
static int strisflags();
/* static int strishex(); */ /* not used */
static int strisipaddr();
static int strisnum();
static struct arg_entry *get_arg_ent();
static struct policy_entry *get_policy_ent();
static uid_t get_user_id();
static void chk_event_num();
static void chk_event_str();
static void chk_retval();
static void chk_sorf();
static void chk_tid();
static void do_args();
static void do_audit();
static void do_chkconf();
static void do_conf();
/* static void do_default(); */ /* not used */
static void do_getasid();
static void do_getaudit();
static void do_getauid();
static void do_getcar();
static void do_getclass();
static void do_getcond();
static void do_getcwd();
static void do_getkmask();
static void do_getpinfo();
static void do_getpolicy();
static void do_getqbufsz();
static void do_getqctrl();
static void do_getqdelay();
static void do_getqhiwater();
static void do_getqlowater();
static void do_getstat();
static void do_gettermid();
static void do_getuseraudit();
static void do_lsevent();
static void do_lspolicy();
static void do_setasid();
/* static void do_setauall(); */ /* not used */
static void do_setaudit();
static void do_setauid();
static void do_setcond();
static void do_setclass();
static void do_setkmask();
static void do_setpmask();
static void do_setsmask();
static void do_setumask();
static void do_setpolicy();
static void do_setqbufsz();
static void do_setqctrl();
static void do_setqdelay();
static void do_setqhiwater();
static void do_setqlowater();
static void do_setstat();
static void do_settid();
static void do_setuseraudit();
static void do_getfsize();
static void do_setfsize();
static void str2mask();
static void str2tid();
static void strsplit();

/* static pid_t egetppid(); */ /* not used */
static void eauditon();
static void egetaudit();
static void egetauditflagsbin();
static void egetauid();
static void esetaudit();
static void esetauid();
static void execit();
static void exit_error();
static void exit_usage();
static void parse_args();
static void print_asid();
static void print_auid();
static void print_mask();
static void print_mask1();
static void print_stats();
static void print_tid();
#ifdef SunOS_CMW 
static void egetuseraudit();
static void esetuseraudit();
#endif /* SunOS_CMW */

extern char *sys_errlist[];

int
main(argc, argv)
	int argc;
	char **argv;
{

	progname = *argv;
	progname = "auditconfig";

	if (argc == 1) {
		exit_usage(0);
		/* do_default(); */
		exit(0);
	}

	if (argc == 2 && 
		(argv[1][0] == '?' ||
		!strcmp(argv[1], "-h") || !strcmp(argv[1], "-?")))
		exit_usage(0);

	parse_args(argv);

	do_args(argv);

	exit(0);
	/* NOTREACHED */
}


/*
 * parse_args()
 *     Desc: Checks command line argument syntax.
 *     Inputs: Command line argv;
 *     Returns: If a syntax error is detected, a usage message is printed
 *              and exit() is called. If a syntax error is not detected,
 *              parse_args() returns without a value.
 */
static void
parse_args(argv)
	char **argv;
{
	struct arg_entry *ae;

	au_mask_t pmask;
	au_mask_t smask;
	au_mask_t umask;
	u_long cond;
	u_long policy;

	for (++argv; *argv; argv++) {
		if ((ae = get_arg_ent(*argv)) == (struct arg_entry *) 0) {
			exit_usage(1);
		}

		switch(ae->auditconfig_cmd) {

		case AC_ARG_AUDIT:
			++argv;
			if (!*argv)
				exit_usage(1);
			if (strisnum(*argv)) {
				chk_event_num(AC_USER_EVENT,
					(au_event_t)atol(*argv));
			} else
				chk_event_str(AC_USER_EVENT,*argv);
			++argv;
			if (!*argv)
				exit_usage(1);
			chk_sorf(*argv);
			++argv;
			if (!*argv)
				exit_usage(1);
			chk_retval(*argv);
			++argv;
			if (!*argv)
				exit_usage(1);
			break;

		case AC_ARG_CHKCONF:
			break;

		case AC_ARG_CONF:
			break;

		case AC_ARG_GETASID:
		case AC_ARG_GETSID:
			break;

		case AC_ARG_GETAUID:
			break;

		case AC_ARG_GETAUDIT:
			break;

		case AC_ARG_GETCLASS:
		case AC_ARG_GETESTATE:
			++argv;
			if (!*argv)
				exit_usage(1);
			if (strisnum(*argv))
				chk_event_num(AC_KERN_EVENT,
					(au_event_t)atol(*argv));
			else
				chk_event_str(AC_KERN_EVENT, *argv);
			break;

		case AC_ARG_GETCAR:
			break;

		case AC_ARG_GETCOND:
			break;

		case AC_ARG_GETCWD:
			break;

		case AC_ARG_GETKERNSTATE:
		case AC_ARG_GETKMASK:
			break;

		case AC_ARG_GETPOLICY:
			break;

		case AC_ARG_GETQBUFSZ:
			break;

		case AC_ARG_GETQCTRL:
			break;

		case AC_ARG_GETQDELAY:
			break;

		case AC_ARG_GETQHIWATER:
			break;

		case AC_ARG_GETQLOWATER:
			break;

		case AC_ARG_GETSTAT:
			break;

		case AC_ARG_GETTERMID:
			break;

		case AC_ARG_GETUSERAUDIT:
			++argv;
			if (!*argv)
				exit_usage(1);
			break;

		case AC_ARG_LSEVENT:
			break;

		case AC_ARG_LSPOLICY:
			break;

		case AC_ARG_SETASID:
			++argv;
			if (!*argv)
				exit_usage(1);
			
			if (*argv) {
				while (*argv)
					++argv;
				--argv;
			}
			break;

		case AC_ARG_SETAUID:
			++argv;
			if (!*argv)
				exit_usage(1);
			
			if (*argv) {
				while (*argv)
					++argv;
				--argv;
			}
			break;

		case AC_ARG_SETAUDIT:
			++argv;
			if (!*argv)
				exit_usage(1);
			
			if (*argv) {
				while (*argv)
					++argv;
				--argv;
			}
			break;

		case AC_ARG_SETCOND:
			++argv;
			if (!*argv)
				exit_usage(1);
			if (str2cond(*argv, &cond))
				exit_error(gettext(
					"Invalid audit condition specified."));
			break;

		case AC_ARG_SETCLASS:
		case AC_ARG_SETESTATE:
			++argv;
			if (!*argv)
				exit_usage(1);
			if (strisnum(*argv))
				chk_event_num(AC_KERN_EVENT,
					(au_event_t)atol(*argv));
			else
				chk_event_str(AC_KERN_EVENT, *argv);
			++argv;
			if (!*argv)
				exit_usage(1);
			str2mask(*argv, &pmask);
			break;

		case AC_ARG_SETKERNSTATE:
		case AC_ARG_SETKMASK:
			++argv;
			if (!*argv)
				exit_usage(1);
			str2mask(*argv, &pmask);
			break;

		case AC_ARG_SETPOLICY:
			++argv;
			if (!*argv)
				exit_usage(1);
			if (str2policy(*argv, &policy)) {
				exit_error(gettext(
					"Invalid policy (%s) specified."),
					*argv);
			}
			break;

		case AC_ARG_SETSTAT:
			break;

		case AC_ARG_GETPINFO:
			++argv;
			if (!*argv)
				exit_usage(1);
			break;

		case AC_ARG_SETPMASK:
			++argv;
			if (!*argv)
				exit_usage(1);
			++argv;
			if (!*argv)
				exit_usage(1);
			str2mask(*argv, &pmask);
			break;

		case AC_ARG_SETQBUFSZ:
			++argv;
			if (!*argv)
				exit_usage(1);
			if (!strisnum(*argv))
				exit_error(gettext("Invalid bufsz specified."));
			break;
				
		case AC_ARG_SETQCTRL:
			++argv;
			if (!*argv)
				exit_usage(1);
			if (!strisnum(*argv))
				exit_error(gettext(
					"Invalid hiwater specified."));
			++argv;
			if (!*argv)
				exit_usage(1);
			if (!strisnum(*argv))
				exit_error(gettext(
					gettext("Invalid lowater specified.")));
			++argv;
			if (!*argv)
				exit_usage(1);
			if (!strisnum(*argv))
				exit_error(gettext("Invalid bufsz specified."));
			++argv;
			if (!*argv)
				exit_usage(1);
			if (!strisnum(*argv))
				exit_error(gettext("Invalid delay specified."));
			break;

		case AC_ARG_SETQDELAY:
			++argv;
			if (!*argv)
				exit_usage(1);
			if (!strisnum(*argv))
				exit_error(gettext("Invalid delay specified."));
			break;

		case AC_ARG_SETQHIWATER:
			++argv;
			if (!*argv)
				exit_usage(1);
			if (!strisnum(*argv))
				exit_error(gettext(
					"Invalid hiwater specified."));
			break;

		case AC_ARG_SETQLOWATER:
			++argv;
			if (!*argv)
				exit_usage(1);
			if (!strisnum(*argv))
				exit_error(gettext(
					"Invalid lowater specified."));
			break;

		case AC_ARG_SETTERMID:
			++argv;
			chk_tid(*argv);
			break;

		case AC_ARG_SETUSERAUDIT:
			++argv;
			if (!*argv)
				exit_usage(1);
			++argv;
			if (!*argv)
				exit_usage(1);
			break;
		case AC_ARG_SETSMASK:
			++argv;
			if (!*argv)
				exit_usage(1);
			++argv;
			if (!*argv)
				exit_usage(1);
			str2mask(*argv, &smask);
			break;

		case AC_ARG_SETUMASK:
			++argv;
			if (!*argv)
				exit_usage(1);
			++argv;
			if (!*argv)
				exit_usage(1);
			str2mask(*argv, &umask);
			break;
		case AC_ARG_GETFSIZE:
			break;
		case AC_ARG_SETFSIZE:
			++argv;
			if (!*argv)
				exit_usage(1);
			if (!strisnum(*argv))
				exit_error(gettext(
					"Invalid hiwater specified."));
			break;

		default:
			exit_error(gettext("Internal error #1."));
			break;


		}
	}
}

/* 
 * do_default()
 *    Display all parameters that can easily be displayed.
 */

/*
 * Not used any more
 *
 * static void
 * do_default()
 * {
 *	do_getcar();
 *	do_getcond();
 *	do_getcwd();
 *	do_getpolicy();
 *	do_getqctrl();
 *	do_getkmask();
 *	do_getaudit();
 *
 *	return;
 * }
 */

/*
 * do_args()
 *     Desc: Do command line arguments in the order in which they appear.
 */
static void 
do_args(argv)
	char **argv;
{
	struct arg_entry *ae;

	for (++argv; *argv; argv++) {
		ae = get_arg_ent(*argv);

		switch(ae->auditconfig_cmd) {

		case AC_ARG_AUDIT:
			{ char sorf;
		 	  int  retval;
			  char *event_name;
			  char *audit_str;
			++argv;
			event_name = *argv;
			++argv;
			sorf = (char)atoi(*argv);
			++argv;
			retval = atoi(*argv);
			++argv;
			audit_str = *argv;
			do_audit(event_name, sorf, retval, audit_str);
			}
			break;

		case AC_ARG_CHKCONF:
			do_chkconf();
			break;

		case AC_ARG_CONF:
			do_conf();
			break;

		case AC_ARG_GETASID:
		case AC_ARG_GETSID:
			do_getasid();
			break;

		case AC_ARG_GETAUID:
			do_getauid();
			break;

		case AC_ARG_GETAUDIT:
			do_getaudit();
			break;

		case AC_ARG_GETCLASS:
		case AC_ARG_GETESTATE:
			++argv;
			do_getclass(*argv);
			break;

		case AC_ARG_GETCAR:
			do_getcar();
			break;

		case AC_ARG_GETCOND:
			do_getcond();
			break;

		case AC_ARG_GETCWD:
			do_getcwd();
			break;

		case AC_ARG_GETKERNSTATE:
		case AC_ARG_GETKMASK:
			do_getkmask();
			break;

		case AC_ARG_GETPOLICY:
			do_getpolicy();
			break;

		case AC_ARG_GETQBUFSZ:
			do_getqbufsz();
			break;

		case AC_ARG_GETQCTRL:
			do_getqctrl();
			break;

		case AC_ARG_GETQDELAY:
			do_getqdelay();
			break;

		case AC_ARG_GETQHIWATER:
			do_getqhiwater();
			break;

		case AC_ARG_GETQLOWATER:
			do_getqlowater();
			break;

		case AC_ARG_GETSTAT:
			do_getstat();
			break;

		case AC_ARG_GETTERMID:
			do_gettermid();
			break;

		case AC_ARG_GETUSERAUDIT:
			++argv;
			do_getuseraudit(*argv);
			break;

		case AC_ARG_LSEVENT:
			do_lsevent();
			break;

		case AC_ARG_LSPOLICY:
			do_lspolicy();
			break;

		case AC_ARG_SETASID:
			{ char *sid_str;
			  ++argv;
			  sid_str = *argv;
			  ++argv;
			  do_setasid(sid_str,argv);
			}
			break;

		case AC_ARG_SETAUID:
			{ char *user;
			  ++argv;
			  user = *argv;
			  ++argv;
			  do_setauid(user,argv);
			}
			break;

		case AC_ARG_SETAUDIT:
			{ char *user_str;
			  char *mask_str;
			  char *tid_str;
			  char *sid_str;
			  ++argv;
			  user_str = *argv;
			  ++argv;
			  mask_str = *argv;
			  ++argv;
			  tid_str = *argv;
			  ++argv;
			  sid_str = *argv;
			  ++argv;
			  do_setaudit(user_str,mask_str,tid_str,sid_str,argv);
			}
			break;

		case AC_ARG_SETCOND:
			++argv;
			do_setcond(*argv);
			break;

		case AC_ARG_SETCLASS:
		case AC_ARG_SETESTATE:
			{ char *event_str, *audit_flags;
			++argv; event_str = *argv;
			++argv; audit_flags = *argv;
			do_setclass(event_str, audit_flags);
			}
			break;

		case AC_ARG_SETKERNSTATE:
		case AC_ARG_SETKMASK:
			++argv;
			do_setkmask(*argv);
			break;

		case AC_ARG_SETPOLICY:
			++argv;
			do_setpolicy(*argv);
			break;

		case AC_ARG_GETPINFO:
			{ char *pid_str;
			  ++argv;
			  pid_str = *argv;
			  do_getpinfo(pid_str);
			}
			break;

		case AC_ARG_SETPMASK:
			{ char *pid_str;
			  char *audit_flags;
			  ++argv;
			  pid_str = *argv;
			  ++argv;
			  audit_flags = *argv;
			  do_setpmask(pid_str, audit_flags);
			}
			break;

		case AC_ARG_SETSTAT:
			do_setstat();
			break;

		case AC_ARG_SETQBUFSZ:
			++argv;
			do_setqbufsz(*argv);
			break;

		case AC_ARG_SETQCTRL:
			{ char *hiwater, *lowater, *bufsz, *delay;
			++argv; hiwater = *argv;
			++argv; lowater = *argv;
			++argv; bufsz = *argv;
			++argv; delay = *argv;
			do_setqctrl(hiwater, lowater, bufsz, delay);
			}
			break;
				
		case AC_ARG_SETQDELAY:
			++argv;
			do_setqdelay(*argv);
			break;

		case AC_ARG_SETQHIWATER:
			++argv;
			do_setqhiwater(*argv);
			break;

		case AC_ARG_SETQLOWATER:
			++argv;
			do_setqlowater(*argv);
			break;

		case AC_ARG_SETTERMID:
			++argv;
			do_settid(*argv);
			break;

		case AC_ARG_SETUSERAUDIT:
			{ char *user;
			  char *aflags;
			  ++argv;
			  user = *argv;
			  ++argv;
			  aflags = *argv;
			  do_setuseraudit(user,aflags);
			}
			break;
		case AC_ARG_SETSMASK:
			{
				char *asid_str;
				char *audit_flags;
				++argv;
				asid_str = *argv;
				++argv;
				audit_flags = *argv;
				do_setsmask(asid_str, audit_flags);
			}
			break;
		case AC_ARG_SETUMASK:
			{
				char *auid_str;
				char *audit_flags;
				++argv;
				auid_str = *argv;
				++argv;
				audit_flags = *argv;
				do_setumask(auid_str, audit_flags);
			}
			break;
		case AC_ARG_GETFSIZE:
			do_getfsize();
			break;
		case AC_ARG_SETFSIZE:
			++argv;
			do_setfsize(*argv);
			break;
			
		default:
			exit_error("Internal error #2.");
			break;

		}
	}

	return;
}

static void
do_chkconf()
{
	register au_event_ent_t *evp;
	au_mask_t pmask;
	char conf_aflags[256];
	char run_aflags[256];
	au_stat_t as;
	int class;
	int			len;
	struct au_evclass_map	cmap;
	
	pmask.am_success = pmask.am_failure = 0;
	eauditon(A_GETSTAT, (caddr_t)&as, NULL);

	setauevent();
	if ((evp = getauevent()) == (au_event_ent_t *)NULL) {
		endauevent();
		(void)exit_error(gettext(
			"NO AUDIT EVENTS: Could not read %s\n."),
			AUDITEVENTFILE);
	}
	endauevent();

	setauevent();
	while((evp = getauevent()) != (au_event_ent_t *)NULL) {
		cmap.ec_number = evp->ae_number;
		len = sizeof(struct au_evclass_map);
		if (evp->ae_number <= as.as_numevent)
			if (auditon(A_GETCLASS, (caddr_t) &cmap, len) == -1) {
				(void)printf("%s(%d):%s", 
				evp->ae_name, evp->ae_number, gettext(
"UNKNOWN EVENT: Could not get class for event. Configuration may be bad.\n"));
			} else {
				class = cmap.ec_class;
				if (class != evp->ae_class) {
					conf_aflags[0] = run_aflags[0] = '\0';
					pmask.am_success = class;
					pmask.am_failure = class;
					(void)getauditflagschar(run_aflags, 
						&pmask, 0);
					pmask.am_success = evp->ae_class;
					pmask.am_failure = evp->ae_class;
					(void)getauditflagschar(conf_aflags, 
						&pmask, 0);

					(void)printf(gettext(
"%s(%d): CLASS MISMATCH: runtime class (%s) != configured class (%s)\n"),
					evp->ae_name, evp->ae_number,
					NONE(run_aflags), NONE(conf_aflags));
				}
			}
	}
	endauevent();

	return;
}

static void
do_conf()
{
	register au_event_ent_t *evp;
	register int i;
	au_evclass_map_t ec;
	au_stat_t as;

	eauditon(A_GETSTAT, (caddr_t)&as, NULL);

	i=0;
	setauevent();
	while((evp = getauevent()) != (au_event_ent_t *)NULL) {
		if (evp->ae_number <= as.as_numevent) {
			++i;
			ec.ec_number = evp->ae_number;
			ec.ec_class = evp->ae_class;
			eauditon(A_SETCLASS, (caddr_t)&ec, sizeof(ec));
		}
	}
	endauevent();
	(void)printf(gettext("Configured %d kernel events.\n"),i);

	return;
}

static void
do_audit(event, sorf, retval, audit_str)
	char *event;
	char sorf;
	int retval;
	char *audit_str;
{
	int rtn;
	int rd;
	au_event_t event_num;
	au_event_ent_t *evp;
	auditinfo_t ai;
	token_t *tokp;

	egetaudit(&ai);

	if (strisnum(event)) {
		event_num = (au_event_t)atoi(event);
		evp = egetauevnum(event_num);
	} else
		evp = egetauevnam(event);

	rtn = au_preselect(evp->ae_number, &ai.ai_mask, (int)sorf,
		AU_PRS_USECACHE);

	if (rtn == -1)
		exit_error("%s\n%s %d\n",
			gettext("Check audit event configuration."),
			gettext("Could not get audit class for event number"),
			evp->ae_number);

	/* record is preselected */
	if (rtn == 1) {
		if ((rd = au_open()) == -1)
			exit_error(gettext(
				"Could not get and audit record descriptor\n"));
		if ((tokp = au_to_me()) == (token_t *)NULL)
			exit_error(gettext(
				"Could not allocate subject token\n"));
		if ((tokp = au_to_text(audit_str)) == (token_t *)NULL)
			exit_error(gettext("Could not allocate text token\n"));
		if (au_write(rd,tokp) == -1)
exit_error(gettext("Could not construct text token of audit record\n"));
		if ((tokp = au_to_return(sorf, retval)) == (token_t *)NULL)
			exit_error(gettext(
				"Could not allocate return token\n"));
		if (au_write(rd,tokp) == -1)
			exit_error(gettext(
			"Could not construct return token of audit record\n"));
		if (au_close(rd, 1, evp->ae_number) == -1)
			exit_error(gettext(
				"Could not write audit record: %s\n"),
					strerror(errno));
		}
				
		return;
	}

static void
do_getauid()
{
	au_id_t auid;

	egetauid(&auid);
	print_auid(auid);

	return;
}

static void
do_getaudit()
{
	auditinfo_t ai;

	egetaudit(&ai);
	print_auid(ai.ai_auid);
	print_mask(gettext("process preselection mask"), &ai.ai_mask);
	print_tid(&ai.ai_termid);
	print_asid(ai.ai_asid);

	return;
}

static void
do_getcar() 
{
	char path[MAXPATHLEN];

	eauditon(A_GETCAR, (caddr_t)path, ((int)sizeof(path)));
	(void)printf(gettext("current active root = %s\n"), path);

	return;
}

static void
do_getclass(event_str)
	char *event_str;
{
	au_evclass_map_t ec;
	/* au_mask_t pmask; */ /* not used */
	au_event_ent_t *evp;
	au_event_t event_number;
	char *event_name;
	char desc[256];

	setauevent();
	if (strisnum(event_str)) {
		event_number = atol(event_str);
		if ((evp = egetauevnum(event_number)) != (au_event_ent_t *)NULL) {
			event_number = evp->ae_number;
			event_name = evp->ae_name;
		} else
			event_name = "unknown";
	} else {
		event_name = event_str;
		if ((evp = egetauevnam(event_str)) != (au_event_ent_t *)NULL)
			event_number = evp->ae_number;
	}
	endauevent();

	ec.ec_number = event_number;
	eauditon(A_GETCLASS, (caddr_t)&ec, NULL);

	/* pmask not used any more */
	/* pmask.am_success = pmask.am_failure = ec.ec_class; */
	(void)sprintf(desc, gettext("audit class mask for event %s(%d)"),
			event_name, event_number);
	print_mask1(desc, ec.ec_class);

	return;
}

static void
do_getcond()
{
	char cond_str[16];
	u_long cond;

	eauditon(A_GETCOND, (caddr_t)&cond, sizeof(cond));

	(void)cond2str(cond, cond_str);
	(void)printf(gettext("audit condition = %s\n"), cond_str);

	return;
}

static void
do_getcwd()
{
	char path[MAXPATHLEN];

	eauditon(A_GETCWD, (caddr_t)path, ((int)sizeof(path)));
	(void)printf(gettext("current working directory = %s\n"), path);

	return;
}

static void
do_getkmask()
{
	au_mask_t pmask;

	eauditon(A_GETKMASK, (caddr_t)&pmask, sizeof(pmask));
	print_mask(gettext("audit flags for non-attrib events"), &pmask);

	return;
}

static void
do_getpolicy()
{
	char policy_str[1024];
	u_long policy;

	eauditon(A_GETPOLICY, (caddr_t)&policy, NULL);
	(void)policy2str(policy, policy_str);
	(void)printf(gettext("audit policies = %s\n"), policy_str);

	return;
}

static void
do_getpinfo( pid_str)
	char *pid_str;
{
	struct auditpinfo ap;

	if (strisnum(pid_str))
		ap.ap_pid = (pid_t)atoi(pid_str);
	else
		exit_usage(1);

	eauditon( A_GETPINFO, (caddr_t) &ap, 0);

	print_auid( ap.ap_auid );
	print_mask(gettext("process preselection mask"), &(ap.ap_mask) );
	print_tid( &(ap.ap_termid) );
	print_asid( ap.ap_asid);

	return;
}

static void
do_getqbufsz()
{
	struct au_qctrl qctrl;
	
	eauditon(A_GETQCTRL, (caddr_t)&qctrl, NULL);
	(void)printf(gettext("audit queue buffer size (bytes) = %ld\n"),
		qctrl.aq_bufsz);

	return;
}

static void
do_getqctrl()
{
	struct au_qctrl qctrl;
	eauditon(A_GETQCTRL, (caddr_t)&qctrl, NULL);
	(void)printf(gettext("audit queue hiwater mark (records) = %ld\n"),
		qctrl.aq_hiwater);
	(void)printf(gettext("audit queue lowater mark (records) = %ld\n"),
		qctrl.aq_lowater);
	(void)printf(gettext("audit queue buffer size (bytes) = %ld\n"),
		qctrl.aq_bufsz);
	(void)printf(gettext("audit queue delay (ticks) = %ld\n"),
		qctrl.aq_delay);

	return;
}

static void
do_getqdelay()
{
	struct au_qctrl qctrl;
	
	eauditon(A_GETQCTRL, (caddr_t)&qctrl, NULL);
	(void)printf(gettext("audit queue delay (ticks) = %ld\n"),
		qctrl.aq_delay);

	return;
}

static void
do_getqhiwater()
{
	struct au_qctrl qctrl;
	
	eauditon(A_GETQCTRL, (caddr_t)&qctrl, NULL);
	(void)printf(gettext("audit queue hiwater mark (records) = %ld\n"),
		qctrl.aq_hiwater);

	return;
}

static void
do_getqlowater()
{
	struct au_qctrl qctrl;
	
	eauditon(A_GETQCTRL, (caddr_t)&qctrl, NULL);
	(void)printf(gettext("audit queue lowater mark (records) = %ld\n"),
		qctrl.aq_lowater);

	return;
}

static void
do_getasid()
{
	auditinfo_t ai;

	if (getaudit(&ai)) {
		exit_error(gettext("getaudit(2) failed"));
	}
	print_asid(ai.ai_asid);

	return;
}

static void
do_getstat()
{
	au_stat_t as;

	eauditon(A_GETSTAT, (caddr_t)&as, NULL);
	print_stats(&as);

}

static void
do_gettermid()
{
	auditinfo_t ai;

	if (getaudit(&ai)) {
		exit_error(gettext("getaudit(2) failed"));
	}
	print_tid(&ai.ai_termid);

	return;
}

static void
do_getfsize()
{
	au_fstat_t fstat;

	eauditon(A_GETFSIZE, (caddr_t)&fstat, NULL);
	(void)printf(gettext("Maximum file size %ld, current file size %ld\n"),
		fstat.af_filesz, fstat.af_currsz);
	return;
}
	
/* keep lint from complaining about unsed args */
/*ARGSUSED*/
static void
do_getuseraudit(user)
char *user;
{
#ifdef SunOS_CMW

	char desc[256];
	au_mask_t pmask;
	au_id_t auid;

	auid = (au_id_t)get_user_id(user);
	egetuseraudit(auid, &pmask);
	(void)sprintf(desc, "process preselection mask for %s", user);
	print_mask(desc, &pmask);

#else /* !SunOS_CMW */

	(void) printf("-getuseraudit supported on SunOS CMW only.\n");

#endif /* SunOS_CMW */

	return;
}

static void
do_lsevent()
{
	register au_event_ent_t *evp;
	au_mask_t pmask;
	char auflags[256];

	setauevent();
	if ((evp = getauevent()) == (au_event_ent_t *)NULL) {
		endauevent();
		(void)exit_error(gettext(
			"NO AUDIT EVENTS: Could not read %s\n."),
			AUDITEVENTFILE);
	}
	endauevent();

	setauevent();
	while((evp = getauevent()) != (au_event_ent_t *)NULL) {
		pmask.am_success = pmask.am_failure = evp->ae_class;
		if (getauditflagschar(auflags, &pmask, 0) == -1)
			(void)strcpy(auflags, "unknown");
		(void)printf("%-30s %5d %s %s\n",
			evp->ae_name, evp->ae_number, auflags, evp->ae_desc);
	}
	endauevent();

	return;
}

static void
do_lspolicy()
{
	register int i;

	(void)printf("policy string    description:\n");
	for (i=0; i<POLICY_TBL_SZ; i++)
		(void)printf("%-17s%s\n", 
			policy_table[i].policy_str,
			gettext(policy_table[i].policy_desc));

	return;
}

static void
do_setasid(sid_str, argv)
char *sid_str;
char **argv;
{
	struct auditinfo ai;

	if (getaudit(&ai)) {
		exit_error(gettext("getaudit(2) failed"));
	}
	ai.ai_asid = (au_asid_t)atol(sid_str);
	if (setaudit(&ai)) {
		exit_error(gettext("setaudit(2) failed"));
	}
	execit(argv);

	return;
}

static void
do_setaudit(user_str,mask_str,tid_str,sid_str,argv)
	char *user_str;
	char *mask_str;
	char *tid_str;
	char *sid_str;
	char **argv;
{
	struct auditinfo ai;

	ai.ai_auid = (au_id_t)get_user_id(user_str);
	str2mask(mask_str,&ai.ai_mask),
	str2tid(tid_str,&ai.ai_termid);
	ai.ai_asid = (au_asid_t)atol(sid_str);

	esetaudit(&ai);
	execit(argv);

	return;
}

static void
do_setauid(user,argv)
	char *user;
	char **argv;
{
	au_id_t auid;

	auid = get_user_id(user);
	esetauid(&auid);
	execit(argv);

	return;
}

static void
do_setpmask(pid_str, audit_flags)
	char *pid_str;
	char *audit_flags;
{
	struct auditpinfo ap;

	if (strisnum(pid_str))
		ap.ap_pid = (pid_t)atoi(pid_str);
	else
		exit_usage(1);

	str2mask(audit_flags, &ap.ap_mask);

	eauditon(A_SETPMASK, (caddr_t) &ap, sizeof(ap));

	return;
}

static void
do_setsmask(asid_str, audit_flags)
	char *asid_str;
	char *audit_flags;
{
	struct auditinfo ainfo;

	if (strisnum(asid_str))
		ainfo.ai_asid = (pid_t)atoi(asid_str);
	else
		exit_usage(1);

	str2mask(audit_flags, &ainfo.ai_mask);

	eauditon(A_SETSMASK, (caddr_t) &ainfo, sizeof(ainfo));

	return;
}
	
static void
do_setumask(auid_str, audit_flags)
	char *auid_str;
	char *audit_flags;
{
	struct auditinfo ainfo;

	if (strisnum(auid_str))
		ainfo.ai_auid = (pid_t)atoi(auid_str);
	else
		exit_usage(1);

	str2mask(audit_flags, &ainfo.ai_mask);

	eauditon(A_SETUMASK, (caddr_t) &ainfo, sizeof(ainfo));

	return;
}
	
static void
do_setstat()
{
	au_stat_t as;

	as.as_audit	= (u_long) -1;
	as.as_auditctl	= (u_long) -1;
	as.as_dropped	= (u_long) -1;
	as.as_enqueue	= (u_long) -1;
	as.as_generated	= (u_long) -1;
	as.as_kernel	= (u_long) -1;
	as.as_nonattrib	= (u_long) -1;
	as.as_rblocked	= (u_long) -1;
	as.as_totalsize	= (u_long) -1;
	as.as_wblocked	= (u_long) -1;
	as.as_written	= (u_long) -1;

	eauditon(A_SETSTAT, (caddr_t)&as, sizeof(as));
	(void)puts(gettext("audit stats reset"));

	return;
}

/*ARGSUSED*/ /* keep lint from complaining about unused args */
static void
do_setuseraudit(user, auditflags)
	char *user;
	char *auditflags;
{
#ifdef SunOS_CMW

	au_mask_t pmask;
	au_id_t auid;

	auid = (au_id_t)get_user_id(user);
    	str2mask(auditflags, &pmask);
	esetuseraudit(auid, &pmask);

#else /* !SunOS_CMW */

	(void)printf("-setuseraudit supported on SunOS CMW only.\n");

#endif /* SunOS_CMW */

	return;
}

static void
do_setcond(cond_str)
char *cond_str;
{
	u_long cond;

	(void)str2cond(cond_str, &cond);

	(void)eauditon(A_SETCOND, (caddr_t)&cond, sizeof(cond));

	return;
}

static void
do_setclass(event_str, audit_flags)
	char *event_str;
	char *audit_flags;
{
	au_event_t event;
	int mask;
	au_mask_t pmask;
	au_evclass_map_t ec;
	au_event_ent_t *evp;

	if (strisnum(event_str))
		event = (u_long) atol(event_str);
	else {
                if ((evp = egetauevnam(event_str)) != (au_event_ent_t *)NULL)
                        event = evp->ae_number;
	}

	if (strisnum(audit_flags))
		mask = atoi(audit_flags);
	else {
 		str2mask(audit_flags, &pmask);
		mask = pmask.am_success | pmask.am_failure;
	}

	ec.ec_number = event;
	ec.ec_class = mask;
	eauditon(A_SETCLASS, (caddr_t)&ec, sizeof(ec));

	return;
}

static void
do_setkmask(audit_flags)
char *audit_flags;
{
	au_mask_t pmask;

 	str2mask(audit_flags, &pmask);
	eauditon(A_SETKMASK, (caddr_t)&pmask, sizeof(pmask));
	print_mask(gettext("audit flags for non-attrib events"), &pmask);

	return;
}

static void
do_setpolicy(policy_str)
char *policy_str;
{
	u_long policy;

	(void)str2policy(policy_str, &policy);

	eauditon(A_SETPOLICY, (caddr_t)&policy, NULL);

	return;
}

static void
do_setqbufsz(bufsz)
char *bufsz;
{
	struct au_qctrl qctrl;
	
	eauditon(A_GETQCTRL, (caddr_t)&qctrl, NULL);
	qctrl.aq_bufsz = atol(bufsz);
	eauditon(A_SETQCTRL, (caddr_t)&qctrl, NULL);

	return;
}


static void
do_setqctrl(hiwater, lowater, bufsz, delay)
	char *hiwater;
	char *lowater;
	char *bufsz;
	char *delay;
{
	struct au_qctrl qctrl;
	
	qctrl.aq_hiwater = atol(hiwater);
	qctrl.aq_lowater = atol(lowater);
	qctrl.aq_bufsz = atol(bufsz);
	qctrl.aq_delay = atol(delay);
	eauditon(A_SETQCTRL, (caddr_t)&qctrl, NULL);

	return;
}

static void
do_setqdelay(delay)
char *delay;
{
	struct au_qctrl qctrl;
	
	eauditon(A_GETQCTRL, (caddr_t)&qctrl, NULL);
	qctrl.aq_delay = atol(delay);
	eauditon(A_SETQCTRL, (caddr_t)&qctrl, NULL);

	return;
}

static void
do_setqhiwater(hiwater)
char *hiwater;
{
	struct au_qctrl qctrl;
	
	eauditon(A_GETQCTRL, (caddr_t)&qctrl, NULL);
	qctrl.aq_hiwater = atol(hiwater);
	eauditon(A_SETQCTRL, (caddr_t)&qctrl, NULL);

	return;
}

static void
do_setqlowater(lowater)
	char *lowater;
{
	struct au_qctrl qctrl;
	
	eauditon(A_GETQCTRL, (caddr_t)&qctrl, NULL);
	qctrl.aq_lowater = atol(lowater);
	eauditon(A_SETQCTRL, (caddr_t)&qctrl, NULL);

	return;
}

static void
do_settid(tid_str)
char *tid_str;
{
	struct auditinfo ai;

	if (getaudit(&ai)) {
		exit_error("getaudit(2) failed");
	}
	str2tid(tid_str, &ai.ai_termid);
	if (setaudit(&ai)) {
		exit_error("setaudit(2) failed");
	}

	return;
}

static void
do_setfsize(size)
	char *size;
{
	au_fstat_t fstat;

	fstat.af_filesz = atol(size);
	eauditon(A_SETFSIZE, (caddr_t)&fstat, NULL);
	return;
}
	
static void
eauditon(cmd, data, length)
	int cmd;
	caddr_t data;
	int length;
{
	if (auditon(cmd, data, length) == -1)
		exit_error(gettext("auditon(2) failed."));

	return;
}

static void
egetauid(auid)
	au_id_t *auid;
{
	if (getauid(auid) == -1)
		exit_error(gettext("getauid(2) failed."));

	return;
}

static void
egetaudit(ai)
	auditinfo_t *ai;
{
	if (getaudit(ai) == -1)
		exit_error(gettext("getaudit(2) failed."));

	return;
}

static void
egetauditflagsbin(auditflags, pmask)
	char *auditflags;
	au_mask_t *pmask;
{
	pmask->am_success = pmask->am_failure = 0;

	if (strcmp(auditflags, "none") == 0)
		return;
	
	if (getauditflagsbin(auditflags, pmask) < 0) {
		exit_error(gettext("Could not get audit flags (%s)"),
			auditflags);
	}

	return;
}

static au_event_ent_t *
egetauevnum(event_number)
	au_event_t event_number;
{
	register au_event_ent_t *evp;

	setauevent();
	if ((evp = getauevnum(event_number)) == (au_event_ent_t *)NULL)
		exit_error(gettext("Could not get audit event %d"),
			event_number);
	endauevent();

	return evp;
}

static au_event_ent_t *
egetauevnam(event_name)
	char *event_name;
{
	register au_event_ent_t *evp;

	setauevent();
	if ((evp = getauevnam(event_name)) == (au_event_ent_t *)NULL)
		exit_error(gettext("Could not get audit event %s"), event_name);
	endauevent();

	return evp;
}
		
#ifdef SunOS_CMW 
static void
egetuseraudit(uid, pmask)
	uid_t uid;
	au_mask_t *pmask;
{
	if (getuseraudit(uid, pmask) == -1)
		exit_error("getuseraudit(2) failed");

	return;
}
#endif /* SunOS_CMW */

/*
 * Not used any more
 * static pid_t
 * egetppid()
 * {
 * 	pid_t pid;
 * 	if ((pid = getppid()) == (pid_t)-1)
 * 		exit_error("getppid(2) failed");
 *
 *	return pid;
 * }
 */

static void
esetauid(auid)
	au_id_t *auid;
{
	if (setauid(auid) == -1)
		exit_error(gettext("setauid(2) failed."));

	return;
}

static void
esetaudit(ai)
	auditinfo_t *ai;
{
	if (setaudit(ai) == -1)
		exit_error(gettext("setaudit(2) failed."));

	return;
}

#ifdef SunOS_CMW
static void
esetuseraudit(uid,pmask)
	uid_t uid;
	au_mask_t *pmask;
{
	if (setuseraudit(uid,pmask) == -1)
		exit_error("setuseraudit(2) failed.");

	return;
}
#endif /* SunOS_CMW */

static uid_t
get_user_id(user)
	char *user;
{
	struct passwd *pwd;
	uid_t uid;

	setpwent();
	if (isdigit(*user)) {
		uid = atoi(user);
		if ((pwd = getpwuid(uid)) == (struct passwd *)NULL) {
			exit_error(gettext("Invalid user: %s"), user);
		}
	} else {
		if ((pwd = getpwnam(user)) == (struct passwd *)NULL) {
			exit_error(gettext("Invalid user: %s"), user);
		}
	}
	endpwent();

	return pwd->pw_uid;
}

/*
 * get_arg_ent()
 *     Inputs: command line argument string
 *     Returns ptr to policy_entry if found; null, if not found
 */
static struct arg_entry *
get_arg_ent(arg_str)
	char *arg_str;
{
	struct arg_entry key;
	
	key.arg_str = arg_str;

	return (struct arg_entry *) bsearch((char *)&key, 
                                            (char *)arg_table, ARG_TBL_SZ,
		                            sizeof(struct arg_entry), 
                                            arg_ent_compare);
}

/*
 * arg_ent_compare()
 *     Compares two command line arguments to determine which is 
 *       lexicographically greater.
 *     Inputs: two argument map table entry pointers
 *     Returns: > 1: aep1->arg_str > aep2->arg_str
 *              < 1: aep1->arg_str < aep2->arg_str
 *                0: aep1->arg_str = aep->arg_str2
 */
static int 
arg_ent_compare(aep1, aep2)
struct arg_entry *aep1, *aep2;
{
	return strcmp(aep1->arg_str, aep2->arg_str);
}

/*
 * Convert mask of the following forms:
 *
 *    audit_flags (ie. +lo,-ad,pc)
 *    0xffffffff,0xffffffff
 *    ffffffff,ffffffff
 *    20,20
 */
static void
str2mask(mask_str, mp)
	char *mask_str;
	au_mask_t *mp;
{

	char sp[256];
	char fp[256];
		
	mp->am_success = 0;
	mp->am_failure = 0;

	/*  
	 * a mask of the form +aa,bb,cc,-dd 
	 */
	if (strisflags(mask_str)) {
		egetauditflagsbin(mask_str, mp);
	/* 
	 * a mask of the form 0xffffffff,0xffffffff or 1,1 
	 */
	} else { 		
		strsplit(mask_str,sp,fp,',');

		if (strlen(sp) > (size_t)2 && !strncasecmp(sp, "0x", 2)) 
				(void)sscanf(sp+2, "%x", &mp->am_success);
		else
			(void)sscanf(sp, "%u", &mp->am_success);
			
		if (strlen(fp) > (size_t)2 && !strncasecmp(fp, "0x", 2))
				(void)sscanf(fp+2, "%x", &mp->am_failure);
		else
			(void)sscanf(fp, "%u", &mp->am_failure);
	}

	return;
}

static void
str2tid(tid_str, tp)
	char *tid_str;
	au_tid_t *tp;
{
	char *major_str = (char *)NULL;
	char *minor_str = (char *)NULL;
	char *host_str = (char *)NULL;
	major_t major = 0;
	major_t minor = 0;
	dev_t dev = 0;
	struct hostent *phe;

	tp->port = 0;
	tp->machine = 0;

	major_str = tid_str;
	if((minor_str = strchr(tid_str, ',')) != (char *)NULL) {
		*minor_str = '\0';
		minor_str++;
	}

	if (minor_str)
		if((host_str = strchr(minor_str, ',')) != (char *)NULL) {
			*host_str = '\0';
			host_str++;
		}

	if (major_str)
		major = (major_t)atoi(major_str);

	if (minor_str)
		minor = (major_t)atoi(major_str);

	if ((dev = makedev(major, minor)) != NODEV)
		tp->port = dev;

	if (host_str)
		if (strisipaddr(host_str))
			tp->machine = inet_addr(host_str);
		else
			if ((phe = gethostbyname(host_str)) != (struct hostent *)NULL)
				(void)memcpy(&tp->machine, 
						phe->h_addr_list[0], 
						sizeof(tp->machine));

	return;
}

static int
str2cond(cond_str, cond)
	char *cond_str;
	u_long *cond;
{
	char *buf = strdup(cond_str);

	*cond = 0;

	(void)strtolower(buf);

	if (!strcmp(buf, "auditing")) {
		*cond = AUC_AUDITING;
		free(buf);
		return 0;
	}

	if (!strcmp(buf, "noaudit")) {
		*cond = AUC_NOAUDIT;
		free(buf);
		return 0;
	}

	if (!strcmp(buf, "noaudit")) {
		*cond = AUC_NOAUDIT;
		free(buf);
		return 0;
	}

	return 1;
}

static int
cond2str(cond, cond_str)
	u_long cond;
	char *cond_str;
{
	*cond_str = '\0';

	if (cond == AUC_AUDITING) {
		(void)strcpy(cond_str, "auditing");
		return 0;
	}

	if (cond == AUC_NOAUDIT) {
		(void)strcpy(cond_str, "noaudit");
		return 0;
	}

	if (cond == AUC_UNSET) {
		(void)strcpy(cond_str, "unset");
		return 0;
	}

	return 1;
}

static struct policy_entry *
get_policy_ent(policy)
	char *policy;
{
	register int i;

	for (i = 0; i < POLICY_TBL_SZ; i++)
		if (!strcmp(strtolower(policy),policy_table[i].policy_str))
			return (&policy_table[i]);

	return (struct policy_entry *)NULL;
}

static int
str2policy(policy_str, policy_mask)
	char *policy_str;
	u_long *policy_mask;
{
	char *buf;
	char *tok;
	char pfix;
	u_long pm = 0;
	u_long curp = 0;
	struct policy_entry *pep;

	pfix = *policy_str;

	if (pfix == '-' || pfix == '+' || pfix == '=')
		++policy_str;

	if ((buf = strdup(policy_str)) == (char *)NULL)
		return 1;

	for(tok=strtok(buf,","); tok != (char *)NULL; tok=strtok((char *)NULL,",")) {
		if ((pep = get_policy_ent(tok)) == (struct policy_entry *)NULL)
			return 1;
		else
			pm |= pep->policy_mask;
	}

	free(buf);

	if (pfix == '-') {
		eauditon(A_GETPOLICY, (caddr_t)&curp, NULL);
		*policy_mask = curp & ~pm;
	} else if (pfix == '+') {
		eauditon(A_GETPOLICY, (caddr_t)&curp, NULL);
		*policy_mask = curp | pm;
	} else
		*policy_mask = pm;

	return 0;
}

static int
policy2str(policy, policy_str)
	u_long policy;
	char *policy_str;
{
	register int i,j;

	if (policy == ALL_POLICIES) {
		(void)strcpy(policy_str, "all");
		return 1;
	}

	if (policy == NO_POLICIES) {
		(void)strcpy(policy_str, "none");
		return 1;
	}

	*policy_str = '\0';

	for (i = 0, j = 0; i < POLICY_TBL_SZ; i++)
		if (policy & policy_table[i].policy_mask &&
		    policy_table[i].policy_mask != ALL_POLICIES) {
			if (j++)
				(void)strcat(policy_str, ",");
			(void)strcat(policy_str, policy_table[i].policy_str);
		}

	if (*policy_str)
		return 0;

	return 1;
}

/*
 * static int
 * strishex(s)
 *	char *s;
 * {
 *	if (s == (char *)NULL || !*s)
 *		return 0;
 *
 *	for (; *s == '-' || *s == '+'; s++)
 *
 *	if (!*s)
 *		return 0;
 *
 *	for (; *s; s++)
 *		if (!isxdigit(*s))
 *			return 0;
 *
 *	return 1;
 * }
 */

static int
strisnum(s)
	char *s;
{
	if (s == (char *)NULL || !*s)
		return 0;

	for (; *s == '-' || *s == '+'; s++)

	if (!*s)
		return 0;

	for (; *s; s++)
		if (!isdigit(*s))
			return 0;

	return 1;
}	

static int
strisflags(s)
	char *s;
{
	if (s == (char *)NULL || !*s)
		return 0;

	for (; *s; s++) {
		if (!isalpha(*s) &&
		   (*s != '+' && *s != '-' && *s != '^' && *s != ','))
			return 0;
	}

	return 1;
}

static int
strisipaddr(s)
	char *s;
{
	if (s == (char *)NULL || !*s)
		return 0;

	for (; *s; s++)
		if (!isdigit(*s) || *s != '.')
			return 0;

	return 1;
}

static void
strsplit(s,p1,p2,c)
	register char *s;
	register char *p1;
	register char *p2;
	register char c;
{
	*p1 = *p2 = '\0';

	while(*s != '\0' && *s != c)
		*p1++ = *s++;
	*p1 = '\0';
	s++;

	while(*s != '\0')
		*p2++ = *s++;
	*p2 = '\0';
		
	return;
}

static char *
strtolower(s)
	char *s;
{
	char *save;

	for(save = s; *s; s++)
		(void)tolower(*s);

	return save;
}

static void
chk_event_num(etype,event)
	int etype;
	au_event_t event;
{
	au_stat_t as;

	eauditon(A_GETSTAT, (caddr_t)&as, NULL);

	if (etype == AC_KERN_EVENT) {
			
		if (event > as.as_numevent) {
			exit_error("%s\n%s: %ld %s 0-%ld.", 
			gettext("Invalid kernel audit event number specified."),
				progname, event,
				gettext(" is outside allowable range 0-"),
				as.as_numevent);
		}
	} else  { /* user event */
		if (event <= as.as_numevent) {
			exit_error(gettext(
			"Invalid user level audit event number specified %ld."),
				event);
		}
	}

	return;
}

static void
chk_event_str(etype, event_str)
	int etype;
	char *event_str;
{
	register au_event_ent_t *evp;
	au_stat_t as;

	eauditon(A_GETSTAT, (caddr_t)&as, NULL);

	setauevent();
	evp = egetauevnam(event_str);
	if (etype == AC_KERN_EVENT && (evp->ae_number > as.as_numevent)) {
		exit_error("%s\n%s: \"%s\" %s\n%s\n",
			gettext("Invalid kernel audit event string specified."),
			event_str,
			event_str,
			gettext(" appears to be a user level event."),
			gettext("Check configuration."));
	} else if (etype == AC_USER_EVENT && (evp->ae_number < as.as_numevent)) {
		exit_error("Invalid user audit event string specified.\n%s: \"%s\" appears to be a kernel event. Check configuration",event_str,event_str);
	}
	endauevent();

	return;
}

static void
chk_sorf(sorf_str)
	char *sorf_str;
{
	if (!strisnum(sorf_str))
		exit_error(gettext("Invalid sorf specified: %s"), sorf_str);

	return;
}

static void
chk_retval(retval_str)
	char *retval_str;
{
	if (!strisnum(retval_str))
		exit_error(gettext("Invalid retval specified: %s"), retval_str);

	return;
}

static void
chk_tid(tid_str)
	char *tid_str;
{
	register int c;
	register char *p;

	/* need two commas (maj,min,hostname) */


	for(p = tid_str, c = 0; *p; p++)
		if (*p == ',')
			++c;
	if (c != 2)
		exit_error(gettext("Invalid tid specified: %s"), tid_str);

	return;
}

static void
execit(argv)
	char **argv;
{
	char *shell;

        if (*argv)
	    (void)execvp(*argv, argv);
        else {
            if (((shell = getenv("SHELL")) == (char *)NULL) || *shell != '/') 
                shell = "/bin/csh";

	    (void)execlp(shell, shell, (char *)NULL);
	}

	exit_error("exec(2) failed");

	return;
}

/*
 * exit_error()
 *     Desc: Prints an error message along with corresponding system 
 *                  error number and error message, then exits.
 *     Inputs: Program name, program error message.
 */
/*VARARGS0*/
static void
exit_error(va_alist)
	va_dcl
{
	va_list args;
	char *fmt;
 
	(void)fprintf(stderr, "%s: ", progname);

	va_start(args);
	fmt = va_arg(args, char *);
	(void)vfprintf(stderr, fmt, args);
	va_end(args);

	(void)fputc('\n', stderr);
	if (errno)
		(void)fprintf(stderr, gettext("%s: error = %s(%d)\n"),
			progname, strerror(errno), errno);
	(void)fflush(stderr);

	exit(1);
}

static void
exit_usage(status)
	int status;
{
	register FILE *fp;
	register int i;

	fp = (status ? stderr : stdout);
	(void)fprintf(fp, gettext("usage: %s [args]\n"), progname);

	for (i = 0; i < ARG2_TBL_SZ; i++)
		(void)fprintf(fp, " %s %s\n",
			arg2_table[i].arg_str, arg2_table[i].arg_opts);

	exit(status);
}

static void
print_asid(asid)
	au_asid_t asid;
{
	(void)printf(gettext("audit session id = %ld\n"), asid);

	return;
}

static void
print_auid(auid)
	au_id_t auid;
{
	struct passwd *pwd;
	char *username;

	setpwent();
	if ((pwd = getpwuid((uid_t)auid)) != (struct passwd *)NULL)
		username = pwd->pw_name;
	else
		username = "unknown";
	endpwent();

	(void)printf(gettext("audit id = %s(%ld)\n"), username, auid);

	return;
}

static void
print_mask(desc, pmp)
	char *desc;
	au_mask_t *pmp;
{
	char auflags[512];

	if (getauditflagschar(auflags, pmp, NULL) < 0)
		(void)strcpy(auflags, "unknown");

	(void)printf("%s = %s(0x%x,0x%x)\n", 
		desc, auflags, pmp->am_success, pmp->am_failure);

	return;
}

static void
print_mask1(desc, mask1)
	char *desc;
	au_class_t	mask1;
{
	(void)printf("%s = 0x%x\n", desc, (int) mask1);

	return;
}

static void
print_stats(s)
	au_stat_t *s;
{
	int offset[12];   /* used to line the header up correctly */
	char buf[512];

	(void)sprintf(buf,
            "%4lu %n%4lu %n%4lu %n%4lu %n%4lu %n%4lu %n%4lu %n%4lu %n%4lu %n%4lu %n%4lu %n%4lu%n",
            s->as_generated,     &(offset[0]),
	    s->as_nonattrib,     &(offset[1]),
	    s->as_kernel,        &(offset[2]),
	    s->as_audit,         &(offset[3]),
	    s->as_auditctl,      &(offset[4]),
	    s->as_enqueue,       &(offset[5]),
	    s->as_written,       &(offset[6]),
	    s->as_wblocked,      &(offset[7]),
	    s->as_rblocked,      &(offset[8]),
	    s->as_dropped,       &(offset[9]),
	    s->as_totalsize/ONEK,&(offset[10]),
	    s->as_memused/ONEK,  &(offset[11]));

	/*
	 * TRANSLATION_NOTE
	 *	Print a properly aligned header.
	 */
	(void) printf("%*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s\n",
	   	offset[0]-1,             gettext("gen"),
	   	offset[1]-offset[0]-1,   gettext("nona"),
	   	offset[2]-offset[1]-1,   gettext("kern"),
	   	offset[3]-offset[2]-1,   gettext("aud"),
	   	offset[4]-offset[3]-1,   gettext("ctl"),
	   	offset[5]-offset[4]-1,   gettext("enq"),
	   	offset[6]-offset[5]-1,   gettext("wrtn"),
	   	offset[7]-offset[6]-1,   gettext("wblk"),
	   	offset[8]-offset[7]-1,   gettext("rblk"),
	   	offset[9]-offset[8]-1,   gettext("drop"),
	   	offset[10]-offset[9]-1,  gettext("tot"),
	   	offset[11]-offset[10],   gettext("mem"));

	(void)puts(buf);

	return;
}

static void
print_tid(tidp)
	au_tid_t *tidp;
{
	struct hostent *phe;
	char *hostname;
	struct in_addr ia;


	if ((phe = gethostbyaddr((char *)&tidp->machine, sizeof(tidp->machine), 
				AF_INET)) != (struct hostent *)NULL)
		hostname = phe->h_name;
	else
		hostname = "unknown";

	ia.s_addr = tidp->machine;

	(void)printf(gettext(
			"terminal id (maj,min,host) = %lu,%lu,%s(%s)\n"),
			major(tidp->port), minor(tidp->port),
			hostname, inet_ntoa(ia));

	return;
}
