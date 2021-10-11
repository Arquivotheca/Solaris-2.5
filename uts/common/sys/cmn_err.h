/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ifndef _SYS_CMN_ERR_H
#define	_SYS_CMN_ERR_H

#pragma ident	"@(#)cmn_err.h	1.21	94/07/06 SMI"	/* SVr4.0 11.8	*/

#ifdef	__cplusplus
extern "C" {
#endif

/* Common error handling severity levels */

#define	CE_CONT  0	/* continuation				*/
#define	CE_NOTE  1	/* notice				*/
#define	CE_WARN	 2	/* warning				*/
#define	CE_PANIC 3	/* panic				*/

/*	Codes for where output should go.			*/

#define	PRW_BUF		0x01	/* Output to putbuf.		*/
#define	PRW_CONS	0x02	/* Output to console.		*/
#define	PRW_STRING	0x04	/* Output to string for sprintf. */

#define	VA_LIST void *
#define	VA_START(list, name) list = \
	(void *)((char *)&name + ((sizeof (name) + (sizeof (int) - 1)) & \
	    ~(sizeof (int) - 1)))
#define	VA_ARG(list, mode) ((mode *) \
	(list = (void *)((char *)list+sizeof (mode))))[-1]

#if defined(_KERNEL)

#include <sys/varargs.h>

/*PRINTFLIKE2*/
extern void cmn_err(int, char *, ...);
extern void vcmn_err(int, char *, va_list);
/*PRINTFLIKE1*/
extern void printf(char *, ...);
extern void vprintf(char *, va_list);
/*PRINTFLIKE1*/
extern void uprintf(char *, ...);
/*PRINTFLIKE2*/
extern char *sprintf(char *, const char *, ...);
extern char *vsprintf(char *, const char *, va_list);
extern char *vsprintf_len(int, char *, const char *, va_list);
/*PRINTFLIKE3*/
extern char *sprintf_len(int len, char *buf, const char *fmt, ...);
extern char *vsprintf_len(int len, char *buf, const char *fmt, va_list ap);
/*PRINTFLIKE1*/
extern void panic(char *, ...);
extern void panic_hook(void);
void do_panic(char *, va_list);
extern void cnputs(char *, u_int, int);
extern void cnputc(int, int);
extern void gets(char *);

struct vnode;
extern void prf(char *, va_list, struct vnode *, int);

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CMN_ERR_H */
