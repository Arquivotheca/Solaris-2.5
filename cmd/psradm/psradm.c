#ident	"@(#)psradm.c	1.3	92/09/23 SMI"

#include <sys/types.h>
#include <sys/procset.h>
#include <sys/processor.h>
#include <sys/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <utmp.h>

static char *	cmdname;	/* command name for messages */

static char	verbose;	/* non-zero if the -v option has been given */
static char	all_flag;	/* non-zero if the -a option has been given */
static char	log_open;	/* non-zero if openlog() has been called */

static struct utmp ut;		/* structure for logging to /etc/wtmp. */

static char * basename(char *);

static void
usage(void)
{
	fprintf(stderr, "usage: \n\t%s -f|-n [-v] processor_id ...\n"
		"\t%s -a -f|-n [-v]\n", cmdname, cmdname);
}

int
main(int argc, char *argv[])
{
	extern char *optarg;
	extern int optind, opterr, optopt;
	int	c;
	int	action = 0;
	processorid_t	cpu;
	int	ncpus;
	char	*errptr;
	int	errors;

	cmdname = basename(argv[0]);

	while ((c = getopt(argc, argv, "afnv")) != EOF) {
		switch (c) {

		case 'a':		/* applies to all possible CPUs */
			all_flag = 1;
			break;

		case 'f':
		case 'n':
			if (action != 0 && action != c) {
				fprintf(stderr, "%s: options -f and -n are "
				    "mutually exclusive.\n", cmdname);
				usage();
				return (2);
			}
			action = c;
			break;

		case 'v':
			verbose = 1;
			break;

		default:
			usage();
			return (2);
		}
	}

	switch (action) {
	case 'f':
		action = P_OFFLINE;
		break;
	case 'n':
		action = P_ONLINE;
		break;
	default:
		fprintf(stderr, "%s: option -f or -n must "
		    "be specified.\n", cmdname);
		usage();
		return (2);
	}

	errors = 0;
	if (all_flag) {
		if (argc != optind) {
			usage();
			return (2);
		}
		ncpus = sysconf(_SC_NPROCESSORS_CONF);
		if (ncpus < 0) {
			perror("sysconf _SC_NPROCESSORS_CONF");
			return (1);
		}
		for (cpu = 0; ncpus > 0; cpu++) {
			if (vary_cpu(cpu, action))
				ncpus--;
		}
	} else {
		argc -= optind;
		if (argc <= 0) {
			usage();	/* not enough arguments */
			return (2);
		}
		for (argv += optind; argc > 0; argv++, argc--) {
			cpu = strtol(*argv, &errptr, 10);
			if (errptr != NULL && *errptr != '\0') {
				fprintf(stderr, "%s: invalid processor ID %s\n",
					cmdname, *argv);
				errors = 2;
				continue;
			}
			if (vary_cpu(cpu, action) <= 0)
				errors = 1;
		}
	}
	if (log_open) {
		closelog();
	}
	return (errors);
}

/*
 * Turn CPU online or offline.
 *	Return non-zero if a processor was found.
 *	Print messages and update wtmp and the system log.
 *	If the all_flag is set, it is not an error if a processor isn't there.
 */

static int
vary_cpu(processorid_t cpu, int action)
{
	int	old_state;
	int	err;
	struct tm *tm;
	time_t	now;
	char	buf[80];

	old_state = p_online(cpu, action);
	if (old_state < 0) {
		if (errno == EINVAL && all_flag)
			return (0);	/* no such processor */
		err = errno;		/* in case sprintf smashes errno */
		sprintf(buf, "%s: processor %d", cmdname, cpu);
		errno = err;
		perror(buf);
		return (-1);
	}

	if (old_state == action) {
		if (verbose)
			printf("processor %d already %s.\n", cpu,
			    action == P_ONLINE ? "on-line" : "off-line");
		return (1);		/* no change */
	}

	sprintf(buf, "processor %d %s.", cpu,
		action == P_ONLINE ? "brought on-line" : "taken off-line");

	if (verbose)
		printf("%s\n", buf);

	/*
	 * Log the change.
	 */
	if (!log_open) {
		log_open = 1;
		openlog(cmdname, LOG_CONS, LOG_USER);	/* open syslog */
		(void) setlogmask(LOG_UPTO(LOG_INFO));

		ut.ut_pid = getpid();
		ut.ut_type = USER_PROCESS;
		strncpy(ut.ut_user, "psradm", sizeof (ut.ut_user) - 1);
	}

	syslog(LOG_INFO, buf);

	/*
	 * Update wtmp.
	 */
	sprintf(ut.ut_line, PSRADM_MSG, cpu, action == P_ONLINE ? "on" : "off");
	(void) time(&now);
	ut.ut_time = now;
	updwtmp(WTMP_FILE, &ut);

	return (1);	/* the processor exists and no errors occurred */
}


/*
 * Find base name of filename.
 */
static char *
basename(char *cp)
{
	char *sp;

	if ((sp = strrchr(cp, '/')) != NULL)
		return (sp + 1);
	return (cp);
}
