/*
 *	nis_log_common.c
 *
 *	Copyright (c) 1988-1992 Sun Microsystems Inc
 *	All Rights Reserved.
 */

#pragma ident	"@(#)nis_log_common.c	1.13	94/12/15 SMI"


/*
 *	nis_log_common.c
 *
 * This module contains logging functions that are common to the service and
 * the log diagnostic utilities.
 */

#include <syslog.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <rpc/types.h>
#include <rpc/rpc.h>
#include <rpc/xdr.h>
#include <rpcsvc/nis.h>
#include "nis_proc.h"
#include "log.h"

/* string guard, it is always safe to print nilptr(char_pointer) */
#define	nilptr(s)	((s) ? (s) : "(nil)")

extern int verbose;
int	__nis_logfd 	= -1;
log_hdr	*__nis_log 	= NULL;
u_long	__nis_filesize	= FILE_BLK_SZ;
u_long	__nis_logsize;

/*
 * __make_name()
 *
 * This function prints out a nice name for a search entry.
 */
char *
__make_name(le)
	log_entry	*le;
{
	static char	namestr[2048];
	int		i;

	if (le->le_attrs.le_attrs_len)
		strcpy(namestr, "[ ");
	else
		namestr[0] = NUL;

	for (i = 0; i < le->le_attrs.le_attrs_len; i++) {
		strcat(namestr, le->le_attrs.le_attrs_val[i].zattr_ndx);
		strcat(namestr, " = ");
		if (le->le_attrs.le_attrs_val[i].zattr_val.zattr_val_len)
			strcat(namestr,
			le->le_attrs.le_attrs_val[i].zattr_val.zattr_val_val);
		else
			strcat(namestr, "(nil)");
		strcat(namestr, ", ");
	}

	if (le->le_attrs.le_attrs_len) {
		namestr[strlen(namestr) - 2] = '\0';
		strcat(namestr, " ],");
	}
	strcat(namestr, le->le_name);
	return (namestr);
}

/*
 * __log_resync()
 *
 * This function will relocate the log to its current position in
 * memory. Errors returned :
 *	 0	success
 *	-1 	Illegal Update
 *	-2 	Missing Data
 * 	-3 	Not enough updates
 *
 * Private flag (p):
 *	FNISD	called from nisd
 *	FNISLOG	called from nislog
 *	FCHKPT	called from checkpoint_log
 */
int
__log_resync(log, p)
	log_hdr	*log;
	int	p;
{
	log_upd		*prev, *cur;
	int		i, ret;
	u_long		addr_p;

	/*
	 * Resync the hard way. In this section of code we
	 * reconstruct the log pointers by calculating where
	 * the pieces would be placed. Our calcuation is
	 * verified by the presence of the appropriate MAGIC
	 * number at the address we've calculated. If any
	 * magic number isn't present, we note that the log
	 * is corrupt and exit the service. The user will
	 * have to either patch the log, or resync the slaves
	 * by hand. (Forced resync)
	 *
	 */

	if (verbose)
		syslog(LOG_INFO, "Resynchronizing transaction log.");

	addr_p = NISRNDUP((u_long)log + sizeof (log_hdr));
	prev = NULL;

	/*
	 * if there are any transactions, this is the first one.
	 */
	if (log->lh_num)
		log->lh_head = (log_upd *)(addr_p);
	if (verbose) {
		syslog(LOG_INFO, "Log has %d transactions in it.", log->lh_num);
		syslog(LOG_INFO, "Last valid transaction is %d.", log->lh_xid);
	}

	for (i = 0; i < log->lh_num; i++) {
		cur = (log_upd *)(addr_p);
		if (cur->lu_magic != LOG_UPD_MAGIC) {
			syslog(LOG_ERR,
			"__log_resync: Transaction #%d has a bad magic number.",
									i+1);
			break; /* major corruption */
		}

		/* Fix up the link lists */
		log->lh_tail = cur; /* Track the current update */
		if (prev)
			prev->lu_next = cur;
		cur->lu_prev = prev;
		cur->lu_next = NULL;
		cur->lu_dirname = (char *)(NISRNDUP((((u_long) cur) +
							sizeof (log_upd) +
							cur->lu_size)));
		prev = cur;

		/* move to next update in the list */
		if (verbose) {
			syslog(LOG_INFO, "Resync'd transaction #%d.", i+1);
			syslog(LOG_INFO, "Directory was '%s'.",
						nilptr(cur->lu_dirname));
		}
		addr_p += XID_SIZE(cur);
	}
	if (i < log->lh_num) {
		if (verbose)
			syslog(LOG_INFO, "%d valid transactions.", i);
		if (prev) {
			if (prev->lu_xid > log->lh_xid) {
				syslog(LOG_INFO,
		"__log_resync: Incomplete last update transaction, removing.");
				while (log->lh_tail->lu_xid > log->lh_xid) {
					log->lh_tail = log->lh_tail->lu_prev;
					i--;
					if (! log->lh_tail)
						break;
				}
			} else
				log->lh_tail = prev;
			log->lh_num = i;
		} else {
			log->lh_tail = NULL;
			log->lh_num = 0;
		}
	}
	__nis_logsize = (log->lh_tail) ? LOG_SIZE(log) : sizeof (log_hdr);
	if (verbose)
		syslog(LOG_INFO, "Log size is %d bytes", __nis_logsize);

	if (p == FNISLOG) {
		/* called from nislog, don't ftruncate() or msync() */
		return (0);
	} else if (p == FCHKPT) {
		/* called from checkpoint, truncate transaction log file */
		__nis_filesize = (((__nis_logsize / FILE_BLK_SZ) + 1) *
							FILE_BLK_SZ);
		ret = ftruncate(__nis_logfd, __nis_filesize);
		if (ret == -1) {
			syslog(LOG_ERR,
			"__log_resync: Cannot truncate transaction log file");
			return (-1);
		}
		ret = (int)lseek(__nis_logfd, 0L, SEEK_CUR);
		if (ret == -1) {
			syslog(LOG_ERR,
	"__log_resync: cannot seek to begining of transaction log file");
			return (-1);
		}
		ret = (int)lseek(__nis_logfd, __nis_filesize, SEEK_SET);
		if (ret == -1) {
			syslog(LOG_ERR,
		"__log_resync: cannot increase transaction log file size");
			return (-1);
		}
		ret = (int) write(__nis_logfd, "+", 1);
		if (ret != 1) {
			syslog(LOG_ERR,
		"__log_resync: cannot write one byte to transaction log file");
			return (-1);
		}
	}

	log->lh_addr = log;
	ret = msync((caddr_t)log, __nis_logsize, 0); /* Could take a while */
	if (ret == -1) {
		syslog(LOG_ERR, "msync() error in __log_resync()");
		abort();
	}
	return (0);
}

/*
 * map_log()
 *
 * This function maps in the logfile into the address space of thenis
 * server process. After the log is successfully mapped it is "relocated"
 * so that the pointers in the file are valid for the log's position in
 * memory. Once relocated, the log is ready for use. This function returns
 * 0 if the log file is OK and !0 if it is corrupted.
 *
 * 	Error numbers :
 *		-4	File not found.
 *		-5	Cannot MMAP file
 *		-6	Corrupt file, bad magic number in header.
 *		-7	Unknonwn (illegal) state value
 */

unsigned long __maxloglen = MAXLOGLEN;
unsigned long __loghiwater = HIWATER;

int
map_log(p)
	int	p;	/* Private flag:		*/
			/*	FNISD=called from nisd	*/
			/* 	FNISLOG=from nislog	*/
{
	char		logname[80];
	int		error, fd, ret;
	log_upd		*update;
	log_hdr		tmp_hdr;
	log_entry	*entry_1, *entry_2;
	struct stat	st;
	long		log_size;

	sprintf(logname, "%s", LOG_FILE);
	if (stat(logname, &st) == -1) {
		if (p == FNISLOG) {	/* called from nislog */
			return (-4);
		}

		__nis_logfd = open(logname, O_RDWR+O_CREAT, 0600);
		if (__nis_logfd == -1) {
			syslog(LOG_ERR, "Unable to open logfile '%s'", logname);
			return (-4);
		}

		/* Make this file as it doesn't exist */
		ret = (int)lseek(__nis_logfd, 0L, SEEK_CUR);
		if (ret == -1) {
			syslog(LOG_ERR,
		"map_log: cannot seek to begining of transaction log file");
			return (-1);
		}
		ret = (int)lseek(__nis_logfd, __nis_filesize, SEEK_SET);
		if (ret == -1) {
			syslog(LOG_ERR,
			"map_log: cannot increase transaction log file size");
			return (-1);
		}
		/* writing a / all the time seemed silly */
		ret = (int) write(__nis_logfd, "+", 1);
		if (ret != 1) {
			syslog(LOG_ERR,
		"map_log: cannot write one byte to transaction log file");
			return (-1);
		}
		fstat(__nis_logfd, &st);
	} else {	/* transaction log file exists */
		__maxloglen = (st.st_size > __maxloglen) ? st.st_size :
								__maxloglen;
		__nis_logfd = open(logname, O_RDWR, 0600);
		if (__nis_logfd == -1) {
			syslog(LOG_ERR, "Unable to open logfile '%s'", logname);
			return (-4);
		}
	}
	__nis_filesize = st.st_size;


	if (p == FNISLOG) {
		/* called from nislog */
		__nis_log = (log_hdr *)mmap(0, __nis_filesize,
			PROT_READ+PROT_WRITE, MAP_PRIVATE, __nis_logfd, 0);
	} else {
		/* called from nisd */
		__nis_log = (log_hdr *)mmap(0, __maxloglen,
			PROT_READ+PROT_WRITE, MAP_SHARED, __nis_logfd, 0);
	}

	if ((int)(__nis_log) == -1) {
		syslog(LOG_ERR,
		"Unable to map logfile of length %ld into address space.",
								__maxloglen);
		close(__nis_logfd);
		return (-5);
	}

	if (__nis_log->lh_magic != LOG_HDR_MAGIC) {
		/* If it's NULL then we just created the file */
		if (__nis_log->lh_magic == 0) {
			(void) memset(__nis_log, 0, sizeof (log_hdr));
			__nis_log->lh_state = LOG_STABLE;
			__nis_log->lh_magic = LOG_HDR_MAGIC;
			__nis_log->lh_addr  = __nis_log;
			if (p == FNISD)
				msync((caddr_t)__nis_log, sizeof (log_hdr), 0);
		} else {
			syslog(LOG_ERR, "Illegal log file, remove and restart");
			return (-6);
		}

	}
	switch (__nis_log->lh_state) {
		case LOG_STABLE :
			if (verbose)
				syslog(LOG_INFO, "Log state is STABLE.");
			error = __log_resync(__nis_log, p);
			if (error)
				return (error); /* resync failed */
			break;
		case LOG_RESYNC :
			if (verbose)
				syslog(LOG_INFO, "Log state is RESYNC.");
			error = __log_resync(__nis_log, p);
			if ((error) || (p == FNISLOG))
				return (error);
			__nis_log->lh_state = LOG_STABLE;
			msync((caddr_t)__nis_log, sizeof (log_hdr), 0);
			break;
		case LOG_UPDATE :
			if (verbose)
				syslog(LOG_INFO, "Log state is IN UPDATE.");
			error = __log_resync(__nis_log, p);
			if ((error) || (p == FNISLOG))
				return (error);

			update = __nis_log->lh_tail;
			/*
			 * Check to see if the update was written to the
			 * log.
			 */
			if (update->lu_xid == __nis_log->lh_xid) {
				/* never made it, so we're done. */
				__nis_log->lh_state = LOG_STABLE;
				msync((caddr_t)__nis_log, sizeof (log_hdr), 0);
				return (0);
			}
			/* Back out the change */
			return (abort_transaction(0));
			break;
		case LOG_CHECKPOINT :
			if (verbose)
				syslog(LOG_INFO, "Log state is IN CHECKPOINT");
			if (p == FNISLOG) {
				error = __log_resync(__nis_log, p);
				return (error);
			}
			strcpy(logname, nis_data(BACKUP_LOG));
			fd = open(logname, O_RDONLY);
			if (fd == -1) {
				syslog(LOG_ERR, "map_log: missing backup.");
				return (-1);
			}
			error = read(fd, &log_size, sizeof (long));
			if (error == -1) {
				syslog(LOG_ERR,
					"map_log: unable to read backup");
				close(fd);
				return (-1);
			}
			error = read(fd, __nis_log, log_size);
			close(fd);
			if ((error == -1) || (error != log_size)) {
				__nis_log->lh_state = LOG_CHECKPOINT;
				syslog(LOG_ERR,
				"map_log: Backup log truncated, fatal error.");
				return (-1);
			}
			/* Manually resync the log */
			error = __log_resync(__nis_log, p);
			if (! error) {
				__nis_log->lh_state = LOG_STABLE;
				msync((caddr_t)__nis_log, sizeof (log_hdr), 0);
				unlink(logname);
				break;
			}
			syslog(LOG_ERR,
			"map_log: Unable to resync after checkpoint restore.");
			return (error);
			break;
		default :
			syslog(LOG_ERR,
				"Illegal log state, aborting.");
			return (-7);
	}
	/* At this point everything should be cool. */
	return (0);
}
