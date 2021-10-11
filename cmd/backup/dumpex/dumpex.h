/*	@(#)dumpex.h 1.1 91/12/20	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

/*
 * Function declarations used by dumpex
 */
#ifdef __STDC__
extern void unlinklpfile(void);
extern void fixtape(char *, char *);
extern void incrmastercycle(void);
extern void markdone(char *, int, int *);
extern void markfail(int, char *);
extern void markundone(struct devcycle_f *);
extern void outputfile(char *);
extern void dodump(void);
extern void display_init(void);
extern void display(char *);
#else
extern void unlinklpfile();
extern void fixtape();
extern void incrmastercycle();
extern void markdone();
extern void markfail();
extern void markundone();
extern void outputfile();
extern void dodump();
extern void display_init();
extern void display();
#endif
