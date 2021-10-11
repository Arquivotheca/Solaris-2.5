/*	@(#)rmt.h 1.0 90/12/08 SMI	*/

/*	@(#)rmt.h 1.3 93/05/13	*/

/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */
#include <sys/mtio.h>

#ifdef __STDC__
extern void rmtinit(void (*)(const char *, ...), void (*)(int));
extern int rmthost(char *, int);
extern int rmtopen(char *, int);
extern void rmtclose(void);
extern int rmtstatus(struct mtget *);
extern int rmtread(char *, int);
extern int rmtwrite(char *, int);
extern int rmtseek(int, int);
extern int rmtioctl(int, int);
#else
extern void rmtinit();
extern int rmthost();
extern int rmtopen();
extern void rmtclose();
extern int rmtstatus();
extern int rmtread();
extern int rmtwrite();
extern int rmtseek();
extern int rmtioctl();
#endif
