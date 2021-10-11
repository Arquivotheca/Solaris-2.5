/*
 * dynlib.h
 * Function prototypes for dynlib.c
 */

#ident	"@(#)dynlib.h	1.1	94/11/10 SMI"

#ifndef DYNLIB_H
#define	DYNLIB_H

extern	void	clear_names(void);
extern	void	load_lib_name(const char * name);
extern	void	load_lib_dir(const char * dir);
extern	void	load_ldd_names(int pfd);
extern	void	load_exec_name(const char * name);
extern	void	make_exec_name(const char * name);
extern	char *	lookup_raw_file(dev_t dev, ino_t ino);
extern	char *	lookup_file(dev_t dev, ino_t ino);
extern	char *	index_name(int index);

#endif	/* DYNLIB_H */
