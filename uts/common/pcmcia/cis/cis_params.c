/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ident "@(#)cis_params.c	1.6	95/01/25 SMI"

#include <sys/types.h>

#include <pcmcia/sys/cis.h>
#include <pcmcia/sys/cis_handlers.h>

/*
 *
 * The following speed tables are used by cistpl_devspeed() to generate
 *	device speeds from tuple data.
 *
 * Define the device speed table.  For a description of this table's contents,
 *	see PCMCIA Release 2.01 Card Metaformat pg. 5-14 table 5-12.
 *
 * All times in this table are in nS.
 */
int cistpl_devspeed_table[CISTPL_DEVSPEED_MAX_TBL] = {
    0,		/* 0x00 - DSPEED_NULL */
    250,	/* 0x01 - DSPEED_250NS */
    200,	/* 0x02 - DSPEED_200NS */
    150,	/* 0x03 - DSPEED_150NS */
    100,	/* 0x04 - DSPEED_100NS */
    0,		/* 0x05 - reserved */
    0,		/* 0x06 - reserved */
    0		/* 0x07 - use extended speed byte */
};

/*
 * Define the power-of-10 table.
 */
int cistpl_exspeed_tenfac[] = {
    1,		/* 10^0 */
    10,		/* 10^1 */
    100,	/* 10^2 */
    1000,	/* 10^3 */
    10000,	/* 10^4 */
    100000,	/* 10^5 */
    1000000,	/* 10^6 */
    10000000	/* 10^7	 */
};

/*
 * The extended device speed code mantissa table.
 *
 * This table is described in PCMCIA Release 2.01 Card Metaformat
 *	pg. 5-15 table 5-13.
 *
 * The description of this table uses non-integer values.  We multiply
 *	everything by 10 before it goes into the table, and the code
 *	will divide by 10 after it calculates the device speed.
 */
int cistpl_devspeed_man[CISTPL_DEVSPEED_MAX_MAN] = {
    0,		/* no units */
    10,		/* no units */
    12,		/* no units */
    13,		/* no units */
    15,		/* no units */
    20,		/* no units */
    25,		/* no units */
    30,		/* no units */
    35,		/* no units */
    40,		/* no units */
    45,		/* no units */
    50,		/* no units */
    55,		/* no units */
    60,		/* no units */
    70,		/* no units */
    80,		/* no units */
};

/*
 * The extended device speed code exponent table.
 *
 * This table is described in PCMCIA Release 2.01 Card Metaformat
 *	pg. 5-15 table 5-13.
 *
 * The description of this table uses various timing units.  This
 *	table contains all times in nS.
 */
int cistpl_devspeed_exp[CISTPL_DEVSPEED_MAX_EXP] = {
    1,		/* 1 nS */
    10,		/* 10 nS */
    100,	/* 100 nS */
    1000,	/* 1000 nS */
    10000,	/* 10000 nS */
    100000,	/* 100000 nS */
    1000000,	/* 1000000 nS */
    10000000	/* 10000000 nS */
};

/*
 * The power description mantissa table.
 *
 * This table is described in PCMCIA Release 2.01 Card Metaformat
 *	pg. 5-28 table 5-32.
 *
 * The description of this table uses non-integer values.  We multiply
 *	everything by 10 before it goes into the table, and the code
 *	will divide by 10 after it calculates the device power.
 */
int cistpl_pd_man[] = {
    10,		/* no units */
    12,		/* no units */
    13,		/* no units */
    15,		/* no units */
    20,		/* no units */
    25,		/* no units */
    30,		/* no units */
    35,		/* no units */
    40,		/* no units */
    45,		/* no units */
    50,		/* no units */
    55,		/* no units */
    60,		/* no units */
    70,		/* no units */
    80,		/* no units */
    90,		/* no units */
};

/*
 * The power description exponent table.
 *
 * This table is described in PCMCIA Release 2.01 Card Metaformat
 *	pg. 5-28 table 5-32.
 *
 * The description of this table uses various voltage and current units.
 *	This table contains all currents in nanoAMPS and all voltages
 *	in microVOLTS.
 *
 * Note if you're doing a current table lookup, you need to multiply
 *	the lookup value by ten.
 */
int cistpl_pd_exp[] = {
    10,		/* 10 microVOLTS, 100 nanoAMPS */
    100,	/* 100 microVOLTS, 1000 nanoAMPS */
    1000,	/* 1000 microVOLTS, 10000 nanoAMPS */
    10000,	/* 10000 microVOLTS, 100000 nanoAMPS */
    100000,	/* 100000 microVOLTS, 1000000 nanoAMPS */
    1000000,	/* 1000000 microVOLTS, 10000000 nanoAMPS */
    10000000,	/* 10000000 microVOLTS, 100000000 nanoAMPS */
    100000000	/* 100000000 microVOLTS, 1000000000 nanoAMPS */
};

/*
 * Fill out the structure pointers.
 */
cistpl_devspeed_struct_t cistpl_devspeed_struct = {
	cistpl_devspeed_table,
	cistpl_exspeed_tenfac,
	cistpl_devspeed_man,
	cistpl_devspeed_exp,
};

cistpl_pd_struct_t cistpl_pd_struct = {
	cistpl_pd_man,
	cistpl_pd_exp,
};

/*
 * Some handy lookup tables that should probably eventually be
 *	done away with.
 *
 * These are used mostly by the CISTPL_CFTABLE_ENTRY tuple handler.
 */
int cistpl_cftable_io_size_table[] = {
	0,
	1,
	2,
	4,
};

int cistpl_cftable_shift_table[] = {
	0,
	8,
	16,
	24,
};
