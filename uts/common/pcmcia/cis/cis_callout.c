/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident "@(#)cis_callout.c	1.9	95/07/17 SMI"

/*
 * This file contains an array of structures, each of which refers to
 *	a tuple that we are prepared to handle.  The last structure
 *	in this array must have a type of CISTPL_END.
 *
 * If you want the generic tuple handler to be called for a tuple, use
 *	the cis_no_tuple_handler() entry point.
 */

#include <sys/types.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/buf.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/autoconf.h>
#include <sys/vtoc.h>
#include <sys/dkio.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/kstat.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/kobj.h>

#include <pcmcia/sys/cis.h>
#include <pcmcia/sys/cis_handlers.h>
#include <pcmcia/sys/cs_types.h>
#include <pcmcia/sys/cs.h>
#include <pcmcia/sys/cs_priv.h>
#include <pcmcia/sys/cis_protos.h>

/*
 * cistpl_std_callout - callout list for standard tuples
 */
cistpl_callout_t cistpl_std_callout[] = {
	{	CISTPL_DEVICE,			/* device information */
		0,
		0,
		cistpl_device_handler,
		"CISTPL_DEVICE"		},
	{	CISTPL_CHECKSUM,		/* checksum control */
		0,
		0,
		cis_no_tuple_handler,
		"CISTPL_CHECKSUM"	},
	{	CISTPL_LONGLINK_A,		/* long-link to AM */
		0,
		0,
		cistpl_longlink_ac_handler,
		"CISTPL_LONGLINK_A"	},
	{	CISTPL_LONGLINK_C,		/* long-link to CM */
		0,
		0,
		cistpl_longlink_ac_handler,
		"CISTPL_LONGLINK_C"	},
	{	CISTPL_LINKTARGET,		/* link-target control */
		0,
		0,
		cistpl_linktarget_handler,
		"CISTPL_LINKTARGET"	},
	{	CISTPL_NO_LINK,			/* no-link control */
		0,
		0,
		cis_no_tuple_handler,
		"CISTPL_NO_LINK"	},
	{	CISTPL_VERS_1,			/* level 1 version info */
		0,
		0,
		cistpl_vers_1_handler,
		"CISTPL_VERS_1"		},
	{	CISTPL_ALTSTR,			/* alternate language string */
		0,
		0,
		cis_no_tuple_handler,
		"CISTPL_ALTSTR"		},
	{	CISTPL_DEVICE_A,		/* AM device information */
		0,
		0,
		cistpl_device_handler,
		"CISTPL_DEVICE_A"	},
	{	CISTPL_JEDEC_C,			/* JEDEC info for CM */
		0,
		0,
		cistpl_jedec_handler,
		"CISTPL_JEDEC_C"	},
	{	CISTPL_JEDEC_A,			/* JEDEC info for AM */
		0,
		0,
		cistpl_jedec_handler,
		"CISTPL_JEDEC_A"	},
	{	CISTPL_CONFIG,			/* configuration */
		0,
		0,
		cistpl_config_handler,
		"CISTPL_CONFIG"		},
	{	CISTPL_CFTABLE_ENTRY,		/* configuration-table-entry */
		0,
		0,
		cistpl_cftable_handler,
		"CISTPL_CFTABLE_ENTRY"	},
	{	CISTPL_DEVICE_OC,		/* other conditions for CM */
		0,
		0,
		cis_no_tuple_handler,
		"CISTPL_DEVICE_OC"	},
	{	CISTPL_DEVICE_OA,		/* other conditions for AM */
		0,
		0,
		cis_no_tuple_handler,
		"CISTPL_DEVICE_OA"	},
	{	CISTPL_VERS_2,			/* level 2 version info */
		0,
		0,
		cistpl_vers_2_handler,
		"CISTPL_VERS_2"		},
	{	CISTPL_FORMAT,			/* format type */
		0,
		0,
		cistpl_format_handler,
		"CISTPL_FORMAT"		},
	{	CISTPL_GEOMETRY,		/* geometry */
		0,
		0,
		cistpl_geometry_handler,
		"CISTPL_GEOMETRY"	},
	{	CISTPL_BYTEORDER,		/* byte order */
		0,
		0,
		cistpl_byteorder_handler,
		"CISTPL_BYTEORDER"	},
	{	CISTPL_DATE,			/* card initialization date */
		0,
		0,
		cistpl_date_handler,
		"CISTPL_DATE"		},
	{	CISTPL_BATTERY,			/* battery replacement date */
		0,
		0,
		cistpl_battery_handler,
		"CISTPL_BATTERY"	},
	{	CISTPL_ORG,			/* organization */
		0,
		0,
		cistpl_org_handler,
		"CISTPL_ORG"		},
	{	CISTPL_FUNCID,			/* card function ID */
		0,
		0,
		cistpl_funcid_handler,
		"CISTPL_FUNCID"		},
	{	CISTPL_FUNCE,			/* card function extension */
		TPLFUNC_MULTI,		/* for multifunction cards */
		0,
		cis_no_tuple_handler,
		"CISTPL_FUNCE/MULTI"	},
	{	CISTPL_FUNCE,			/* card function extension */
		TPLFUNC_MEMORY,		/* for memory cards */
		0,
		cis_no_tuple_handler,
		"CISTPL_FUNCE/MEMORY"	},
	{	CISTPL_FUNCE,			/* card function extension */
		TPLFUNC_SERIAL,		/* for serial port cards */
		0,
		cistpl_funce_serial_handler,
		"CISTPL_FUNCE/SERIAL"	},
	{	CISTPL_FUNCE,			/* card function extension */
		TPLFUNC_PARALLEL,		/* for parallel port cards */
		0,
		cis_no_tuple_handler,
		"CISTPL_FUNCE/PARALLEL"	},
	{	CISTPL_FUNCE,			/* card function extension */
		TPLFUNC_FIXED,		/* for fixed disk cards */
		0,
		cis_no_tuple_handler,
		"CISTPL_FUNCE/FIXED"	},
	{	CISTPL_FUNCE,			/* card function extension */
		TPLFUNC_VIDEO,		/* for video cards */
		0,
		cis_no_tuple_handler,
		"CISTPL_FUNCE/VIDEO"	},
	{	CISTPL_FUNCE,			/* card function extension */
		TPLFUNC_LAN,		/* for LAN cards */
		0,
		cistpl_funce_lan_handler,
		"CISTPL_FUNCE/LAN"	},
	{	CISTPL_FUNCE,			/* card function extension */
		TPLFUNC_UNKNOWN,	/* for unknown functions */
		0,
		cis_no_tuple_handler,
		"CISTPL_FUNCE/unknown"	},
	{	CISTPL_MANFID,			/* manufacturer ID */
		0,
		0,
		cistpl_manfid_handler,
		"CISTPL_MANFID"		},
	{	CISTPL_END,			/* end-of-list tuple */
		0,
		0,
		cis_no_tuple_handler,
		"unknown tuple"		},
	};
