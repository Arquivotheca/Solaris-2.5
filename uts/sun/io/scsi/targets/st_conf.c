/*
 * Copyright (c) 1989 - 1991 by Sun Microsystems, Inc.
 */

#pragma ident  "@(#)st_conf.c 1.37     95/07/06 SMI"

#include <sys/scsi/scsi.h>
#include <sys/mtio.h>
#include <sys/scsi/targets/stdef.h>

/*
 * Drive Tables.
 *
 * The structure and option definitions can be
 * found in <scsi/targets/stdef.h>.
 *
 * Note: that blocksize should be a power of two
 * for fixed-length recording devices.
 *
 * Note: the speed codes are unused at present.
 * The driver turns around whatever is reported
 * from the drive via the mode sense.
 *
 * Note: the read retry and write retry counts
 * are there to provide a limit until warning
 * messages are printed.
 *
 *
 * Note: For drives that are not in this table....
 *
 * The first open of the device will cause a MODE SENSE command
 * to be sent. From that we can determine block size. If block
 * size is zero, than this drive is in variable-record length
 * mode. The driver uses the SCSI-2 specification density codes in
 * order to attempt to determine what kind of sequential access
 * device this is. This will allow determination of 1/4" cartridge,
 * 1/2" cartridge, some helical scan (3.81 && 8 mm cartridge) and
 * 1/2" reel tape devices. The driver will print what it finds and is
 * assuming to the console. If the device you have hooked up returns
 * the default density code (0) after power up, the drive cannot
 * determine what kind of drive it might be, so it will assume that
 * it is an unknown 1/4" cartridge tape (QIC).
 *
 * If the drive is determined in this way to be a 1/2" 9-track reel
 * type device, an attempt will be mode to put it in Variable
 * record length mode.
 *
 * Generic drives are assumed to support only the long erase option
 * and will not to be run in buffered mode.
 *
 */

struct st_drivetype st_drivetypes[] = {

/* Emulex MT-02 controller for 1/4" cartridge */
/*
 * The EMULEX MT-02 adheres to CCS level 0, and thus
 * returns nothing of interest for the Inquiry command
 * past the 'response data format' field (which will be
 * zero). The driver will recognize this and assume that
 * a drive that so responds is actually an MT-02 (there
 * is no other way to really do this, ungracious as it
 * may seem).
 *
 */
{
	"Emulex MT02 QIC-11/QIC-24",
	8, "\0\0\0\0\0\0\0\0", ST_TYPE_EMULEX, 512,
	ST_QIC | ST_KNOWS_EOD,
	130, 130,
	/*
	 * Note that low density is a vendor unique density code.
	 * This gives us 9 Track QIC-11. Supposedly the MT02 can
	 * read 4 Track QIC-11 while in this mode. If that doesn't
	 * work, change one of the duplicated QIC-24 fields to 0x4.
	 *
	 */
	{ 0x84, 0x05, 0x05, 0x05 }, MT_DENSITY2,
	{ 0, 0, 0, 0 }
},
/* Archive QIC-150 1/4" cartridge */
{
	"Archive QIC-150", 13, "ARCHIVE VIPER", ST_TYPE_ARCHIVE, 512,
	/*
	 * The manual for the Archive drive claims that this drive
	 * can backspace filemarks. In practice this doens't always
	 * seem to be the case.
	 *
	 */
	(ST_QIC | ST_KNOWS_EOD | ST_AUTODEN_OVERRIDE | ST_UNLOADABLE),
	400, 400,
	{ 0x00, 0x00, 0x00, 0x00}, MT_DENSITY2,
	{  0, 0, 0, 0 }
},

/* Tandberg 2.5 Gig QIC */
{
	"Tandberg 2.5 Gig QIC", 17, "TANDBERG TDC 4200", ST_TYPE_TAND25G, 512,
	(ST_QIC | ST_BSF | ST_BSR | ST_LONG_ERASE |
	ST_AUTODEN_OVERRIDE | ST_KNOWS_EOD | ST_UNLOADABLE |
	ST_NO_RECSIZE_LIMIT),
	400, 400,
	{ 0x00, 0x00, 0x00, 0x00}, MT_DENSITY2,
	{  0, 0, 0, 0 }
},

/* HP 1/2" reel */
{
	"HP-88780 1/2\" Reel", 13, "HP      88780", ST_TYPE_HP, 0,
	(ST_REEL | ST_VARIABLE | ST_BSF | ST_BSR | ST_UNLOADABLE),
	400, 400,
	/*
	 * Note the vendor unique density '0xC3':
	 * this is compressed 6250 mode. Beware
	 * that using large data sets consisting
	 * of repeated data compresses *too* well
	 * and one can run into the unix 2 gb file
	 * offset limit this way.
	 */
	{ 0x01, 0x02, 0x03, 0xC3}, MT_DENSITY2,
	{  0, 0, 0, 0 }
},
/*
 * Exabyte 8mm 5GB cartridge
 * ST_KNOWS_EOD here will cause medium error messages
 * the string length (16) has been reduced to 15 to allow for other
 * compatible models (eg half_height)  (1091196)
 */
{
	"Exabyte EXB-8500 8mm Helical Scan", 15, "EXABYTE EXB-8500",
	ST_TYPE_EXB8500, 0,
	(ST_VARIABLE | ST_BSF | ST_BSR | ST_LONG_ERASE | ST_UNLOADABLE |
	ST_KNOWS_EOD | ST_SOFT_ERROR_REPORTING | ST_BUFFERED_WRITES |
	ST_NO_RECSIZE_LIMIT),
	5000, 5000,
	{ 0x14, 0x15, 0x8C, 0x8C }, MT_DENSITY2,
	{  0, 0, 0, 0 }
},
/* Exabyte 8mm 2GB cartridge */
{
	"Exabyte EXB-8200 8mm Helical Scan", 16, "EXABYTE EXB-8200",
	ST_TYPE_EXABYTE, 0,
	(ST_VARIABLE | ST_BSF | ST_BSR | ST_LONG_ERASE | ST_AUTODEN_OVERRIDE |
	ST_UNLOADABLE | ST_SOFT_ERROR_REPORTING | ST_NO_RECSIZE_LIMIT),
	5000, 5000,
	{ 0x00, 0x00, 0x00, 0x00 }, MT_DENSITY2,
	{  0, 0, 0, 0 }
},
/* Archive Python 4mm 2GB drive */
{
	"Archive Python 4mm Helical Scan", 14, "ARCHIVE Python",
	ST_TYPE_PYTHON, 0,
	(ST_VARIABLE | ST_BSF | ST_BSR | ST_LONG_ERASE |
	ST_KNOWS_EOD | ST_UNLOADABLE | ST_LONG_TIMEOUTS |
	ST_SOFT_ERROR_REPORTING | ST_BUFFERED_WRITES |
	ST_NO_RECSIZE_LIMIT),
	5000, 5000,
	{ 0x00, 0x8C, 0x8C, 0x8C }, MT_DENSITY4,
	{  0, 0, 0, 0 }
},
/* STC 3490 1/2" cartridge */
{
	"STK 1/2\" Cartridge", 3, "STK", ST_TYPE_STC3490, 0,
	(ST_REEL | ST_VARIABLE | ST_BSF | ST_BSR | ST_UNLOADABLE |
	ST_BUFFERED_WRITES | ST_NO_RECSIZE_LIMIT | ST_MODE_SEL_COMP |
	ST_LONG_ERASE),
	400, 400,
	{ 0x00, 0x00, 0x00, 0x00}, MT_DENSITY1,
	{ 0, 0, 0, 0 }
},


/*
 * The drives below have not been qualified, and are
 * not supported by Sun Microsystems. However, many
 * customers have stated a strong desire for them,
 * so our best guess as to their capabilities is
 * included herein.
 */
/* Wangtek QIC-150 1/4" cartridge */ {
	"Wangtek QIC-150", 14, "WANGTEK 5150ES", ST_TYPE_WANGTEK, 512,
	(ST_QIC | ST_KNOWS_EOD | ST_AUTODEN_OVERRIDE | ST_UNLOADABLE),
	400, 400,
	{ 0x00, 0x00, 0x00, 0x00}, MT_DENSITY2,
	{  0, 0, 0, 0 }
},
/* Exabyte DC-2000 cartridge */
{
	"Exabyte EXB-2501 QIC", 14, "EXABYTE EXB-2501",
	ST_TYPE_EXABYTE, 1024,
	(ST_QIC | ST_AUTODEN_OVERRIDE | ST_UNLOADABLE),
	400, 400,
	{ 0x00, 0x00, 0x00, 0x00 }, MT_DENSITY2,
	{  0, 0, 0, 0 }
},
/* Kennedy 1/2" reel */
{
	"Kennedy 1/2\" Reel", 4, "KENNEDY", ST_TYPE_KENNEDY, 0,
	(ST_REEL | ST_VARIABLE | ST_BSF | ST_BSR | ST_UNLOADABLE),
	400, 400,
	{ 0x01, 0x02, 0x03, 0x03 }, MT_DENSITY2,
	{ 0, 0, 0, 0 }
},
/* Anritsu 1/2" reel */
{
	"Unisys 1/2\" Reel", 15, "ANRITSU DMT2120", ST_TYPE_ANRITSU, 0,
	(ST_REEL | ST_VARIABLE | ST_BSF | ST_BSR | ST_UNLOADABLE),
	400, 400,
	{ 0x00, 0x02, 0x03, 0x03 }, MT_DENSITY2,
	{0, 0, 0, 0 }
},

/* CDC 1/2" cartridge */
{
	"CDC 1/2\" Cartridge", 3, "LMS", ST_TYPE_CDC, 0,
	(ST_QIC | ST_KNOWS_EOD | ST_VARIABLE | ST_BSF | ST_LONG_ERASE |
	ST_AUTODEN_OVERRIDE | ST_UNLOADABLE),
	300, 300,
	{ 0x00, 0x00, 0x00, 0x00}, MT_DENSITY2,
	{ 0, 0, 0, 0 }
},
/* Fujitsu 1/2" cartridge */
{
	"Fujitsu 1/2\" Cartridge", 2, "\076\000", ST_TYPE_FUJI, 0,
	(ST_QIC | ST_KNOWS_EOD | ST_VARIABLE | ST_BSF | ST_BSR | ST_LONG_ERASE |
	ST_UNLOADABLE),
	300, 300,
	{ 0x00, 0x00, 0x00, 0x00}, MT_DENSITY2,
	{ 0, 0, 0, 0 }
},
/* M4 Data Systems 9303 transport with 9700 512k i/f */
{
	"M4-Data 1/2\" Reel", 19, "M4 DATA 123107 SCSI", ST_TYPE_REEL, 0,
	/*
	 * This is in non-buffered mode because it doesn't flush the
	 * buffer at end of tape writes. If you don't care about end
	 * of tape conditions (e.g., you use dump(8) which cannot
	 * handle end-of-tape anyhow), take out the ST_NOBUF.
	 */
	(ST_REEL | ST_VARIABLE | ST_BSF | ST_BSR | ST_NOBUF | ST_UNLOADABLE),
	500, 500,
	{ 0x01, 0x02, 0x06, 0x06}, MT_DENSITY2,
	{ 0, 0, 0, 0 }
},
/*  Wangtek 4mm RDAT drive */
{
	"Wangtek 4mm Helical Scan", 14, "WANGTEK 6130-HS", ST_TYPE_WANGTHS,
	0,
	(ST_VARIABLE | ST_BSF | ST_BSR | ST_KNOWS_EOD | ST_AUTODEN_OVERRIDE |
	ST_UNLOADABLE),
	400, 400,
	{ 0x00, 0x00, 0x00, 0x00}, MT_DENSITY2,
	{ 0, 0, 0, 0 }
},
/* WangDAT 3.81mm cartridge */
{
	"Wang DAT 3.81 Helical Scan", 7, "WangDAT", ST_TYPE_WANGDAT, 0,
	(ST_VARIABLE | ST_BSF | ST_BSR | ST_KNOWS_EOD | ST_AUTODEN_OVERRIDE |
	ST_UNLOADABLE),
	5000, 5000,
	{ 0x00, 0x00, 0x00, 0x00 }, MT_DENSITY2,
	{  0, 0, 0, 0 }
}
};
int st_ndrivetypes = (sizeof (st_drivetypes)/sizeof (st_drivetypes[0]));
