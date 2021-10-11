/*
 * Copyright (c) 1993 Sun Microsystems, Inc.  All Rights Reserved.
 */
#pragma ident "@(#)codesets.h	1.6	93/04/22 SMI"

typedef struct {
	char *name;			/* name of the codeset */
	const char *const bdf_file;	/* pathname of the BDF file */
} codeset_t;

#if _EGA

static codeset_t chars8x8[] = {
	{ "437", "/usr/share/lib/fonts/437.bdf" },
	{ NULL, NULL }
};
static codeset_t chars8x14[] = {
	{ "437", "/usr/share/lib/fonts/437.bdf" },
	{ NULL, NULL }
};
static codeset_t chars8x14m[] = {
	{ "437", NULL }
	{ NULL, NULL }
};
#endif /* _EGA */

static codeset_t chars8x16[] = {
	{ "8859", "/usr/share/lib/fonts/8859.bdf"     },
	{ "437", "/usr/share/lib/fonts/437.bdf"       },
	{ "8859-1", "/usr/share/lib/fonts/8859-1.bdf" },
	{ "8859-2", "/usr/share/lib/fonts/8859-2.bdf" },
	{ "8859-3", "/usr/share/lib/fonts/8859-3.bdf" },
	{ "8859-4", "/usr/share/lib/fonts/8859-4.bdf" },
	{ "8859-5", "/usr/share/lib/fonts/8859-5.bdf" },
	{ "8859-7", "/usr/share/lib/fonts/8859-7.bdf" },
	{ "8859-9", "/usr/share/lib/fonts/8859-9.bdf" },
	{ "646g", "/usr/share/lib/fonts/646g.bdf"     },
	{ "646y", "/usr/share/lib/fonts/646y.bdf"     },
	{ "850", "/usr/share/lib/fonts/850.bdf"       },
	{ "861", "/usr/share/lib/fonts/861.bdf"       },
	{ "863", "/usr/share/lib/fonts/863.bdf"       },
	{ "865", "/usr/share/lib/fonts/865.bdf"       },
	{ "866", "/usr/share/lib/fonts/866.bdf"       },
	{ "csfr", "/usr/share/lib/fonts/csfr.bdf"     },
	{ "greek", "/usr/share/lib/fonts/greek.bdf"   },
	{ NULL, NULL }
};
