
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)main.c	1.16	95/02/26 SMI"

/*
 * This file contains the main entry point of the program and other
 * routines relating to the general flow.
 */
#include "global.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <memory.h>
#include <string.h>
#include <sys/fcntl.h>

#ifdef sparc
#include <sys/hdio.h>
#include <sys/dkbad.h>
#endif

#include <sys/time.h>
#include "main.h"
#include "analyze.h"
#include "menu.h"
#include "param.h"
#include "misc.h"
#include "startup.h"
#include "menu_command.h"
#include "menu_partition.h"
#include "prompts.h"
#include "checkmount.h"
#include "label.h"

extern	int	errno;
extern	char	*sys_errlist[];

extern	struct menu_item menu_command[];


/*
 * This is the main entry point.
 */
void
main(argc, argv)
	int	argc;
	char	*argv[];
{
	int	i;
	char	**arglist;
	struct	disk_info *disk = NULL;
	struct	disk_type *type, *oldtype;
	struct	partition_info *parts;
	struct	sigaction act;

	solaris_offset = 0;
	/*
	 * Catch ctrl-C and ctrl-Z so critical sections can be
	 * implemented.  We use sigaction, as this sets up the
	 * signal handler permanently, and also automatically
	 * restarts any interrupted system call.
	 */
	act.sa_handler = cmdabort;
	memset(&act.sa_mask, 0, sizeof (sigset_t));
	act.sa_flags = SA_RESTART;
	if (sigaction(SIGINT, &act, (struct sigaction *)NULL) == -1) {
		err_print("sigaction(SIGINT) failed - %s\n",
			sys_errlist[errno]);
		fullabort();
	}

	act.sa_handler = onsusp;
	memset(&act.sa_mask, 0, sizeof (sigset_t));
	act.sa_flags = SA_RESTART;
	if (sigaction(SIGTSTP, &act, (struct sigaction *)NULL) == -1) {
		err_print("sigaction(SIGTSTP) failed - %s\n",
			sys_errlist[errno]);
		fullabort();
	}

	act.sa_handler = onalarm;
	memset(&act.sa_mask, 0, sizeof (sigset_t));
	act.sa_flags = SA_RESTART;
	if (sigaction(SIGALRM, &act, (struct sigaction *)NULL) == -1) {
		err_print("sigaction(SIGALRM) failed - %s\n",
			sys_errlist[errno]);
		fullabort();
	}

	/*
	 * Decode the command line options.
	 */
	i = do_options(argc, argv);
	/*
	 * If we are to run from a command file, open it up.
	 */
	if (option_f) {
		if (freopen(option_f, "r", stdin) == NULL) {
			err_print("Unable to open command file '%s'.\n",
				option_f);
			fullabort();
		}
	}
	/*
	 * If we are logging, open the log file.
	 */
	if (option_l) {
		if ((log_file = fopen(option_l, "w")) == NULL) {
			err_print("Unable to open log file '%s'.\n",
				option_l);
			fullabort();
		}
	}
	/*
	 * Read in the data file and initialize the hardware structs.
	 */
	sup_init();
	/*
	 * If there are no disks on the command line, search the
	 * appropriate device directory for character devices that
	 * look like disks.
	 */
	if (i < 0) {
		arglist = (char **)NULL;
	/*
	 * There were disks on the command line.  They comprise the
	 * search list.
	 */
	} else {
		arglist = &argv[i];
	}
	/*
	 * Perform the search for disks.
	 */
	do_search(arglist);
	/*
	 * If there was only 1 disk on the command line, mark it
	 * to be the current disk.  If it wasn't found, it's an error.
	 */
	if (i == argc - 1) {
		disk = disk_list;
		if (disk == NULL) {
			err_print("Unable to find specified disk '%s'.\n",
			    argv[i]);
			fullabort();
		}
	}
	/*
	 * A disk was forced on the command line.
	 */
	if (option_d) {
		/*
		 * Find it in the list of found disks and mark it to
		 * be the current disk.
		 */
		for (disk = disk_list; disk != NULL; disk = disk->disk_next)
			if (diskname_match(option_d, disk))
				break;
		/*
		 * If it wasn't found, it's an error.
		 */
		if (disk == NULL) {
			err_print("Unable to find specified disk '%s'.\n",
			    option_d);
			fullabort();
		}
	}
	/*
	 * A disk type was forced on the command line.
	 */
	if (option_t) {
		/*
		 * Only legal if a disk was also forced.
		 */
		if (disk == NULL) {
			err_print("Must specify disk as well as type.\n");
			fullabort();
		}
		oldtype = disk->disk_type;
		/*
		 * Find the specified type in the list of legal types
		 * for the disk.
		 */
		for (type = disk->disk_ctlr->ctlr_ctype->ctype_dlist;
		    type != NULL; type = type->dtype_next)
			if (strcmp(option_t, type->dtype_asciilabel) == 0)
				break;
		/*
		 * If it wasn't found, it's an error.
		 */
		if (type == NULL) {
			err_print(
"Specified type '%s' is not a known type.\n", option_t);
			fullabort();
		}
		/*
		 * If the specified type is not the same as the type
		 * in the disk label, update the type and nullify the
		 * partition map.
		 */
		if (type != oldtype) {
			disk->disk_type = type;
			disk->disk_parts = NULL;
		}
	}
	/*
	 * A partition map was forced on the command line.
	 */
	if (option_p) {
		/*
		 * Only legal if both disk and type were also forced.
		 */
		if (disk == NULL || disk->disk_type == NULL) {
			err_print("Must specify disk and type as well ");
			err_print("as partitiion.\n");
			fullabort();
		}
		/*
		 * Find the specified map in the list of legal maps
		 * for the type.
		 */
		for (parts = disk->disk_type->dtype_plist; parts != NULL;
		    parts = parts->pinfo_next)
			if (strcmp(option_p, parts->pinfo_name) == 0)
				break;
		/*
		 * If it wasn't found, it's an error.
		 */
		if (parts == NULL) {
			err_print(
"Specified table '%s' is not a known table.\n", option_p);
			fullabort();
		}
		/*
		 * Update the map.
		 */
		disk->disk_parts = parts;
	}
	/*
	 * If a disk was marked to become current, initialize the state
	 * to make it current.  If not, ask user to pick one.
	 */
	if (disk != NULL) {
		init_globals(disk);
	} else if (option_f == 0 && option_d == 0) {
		(void) c_disk();
	}

#ifdef	BUG1134748
	/*
	 * if -f command-file is specified, check for disk and disktype
	 * input also. For SCSI disks, the type input may not be needed
	 * since format would have figured that using inquiry information.
	 */
	if (option_f) {
		if (cur_disk == NULL) {
			err_print("Must specify a disk using -d option.\n");
			fullabort();
		}
		if (cur_dtype == NULL) {
			err_print("Must specify disk as well as type.\n");
			fullabort();
		}
	}
#endif	BUG1134748

	/*
	 * Run the command menu.
	 */
	cur_menu = last_menu = 0;
	run_menu(menu_command, "FORMAT", "format", 1);

	/*
	 * normal ending. Explicitly exit(0);
	 */
	exit(0);
	/* NOTREACHED */
}

#ifdef sparc
/*
 * This routine notifies the SunOS kernel of the geometry
 * for the current disk.  It also tells it the drive type if that
 * is necessary for the ctlr.
 */
int
notify_unix()
{
	struct	hdk_type type;
	struct	dk_geom geom;
	int	status = 0, error = 0;

	/*
	 * Zero out the ioctl structs.
	 */
	bzero((char *)&type, sizeof (struct hdk_type));
	bzero((char *)&geom, sizeof (struct dk_geom));

	/*
	 * Let us do SGEOM, only if the drive is going to be formatted.
	 * We need not alter the present geom settings for drives that are
	 * in use already.
	 * We do this only for XY450 and XD7053 drives.
	 */
	if (!(cur_flags & DISK_FORMATTED) &&
		(cur_ctype->ctype_ctype == DKC_XY450 ||
		cur_ctype->ctype_ctype == DKC_XD7053)) {
		/*
		 * Fill in the geometry info.
		 */
		geom.dkg_ncyl = ncyl;
		geom.dkg_acyl = acyl;
		geom.dkg_nhead = nhead;
		geom.dkg_nsect = nsect;
		geom.dkg_intrlv = 1;
		geom.dkg_apc = apc;
		geom.dkg_rpm = cur_dtype->dtype_rpm;
		geom.dkg_pcyl = pcyl;
		/*
		 * Do the ioctl to tell the kernel the geometry.
		 */
		status = ioctl(cur_file, DKIOCSGEOM, &geom);
		if (status) {
			err_print(
			"Warning: error telling SunOS drive geometry.\n");
			error = -1;
		}
	}

	/*
	 * If this ctlr needs drive types, do an ioctl to tell it the
	 * drive type for the current disk.
	 */
	if (cur_ctype->ctype_flags & CF_450_TYPES) {
		type.hdkt_drtype = cur_dtype->dtype_dr_type;
		status = ioctl(cur_file, HDKIOCSTYPE, &type);
		if (status) {
			err_print("Warning: error telling SunOS drive type.\n");
			error = -1;
		}
	}
	/*
	 * Return status.
	 */
	return (error);
}
#endif

/*
 * This routine initializes the internal state to ready it for a new
 * current disk.  There are a zillion state variables that store
 * information on the current disk, and they must all be updated.
 * We also tell SunOS about the disk, since it may not know if the
 * disk wasn't labeled at boot time.
 */
void
init_globals(disk)
	struct	disk_info *disk;
{
	int		i;
	int		status;
#ifdef sparc
	caddr_t		bad_ptr = (caddr_t)&badmap;
#endif

	/*
	 * If there was an old current disk, close the file for it.
	 */
	if (cur_disk != NULL)
		(void) close(cur_file);
	/*
	 * Kill off any defect lists still lying around.
	 */
	kill_deflist(&cur_list);
	kill_deflist(&work_list);
	/*
	 * If there were any buffers, free them up.
	 */
	if ((char *)cur_buf != NULL) {
		destroy_data((char *)cur_buf);
		cur_buf = NULL;
	}
	if ((char *)pattern_buf != NULL) {
		destroy_data((char *)pattern_buf);
		pattern_buf = NULL;
	}
	/*
	 * Fill in the hardware struct pointers for the new disk.
	 */
	cur_disk = disk;
	cur_dtype = cur_disk->disk_type;
	cur_ctlr = cur_disk->disk_ctlr;
	cur_parts = cur_disk->disk_parts;
	cur_ctype = cur_ctlr->ctlr_ctype;
	cur_ops = cur_ctype->ctype_ops;
	cur_flags = 0;
	/*
	 * Open a file for the new disk.
	 */
	if ((cur_file = open_disk(cur_disk->disk_path,
					O_RDWR | O_NDELAY)) < 0) {
		err_print(
"Error: can't open selected disk '%s'.\n", cur_disk->disk_name);
		fullabort();
	}
#ifdef sparc
	/*
	 * If the new disk uses bad-144, initialize the bad block table.
	 */
	if (cur_ctlr->ctlr_flags & DKI_BAD144) {
		badmap.bt_mbz = badmap.bt_csn = badmap.bt_flag = 0;
		for (i = 0; i < NDKBAD; i++) {
			badmap.bt_bad[i].bt_cyl = -1;
			badmap.bt_bad[i].bt_trksec = -1;
		}
	}
#endif
	/*
	 * If the type of the new disk is known...
	 */
	if (cur_dtype != NULL) {
		/*
		 * Initialize the physical characteristics.
		 * If need disk specs, prompt for undefined disk
		 * characteristics.  If running from a file,
		 * use defaults.
		 */
		if (cur_dtype->dtype_flags & DT_NEED_SPEFS) {
			get_disk_characteristics();
			cur_dtype->dtype_flags &= ~DT_NEED_SPEFS;
		}

		ncyl = cur_dtype->dtype_ncyl;
		acyl = cur_dtype->dtype_acyl;
		pcyl = cur_dtype->dtype_pcyl;
		nhead = cur_dtype->dtype_nhead;
		nsect = cur_dtype->dtype_nsect;
		phead = cur_dtype->dtype_phead;
		psect = cur_dtype->dtype_psect;
		/*
		 * Alternates per cylinder are forced to 0 or 1,
		 * independent of what the label says.  This works
		 * because we know which ctlr we are dealing with.
		 */
		if (cur_ctype->ctype_flags & CF_APC)
			apc = 1;
		else
			apc = 0;
#ifdef sparc
		/*
		 * For the xy450 controller, check the drive type.
		 */
		if (cur_ctype->ctype_flags & CF_450_TYPES) {
			check_xy450_drive_type();
		}
		/*
		 * Notify the SunOS kernel of what we have found.
		 */
		(void) notify_unix();
#endif
		/*
		 * Initialize the surface analysis info.  We always start
		 * out with scan set for the whole disk.  Note,
		 * for SCSI disks, we can only scan the data area.
		 */
		scan_lower = 0;
		scan_size = BUF_SECTS;
		if (cur_ctype->ctype_flags & CF_SCSI)
			scan_upper = datasects() - 1;
		else
			scan_upper = physsects() - 1;

		/*
		 * Allocate the buffers.
		 */
		cur_buf = (void *) zalloc(BUF_SECTS * SECSIZE);
		pattern_buf = (void *) zalloc(BUF_SECTS * SECSIZE);

		/*
		 * Tell the user which disk (s)he selected.
		 */
		if (chk_volname(cur_disk)) {
			fmt_print("selecting %s: ", cur_disk->disk_name);
			print_volname(cur_disk);
			fmt_print("\n");
		} else {
			fmt_print("selecting %s\n", cur_disk->disk_name);
		}

		/*
		 * If the drive is formatted...
		 */
		if ((*cur_ops->op_ck_format)()) {
			/*
			 * Mark it formatted.
			 */
			cur_flags |= DISK_FORMATTED;
			/*
			 * Read the defect list, if we have one.
			 */
			if (!EMBEDDED_SCSI) {
				read_list(&cur_list);
			}
#ifdef sparc
			/*
			 * If the disk does BAD-144, we do an ioctl to
			 * tell SunOS about the bad block table.
			 */
			if (cur_ctlr->ctlr_flags & DKI_BAD144) {
				if (ioctl(cur_file, HDKIOCSBAD, &bad_ptr)) {
					err_print(
"Warning: error telling SunOS bad block map table.\n");
				}
			}
#endif
			fmt_print("[disk formatted");
			if (!EMBEDDED_SCSI) {
				if (cur_list.list != NULL) {
					fmt_print(", defect list found");
				} else {
					fmt_print(", no defect list found");
				}
			}
			fmt_print("]");
		/*
		 * Drive wasn't formatted.  Tell the user in case he
		 * disagrees.
		 */
		} else if (EMBEDDED_SCSI) {
			fmt_print("[disk unformatted]");
		} else {
			/*
			 * Make sure the user is serious.  Note, for
			 * SCSI disks since this is instantaneous, we
			 * will just do it and not ask for confirmation.
			 */
			status = 0;
			if (!(cur_ctype->ctype_flags & CF_CONFIRM)) {
				if (check("\n\
Ready to get manufacturer's defect list from unformatted drive.\n\
This cannot be interrupted and takes a long while.\n\
Continue"))
					status = 1;
				else
					fmt_print(
				"Extracting manufacturer's defect list...");
			}
			/*
			 * Extract manufacturer's defect list.
			 */
			if ((status == 0) && (cur_ops->op_ex_man != NULL)) {
				status = (*cur_ops->op_ex_man)(&cur_list);
			} else {
				status = 1;
			}
			fmt_print("[disk unformatted");
			if (status != 0) {
				fmt_print(", no defect list found]");
			} else {
				fmt_print(", defect list found]");
			}
		}
	} else {
		/*
		 * Disk type is not known.
		 * Initialize physical characteristics to 0 and tell the
		 * user we don't know what type the disk is.
		 */
		ncyl = acyl = nhead = nsect = psect = 0;
	}

	fmt_print("\n");

	/*
	 * Check to see if there are any mounted file systems on the
	 * disk.  If there are, print a warning.
	 */
	if (checkmount((daddr_t)-1, (daddr_t)-1))
		err_print("Warning: Current Disk has mounted partitions.\n");
}


/*
 * Prompt for some undefined disk characteristics.
 * Used when there is no disk definition, but the
 * disk has a valid label, so basically we're
 * prompting for everything that isn't in the label.
 */
void
get_disk_characteristics()
{
	int		status;
#ifdef sparc
	struct		hdk_type dktype;
#endif

	/*
	 * The need_spefs flag is used to tell us that this disk
	 * is not a known type and the ctlr specific info must
	 * be prompted for.  We only prompt for the info that applies
	 * to this ctlr.
	 */
	assert(cur_dtype->dtype_flags & DT_NEED_SPEFS);

	/*
	 * If we're running with input from a file, use
	 * reasonable defaults, since prompting for the
	 * information will probably mess things up.
	 */
	if (option_f) {
		cur_dtype->dtype_pcyl = ncyl + acyl;
		cur_dtype->dtype_rpm = AVG_RPM;
		cur_dtype->dtype_bpt = INFINITY;
		cur_dtype->dtype_phead = 0;
		cur_dtype->dtype_psect = 0;
		cur_dtype->dtype_cyl_skew = 0;
		cur_dtype->dtype_trk_skew = 0;
		cur_dtype->dtype_trks_zone = 0;
		cur_dtype->dtype_atrks = 0;
		cur_dtype->dtype_asect = 0;
		cur_dtype->dtype_cache = 0;
		cur_dtype->dtype_threshold = 0;
		cur_dtype->dtype_prefetch_min = 0;
		cur_dtype->dtype_prefetch_max = 0;

		if (cur_ctype->ctype_flags & CF_SMD_DEFS) {
			cur_dtype->dtype_bps = AVG_BPS;
		}
#ifdef sparc
		if (cur_ctype->ctype_flags & CF_450_TYPES) {
			status = ioctl(cur_file, HDKIOCGTYPE, &dktype);
			if (status) {
				err_print(
				"Unable to read drive configuration.\n");
				fullabort();
			}
			cur_dtype->dtype_dr_type = dktype.hdkt_drtype;
		}
#endif
	} else {

		cur_dtype->dtype_pcyl = get_pcyl(ncyl, cur_dtype->dtype_acyl);
		cur_dtype->dtype_bpt = get_bpt(cur_dtype->dtype_nsect,
			&cur_dtype->dtype_options);
		cur_dtype->dtype_rpm = get_rpm();
		cur_dtype->dtype_fmt_time =
			get_fmt_time(&cur_dtype->dtype_options);
		cur_dtype->dtype_cyl_skew =
			get_cyl_skew(&cur_dtype->dtype_options);
		cur_dtype->dtype_trk_skew =
			get_trk_skew(&cur_dtype->dtype_options);
		cur_dtype->dtype_trks_zone =
			get_trks_zone(&cur_dtype->dtype_options);
		cur_dtype->dtype_atrks = get_atrks(&cur_dtype->dtype_options);
		cur_dtype->dtype_asect = get_asect(&cur_dtype->dtype_options);
		cur_dtype->dtype_cache = get_cache(&cur_dtype->dtype_options);
		cur_dtype->dtype_threshold =
			get_threshold(&cur_dtype->dtype_options);
		cur_dtype->dtype_prefetch_min =
			get_min_prefetch(&cur_dtype->dtype_options);
		cur_dtype->dtype_prefetch_max =
			get_max_prefetch(cur_dtype->dtype_prefetch_min,
			&cur_dtype->dtype_options);
		cur_dtype->dtype_phead =
			get_phead(nhead, &cur_dtype->dtype_options);
		cur_dtype->dtype_psect = get_psect(&cur_dtype->dtype_options);
		cur_dtype->dtype_bps = get_bps();
#ifdef sparc
		cur_dtype->dtype_dr_type =
			get_drive_type(&cur_dtype->dtype_options);
#endif
	}
}
