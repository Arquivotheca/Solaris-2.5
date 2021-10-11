/*
 * Copyright (c) 1987-1990 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_CONSDEV_H
#define	_SYS_CONSDEV_H

#pragma ident	"@(#)consdev.h	5.18	93/08/27 SMI"	/* from SunOS-4.0 5.7 */

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef	_KERNEL

/*
 * Used to determine which device is the intended console on PCs
 */
#define	CONSOLE_NOT_SET	-1
#define	CONSOLE_IS_KD	0
#define	CONSOLE_IS_ASY	1

/*
 * Console redirection.
 */
extern dev_t	rconsdev;	/* real (underlying) console */
extern struct vnode *rconsvp;	/* pointer to vnode for that device */

/*
 * Mouse, keyboard, and frame buffer configuration information.
 *
 * XXX:	Assumes a single mouse/keyboard/frame buffer triple.
 */
extern dev_t	mousedev;	/* default mouse device */
extern dev_t	kbddev;		/* default (actual) keyboard device */
extern dev_t	stdindev;	/* default standard input device */
extern dev_t	fbdev;		/* default framebuffer device */
extern struct vnode *fbvp;	/* pointer to vnode for that device */

/*
 * Workstation console redirection.
 *
 * The workstation console device is the multiplexor that hooks keyboard and
 * frame buffer together into a single tty-like device.  Access to it is
 * through the redirecting driver, so that frame buffer output can be
 * redirected to other devices.  wsconsvp names the redirecting access point,
 * and rwsconsvp names the workstation console itself.
 *
 * XXX:	Assumes a single workstation console.
 */
extern struct vnode *wsconsvp;	/* vnode for redirecting ws cons access */
extern struct vnode *rwsconsvp;	/* vnode for underlying workstation console */

extern int cn_conf;

#endif	/* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CONSDEV_H */
