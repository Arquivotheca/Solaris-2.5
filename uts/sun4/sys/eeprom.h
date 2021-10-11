/*
 * Copyright (c) 1985, 1990 by Sun Microsystems, Inc.
 */

#ifndef _SYS_EEPROM_H
#define	_SYS_EEPROM_H

#pragma ident	"@(#)eeprom.h	1.15	94/05/09 SMI"
/* From SunOS 4.1.1 sun4/eeprom.h 1.4 */

#include <sys/mmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The EEPROM consists of one 2816 type EEPROM providing 2K bytes
 * of electically erasable storage.  To modify the EEPROM, each
 * byte must be written separately.  After writing each byte
 * a 10 millisecond pause must be observed before the EEPROM can
 * read or written again.  The majority of the EEPROM is diagnostic
 * and is defined in ../mon/eeprom.h.  The software specific
 * information (struct ee_soft) is defined here.  This structure
 * is 0x100 bytes big.
 */
#ifndef _ASM
#include <sys/t_lock.h>
#include <sys/clock.h>

struct ee_soft {
	u_short	ees_wrcnt[3];		/* write count (3 copies) */
	u_short	ees_nu1;		/* not used */
	u_char	ees_chksum[3];		/* software area checksum (3 copies) */
	u_char	ees_nu2;		/* not used */
	/*
	 * We reduce the size of ees_resv by sizeof (struct mostek48TO2)
	 * so that we don't read that area looking for consistent data.
	 * The clock is changing it every second.  This was giving the
	 * eeeprom utility problems because it was expecting static data
	 * there when checksumming.
	 */
	u_char	ees_resv[0x100 - 0xc - sizeof (struct mostek48T02)];
};

#define	EE_SOFT_DEFINED		/* tells ../mon/eeprom.h to use this ee_soft */

#include <sys/eeprom_com.h>
#endif /* !_ASM */

#define	OBIO_EEPROM_ADDR 0xF2000000	/* address of eeprom in obio space */

#define	EEPROM_ADDR	(MDEVBASE + 0x2000)	/* virtual address we map to */
#define	EEPROM_SIZE	0x800		/* size of eeprom in bytes */
#define	EEPROM		((struct eeprom *)EEPROM_ADDR)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_EEPROM_H */
