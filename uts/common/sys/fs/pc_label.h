/*
 * Copyright (c) 1989 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_FS_PC_LABEL_H
#define	_SYS_FS_PC_LABEL_H

#pragma ident	"@(#)pc_label.h	1.9	93/09/29 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/isa_defs.h>

/*
 * PC master boot block & partition table defines.
 */

#define	PCB_BPSEC	11	/* (short) bytes per sector */
#define	PCB_SPC		13	/* (byte) sectors per cluster */
#define	PCB_RESSEC	14	/* (short) reserved sectors */
#define	PCB_NFAT	16	/* (byte) number of fats */
#define	PCB_NROOTENT	17	/* (short) number of root dir entries */
#define	PCB_NSEC	19	/* (short) number of sectors on disk */
#define	PCB_MEDIA	21	/* (byte) media descriptor */
#define	PCB_SPF		22	/* (short) sectors per fat */
#define	PCB_SPT		24	/* (short) sectors per track */
#define	PCB_NHEAD	26	/* (short) number of heads */
#define	PCB_HIDSEC	28	/* (short) number of hidden sectors */

#define	PCFS_PART	0x1be	/* partition table offs in blk 0 of unit */
#define	PCFS_NUMPART	4	/* Number of partitions in blk 0 of unit */

#define	PCFS_BPB	0xb	/* offset of the BPB in the boot block	*/
#define	PCFS_SIGN	0x1fe   /* offset of the DOS signature		*/
#define	DOS_SYSFAT12    1	/* DOS FAT 12 system indicator		*/
#define	DOS_SYSFAT16	4	/* DOS FAT 16 system indicator		*/
#define	DOS_SYSHUGE	6	/* DOS FAT 16 system indicator > 32MB	*/
#define	DOS_F12MAXS	20740	/* Max sector for 12 Bit FAT (DOS>=3.2)	*/
#define	DOS_F12MAXC	4086	/* Max cluster for 12 Bit FAT (DOS>=3.2) */

#define	DOS_ID1		0xe9	/* JMP intrasegment			*/
#define	DOS_ID2a	0xeb    /* JMP short				*/
#define	DOS_ID2b	0x90
#define	DOS_SIGN	0xaa55	/* DOS signature in boot and partition	*/

#define	PC_FATBLOCK	1	/* starting block number of fat */
/*
 * Media descriptor byte.
 * Found in the boot block and in the first byte of the FAT.
 * Second and third byte in the FAT must be 0xFF.
 * Note that all technical sources indicate that this means of
 * identification is extremely unreliable.
 */
#define	MD_FIXED	0xF8	/* fixed disk				*/
#define	SS8SPT		0xFE	/* single sided 8 sectors per track	*/
#define	DS8SPT		0xFF	/* double sided 8 sectors per track	*/
#define	SS9SPT		0xFC	/* single sided 9 sectors per track	*/
#define	DS9SPT		0xFD	/* double sided 9 sectors per track	*/
#define	DS18SPT		0xF0	/* double sided 18 sectors per track	*/
#define	DS9_15SPT	0xF9	/* double sided 9/15 sectors per track	*/

#define	PC_SECSIZE	512	/* pc filesystem sector size */

/*
 * conversions to/from little endian format
 */
#ifdef _LITTLE_ENDIAN
/* i386 machines */
#define	ltohs(S)	(*((u_short *)(&(S))))
#define	ltohl(L)	(*((u_long *)(&(L))))
#define	htols(S)	(*((u_short *)(&(S))))
#define	htoll(L)	(*((u_long *)(&(L))))

#else
#ifdef _BIG_ENDIAN
/* sparc machines */
#define	getbyte(A, N)	(((unsigned char *)(&(A)))[N])
#define	ltohs(S)	((getbyte(S, 1) << 8) | getbyte(S, 0))
#define	ltohl(L)	((getbyte(L, 3) << 24) | (getbyte(L, 2) << 16) | \
			    (getbyte(L, 1) << 8) | getbyte(L, 0))
#define	htols(S)	((getbyte(S, 1) << 8) | getbyte(S, 0))
#define	htoll(L)	((getbyte(L, 3) << 24) | (getbyte(L, 2) << 16) | \
			    (getbyte(L, 1) << 8) | getbyte(L, 0))
#endif
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_FS_PC_LABEL_H */
