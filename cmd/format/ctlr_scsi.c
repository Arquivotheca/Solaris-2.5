
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)ctlr_scsi.c	1.17	94/05/25 SMI"

/*
 * This file contains the routines for embedded scsi disks
 */
#include "global.h"

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <errno.h>
#include <memory.h>
#include <malloc.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/byteorder.h>



#include "startup.h"
#include "scsi_com.h"
#include "misc.h"
#include "ctlr_scsi.h"
#include "analyze.h"
#include "param.h"
#include "io.h"


#ifndef	DAD_MODE_CACHE_CCS
#define	DAD_MODE_CACHE_CCS		0x38
#endif


#ifdef	__STDC__
/*
 *	Local prototypes for ANSI C compilers
 */
static int	scsi_format(int, int, struct defect_list *);
static int	scsi_raw_format(int format_timeout);
static int	scsi_ms_page8(int);
static int	scsi_ms_page38(int);
static void	scsi_convert_list_to_new(struct defect_list *,
			struct scsi_defect_list *, int);
static char	*scsi_find_command_name(u_int);
static int	chg_list_affects_page(struct chg_list *, int);
static void	scsi_printerr(struct uscsi_cmd *,
			struct scsi_extended_sense *, int);
static void	scsi_print_extended_sense(struct scsi_extended_sense *, int);

#else

static int	scsi_format();
static int	scsi_raw_format();
static int	scsi_ms_page8();
static int	scsi_ms_page38();
static void	scsi_convert_list_to_new();
static char	*scsi_find_command_name();
static int	chg_list_affects_page();
static void	scsi_printerr();
static void	scsi_print_extended_sense();

#endif	/* __STDC__ */



struct	ctlr_ops scsiops = {
	scsi_rdwr,
	scsi_ck_format,
	scsi_format,
	scsi_ex_man,
	scsi_ex_cur,
	scsi_repair,
	0,
};



#define	SCMD_UNKNOWN		0xff

/*
 * Names of commands.  Must have SCMD_UNKNOWN at end of list.
 */
static struct scsi_command_name {
	u_char command;
	char *name;
} scsi_command_names[] = {
	SCMD_FORMAT,		"format",
	SCMD_READ,		"read",
	SCMD_WRITE,		"write",
	SCMD_READ|SCMD_GROUP1,	"read",
	SCMD_WRITE|SCMD_GROUP1,	"write",
	SCMD_INQUIRY,		"inquiry",
	SCMD_MODE_SELECT,	"mode select",
	SCMD_MODE_SENSE,	"mode sense",
	SCMD_REASSIGN_BLOCK,	"reassign block",
	SCMD_READ_DEFECT_LIST,	"read defect list",
	SCMD_UNKNOWN,		"unknown"
};


/*
 * Strings for printing mode sense page control values
 */
static slist_t page_control_strings[] = {
	{ "current",	"",	MODE_SENSE_PC_CURRENT },
	{ "changeable",	"",	MODE_SENSE_PC_CHANGEABLE },
	{ "default",	"",	MODE_SENSE_PC_DEFAULT },
	{ "saved",	"",	MODE_SENSE_PC_SAVED }
};

/*
 * Strings for printing the mode select options
 */
static slist_t mode_select_strings[] = {
	{ "",		"",	0 },
	{ " (pf)",	"",	MODE_SELECT_PF },
	{ " (sp)",	"",	MODE_SELECT_SP },
	{ " (pf,sp)",	"",	MODE_SELECT_PF|MODE_SELECT_SP }
};

static int scsi_format_revolutions = 5;
static int scsi_format_timeout = 2*60*60;		/* two hours */


/*
 * Read or write the disk.
 */
int
scsi_rdwr(dir, fd, blkno, secnt, bufaddr, flags)
	int	dir;
	int	fd;
	daddr_t blkno;
	int	secnt;
	caddr_t bufaddr;
	int	flags;

{
	struct uscsi_cmd	ucmd;
	union scsi_cdb		cdb;

	/*
	 * Build and execute the uscsi ioctl.  We build a group0
	 * or group1 command as necessary, since some targets
	 * do not support group1 commands.
	 */
	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	cdb.scc_cmd = (dir == DIR_READ) ? SCMD_READ : SCMD_WRITE;
	if (blkno < (2<<20) && secnt <= 0xff) {
		FORMG0ADDR(&cdb, blkno);
		FORMG0COUNT(&cdb, secnt);
		ucmd.uscsi_cdblen = CDB_GROUP0;
	} else {
		FORMG1ADDR(&cdb, blkno);
		FORMG1COUNT(&cdb, secnt);
		ucmd.uscsi_cdblen = CDB_GROUP1;
		cdb.scc_cmd |= SCMD_GROUP1;
	}
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_bufaddr = bufaddr;
	ucmd.uscsi_buflen = secnt * SECSIZE;
	return (uscsi_cmd(fd, &ucmd, flags));
}


/*
 * Check to see if the disk has been formatted.
 * If we are able to read the first track, we conclude that
 * the disk has been formatted.
 */
int
scsi_ck_format()
{
	int	status;

	/*
	 * Try to read the first four blocks.
	 */
	status = scsi_rdwr(DIR_READ, cur_file, 0, 4, (caddr_t) cur_buf,
		F_SILENT);
	return (!status);
}


/*
 * Format the disk, the whole disk, and nothing but the disk.
 */
/*ARGSUSED*/
static int
scsi_format(start, end, list)
	int			start;		/* irrelevant for us */
	int			end;
	struct defect_list	*list;
{
	struct uscsi_cmd	ucmd;
	union scsi_cdb		cdb;
	struct scsi_defect_hdr	defect_hdr;
	int			status;
	int			flag;
	struct scsi_inquiry	*inq;
	char			rawbuf[MAX_MODE_SENSE_SIZE];


	/*
	 * Determine if the target appears to be SCSI-2
	 * compliant.  We handle mode sense/mode selects
	 * a little differently, depending upon CCS/SCSI-2
	 */
	if (uscsi_inquiry(cur_file, rawbuf, sizeof (rawbuf))) {
		err_print("Inquiry failed\n");
		return (-1);
	}
	inq = (struct scsi_inquiry *) rawbuf;
	flag = (inq->inq_rdf == RDF_SCSI2);

	/*
	 * Reserve the scsi disk before performing mode select and
	 * format operations. This will keep other hosts, if any, from
	 * touching the disk while we are here.
	 */
	if (uscsi_reserve_release(cur_file, SCMD_RESERVE)) {
		err_print("Reserve failed\n");
		return (-1);
	}

	/*
	 * Set up the various SCSI parameters specified before
	 * formatting the disk.  Each routine handles the
	 * parameters relevent to a particular page.
	 * If no parameters are specified for a page, there's
	 * no need to do anything.  Otherwise, issue a mode
	 * sense for that page.  If a specified parameter
	 * differs from the drive's default value, and that
	 * parameter is not fixed, then issue a mode select to
	 * set the default value for the disk as specified
	 * in format.dat.
	 */
	if (scsi_ms_page1(flag) || scsi_ms_page2(flag) ||
		scsi_ms_page4(flag) || scsi_ms_page38(flag) ||
			scsi_ms_page8(flag) || scsi_ms_page3(flag)) {
		uscsi_reserve_release(cur_file, SCMD_RELEASE);
		return (-1);
	}

	/*
	 * If we're debugging the drive, dump every page
	 * the device supports, for thorough analysis.
	 */
	if (option_msg && diag_msg) {
		(void) scsi_dump_mode_sense_pages(MODE_SENSE_PC_DEFAULT);
		(void) scsi_dump_mode_sense_pages(MODE_SENSE_PC_CURRENT);
		(void) scsi_dump_mode_sense_pages(MODE_SENSE_PC_SAVED);
		(void) scsi_dump_mode_sense_pages(MODE_SENSE_PC_CHANGEABLE);
		err_print("\n");
	}

	/*
	 * Construct the uscsi format ioctl.  The form depends
	 * upon the defect list the user extracted.  If s/he
	 * extracted the "original" list, we format with only
	 * the P (manufacturer's defect) list.  Otherwise, we
	 * format with both the P and the G (grown) list.
	 * To format with the P and G list, we set the fmtData
	 * bit, and send an empty list.  To format with the
	 * P list only, we also set the cmpLst bit, meaning
	 * that the (empty) list we send down is the complete
	 * G list, thereby discarding the old G list..
	 */
	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	(void) memset((char *)&defect_hdr, 0, sizeof (defect_hdr));
	cdb.scc_cmd = SCMD_FORMAT;
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_cdblen = CDB_GROUP0;
	ucmd.uscsi_bufaddr = (caddr_t) &defect_hdr;
	ucmd.uscsi_buflen = sizeof (defect_hdr);
	cdb.cdb_opaque[1] = FPB_DATA;
	if ((list->list != NULL) && ((list->flags & LIST_PGLIST) == 0)) {
		/*
		 * No G list.  The empty list we send down
		 * is the complete list.
		 */
		cdb.cdb_opaque[1] |= FPB_CMPLT;
	}

	/*
	 * Issue the format ioctl
	 */
	fmt_print("Formatting...\n");
	(void) fflush(stdout);
	status = uscsi_cmd(cur_file, &ucmd,
		(option_msg && diag_msg) ? F_NORMAL : F_SILENT);
	if (status != 0) {
		fmt_print("Format failed\n");
		uscsi_reserve_release(cur_file, SCMD_RESERVE);
		status = scsi_raw_format(ucmd.uscsi_timeout);
	}
	uscsi_reserve_release(cur_file, SCMD_RELEASE);
	return (status);
}

/*
 * Format without any of the standard mode selects ignoring Grown defects list.
 */
static int
scsi_raw_format(int format_timeout)
{
	struct uscsi_cmd	ucmd;
	union scsi_cdb		cdb;
	struct scsi_defect_hdr	defect_hdr;
	int			status;

	fmt_print("\n"
	    "Retry of formatting operation without any of the standard\n"
	    "mode selects and ignoring disk's Grown Defects list.  The\n"
	    "disk may be able to be reformatted this way if an earlier\n"
	    "formatting operation was interrupted by a power failure or\n"
	    "SCSI bus reset.  The Grown Defects list will be recreated\n"
	    "by format verification and surface analysis.\n\n");

	if (check("Retry format without mode selects and Grown Defects list")
		!= 0) {
		return (-1);
	}

	/*
	 * Construct the uscsi format ioctl.
	 * To format with the P and G list, we set the fmtData
	 * and cmpLst bits to zero.  To format with just the
	 * P list, we set the fmtData bit (meaning that we will
	 * send down a defect list in the data phase) and the
	 * cmpLst bit (meaning that the list we send is the
	 * complete G list), and a defect list header with
	 * a defect list length of zero.
	 */
	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	cdb.scc_cmd = SCMD_FORMAT;
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_cdblen = CDB_GROUP0;

	/*
	 * No G list.   Send empty defect list to replace it.
	 */
	cdb.cdb_opaque[1] = FPB_DATA | FPB_CMPLT | FPB_BFI;
	(void) memset((char *)&defect_hdr, 0, sizeof (defect_hdr));
	ucmd.uscsi_bufaddr = (caddr_t) &defect_hdr;
	ucmd.uscsi_buflen = sizeof (defect_hdr);

	/*
	 * Use the timeout from the earlier attempt of formatting the
	 * drive. This is a retry of format, we want to avoid doing any
	 * other cmd on the drive (format time calculation uses mode sense).
	 */
	ucmd.uscsi_timeout = format_timeout;

	/*
	 * Issue the format ioctl
	 */
	fmt_print("Formatting...\n");
	(void) fflush(stdout);
	status = uscsi_cmd(cur_file, &ucmd, F_NORMAL);
	/* fmt_print(status ? "Format failed\n\n" : "Format ok\n\n"); */
	return (status);
}

/*
 * Estimate the time required for format operation (See 1163770).
 * format time = (5_revs * p4_heads * p4_cylinders) / p4_rpm
 * 5 revolutions (correspond to format_time keyword in format.dat file) are:
 *	1 rev.  for positioning
 *	2 rev.  for writing the track
 *	1 rev.  for positioning
 *	1 rev.  for cerifying the data integrity of the track
 * The return value is a good estimate on the formatting time in minutes.
 * Caller should add 50% margin to cover defect management overhead.
 */
int
scsi_format_time()
{
	struct mode_geometry		*page4;
	struct scsi_ms_header		header;
	int				status;
	int				p4_cylinders, p4_heads, p4_rpm;
	int				length;
	int				format_time;
	union {
		struct mode_geometry	page4;
		char			rawbuf[MAX_MODE_SENSE_SIZE];
	} u_page4;

	page4 = &u_page4.page4;

	/*
	 * Issue a mode sense to determine the default parameters
	 * If it fail, try to use the saved or current instead.
	 */
	status = uscsi_mode_sense(cur_file, DAD_MODE_GEOMETRY,
		MODE_SENSE_PC_DEFAULT, (caddr_t) page4,
		MAX_MODE_SENSE_SIZE, &header);

	if (status) {
		status = uscsi_mode_sense(cur_file, DAD_MODE_GEOMETRY,
			MODE_SENSE_PC_SAVED, (caddr_t) page4,
			MAX_MODE_SENSE_SIZE, &header);
	}
	if (status) {
		status = uscsi_mode_sense(cur_file, DAD_MODE_GEOMETRY,
			MODE_SENSE_PC_CURRENT, (caddr_t) page4,
			MAX_MODE_SENSE_SIZE, &header);
	}
	if (status) {
		return (0);
	}

	/*
	 * We only need the common subset between the CCS
	 * and SCSI-2 structures, so we can treat both
	 * cases identically.
	 */
	length = MODESENSE_PAGE_LEN(page4);
	if (length < MIN_PAGE4_LEN) {
		return (0);
	}

#ifdef _LITTLE_ENDIAN
	page4->rpm = ntohs(page4->rpm);
#endif
	p4_cylinders = (page4->cyl_ub << 16) + (page4->cyl_mb << 8) +
		page4->cyl_lb;
	p4_heads = page4->heads;
	p4_rpm = page4->rpm;

	/*
	 * Some drives report 0 for page4->rpm, adjust it to AVG_RPM, 3600.
	 */
	if (p4_rpm < MIN_RPM || p4_rpm > MAX_RPM) {
		err_print("Mode sense page(4) reports rpm value as %d,"
			" adjusting it to %d\n", p4_rpm, AVG_RPM);
		p4_rpm = AVG_RPM;
	}

	if (p4_cylinders <= 0 || p4_heads <= 0)
		return (0);

	format_time = ((scsi_format_revolutions * p4_heads *
			p4_cylinders) + p4_rpm) / p4_rpm;

	if (option_msg && diag_msg) {
		err_print("       pcyl:    %d\n", p4_cylinders);
		err_print("      heads:    %d\n", p4_heads);
		err_print("        rpm:    %d\n", p4_rpm);
		err_print("format_time:    %d minutes\n", format_time);
	}
	return (format_time);
}

/*
 * Check disk error recovery parameters via mode sense.
 * Issue a mode select if we need to change something.
 */
/*ARGSUSED*/
int
scsi_ms_page1(scsi2_flag)
	int	scsi2_flag;
{
	struct mode_err_recov		*page1;
	struct mode_err_recov		*fixed;
	struct scsi_ms_header		header;
	struct scsi_ms_header		fixed_hdr;
	int				status;
	int				tmp1, tmp2;
	int				flag;
	int				length;
	int				sp_flags;
	union {
		struct mode_err_recov	page1;
		char			rawbuf[MAX_MODE_SENSE_SIZE];
	} u_page1, u_fixed;


	page1 = &u_page1.page1;
	fixed = &u_fixed.page1;

	/*
	 * If debugging, issue mode senses on the default and
	 * current values.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, DAD_MODE_ERR_RECOV,
			MODE_SENSE_PC_DEFAULT, (caddr_t) page1,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, DAD_MODE_ERR_RECOV,
			MODE_SENSE_PC_CURRENT, (caddr_t) page1,
			MAX_MODE_SENSE_SIZE, &header);
	}

	/*
	 * Issue a mode sense to determine the saved parameters
	 * If the saved values fail, use the current instead.
	 */
	status = uscsi_mode_sense(cur_file, DAD_MODE_ERR_RECOV,
			MODE_SENSE_PC_SAVED, (caddr_t) page1,
			MAX_MODE_SENSE_SIZE, &header);
	if (status) {
		status = uscsi_mode_sense(cur_file, DAD_MODE_ERR_RECOV,
			MODE_SENSE_PC_CURRENT, (caddr_t) page1,
			MAX_MODE_SENSE_SIZE, &header);
		if (status) {
			return (0);
		}
	}

	/*
	 * We only need the common subset between the CCS
	 * and SCSI-2 structures, so we can treat both
	 * cases identically.  Whatever the drive gives
	 * us, we return to the drive in the mode select,
	 * delta'ed by whatever we want to change.
	 */
	length = MODESENSE_PAGE_LEN(page1);
	if (length < MIN_PAGE1_LEN) {
		return (0);
	}

	/*
	 * Ask for changeable parameters.
	 */
	status = uscsi_mode_sense(cur_file, DAD_MODE_ERR_RECOV,
		MODE_SENSE_PC_CHANGEABLE, (caddr_t) fixed,
		MAX_MODE_SENSE_SIZE, &fixed_hdr);
	if (status || MODESENSE_PAGE_LEN(fixed) < MIN_PAGE1_LEN) {
		return (0);
	}

	/*
	 * We need to issue a mode select only if one or more
	 * parameters need to be changed, and those parameters
	 * are flagged by the drive as changeable.
	 */
	flag = 0;
	tmp1 = page1->read_retry_count;
	tmp2 = page1->write_retry_count;
	if (cur_dtype->dtype_options & SUP_READ_RETRIES &&
			fixed->read_retry_count != 0) {
		flag |= (page1->read_retry_count !=
				cur_dtype->dtype_read_retries);
		page1->read_retry_count = cur_dtype->dtype_read_retries;
	}
	if (length > 8) {
		if (cur_dtype->dtype_options & SUP_WRITE_RETRIES &&
				fixed->write_retry_count != 0) {
			flag |= (page1->write_retry_count !=
					cur_dtype->dtype_write_retries);
			page1->write_retry_count =
					cur_dtype->dtype_write_retries;
		}
	}
	/*
	 * Report any changes so far...
	 */
	if (flag && option_msg) {
		fmt_print(
"PAGE 1: read retries= %d (%d)  write retries= %d (%d)\n",
			page1->read_retry_count, tmp1,
			page1->write_retry_count, tmp2);
	}
	/*
	 * Apply any changes requested via the change list method
	 */
	flag |= apply_chg_list(DAD_MODE_ERR_RECOV, length,
		(u_char *) page1, (u_char *) fixed,
			cur_dtype->dtype_chglist);
	/*
	 * If no changes required, do not issue a mode select
	 */
	if (flag == 0) {
		return (0);
	}
	/*
	 * We always want to set the Page Format bit for mode
	 * selects.  Set the Save Page bit if the drive indicates
	 * that it can save this page via the mode sense.
	 */
	sp_flags = MODE_SELECT_PF;
	if (page1->mode_page.ps) {
		sp_flags |= MODE_SELECT_SP;
	}
	page1->mode_page.ps = 0;
	header.mode_header.length = 0;
	header.mode_header.device_specific = 0;
	status = uscsi_mode_select(cur_file, DAD_MODE_ERR_RECOV,
		sp_flags, (caddr_t) page1, length, &header);
	if (status && (sp_flags & MODE_SELECT_SP)) {
		/* If failed, try not saving mode select params. */
		sp_flags &= ~MODE_SELECT_SP;
		status = uscsi_mode_select(cur_file, DAD_MODE_ERR_RECOV,
			sp_flags, (caddr_t) page1, length, &header);
		}
	if (status && option_msg) {
		err_print("\
Warning: Using default error recovery parameters.\n\n");
	}

	/*
	 * If debugging, issue mode senses on the current and
	 * saved values, so we can see the result of the mode
	 * selects.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, DAD_MODE_ERR_RECOV,
			MODE_SENSE_PC_CURRENT, (caddr_t) page1,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, DAD_MODE_ERR_RECOV,
			MODE_SENSE_PC_SAVED, (caddr_t) page1,
			MAX_MODE_SENSE_SIZE, &header);
	}

	return (0);
}

/*
 * Check disk disconnect/reconnect parameters via mode sense.
 * Issue a mode select if we need to change something.
 */
/*ARGSUSED*/
int
scsi_ms_page2(scsi2_flag)
	int	scsi2_flag;
{
	struct mode_disco_reco		*page2;
	struct mode_disco_reco		*fixed;
	struct scsi_ms_header		header;
	struct scsi_ms_header		fixed_hdr;
	int				status;
	int				flag;
	int				length;
	int				sp_flags;
	union {
		struct mode_disco_reco	page2;
		char			rawbuf[MAX_MODE_SENSE_SIZE];
	} u_page2, u_fixed;

	page2 = &u_page2.page2;
	fixed = &u_fixed.page2;

	/*
	 * If debugging, issue mode senses on the default and
	 * current values.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, MODEPAGE_DISCO_RECO,
			MODE_SENSE_PC_DEFAULT, (caddr_t) page2,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, MODEPAGE_DISCO_RECO,
			MODE_SENSE_PC_CURRENT, (caddr_t) page2,
			MAX_MODE_SENSE_SIZE, &header);
	}

	/*
	 * Issue a mode sense to determine the saved parameters
	 * If the saved values fail, use the current instead.
	 */
	status = uscsi_mode_sense(cur_file, MODEPAGE_DISCO_RECO,
			MODE_SENSE_PC_SAVED, (caddr_t) page2,
			MAX_MODE_SENSE_SIZE, &header);
	if (status) {
		status = uscsi_mode_sense(cur_file, MODEPAGE_DISCO_RECO,
			MODE_SENSE_PC_CURRENT, (caddr_t) page2,
			MAX_MODE_SENSE_SIZE, &header);
		if (status) {
			return (0);
		}
	}

	/*
	 * We only need the common subset between the CCS
	 * and SCSI-2 structures, so we can treat both
	 * cases identically.  Whatever the drive gives
	 * us, we return to the drive in the mode select,
	 * delta'ed by whatever we want to change.
	 */
	length = MODESENSE_PAGE_LEN(page2);
	if (length < MIN_PAGE2_LEN) {
		return (0);
	}

	/*
	 * Ask for changeable parameters.
	 */
	status = uscsi_mode_sense(cur_file, MODEPAGE_DISCO_RECO,
		MODE_SENSE_PC_CHANGEABLE, (caddr_t) fixed,
		MAX_MODE_SENSE_SIZE, &fixed_hdr);
	if (status || MODESENSE_PAGE_LEN(fixed) < MIN_PAGE2_LEN) {
		return (0);
	}

	/*
	 * We need to issue a mode select only if one or more
	 * parameters need to be changed, and those parameters
	 * are flagged by the drive as changeable.
	 */
	flag = 0;
	/*
	 * Apply any changes requested via the change list method
	 */
	flag |= apply_chg_list(MODEPAGE_DISCO_RECO, length,
		(u_char *) page2, (u_char *) fixed,
			cur_dtype->dtype_chglist);
	/*
	 * If no changes required, do not issue a mode select
	 */
	if (flag == 0) {
		return (0);
	}
	/*
	 * We always want to set the Page Format bit for mode
	 * selects.  Set the Save Page bit if the drive indicates
	 * that it can save this page via the mode sense.
	 */
	sp_flags = MODE_SELECT_PF;
	if (page2->mode_page.ps) {
		sp_flags |= MODE_SELECT_SP;
	}
	page2->mode_page.ps = 0;
	header.mode_header.length = 0;
	header.mode_header.device_specific = 0;
	status = uscsi_mode_select(cur_file, MODEPAGE_DISCO_RECO,
		MODE_SELECT_SP, (caddr_t) page2, length, &header);
	if (status && (sp_flags & MODE_SELECT_SP)) {
		/* If failed, try not saving mode select params. */
		sp_flags &= ~MODE_SELECT_SP;
		status = uscsi_mode_select(cur_file, MODEPAGE_DISCO_RECO,
			sp_flags, (caddr_t) page2, length, &header);
		}
	if (status && option_msg) {
		err_print("Warning: Using default .\n\n");
	}

	/*
	 * If debugging, issue mode senses on the current and
	 * saved values, so we can see the result of the mode
	 * selects.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, MODEPAGE_DISCO_RECO,
			MODE_SENSE_PC_CURRENT, (caddr_t) page2,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, MODEPAGE_DISCO_RECO,
			MODE_SENSE_PC_SAVED, (caddr_t) page2,
			MAX_MODE_SENSE_SIZE, &header);
	}

	return (0);
}

/*
 * Check disk format parameters via mode sense.
 * Issue a mode select if we need to change something.
 */
/*ARGSUSED*/
int
scsi_ms_page3(scsi2_flag)
	int	scsi2_flag;
{
	struct mode_format		*page3;
	struct mode_format		*fixed;
	struct scsi_ms_header		header;
	struct scsi_ms_header		fixed_hdr;
	int				status;
	int				tmp1, tmp2, tmp3;
	int				tmp4, tmp5, tmp6;
	int				flag;
	int				length;
	int				sp_flags;
	union {
		struct mode_format	page3;
		char			rawbuf[MAX_MODE_SENSE_SIZE];
	} u_page3, u_fixed;


	page3 = &u_page3.page3;
	fixed = &u_fixed.page3;

	/*
	 * If debugging, issue mode senses on the default and
	 * current values.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, DAD_MODE_FORMAT,
			MODE_SENSE_PC_DEFAULT, (caddr_t) page3,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, DAD_MODE_FORMAT,
			MODE_SENSE_PC_CURRENT, (caddr_t) page3,
			MAX_MODE_SENSE_SIZE, &header);
	}

	/*
	 * Issue a mode sense to determine the saved parameters
	 * If the saved values fail, use the current instead.
	 */
	status = uscsi_mode_sense(cur_file, DAD_MODE_FORMAT,
			MODE_SENSE_PC_SAVED, (caddr_t) page3,
			MAX_MODE_SENSE_SIZE, &header);
	if (status) {
		status = uscsi_mode_sense(cur_file, DAD_MODE_FORMAT,
			MODE_SENSE_PC_CURRENT, (caddr_t) page3,
			MAX_MODE_SENSE_SIZE, &header);
		if (status) {
			return (0);
		}
	}

	/*
	 * We only need the common subset between the CCS
	 * and SCSI-2 structures, so we can treat both
	 * cases identically.  Whatever the drive gives
	 * us, we return to the drive in the mode select,
	 * delta'ed by whatever we want to change.
	 */
	length = MODESENSE_PAGE_LEN(page3);
	if (length < MIN_PAGE3_LEN) {
		return (0);
	}

	/*
	 * Ask for changeable parameters.
	 */
	status = uscsi_mode_sense(cur_file, DAD_MODE_FORMAT,
		MODE_SENSE_PC_CHANGEABLE, (caddr_t) fixed,
		MAX_MODE_SENSE_SIZE, &fixed_hdr);
	if (status || MODESENSE_PAGE_LEN(fixed) < MIN_PAGE3_LEN) {
		return (0);
	}

	/*
	 * We need to issue a mode select only if one or more
	 * parameters need to be changed, and those parameters
	 * are flagged by the drive as changeable.
	 */
	tmp1 = page3->track_skew;
	tmp2 = page3->cylinder_skew;
	tmp3 = page3->sect_track;
	tmp4 = page3->tracks_per_zone;
	tmp5 = page3->alt_tracks_vol;
	tmp6 = page3->alt_sect_zone;

	flag = (page3->data_bytes_sect != SECSIZE);
	page3->data_bytes_sect = SECSIZE;

	flag |= (page3->interleave != 1);
	page3->interleave = 1;

	if (cur_dtype->dtype_options & SUP_CYLSKEW &&
					fixed->cylinder_skew != 0) {
		flag |= (page3->cylinder_skew != cur_dtype->dtype_cyl_skew);
		page3->cylinder_skew = cur_dtype->dtype_cyl_skew;
	}
	if (cur_dtype->dtype_options & SUP_TRKSKEW &&
					fixed->track_skew != 0) {
		flag |= (page3->track_skew != cur_dtype->dtype_trk_skew);
		page3->track_skew = cur_dtype->dtype_trk_skew;
	}
	if (cur_dtype->dtype_options & SUP_PSECT &&
					fixed->sect_track != 0) {
		flag |= (page3->sect_track != psect);
		page3->sect_track = (u_short)psect;
	}
	if (cur_dtype->dtype_options & SUP_TRKS_ZONE &&
					fixed->tracks_per_zone != 0) {
		flag |= (page3->tracks_per_zone != cur_dtype->dtype_trks_zone);
		page3->tracks_per_zone = cur_dtype->dtype_trks_zone;
	}
	if (cur_dtype->dtype_options & SUP_ASECT &&
					fixed->alt_sect_zone != 0) {
		flag |= (page3->alt_sect_zone != cur_dtype->dtype_asect);
		page3->alt_sect_zone = cur_dtype->dtype_asect;
	}
	if (cur_dtype->dtype_options & SUP_ATRKS &&
					fixed->alt_tracks_vol != 0) {
		flag |= (page3->alt_tracks_vol != cur_dtype->dtype_atrks);
		page3->alt_tracks_vol = cur_dtype->dtype_atrks;
	}
	/*
	 * Notify user of any changes so far
	 */
	if (flag && option_msg) {
		fmt_print("PAGE 3: trk skew= %d (%d)   cyl skew= %d (%d)   ",
			page3->track_skew, tmp1, page3->cylinder_skew, tmp2);
		fmt_print("sects/trk= %d (%d)\n", page3->sect_track, tmp3);
		fmt_print("        trks/zone= %d (%d)   alt trks= %d (%d)   ",
			page3->tracks_per_zone, tmp4,
			page3->alt_tracks_vol, tmp5);
		fmt_print("alt sects/zone= %d (%d)\n",
				page3->alt_sect_zone, tmp6);
	}
	/*
	 * Apply any changes requested via the change list method
	 */
	flag |= apply_chg_list(DAD_MODE_FORMAT, length,
		(u_char *) page3, (u_char *) fixed,
			cur_dtype->dtype_chglist);
	/*
	 * If no changes required, do not issue a mode select
	 */
	if (flag == 0) {
		return (0);
	}
	/*
	 * Issue a mode select
	 */
	/*
	 * We always want to set the Page Format bit for mode
	 * selects.  Set the Save Page bit if the drive indicates
	 * that it can save this page via the mode sense.
	 */
	sp_flags = MODE_SELECT_PF;
	if (page3->mode_page.ps) {
		sp_flags |= MODE_SELECT_SP;
	}
	page3->mode_page.ps = 0;
	header.mode_header.length = 0;
	header.mode_header.device_specific = 0;
	status = uscsi_mode_select(cur_file, DAD_MODE_FORMAT,
		MODE_SELECT_SP, (caddr_t) page3, length, &header);
	if (status && (sp_flags & MODE_SELECT_SP)) {
		/* If failed, try not saving mode select params. */
		sp_flags &= ~MODE_SELECT_SP;
		status = uscsi_mode_select(cur_file, DAD_MODE_FORMAT,
			sp_flags, (caddr_t) page3, length, &header);
		}
	if (status && option_msg) {
		err_print("Warning: Using default drive format parameters.\n");
		err_print("Warning: Drive format may not be correct.\n\n");
	}

	/*
	 * If debugging, issue mode senses on the current and
	 * saved values, so we can see the result of the mode
	 * selects.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, DAD_MODE_FORMAT,
			MODE_SENSE_PC_CURRENT, (caddr_t) page3,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, DAD_MODE_FORMAT,
			MODE_SENSE_PC_SAVED, (caddr_t) page3,
			MAX_MODE_SENSE_SIZE, &header);
	}

	return (0);
}

/*
 * Check disk geometry parameters via mode sense.
 * Issue a mode select if we need to change something.
 */
/*ARGSUSED*/
int
scsi_ms_page4(scsi2_flag)
	int	scsi2_flag;
{
	struct mode_geometry		*page4;
	struct mode_geometry		*fixed;
	struct scsi_ms_header		header;
	struct scsi_ms_header		fixed_hdr;
	int				status;
	int				tmp1, tmp2;
	int				flag;
	int				length;
	int				sp_flags;
	union {
		struct mode_geometry	page4;
		char			rawbuf[MAX_MODE_SENSE_SIZE];
	} u_page4, u_fixed;

	page4 = &u_page4.page4;
	fixed = &u_fixed.page4;

	/*
	 * If debugging, issue mode senses on the default and
	 * current values.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, DAD_MODE_GEOMETRY,
			MODE_SENSE_PC_DEFAULT, (caddr_t) page4,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, DAD_MODE_GEOMETRY,
			MODE_SENSE_PC_CURRENT, (caddr_t) page4,
			MAX_MODE_SENSE_SIZE, &header);
	}

	/*
	 * Issue a mode sense to determine the saved parameters
	 * If the saved values fail, use the current instead.
	 */
	status = uscsi_mode_sense(cur_file, DAD_MODE_GEOMETRY,
			MODE_SENSE_PC_SAVED, (caddr_t) page4,
			MAX_MODE_SENSE_SIZE, &header);
	if (status) {
		status = uscsi_mode_sense(cur_file, DAD_MODE_GEOMETRY,
			MODE_SENSE_PC_CURRENT, (caddr_t) page4,
			MAX_MODE_SENSE_SIZE, &header);
		if (status) {
			return (0);
		}
	}

	/*
	 * We only need the common subset between the CCS
	 * and SCSI-2 structures, so we can treat both
	 * cases identically.  Whatever the drive gives
	 * us, we return to the drive in the mode select,
	 * delta'ed by whatever we want to change.
	 */
	length = MODESENSE_PAGE_LEN(page4);
	if (length < MIN_PAGE4_LEN) {
		return (0);
	}

	/*
	 * Ask for changeable parameters.
	 */
	status = uscsi_mode_sense(cur_file, DAD_MODE_GEOMETRY,
		MODE_SENSE_PC_CHANGEABLE, (caddr_t) fixed,
		MAX_MODE_SENSE_SIZE, &fixed_hdr);
	if (status || MODESENSE_PAGE_LEN(fixed) < MIN_PAGE4_LEN) {
		return (0);
	}

	/*
	 * We need to issue a mode select only if one or more
	 * parameters need to be changed, and those parameters
	 * are flagged by the drive as changeable.
	 */
	tmp1 = (page4->cyl_ub << 16) + (page4->cyl_mb << 8) + page4->cyl_lb;
	tmp2 = page4->heads;

	flag = 0;
	if ((cur_dtype->dtype_options & SUP_PHEAD) && fixed->heads != 0) {
		flag |= (page4->heads != phead);
		page4->heads = phead;
	}
	/*
	 * Notify user of changes so far
	 */
	if (flag && option_msg) {
		fmt_print("PAGE 4:   cylinders= %d    heads= %d (%d)\n",
			tmp1, page4->heads, tmp2);
	}
	/*
	 * Apply any changes requested via the change list method
	 */
	flag |= apply_chg_list(DAD_MODE_GEOMETRY, length,
		(u_char *) page4, (u_char *) fixed,
			cur_dtype->dtype_chglist);
	/*
	 * If no changes required, do not issue a mode select
	 */
	if (flag == 0) {
		return (0);
	}
	/*
	 * Issue a mode select
	 */
	/*
	 * We always want to set the Page Format bit for mode
	 * selects.  Set the Save Page bit if the drive indicates
	 * that it can save this page via the mode sense.
	 */
	sp_flags = MODE_SELECT_PF;
	if (page4->mode_page.ps) {
		sp_flags |= MODE_SELECT_SP;
	}
	page4->mode_page.ps = 0;
	header.mode_header.length = 0;
	header.mode_header.device_specific = 0;
	status = uscsi_mode_select(cur_file, DAD_MODE_GEOMETRY,
		MODE_SELECT_SP, (caddr_t) page4, length, &header);
	if (status && (sp_flags & MODE_SELECT_SP)) {
		/* If failed, try not saving mode select params. */
		sp_flags &= ~MODE_SELECT_SP;
		status = uscsi_mode_select(cur_file, DAD_MODE_GEOMETRY,
			sp_flags, (caddr_t) page4, length, &header);
		}
	if (status && option_msg) {
		err_print("Warning: Using default drive geometry.\n\n");
	}

	/*
	 * If debugging, issue mode senses on the current and
	 * saved values, so we can see the result of the mode
	 * selects.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, DAD_MODE_GEOMETRY,
			MODE_SENSE_PC_CURRENT, (caddr_t) page4,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, DAD_MODE_GEOMETRY,
			MODE_SENSE_PC_SAVED, (caddr_t) page4,
			MAX_MODE_SENSE_SIZE, &header);
	}

	return (0);
}

/*
 * Check SCSI-2 disk cache parameters via mode sense.
 * Issue a mode select if we need to change something.
 */
/*ARGSUSED*/
static int
scsi_ms_page8(scsi2_flag)
	int	scsi2_flag;
{
	struct mode_cache		*page8;
	struct mode_cache		*fixed;
	struct scsi_ms_header		header;
	struct scsi_ms_header		fixed_hdr;
	int				status;
	int				flag;
	int				length;
	int				sp_flags;
	union {
		struct mode_cache	page8;
		char			rawbuf[MAX_MODE_SENSE_SIZE];
	} u_page8, u_fixed;

	page8 = &u_page8.page8;
	fixed = &u_fixed.page8;

	/*
	 * Only SCSI-2 devices support this page
	 */
	if (!scsi2_flag) {
		return (0);
	}

	/*
	 * If debugging, issue mode senses on the default and
	 * current values.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, DAD_MODE_CACHE,
			MODE_SENSE_PC_DEFAULT, (caddr_t) page8,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, DAD_MODE_CACHE,
			MODE_SENSE_PC_CURRENT, (caddr_t) page8,
			MAX_MODE_SENSE_SIZE, &header);
	}

	/*
	 * Issue a mode sense to determine the saved parameters
	 * If the saved values fail, use the current instead.
	 */
	status = uscsi_mode_sense(cur_file, DAD_MODE_CACHE,
			MODE_SENSE_PC_SAVED, (caddr_t) page8,
			MAX_MODE_SENSE_SIZE, &header);
	if (status) {
		status = uscsi_mode_sense(cur_file, DAD_MODE_CACHE,
			MODE_SENSE_PC_CURRENT, (caddr_t) page8,
			MAX_MODE_SENSE_SIZE, &header);
		if (status) {
			return (0);
		}
	}

	/*
	 * We only need the common subset between the CCS
	 * and SCSI-2 structures, so we can treat both
	 * cases identically.  Whatever the drive gives
	 * us, we return to the drive in the mode select,
	 * delta'ed by whatever we want to change.
	 */
	length = MODESENSE_PAGE_LEN(page8);
	if (length < MIN_PAGE8_LEN) {
		return (0);
	}

	/*
	 * Ask for changeable parameters.
	 */
	status = uscsi_mode_sense(cur_file, DAD_MODE_CACHE,
		MODE_SENSE_PC_CHANGEABLE, (caddr_t) fixed,
		MAX_MODE_SENSE_SIZE, &fixed_hdr);
	if (status || MODESENSE_PAGE_LEN(fixed) < MIN_PAGE8_LEN) {
		return (0);
	}

	/*
	 * We need to issue a mode select only if one or more
	 * parameters need to be changed, and those parameters
	 * are flagged by the drive as changeable.
	 */
	flag = 0;
	/*
	 * Apply any changes requested via the change list method
	 */
	flag |= apply_chg_list(DAD_MODE_CACHE, length,
		(u_char *) page8, (u_char *) fixed,
			cur_dtype->dtype_chglist);
	/*
	 * If no changes required, do not issue a mode select
	 */
	if (flag == 0) {
		return (0);
	}
	/*
	 * Issue a mode select
	 */
	/*
	 * We always want to set the Page Format bit for mode
	 * selects.  Set the Save Page bit if the drive indicates
	 * that it can save this page via the mode sense.
	 */
	sp_flags = MODE_SELECT_PF;
	if (page8->mode_page.ps) {
		sp_flags |= MODE_SELECT_SP;
	}
	page8->mode_page.ps = 0;
	header.mode_header.length = 0;
	header.mode_header.device_specific = 0;
	status = uscsi_mode_select(cur_file, DAD_MODE_CACHE,
		sp_flags, (caddr_t) page8, length, &header);
	if (status && (sp_flags & MODE_SELECT_SP)) {
		/* If failed, try not saving mode select params. */
		sp_flags &= ~MODE_SELECT_SP;
		status = uscsi_mode_select(cur_file, DAD_MODE_CACHE,
			sp_flags, (caddr_t) page8, length, &header);
		}
	if (status && option_msg) {
		err_print("\
Warning: Using default SCSI-2 cache parameters.\n\n");
	}

	/*
	 * If debugging, issue mode senses on the current and
	 * saved values, so we can see the result of the mode
	 * selects.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, DAD_MODE_CACHE,
			MODE_SENSE_PC_CURRENT, (caddr_t) page8,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, DAD_MODE_CACHE,
			MODE_SENSE_PC_SAVED, (caddr_t) page8,
			MAX_MODE_SENSE_SIZE, &header);
	}

	return (0);
}

/*
 * Check CCS disk cache parameters via mode sense.
 * Issue a mode select if we need to change something.
 */
/*ARGSUSED*/
static int
scsi_ms_page38(scsi2_flag)
	int	scsi2_flag;
{
	struct mode_cache_ccs		*page38;
	struct mode_cache_ccs		*fixed;
	struct scsi_ms_header		header;
	struct scsi_ms_header		fixed_hdr;
	int				status;
	int				tmp1, tmp2, tmp3, tmp4;
	int				flag;
	int				length;
	int				sp_flags;
	union {
		struct mode_cache_ccs	page38;
		char			rawbuf[MAX_MODE_SENSE_SIZE];
	} u_page38, u_fixed;

	/*
	 * First, determine if we need to look at page 38 at all.
	 * Not all devices support it.
	 */
	if (((cur_dtype->dtype_options & (SUP_CACHE | SUP_PREFETCH |
		SUP_CACHE_MIN | SUP_CACHE_MAX)) == 0) &&
			(!chg_list_affects_page(cur_dtype->dtype_chglist,
				0x38))) {
		return (0);
	}

	page38 = &u_page38.page38;
	fixed = &u_fixed.page38;

	/*
	 * If debugging, issue mode senses on the default and
	 * current values.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, DAD_MODE_CACHE_CCS,
			MODE_SENSE_PC_DEFAULT, (caddr_t) page38,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, DAD_MODE_CACHE_CCS,
			MODE_SENSE_PC_CURRENT, (caddr_t) page38,
			MAX_MODE_SENSE_SIZE, &header);
	}

	/*
	 * Issue a mode sense to determine the saved parameters
	 * If the saved values fail, use the current instead.
	 */
	status = uscsi_mode_sense(cur_file, DAD_MODE_CACHE_CCS,
			MODE_SENSE_PC_SAVED, (caddr_t) page38,
			MAX_MODE_SENSE_SIZE, &header);
	if (status) {
		status = uscsi_mode_sense(cur_file, DAD_MODE_CACHE_CCS,
			MODE_SENSE_PC_CURRENT, (caddr_t) page38,
			MAX_MODE_SENSE_SIZE, &header);
		if (status) {
			return (0);
		}
	}

	/*
	 * We only need the common subset between the CCS
	 * and SCSI-2 structures, so we can treat both
	 * cases identically.  Whatever the drive gives
	 * us, we return to the drive in the mode select,
	 * delta'ed by whatever we want to change.
	 */
	length = MODESENSE_PAGE_LEN(page38);
	if (length < MIN_PAGE38_LEN) {
		return (0);
	}

	/*
	 * Ask for changeable parameters.
	 */
	status = uscsi_mode_sense(cur_file, DAD_MODE_CACHE_CCS,
		MODE_SENSE_PC_CHANGEABLE, (caddr_t) fixed,
		MAX_MODE_SENSE_SIZE, &fixed_hdr);
	if (status || MODESENSE_PAGE_LEN(fixed) < MIN_PAGE38_LEN) {
		return (0);
	}

	/*
	 * We need to issue a mode select only if one or more
	 * parameters need to be changed, and those parameters
	 * are flagged by the drive as changeable.
	 */
	tmp1 = page38->mode;
	tmp2 = page38->threshold;
	tmp3 = page38->min_prefetch;
	tmp4 = page38->max_prefetch;

	flag = 0;
	if ((cur_dtype->dtype_options & SUP_CACHE) &&
			(fixed->mode & cur_dtype->dtype_cache) ==
				cur_dtype->dtype_cache) {
		flag |= (page38->mode != cur_dtype->dtype_cache);
		page38->mode = cur_dtype->dtype_cache;
	}
	if ((cur_dtype->dtype_options & SUP_PREFETCH) &&
		(fixed->threshold & cur_dtype->dtype_threshold) ==
				cur_dtype->dtype_threshold) {
		flag |= (page38->threshold != cur_dtype->dtype_threshold);
		page38->threshold = cur_dtype->dtype_threshold;
	}
	if ((cur_dtype->dtype_options & SUP_CACHE_MIN) &&
		(fixed->min_prefetch & cur_dtype->dtype_prefetch_min) ==
				cur_dtype->dtype_prefetch_min) {
		flag |= (page38->min_prefetch != cur_dtype->dtype_prefetch_min);
		page38->min_prefetch = cur_dtype->dtype_prefetch_min;
	}
	if ((cur_dtype->dtype_options & SUP_CACHE_MAX) &&
		(fixed->max_prefetch & cur_dtype->dtype_prefetch_max) ==
				cur_dtype->dtype_prefetch_max) {
		flag |= (page38->max_prefetch != cur_dtype->dtype_prefetch_max);
		page38->max_prefetch = cur_dtype->dtype_prefetch_max;
	}
	/*
	 * Notify the user of changes up to this point
	 */
	if (flag && option_msg) {
		fmt_print("PAGE 38: cache mode= 0x%x (0x%x)\n",
					page38->mode, tmp1);
		fmt_print("         min. prefetch multiplier= %d   ",
					page38->min_multiplier);
		fmt_print("max. prefetch multiplier= %d\n",
					page38->max_multiplier);
		fmt_print("         threshold= %d (%d)   ",
					page38->threshold, tmp2);
		fmt_print("min. prefetch= %d (%d)   ",
					page38->min_prefetch, tmp3);
		fmt_print("max. prefetch= %d (%d)\n",
					page38->max_prefetch, tmp4);
	}
	/*
	 * Apply any changes requested via the change list method
	 */
	flag |= apply_chg_list(DAD_MODE_CACHE_CCS, length,
		(u_char *) page38, (u_char *) fixed,
			cur_dtype->dtype_chglist);
	/*
	 * If no changes required, do not issue a mode select
	 */
	if (flag == 0) {
		return (0);
	}
	/*
	 * Issue a mode select
	 *
	 * We always want to set the Page Format bit for mode
	 * selects.  Set the Save Page bit if the drive indicates
	 * that it can save this page via the mode sense.
	 */
	sp_flags = MODE_SELECT_PF;
	if (page38->mode_page.ps) {
		sp_flags |= MODE_SELECT_SP;
	}
	page38->mode_page.ps = 0;
	header.mode_header.length = 0;
	header.mode_header.device_specific = 0;
	status = uscsi_mode_select(cur_file, DAD_MODE_CACHE_CCS,
		sp_flags, (caddr_t) page38, length, &header);
	if (status && (sp_flags & MODE_SELECT_SP)) {
		/* If failed, try not saving mode select params. */
		sp_flags &= ~MODE_SELECT_SP;
		status = uscsi_mode_select(cur_file, DAD_MODE_CACHE_CCS,
			sp_flags, (caddr_t) page38, length, &header);
		}
	if (status && option_msg) {
		err_print("Warning: Using default CCS cache parameters.\n\n");
	}

	/*
	 * If debugging, issue mode senses on the current and
	 * saved values, so we can see the result of the mode
	 * selects.
	 */
	if (option_msg && diag_msg) {
		(void) uscsi_mode_sense(cur_file, DAD_MODE_CACHE_CCS,
			MODE_SENSE_PC_CURRENT, (caddr_t) page38,
			MAX_MODE_SENSE_SIZE, &header);
		(void) uscsi_mode_sense(cur_file, DAD_MODE_CACHE_CCS,
			MODE_SENSE_PC_SAVED, (caddr_t) page38,
			MAX_MODE_SENSE_SIZE, &header);
	}

	return (0);
}


/*
 * Extract the manufacturer's defect list.
 */
int
scsi_ex_man(list)
	struct  defect_list	*list;
{
	int	i;

	i = scsi_read_defect_data(list, DLD_MAN_DEF_LIST);
	if (i != 0)
		return (i);
	list->flags &= ~LIST_PGLIST;
	return (0);
}

/*
 * Extract the current defect list.
 * For embedded scsi drives, this means both the manufacturer's (P)
 * and the grown (G) lists.
 */
int
scsi_ex_cur(list)
	struct  defect_list *list;
{
	int	i;

	i = scsi_read_defect_data(list, DLD_GROWN_DEF_LIST|DLD_MAN_DEF_LIST);
	if (i != 0)
		return (i);
	list->flags |= LIST_PGLIST;
	return (0);
}


/*
 * Extract the grown list only
 */
int
scsi_ex_grown(list)
	struct defect_list *list;
{
	int	i;

	i = scsi_read_defect_data(list, DLD_GROWN_DEF_LIST);
	if (i != 0)
		return (i);
	list->flags |= LIST_PGLIST;
	return (0);
}


int
scsi_read_defect_data(list, pglist_flags)
	struct  defect_list	*list;
	int			pglist_flags;
{
	struct uscsi_cmd	ucmd;
	union scsi_cdb		cdb;
	struct scsi_defect_list	*defects;
	struct scsi_defect_list	def_list;
	struct scsi_defect_hdr	*hdr;
	int			status;
	int			nbytes;
	int			len;	/* returned defect list length */

	hdr = (struct scsi_defect_hdr *)&def_list;

	/*
	 * First get length of list by asking for the header only.
	 */
	(void) memset((char *)&def_list, 0, sizeof (def_list));

	/*
	 * Build and execute the uscsi ioctl
	 */
	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	cdb.scc_cmd = SCMD_READ_DEFECT_LIST;
	FORMG1COUNT(&cdb, sizeof (struct scsi_defect_hdr));
	cdb.cdb_opaque[2] = pglist_flags | DLD_BFI_FORMAT;
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_cdblen = CDB_GROUP1;
	ucmd.uscsi_bufaddr = (caddr_t) hdr;
	ucmd.uscsi_buflen = sizeof (struct scsi_defect_hdr);
	status = uscsi_cmd(cur_file, &ucmd,
		(option_msg && diag_msg) ? F_NORMAL : F_SILENT);

	if (status != 0) {
		if (option_msg) {
			err_print("No %s defect list.\n",
				pglist_flags & DLD_GROWN_DEF_LIST ?
				"grown" : "manufacturer's");
		}
		return (-1);
	}

	/*
	 * Read the full list the second time
	 */
	hdr->length = ntohs(hdr->length);
	len = hdr->length;
	nbytes = len + sizeof (struct scsi_defect_hdr);

	defects = zalloc(nbytes);
	*(struct scsi_defect_hdr *)defects = *(struct scsi_defect_hdr *)hdr;

	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	cdb.scc_cmd = SCMD_READ_DEFECT_LIST;
	FORMG1COUNT(&cdb, nbytes);
	cdb.cdb_opaque[2] = pglist_flags | DLD_BFI_FORMAT;
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_cdblen = CDB_GROUP1;
	ucmd.uscsi_bufaddr = (caddr_t) defects;
	ucmd.uscsi_buflen = nbytes;
	status = uscsi_cmd(cur_file, &ucmd,
		(option_msg && diag_msg) ? F_NORMAL : F_SILENT);

	if (status) {
		err_print("can't read defect list 2nd time");
		destroy_data((char *) defects);
		return (-1);
	}

	defects->length = ntohs(defects->length);

	if (len != hdr->length) {
		err_print("not enough defects");
		destroy_data((char *) defects);
		return (-1);
	}
	scsi_convert_list_to_new(list, (struct scsi_defect_list *)defects,
			DLD_BFI_FORMAT);
	destroy_data((char *) defects);
	return (0);
}


/*
 * Map a block.
 */
/*ARGSUSED*/
int
scsi_repair(bn, flag)
	int	bn;
	int	flag;
{
	struct uscsi_cmd		ucmd;
	union scsi_cdb			cdb;
	struct scsi_reassign_blk	defect_list;

	/*
	 * Build and execute the uscsi ioctl
	 */
	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	(void) memset((char *)&defect_list, 0,
		sizeof (struct scsi_reassign_blk));
	cdb.scc_cmd = SCMD_REASSIGN_BLOCK;
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_cdblen = CDB_GROUP0;
	ucmd.uscsi_bufaddr = (caddr_t) &defect_list;
	ucmd.uscsi_buflen = sizeof (struct scsi_reassign_blk);
	defect_list.length = sizeof (defect_list.defect);
	defect_list.length = htons(defect_list.length);
	defect_list.defect = bn;
	defect_list.defect = htonl(defect_list.defect);
	return (uscsi_cmd(cur_file, &ucmd,
		(option_msg && diag_msg) ? F_NORMAL : F_SILENT));
}

/*
 * Convert a SCSI-style defect list to our generic format.
 * We can handle different format lists.
 */
static void
scsi_convert_list_to_new(list, def_list, list_format)
	struct defect_list		*list;
	struct scsi_defect_list		*def_list;
	int				 list_format;
{
	register struct scsi_bfi_defect	*old_defect;
	register struct defect_entry	*new_defect;
	register int			len;
	register int			i;


	switch (list_format) {

	case DLD_BFI_FORMAT:
		/*
		 * Allocate space for the rest of the list.
		 */
		len = def_list->length / sizeof (struct scsi_bfi_defect);
		old_defect = def_list->list;
		new_defect = (struct defect_entry *)
				zalloc(LISTSIZE(len) * SECSIZE);

		list->header.count = len;
		list->header.magicno = (u_int) DEFECT_MAGIC;
		list->list = new_defect;

		for (i = 0; i < len; i++, new_defect++, old_defect++) {
			new_defect->cyl = (short) old_defect->cyl;
			new_defect->cyl = ntohs(new_defect->cyl);
			new_defect->head = (short) old_defect->head;
			new_defect->head = ntohs(new_defect->head);
			new_defect->bfi = (int) old_defect->bytes_from_index;
			new_defect->bfi = ntohl(new_defect->bfi);
			new_defect->nbits = UNKNOWN;	/* size of defect */
		}

		break;

	default:
		err_print("scsi_convert_list_to_new: can't deal with it\n");
		exit(0);
		/*NOTREACHED*/
	}

	(void) checkdefsum(list, CK_MAKESUM);
}



/*
 * Execute a command and determine the result.
 * Uses the "uscsi" ioctl interface, which is
 * fully supported.
 */
int
uscsi_cmd(fd, ucmd, flags)
	int			fd;
	struct uscsi_cmd	*ucmd;
	int			flags;
{
	struct scsi_extended_sense	*rq;
	char				rqbuf[255];
	int				status;
	int				rqlen;
	int				timeout = 0;

	/*
	 * Set function flags for driver.
	 */
	ucmd->uscsi_flags = USCSI_ISOLATE | USCSI_DIAGNOSE;
	if (flags & F_SILENT) {
		ucmd->uscsi_flags |= USCSI_SILENT;
	}

	/*
	 * If this command will perform a read, set the USCSI_READ flag
	 */
	if (ucmd->uscsi_buflen > 0) {
		switch (ucmd->uscsi_cdb[0]) {
		case SCMD_READ:
		case SCMD_READ|SCMD_GROUP1:
		case SCMD_MODE_SENSE:
		case SCMD_INQUIRY:
		case SCMD_READ_DEFECT_LIST:
		case SCMD_READ_CAPACITY:
			ucmd->uscsi_flags |= USCSI_READ;
			break;
		}
	}

	/*
	 * Set timeout: 30 seconds for all commands except format
	 */
	switch (ucmd->uscsi_cdb[0]) {
	case SCMD_FORMAT:
		if (ucmd->uscsi_timeout == 0) {
			ucmd->uscsi_timeout = scsi_format_timeout;
			/*
			 * Get the timeout value computed using page4 geometry.
			 * add 50% margin to cover defect management overhead.
			 * add another 50% margin to have a safe timeout.
			 * If it exceeds 2 hours then use this value.
			 */
			if ((timeout = scsi_format_time()) > 0) {
				timeout *= 60;	/* convert to seconds */
				timeout += timeout;
				if (timeout > scsi_format_timeout)
					ucmd->uscsi_timeout = timeout;
			}
		}
		if (option_msg && diag_msg) {
			err_print("format_timeout set to %d seconds, %d"
				" required\n", ucmd->uscsi_timeout, timeout);
		}
		break;

	default:
		ucmd->uscsi_timeout = 30;		/* 30 seconds */
		break;
	}

	/*
	 * Set up Request Sense buffer
	 */
	ucmd->uscsi_flags |= USCSI_RQENABLE;
	ucmd->uscsi_rqbuf = rqbuf;
	ucmd->uscsi_rqlen = sizeof (rqbuf);
	ucmd->uscsi_rqresid = sizeof (rqbuf);
	ucmd->uscsi_rqstatus = IMPOSSIBLE_SCSI_STATUS;

	/*
	 * Clear global error state
	 */
	media_error = 0;

	/*
	 * Execute the ioctl
	 */
	status = ioctl(fd, USCSICMD, ucmd);
	if (status == 0 && ucmd->uscsi_status == 0)
		return (status);

#ifdef sparc
	/*
	 * Check the status and return appropriate errors if the disk is
	 * unavailable (could be formatting) or reserved (by other host).
	 * In either case we can not talk to the disk now.
	 */
	if (status == -1 && errno == EAGAIN) {
		return (DSK_UNAVAILABLE);
	}
	if ((ucmd->uscsi_status & STATUS_MASK) == STATUS_RESERVATION_CONFLICT) {
		return (DSK_RESERVED);
	}
#endif sparc

	/*
	 * If an automatic Request Sense gave us valid
	 * info about the error, we may be able to use
	 * that to print a reasonable error msg.
	 */
	if (ucmd->uscsi_rqstatus == IMPOSSIBLE_SCSI_STATUS) {
		if (option_msg && diag_msg) {
			err_print("No request sense for command %s\n",
				scsi_find_command_name(ucmd->uscsi_cdb[0]));
		}
		return (-1);
	}
	if (ucmd->uscsi_rqstatus != STATUS_GOOD) {
		if (option_msg && diag_msg) {
			err_print("Request sense status for command %s: 0x%x\n",
				scsi_find_command_name(ucmd->uscsi_cdb[0]),
				ucmd->uscsi_rqstatus);
		}
		return (-1);
	}
	rq = (struct scsi_extended_sense *) rqbuf;
	rqlen = ucmd->uscsi_rqlen - ucmd->uscsi_rqresid;
	if ((((int)rq->es_add_len) + 8) < MIN_REQUEST_SENSE_LEN ||
			rq->es_class != CLASS_EXTENDED_SENSE ||
				rqlen < MIN_REQUEST_SENSE_LEN) {
		if (option_msg) {
			err_print("Request sense for command %s failed\n",
				scsi_find_command_name(ucmd->uscsi_cdb[0]));
		}
		if (option_msg && diag_msg) {
			err_print("Sense data:\n");
			dump("", (caddr_t) rqbuf, rqlen, HEX_ONLY);
		}
		return (-1);
	}

	/*
	 * If the failed command is a Mode Select, and the
	 * target is indicating that it has rounded one of
	 * the mode select parameters, as defined in the SCSI-2
	 * specification, then we should accept the command
	 * as successful.
	 */
	if (ucmd->uscsi_cdb[0] == SCMD_MODE_SELECT) {
		if (rq->es_key == KEY_RECOVERABLE_ERROR &&
			rq->es_add_code == ROUNDED_PARAMETER &&
			rq->es_qual_code == 0) {
				return (0);
		}
	}

	if (flags & F_ALLERRS) {
		media_error = (rq->es_key == KEY_MEDIUM_ERROR);
	}
	if (!(flags & F_SILENT) || option_msg) {
		scsi_printerr(ucmd, rq, rqlen);
	}
	if ((rq->es_key != KEY_RECOVERABLE_ERROR) || (flags & F_ALLERRS)) {
		return (-1);
	}
	return (0);
}


/*
 * Execute a uscsi mode sense command.
 * This can only be used to return one page at a time.
 * Return the mode header/block descriptor and the actual
 * page data separately - this allows us to support
 * devices which return either 0 or 1 block descriptors.
 * Whatever a device gives us in the mode header/block descriptor
 * will be returned to it upon subsequent mode selects.
 */
int
uscsi_mode_sense(fd, page_code, page_control, page_data, page_size, header)
	int	fd;			/* file descriptor */
	int	page_code;		/* requested page number */
	int	page_control;		/* current, changeable, etc. */
	caddr_t	page_data;		/* place received data here */
	int	page_size;		/* size of page_data */
	struct	scsi_ms_header *header;	/* mode header/block descriptor */
{
	caddr_t			mode_sense_buf;
	struct mode_header	*hdr;
	struct mode_page	*pg;
	int			nbytes;
	struct uscsi_cmd	ucmd;
	union scsi_cdb		cdb;
	int			status;
	int			maximum;

	assert(page_size >= 0 && page_size < 256);
	assert(page_control == MODE_SENSE_PC_CURRENT ||
		page_control == MODE_SENSE_PC_CHANGEABLE ||
			page_control == MODE_SENSE_PC_DEFAULT ||
				page_control == MODE_SENSE_PC_SAVED);
	/*
	 * Allocate a buffer for the mode sense headers
	 * and mode sense data itself.
	 */
	nbytes = sizeof (struct block_descriptor) +
				sizeof (struct mode_header) + page_size;
	nbytes = page_size;
	if ((mode_sense_buf = malloc((u_int) nbytes)) == NULL) {
		err_print("cannot malloc %ld bytes\n", nbytes);
		return (-1);
	}

	/*
	 * Build and execute the uscsi ioctl
	 */
	(void) memset(mode_sense_buf, 0, nbytes);
	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	cdb.scc_cmd = SCMD_MODE_SENSE;
	FORMG0COUNT(&cdb, (u_char)nbytes);
	cdb.cdb_opaque[2] = page_control | page_code;
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_cdblen = CDB_GROUP0;
	ucmd.uscsi_bufaddr = mode_sense_buf;
	ucmd.uscsi_buflen = nbytes;
	status = uscsi_cmd(fd, &ucmd,
		(option_msg && diag_msg) ? F_NORMAL : F_SILENT);
	if (status) {
		if (option_msg) {
			err_print("Mode sense page 0x%x failed\n",
				page_code);
		}
		free(mode_sense_buf);
		return (-1);
	}

	/*
	 * Verify that the returned data looks reasonabled,
	 * find the actual page data, and copy it into the
	 * user's buffer.  Copy the mode_header and block_descriptor
	 * into the header structure, which can then be used to
	 * return the same data to the drive when issuing a mode select.
	 */
	hdr = (struct mode_header *) mode_sense_buf;
	(void) memset((caddr_t) header, 0, sizeof (struct scsi_ms_header));
	if (hdr->bdesc_length != sizeof (struct block_descriptor) &&
				hdr->bdesc_length != 0) {
		if (option_msg) {
			err_print("\
\nMode sense page 0x%x: block descriptor length %d incorrect\n",
				page_code, hdr->bdesc_length);
			if (diag_msg)
				dump("Mode sense: ", mode_sense_buf,
					nbytes, HEX_ONLY);
		}
		free(mode_sense_buf);
		return (-1);
	}
	(void) memcpy((caddr_t) header, mode_sense_buf,
		(int) (sizeof (struct mode_header) + hdr->bdesc_length));
	pg = (struct mode_page *) ((u_long) mode_sense_buf +
		sizeof (struct mode_header) + hdr->bdesc_length);
	if (pg->code != page_code) {
		if (option_msg) {
			err_print("\
\nMode sense page 0x%x: incorrect page code 0x%x\n",
				page_code, pg->code);
			if (diag_msg)
				dump("Mode sense: ", mode_sense_buf,
					nbytes, HEX_ONLY);
		}
		free(mode_sense_buf);
		return (-1);
	}
	/*
	 * Accept up to "page_size" bytes of mode sense data.
	 * This allows us to accept both CCS and SCSI-2
	 * structures, as long as we request the greater
	 * of the two.
	 */
	maximum = page_size - sizeof (struct mode_page) - hdr->bdesc_length;
	if (((int) pg->length) > maximum) {
		if (option_msg) {
			err_print("\
Mode sense page 0x%x: incorrect page length %d - expected max %d\n",
				page_code, pg->length, maximum);
			if (diag_msg)
				dump("Mode sense: ", mode_sense_buf,
					nbytes, HEX_ONLY);
		}
		free(mode_sense_buf);
		return (-1);
	}

	(void) memcpy(page_data, (caddr_t) pg, MODESENSE_PAGE_LEN(pg));

	if (option_msg && diag_msg) {
		char *pc = find_string(page_control_strings, page_control);
		err_print("\nMode sense page 0x%x (%s):\n", page_code,
			pc != NULL ? pc : "");
		dump("header: ", (caddr_t) header,
			sizeof (struct scsi_ms_header), HEX_ONLY);
		dump("data:   ", page_data,
			MODESENSE_PAGE_LEN(pg), HEX_ONLY);
	}

	free(mode_sense_buf);
	return (0);
}


/*
 * Execute a uscsi mode select command.
 */
int
uscsi_mode_select(fd, page_code, options, page_data, page_size, header)
	int	fd;			/* file descriptor */
	int	page_code;		/* mode select page */
	int	options;		/* save page/page format */
	caddr_t	page_data;		/* place received data here */
	int	page_size;		/* size of page_data */
	struct	scsi_ms_header *header;	/* mode header/block descriptor */
{
	caddr_t				mode_select_buf;
	int				nbytes;
	struct uscsi_cmd		ucmd;
	union scsi_cdb			cdb;
	int				status;

	assert(((struct mode_page *) page_data)->ps == 0);
	assert(header->mode_header.length == 0);
	assert(header->mode_header.device_specific == 0);
	assert((options & ~(MODE_SELECT_SP|MODE_SELECT_PF)) == 0);

	/*
	 * Allocate a buffer for the mode select header and data
	 */
	nbytes = sizeof (struct block_descriptor) +
				sizeof (struct mode_header) + page_size;
	if ((mode_select_buf = malloc((u_int) nbytes)) == NULL) {
		err_print("cannot malloc %ld bytes\n", nbytes);
		return (-1);
	}

	/*
	 * Build the mode select data out of the header and page data
	 * This allows us to support devices which return either
	 * 0 or 1 block descriptors.
	 */
	(void) memset(mode_select_buf, 0, nbytes);
	nbytes = sizeof (struct mode_header);
	if (header->mode_header.bdesc_length ==
				sizeof (struct block_descriptor)) {
		nbytes += sizeof (struct block_descriptor);
	}

	/*
	 * Dump the structures if anyone's interested
	 */
	if (option_msg && diag_msg) {
		char *s;
		s = find_string(mode_select_strings,
			options & (MODE_SELECT_SP|MODE_SELECT_PF));
		err_print("\nMode select page 0x%x%s:\n", page_code,
			s != NULL ? s : "");
		dump("header: ", (caddr_t) header,
			nbytes, HEX_ONLY);
		dump("data:   ", (caddr_t) page_data,
			page_size, HEX_ONLY);
	}

#ifdef _LITTLE_ENDIAN
	/*
	 * Fix the code for byte ordering
	 */

	switch (page_code) {
	case  DAD_MODE_ERR_RECOV:
		{
		struct mode_err_recov *pd;
		pd = (struct mode_err_recov *)page_data;

		pd->recovery_time_limit = htons(pd->recovery_time_limit);
		break;
		}
	case MODEPAGE_DISCO_RECO:
		{
		struct mode_disco_reco *pd;
		pd = (struct mode_disco_reco *)page_data;

		pd->bus_inactivity_limit = htons(pd->bus_inactivity_limit);
		pd->disconect_time_limit = htons(pd->disconect_time_limit);
		pd->connect_time_limit = htons(pd->connect_time_limit);
		pd->max_burst_size = htons(pd->max_burst_size);
		break;
		}
	case DAD_MODE_FORMAT:
		{
		struct mode_format *pd;
		pd = (struct mode_format *)page_data;

		pd->tracks_per_zone = htons(pd->tracks_per_zone);
		pd->alt_sect_zone = htons(pd->alt_sect_zone);
		pd->alt_tracks_zone = htons(pd->alt_tracks_zone);
		pd->alt_tracks_vol = htons(pd->alt_tracks_vol);
		pd->sect_track = htons(pd->sect_track);
		pd->data_bytes_sect = htons(pd->data_bytes_sect);
		pd->interleave = htons(pd->interleave);
		pd->track_skew = htons(pd->track_skew);
		pd->cylinder_skew = htons(pd->cylinder_skew);
		break;
		}
	case DAD_MODE_GEOMETRY:
		{
		struct mode_geometry *pd;
		pd = (struct mode_geometry *)page_data;

		pd->step_rate = htons(pd->step_rate);
		pd->rpm = htons(pd->rpm);
		break;
		}
	case DAD_MODE_CACHE:
		{
		struct mode_cache *pd;
		pd = (struct mode_cache *)page_data;

		pd->dis_prefetch_len = htons(pd->dis_prefetch_len);
		pd->min_prefetch = htons(pd->min_prefetch);
		pd->max_prefetch = htons(pd->max_prefetch);
		pd->prefetch_ceiling = htons(pd->prefetch_ceiling);
		break;
		}
	case MODEPAGE_PDEVICE:
		{
		struct mode_pdevice *pd;
		pd = (struct mode_pdevice *)page_data;

		pd->if_ident = htons(pd->if_ident);
		break;
		}
	case MODEPAGE_CTRL_MODE:
		{
		struct mode_control *pd;
		pd = (struct mode_control *)page_data;

		pd->ready_aen_holdoff = htons(pd->ready_aen_holdoff);
		break;
		}
	}
#endif /* _LITTLE_ENDIAN */

	/*
	 * Put the header and data together
	 */
	(void) memcpy(mode_select_buf, (caddr_t) header, nbytes);
	(void) memcpy(mode_select_buf + nbytes, page_data, page_size);
	nbytes += page_size;

	/*
	 * Build and execute the uscsi ioctl
	 */
	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	cdb.scc_cmd = SCMD_MODE_SELECT;
	FORMG0COUNT(&cdb, (u_char)nbytes);
	cdb.cdb_opaque[1] = (u_char)options;
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_cdblen = CDB_GROUP0;
	ucmd.uscsi_bufaddr = mode_select_buf;
	ucmd.uscsi_buflen = nbytes;
	status = uscsi_cmd(fd, &ucmd,
		(option_msg && diag_msg) ? F_NORMAL : F_SILENT);

	if (status && option_msg) {
		err_print("Mode select page 0x%x failed\n", page_code);
	}

	free(mode_select_buf);
	return (status);
}


/*
 * Execute a uscsi inquiry command and return the
 * resulting data.
 */
int
uscsi_inquiry(fd, inqbuf, inqbufsiz)
	int		fd;
	caddr_t		inqbuf;
	int		inqbufsiz;
{
	struct uscsi_cmd	ucmd;
	union scsi_cdb		cdb;
	struct scsi_inquiry	*inq;
	int			n;
	int			status;

	assert(inqbufsiz >= sizeof (struct scsi_inquiry) &&
		inqbufsiz < 256);

	/*
	 * Build and execute the uscsi ioctl
	 */
	(void) memset((char *)inqbuf, 0, inqbufsiz);
	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	cdb.scc_cmd = SCMD_INQUIRY;
	FORMG0COUNT(&cdb, (u_char)inqbufsiz);
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_cdblen = CDB_GROUP0;
	ucmd.uscsi_bufaddr = (caddr_t) inqbuf;
	ucmd.uscsi_buflen = inqbufsiz;
	status = uscsi_cmd(fd, &ucmd,
		(option_msg && diag_msg) ? F_NORMAL : F_SILENT);
	if (status) {
		if (option_msg) {
			err_print("Inquiry failed\n");
		}
	} else if (option_msg && diag_msg) {
		/*
		 * Dump the inquiry data if anyone's interested
		 */
		inq = (struct scsi_inquiry *) inqbuf;
		n = (int) inq->inq_len + 4;
		n = min(n, inqbufsiz);
		err_print("Inquiry:\n");
		dump("", (caddr_t) inqbuf, n, HEX_ASCII);
	}
	return (status);
}


/*
 * Return the Read Capacity information
 */
int
uscsi_read_capacity(fd, capacity)
	int			fd;
	struct scsi_capacity	*capacity;
{
	struct uscsi_cmd	ucmd;
	union scsi_cdb		cdb;
	int			status;

	/*
	 * Build and execute the uscsi ioctl
	 */
	(void) memset((char *)capacity, 0, sizeof (struct scsi_capacity));
	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	cdb.scc_cmd = SCMD_READ_CAPACITY;
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_cdblen = CDB_GROUP1;
	ucmd.uscsi_bufaddr = (caddr_t) capacity;
	ucmd.uscsi_buflen = sizeof (struct scsi_capacity);
	status = uscsi_cmd(fd, &ucmd,
		(option_msg && diag_msg) ? F_NORMAL : F_SILENT);

	capacity->capacity = ntohl(capacity->capacity);
	capacity->lbasize = ntohl(capacity->lbasize);

	if (status) {
		if (option_msg) {
			err_print("Read capacity failed\n");
		}
	} else if (option_msg && diag_msg) {
		/*
		 * Dump the capacity data if anyone's interested
		 */
		dump("Capacity: ", (caddr_t) capacity,
			sizeof (struct scsi_capacity), HEX_ONLY);
	}
	return (status);
}


/*
 * Reserve the current disk
 */
int
uscsi_reserve_release(fd, cmd)
	int			fd;
	int			cmd;
{
	struct uscsi_cmd	ucmd;
	union scsi_cdb		cdb;
	int			status = 0;

#ifdef sparc
	/*
	 * Build and execute the uscsi ioctl
	 */
	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	cdb.scc_cmd = (cmd == SCMD_RESERVE) ? SCMD_RESERVE : SCMD_RELEASE;
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_cdblen = CDB_GROUP0;
	status = uscsi_cmd(fd, &ucmd,
		(option_msg && diag_msg) ? F_NORMAL : F_SILENT);

	if (status) {
		if (option_msg) {
			err_print("%s failed\n", (cmd == SCMD_RESERVE) ?
			    "Reserve" : "Release");
		}
	}
#endif sparc
	return (status);
}


int
scsi_dump_mode_sense_pages(page_control)
	int			page_control;
{
	struct uscsi_cmd	ucmd;
	union scsi_cdb		cdb;
	char			*msbuf;
	int			nbytes;
	char			*pc_str;
	int			status;
	struct mode_header	*mh;
	char			*p;
	struct mode_page	*mp;
	int			n;
	char			s[16];
	int			result = 0;

	pc_str = find_string(page_control_strings, page_control);

	/*
	 * Allocate memory for the mode sense buffer.
	 */
	nbytes = 255;
	msbuf = (char *) zalloc(nbytes);

	/*
	 * Build and execute the uscsi ioctl
	 */
	(void) memset(msbuf, 0, nbytes);
	(void) memset((char *)&ucmd, 0, sizeof (ucmd));
	(void) memset((char *)&cdb, 0, sizeof (union scsi_cdb));
	cdb.scc_cmd = SCMD_MODE_SENSE;
	FORMG0COUNT(&cdb, (u_char)nbytes);
	cdb.cdb_opaque[2] = page_control | 0x3f;
	ucmd.uscsi_cdb = (caddr_t) &cdb;
	ucmd.uscsi_cdblen = CDB_GROUP0;
	ucmd.uscsi_bufaddr = msbuf;
	ucmd.uscsi_buflen = nbytes;
	status = uscsi_cmd(cur_file, &ucmd,
		(option_msg && diag_msg) ? F_NORMAL : F_SILENT);
	if (status) {
		err_print("\nMode sense page 0x3f (%s) failed\n",
			pc_str);
		result = 1;
	} else {
		err_print("\nMode sense pages (%s):\n", pc_str);
		mh = (struct mode_header *) msbuf;
		nbytes = mh->length - sizeof (struct mode_header) -
				mh->bdesc_length + 1;
		p = msbuf + sizeof (struct mode_header) +
			mh->bdesc_length;
		dump("         ", msbuf, sizeof (struct mode_header) +
			(int)mh->bdesc_length, HEX_ONLY);
		while (nbytes > 0) {
			mp = (struct mode_page *) p;
			n = mp->length + sizeof (struct mode_page);
			nbytes -= n;
			if (nbytes < 0)
				break;
			sprintf(s, "   %3x:  ", mp->code);
			dump(s, p, n, HEX_ONLY);
			p += n;
		}
		if (nbytes < 0) {
			err_print("  Sense data formatted incorrectly:\n");
			dump("    ", msbuf, (int)mh->length+1, HEX_ONLY);
			result = 1;
		}
		err_print("\n");
	}

	free(msbuf);
	return (result);
}


static void
scsi_printerr(ucmd, rq, rqlen)
	struct uscsi_cmd		*ucmd;
	struct scsi_extended_sense	*rq;
	int				rqlen;
{
	int		blkno;

	switch (rq->es_key) {
	case KEY_NO_SENSE:
		err_print("No sense error");
		break;
	case KEY_RECOVERABLE_ERROR:
		err_print("Recoverable error");
		break;
	case KEY_NOT_READY:
		err_print("Not ready error");
		break;
	case KEY_MEDIUM_ERROR:
		err_print("Medium error");
		break;
	case KEY_HARDWARE_ERROR:
		err_print("Hardware error");
		break;
	case KEY_ILLEGAL_REQUEST:
		err_print("Illegal request");
		break;
	case KEY_UNIT_ATTENTION:
		err_print("Unit attention error");
		break;
	case KEY_WRITE_PROTECT:
		err_print("Write protect error");
		break;
	case KEY_BLANK_CHECK:
		err_print("Blank check error");
		break;
	case KEY_VENDOR_UNIQUE:
		err_print("Vendor unique error");
		break;
	case KEY_COPY_ABORTED:
		err_print("Copy aborted error");
		break;
	case KEY_ABORTED_COMMAND:
		err_print("Aborted command");
		break;
	case KEY_EQUAL:
		err_print("Equal error");
		break;
	case KEY_VOLUME_OVERFLOW:
		err_print("Volume overflow");
		break;
	case KEY_MISCOMPARE:
		err_print("Miscompare error");
		break;
	case KEY_RESERVED:
		err_print("Reserved error");
		break;
	default:
		err_print("Unknown error");
		break;
	}

	err_print(" during %s", scsi_find_command_name(ucmd->uscsi_cdb[0]));

	if (rq->es_valid) {
		blkno = (rq->es_info_1 << 24) | (rq->es_info_2 << 16) |
			(rq->es_info_3 << 8) | rq->es_info_4;
		err_print(": block %d (0x%x) (", blkno, blkno);
		pr_dblock(err_print, (daddr_t)blkno);
		err_print(")");
	}

	err_print("\n");

	if (rq->es_add_len >= 6) {
		err_print("ASC: 0x%x   ASCQ: 0x%x\n",
			rq->es_add_code, rq->es_qual_code);
	}

	if (option_msg && diag_msg) {
		if (rq->es_key == KEY_ILLEGAL_REQUEST) {
			dump("cmd:    ", (caddr_t) ucmd,
				sizeof (struct uscsi_cmd), HEX_ONLY);
			dump("cdb:    ", (caddr_t) ucmd->uscsi_cdb,
				ucmd->uscsi_cdblen, HEX_ONLY);
		}
		dump("sense:  ", (caddr_t) rq, rqlen, HEX_ONLY);
	}

	if (option_msg) {
		scsi_print_extended_sense(rq, rqlen);
	}
}


/*
 * Return a pointer to a string telling us the name of the command.
 */
static char *
scsi_find_command_name(cmd)
	register u_int cmd;
{
	register struct scsi_command_name *c;

	for (c = scsi_command_names; c->command != SCMD_UNKNOWN; c++)
		if (c->command == cmd)
			break;
	return (c->name);
}


/*
 * Return true if we support a particular mode page
 */
int
scsi_supported_page(page)
	int	page;
{
	return (page == 1 || page == 2 || page == 3 || page == 4 ||
		page == 8 || page == 0x38);
}


int
apply_chg_list(pageno, pagsiz, curbits, chgbits, chglist)
	int		pageno;
	int		pagsiz;
	u_char		*curbits;
	u_char		*chgbits;
	struct chg_list	*chglist;
{
	u_char		c;
	int		i;
	int		m;
	int		delta;
	int		changed = 0;

	while (chglist != NULL) {
		if (chglist->pageno == pageno &&
				chglist->byteno < pagsiz) {
			i = chglist->byteno;
			c = curbits[i];
			switch (chglist->mode) {
			case CHG_MODE_SET:
				c |= (u_char) chglist->value;
				break;
			case CHG_MODE_CLR:
				c &= (u_char) chglist->value;
				break;
			case CHG_MODE_ABS:
				c = (u_char) chglist->value;
				break;
			}
			/*
			 * Figure out which bits changed, and
			 * are marked as changeable.  If this
			 * result actually differs from the
			 * current value, update the current
			 * value, and note that a mode select
			 * should be done.
			 */
			delta = c ^ curbits[i];
			for (m = 0x01; m < 0x100; m <<= 1) {
				if ((delta & m) && (chgbits[i] & m)) {
					curbits[i] ^= m;
					changed = 1;
				}
			}
		}
		chglist = chglist->next;
	}

	return (changed);
}


/*
 * Return whether a given page is affected by an item on
 * the change list.
 */
static int
chg_list_affects_page(chglist, pageno)
	struct chg_list	*chglist;
	int		pageno;
{
	while (chglist != NULL) {
		if (chglist->pageno == pageno) {
			return (1);
		}
		chglist = chglist->next;
	}

	return (0);
}


/*
 * Labels for the various fields of the scsi_extended_sense structure
 */
char *scsi_extended_sense_labels[] = {
	"Request sense valid:             ",
	"Error class and code:            ",
	"Segment number:                  ",
	"Filemark:                        ",
	"End-of-medium:                   ",
	"Incorrect length indicator:      ",
	"Sense key:                       ",
	"Information field:               ",
	"Additional sense length:         ",
	"Command-specific information:    ",
	"Additional sense code:           ",
	"Additional sense code qualifier: ",
	"Field replaceable unit code:     ",
	"Sense-key specific:              ",
	"Additional sense bytes:          "
};


/*
 * Display the full scsi_extended_sense as returned by the device
 */
static void
scsi_print_extended_sense(rq, rqlen)
	struct scsi_extended_sense	*rq;
	int				rqlen;
{
	char			**p;

	p = scsi_extended_sense_labels;
	if (rqlen < (sizeof (*rq) - 2) || !rq->es_valid) {
		/*
		 * target should be capable of returning at least 18
		 * bytes of data, i.e upto rq->es_skey_specific field.
		 * The additional sense bytes (2 or more ...) are optional.
		 */
		return;
	}

	fmt_print("\n%s%s\n", *p++, rq->es_valid ? "yes" : "no");
	fmt_print("%s0x%02x\n", *p++, (rq->es_class << 4) + rq->es_code);
	fmt_print("%s%d\n", *p++, rq->es_segnum);
	fmt_print("%s%s\n", *p++, rq->es_filmk ? "yes" : "no");
	fmt_print("%s%s\n", *p++, rq->es_eom ? "yes" : "no");
	fmt_print("%s%s\n", *p++, rq->es_ili ? "yes" : "no");
	fmt_print("%s%d\n", *p++, rq->es_key);

	fmt_print("%s0x%02x 0x%02x 0x%02x 0x%02x\n", *p++, rq->es_info_1,
		rq->es_info_2, rq->es_info_3, rq->es_info_4);
	fmt_print("%s%d\n", *p++, rq->es_add_len);
	fmt_print("%s0x%02x 0x%02x 0x%02x 0x%02x\n", *p++, rq->es_cmd_info[0],
		rq->es_cmd_info[1], rq->es_cmd_info[2], rq->es_cmd_info[3]);
	fmt_print("%s0x%02x = %d\n", *p++, rq->es_add_code, rq->es_add_code);
	fmt_print("%s0x%02x = %d\n", *p++, rq->es_qual_code, rq->es_qual_code);
	fmt_print("%s%d\n", *p++, rq->es_fru_code);
	fmt_print("%s0x%02x 0x%02x 0x%02x\n", *p++, rq->es_skey_specific[0],
		rq->es_skey_specific[1], rq->es_skey_specific[2]);

	if (rqlen >= sizeof (*rq)) {
		fmt_print("%s0x%02x 0x%02x%s\n", *p, rq->es_add_info[0],
		rq->es_add_info[1], (rqlen > sizeof (*rq)) ? " ..." : "");
	}

	fmt_print("\n");
}
