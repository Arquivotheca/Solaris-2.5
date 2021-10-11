/*
 * Copyright (c) 1985,1991 by Sun Microsystems, Inc.
 */

#ident  "@(#)syncinit.c 1.5     92/09/11 SMI"

/*
 * Initialize and re-initialize synchronous serial clocking and loopback
 * options.  Interfaces through the S_IOCGETMODE and S_IOCSETMODE ioctls.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ser_sync.h>
#include "dltest.h"

char *yesno[] = {
	"no",
	"yes",
	"silent",
	0,
};

char *txnames[] = {
	"txc",
	"rxc",
	"baud",
	"pll",
	"sysclk",
	"-txc",
	0,
};

char *rxnames[] = {
	"rxc",
	"txc",
	"baud",
	"pll",
	"sysclk",
	"-rxc",
	0,
};

char *txdnames[] = {
	"txd",
	" ",     /* dummy entry, do not remove */
	"-txd",
	0,
};

char *rxdnames[] = {
	"rxd",
	"-rxd",
	0,
};

char *portab[] = {
	"rs422",
	"v35",
	0,
};

#define	equal(a, b)	(strcmp((a), (b)) == 0)

main(argc, argv)
	int argc;
	char **argv;
{
	char cnambuf[32];
	struct scc_mode sm;
	struct strioctl sioc;
	int fd, speed;
	char *arg, *cp;
	char loopchange = 0;
	char echochange = 0;
	char clockchange = 0;
	u_long ppa;
	long buf[MAXDLBUF];	/* aligned on long */

	if (argc == 1) {
		usage();
		exit(1);
	}
	argc--;
	argv++;
	sprintf(cnambuf, "/dev/%s", argv[0]);
	cp = cnambuf;
	while (*cp)			/* find the end of the name */
		cp++;
	cp--;
	if (!isdigit(*cp)) {
		fprintf(stderr,
			"syncinit: %s missing minor device number\n", argv[0]);
		exit(1);
	}
	while (isdigit(*(cp - 1)))
		cp--;
	ppa = strtoul(cp, NULL, 10);
	*cp = '\0';	/* drop number, leaving name of clone device. */
	fd = open(cnambuf, O_RDWR|O_EXCL, 0);
	if (fd < 0) {
		perror("syncinit: open");
		exit(1);
	}
	dlattachreq(fd, ppa);
	dlokack(fd, buf);
	printf("device: %s  ppa: %d\n", cnambuf, (int)ppa);

	argc--;
	argv++;
	if (argc) {	/* setting things */	
		sioc.ic_cmd = S_IOCGETMODE;
		sioc.ic_timout = -1;
		sioc.ic_len = sizeof(struct scc_mode);
		sioc.ic_dp = (char *)&sm;
		if (ioctl(fd, I_STR, &sioc) < 0) {
			perror("S_IOCGETMODE");
			fprintf(stderr,
				"syncinit: can't get sync mode info for %s\n",
				cnambuf);
			exit(1);
		}
		while (argc-- > 0) {
			arg = *argv++;
			if (sscanf(arg, "%d", &speed) == 1)
				sm.sm_baudrate = speed;
			else if (strchr(arg, '=')) {
				if (prefix(arg, "loop")) {
					if (lookup(yesno, arg))
						sm.sm_config |= CONN_LPBK;
					else
						sm.sm_config &= ~CONN_LPBK;
					loopchange++;
				} else if (prefix(arg, "echo")) {
					if (lookup(yesno, arg))
						sm.sm_config |= CONN_ECHO;
					else
						sm.sm_config &= ~CONN_ECHO;
					echochange++;
				} else if (prefix(arg, "nrzi")) {
					if (lookup(yesno, arg))
						sm.sm_config |= CONN_NRZI;
					else
						sm.sm_config &= ~CONN_NRZI;
				}
				else if (prefix(arg, "txc")) {
					sm.sm_txclock = lookup(txnames, arg);
					clockchange++;
				}
				else if (prefix(arg, "rxc")) {
					sm.sm_rxclock = lookup(rxnames, arg);
					clockchange++;
				}
				else if (prefix(arg, "speed")) {
					arg = strchr(arg, '=') + 1;
					if (sscanf(arg, "%d", &speed) == 1) {
						sm.sm_baudrate = speed;
					} else
						fprintf(stderr,
						    "syncinit: %s %s\n",
						    "bad speed:", arg);
				}
			} else if (equal(arg, "external")) {
				sm.sm_txclock = TXC_IS_TXC;
				sm.sm_rxclock = RXC_IS_RXC;
				sm.sm_config &= ~CONN_LPBK;
			} else if (equal(arg, "sender")) {
				sm.sm_txclock = TXC_IS_BAUD;
				sm.sm_rxclock = RXC_IS_RXC;
				sm.sm_config &= ~CONN_LPBK;
			} else if (equal(arg, "internal")) {
				sm.sm_txclock = TXC_IS_PLL;
				sm.sm_rxclock = RXC_IS_PLL;
				sm.sm_config &= ~CONN_LPBK;
			} else if (equal(arg, "stop")) {
				sm.sm_baudrate = 0;
			} else fprintf(stderr, "Bad arg: %s\n", arg);
		}

		/*
		 * If we're going to change the state of loopback, and we
		 * don't have our own plans for clock sources, use defaults.
		 */
		if (loopchange && !clockchange) {
			if (sm.sm_config & CONN_LPBK) {
				sm.sm_txclock = TXC_IS_BAUD;
				sm.sm_rxclock = RXC_IS_BAUD;
			} else {
				sm.sm_txclock = TXC_IS_TXC;
				sm.sm_rxclock = RXC_IS_RXC;
			}
		}
		sioc.ic_cmd = S_IOCSETMODE;
		sioc.ic_timout = -1;
		sioc.ic_len = sizeof(struct scc_mode);
		sioc.ic_dp = (char *)&sm;
		if (ioctl(fd, I_STR, &sioc) < 0) {
			perror("S_IOCSETMODE");
			ioctl(fd, S_IOCGETMODE, &sm);
			fprintf(stderr,
				"syncinit: ioctl failure code = %x\n",
				sm.sm_retval);
			exit(1);
		}
	}

	/* Report State */
	sioc.ic_cmd = S_IOCGETMODE;
	sioc.ic_timout = -1;
	sioc.ic_len = sizeof(struct scc_mode);
	sioc.ic_dp = (char *)&sm;
	if (ioctl(fd, I_STR, &sioc) < 0) {
		perror("S_IOCGETMODE");
		fprintf(stderr,
			"syncinit: can't get sync mode info for %s\n",
			cnambuf);
		exit(1);
	}
	printf("speed=%d, loopback=%s, echo=%s, nrzi=%s, txc=%s, rxc=%s\n",
		sm.sm_baudrate,
		yesno[((int)(sm.sm_config & CONN_LPBK) > 0)],
		yesno[((int)(sm.sm_config & CONN_ECHO) > 0)],
		yesno[((int)(sm.sm_config & CONN_NRZI) > 0)],
		txnames[sm.sm_txclock],
		rxnames[sm.sm_rxclock]);
	exit(0);
}

usage()
{
	fprintf(stderr, "Usage: syncinit cnambuf \\\n\t%s \\\n\t%s \\\n\t%s\n",
		"[baudrate] [loopback=[yes|no]] [echo=[yes|no]] [nrzi=[yes|no]]",
		"[txc=[txc|rxc|baud|pll]]",
		"[rxc=[rxc|txc|baud|pll]]");
	exit(1);
}

prefix(arg, pref)
	char *arg, *pref;
{
	return (strncmp(arg, pref, strlen(pref)) == 0);
}

lookup(table, arg)
	char **table;
	char *arg;
{
	char *val = strchr(arg, '=') + 1;
	int ival;

	for (ival = 0; *table != 0; ival++, table++)
		if (equal(*table, val))
			return (ival);
	fprintf(stderr, "syncinit: bad arg: %s\n", arg);
	exit(1);
	/* NOTREACHED */
}

