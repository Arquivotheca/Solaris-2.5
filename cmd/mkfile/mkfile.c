#pragma ident	"@(#)mkfile.c	1.6	94/10/10 SMI"

/*
 * Copyright (c) 1986, 1991 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define	WRITEBUF_SIZE	8192

#define	MIN(a, b)	((a) < (b) ? (a) : (b))

#define	BLOCK_SIZE	512		/* bytes */
#define	KILOBYTE	1024
#define	MEGABYTE	(KILOBYTE * KILOBYTE)

#define	FILE_MODE	(S_ISVTX + S_IRUSR + S_IWUSR)

static void usage(void);

char buf[WRITEBUF_SIZE];


main(argc, argv)
	char **argv;
{
	char	*opts;
	size_t	size;
	size_t	len;
	size_t	mult = 1;
	int	errors = 0;
	int	verbose = 0;	/* option variable */
	int	nobytes = 0;	/* option variable */

	if (argc == 1)
		usage();

	while (argv[1] && argv[1][0] == '-') {
		opts = &argv[1][0];
		while (*(++opts)) {
			switch (*opts) {
			case 'v':
				verbose++;
				break;
			case 'n':
				nobytes++;
				break;
			default:
				usage();
			}
		}
		argc--;
		argv++;
	}
	if (argc < 3)
		usage();

	len = strlen(argv[1]);
	if (len && isalpha(argv[1][len-1])) {
		switch (argv[1][len-1]) {
		case 'k':
		case 'K':
			mult = KILOBYTE;
			break;
		case 'b':
		case 'B':
			mult = BLOCK_SIZE;
			break;
		case 'm':
		case 'M':
			mult = MEGABYTE;
			break;
		default:
			(void) fprintf(stderr, "unknown size %s\n", argv[1]);
			usage();
		}
		argv[1][len-1] = '\0';
	}
	size = atoi(argv[1]) * mult;

	argv++;
	argc--;

	while (argc > 1) {
		int fd;

		if (verbose)
			(void) fprintf(stdout, "%s %u bytes\n", argv[1], size);
		fd = open(argv[1], O_CREAT|O_TRUNC|O_RDWR, FILE_MODE);
		if (fd < 0) {
			perror(argv[1]);
			errors++;
			argv++;
			argc--;
			continue;
		}
		if (lseek(fd, size-1, 0) < 0) {
			perror(argv[1]);
			(void) close(fd);
			errors++;
			argv++;
			argc--;
			continue;
		} else if (write(fd, "", 1) != 1) {
			perror(argv[1]);
			(void) close(fd);
			errors++;
			argv++;
			argc--;
			continue;
		}

		if (chmod(argv[1], FILE_MODE) < 0)
			(void) fprintf(stderr,
			    "warning: couldn't set mode to %#o\n", FILE_MODE);

		if (!nobytes) {
			size_t written = 0;

			if (lseek(fd, 0, 0) < 0) {
				perror(argv[1]);
				(void) close(fd);
				errors++;
				argv++;
				argc--;
				continue;
			}
			while (written < size) {
				size_t bytes = MIN(sizeof (buf), size-written);

				if (write(fd, buf, bytes) != (ssize_t) bytes) {
					perror(argv[1]);
					errors++;
					break;
				}
				written += bytes;
			}
		}
		(void) close(fd);
		argv++;
		argc--;
	}
	exit(errors);
	/* NOTREACHED */
}

static void usage()
{
	(void) fprintf(stderr,
		"Usage: mkfile [-vn] <size> <name1> [<name2>] ...\n");
	exit(1);
	/* NOTREACHED */
}
