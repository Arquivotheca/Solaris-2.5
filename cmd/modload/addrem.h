#pragma ident   "@(#)addrem.h 1.6     95/01/12 SMI"

/*
 *   Copyright (c) 1993 by Sun Microsystems, Inc.
 */

/* defines for adddrv.c and remdrv.c */

#define	SUCCESS	0
#define	FAILURE -1
#define	NOERR	0
#define	ERROR	-1
#define	UNIQUE	0
#define	NOT_UNIQUE -1

#define	MAX_CMD_LINE	256
#define	MAX_N2M_ALIAS_LINE	FILENAME_MAX + FILENAME_MAX + 1
#define	MAXLEN_NAM_TO_MAJ_ENT 	FILENAME_MAX + MAX_STR_MAJOR + 1
#define	OPT_LEN		128
#define	CADDR_HEX_STR	16
#define	UINT_STR	10
#define	MODLINE_ENT_MAX	(4 * UINT_STR) + CADDR_HEX_STR + MODMAXNAMELEN
#define	MAX_STR_MAJOR	UINT_STR
#define	STR_LONG	10
#define	PERM_STR	4
#define	MAX_PERM_ENTRY	(2 * STR_LONG) + PERM_STR + (2 * FILENAME_MAX) + 1
#define	MAX_DBFILE_ENTRY	MAX_PERM_ENTRY

#define	CLEAN_MINOR_PERM	0x00000001
#define	CLEAN_DRV_ALIAS		0x00000002
#define	CLEAN_NAM_MAJ		0x00000004
#define	CLEAN_DRV_CLASSES	0x00000010
#define	CLEAN_ALL		(CLEAN_MINOR_PERM | CLEAN_DRV_ALIAS | \
				CLEAN_NAM_MAJ | CLEAN_DRV_CLASSES)

/* add_drv/rem_drv database files */
#define	DRIVER_ALIAS	"/etc/driver_aliases"
#define	DRIVER_CLASSES	"/etc/driver_classes"
#define	MINOR_PERM	"/etc/minor_perm"
#define	NAM_TO_MAJ	"/etc/name_to_major"
#define	REM_NAM_TO_MAJ	"/etc/rem_name_to_major"

#define	ADD_REM_LOCK	"/tmp/AdDrEm.lck"
#define	TMPHOLD		"/etc/TmPhOlD"

/* pointers to add_drv/rem_drv database files */
char *driver_aliases;
char *driver_classes;
char *minor_perm;
char *name_to_major;
char *rem_name_to_major;
char *add_rem_lock;
char *tmphold;

/* devfs root string */
char *devfs_root;

/* names of things: directories, commands, files */
#define	KERNEL_DRV	"/kernel/drv"
#define	USR_KERNEL_DRV	"/usr/kernel/drv"
#define	DRVCONFIG_PATH	"/usr/sbin/drvconfig"
#define	DRVCONFIG	"drvconfig"
#define	DEVFS_ROOT	"/devices"

#define	RECONFIGURE	"/reconfigure"
#define	DEVLINKS_PATH	"/usr/sbin/devlinks"
#define	DISKS_PATH	"/usr/sbin/disks"
#define	PORTS_PATH	"/usr/sbin/ports"
#define	TAPES_PATH	"/usr/sbin/tapes"
#define	MODUNLOAD_PATH	"/usr/sbin/modunload"

void remove_entry(int, char *);
char *get_next_entry(char *, char *);
char *get_perm_entry(char *, char *);
int some_checking(int, int);
void err_exit();
void exit_unlock();
char *get_entry(char *, char *, char);
int build_filenames(char *);
int append_to_file(char *, char *, char *, char, char *);
int get_file_entry(char *, char *, char *, int *);
int delete_entry(char *oldfile, char *driver_name, char *marker);


/* modctl() not defined */
extern int modctl(int, ...);

/* drvsubr.c */
#define	XEND	".XXXXXX"

/*
 * XXX
 * define for maximum length of modules paths - we need
 * a common symbol with kbi folks for this
 */
#define	MAXMODPATHS 1024

/* module path list separators */
#define	MOD_SEP	" :"
