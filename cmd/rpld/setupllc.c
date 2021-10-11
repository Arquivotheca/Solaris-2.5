#pragma ident "@(#)setupllc.c 1.1	92/10/15 SMI"

#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/stropts.h>
#include <sys/dlpi.h>

#include <netdlc/uint.h>
#include <netdlc/ll_control.h>
#include <netdlc/ll_proto.h>

#define MAXPRIMSZ	100

main(argc, argv)
	int		argc;
	char		**argv;
{
	int		le_fd;
	int		llc_fd;
	struct strbuf	ctl;
	dl_attach_req_t	attach;
	dl_bind_req_t	bind;
	char		resultbuf[MAXPRIMSZ];	/* Bigger than largest DLPI
						   struct size */
	union DL_primitives *dlp = (union DL_primitives *) resultbuf;
	struct ll_snioc	snioc;
	struct strioctl	strio;
	int		flags = 0;
	int		muxid;

	/*
	 * Open a stream to the Ethernet driver.
	 */
	printf("Opening /dev/le\n");
	if ((le_fd = open("/dev/le", O_RDWR)) < 0) {
		perror("	**open(dev/le)");
		exit (1);
	}

	/*
	 * Attach the Ethernet stream to PPA 0.
	 */
	ctl.len = DL_ATTACH_REQ_SIZE;
	ctl.buf = (char *) &attach;
	attach.dl_primitive = DL_ATTACH_REQ;
	attach.dl_ppa = 0;
	printf("Sending DL_ATTACH_REQ to /dev/le\n");
	if (putmsg(le_fd, &ctl, NULL, 0) < 0) {
		perror("	**putmsg DL_ATTACH_REQ");
		exit (1);
	}

	/*
	 * Read the acknowledgement message (will be DL_OK_ACK if attach
	 * was successful).
	 */
	ctl.maxlen = MAXPRIMSZ;
	ctl.buf = resultbuf;
	if (getmsg(le_fd, &ctl, NULL, &flags) < 0) {
		perror("	**getmsg");
		exit (1);
	}
	if (dlp->dl_primitive != DL_OK_ACK) {
		/* An error occurred! */
		exit (1);
	}
	printf("/dev/le attached ...\n");

	/*
	 * Open a stream to the LLC2 driver.
	 */
	printf("Opening /dev/llc2\n");
	if ((llc_fd = open("/dev/llc2", O_RDWR)) < 0) {
		perror("	**open(dev/le)");
		exit (1);
	}

	/*
	 * I_LINK /dev/le underneath /dev/llc2
	 */
	printf("Linking /dev/le underneath /dev/llc2\n");
	if ((muxid = ioctl(llc_fd, I_LINK, le_fd)) < 0) {
		perror("	**ioctl(I_LINK)");
		exit (1);
	}

	/*
	 * Set the PPA of the Ethernet driver to 3.
	 * (This is the PPA that LLC2 clients need to specify when they
	 * attach to LLC2.)
	 */
	snioc.lli_type = LI_SPPA;
	snioc.lli_ppa = 3;
	snioc.lli_index = muxid;
	strio.ic_cmd = L_SETPPA;
	strio.ic_timout = -1;			/* Infinite timeout */
	strio.ic_len = sizeof(snioc);
	strio.ic_dp = (char *) &snioc;
	if (ioctl(llc_fd, I_STR, (char *) &strio) < 0) {
		perror("	**ioctl(L_SETPPA)");
		exit (1);
	}

	/*
	 * Attach the LLC2 stream to PPA 3.
	 */
	ctl.len = DL_ATTACH_REQ_SIZE;
	ctl.buf = (char *) &attach;
	attach.dl_primitive = DL_ATTACH_REQ;
	attach.dl_ppa = 3;
	printf("Sending DL_ATTACH_REQ to /dev/llc2\n");
	if (putmsg(llc_fd, &ctl, NULL, 0) < 0) {
		perror("	**putmsg DL_ATTACH_REQ");
		exit (1);
	}

	/*
	 * Read the acknowledgement message (will be DL_OK_ACK if attach
	 * was successful).
	 */
	ctl.maxlen = MAXPRIMSZ;
	ctl.buf = resultbuf;
	if (getmsg(llc_fd, &ctl, NULL, &flags) < 0) {
		perror("	**getmsg");
		exit (1);
	}
	if (dlp->dl_primitive != DL_OK_ACK) {
		/* An error occurred! */
		exit (1);
	}
	printf("/dev/llc2 attached ...\n");

	/*
	 * Bind the LLC2 stream to SAP 0xfe.
	 */
	ctl.len = DL_BIND_REQ_SIZE;
	ctl.buf = (char *) &bind;
	bind.dl_primitive = DL_BIND_REQ;
	bind.dl_sap = 0xfe;
	bind.dl_max_conind = 0;		   /* This is not a listen stream. */
	bind.dl_service_mode = DL_CODLS;   /* LLC2 */
	bind.dl_conn_mgmt = 0;
	bind.dl_xidtest_flg = DL_AUTO_TEST | DL_AUTO_XID;
	printf("Sending DL_BIND_REQ to /dev/llc2\n");
	if (putmsg(llc_fd, &ctl, NULL, 0) < 0) {
		perror("	**putmsg DL_BIND_REQ");
		exit (1);
	}

	/*
	 * Read the acknowledgement message (will be DL_BIND_ACK if bind
	 * was successful).
	 */
	ctl.maxlen = MAXPRIMSZ;
	ctl.buf = resultbuf;
	if (getmsg(llc_fd, &ctl, NULL, &flags) < 0) {
		perror("	**getmsg");
		exit (1);
	}
	if (dlp->dl_primitive != DL_BIND_ACK) {
		/* An error occurred! */
		exit (1);
	}
	printf("Received DL_BIND_ACK from /dev/llc2\n");
      pause();
}
