/*
 * Copyright (c) 1987,1991 by Sun Microsystems, Inc.
 */

#ident  "@(#)syncloop.c 1.8     94/12/21 SMI"

/*
 * Synchronous loop-back test program
 * For installation verification of synchronous lines and facilities
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/file.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/poll.h>
#include <sys/ser_sync.h>
#include "dltest.h"

char *portname = NULL;
char *gets();
unsigned int speed = 9600;
int reccount = 100;
int reclen = 100;
char loopstr[MAX_INPUT];
int looptype = 0;
int loopchange = 0;
int clockchange = 0;
int cfd, dfd;		/* control and data descriptors */
int data = -1;
int verbose = 0;

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

#define	MAXPACKET 4096

main(argc, argv)
	int argc;
	char **argv;
{
	extern int errno;
	char cnambuf[20], dnambuf[20], *cp, *cpp; 
	char command[100];
        struct scc_mode sm;
        struct strioctl sioc;
	u_long ppa;
	long buf[MAXDLBUF];	/* aligned on long */

	argc--;
	argv++;
	while (argc > 0 && argv[0][0] == '-') switch (argv[0][1]) {
	case 'c':	/* rec count */
		if (argc < 2)
			Usage();
		reccount = atoi(argv[1]);
		argc -= 2;
		argv += 2;
		break;
	case 'd':
		if (sscanf(argv[1], "%x", &data) != 1)
			Usage();
		argc -= 2;
		argv += 2;
		break;
	case 'l':	/* rec length */
		if (argc < 2)
			Usage();
		reclen = atoi(argv[1]);
		argc -= 2;
		argv += 2;
		break;
	case 's':	/* line speed */
		if (argc < 2)
			Usage();
		speed = atoi(argv[1]);
		argc -= 2;
		argv += 2;
		break;
	case 't':	/* test type */
		if (argc < 2)
			Usage();
		looptype = atoi(argv[1]);
		argc -= 2;
		argv += 2;
		break;
	case 'v':
		verbose = 1;
		argc--;
		argv++;
		break;
	}
	if (argc != 1)
		Usage();
	portname = argv[0];

	sprintf(dnambuf, "/dev/%s", portname);
	dfd = open(dnambuf, O_RDWR);
	if (dfd < 0) {
		fprintf(stderr, "syncloop: cannot open %s\n", dnambuf);
		perror(dnambuf);
		exit(1);
	}
	for (cp = portname; (*cp) && (!isdigit(*cp)); cp++) {}
	ppa = strtoul(cp, &cpp, 10);
	if (cpp == cp) {
		fprintf(stderr,
			"syncinit: %s missing minor device number\n", portname);
		exit(1);
	}
	*cp = '\0';	/* drop number, leaving name of clone device. */
	sprintf(cnambuf, "/dev/%s", portname);
	cfd = open(cnambuf, O_RDWR);
	if (cfd < 0) {
		fprintf(stderr, "syncloop: cannot open %s\n", cnambuf);
		perror(cnambuf);
		exit(1);
	}
	dlattachreq(cfd, ppa);
	dlokack(cfd, buf);

	if (reclen < 0 || reclen > MAXPACKET) {
		printf("invalid packet length: %d\n", reclen);
		exit(1);
	}
	printf("[ Data device: %s | Control device: %s, ppa=%d ]\n",
		dnambuf, cnambuf, ppa);

        sioc.ic_cmd = S_IOCGETMODE;
        sioc.ic_timout = -1;
        sioc.ic_len = sizeof(struct scc_mode);
        sioc.ic_dp = (char *)&sm;
        if (ioctl(cfd, I_STR, &sioc) < 0) {
                perror("S_IOCGETMODE");
                fprintf(stderr,
                        "syncloop: can't get sync mode info for %s\n", cnambuf);
                exit(1);
        }
	while (looptype < 1 || looptype > 4) {
		printf("Enter test type:\n");
		printf("1: Internal Test\n");
		printf("            (internal data loop, internal clocking)\n");
		printf("2: Test using loopback plugs\n");
		printf("            (external data loop, internal clocking)\n");
		printf("3: Test using local or remote modem loopback\n");
		printf("            (external data loop, external clocking)\n");
		printf("4: Other, previously set, special mode\n");
		printf("> "); fflush(stdout);
		gets(loopstr);
		sscanf(loopstr, "%d",&looptype);
	}
	switch (looptype) {
	case 1:
		if ((sm.sm_txclock != TXC_IS_BAUD) ||
		    (sm.sm_rxclock != RXC_IS_BAUD))
			clockchange++;
                sm.sm_txclock = TXC_IS_BAUD;
                sm.sm_rxclock = RXC_IS_BAUD;
		if ((sm.sm_config & CONN_LPBK) == 0)
			loopchange++;
                sm.sm_config |= CONN_LPBK;
		break;
	case 2:
		if ((sm.sm_txclock != TXC_IS_BAUD) ||
		    (sm.sm_rxclock != RXC_IS_RXC))
			clockchange++;
                sm.sm_txclock = TXC_IS_BAUD;
                sm.sm_rxclock = RXC_IS_RXC;
		if ((sm.sm_config & CONN_LPBK) != 0)
			loopchange++;
                sm.sm_config &= ~CONN_LPBK;
		break;
	case 3:
		if ((sm.sm_txclock != TXC_IS_TXC) ||
		    (sm.sm_rxclock != RXC_IS_RXC))
			clockchange++;
                sm.sm_txclock = TXC_IS_TXC;
                sm.sm_rxclock = RXC_IS_RXC;
		if ((sm.sm_config & CONN_LPBK) != 0)
			loopchange++;
                sm.sm_config &= ~CONN_LPBK;
		break;
	case 4:
		goto no_params;
	}

	sm.sm_baudrate = speed;

        sioc.ic_cmd = S_IOCSETMODE;
        sioc.ic_timout = -1;
        sioc.ic_len = sizeof(struct scc_mode);
        sioc.ic_dp = (char *)&sm;
        if (ioctl(cfd, I_STR, &sioc) < 0) {
                perror("S_IOCSETMODE");
                fprintf(stderr,
                        "syncloop: can't set sync mode info for %s\n", cnambuf);
                exit(1);
        }

no_params:
        /* report state */
        sioc.ic_cmd = S_IOCGETMODE;
        sioc.ic_timout = -1;
        sioc.ic_len = sizeof(struct scc_mode);
        sioc.ic_dp = (char *)&sm;
        if (ioctl(cfd, I_STR, &sioc) < 0) {
                perror("S_IOCGETMODE");
                fprintf(stderr,
                        "syncloop: can't get sync mode info for %s\n", cnambuf);
                exit(1);
        }
        printf("speed=%d, loopback=%s, nrzi=%s, txc=%s, rxc=%s\n",
                sm.sm_baudrate,
                yesno[((int)(sm.sm_config & CONN_LPBK) > 0)],
                yesno[((int)(sm.sm_config & CONN_NRZI) > 0)],
                txnames[sm.sm_txclock],
                rxnames[sm.sm_rxclock]);

	quiet_period();
	first_packet();
	many_packets();
	exit(0);
}

Usage()
{
	printf("Usage: syncloop [ options ] portname\n");
	printf("Options: -c packet_count\n");
	printf("         -l packet_length\n");
	printf("         -s line_speed\n");
	printf("         -t test_type\n");
	printf("         -d hex_data_byte\n");
	exit(1);
}

int zero_time = 0;
int short_time = 1000;
int long_time = 4000;
char bigbuf[4096];
char packet[MAXPACKET];
struct pollfd pfd;

quiet_period()
{
	int pollret;

	printf("[ checking for quiet line ]\n");
	pfd.fd = dfd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	while (poll(&pfd, 1, short_time) == 1) {
		(void) read(dfd, bigbuf, sizeof bigbuf);
	}
	if (poll(&pfd, 1, long_time) == 1) {
		printf("packet received but none sent!\n");
		printf("quiesce other end before starting syncloop\n");
		exit(1);
	}
}

first_packet()
{
	extern int errno;
	int i, len;
	int pollret;
	struct strioctl sioc;
	struct sl_stats start_stats, end_stats;
	struct scc_mode sm;

	for (i = 0; i < reclen; i++)
		packet[i] = (data == -1) ? rand() : data;
	printf("[ Trying first packet ]\n");
	sioc.ic_cmd = S_IOCGETSTATS;
	sioc.ic_timout = -1;
	sioc.ic_len = sizeof(struct sl_stats);
	sioc.ic_dp = (char *)&start_stats;
	if (ioctl(cfd, I_STR, &sioc) < 0) {
		perror("S_IOCGETSTATS");
		exit(1);
	}

	for (i = 0; i < 5; i++) {
		if (write(dfd, packet, reclen) != reclen) {
			fprintf(stderr, "packet write failed, errno %d\n",
				errno);
			exit(1);
		}
		pfd.fd = dfd;
		pfd.events = POLLIN;
		pollret = poll(&pfd, 1, long_time);
		if(pollret < 0) perror("poll");
		if(pollret == 0)
			printf("poll: nothing to read.\n");
		if (pollret == 1) {
			len = read(dfd, bigbuf, reclen);
			if (len == reclen && memcmp(packet, bigbuf, len) == 0)
				return;	/* success */
			else {
				printf("len %d should be %d\n",
					len, reclen);
				if (verbose) {
					printf( "           ");
					printhex(bigbuf, len);
					printf("\nshould be ");
					printhex(packet, reclen);
					printf("\n");
				}
			}
		}
	}
	printf("Loopback has TOTALLY FAILED - ");
	printf("no packets returned after 5 attempts\n");
	sioc.ic_cmd = S_IOCGETSTATS;
	sioc.ic_timout = -1;
	sioc.ic_len = sizeof(struct sl_stats);
	sioc.ic_dp = (char *)&end_stats;
	if (ioctl(cfd, I_STR, &sioc) < 0) {
		perror("S_IOCGETSTATS");
		exit(1);
	}
	if (start_stats.opack == end_stats.opack)
		printf("No packets transmitted - no transmit clock present\n");
	exit(1);
}

many_packets()
{
	struct strioctl sioc;
	struct sl_stats start_stats, end_stats;
	struct timeval start_time, end_time;
	int baddata = 0;
	float secs, speed;
	int i, len;
	int incount = 0;
	long prev_sec = -1;
	int pollret;

	printf("[ Trying many packets ]\n");
	sioc.ic_cmd = S_IOCGETSTATS;
	sioc.ic_timout = -1;
	sioc.ic_len = sizeof(struct sl_stats);
	sioc.ic_dp = (char *)&start_stats;
	if (ioctl(cfd, I_STR, &sioc) < 0) {
		perror("S_IOCGETSTATS");
		exit(1);
	}
	gettimeofday(&start_time, 0);
	end_time = start_time;

	for (i = 0; i < reccount;) {
		if (end_time.tv_sec != prev_sec) {
			prev_sec = end_time.tv_sec;
			printf("\r %d ", incount);
			fflush(stdout);
		}
		pfd.fd = dfd;
		pfd.events = POLLIN;
		while (pollret = poll(&pfd, 1, zero_time)) {
			if(pollret < 0) perror("poll");
			else {
				(void) lseek(dfd, (long)0, 0);
				len = read(dfd, bigbuf, reclen);
				if (len != reclen ||
				    memcmp(packet, bigbuf, len) != 0) {
					printf("len %d should be %d\n",
						len, reclen);
					if (verbose) {
						printf( "           ");
						printhex(bigbuf, len);
						printf("\nshould be ");
						printhex(packet, reclen);
						printf("\n");
					}
					baddata++;
				}
				incount++;
				gettimeofday(&end_time, 0);
			}
		}
		pfd.fd = dfd;
		pfd.events = POLLIN|POLLOUT;
		pollret = poll(&pfd, 1, long_time);
		if(pollret < 0) perror("poll");
		if(pollret == 0)
			printf("poll: nothing to read or write.\n");
		if (pollret == 1) {
			if (pfd.revents & POLLOUT) {
				write(dfd, packet, reclen);
				i++;
			} else if (!(pfd.revents & POLLIN)) {
				printf("OUTPUT HAS LOCKED UP!!!\n");
				break;
			}
		}
	}
	pfd.fd = dfd;
	pfd.events = POLLIN;
	while ((incount < reccount) && (poll(&pfd, 1, long_time) == 1)) {
		if (end_time.tv_sec != prev_sec) {
			prev_sec = end_time.tv_sec;
			printf("\r %d ", incount);
			fflush(stdout);
		}
		len = read(dfd, bigbuf, reclen);
		if (len != reclen || memcmp(packet, bigbuf, len) != 0) {
			printf("len %d should be %d\n", len, reclen);
			if (verbose) {
				printf( "           ");
				printhex(bigbuf, len);
				printf("\nshould be ");
				printhex(packet, reclen);
				printf("\n");
			}
			baddata++;
		}
		incount++;
		gettimeofday(&end_time, 0);
	}
	printf("\r %d \n", incount);
	if (baddata) 
		printf("%d packets with wrong data received!\n", baddata);
	sioc.ic_cmd = S_IOCGETSTATS;
	sioc.ic_timout = -1;
	sioc.ic_len = sizeof(struct sl_stats);
	sioc.ic_dp = (char *)&end_stats;
	if (ioctl(cfd, I_STR, &sioc) < 0) {
		perror("S_IOCGETSTATS");
		exit(1);
	}
	end_stats.ipack -= start_stats.ipack;
	end_stats.opack -= start_stats.opack;
	end_stats.abort -= start_stats.abort;
	end_stats.crc -= start_stats.crc;
	end_stats.overrun -= start_stats.overrun;
	end_stats.underrun -= start_stats.underrun;
	end_stats.ierror -= start_stats.ierror;
	end_stats.oerror -= start_stats.oerror;
	if (reccount > end_stats.opack)
		printf("%d packets lost in outbound queueing\n", 
			reccount - end_stats.opack);
	if (incount < end_stats.ipack && incount < reccount)
		printf("%d packets lost in inbound queueing\n", 
			end_stats.ipack - incount);
	printf("%d packets sent, %d received\n", reccount, incount);
	printf("CRC errors    Aborts   Overruns  Underruns         ");
	printf("   In <-Drops-> Out\n%9d  %9d  %9d  %9d  %12d  %12d\n",
		end_stats.crc, end_stats.abort,
		end_stats.overrun, end_stats.underrun,
		end_stats.ierror, end_stats.oerror);
	secs = (float) (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
	secs += (float) (end_time.tv_sec - start_time.tv_sec);
	if (secs) {
		speed = 8 * incount * (4 + reclen) / secs;
		printf("estimated line speed = %d bps\n", (int)speed);
	}
}

printhex(cp, len)
	char *cp;
	int len;
{
	char c, *hex = "0123456789ABCDEF";
	int i;

	for (i = 0; i< len; i++) {
		c = *cp++;
		putchar(hex[(c >> 4) & 0xF]);
		putchar(hex[c & 0xF]);
	}
}
