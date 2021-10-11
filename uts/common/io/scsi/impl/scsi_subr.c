/*
 * Copyright (c) 1988-1991 Sun Microsystems, Inc.
 */

#pragma ident  "@(#)scsi_subr.c 1.49     95/01/20 SMI"

#include <sys/scsi/scsi.h>

/*
 * Utility SCSI routines
 */

/*
 * Polling support routines
 */

extern int scsi_callback_id;

static int scsi_poll_busycnt = SCSI_POLL_TIMEOUT;

/*
 * Common buffer for scsi_log
 */

extern kmutex_t scsi_log_mutex;
static char scsi_log_buffer[256];


#define	A_TO_TRAN(ap)	(ap->a_hba_tran)
#define	P_TO_TRAN(pkt)	((pkt)->pkt_address.a_hba_tran)
#define	P_TO_ADDR(pkt)	(&((pkt)->pkt_address))


int
scsi_poll(struct scsi_pkt *pkt)
{
	register busy_count, rval = -1, savef;
	long savet;
	void (*savec)();

	/*
	 * save old flags..
	 */
	savef = pkt->pkt_flags;
	savec = pkt->pkt_comp;
	savet = pkt->pkt_time;


	/*
	 * set NOINTR and NODISCON so we minimize the polling time
	 * except when tagged
	 */
	if (pkt->pkt_flags & FLAG_TAGMASK) {
		pkt->pkt_flags |= FLAG_NOINTR;
	} else {
		pkt->pkt_flags |= FLAG_NOINTR | FLAG_NODISCON;
	}

	/*
	 * XXX there is nothing in the scsi spec that states that we should not
	 * do a callback for polled cmds; however, removing this will break sd
	 * and probably other target drivers
	 */
	pkt->pkt_comp = 0;

	/*
	 * we don't like a polled command without timeout.
	 * 60 seconds seems long enough.
	 */
	if (pkt->pkt_time == 0) {
		pkt->pkt_time = SCSI_POLL_TIMEOUT;
	}

	for (busy_count = 0; busy_count < scsi_poll_busycnt; busy_count++) {

		if (scsi_transport(pkt) != TRAN_ACCEPT) {
			break;
		}
		if (pkt->pkt_reason == CMD_INCOMPLETE && pkt->pkt_state == 0) {
			drv_usecwait(1000000);

		} else if (pkt->pkt_reason != CMD_CMPLT) {
			break;

		} else if (((*pkt->pkt_scbp) & STATUS_MASK) == STATUS_BUSY) {
			drv_usecwait(1000000);

		} else {
			rval = 0;
			break;
		}
	}

	pkt->pkt_flags = savef;
	pkt->pkt_comp = savec;
	pkt->pkt_time = savet;
	return (rval);
}

/*
 * Command packaging routines (here for compactness rather than speed)
 */


void
makecom_g0(struct scsi_pkt *pkt, struct scsi_device *devp,
    int flag, int cmd, int addr, int cnt)
{
	MAKECOM_G0(pkt, devp, flag, cmd, addr, (u_char) cnt);
}

void
makecom_g0_s(struct scsi_pkt *pkt, struct scsi_device *devp,
    int flag, int cmd, int cnt, int fixbit)
{
	MAKECOM_G0_S(pkt, devp, flag, cmd, cnt, (u_char) fixbit);
}

void
makecom_g1(struct scsi_pkt *pkt, struct scsi_device *devp,
    int flag, int cmd, int addr, int cnt)
{
	MAKECOM_G1(pkt, devp, flag, cmd, addr, cnt);
}

void
makecom_g5(struct scsi_pkt *pkt, struct scsi_device *devp,
    int flag, int cmd, int addr, int cnt)
{
	MAKECOM_G5(pkt, devp, flag, cmd, addr, cnt);
}

/*
 * Common iopbmap data area packet allocation routines
 */

struct scsi_pkt *
get_pktiopb(struct scsi_address *ap, caddr_t *datap, int cdblen, int statuslen,
    int datalen, int readflag, int (*func)())
{
	scsi_hba_tran_t	*tran = A_TO_TRAN(ap);
	dev_info_t	*pdip = tran->tran_hba_dip;
	struct scsi_pkt	*pkt = NULL;
	struct buf	local;

	if (!datap)
		return (pkt);
	*datap = (caddr_t)0;
	bzero((caddr_t)&local, sizeof (struct buf));
	if (ddi_iopb_alloc(pdip, (ddi_dma_lim_t *)0,
	    (u_int) datalen, &local.b_un.b_addr)) {
		return (pkt);
	}
	if (readflag)
		local.b_flags = B_READ;
	local.b_bcount = datalen;
	pkt = (*tran->tran_init_pkt) (ap, NULL, &local,
		cdblen, statuslen, 0, PKT_CONSISTENT,
		(func == SLEEP_FUNC) ? SLEEP_FUNC : NULL_FUNC,
		NULL);
	if (!pkt) {
		ddi_iopb_free(local.b_un.b_addr);
		if (func != NULL_FUNC) {
			ddi_set_callback(func, NULL, &scsi_callback_id);
		}
	} else {
		*datap = local.b_un.b_addr;
	}
	return (pkt);
}

/*
 *  Equivalent deallocation wrapper
 */

void
free_pktiopb(struct scsi_pkt *pkt, caddr_t datap, int datalen)
{
	register struct scsi_address	*ap = P_TO_ADDR(pkt);
	register scsi_hba_tran_t	*tran = A_TO_TRAN(ap);

	(*tran->tran_destroy_pkt)(ap, pkt);
	if (datap && datalen) {
		ddi_iopb_free(datap);
	}
	if (scsi_callback_id != 0) {
		ddi_run_callback(&scsi_callback_id);
	}
}

/*
 * Common naming functions
 */

static char scsi_tmpname[64];

char *
scsi_dname(int dtyp)
{
	static char *dnames[] = {
		"Direct Access",
		"Sequential Access",
		"Printer",
		"Processor",
		"Write-Once/Read-Many",
		"Read-Only Direct Access",
		"Scanner",
		"Optical",
		"Changer",
		"Communications"
	};

	if ((dtyp & DTYPE_MASK) <= DTYPE_COMM) {
		return (dnames[dtyp&DTYPE_MASK]);
	} else if (dtyp == DTYPE_NOTPRESENT) {
		return ("Not Present");
	}
	return ("<unknown device type>");

}

char *
scsi_rname(u_char reason)
{
	static char *rnames[] = {
		"cmplt",
		"incomplete",
		"dma_derr",
		"tran_err",
		"reset",
		"aborted",
		"timeout",
		"data_ovr",
		"ovr",
		"sts_ovr",
		"badmsg",
		"nomsgout",
		"xid_fail",
		"ide_fail",
		"abort_fail",
		"reject_fail",
		"nop_fail",
		"per_fail",
		"bdr_fail",
		"id_fail",
		"unexpected_bus_free",
		"tag reject",
	};
	if (reason > CMD_TAG_REJECT) {
		return ("<unknown reason>");
	} else {
		return (rnames[reason]);
	}
}

char *
scsi_mname(u_char msg)
{
	static char *imsgs[18] = {
		"COMMAND COMPLETE",
		"EXTENDED",
		"SAVE DATA POINTER",
		"RESTORE POINTERS",
		"DISCONNECT",
		"INITIATOR DETECTED ERROR",
		"ABORT",
		"REJECT",
		"NO-OP",
		"MESSAGE PARITY",
		"LINKED COMMAND COMPLETE",
		"LINKED COMMAND COMPLETE (W/FLAG)",
		"BUS DEVICE RESET",
		"ABORT TAG",
		"CLEAR QUEUE",
		"INITIATE RECOVERY",
		"RELEASE RECOVERY",
		"TERMINATE PROCESS"
	};
	static char *imsgs_2[4] = {
		"SIMPLE QUEUE TAG",
		"HEAD OF QUEUE TAG",
		"ORDERED QUEUE TAG",
		"IGNORE WIDE RESIDUE"
	};

	if (msg < 18) {
		return (imsgs[msg]);
	} else if (IS_IDENTIFY_MSG(msg)) {
		return ("IDENTIFY");
	} else if (IS_2BYTE_MSG(msg) &&
	    (int)((msg) & 0xF) < sizeof (imsgs_2)) {
		return (imsgs_2[msg & 0xF]);
	} else {
		return ("<unknown msg>");
	}

}

char *
scsi_cname(u_char cmd, register char **cmdvec)
{
	while (*cmdvec != (char *)0) {
		if (cmd == **cmdvec) {
			return ((char *)((int)(*cmdvec)+1));
		}
		cmdvec++;
	}
	return (sprintf(scsi_tmpname, "<undecoded cmd 0x%x>", cmd));
}

char *
scsi_cmd_name(u_char cmd, struct scsi_key_strings *cmdlist, char *tmpstr)
{
	int i = 0;

	while (cmdlist[i].key !=  -1) {
		if (cmd == cmdlist[i].key) {
			return ((char *)cmdlist[i].message);
		}
		i++;
	}
	return (sprintf(tmpstr, "<undecoded cmd 0x%x>", cmd));
}

char *
scsi_esname(u_int key, char *tmpstr)
{
	struct scsi_key_strings extended_sense_list[] = {
		0x00, "no additional sense info",
		0x01, "no index/sector signal",
		0x02, "no seek complete",
		0x03, "peripheral device write fault",
		0x04, "LUN not ready",
		0x05, "LUN does not respond to selection",
		0x06, "reference position found",
		0x07, "multiple peripheral devices selected",
		0x08, "LUN communication failure",
		0x09, "track following error",
		0x0a, "error log overflow",
		0x0c, "write error",
		0x10, "ID CRC or ECC error",
		0x11, "unrecovered read error",
		0x12, "address mark not found for ID field",
		0x13, "address mark not found for data field",
		0x14, "recorded entity not found",
		0x15, "random positioning error",
		0x16, "data sync mark error",
		0x17, "recovered data with no error correction",
		0x18, "recovered data with error correction",
		0x19, "defect list error",
		0x1a, "parameter list length error",
		0x1b, "synchronous data xfer error",
		0x1c, "defect list not found",
		0x1d, "miscompare during verify",
		0x1e, "recovered ID with ECC",
		0x20, "invalid command operation code",
		0x21, "logical block address out of range",
		0x22, "illegal function",
		0x24, "invalid field in cdb",
		0x25, "LUN not supported",
		0x26, "invalid field in param list",
		0x27, "write protected",
		0x28, "medium may have changed",
		0x29, "power on, reset, or bus reset occurred",
		0x2a, "parameters changed",
		0x2b, "copy cannot execute since host cannot disconnect",
		0x2c, "command sequence error",
		0x2d, "overwrite error on update in place",
		0x2f, "commands cleared by another initiator",
		0x30, "incompatible medium installed",
		0x31, "medium format corrupted",
		0x32, "no defect spare location available",
		0x33, "tape length error",
		0x36, "ribbon, ink, or toner failure",
		0x37, "rounded parameter",
		0x39, "saving parameters not supported",
		0x3a, "medium not present",
		0x3b, "sequential positioning error",
		0x3d, "invalid bits in indentify message",
		0x3e, "LUN has not self-configured yet",
		0x3f, "target operating conditions have changed",
		0x40, "ram failure",
		0x41, "data path failure",
		0x42, "power-on or self-test failure",
		0x43, "message error",
		0x44, "internal target failure",
		0x45, "select or reselect failure",
		0x46, "unsuccessful soft reset",
		0x47, "scsi parity error",
		0x48, "initiator detected error message received",
		0x49, "invalid message error",
		0x4a, "command phase error",
		0x4b, "data phase error",
		0x4c, "logical unit failed self-configuration",
		0x4e, "overlapped commands attempted",
		0x50, "write append error",
		0x51, "erase failure",
		0x52, "cartridge fault",
		0x53, "media load or eject failed",
		0x54, "scsi to host system interface failure",
		0x55, "system resource failure",
		0x57, "unable to recover TOC",
		0x58, "generation does not exist",
		0x59, "updated block read",
		0x5a, "operator request or state change input",
		0x5b, "log exception",
		0x5c, "RPL status change",
		0x60, "lamp failure",
		0x61, "video aquisition error",
		0x62, "scan head positioning error",
		0x63, "end of user area encountered on this track",
		0x64, "illegal mode for this track",

		-1, NULL,
	};
	int i = 0;

	while (extended_sense_list[i].key != -1) {
		if (key == extended_sense_list[i].key) {
			return ((char *)extended_sense_list[i].message);
		}
		i++;
	}
	return (sprintf(tmpstr, "<vendor unique code 0x%x>", key));
}

char *
scsi_sname(u_char sense_key)
{
	if (sense_key >= (u_char)(NUM_SENSE_KEYS+NUM_IMPL_SENSE_KEYS)) {
		return ("<unknown sense key>");
	} else {
		return (sense_keys[sense_key]);
	}
}



/*
 * Print a piece of inquiry data- cleaned up for non-printable characters
 * and stopping at the first space character after the beginning of the
 * passed string;
 */

static void
inq_fill(char *p, int l, char *s)
{
	register unsigned i = 0;
	char c;

	if (!p)
		return;

	while (i++ < l) {
		/* clean string of non-printing chars */
		if ((c = *p++) < ' ' || c >= 0177) {
			c = '*';
		} else if (i != 1 && c == ' ') {
			break;
		}
		*s++ = c;
	}
	*s++ = 0;
}

void
scsi_errmsg(struct scsi_device *devp, struct scsi_pkt *pkt, char *label,
    int severity, int blkno, int err_blkno,
    struct scsi_key_strings *cmdlist, struct scsi_extended_sense *sensep)
{
	u_char com;
	auto char buf[256], tmpbuf[64];
	dev_info_t *dev = devp->sd_dev;
	static char *error_classes[] = {
		"All", "Unknown", "Informational",
		"Recovered", "Retryable", "Fatal"
	};

	bzero((caddr_t)buf, 256);
	com = ((union scsi_cdb *)pkt->pkt_cdbp)->scc_cmd;
	(void) sprintf(buf, "Error for command '%s'\tError Level: %s",
	    scsi_cmd_name(com, cmdlist, tmpbuf), error_classes[severity]);
	scsi_log(dev, label, CE_WARN, buf);

	bzero((caddr_t)buf, 256);
	if (blkno != -1 || err_blkno != -1 &&
	    ((com & 0xf) == SCMD_READ) || ((com & 0xf) == SCMD_WRITE)) {
		(void) sprintf(buf, "Requested Block %d, Error Block: %d\n",
		    blkno, err_blkno);
		scsi_log(dev, label, CE_CONT, buf);
	}

	if (sensep) {
		bzero((caddr_t)buf, 256);
		(void) sprintf(buf, "Sense Key: %s\n",
		    sense_keys[sensep->es_key]);

		scsi_log(dev, label, CE_CONT, buf);
		if (sensep->es_add_code) {
			bzero((caddr_t)buf, 256);
			strcpy(buf, "Vendor '");
			inq_fill(devp->sd_inq->inq_vid, 8, &buf[strlen(buf)]);
			(void) sprintf(&buf[strlen(buf)],
			"':\n        ASC = 0x%x (%s), ASCQ = 0x%x, FRU = 0x%x",
			    sensep->es_add_code,
			    scsi_esname(sensep->es_add_code, tmpbuf),
			    sensep->es_qual_code,
			    sensep->es_fru_code);
			scsi_log(dev, label, CE_CONT, "%s\n", buf);
		}
	}
}

/*ARGSUSED*/
void
scsi_log(dev_info_t *dev, char *label, u_int level,
    const char *fmt, ...)
{
	auto char name[256];
	va_list ap;
	int log_only = 0;
	int boot_only = 0;
	int console_only = 0;

	mutex_enter(&scsi_log_mutex);

	if (dev) {
		if (level == CE_PANIC || level == CE_WARN) {
			sprintf(name, "%s (%s%d):\n",
				ddi_pathname(dev, scsi_log_buffer), label,
				ddi_get_instance(dev));
		} else if (level == CE_NOTE ||
		    level >= (u_int) SCSI_DEBUG) {
			sprintf(name,
			    "%s%d:", label, ddi_get_instance(dev));
		} else if (level == CE_CONT) {
			name[0] = '\0';
		}
	} else {
		sprintf(name, "%s:", label);
	}

	va_start(ap, fmt);
	(void) vsprintf(scsi_log_buffer, fmt, ap);
	va_end(ap);

	switch (scsi_log_buffer[0]) {
	case '!':
		log_only = 1;
		break;
	case '?':
		boot_only = 1;
		break;
	case '^':
		console_only = 1;
		break;
	}

	switch (level) {
		case CE_NOTE:
			level = CE_CONT;
			/* FALLTHROUGH */
		case CE_CONT:
		case CE_WARN:
		case CE_PANIC:
			if (boot_only) {
				cmn_err(level, "?%s\t%s", name,
					&scsi_log_buffer[1]);
			} else if (console_only) {
				cmn_err(level, "^%s\t%s", name,
					&scsi_log_buffer[1]);
			} else if (log_only) {
				cmn_err(level, "!%s\t%s", name,
					&scsi_log_buffer[1]);
			} else {
				cmn_err(level, "%s\t%s", name,
					scsi_log_buffer);
			}
			break;
		case (u_int) SCSI_DEBUG:
		default:
			cmn_err(CE_CONT, "^DEBUG: %s\t%s", name,
					scsi_log_buffer);
			break;
	}

	mutex_exit(&scsi_log_mutex);
}
