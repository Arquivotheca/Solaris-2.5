/*
 * Copyright (c) 1991-1993, Sun Microsystems, Inc.
 */

#ifndef	_SYS_BOOTCONF_H
#define	_SYS_BOOTCONF_H

#pragma ident	"@(#)bootconf.h	1.26	94/12/15 SMI" /* SunOS-4.0 1.7 */

/*
 * Boot time configuration information objects
 */

#include <sys/types.h>
#include <sys/memlist.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * masks to hand to bsys_alloc memory allocator
 * XXX	These names shouldn't really be srmmu derived.
 */
#define	BO_NO_ALIGN	0x00001000
#define	BO_ALIGN_L3	0x00001000
#define	BO_ALIGN_L2	0x00040000
#define	BO_ALIGN_L1	0x01000000

/*
 *  We pass a ptr to the space that boot has been using
 *  for its memory lists.
 */
struct bsys_mem {
	struct memlist *physinstalled;	/* amt of physmem installed */
	struct memlist *physavail;	/* amt of physmem avail for use */
	struct memlist *virtavail;	/* amt of virtmem avail for use */
	u_int		extent; 	/* number of bytes in the space */
};

#define	BO_VERSION	4		/* bootops interface revision # */

struct bootops {
	/*
	 * the ubiquitous version number
	 */
	u_int	bsys_version;

	/*
	 * pointer to our parents bootops
	 */
	struct bootops	*bsys_super;

	/*
	 * the area containing boot's memlists
	 */
	struct 	bsys_mem *boot_mem;

	/*
	 * open a file
	 */
	int	(*bsys_open)(struct bootops *, char *s, int flags);

	/*
	 * read from a file
	 */
	int	(*bsys_read)(struct bootops *, int fd, caddr_t buf,
	    u_int size);

	/*
	 * seek (hi<<32) + lo bytes into a file
	 */
	int	(*bsys_seek)(struct bootops *, int fd, off_t hi, off_t lo);

	/*
	 * for completeness..
	 */
	int	(*bsys_close)(struct bootops *, int fd);

	/*
	 * have boot allocate size bytes at virthint
	 */
	caddr_t	(*bsys_alloc)(struct bootops *, caddr_t virthint, u_int size,
		int align);

	/*
	 * free size bytes allocated at virt - put the
	 * address range back onto the avail lists.
	 */
	void	(*bsys_free)(struct bootops *, caddr_t virt, u_int size);

	/*
	 * associate a physical mapping with the given vaddr
	 */
	caddr_t	(*bsys_map)(struct bootops *, caddr_t virt, int space,
	    caddr_t phys, u_int size);

	/*
	 * disassociate the mapping with the given vaddr
	 */
	void	(*bsys_unmap)(struct bootops *, caddr_t virt, u_int size);

	/*
	 * boot should quiesce its io resources
	 */
	void	(*bsys_quiesce_io)(struct bootops *);

	/*
	 * to find the size of the buffer to allocate
	 */
	int	(*bsys_getproplen)(struct bootops *, char *name);

	/*
	 * get the value associated with this name
	 */
	int	(*bsys_getprop)(struct bootops *, char *name, void *value);

	/*
	 * get the name of the next property in succession
	 * from the standalone
	 */
	char	*(*bsys_nextprop)(struct bootops *, char *prevprop);

	/*
	 * print formatted output - PRINTFLIKE1
	 */
	void	(*bsys_printf)(struct bootops *, char *, ...);
};

#define	BOP_GETVERSION(bop)		((bop)->bsys_version)
#define	BOP_OPEN(bop, s, flags)		((bop)->bsys_open)(bop, s, flags)
#define	BOP_READ(bop, fd, buf, size)	((bop)->bsys_read)(bop, fd, buf, size)
#define	BOP_SEEK(bop, fd, hi, lo)	((bop)->bsys_seek)(bop, fd, hi, lo)
#define	BOP_CLOSE(bop, fd)		((bop)->bsys_close)(bop, fd)
#define	BOP_ALLOC(bop, virthint, size, align)	\
				((bop)->bsys_alloc)(bop, virthint, size, align)
#define	BOP_FREE(bop, virt, size)	((bop)->bsys_free)(bop, virt, size)
#define	BOP_MAP(bop, virt, space, phys, size)	\
				((bop)->bsys_map)(bop, virt, space, phys, size)
#define	BOP_UNMAP(bop, virt, size)	((bop)->bsys_unmap)(bop, virt, size)
#define	BOP_QUIESCE_IO(bop)		((bop)->bsys_quiesce_io)(bop)
#define	BOP_GETPROPLEN(bop, name)	((bop)->bsys_getproplen)(bop, name)
#define	BOP_GETPROP(bop, name, buf)	((bop)->bsys_getprop)(bop, name, buf)
#define	BOP_NEXTPROP(bop, prev)		((bop)->bsys_nextprop)(bop, prev)

#if defined(_KERNEL) && !defined(_BOOT)

/*
 * Boot configuration information
 */

#define	BO_MAXFSNAME	16
#define	BO_MAXOBJNAME	128

struct bootobj {
	char	bo_fstype[BO_MAXFSNAME];	/* vfs type name (e.g. nfs) */
	char	bo_name[BO_MAXOBJNAME];		/* name of object */
	int	bo_flags;			/* flags, see below */
	int	bo_size;			/* number of blocks */
	struct vnode *bo_vp;			/* vnode of object */
};

/*
 * flags
 */
#define	BO_VALID	0x01	/* all information in object is valid */
#define	BO_BUSY		0x02	/* object is busy */

extern struct bootobj rootfs;
extern struct bootobj frontfs;
extern struct bootobj backfs;
extern struct bootobj dumpfile;
extern struct bootobj swapfile;

extern dev_t getrootdev(void);
extern dev_t getswapdev(char *);
extern void getfsname(char *, char *);
extern int loadrootmodules(void);

extern int strplumb(void);
extern int strplumb_get_driver_list(int, char **, char **);

extern void consconfig(void);

/* XXX	Doesn't belong here */
extern int zsgetspeed(dev_t);

extern void param_check(void);

/*
 * XXX	The memlist stuff belongs in a header of its own
 */
extern int check_boot_version(int);
extern void size_physavail(struct memlist *, u_int *, int *);
extern int copy_physavail(struct memlist *, struct memlist **,
    u_int, u_int);
extern void installed_top_size(struct memlist *, int *, int *);
extern int check_memexp(struct memlist *, u_int);
extern void copy_memlist(struct memlist *, struct memlist **);
extern int address_in_memlist(struct memlist *, caddr_t, u_int);
extern void fix_prom_pages(struct memlist *, struct memlist *);

extern struct bootops *bootops;
extern struct memlist *phys_install;
extern int netboot;
extern int swaploaded;
extern int modrootloaded;
extern char kern_bootargs[];
extern char *default_path;

#endif /* _KERNEL && !_BOOT */

#if defined(_BOOT)

/*
 * This structure is used by boot.  So don't remove it
 * XXX	So put it somewhere else.
 */
struct avreg {
	u_int	type;
	u_int	start;
	u_int	size;
};

#endif /* _BOOT */

#ifdef __cplusplus
}
#endif

#endif	/* _SYS_BOOTCONF_H */
