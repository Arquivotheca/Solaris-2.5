/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_KOBJ_IMPL_H
#define	_SYS_KOBJ_IMPL_H

#pragma ident	"@(#)kobj_impl.h	1.8	95/07/18 SMI"

/*
 * Boot/aux vector attributes.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#define	BA_DYNAMIC	0
#define	BA_PHDR		1
#define	BA_PHNUM	2
#define	BA_PHENT	3
#define	BA_ENTRY	4
#define	BA_PAGESZ	5
#define	BA_LPAGESZ	6
#define	BA_LDELF	7
#define	BA_LDSHDR	8
#define	BA_LDNAME	9
#define	BA_BSS		10
#define	BA_IFLUSH	11
#define	BA_NUM		12

typedef union {
		unsigned int ba_val;
		void *ba_ptr;
} val_t;

/*
 * Segment info.
 */
struct proginfo {
	u_int size;
	u_int align;
};

/*
 * Implementation-specific flags.
 */
#define		KOBJ_SYMSWAP	0x01	/* symbol tables swappable	*/
#define		KOBJ_SYMKMEM	0x02	/* symbol tables kmem-alloced	*/
#define		KOBJ_EXEC	0x04	/* executable (unix module)	*/
#define		KOBJ_INTERP	0x08	/* the interpreter module	*/
#define		KOBJ_PRIM	0x10	/* a primary kernel module	*/
#define		KOBJ_RESOLVED	0x20	/* fully resolved		*/

#ifdef KOBJ_DEBUG
/*
 * Debugging flags.
 */
#define	D_DEBUG			0x001	/* general debugging */
#define	D_SYMBOLS		0x002	/* debug symbols */
#define	D_RELOCATIONS		0x004	/* debug relocations */
#define	D_BINDPRI		0x008	/* primary binding */
#define	D_BOOTALLOC		0x010	/* mem allocated from boot */
#define	D_LOADING		0x020	/* section loading */
#define	D_PRIMARY		0x040	/* debug primary mods only */
#define	D_LTDEBUG		0x080	/* light debugging */

extern int kobj_debug;		/* different than moddebug */
#endif

extern void kobj_init(void *romvec, void *dvec,
	struct bootops *bootvec, val_t *bootaux);
extern int do_relocations(struct module *);
extern int do_relocate(struct module *, char *, int, int, Elf32_Addr);
extern void (*_printf)();
extern struct bootops *ops;

extern int strcmp(const char *, const char *);
extern size_t strlen(const char *);
extern char *strcpy(char *, const char *);
extern char *strcat(char *, const char *);
extern void exitto(caddr_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_KOBJ_IMPL_H */
