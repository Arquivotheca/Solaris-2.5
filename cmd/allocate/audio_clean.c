/* Copyright (c) 1991 by Sun Microsystems, Inc. */

#pragma	ident	"@(#)audio_clean.c	1.9	94/10/14 SMI"

/*
 * audio_clean - Clear any residual data that may be residing in the
 *		 in the audio device driver or chip.
 *
 *		 Usage: audio_clean -[isf] device_name information_label
 *		 Note that currently the same operation is performed for
 *		 all three flags.  Also, information is ignored because
 *		 audio device does not involve removable media.  It and
 *		 support for the flags is provided here so that the framework
 *		 is place if added functionality is required.
 */

#include <locale.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <sys/audioio.h>

#include <stropts.h>
#include <sys/ioctl.h>

#define	TRUE 		1
#define	FALSE 		0
#define	NUM_ARGUMENTS	3
#define	error_msg	(void) fprintf		/* For lint */
#define	BUF_SIZE 512

static void clear_info();
static void clear_prinfo();
static void usage();
static void first_field();

/* Local variables */
static char *prog; /* Name program invoked with */
static char prog_desc[] = "Clean the audio device"; /* Used by usage message */
static char prog_opts[] = "ifs"; /* For getopt, flags */
static char dminfo_str[] = "dminfo -v -n"; /* Cmd to xlate name to device */

static char *Audio_dev = "/dev/audio";	/* Device name of audio device */
static char *Inf_label;			/* label supplied on cmd line */
static int Audio_fd = -1;		/* Audio device file desc. */

/* Global variables */
extern int	getopt();
extern int	optind;
extern char	*optarg;
extern int	errno;
extern char	*strcat();

/*
 * main()		Main parses the command line arguments,
 *			opens the audio device, and calls clear_info().
 *			to set the info structure to a known state, and
 *			then perfroms an ioctl to set the device to that
 *			state.
 *
 *			Note that we use the AUDIO_SETINFO ioctl instead
 *			of the low level AUDIOSETREG command which is
 *			used to perform low level operations on the device.
 *			If a process had previously used AUDIOSETREG to monkey
 *			with the device, then the driver would have reset the
 *			chip when the process performed a close, so we don't
 *			worry about this case
 */

main(argc, argv)
int	argc;
char	**argv;
{
	int	err = 0;
	struct stat st;
	audio_info_t	info;
	int	i;
	int	forced = 0;  /* Command line options */
	int	initial = 0;
	int	standard = 0;
	char	cmd_str[BUF_SIZE];
	char	map[BUF_SIZE];
	FILE * fp;

	prog = argv[0];		/* save program initiation name */

	/*
	 * Parse arguments.  Currently i, s and f all do the
	 * the same thing.
	 */

	if (argc != NUM_ARGUMENTS) {
		usage();
		goto error;
	}

	while ((i = getopt(argc, argv, prog_opts)) != EOF) {
		switch (i) {
		case 'i':
			initial = TRUE;
			if (standard || forced)
				err++;
			break;
		case 's':
			standard = TRUE;
			if (initial || forced)
				err++;
			break;
		case 'f':
			forced = TRUE;
			if (initial || standard)
				err++;
			break;
		case '?':
			err++;
			break;
		}
		if (err) {
			usage();
			exit(1);
		}
		argc -= optind;
		argv += optind;
	}

#ifdef NOTDEF
	Inf_label = argv[1]; /* Inf label not used now. This is for future */
	Inf_label = Inf_label;	/* For lint */
#endif

	*cmd_str = 0;
	(void) strcat(cmd_str, dminfo_str);
	(void) strcat(cmd_str, " ");
	(void) strcat(cmd_str, argv[0]); /* Agrv[0] is the name of the device */

	if ((fp = popen(cmd_str, "r")) == NULL) {
		error_msg(stderr, gettext("%s couldn't execute \"%s\"\n"), prog,
		    cmd_str);
		exit(1);
	}

	if (fread(map, 1, BUF_SIZE, fp) == 0) {
		error_msg(stderr, gettext("%s couldn't execute \"%s\"\n"), prog,
		    cmd_str);
		exit(1);
	}

	(void) pclose(fp);

	first_field(map, Audio_dev);  /* Put the 1st field in dev */

	/*
	 * Validate and open the audio device
	 */
	err = stat(Audio_dev, &st);

	if (err < 0) {
		error_msg(stderr, gettext("%s: cannot stat "), prog);
		perror(Audio_dev);
		exit(1);
	}
	if (!S_ISCHR(st.st_mode)) {
		error_msg(stderr, gettext("%s: %s is not an audio device\n"),
			prog, Audio_dev);
		exit(1);
	}

	/*
	 * Since the device /dev/audio can suspend if someone else is
	 * using it we check to see if we're going to hang before we
	 * do anything.
	 */
	/* Try it quickly, first */
	Audio_fd = open(Audio_dev, O_WRONLY | O_NDELAY);

	if ((Audio_fd < 0) && (errno == EBUSY)) {
		error_msg(stderr, gettext("%s: waiting for %s..."),
			prog, Audio_dev);
		(void) fflush(stderr);

		/* Now hang until it's open */
		Audio_fd = open(Audio_dev, O_WRONLY);
		if (Audio_fd < 0) {
			perror(Audio_dev);
			goto error;
		}
	} else if (Audio_fd < 0) {
		error_msg(stderr, gettext("%s: error opening "), prog);
		perror(Audio_dev);
		goto error;
	}


	/*
	 * Read the audio_info structure.
	 * Currently, we overwrite all these values that we get back,
	 * but this is a good test that GETINFO/SETINFO ioctls are
	 * supported.
	 */

	if (ioctl(Audio_fd, AUDIO_GETINFO, &info) != 0)  {
		perror("Ioctl error");
		goto error;
	}

	/*
	 * Clear the data structure. Clear_info set the info structure
	 * to a known state.
	 */
	clear_info(&info);

	if (ioctl(Audio_fd, AUDIO_SETINFO, &info) != 0) {
		perror(gettext("Ioctl error"));
		goto error;
	}


	(void) close(Audio_fd);			/* close output */
	exit(0);
	/*NOTREACHED*/
error:
	(void) close(Audio_fd);			/* close output */
	exit(1);
	/*NOTREACHED*/
}


/*
 * clear_info(info)	- Set the info structure to a known state.
 *			  Several of the field that are modified here are
 *			  read-only and will not be modified in the device
 *			  driver.  We set them all here for completeness.
 *			  Thess values were the values present after rebooting
 *			  a 4.1.1 rev B system.
 */

static void
clear_info(info)
audio_info_t *info;
{
	/* Clear output (play) side */
	clear_prinfo(&info->play);

	/* Clear input (record) side */
	clear_prinfo(&info->record);

	/* Clear other flags */
	info->monitor_gain = 0;

	/* Clear Reserved fields */
	info->_yyy[0] = 0;
	info->_yyy[1] = 0;
	info->_yyy[2] = 0;
	info->_yyy[3] = 0;
}


/*
 * clear_prinfo(prinfo)	- The prinfo struture is a subcomponent of the
 *			  info structure.  Once again, we set some values
 *			  to values that are read only and the driver won't
 *			  change. We do this out of paranoia.
 */

static void
clear_prinfo(prinfo)
audio_prinfo_t *prinfo;
{
	/* The following values decribe audio data encoding: */
	prinfo->sample_rate = 8000;
	prinfo->channels = 1;
	prinfo->precision = 8;
	prinfo->encoding = 1;

	/* The following values control audio device configuration */
	prinfo->gain = 127;
	prinfo->port = 1;
	prinfo->avail_ports = 0;

	/* These are Reserved for future use, but we clear them  */
	prinfo->_xxx[0] = 0;
	prinfo->_xxx[1] = 0;
	prinfo->_xxx[2] = 0;

	/* The following values describe driver state */
	prinfo->samples = 0;
	prinfo->eof = 0;
	prinfo->pause = 0;
	prinfo->error = 0;
	prinfo->waiting = 0;
	prinfo->balance = 0;

	/* The following values are read-only state flags */
	prinfo->open = 0;
	prinfo->active = 0;
}


/*
 * usage()		- Print usage message.
 */

static void
usage()
{
	error_msg(stderr,
		gettext("usage: %s [-s|-f|-i] device info_label\n"), prog);
}


/*
 * first_field(string, item)	- return the first substring in string
 *				  before the ':' in "item"
 */
static void
first_field(string, item)
char	*string, *item;
{
	item = string;

	while (*item != ':')
		item++;
	*item = 0;
}
