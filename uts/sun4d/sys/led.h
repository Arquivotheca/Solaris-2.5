/*
 * Copyright (c) 1988 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_LED_H
#define	_SYS_LED_H

#pragma ident	"@(#)led.h	1.6	95/01/25 SMI"	/* SunOS-4.1 1.9 */

#ifdef	__cplusplus
extern "C" {
#endif

extern void led_init(void);
extern void led_blink_all(void);
extern void led_set_cpu(u_char cpu_id, u_char pattern);
extern u_char led_get_ecsr(u_int cpu_id);
extern void led_set_ecsr(u_int cpu_id, u_char pattern);

#define	LED_CPU_RESUME	0x0

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_LED_H */
