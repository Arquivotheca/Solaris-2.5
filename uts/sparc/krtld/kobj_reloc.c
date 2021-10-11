/*
 * Copyright (c) 1991-1993, Sun Microsystems, Inc.
 */

#pragma ident	"@(#)kobj_reloc.c	1.11	95/07/18 SMI"

/*
 * SPARC relocation code.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/systm.h>
#include <sys/user.h>
#include <sys/bootconf.h>
#include <sys/modctl.h>
#include <sys/elf.h>
#include <sys/kobj.h>
#include <sys/kobj_impl.h>

int
do_relocations(mp)
	struct module *mp;
{
	u_int shn;
	Elf32_Shdr *shp, *rshp;
	u_int nreloc;

	/* do the relocations */
	for (shn = 1; shn < mp->hdr.e_shnum; shn++) {
		rshp = (Elf32_Shdr *)
			(mp->shdrs + shn * mp->hdr.e_shentsize);
		if (rshp->sh_type == SHT_REL) {
			_printf(ops, "%s can't process type SHT_REL\n",
			    mp->filename);
			return (1);
		}
		if (rshp->sh_type != SHT_RELA)
			continue;
		if (rshp->sh_link != mp->symtbl_section) {
			_printf(ops, "%s reloc for non-default symtab\n",
			    mp->filename);
			return (-1);
		}
		if (rshp->sh_info >= mp->hdr.e_shnum) {
			_printf(ops, "do_relocations: %s sh_info out of "
			    "range %d\n", mp->filename, shn);
			goto bad;
		}
		nreloc = rshp->sh_size / rshp->sh_entsize;

		/* get the section header that this reloc table refers to */
		shp = (Elf32_Shdr *)
		    (mp->shdrs + rshp->sh_info * mp->hdr.e_shentsize);

		if (do_relocate(mp, (char *)rshp->sh_addr, nreloc,
		    rshp->sh_entsize, shp->sh_addr) < 0) {
			_printf(ops, "do_relocations: %s do_relocate failed\n",
			    mp->filename);
			goto bad;
		}
		kobj_free((void *)rshp->sh_addr, rshp->sh_size);
	}
	return (0);
bad:
	kobj_free((void *)rshp->sh_addr, rshp->sh_size);
	return (-1);
}


/* the following started life in rtld/sparc/reloc.c */

#define	MASK(n)			((1<<(n))-1)
#define	IS_PC_RELATIVE(X)	(pc_rel_type[(X)] == 1)
#define	IN_RANGE(v, n)		((-(1<<((n)-1))) <= (v) && (v) < (1<<((n)-1)))


/*
 * PC relative relocation if non-zero.
 */
static unsigned char pc_rel_type[] = {
	0,		/* R_SPARC_NONE		*/
	0,		/* R_SPARC_8		*/
	0,		/* R_SPARC_16		*/
	0,		/* R_SPARC_32		*/
	1,		/* R_SPARC_DISP8	*/
	1,		/* R_SPARC_DISP16	*/
	1,		/* R_SPARC_DISP32	*/
	1,		/* R_SPARC_WDISP30	*/
	1,		/* R_SPARC_WDISP22	*/
	0,		/* R_SPARC_HI22		*/
	0,		/* R_SPARC_22		*/
	0,		/* R_SPARC_13		*/
	0,		/* R_SPARC_LO10		*/
	0,		/* R_SPARC_GOT10	*/
	0,		/* R_SPARC_GOT13	*/
	0,		/* R_SPARC_GOT22	*/
	1,		/* R_SPARC_PC10		*/
	1,		/* R_SPARC_PC22		*/
	1,		/* R_SPARC_WPLT30	*/
	0,		/* R_SPARC_COPY		*/
	0,		/* R_SPARC_GLOB_DAT	*/
	0,		/* R_SPARC_JMP_SLOT	*/
	0,		/* R_SPARC_RELATIVE	*/
	0,		/* R_SPARC_UA32		*/
	0,		/* R_SPARC_PLT32	*/
	0,		/* R_SPARC_HIPLT22	*/
	0,		/* R_SPARC_LOPLT10	*/
	1,		/* R_SPARC_PCPLT32	*/
	1,		/* R_SPARC_PCPLT22	*/
	1,		/* R_SPARC_PCPLT10	*/
	0,		/* R_SPARC_10		*/
	0,		/* R_SPARC_11		*/
	0,		/* R_SPARC_64 		*/
	0,		/* R_SPARC_OLO10	*/
	0,		/* R_SPARC_HH22		*/
	0,		/* R_SPARC_HM10		*/
	0,		/* R_SPARC_LM22		*/
	1,		/* R_SPARC_PCHH22	*/
	1,		/* R_SPARC_PCHM10	*/
	1,		/* R_SPARC_PCLM22	*/
	1,		/* R_SPARC_WDISP16	*/
	1,		/* R_SPARC_WDISP19	*/
	0,		/* R_SPARC_GLOB_JMP	*/
	0,		/* R_SPARC_5		*/
	0,		/* R_SPARC_6		*/
	0		/* R_SPARC_7		*/
};

/*
 * Probe discovery support
 */

void *__tnf_probe_list_head;
void *__tnf_tag_list_head;

#define	PROBE_MARKER_SYMBOL	"__tnf_probe_version_1"
#define	PROBE_CONTROL_BLOCK_LINK_OFFSET 4
#define	TAG_MARKER_SYMBOL	"__tnf_tag_version_1"

/*
 * The kernel run-time linker calls this to try to resolve a reference
 * it can't otherwise resolve.  We see if it's marking a probe control
 * block or a probe tag block; if so, we do the resolution and return 0.
 * If not, we return 1 to show that we can't resolve it, either.
 */
int
tnf_reloc_resolve(char *symname, Elf32_Addr *value_p, long *addend_p,
    unsigned long *offset_p)
{
	if (strcmp(symname, PROBE_MARKER_SYMBOL) == 0) {
		*addend_p = 0;
		*value_p = (long)__tnf_probe_list_head;
		__tnf_probe_list_head = (void *)*offset_p;
		*offset_p += PROBE_CONTROL_BLOCK_LINK_OFFSET;
		return (0);
	}
	if (strcmp(symname, TAG_MARKER_SYMBOL) == 0) {
		*addend_p = 0;
		*value_p = (long) __tnf_tag_list_head;
		__tnf_tag_list_head = (void *)*offset_p;
		return (0);
	}
	return (1);
}

int
do_relocate(mp, reltbl, nreloc, relocsize, baseaddr)
	struct module *mp;
	char *reltbl;
	int nreloc;
	int relocsize;
	Elf32_Addr baseaddr;
{
	extern int standalone;
	extern int use_iflush;
	unsigned long stndx;
	unsigned long off;
	register unsigned long reladdr, rend;
	register u_int rtype;
	long addend;
	long value;
	Elf32_Sym *symref;
	unsigned long *offptr;
	union {
		unsigned long l;
		char c[4];
	} symval;
	int err = 0;
	int symnum;

	reladdr = (unsigned long)reltbl;
	rend = reladdr + nreloc * relocsize;

	symnum = -1;
	/* loop through relocations */
	while (reladdr < rend) {

		symnum++;
		rtype = ELF32_R_TYPE(((Elf32_Rela *)reladdr)->r_info);
		off = ((Elf32_Rela *)reladdr)->r_offset;
		stndx = ELF32_R_SYM(((Elf32_Rela *)reladdr)->r_info);
		if (stndx >= mp->nsyms) {
			_printf(ops, "do_relocate: bad strndx %d\n", symnum);
			return (-1);
		}
		addend = (long)(((Elf32_Rela *)reladdr)->r_addend);
		reladdr += relocsize;

		if (rtype == R_SPARC_NONE)
			continue;

		if (!(mp->flags & KOBJ_EXEC))
			off += baseaddr;
		/*
		 * if R_SPARC_RELATIVE, simply add base addr
		 * to reloc location
		 */
		if (rtype == R_SPARC_RELATIVE) {
			value = baseaddr;
		} else {
			/*
			 * get symbol table entry - if symbol is local
			 * value is base address of this object
			 */
			symref = (Elf32_Sym *)
				(mp->symtbl+(stndx * mp->symhdr->sh_entsize));
			if (ELF32_ST_BIND(symref->st_info) == STB_LOCAL) {
				/* *** this is different for .o and .so */
				value = symref->st_value;
			} else {
				/*
				 * It's global. Allow weak references.  If
				 * the symbol is undefined, give TNF (the
				 * kernel probes facility) a chance to see
				 * if it's a probe site, and fix it up if so.
				 */
				if (symref->st_shndx == SHN_UNDEF &&
				    tnf_reloc_resolve(mp->strings +
					symref->st_name, &symref->st_value,
					&addend, &off) != 0) {
					if (ELF32_ST_BIND(symref->st_info)
					    != STB_WEAK) {
						_printf(ops, "not found: %s\n",
						    mp->strings +
						    symref->st_name);
						err = 1;
					}
					continue;
				} else { /* symbol found  - relocate */
					/*
					 * calculate location of definition
					 * - symbol value plus base address of
					 * containing shared object
					 */
					value = symref->st_value;
					/*
					 * calculate final value -
					 * if PC-relative, subtract ref addr
					 */
					if (IS_PC_RELATIVE(rtype))
						value -= off;

				} /* end else symbol found */
			}
		} /* end not R_SPARC_RELATIVE */

		if (rtype != R_SPARC_UA32 && (off & 3) != 0) {
			_printf(ops, "unaligned reloc\n");
			return (-1);
		}
		offptr = (unsigned long *)off;
		/*
		 * insert value calculated at reference point
		 * 3 cases - normal byte order aligned, normal byte
		 * order unaligned, and byte swapped
		 * for the swapped and unaligned cases we insert value
		 * a byte at a time
		 */
		symval.l = value;
		switch (rtype) {

		case R_SPARC_GLOB_DAT:  /* 32bit word aligned */
		case R_SPARC_RELATIVE:
		case R_SPARC_DISP32:
		case R_SPARC_32:
			/*
			 * 7/19/89 rmk adding current value of *offptr to
			 * value.  Should not be needed, since this is a
			 * RELA type and *offptr should be in addend
			 */
			*offptr = *offptr + value + addend;
			break;
		case R_SPARC_NONE:
			break;
		case R_SPARC_8:
		case R_SPARC_DISP8:
			value += addend;
			if (IN_RANGE(value, 8)) {
				value &= MASK(8);
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 8, offptr);
				err = 1;
			}
			break;
		case R_SPARC_LO10:	/* 10 bits truncated in 13 bit field */
		case R_SPARC_PC10:
			value += addend;
			value &= MASK(10);
			*offptr |= value;
			break;
		case R_SPARC_13:
			value += addend;
			if (IN_RANGE(value, 13)) {
				value &= MASK(13);
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 13, off);
				err = 1;
			}
			break;
		case R_SPARC_16:
		case R_SPARC_DISP16:
			value += addend;
			if (IN_RANGE(value, 16)) {
				value &= MASK(16);
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 16, off);
				err = 1;
			}
			break;
		case R_SPARC_22:
			value += addend;
			if (IN_RANGE(value, 22)) {
				value &= MASK(22);
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 22, off);
				err = 1;
			}
			break;
		case R_SPARC_PC22:
			value += addend;
			value = (unsigned)value >> 10;
			if (IN_RANGE(value, 22)) {
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 22, off);
				err = 1;
			}
			break;
		case R_SPARC_WDISP22:
			value += addend;
			value = value >> 2;
			if (IN_RANGE(value, 22)) {
				value &= MASK(22);
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 22, off);
				err = 1;
			}
			break;
		case R_SPARC_HI22:
			value += addend;
			value = (unsigned)value >> 10;
			*offptr |= value;
			break;
		case R_SPARC_WDISP30:
		case R_SPARC_WPLT30:
			value = (unsigned)value >> 2;
			*offptr |= value;
			break;
		case R_SPARC_UA32:
			symval.l += addend;
			((char *)off)[0] = symval.c[0];
			((char *)off)[1] = symval.c[1];
			((char *)off)[2] = symval.c[2];
			((char *)off)[3] = symval.c[3];
			break;
		case R_SPARC_10:
			value += addend;
			if (IN_RANGE(value, 10)) {
				value &= MASK(10);
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 10, off);
				err = 1;
			}
			break;
		case R_SPARC_11:
			value += addend;
			if (IN_RANGE(value, 11)) {
				value &= MASK(11);
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 11, off);
				err = 1;
			}
			break;
		case R_SPARC_WDISP16:
			value += addend;
			value = value >> 2;
			if (IN_RANGE(value, 16)) {
				value &= MASK(16);
				*offptr |= (((value & 0xc000) << 6)
						| (value & 0x3fff));
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 16, off);
				err = 1;
			}
			break;
		case R_SPARC_WDISP19:
			value += addend;
			value = value >> 2;
			if (IN_RANGE(value, 19)) {
				value &= MASK(19);
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 19, off);
				err = 1;
			}
			break;
		case R_SPARC_5:
			value += addend;
			if (IN_RANGE(value, 5)) {
				value &= MASK(5);
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 5, off);
				err = 1;
			}
			break;
		case R_SPARC_6:
			value += addend;
			if (IN_RANGE(value, 6)) {
				value &= MASK(6);
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 6, off);
				err = 1;
			}
			break;
		case R_SPARC_7:
			value += addend;
			if (IN_RANGE(value, 7)) {
				value &= MASK(7);
				*offptr |= value;
			} else {
				_printf(ops, "relocation error: value %x "
				    "overflows %d bits at location %x",
				    value, 7, off);
				err = 1;
			}
			break;
		default:
			_printf(ops, "invalid relocation type %d at 0x%x",
				rtype, off);
			err = 1;
			goto done;
		}
		/*
		 * Text relocations may be in the icache
		 * when binding the primary modules. Flush
		 * it to be safe.
		 */
		if (standalone && use_iflush) {
			extern void doflush();

			doflush(offptr);
		}
	} /* end of while loop */
done:
	if (err)
		return (-1);
	return (0);
}
