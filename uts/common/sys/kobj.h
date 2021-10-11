/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#ifndef _SYS_KOBJ_H
#define	_SYS_KOBJ_H

#pragma ident	"@(#)kobj.h	1.27	95/02/09 SMI"

#include <sys/modctl.h>
#include <sys/elf.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * List of modules maintained by kobj.c
 */
struct module_list {
	struct module_list *next;
	struct module *mp;
};

struct module {
	struct kobj_symbol_info *syminfo;
	int total_allocated;

	Elf32_Ehdr hdr;
	char *shdrs;
	Elf32_Shdr *symhdr, *strhdr;

	char *depends_on;

	unsigned int symsize;
	char *symspace;	/* symbols + strings + hashtbl, or NULL */
	int flags;

	unsigned int text_size;
	unsigned int data_size;
	char *text;
	char *data;

	unsigned int symtbl_section;
	/* pointers into symspace, or NULL */
	char *symtbl;
	char *strings;

	unsigned int hashsize;
	unsigned int *buckets;
	unsigned int *chains;

	unsigned int nsyms;

	unsigned int bss_align;
	unsigned int bss_size;
	unsigned int bss;

	char *filename;

	struct module_list *head, *tail;
};

/*
 * This structure is for the debugger, and it is separate
 * from the main structures so we don't have to change it
 * as often
 */
struct kobj_symbol_info {
	struct kobj_symbol_info *next;
	unsigned int base1, len1;
	unsigned int base2, len2;
	unsigned int base3, len3;
	int nsyms;
	int symsize;
	char *symtbl;
	int string_size;
	char *strings;
	int hash_size;
	unsigned int *buckets;
	unsigned int *chains;
	int *byaddr;
	int version;
	int id;
};


struct kobj_mem {
	struct kobj_mem	*km_next;
	struct kobj_mem *km_prev;
	ulong_t		km_addr;
	size_t		km_size;
	ulong_t		km_alloc_addr;
	size_t		km_alloc_size;
};

struct _buf {
	int		_cnt;
	char		*_ptr;
	char		*_base;
	int		 _size;
	int		 _fd;
	int		 _off;
	char 		*_name;
	int		_ln;
};


/*
 * Statistical info.
 */
typedef struct {
	int nalloc;
	int nfree;
	int nalloc_calls;
	int nfree_calls;
} kobj_stat_t;

#define	kobj_filename(p) ((p)->_name)
#define	kobj_linenum(p)  ((p)->_ln)
#define	kobj_newline(p)	 ((p)->_ln++)
#define	kobj_getc(p)	(--(p)->_cnt >= 0 ? ((int)*(p)->_ptr++):kobj_filbuf(p))
#define	kobj_ungetc(p)	 (++(p)->_cnt > (p)->_size ? -1 : ((int)*(p)->_ptr--))

#define	B_OFFSET(f_offset) (f_offset & (MAXBSIZE-1))	/* Offset into buffer */
#define	F_PAGE(f_offset)   (f_offset & ~(MAXBSIZE-1))	/* Start of page */

#if defined(_KERNEL)

extern void kobj_load_module(struct modctl *, int);
extern void kobj_unload_module(struct modctl *);
extern unsigned int kobj_lookup(void *, char *);
extern Elf32_Sym *kobj_lookup_all(struct module *, char *, int);
extern int kobj_addrcheck(void *, caddr_t);
extern int kobj_module_to_id(void *);
extern void kobj_getmodinfo(void *, struct modinfo *);
extern int kobj_get_needed(void *, short *, int);
extern unsigned int kobj_getsymvalue(char *, int);
extern char *kobj_getsymname(unsigned int, unsigned int *);
extern char *kobj_searchsym(struct module *, u_int, u_int *);
extern void kobj_get_packing_info(char *);

extern int kobj_open(char *);
extern struct _buf *kobj_open_path(char *, int);
extern int kobj_read(int, char *, unsigned int, unsigned int);
extern void kobj_close(int);
extern void *kobj_alloc(size_t, int);
extern void *kobj_zalloc(size_t, int);
extern void kobj_free(void *, size_t);
extern struct _buf *kobj_open_file(char *);
extern void kobj_close_file(struct _buf *);
extern int kobj_read_file(struct _buf *, char *, unsigned, unsigned);
extern unsigned int kobj_getelfsym(char *, void *, int *);

extern int kobj_filbuf(struct _buf *);
extern void kobj_sync(void);

extern int kobj_lock_syms(void);
extern int kobj_unlock_syms(void);

extern void kobj_stat_get(kobj_stat_t *);

#endif	/* defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_KOBJ_H */
