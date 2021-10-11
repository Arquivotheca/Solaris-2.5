/*
 * Copyright (c) 1993, Sun Microsystems, Inc.
 */

#pragma ident	"@(#)kobj_boot.c	1.13	95/07/18 SMI"

/*
 * Bootstrap the linker/loader.
 */

#include <sys/types.h>
#include <sys/bootconf.h>
#include <sys/debug/debug.h>
#include <sys/elf.h>
#include <sys/link.h>
#include <sys/auxv.h>
#include <sys/kobj.h>
#include <sys/kobj_impl.h>

/*
 * We don't use the global offset table, but
 * ld may throw in an UNDEFINED reference in
 * our symbol table.
 */
#pragma	weak		_GLOBAL_OFFSET_TABLE_

#define	MASK(n)		((1<<(n))-1)
#define	IN_RANGE(v, n)	((-(1<<((n)-1))) <= (v) && (v) < (1<<((n)-1)))
#define	ALIGN(x, a)	((a) == 0 ? (int)(x) : \
			(((int)(x) + (a) - 1) & ~((a) - 1)))

#define	roundup		ALIGN

/*
 * This function must be inlined, since we call it before we
 * have relocated ourselves.
 */
extern void doflush();

/*
 * Boot transfers control here. At this point,
 * we haven't relocated our own symbols, so the
 * world (as we know it) is pretty small right now.
 */
void
_kobj_boot(romp, dvec, bootops, ebp)
	void *romp;
	struct debugvec *dvec;
	struct bootops *bootops;
	Elf32_Boot *ebp;
{
	Elf32_Shdr *section[24];	/* cache */
	val_t bootaux[BA_NUM];
	Elf32_Phdr *phdr;
	auxv_t *auxv = NULL;
	u_int sh, sh_num, sh_size;
	u_int end, edata = 0;
	int i;

	for (i = 0; i < BA_NUM; i++)
		bootaux[i].ba_val = NULL;
	/*
	 * Check the bootstrap vector.
	 */
	for (; ebp->eb_tag != EB_NULL; ebp++) {
		switch (ebp->eb_tag) {
		case EB_AUXV:
			auxv = (auxv_t *)ebp->eb_un.eb_ptr;
			break;
		case EB_DYNAMIC:
			bootaux[BA_DYNAMIC].ba_ptr = (void *)ebp->eb_un.eb_ptr;
			break;
		}
	}
	if (auxv == NULL)
		return;
	/*
	 * Now the aux vector.
	 */
	for (; auxv->a_type != AT_NULL; auxv++) {
		switch (auxv->a_type) {
		case AT_PHDR:
			bootaux[BA_PHDR].ba_ptr = auxv->a_un.a_ptr;
			break;
		case AT_PHENT:
			bootaux[BA_PHENT].ba_val = auxv->a_un.a_val;
			break;
		case AT_PHNUM:
			bootaux[BA_PHNUM].ba_val = auxv->a_un.a_val;
			break;
		case AT_PAGESZ:
			bootaux[BA_PAGESZ].ba_val = auxv->a_un.a_val;
			break;
		case AT_SUN_LDELF:
			bootaux[BA_LDELF].ba_ptr = auxv->a_un.a_ptr;
			break;
		case AT_SUN_LDSHDR:
			bootaux[BA_LDSHDR].ba_ptr = auxv->a_un.a_ptr;
			break;
		case AT_SUN_LDNAME:
			bootaux[BA_LDNAME].ba_ptr = auxv->a_un.a_ptr;
			break;
		case AT_SUN_LPAGESZ:
			bootaux[BA_LPAGESZ].ba_val = auxv->a_un.a_val;
			break;
		case AT_SUN_IFLUSH:
			bootaux[BA_IFLUSH].ba_val = auxv->a_un.a_val;
			break;
		case AT_ENTRY:
			bootaux[BA_ENTRY].ba_ptr = auxv->a_un.a_ptr;
			break;
		}
	}
	for (i = 0; i < BA_NUM; i++) {
		if (i == BA_LPAGESZ || i == BA_BSS || i == BA_IFLUSH)
			continue;
		/*
		 * Any other values are required
		 * and must have been supplied.
		 */
		if (bootaux[i].ba_val == NULL)
			return;
	}

	sh = (u_int)bootaux[BA_LDSHDR].ba_ptr;
	sh_num = ((Elf32_Ehdr *)bootaux[BA_LDELF].ba_ptr)->e_shnum;
	sh_size = ((Elf32_Ehdr *)bootaux[BA_LDELF].ba_ptr)->e_shentsize;
	/*
	 * Build cache table for section addresses.
	 */
	for (i = 0; i < sh_num; i++) {
		section[i] = (Elf32_Shdr *)sh;
		sh += sh_size;
	}
	/*
	 * Find the end of data
	 * (to allocate bss)
	 */
	phdr = (Elf32_Phdr *)bootaux[BA_PHDR].ba_ptr;
	for (i = 0; i < bootaux[BA_PHNUM].ba_val; i++) {
		if (phdr->p_type == PT_LOAD &&
		    (phdr->p_flags & PF_W) && (phdr->p_flags & PF_X)) {
			edata = end = phdr->p_vaddr + phdr->p_memsz;
			break;
		}
		phdr = (Elf32_Phdr *)((u_int)phdr + bootaux[BA_PHENT].ba_val);
	}
	if (edata == NULL)
		return;

	/*
	 * Find the symbol table, and then loop
	 * through the symbols adjusting their
	 * values to reflect where the sections
	 * were loaded.
	 */
	for (i = 1; i < sh_num; i++) {
		Elf32_Shdr *shp;
		Elf32_Sym *sp;
		u_int off;

		shp = section[i];
		if (shp->sh_type != SHT_SYMTAB)
			continue;

		for (off = 0; off < shp->sh_size; off += shp->sh_entsize) {
			sp = (Elf32_Sym *)(shp->sh_addr + off);

			if (sp->st_shndx == SHN_ABS ||
			    sp->st_shndx == SHN_UNDEF)
				continue;
			/*
			 * Assign the addresses for COMMON
			 * symbols even though we haven't
			 * actually allocated bss yet.
			 */
			if (sp->st_shndx == SHN_COMMON) {
				end = ALIGN(end, sp->st_value);
				sp->st_value = end;
				/*
				 * Squirrel it away for later.
				 */
				if (bootaux[BA_BSS].ba_val == 0)
					bootaux[BA_BSS].ba_val = end;
				end += sp->st_size;
				continue;
			} else if (sp->st_shndx > (Elf32_Half)sh_num)
				return;

			/*
			 * Symbol's new address.
			 */
			sp->st_value += section[sp->st_shndx]->sh_addr;
		}
	}
	/*
	 * Allocate bss for COMMON, if any.
	 */
	if (end > edata) {
		unsigned int va, bva;
		unsigned int asize;
		unsigned int align;

		if (bootaux[BA_LPAGESZ].ba_val) {
			asize = bootaux[BA_LPAGESZ].ba_val;
			align = bootaux[BA_LPAGESZ].ba_val;
		} else {
			asize = bootaux[BA_PAGESZ].ba_val;
			align = BO_NO_ALIGN;
		}
		va = roundup(edata, asize);
		bva = roundup(end, asize);

		if (bva > va) {
			bva = (unsigned int)BOP_ALLOC(bootops, (caddr_t)va,
				bva - va, align);
			if (bva == NULL)
				return;
		}
		/*
		 * Zero it.
		 */
		for (va = edata; va < end; va++)
			*(char *)va = 0;
		/*
		 * Update the size of data.
		 */
		phdr->p_memsz += (end - edata);
	}
	/*
	 * Relocate our own symbols.  We'll handle the
	 * undefined symbols later.
	 */
	for (i = 1; i < sh_num; i++) {
		Elf32_Shdr *rshp, *shp, *ssp;
		unsigned long baseaddr, reladdr, rend;
		int relocsize;

		rshp = section[i];

		if (rshp->sh_type != SHT_RELA)
			continue;
		/*
		 * Get the section being relocated
		 * and the symbol table.
		 */
		shp = section[rshp->sh_info];
		ssp = section[rshp->sh_link];

		reladdr = rshp->sh_addr;
		baseaddr = shp->sh_addr;
		rend = reladdr + rshp->sh_size;
		relocsize = rshp->sh_entsize;
		/*
		 * Loop through relocations.
		 */
		while (reladdr < rend) {
			Elf32_Sym *symref;
			Elf32_Rela *reloc;
			register unsigned long stndx;
			unsigned long off, *offptr;
			long addend, value;
			int rtype;

			reloc = (Elf32_Rela *)reladdr;
			off = reloc->r_offset;
			addend = (long)reloc->r_addend;
			rtype = ELF32_R_TYPE(reloc->r_info);
			stndx = ELF32_R_SYM(reloc->r_info);

			reladdr += relocsize;

			if (rtype == R_SPARC_NONE) {
				continue;
			}
			off += baseaddr;
			/*
			 * if R_SPARC_RELATIVE, simply add base addr
			 * to reloc location
			 */
			if (rtype == R_SPARC_RELATIVE) {
				value = baseaddr;
			} else {
				register unsigned int symoff, symsize;

				symsize = ssp->sh_entsize;

				for (symoff = 0; stndx; stndx--)
					symoff += symsize;
				symref = (Elf32_Sym *)(ssp->sh_addr + symoff);

				/*
				 * Check for bad symbol index.
				 */
				if (symoff > ssp->sh_size)
					return;
				/*
				 * Just bind our own symbols at this point.
				 */
				if (symref->st_shndx == SHN_UNDEF) {
					continue;
				}

				value = symref->st_value;
				if (ELF32_ST_BIND(symref->st_info) !=
				    STB_LOCAL) {
					/*
					 * If PC-relative, subtract ref addr.
					 */
					if (rtype == R_SPARC_PC10 ||
					    rtype == R_SPARC_PC22 ||
					    rtype == R_SPARC_DISP8 ||
					    rtype == R_SPARC_DISP16 ||
					    rtype == R_SPARC_DISP32 ||
					    rtype == R_SPARC_WPLT30 ||
					    rtype == R_SPARC_WDISP30 ||
					    rtype == R_SPARC_WDISP22 ||
					    rtype == R_SPARC_WDISP16 ||
					    rtype == R_SPARC_WDISP19)
						value -= off;
				}
			}
			if (rtype != R_SPARC_UA32 && (off & 3) != 0)
				return;
			offptr = (unsigned long *)off;
			/*
			 * insert value calculated at reference point
			 * 3 cases - normal byte order aligned, normal byte
			 * order unaligned, and byte swapped
			 * for the swapped and unaligned cases we insert value
			 * a byte at a time
			 */
			switch (rtype) {

			case R_SPARC_GLOB_DAT:  /* 32bit word aligned */
			case R_SPARC_RELATIVE:
			case R_SPARC_DISP32:
			case R_SPARC_32:
				/*
				 * 7/19/89 rmk adding current value of
				 * *offptr to value.  Should not be needed,
				 * since this is a RELA type and *offptr
				 * should be in addend
				 */
				*offptr = *offptr + value + addend;
				break;
			case R_SPARC_8:
			case R_SPARC_DISP8:
				value += addend;
				if (IN_RANGE(value, 8))
					*offptr |= value;
				else
					return;
				break;
			case R_SPARC_LO10:
			case R_SPARC_PC10:
				value += addend;
				value &= MASK(10);
				*offptr |= value;
				break;
			case R_SPARC_13:
				value += addend;
				if (IN_RANGE(value, 13))
					*offptr |= value;
				else
					return;
				break;
			case R_SPARC_16:
			case R_SPARC_DISP16:
				value += addend;
				if (IN_RANGE(value, 16))
					*offptr |= value;
				else
					return;
				break;
			case R_SPARC_22:
				value += addend;
				if (IN_RANGE(value, 22))
					*offptr |= value;
				else
					return;
				break;
			case R_SPARC_PC22:
				value += addend;
				value = (unsigned)value >> 10;
				if (IN_RANGE(value, 22))
					*offptr |= value;
				else
					return;
				break;
			case R_SPARC_WDISP22:
				value += addend;
				value = (unsigned)value >> 2;
				if (IN_RANGE(value, 22))
					*offptr |= value;
				else
					return;
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
			case R_SPARC_UA32: {
				union {
					unsigned long l;
					char c[4];
				} symval;

				symval.l = value + addend;

				((char *)off)[0] = symval.c[0];
				((char *)off)[1] = symval.c[1];
				((char *)off)[2] = symval.c[2];
				((char *)off)[3] = symval.c[3];
				break;
			}
			case R_SPARC_10:
				value += addend;
				if (IN_RANGE(value, 10))
					*offptr |= value;
				else
					return;
				break;
			case R_SPARC_11:
				value += addend;
				if (IN_RANGE(value, 11))
					*offptr |= value;
				else
					return;
				break;
			case R_SPARC_WDISP16:
				value += addend;
				value = (unsigned)value >> 2;
				if (IN_RANGE(value, 16)) {
					*offptr |= (((value & 0xc000) << 6)
							| (value & 0x3fff));
				} else
					return;
				break;
			case R_SPARC_WDISP19:
				value += addend;
				value = (unsigned)value >> 2;
				if (IN_RANGE(value, 19))
					*offptr |= value;
				else
					return;
				break;
			case R_SPARC_5:
				value += addend;
				if (IN_RANGE(value, 5))
					*offptr |= value;
				else
					return;
				break;
			case R_SPARC_6:
				value += addend;
				if (IN_RANGE(value, 6))
					*offptr |= value;
				else
					return;
				break;
			case R_SPARC_7:
				value += addend;
				if (IN_RANGE(value, 7))
					*offptr |= value;
				else
					return;
				break;
			default:
				return;
			}
			if (bootaux[BA_IFLUSH].ba_val)
				doflush(offptr);
			/*
			 * We only need to do it once.
			 */
			reloc->r_info = ELF32_R_INFO(stndx, R_SPARC_NONE);
		} /* while */
	}

	/*
	 * Done relocating all of our *defined*
	 * symbols, so we hand off.
	 */
	kobj_init(romp, dvec, bootops, bootaux);
}
