#ident	"@(#)tapelib.h 1.3 93/10/13"

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ifdef __STDC__
extern void tl_open(char *, char *);
extern void tl_close(void);
extern int tl_add(void);
extern void tl_read(int, struct tapedesc_f *);
extern void tl_write(int, struct tapedesc_f *);
extern void tl_update(int, int, int);
extern void tl_reserve(int, int);
extern void tl_setstatus(int, int);
extern void tl_setdate(int, int);
extern void tl_error(int);
extern void tl_markstatus(int, int);
extern int tl_findfree(int, int);
extern void tl_unlock(void);
extern void tl_lock(void);
#else
extern void tl_open();
extern void tl_close();
extern int tl_add();
extern void tl_read();
extern void tl_write();
extern void tl_update();
extern void tl_reserve();
extern void tl_setstatus();
extern void tl_setdate();
extern void tl_error();
extern void tl_markstatus();
extern int tl_findfree();
extern void tl_unlock();
extern void tl_lock();
#endif
