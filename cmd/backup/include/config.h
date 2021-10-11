/*	@(#)config.h 1.5 91/07/19 SMI	*/

/*	@(#)config.h 1.10 93/04/28	*/

/*
 * Copyright (c) 1990,1991,1992 by Sun Microsystems, Inc.
 */

#ifndef _CONFIG_H__
#define	_CONFIG_H__

#include <syslog.h>
#include <sys/types.h>

#define	NMSGLEVELS	LOG_PRIMASK+1	/* number of notification levels */
#define	CONFIGFILE	"dump.conf"
#define	TMPDIR		"/var/tmp"
#define	MAXMAILLIST	1024
#define	BCHOSTNAMELEN	64

enum active_action {
	error = -1,
	donothing = 0,
	retry = 1,
	report = 2,
	reportandretry = 3
};
typedef enum active_action action_t;

enum ldev_type {
	none = 0,
	sequence = 1
};
typedef enum ldev_type dtype_t;

enum dirpath {
	rootdir = 0,
	bindir = 1,
	sbindir = 2,
	libdir = 3,
	localedir = 4,
	etcdir = 5,
	admdir = 6
};
typedef enum dirpath dirpath_t;

#ifdef __STDC__
extern int readconfig(char *, void (*)(const char *, ...));
extern void printconfig(void);
extern char *getstring(char *, char **);
extern char *gettmpdir(void);
extern int setopserver(const char *);
extern int getopserver(char *, int);
extern int setdbserver(const char *);
extern int getdbserver(char *, int);
extern int makedevice(char *, char *, dtype_t);
extern int setdevice(char *);
extern char *getdevice(void);
extern void getdevinfo(dtype_t *, int *, int *);
extern void setfsname(const char *);
extern char *getfslocktype(void);
extern int getfsonline(void);
extern int getfsreset(void);
extern action_t getfsaction(size_t, int);
extern int setmail(const char *);
extern int getmail(char *, int);
extern void sethsmpath(char *);
extern char *gethsmpath(dirpath_t);
#else
extern int readconfig();
extern void printconfig();
extern char *getstring();
extern char *gettmpdir();
extern int setopserver();
extern int getopserver();
extern int setdbserver();
extern int getdbserver();
extern int makedevice();
extern int setdevice();
extern char *getdevice();
extern void getdevinfo();
extern void setfsname();
extern char *getfslocktype();
extern int getfonline();
extern int getfsreset();
extern action_t getfsaction();
extern int setmail();
extern int getmail();
extern void sethsmpath();
extern char *gethsmpath();
#endif
#endif
