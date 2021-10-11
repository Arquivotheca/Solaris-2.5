/*
 * Copyright (c) 1994, by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)bootvfs.h	1.1	94/08/01 SMI"

/* same as those in /usr/include/unistd.h */
#define	SEEK_SET	0	/* Offset */
#define	SEEK_CUR	1	/* Current + Offset */
#define	SEEK_END	2	/* EOF + Offset */

/*
 * unified (vfs-like) file system operations for booters
 */

struct boot_fs_ops {
    char	*fsw_name;
    int		(*fsw_mountroot)(char *str);
    int		(*fsw_open)(char *filename, int flags);
    int		(*fsw_close)(int fd);
    int		(*fsw_read)(int fd, caddr_t buf, int size);
    off_t	(*fsw_lseek)(int filefd, off_t addr, int whence);
    int		(*fsw_fstat)(int filefd, struct stat *buf);
    void	(*fsw_closeall)(int flag);
};

/*
 *  Function prototypes
 *
 *	fstat() (if exists) supports size and mode right now.
 */

extern	int	mountroot(char *str);
extern	int	open(char *filename, int flags);
extern	int	close(int fd);
extern	int	read(int fd, caddr_t buf, int size);
extern	off_t	lseek(int filefd, off_t addr, int whence);
extern	int	fstat(int fd, struct stat *buf);
extern	void	closeall(int flag);

extern	int	kern_read(int fd, caddr_t buf, u_int size);
extern	int	kern_open(char *filename, int flags);
extern	off_t	kern_seek(int fd, off_t hi, off_t lo);
extern	int	kern_close(int fd);
extern	int	kern_fstat(int fd, struct stat *buf);

/*
 * there are common cache interface routines
 */
extern	caddr_t get_db_cache(int block, int size);
extern	ino_t	get_dcache(char *name, int len, ino_t inode_num);
extern	void	set_dcache(char *path, int len, ino_t pin, ino_t in);
extern	void	release_cache(void);
extern	void	print_cache_data(void);

/*
 * these are for common fs switch interface routines
 */
extern	int	boot_no_ops();		/* no ops entry */
extern	void	boot_no_ops_void();	/* no ops entry */

extern	struct boot_fs_ops *get_fs_ops_pointer(char *fsw_name);
extern	void	set_default_fs(char *fsw_name);
extern	char 	*set_fstype(char *v2path);

extern	struct boot_fs_ops *boot_fsw[];
extern	int	boot_nfsw;		/* number of entries in fsw[] */
