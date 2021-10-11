/*
 *	Copyright (c) 1991, Sun Microsystems, Inc.
 *	All Rights Reserved.
 */
#ident	"@(#)kbd.c	1.2	93/11/22 SMI"

/*
 *	Usage:	kbd [-r] [-t] [-c on|off] [-d keyboard device]
 *	-r			reset the keyboard as if power-up
 *	-t			return the type of the keyboard being used
 *	-c on|off		turn on|off clicking
 *	-d keyboard device	chooses the kbd device, default /dev/kbd.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/kbio.h>
#include <sys/kbd.h>
#include <stdio.h>
#include <fcntl.h>

#define	KBD_DEVICE	"/dev/kbd"	/* default keyboard device */

void reset();
void get_type();
void click();
void usage();

main (argc, argv)
	int argc;
	char **argv;
{
	int c;
	int rflag = 0;
	int tflag = 0;
	int cflag = 0;
	int dflag = 0;
	int errflag = 0;
	char *copt = NULL;
	char *dopt = NULL;
	extern char *optarg;
	extern int optind;

	while ((c = getopt(argc, argv, "rtc:d:")) != EOF) {
		switch (c) {
		case 'r':
			rflag++;
			break;
		case 't':
			tflag++;
			break;
		case 'c':
			copt = optarg;
			cflag++;
			break;
		case 'd':
			dopt = optarg;
			dflag++;
			break;
		case '?':
			errflag++;
			break;
		}
	}

	/*
	 * check for valid arguments
	 */
	if (errflag || (cflag && argc < 3) || (dflag && argc < 3) ||
	    argc != optind || argc == 1) {
		(void) usage();
		exit(1);
	}

	if (dflag && !rflag && !tflag && !cflag) {
		(void) usage();
		exit(1);
	}

	if (tflag) {
		(void) get_type(dopt);
	}
	if (cflag) {
		(void) click(copt, dopt);
	}
	if (rflag) {
		(void) reset(dopt);
	}

	exit(0);
}

/*
 * this routine resets the state of the keyboard as if power-up
 */
void
reset(dopt)
	char *dopt;
{
	int cmd;
	int fd;

	if (dopt == NULL) {
		if ((fd = open(KBD_DEVICE, O_WRONLY, 0)) < 0) {
			fprintf(stderr, "Cannot open %s\n", KBD_DEVICE);
			exit(1);
		}
	} else {
		if ((fd = open(dopt, O_WRONLY, 0)) < 0) {
			fprintf(stderr, "Cannot open %s\n", dopt);
			exit(1);
		}
	}

	cmd = KBD_CMD_RESET;

	if (ioctl(fd, KIOCCMD, &cmd)) {
		perror("kbd: ioctl error");
		exit(1);
	}

}

/*
 * this routine gets the type of the keyboard being used
 */
void
get_type(dopt)
	char *dopt;
{
	int kbd_type;
	int fd;

	if (dopt == NULL) {
		if ((fd = open(KBD_DEVICE, O_RDONLY, 0)) < 0) {
			fprintf(stderr, "Cannot open %s", KBD_DEVICE);
			exit(1);
		}
	} else {
		if ((fd = open(dopt, O_RDONLY, 0)) < 0) {
			fprintf(stderr, "Cannot open %s\n", dopt);
			exit(1);
		}
	}

	if (ioctl(fd, KIOCTYPE, &kbd_type)) {
		perror("kbd: ioctl error");
		exit(1);
	}

	switch (kbd_type) {

	case KB_SUN3:
		printf("Type 3 Sun keyboard\n");
		break;

	case KB_SUN4:
		printf("Type 4 Sun keyboard\n");
		break;

	case KB_ASCII:
		printf("ASCII\n");
		break;

	default:
		printf("Unknown keyboard type\n");
		break;
	}
}

/*
 * this routine enables or disables clicking of the keyboard
 */
void
click(copt, dopt)
	char *copt;
	char *dopt;
{
	int cmd;
	int fd;

	if (dopt == NULL) {
		if ((fd = open(KBD_DEVICE, O_WRONLY, 0)) < 0) {
			fprintf(stderr, "Cannot open %s\n", KBD_DEVICE);
			exit(1);
		}
	} else {
		if ((fd = open(dopt, O_WRONLY, 0)) < 0) {
			fprintf(stderr, "Cannot open %s\n", dopt);
			exit(1);
		}
	}

	if (strcmp(copt, "on") == 0)
		cmd = KBD_CMD_CLICK;
	else if (strcmp(copt, "off") == 0)
		cmd = KBD_CMD_NOCLICK;
	else {
		fprintf(stderr, "wrong option -- %s\n", copt);
		(void) usage();
		exit(1);
	}

	if (ioctl(fd, KIOCCMD, &cmd)) {
		perror("kbd: ioctl error");
		exit(1);
	}
}

void
usage()
{
	fprintf(stderr,
		"Usage: kbd [-r] [-t] [-c on|off] [-d keyboard device]\n");
}
