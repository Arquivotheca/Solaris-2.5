/*
 * Copyright (c) 1991-1993, by Sun Microsystems, Inc.
 */

#ifndef	_SYS_REBOOT_H
#define	_SYS_REBOOT_H

#pragma ident	"@(#)reboot.h	2.25	94/12/18 SMI"	/* SunOS 4.0 2.11 */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Boot flags and flags to "reboot" system call.
 *
 * Not all of these necessarily apply to all machines.
 */
#define	RB_AUTOBOOT	0	/* flags for system auto-booting itself */

#define	RB_ASKNAME	0x001	/* ask for file name to reboot from */
#define	RB_SINGLE	0x002	/* reboot to single user only */
#define	RB_NOSYNC	0x004	/* dont sync before reboot */
#define	RB_HALT		0x008	/* don't reboot, just halt */
#define	RB_INITNAME	0x010	/* name given for /etc/init */
#define	RB_NOBOOTRC	0x020	/* don't run /etc/rc.boot */
#define	RB_DEBUG	0x040	/* being run under debugger */
#define	RB_DUMP		0x080	/* dump system core */
#define	RB_WRITABLE	0x100	/* mount root read/write */
#define	RB_STRING	0x200	/* pass boot args to prom monitor */
#define	RB_GDB		0x400	/* set up for GDB remote debugging */
#define	RB_CONFIG	0x800	/* pass to init on a boot -c */
#define	RB_RECONFIG	0x1000	/* pass to init on a boot -r */
#define	RB_VERBOSE	0x2000	/* unless set, booting is very quiet .. */
#define	RB_KDBX		0x4000	/* set up for kdbx remote debugging */
#define	RB_FLUSHCACHE	0x10000	/* flush root cache */

#if defined(_KERNEL)

#if defined(_BOOT) || defined(_KADB)
extern int bootflags(char *);
#else
extern int boothowto;
extern void bootflags(void);
#endif /* _BOOT || _KADB */

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_REBOOT_H */
