
#ifndef lint
#pragma ident	"@(#)init_menus.c	1.17	95/02/18 SMI"
#endif	lint

/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

/*
 * This file contains the declarations of menus for the program.  To add
 * a new command/menu, simply add it to the appropriate table and define
 * the function that executes it.
 */
#include "global.h"
#include "menu.h"
#include "menu_partition.h"
#include "menu_command.h"
#include "menu_analyze.h"
#include "menu_defect.h"
#include "add_definition.h"
#include "menu_scsi.h"
#include "menu_developer.h"

#ifdef i386
#include "menu_fdisk.h"
#endif


/*
 * This declaration is for the command menu.  It is the menu first
 * encountered upon entering the program.
 */
struct	menu_item menu_command[] = {
	{ "disk       - select a disk",
		c_disk, true },

	{ "type       - select (define) a disk type",
		c_type, true },

	{ "partition  - select (define) a partition table",
		c_partition, true },

	{ "current    - describe the current disk",
		c_current, true },

	{ "format     - format and analyze the disk",
		c_format, true },

#ifdef i386
	{ "fdisk      - run the fdisk program",
		c_fdisk, true },
#endif

	{ "repair     - repair a defective sector",
		c_repair, true },

	{ "show       - translate a disk address",
		c_show,	 not_scsi },

	{ "label      - write label to the disk",
		c_label, true },

	{ "analyze    - surface analysis",
		c_analyze, true },

	{ "defect     - defect list management",
		c_defect, true },

	{ "backup     - search for backup labels",
		c_backup, true },

	{ "verify     - read and display labels",
		c_verify, true },

	{ "save       - save new disk/partition definitions",
		add_definition, true },

	{ "inquiry    - show vendor, product and revision",
		c_inquiry, scsi },

	{ "scsi       - independent SCSI mode selects",
		c_scsi, scsi_expert },

	{ "volname    - set 8-character volume name",
		c_volname, true },

	{ "developer  - dump developer things",
		c_developer, developer },

	{ NULL }
};


/*
 * This declaration is for the partition menu.  It is used to create
 * and maintain partition tables.
 */
struct	menu_item menu_partition[] = {
	{ "0      - change `0' partition",
		p_apart, true },
	{ "1      - change `1' partition",
		p_bpart, true },
	{ "2      - change `2' partition",
		p_cpart, true },
	{ "3      - change `3' partition",
		p_dpart, true },
	{ "4      - change `4' partition",
		p_epart, true },
	{ "5      - change `5' partition",
		p_fpart, true },
	{ "6      - change `6' partition",
		p_gpart, true },
	{ "7      - change `7' partition",
		p_hpart, true },
#ifdef i386
	{ "9      - change `9' partition",
		p_jpart, expert },
#endif
	{ "select - select a predefined table",
		p_select, true },
	{ "modify - modify a predefined partition table",
		p_modify, true },
	{ "name   - name the current table",
		p_name, true },
	{ "print  - display the current table",
		p_print, true },
	{ "label  - write partition map and label to the disk",
		c_label, true },
	{ NULL }
};



/*
 * This declaration is for the analysis menu.  It is used to set up
 * and execute surface analysis of a disk.
 */
struct menu_item menu_analyze[] = {
	{ "read     - read only test   (doesn't harm SunOS)",
		a_read,	 true },
	{ "refresh  - read then write  (doesn't harm data)",
		a_refresh, true },
	{ "test     - pattern testing  (doesn't harm data)",
		a_test,	 true },
	{ "write    - write then read      (corrupts data)",
		a_write, true },
	{ "compare  - write, read, compare (corrupts data)",
		a_compare, true },
	{ "purge    - write, read, write   (corrupts data)",
		a_purge, true },
	{ "verify   - write entire disk, then verify (corrupts data)",
		a_verify, true },
	{ "print    - display data buffer",
		a_print, true },
	{ "setup    - set analysis parameters",
		a_setup, true },
	{ "config   - show analysis parameters",
		a_config, true },
	{ NULL }
};



/*
 * This declaration is for the defect menu.  It is used to manipulate
 * the defect list for a disk.
 */
struct menu_item menu_defect[] = {
	{ "restore  - set working list = current list",
		d_restore, not_embedded_scsi },
	{ "original - extract manufacturer's list from disk",
		d_original, not_embedded_scsi },
	{ "extract  - extract working list from disk",
		d_extract, not_embedded_scsi },
	{ "primary  - extract manufacturer's defect list",
		d_primary, embedded_scsi },
	{ "grown    - extract manufacturer's and repaired defects lists",
		d_grown, embedded_scsi },
	{ "both     - extract both primary and grown defects lists",
		d_both, embedded_scsi },
	{ "add      - add defects to working list",
		d_add,	 not_embedded_scsi },
	{ "delete   - delete a defect from working list",
		d_delete, not_embedded_scsi },
	{ "print    - display working list",
		d_print, true },
	{ "dump     - dump working list to file",
		d_dump,	 true },
	{ "load     - load working list from file",
		d_load,	 not_embedded_scsi },
	{ "commit   - set current list = working list",
		d_commit, not_embedded_scsi },
	{ "create   - recreates maufacturer's defect list on disk",
		d_create, not_embedded_scsi },
	{ NULL }
};

/*
 * This declaration is for the developer menu.
 */
struct menu_item menu_developer[] = {
	{ "dump_disk  - dump disk entries",
		dv_disk, true },
	{ "dump_cont  - dump controller entries",
		dv_cont, true },
	{ "dump_c_chain - dump controller chain entries",
		dv_cont_chain, true },
	{ "dev_params - dump device parameters",
		dv_params, true },
	{ NULL }
};
