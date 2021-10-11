/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */
#pragma	ident	"@(#)kobj_stubs.c	1.8	95/02/09 SMI"

#include <sys/kobj.h>

/*
 * Stubs for entry points into
 * the stand-alone linker/loader.
 */

/*ARGSUSED*/
void
kobj_load_module(struct modctl *modp, int use_path)
{
}

/*ARGSUSED*/
void
kobj_unload_module(struct modctl *modp)
{
}

/*ARGSUSED*/
struct _buf *
kobj_open_path(char *name, int use_path)
{
	return (NULL);
}

/*ARGSUSED*/
struct _buf *
kobj_open_file(char *name)
{
	return (NULL);
}

/*ARGSUSED*/
int
kobj_read_file(struct _buf *file, char *buf, unsigned size, unsigned off)
{
	return (-1);
}

/*ARGSUSED*/
void
kobj_close_file(struct _buf *file)
{
}

/*ARGSUSED*/
int
kobj_open(char *filename)
{
	return (-1);
}

/*ARGSUSED*/
int
kobj_read(int descr, char *buf, unsigned size, unsigned offset)
{
	return (-1);
}

/*ARGSUSED*/
void
kobj_close(int descr)
{
}

/*ARGSUSED*/
int
kobj_filbuf(struct _buf *f)
{
	return (-1);
}

/*ARGSUSED*/
int
kobj_addrcheck(void *xmp, caddr_t adr)
{
	return (1);
}

/*ARGSUSED*/
u_int
kobj_getelfsym(char *name, void *mp, int *size)
{
	return (0);
}

/*ARGSUSED*/
void
kobj_getmodinfo(void *xmp, struct modinfo *modinfo)
{
}

/*ARGSUSED*/
char *
kobj_getsymname(u_int value, u_int *offset)
{
	return (NULL);
}

/*ARGSUSED*/
u_int
kobj_getsymvalue(char *name, int kernelonly)
{
	return (0);
}

/*ARGSUSED*/
char *
kobj_searchsym(struct module *mp, u_int value, u_int *offset)
{
	return (NULL);
}

/*ARGSUSED*/
u_int
kobj_lookup(void *mod, char *name)
{
	return (0);
}

/*ARGSUSED*/
Elf32_Sym *
kobj_lookup_all(struct module *mp, char *name, int include_self)
{
	return (NULL);
}

/*ARGSUSED*/
void *
kobj_alloc(size_t size, int flag)
{
	return (NULL);
}

/*ARGSUSED*/
void *
kobj_zalloc(size_t size, int flag)
{
	return (NULL);
}

/*ARGSUSED*/
void
kobj_free(void *address, size_t size)
{
}

/*ARGSUSED*/
void
kobj_sync(void)
{
}

/*ARGSUSED*/
void
kobj_stat_get(kobj_stat_t *kp)
{
}

/*ARGSUSED*/
void
kobj_get_packing_info(char *file)
{
}
