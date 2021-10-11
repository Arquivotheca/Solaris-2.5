/*
 * Copyright (c) 1995 by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)ksyms.c	1.11	95/09/13 SMI"

/*
 * ksyms driver - exports a single symbol/string table for the kernel
 * by concatenating all the module symbol/string tables.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/uio.h>
#include <sys/kmem.h>
#include <sys/cred.h>
#include <sys/mman.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/modctl.h>
#include <sys/elf.h>
#include <sys/debug.h>
#include <sys/kobj.h>
#include <vm/as.h>
#include <vm/seg_kp.h>
#include <sys/systm.h>
#include <sys/ddi.h>		/* amusing in light of what follows... */
#include <sys/sunddi.h>

extern char stubs_base[], stubs_end[];	/* so we can distinguish stubs */
extern void _start();			/* start of kernel text */
extern char t0stack[];			/* start of kernel data */
extern char *e_text, *e_data;		/* end of kernel text, data */
extern int maxusers;			/* to initialize max clones */

static const char ksyms_shstrtab[] = "\0.symtab\0.strtab\0.shstrtab\0";

typedef struct ksyms_header {
	Elf32_Ehdr	elf_hdr;	/* Elf file header */
	Elf32_Phdr	text_phdr;	/* text program header */
	Elf32_Phdr	data_phdr;	/* data program header */
	Elf32_Shdr	null_shdr;	/* first shdr is null */
	Elf32_Shdr	sym_shdr;	/* symtab shdr */
	Elf32_Shdr	str_shdr;	/* strtab shdr */
	Elf32_Shdr	shstr_shdr;	/* shstrtab shdr */
	char		shstr_raw[sizeof (ksyms_shstrtab)];
} ksyms_header_t;

struct elf_image {
	char *elf_base;		/* base address of ELF image */
	int elf_size;		/* size of ELF image */
	int locked;		/* set if image is locked down */
	int refcnt;		/* reference count */
	int modmix;		/* module mix at time of snapshot */
};

int nksyms_clones;		/* tunable: max clones of this device */
int nksyms_images = 10;		/* tunable: max ELF images (consumes segkp) */
int nksyms_mmaps = 1;		/* tunable: max mmaps (consumes physmem) */

static int nksyms_images_active;
static int nksyms_mmaps_active;
static struct elf_image *current_image;
static struct elf_image **ksyms_clones;	/* clone device array */
static kmutex_t ksyms_lock;
static kmutex_t ksyms_image_lock;
static dev_info_t *ksyms_devi;

static struct elf_image *
ksyms_image_create(void)
{
	struct elf_image *eip;
	struct module *mod;
	struct modctl *modp;
	void *locbuf, *gblbuf, *strbuf;	/* bufs for locals, globals, strings */
	u_int locsz, gblsz, strsz;	/* sizes of above */
	Elf32_Sym *locwlk, *gblwlk;	/* current ptr into locals, globals */
	char *strwlk;			/* current ptr into strings */
	int bufsz = 150000;		/* initial guess at buffer size */
	Elf32_Sym *symtbl, *sp, *tp;
	ksyms_header_t *hdr;

	if (nksyms_images_active >= nksyms_images)
		return (NULL);

	eip = kmem_zalloc(sizeof (struct elf_image), KM_SLEEP);
retry:
	locwlk = locbuf = kmem_zalloc(3 * bufsz, KM_SLEEP);
	gblwlk = gblbuf = (char *)locbuf + bufsz;
	strwlk = strbuf = (char *)gblbuf + bufsz;
	strwlk++;	/* first byte of string table is null */

	eip->modmix = mod_mix_changed;
	modp = &modules;
	do {
		mutex_enter(&mod_lock);
		while (mod_hold_by_modctl(modp) == 1)
			continue;
		mutex_exit(&mod_lock);
		if ((mod = modp->mod_mp) == NULL) {
			mod_release_mod(modp);
			continue;
		}

		/*
		 * Copy symbol table into our buffers, filtering out
		 * various cruft along the way.  Stubs, already-defined
		 * globals and local undef symbols are all tossed.
		 */
		symtbl = (Elf32_Sym *)mod->symtbl;
		for (sp = symtbl; sp < &symtbl[mod->nsyms]; sp++) {
			char *symname = mod->strings + (u_int)sp->st_name;
			int symlen = sp->st_name ? strlen(symname) + 1 : 0;
			if (ELF32_ST_BIND(sp->st_info) == STB_LOCAL) {
				if (mod != modules.mod_mp &&
				    sp->st_shndx == SHN_UNDEF)
					continue;
				tp = locwlk++;
			} else {
				if ((mod == modules.mod_mp &&
				    sp->st_value >= (u_int)stubs_base &&
				    sp->st_value < (u_int)stubs_end &&
				    ELF32_ST_TYPE(sp->st_info) == STT_NOTYPE) ||
				    kobj_lookup(mod, symname) == NULL)
					continue;
				tp = gblwlk++;
			}
			locsz = (char *)locwlk - (char *)locbuf;
			gblsz = (char *)gblwlk - (char *)gblbuf;
			strsz = (char *)strwlk - (char *)strbuf + symlen;
			if (locsz > bufsz || gblsz > bufsz || strsz > bufsz) {
				mod_release_mod(modp);
				kmem_free(locbuf, 3 * bufsz);
				bufsz *= 2;
				goto retry;
			}
			*tp = *sp;	/* struct copy */
			if (symlen != 0) {
				tp->st_shndx = SHN_ABS;	/* it's all absolute */
				tp->st_name = strwlk - (char *)strbuf;
				strcpy(strwlk, symname);
				strwlk += symlen;
			}
		}
		mod_release_mod(modp);
	} while ((modp = modp->mod_next) != &modules);

	eip->elf_size = sizeof (ksyms_header_t) + locsz + gblsz + strsz;
	eip->elf_base = segkp_get(segkp, roundup(eip->elf_size, PAGESIZE), 0);
	if (eip->elf_base == NULL) {
		cmn_err(CE_NOTE, "ksyms: can't allocate space for image");
		kmem_free(eip, sizeof (struct elf_image));
		kmem_free(locbuf, 3 * bufsz);
		return (NULL);
	}

	hdr = (ksyms_header_t *)eip->elf_base;
	bzero((void *)hdr, sizeof (*hdr));

	hdr->elf_hdr = ((struct module *)modules.mod_mp)->hdr;
	hdr->elf_hdr.e_phoff = (int)&((ksyms_header_t *)0)->text_phdr;
	hdr->elf_hdr.e_shoff = (int)&((ksyms_header_t *)0)->null_shdr;
	hdr->elf_hdr.e_phnum = 2;
	hdr->elf_hdr.e_shnum = 4;
	hdr->elf_hdr.e_shstrndx = 3;

	hdr->text_phdr.p_type = PT_LOAD;
	hdr->text_phdr.p_vaddr = (Elf32_Addr)_start;	/* XXX - unix only */
	hdr->text_phdr.p_memsz = (Elf32_Word)((u_int)e_text - (u_int)_start);
	hdr->text_phdr.p_flags = PF_R | PF_X;

	hdr->data_phdr.p_type = PT_LOAD;
	hdr->data_phdr.p_vaddr = (Elf32_Addr)t0stack;	/* XXX - unix only */
	hdr->data_phdr.p_memsz = (Elf32_Word)((u_int)e_data - (u_int)t0stack);
	hdr->data_phdr.p_flags = PF_R | PF_W | PF_X;

	hdr->sym_shdr.sh_name = 0x1;	/* ksyms_shstrtab[1] = ".symtab" */
	hdr->sym_shdr.sh_type = SHT_SYMTAB;
	hdr->sym_shdr.sh_offset = sizeof (ksyms_header_t);
	hdr->sym_shdr.sh_size = locsz + gblsz;
	hdr->sym_shdr.sh_link = 2;
	hdr->sym_shdr.sh_info = locsz / sizeof (Elf32_Sym);
	hdr->sym_shdr.sh_addralign = 4;
	hdr->sym_shdr.sh_entsize = sizeof (Elf32_Sym);

	hdr->str_shdr.sh_name = 9;	/* ksyms_shstrtab[9] = ".strtab" */
	hdr->str_shdr.sh_type = SHT_STRTAB;
	hdr->str_shdr.sh_offset = sizeof (ksyms_header_t) + locsz + gblsz;
	hdr->str_shdr.sh_size = strsz;
	hdr->str_shdr.sh_addralign = 1;

	hdr->shstr_shdr.sh_name = 17;	/* ksyms_shstrtab[17] = ".shstrtab" */
	hdr->shstr_shdr.sh_type = SHT_STRTAB;
	hdr->shstr_shdr.sh_offset = (int)&((ksyms_header_t *)0)->shstr_raw;
	hdr->shstr_shdr.sh_size = sizeof (ksyms_shstrtab);
	hdr->shstr_shdr.sh_addralign = 1;

	/*
	 * XXX -- the following is WRONG, but it's what the ksyms driver
	 * always used to do and it's too late in the Solaris 2.5 release
	 * to risk changing its behavior.  e_entry should not be modified
	 * from what the 'unix' module reports (_start).  p_align should be 0.
	 * Delete these seven lines of code early in the next release.
	 */
	hdr->elf_hdr.e_entry = 0;
#ifdef i386
	hdr->text_phdr.p_align = hdr->data_phdr.p_align = 0x1000;
#endif
#ifdef sparc
	hdr->text_phdr.p_align = hdr->data_phdr.p_align = 0x10000;
#endif

	bcopy((void *)ksyms_shstrtab, hdr->shstr_raw, sizeof (ksyms_shstrtab));

	bcopy(locbuf, eip->elf_base + sizeof (*hdr), locsz);
	bcopy(gblbuf, eip->elf_base + sizeof (*hdr) + locsz, gblsz);
	bcopy(strbuf, eip->elf_base + sizeof (*hdr) + locsz + gblsz, strsz);

	kmem_free(locbuf, 3 * bufsz);
	return (eip);
}

static struct elf_image *
ksyms_image_hold(dev_t dev)
{
	minor_t clone = getminor(dev);
	struct elf_image *eip = NULL;

	mutex_enter(&ksyms_lock);
	if (clone < nksyms_clones && (eip = ksyms_clones[clone]) != NULL)
		eip->refcnt++;
	mutex_exit(&ksyms_lock);
	return (eip);
}

static void
ksyms_image_release(struct elf_image *eip)
{
	mutex_enter(&ksyms_lock);
	if (eip != NULL && --eip->refcnt == 0) {
		nksyms_images_active--;
		nksyms_mmaps_active -= eip->locked;
		mutex_exit(&ksyms_lock);
		segkp_release(segkp, eip->elf_base);
		kmem_free(eip, sizeof (struct elf_image));
	} else
		mutex_exit(&ksyms_lock);
}

static int
ksyms_image_lockdown(struct elf_image *eip)
{
	int locked;

	mutex_enter(&ksyms_image_lock);
	mutex_enter(&ksyms_lock);
	if ((locked = eip->locked) == 0 && nksyms_mmaps_active < nksyms_mmaps) {
		mutex_exit(&ksyms_lock);
		locked = as_fault(kas.a_hat, &kas, eip->elf_base,
		    roundup(eip->elf_size, PAGESIZE), F_SOFTLOCK, S_READ) == 0;
		mutex_enter(&ksyms_lock);
		nksyms_mmaps_active += eip->locked = locked;
	}
	mutex_exit(&ksyms_lock);
	mutex_exit(&ksyms_image_lock);
	return (locked);
}

/*
 * Open the ELF image to kernel symbol/string info.  This is a clone driver;
 * the minor number associates a particular ELF image with this open.
 */
/* ARGSUSED */
static int
ksyms_open(dev_t *devp, int flag, int otyp, struct cred *cred)
{
	struct elf_image *eip;
	minor_t clone;

	ASSERT(getminor(*devp) == 0);
	mutex_enter(&mod_unload_lock);
	(void) modunloadctrl(MOD_AUTOUNLOAD_OFF);
	mutex_exit(&mod_unload_lock);

	mutex_enter(&ksyms_image_lock);
	mutex_enter(&ksyms_lock);
	if ((eip = current_image) == NULL || eip->modmix != mod_mix_changed) {
		mutex_exit(&ksyms_lock);
		ksyms_image_release(eip);
		if ((eip = current_image = ksyms_image_create()) == NULL) {
			mutex_exit(&ksyms_image_lock);
			goto bad;
		}
		mutex_enter(&ksyms_lock);
		eip->refcnt = 1;		/* hold new current_image */
		nksyms_images_active++;
	}
	mutex_exit(&ksyms_image_lock);

	/* XXX -- Don't use clone 0, that's the "real" minor number */
	for (clone = 1; clone < nksyms_clones; clone++) {
		if (ksyms_clones[clone] == NULL) {
			ksyms_clones[clone] = eip;
			eip->refcnt++;		/* hold the image */
			*devp = makedevice(getemajor(*devp), clone);
			mutex_exit(&ksyms_lock);
			return (0);
		}
	}
	mutex_exit(&ksyms_lock);
	cmn_err(CE_NOTE, "ksyms: too many open references");
bad:
	mutex_enter(&mod_unload_lock);
	(void) modunloadctrl(MOD_AUTOUNLOAD_ON);
	mutex_exit(&mod_unload_lock);
	return (ENXIO);
}

/* ARGSUSED */
static int
ksyms_close(dev_t dev, int flag, int otyp, struct cred *cred)
{
	minor_t clone = getminor(dev);
	struct elf_image *eip;

	mutex_enter(&ksyms_lock);
	eip = ksyms_clones[clone];
	ASSERT(eip != NULL);
	ksyms_clones[clone] = NULL;
	mutex_exit(&ksyms_lock);
	ksyms_image_release(eip);

	mutex_enter(&mod_unload_lock);
	(void) modunloadctrl(MOD_AUTOUNLOAD_ON);
	mutex_exit(&mod_unload_lock);
	return (0);
}

/* ARGSUSED */
static int
ksyms_read(dev_t dev, struct uio *uio, struct cred *cred)
{
	int error;
	struct elf_image *eip = ksyms_image_hold(dev);

	error = uiomove(eip->elf_base + uio->uio_offset, min(uio->uio_resid,
		eip->elf_size - uio->uio_offset), UIO_READ, uio);

	ksyms_image_release(eip);
	return (error);
}

static int
ksyms_mmap(dev_t dev, off_t off, int prot)
{
	int retval = -1;
	struct elf_image *eip = ksyms_image_hold(dev);

	if (!(prot & PROT_WRITE) &&
	    off <= eip->elf_size && ksyms_image_lockdown(eip))
		retval = (int)hat_getkpfnum(eip->elf_base + off);

	ksyms_image_release(eip);
	return (retval);
}

/*
 * Property operations.  Return the size of the referenced ksyms image.
 * We only handle size property requests.  Others are passed on.
 */
static int
ksyms_prop_op(dev_t dev, dev_info_t *dip, ddi_prop_op_t prop,
	int m, char *name, char *valuep, int *lengthp)
{
	struct elf_image *eip;
	int length, image_size, *retbuf;

	if (strcmp(name, "size") == 0) {
		if ((eip = ksyms_image_hold(dev)) == NULL)
			return (DDI_PROP_NOT_FOUND);
		image_size = eip->elf_size;
		ksyms_image_release(eip);

		length = *lengthp;		/* get caller's length */
		*lengthp = sizeof (int);	/* set caller's length */

		switch (prop) {

		case PROP_LEN:
			return (DDI_PROP_SUCCESS);

		case PROP_LEN_AND_VAL_ALLOC:
			retbuf = kmem_alloc(sizeof (int),
			    (m & DDI_PROP_CANSLEEP) ? KM_SLEEP : KM_NOSLEEP);
			if (retbuf == NULL)
				return (DDI_PROP_NO_MEMORY);
			*(int **)valuep = retbuf;	/* set caller's buf */
			*retbuf = image_size;
			return (DDI_PROP_SUCCESS);

		case PROP_LEN_AND_VAL_BUF:
			if (length < sizeof (int))
				return (DDI_PROP_BUF_TOO_SMALL);
			*(int *)valuep = image_size;
			return (DDI_PROP_SUCCESS);
		}
	}
	return (ddi_prop_op(dev, dip, prop, m, name, valuep, lengthp));
}

/* ARGSUSED */
static int
ksyms_info(dev_info_t *dip, ddi_info_cmd_t infocmd, void *arg, void **result)
{
	switch (infocmd) {
	case DDI_INFO_DEVT2DEVINFO:
		*result = ksyms_devi;
		return (DDI_SUCCESS);
	case DDI_INFO_DEVT2INSTANCE:
		*result = 0;
		return (DDI_SUCCESS);
	}
	return (DDI_FAILURE);
}

static int
ksyms_identify(dev_info_t *devi)
{
	if (strcmp(ddi_get_name(devi), "ksyms") == 0)
		return (DDI_IDENTIFIED);
	return (DDI_NOT_IDENTIFIED);
}

static int
ksyms_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH)
		return (DDI_FAILURE);
	if (ddi_create_minor_node(devi, "ksyms", S_IFCHR, 0, NULL, NULL) ==
	    DDI_FAILURE) {
		ddi_remove_minor_node(devi, NULL);
		return (DDI_FAILURE);
	}
	ksyms_devi = devi;
	return (DDI_SUCCESS);
}

static int
ksyms_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH)
		return (DDI_FAILURE);
	ddi_remove_minor_node(devi, NULL);
	return (DDI_SUCCESS);
}

static struct cb_ops ksyms_cb_ops = {
	ksyms_open,		/* open */
	ksyms_close,		/* close */
	nodev,			/* strategy */
	nodev,			/* print */
	nodev,			/* dump */
	ksyms_read,		/* read */
	nodev,			/* write */
	nodev,			/* ioctl */
	nodev,			/* devmap */
	ksyms_mmap,		/* mmap */
	nodev,			/* segmap */
	nochpoll,		/* poll */
	ksyms_prop_op,		/* prop_op */
	0,			/* streamtab  */
	D_NEW | D_MP		/* Driver compatibility flag */
};

static struct dev_ops ksyms_ops = {
	DEVO_REV,		/* devo_rev, */
	0,			/* refcnt  */
	ksyms_info,		/* info */
	ksyms_identify,		/* identify */
	nulldev,		/* probe */
	ksyms_attach,		/* attach */
	ksyms_detach,		/* detach */
	nodev,			/* reset */
	&ksyms_cb_ops,		/* driver operations */
	(struct bus_ops *)0	/* no bus operations */
};

static struct modldrv modldrv = {
	&mod_driverops, "kernel symbols driver", &ksyms_ops,
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *)&modldrv, NULL
};

int
_init(void)
{
	int error;

	if (nksyms_clones == 0)
		nksyms_clones = maxusers + 50;

	mutex_init(&ksyms_lock, "ksyms_lock", MUTEX_DEFAULT, NULL);
	mutex_init(&ksyms_image_lock, "ksyms_image_lock", MUTEX_DEFAULT, NULL);
	ksyms_clones = kmem_zalloc(nksyms_clones * sizeof (void *), KM_SLEEP);

	if ((error = mod_install(&modlinkage)) != 0) {
		mutex_destroy(&ksyms_lock);
		mutex_destroy(&ksyms_image_lock);
		kmem_free(ksyms_clones, nksyms_clones * sizeof (void *));
	}
	return (error);
}

int
_fini(void)
{
	int error;

	if ((error = mod_remove(&modlinkage)) == 0) {
		ksyms_image_release(current_image);
		ASSERT(nksyms_images_active == 0);
		mutex_destroy(&ksyms_lock);
		mutex_destroy(&ksyms_image_lock);
		kmem_free(ksyms_clones, nksyms_clones * sizeof (void *));
	}
	return (error);
}

int
_info(struct modinfo *modinfop)
{
	return (mod_info(&modlinkage, modinfop));
}
