
#ifndef lint
#ident	"@(#)ctlr_id.c	1.10	95/02/18 SMI"
#endif	lint

/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

/*
 * This file contains the ctlr dependent routines for
 * the IPI ISP-80 (PANTHER) ctlr.
 */
#include "global.h"
#include "param.h"
#include "analyze.h"
#include "misc.h"
#include <sys/dkbad.h>
#include <sys/buf.h>
#include <sys/param.h>
#include <sys/hdio.h>
#include <errno.h>

#include "ctlr_id.h"
#include <sys/open.h>
#include <sys/dditypes.h>
#include <sys/ipi3.h>
#include <sys/ipi_driver.h>
#include <sys/idvar.h>

extern	int	errno;

/* #define	DEBUG */
#define	ALIGN(x, i)	(u_long)(((u_long)x + (u_long)i) & ~(u_long)(i - 1))
#define	MAXCYLS		10

#ifdef	__STDC__
/*
 * ANSI prototypes for local functions
 */
static int	id_ck_format(void);
static int	id_format(daddr_t, daddr_t, struct defect_list *);
static int	id_ex_man(struct defect_list *);
static int	id_ex_cur(struct defect_list *);
static int	id_repair(daddr_t, int);
static int	id_wr_cur(struct defect_list *);
static int	idcmd(int, struct hdk_cmd *, int);
#else
static int	id_ck_format();
static int	id_format();
static int	id_ex_man();
static int	id_ex_cur();
static int	id_repair();
static int	id_wr_cur();
static int	idcmd();
#endif	/* __STDC__ */

/*
 * This is the operation vector for the IPI ISP-80 (PANTHER).  It is
 * referenced in init_ctypes.c where the supported controllers are defined.
 */
struct	ctlr_ops idops = {
	id_rdwr,
	id_ck_format,
	id_format,
	id_ex_man,
	id_ex_cur,
	id_repair,
	0,
	id_wr_cur,
};


struct idmdef {
	u_long cyl;
	u_short hd;
	u_long offset;
	u_short len;
};

struct idmdef_a {
	u_short cyl_h;
	u_short cyl_l;
	u_short hd;
	u_short offset_h;
	u_short offset_l;
	u_short len;
};

#define	MAN_DEFECT_RW	0x0	/* MANUFACTURER's list (permanent) */
#define	PAN_DEFECT_RW	0x6	/* PANTHER list(suspect temporary) */
#define	PAN_DEFECT_CR	0xA	/* PANTHER list (suspect temporary) */
#define	FOR_DEFECT_RW	0x2	/* disk state (working temporary) */

#define	MAN_DEFECT_F	8
#define	PAN_DEFECT_F	2
#define	M_P_DEFECT_F	4

/*
 * The following routines are the controller specific routines accessed
 * through the controller ops vector.
 */

/*
 * This routine is used to read/write the disk.  The flags determine
 * what mode the operation should be executed in (silent, diagnostic, etc).
 */
int
id_rdwr(dir, file, blkno, secnt, bufaddr, flags)
	int	dir, file, secnt, flags;
	daddr_t	blkno;
	caddr_t	bufaddr;
{
	struct	hdk_cmd cmdblk;
	u_short opmod;

	/*
	 * Fill in a command block with the command info.
	 */
	opmod = 1;
	flags = F_ALLERRS;
	cmdblk.hdkc_blkno = blkno;
	cmdblk.hdkc_secnt = secnt;
	cmdblk.hdkc_bufaddr = bufaddr;
	cmdblk.hdkc_buflen = secnt * SECSIZE;
	cmdblk.hdkc_flags = F_ALLERRS;

	if (dir == DIR_READ)
		cmdblk.hdkc_cmd = (opmod << 8) | IP_READ;
	else
		cmdblk.hdkc_cmd = (opmod << 8) | IP_WRITE;
	/*
	 * Run the command and return status.
	 */
	return (idcmd(file, &cmdblk, flags));
}

/*
 * This routine is used to check whether the current disk is formatted.
 * The approach used is to attempt to read the 4 sectors from an area
 * on the disk.  We can't just read one, cause that will succeed on a
 * new drive if the defect info is just right. We are guaranteed that
 * the first track is defect free, so we should never have problems
 * because of that. We also check all over the disk now to make very
 * sure of whether it is formatted or not.
 */
static int
id_ck_format()
{
	struct	hdk_cmd cmdblk;
	int	status, i;
	u_short opmod;

	opmod = 1;
	for (i = 0; i < ncyl; i += ncyl/4) {
		/*
		 * Fill in a command block with the command info. We run the
		 * command in silent mode because the error message is not
		 * important, just the success/failure.
		 */
		cmdblk.hdkc_cmd = (opmod << 8) | IP_READ;
		cmdblk.hdkc_blkno = (daddr_t)i * nsect * nhead;
		cmdblk.hdkc_secnt = 4;
		cmdblk.hdkc_bufaddr = (caddr_t)cur_buf;
		cmdblk.hdkc_buflen = SECSIZE * 4;
		cmdblk.hdkc_flags = F_SILENT;

		status = idcmd(cur_file, &cmdblk, F_SILENT);
		/*
		 * If any area checked returns no error, the disk is at
		 * least partially formatted.
		 */
		if (!status)
			return (1);
	}
	/*
	 * OK, it really is unformatted...
	 */
	return (0);
}

/*
 * This routine formats a portion of the current disk.
 * This routines grain size is a cylinder.
 */
static int
id_format(start, end, dlist)
	daddr_t	start, end;
	struct	defect_list *dlist;
{
	struct	idmdef_a		mdef[16 * DEV_BSIZE + 4];
	struct	idmdef_a		*md, *sav_md;
	struct	defect_entry	*pdef;
	struct	hdk_cmd cmdblk;
	int	startcyl, startblkno;
	int	endcyl, count;
	int	cylstoformat;
	int	status;
	int	i;
	u_short	opmod;


	md = (struct idmdef_a *)ALIGN(mdef, 4);
	sav_md = md;

#ifdef lint
	md = md;
	pdef = 0;
	pdef = pdef;
#endif

	pdef = dlist->list;

	for (i = 0; i < dlist->header.count; i++) {
		md->cyl_h = (pdef->cyl >> 16) & 0xffff;
		md->cyl_l = (pdef->cyl & 0xffff);
		md->hd = pdef->head;
		md->len = pdef->nbits;
		md->offset_h = (pdef->bfi >> 16) & 0xffff;
		md->offset_l = pdef->bfi & 0xffff;

		pdef++; md++;
	}



	opmod = PAN_DEFECT_CR;
	cmdblk.hdkc_cmd = (opmod << 8) | IP_WRITE_DEFLIST;

	cmdblk.hdkc_flags = 0;

	cmdblk.hdkc_blkno = 0;
	cmdblk.hdkc_bufaddr = (char *)sav_md;
	cmdblk.hdkc_buflen = i * (sizeof (struct idmdef_a));
	cmdblk.hdkc_secnt = 16;

	status = idcmd(cur_file, &cmdblk, 0);

	if (status == -1) {
		err_print("Could not write ISP-80 working LIST\n");
		return (-1);
	}


	/* start formatting */


	startcyl = start / (nsect * nhead);
	startblkno = startcyl * nsect * nhead;

	if (start != startblkno) {
		fmt_print("Starting block no. not on cylinder boundry\n");
		return (-1);
	}

	endcyl = end / (nsect * nhead);
	count = endcyl - startcyl + 1;

	i = count;

	nolog_print("      ");
	while (count > 0) {

		cylstoformat = min(count, 3);
				/*
				 * Request the driver and IPI controller
				 * to format only a few cylinders
				 * at a time.
				 * This limits the duration of the IPI
				 * controller command execution,
				 * without increasing the overall
				 * execution time of the format utility.
				 * If controller command
				 * execution time becomes too long, and
				 * there are many drives being formatted
				 * simultaneously, the driver may
				 * timeout and consider the controller
				 * failed.
				 * This hack avoids fixing the driver
				 * to understand the IPI controller takes
				 * different amounts of time, especially
				 * for format commands.
				 */

		nolog_print("  %3d%c\b\b\b\b\b\b", (i - count) * 100 /i, '%');
		(void) fflush(stdout);

		opmod = 1;
		cmdblk.hdkc_cmd = (opmod << 8) | IP_FORMAT;
		cmdblk.hdkc_flags = 0;
		cmdblk.hdkc_buflen = 0;
		cmdblk.hdkc_blkno = startblkno;
		cmdblk.hdkc_secnt = cylstoformat * (nsect * nhead);
		status = idcmd(cur_file, &cmdblk, 0);

		if (status == -1)
			return (-1);

		startblkno	+= (cylstoformat * nsect * nhead);
		count	-= cylstoformat;

	}
	nolog_print("      \b\b\b\b\b\b");	/* clean up the field */
	return (0);



}

/*
 * This routine extracts the manufacturer's defect list from the disk.
 */
static int
id_ex_man(dlist)
	struct	defect_list *dlist;
{
	struct	idmdef_a		mdef[16 * DEV_BSIZE+4];
	struct	idmdef_a		*md;
	struct	hdk_cmd		cmdblk;
	struct	defect_entry	def;
	int			index;
	int			status;
	u_short			opmod;

	opmod = MAN_DEFECT_RW;
	cmdblk.hdkc_cmd = (opmod << 8) | IP_READ_DEFLIST;
	cmdblk.hdkc_flags = 0;
	md = (struct idmdef_a *)ALIGN(mdef, 4);
	cmdblk.hdkc_bufaddr = (char *)md;
	bzero((char *)md, (16 * DEV_BSIZE));
	cmdblk.hdkc_buflen = 16 * DEV_BSIZE;
	cmdblk.hdkc_secnt = 16 * DEV_BSIZE; /* eek ! this is in bytes */

	status = idcmd(cur_file, &cmdblk, 0);

	if (status == -1) {
		err_print("Manufacturers list extraction Failed\n");
		return (-1);
	}

	dlist->header.magicno = (u_int) DEFECT_MAGIC;
	dlist->header.count = 0;
	dlist->list = (struct defect_entry *)zalloc(16 * SECSIZE);


	while (md->len != 0 && md < &mdef[16 * DEV_BSIZE]) {

		def.cyl = (md->cyl_h << 16) | (md->cyl_l);
		def.head = md->hd;
		def.bfi = (md->offset_h << 16) | md->offset_l;
		def.nbits = md->len;

		index = sort_defect(&def, dlist);
		add_def(&def, dlist, index);

		md++;
	}

	if (dlist->header.count == 0) {	/* empty defect list is */
		fmt_print("NULL MANUFACTURER'S list: Ignoring this list\n");
		return (-1);		/* an error condition forcibly */
	}


	return (0);
}

/*
 * This routine extracts the current defect list from the disk.  It does
 * so by reading the ISP-80 (PANTHER) defect list.
 */
static int
id_ex_cur(dlist)
	struct	defect_list *dlist;
{
	struct	idmdef_a		mdef[16 * DEV_BSIZE+4];
	struct	idmdef_a		*md;
	struct	hdk_cmd		cmdblk;
	struct	defect_entry	def;
	int			index;
	int			status;
	u_short			opmod;

	opmod = PAN_DEFECT_RW;
	cmdblk.hdkc_cmd = (opmod << 8) | IP_READ_DEFLIST;
	cmdblk.hdkc_flags = 0;
	md = (struct idmdef_a *)ALIGN(mdef, 4);
	cmdblk.hdkc_bufaddr = (char *)md;
	bzero((char *)md, (16 * DEV_BSIZE));
	cmdblk.hdkc_buflen = 16 * DEV_BSIZE;
	cmdblk.hdkc_secnt = 16;

	status = idcmd(cur_file, &cmdblk, 0);

	if (status == -1) {
		err_print("ISP-80 working list extraction Failed\n");
		return (-1);
	}

	dlist->header.magicno = (u_int) DEFECT_MAGIC;
	dlist->header.count = 0;
	dlist->list = (struct defect_entry *)zalloc(16 * SECSIZE);


	while (md->len != 0 && md < &mdef[16 * DEV_BSIZE]) {

		def.cyl = (md->cyl_h << 16) | (md->cyl_l);
		def.head = md->hd;
		def.bfi = (md->offset_h << 16) | md->offset_l;
		def.nbits = md->len;

		index = sort_defect(&def, dlist);
		add_def(&def, dlist, index);

		md++;
	}

	if (dlist->header.count == 0) {	/* empty defect list is */
		fmt_print("NULL ISP-80 working list: Ignoring this list\n");
		return (-1);		/* an error condition forcibly */
	}

	return (0);
}

/*
 * This routine is used to repair defective sectors.  It is assumed that
 * a higher level routine will take care of adding the defect to the
 * defect list.  The approach is to try to slid the sector, and if that
 * fails map it.
 */
/*ARGSUSED*/
static int
id_repair(blkno, flag)
	daddr_t	blkno;
	int	flag;
{
	struct	hdk_cmd		cmdblk;
	u_short			opmod;
	int			status;

	opmod = 1;
	cmdblk.hdkc_cmd = (opmod << 8) | IP_REALLOC;

	cmdblk.hdkc_blkno = blkno;
	cmdblk.hdkc_bufaddr = (char *)0;
	cmdblk.hdkc_buflen = 0;
	cmdblk.hdkc_secnt = 1;
	cmdblk.hdkc_flags = 0;

	status = idcmd(cur_file, &cmdblk, 0);

	if (status == -1) {
		err_print("Could not repair block %d\n", blkno);
		return (-1);
	}
	return (0);

}


static int
id_wr_cur(dlist)
	struct	defect_list *dlist;
{
	struct	idmdef_a	mdef[16 * DEV_BSIZE + 4];
	struct	idmdef_a	*md, *sav_md;
	struct	defect_entry	*pdef;
	struct	hdk_cmd cmdblk;
	int	status;
	int	i;
	u_short	opmod;


	md = (struct idmdef_a *)ALIGN(mdef, 4);
	sav_md = md;
	(void) bzero((char *)md, 16 * DEV_BSIZE);

#ifdef lint
	md = md;
	pdef = 0;
	pdef = pdef;
#endif

	pdef = dlist->list;

	for (i = 0; i < dlist->header.count; i++) {
		md->cyl_h = (pdef->cyl >> 16) & 0xffff;
		md->cyl_l = (pdef->cyl & 0xffff);
		md->hd = pdef->head;
		md->len = pdef->nbits;
		md->offset_h = (pdef->bfi >> 16) & 0xffff;
		md->offset_l = pdef->bfi & 0xffff;

		pdef++; md++;
	}



	fmt_print("Write ISP-80 working list: count is %d\n", i);
	opmod = PAN_DEFECT_CR;
	cmdblk.hdkc_cmd = (opmod << 8) | IP_WRITE_DEFLIST;

	cmdblk.hdkc_flags = 0;

	cmdblk.hdkc_blkno = 0;
	cmdblk.hdkc_bufaddr = (char *)sav_md;
	cmdblk.hdkc_buflen = i * (sizeof (struct idmdef_a));
	cmdblk.hdkc_secnt = 16;

	status = idcmd(cur_file, &cmdblk, 0);

	if (status == -1) {
		err_print("Could not write ISP-80 working LIST\n");
		return (-1);
	}
	return (0);
}

/*
 * This routine runs all the commands that use the generic command ioctl
 * interface.  It is the back door into the driver.
 */
/*ARGSUSED*/
static int
idcmd(file, cmdblk, flags)
	int	file, flags;
	struct	hdk_cmd *cmdblk;
{
	int	status;
	struct	hdk_diag	dkdiag;
#ifdef  DEBUG
	fmt_print("cmd %x blkno %d, count %d, bufaddr %x, buflen %x\n",
		cmdblk->hdkc_cmd,
		cmdblk->hdkc_blkno, cmdblk->hdkc_secnt, cmdblk->hdkc_bufaddr,
		cmdblk->hdkc_buflen);
#endif

	if (option_msg || diag_msg) {
		cmdblk->hdkc_flags = HDK_DIAGNOSE;
	} else {
		cmdblk->hdkc_flags = HDK_DIAGNOSE | HDK_SILENT;
	}
	status = ioctl(file, HDKIOCSCMD, cmdblk);


	if (status == -1) {
		if (errno == EINVAL) {
#ifdef  DEBUG
			printf("idcmd: ipi disk doesn't exist\n");
#endif
			return (ID_ERROR);
		}
		if ((status = ioctl(file, HDKIOCGDIAG, &dkdiag)) == -1) {
			perror("format diag failed");	/* full abort?? */
			exit(-1);
		}

		media_error = 0;

		switch (dkdiag.hdkd_errno) {
		case IDE_ERRNO(IDE_NOERROR): /* no error */
		case IDE_ERRNO(IDE_RETRIED): /* retried OK */
			break;
		case IDE_ERRNO(IDE_CORR):    /* corrected data error */
		case IDE_ERRNO(IDE_UNCORR):  /* hard data error */
		case IDE_ERRNO(IDE_DATA_RETRIED): /* media retried OK */
			media_error = 1;
			if (option_msg || diag_msg) {
				fmt_print("Media error at sector %d\n",
				    dkdiag.hdkd_errsect);
			}
			return (ID_ERROR);
		case IDE_ERRNO(IDE_FATAL): /* unspecified bad error */
			if (option_msg || diag_msg) {
				fmt_print("Fatal error at sector %d\n",
				    dkdiag.hdkd_errsect);
			}
			break;
		default:
			fmt_print("Unknown error for cmd 0x%x, sector 0x%x, "
				"errno 0x%x, severity 0x%x\n",
				dkdiag.hdkd_errcmd, dkdiag.hdkd_errsect,
				dkdiag.hdkd_errno, dkdiag.hdkd_severe);
			break;
		}
		/*
		 * Disk may be reserved
		 */
		return (ID_MAY_BE_RESERVED);
	}

	return (status);
}
