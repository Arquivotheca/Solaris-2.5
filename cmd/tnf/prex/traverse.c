/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)traverse.c 1.25 94/09/08 SMI"

/*
 * Includes
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/procfs.h>
#include <sys/stat.h>

#include "prbutlp.h"
#include "dbg.h"


/*
 * Typedefs
 */

typedef struct obj_id {
	dev_t		   oi_dev;
	ino_t		   oi_ino;
	struct obj_id  *oi_next_p;

}			   obj_id_t;


/* ---------------------------------------------------------------- */
/* ----------------------- Public Functions ----------------------- */
/* ---------------------------------------------------------------- */

/*
 * prb_traverse_process() - traverses all the mappings in a process, calling
 * the supplied function on each.
 */

prb_status_t
prb_traverse_process(int procfd,
	prb_elf_search_t * search_info_p)
{
	int			 nmaps = 0;
	caddr_t		*addrs_p = NULL;
	size_t		 *sizes_p = NULL;
	long		   *mflags_p = NULL;
	int			 i;
	prb_status_t	prbstat = PRB_STATUS_OK;

	prbstat = prb_proc_mappings(procfd, &nmaps, &addrs_p,
		&sizes_p, &mflags_p);
	if (prbstat)
		return (prbstat);
#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"prb_traverse_process: checking %d mappings\n",
			nmaps);
#endif


	search_info_p->mapping_data = NULL;

	/* call mapping function on all maps */
	for (i = 0; i < nmaps; i++) {
#ifdef DEBUG
		if (__prb_verbose >= 3) {
			char			flagstr[32];

			flagstr[0] = '\0';
			(void) strcat(flagstr,
				(mflags_p[i] & MA_READ) ? " RD" : " --");
			(void) strcat(flagstr,
				(mflags_p[i] & MA_WRITE) ? " WR" : " --");
			(void) strcat(flagstr,
				(mflags_p[i] & MA_EXEC) ? " EX" : " --");
			(void) strcat(flagstr,
				(mflags_p[i] & MA_SHARED) ? " SH" : " --");
			(void) strcat(flagstr,
				(mflags_p[i] & MA_BREAK) ? " BK" : " --");
			(void) strcat(flagstr,
				(mflags_p[i] & MA_STACK) ? " ST" : " --");

			(void) fprintf(stderr,
				"  prb_traverse_mapping: 0x%08x 0x%06x %s\n",
				(unsigned) addrs_p[i], sizes_p[i], flagstr);
		}
#endif
		prbstat = search_info_p->mapping_func(procfd,
			addrs_p[i], sizes_p[i],
			mflags_p[i], search_info_p);
		if (prbstat)
			break;
	}

	if (addrs_p)
		free(addrs_p);
	if (sizes_p)
		free(sizes_p);
	if (mflags_p)
		free(mflags_p);

	return (prbstat);

}				/* end prb_traverse_process */


/*
 * prb_traverse_mapping() - function to be called on each mapping in a
 * process.  This function differs from prb_traverse_mapobj in that duplicate
 * mappings of the same object are traversed.
 */

/*ARGSUSED*/
prb_status_t
prb_traverse_mapping(int procfd,
	caddr_t addr,
	size_t size,
	long mflags,
	prb_elf_search_t * search_info_p)
{
	prb_status_t	prbstat = PRB_STATUS_OK;
	caddr_t		 vaddr = addr;
	int			 objfd;
	prb_lmap_entry_t *entry_p;

#ifdef OLD
	/*
	 * * #### NOT SURE WHY THIS WORKS - we know that if we demand
	 * mappings * of MA_READ and MA_EXEC we avoid trouble with improperly
	 * aliased * mappings.  But we're not sure why ...
	 */
	if (mflags != (MA_READ | MA_EXEC))
		return (PRB_STATUS_OK);
#endif

	/* Make sure this mapping is in the link map */
	prbstat = prb_lmap_find(procfd, addr, &entry_p);
	switch (prbstat) {
	case PRB_STATUS_NOTINLMAP:	/* normal case, not in link map */
		return (PRB_STATUS_OK);

	case PRB_STATUS_OK:	/* normal case, in link map */
		break;

	default:		/* error cases */
		return (prbstat);
	}

	/* "publish" the link map entry for the later traversal levels */
	search_info_p->mapping_data = (void *) entry_p;

	/* open a file descriptor on the object */
again:
	objfd = ioctl(procfd, PIOCOPENM, &vaddr);
	if (objfd < 0) {
		if (errno == EINTR)
			goto again;

		/* its OK to get an EINVAL, just skip this puppy */
		if (errno == EINVAL) {
#ifdef DEBUG
			if (__prb_verbose >= 3)
				(void) fprintf(stderr,
					"  prb_traverse_mapping: "
					"invalid mapping at "
					"0x%x\n", (unsigned) addr);
#endif
			return (PRB_STATUS_OK);
		}
		DBG(perror("prb_traverse_mapping: PIOCOPENM failed"));
		return (prb_status_map(errno));
	}
	/* traverse all the sections in the object */
	prbstat = search_info_p->object_func(objfd, addr, search_info_p);

	/* its OK if that wasn't an elf object, just go on ... */
	if (prbstat == PRB_STATUS_BADELFOBJ)
		prbstat = PRB_STATUS_OK;

Cleanup:
	(void) close(objfd);
	return (prbstat);

}				/* end prb_traverse_mapping */


#ifdef OLD
/*
 * prb_traverse_mapobj() - function to be called on each mapped object in a
 * process.  This function differs from prb_traverse_mapping in that
 * subsequent mappings of the same object are skipped.
 */

prb_status_t
prb_traverse_mapobj(int procfd,
			caddr_t addr,
			size_t size,
			long mflags,
			prb_elf_search_t * search_info_p)
{
	prb_status_t	prbstat = PRB_STATUS_OK;
	caddr_t		 vaddr = addr;
	int			 objfd;
	struct stat	 statbuf;
	obj_id_t	   *objid_p;
	prb_lmap_entry_t *entry_p;

	/* Make sure this mapping is in the link map */
	prbstat = prb_lmap_find(procfd, addr, &entry_p);
	switch (prbstat) {
	case PRB_STATUS_NOTINLMAP:	/* normal case, not in link map */
		return (PRB_STATUS_OK);

	case PRB_STATUS_OK:	/* normal case, in link map */
		break;

	default:		/* error cases */
		return (prbstat);
	}

	/* open a file descriptor on the object */
again:
	objfd = ioctl(procfd, PIOCOPENM, &vaddr);
	if (objfd < 0) {
		if (errno == EINTR)
			goto again;

		/* its OK to get an EINVAL, just skip this puppy */
		if (errno == EINVAL) {
#ifdef DEBUG
			if (__prb_verbose >= 3)
				(void) fprintf(stderr,
					"  prb_traverse_mapobj: "
					"invalid mapping at "
					"0x%x\n", (unsigned) addr);
#endif
			return (PRB_STATUS_OK);
		}
		DBG(perror("prb_traverse_mapobj: PIOCOPENM failed"));
		return (prb_status_map(errno));
	}
	/* stat the file to make sure we haven't seen it already */
	if (fstat(objfd, &statbuf) != 0) {
		DBG(perror("prb_traverse_mapobj: fstat failed"));
		return (prb_status_map(errno));
	}
	for (objid_p = (obj_id_t *) search_info_p->mapping_data; objid_p;
		objid_p = objid_p->oi_next_p) {
		if (statbuf.st_dev == objid_p->oi_dev &&
			statbuf.st_ino == objid_p->oi_ino) {
#ifdef DEBUG
			if (__prb_verbose >= 3)
				(void) fprintf(stderr,
					"  prb_traverse_mapobj: "
					"duplicate mapping at "
					"0x%x\n", (unsigned) addr);
#endif
			goto Cleanup;
		}
	}

	/* must be a new object for us, remember it for the future */
	objid_p = (obj_id_t *) malloc(sizeof (obj_id_t));
	if (!objid_p) {
		DBG((void) fprintf(stderr,
			"prb_traverse_mapobj: malloc failed\n"));
		prbstat = PRB_STATUS_ALLOCFAIL;
		goto Cleanup;
	}
	objid_p->oi_dev = statbuf.st_dev;
	objid_p->oi_ino = statbuf.st_ino;
	objid_p->oi_next_p = search_info_p->mapping_data;
	search_info_p->mapping_data = objid_p;

	/* traverse all the sections in the object */
	prbstat = search_info_p->object_func(objfd, addr, search_info_p);

	/* its OK if that wasn't an elf object, just go on ... */
	if (prbstat == PRB_STATUS_BADELFOBJ)
		prbstat = PRB_STATUS_OK;

Cleanup:
	(void) close(objfd);
	return (prbstat);

}				/* end prb_traverse_mapobj */
#endif


/*
 * prb_traverse_object() - traverses all of the elf sections in an object,
 * calling the supplied function on each.
 */

prb_status_t
prb_traverse_object(int objfd,
			caddr_t addr,
			prb_elf_search_t * search_info_p)
{
	Elf			*elf;
	Elf32_Ehdr	 *ehdr;
	char		   *strs;
	Elf32_Shdr	 *shdr;
	Elf_Data	   *data;
	u_int		   idx;
	prb_status_t	prbstat = PRB_STATUS_OK;

#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"	prb_traverse_object: vaddr=0x%08x",
			(unsigned) addr);
#endif

	if (elf_version(EV_CURRENT) == EV_NONE)
		return (PRB_STATUS_BADELFVERS);

	/* open elf descriptor on the fd */
	elf = elf_begin(objfd, ELF_C_READ, NULL);
	if (elf == NULL || elf_kind(elf) != ELF_K_ELF) {
#ifdef DEBUG
		if (__prb_verbose >= 3)
			(void) fprintf(stderr, "; not elf object\n");
#endif
		return (PRB_STATUS_BADELFOBJ);
	}
	/* get the elf header */
	if ((ehdr = elf32_getehdr(elf)) == NULL) {
		DBG((void) fprintf(stderr,
			"prb_traverse_object: elf32_getehdr failed\n"));
		(void) elf_end(elf);
		return (PRB_STATUS_BADELFOBJ);
	}
	if ((ehdr->e_type != ET_EXEC) && (ehdr->e_type != ET_DYN)) {
		DBG((void) fprintf(stderr,
			"prb_traverse_object: not an "
			"executable or a shared object\n"));
		(void) elf_end(elf);
		return (PRB_STATUS_BADELFOBJ);
	}
	/* if an executable file, the base address is 0 */
	if (ehdr->e_type == ET_EXEC)
		addr = (caddr_t) 0;
	/* get a pointer to the elf header string table */
	strs = elf_strptr(elf, ehdr->e_shstrndx, NULL);

#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr, "; found %d sections\n",
			ehdr->e_shnum);
#endif

	for (idx = 1; idx < ehdr->e_shnum; idx++) {
		Elf_Scn		*scn;

		if ((scn = elf_getscn(elf, idx)) == NULL) {
			DBG((void) fprintf(stderr,
				"prb_traverse_object: elf_getscn failed\n"));
			prbstat = PRB_STATUS_BADELFOBJ;
			break;
		}
		if ((shdr = elf32_getshdr(scn)) == NULL) {
			DBG((void) fprintf(stderr,
				"prb_traverse_object:elf32_getshdr failed\n"));
			prbstat = PRB_STATUS_BADELFOBJ;
			break;
		}
		/* After adding i386 support: need to accept REL and RELA */
#if 0
		/* optimization if set - narrow search to requested sections */
		if (search_info_p->object_data) {
			if (shdr->sh_type != (Elf32_Word)
				search_info_p->object_data) {
				prbstat = PRB_STATUS_OK;
				continue;
			}
		}
#endif

		if ((data = elf_getdata(scn, NULL)) == NULL) {
			DBG((void) fprintf(stderr,
				"prb_traverse_object:elf32_getdata failed\n"));
			prbstat = PRB_STATUS_BADELFOBJ;
			break;
		}
		/* call the supplied function */
		prbstat = search_info_p->section_func(elf,
			strs, scn, shdr, data, addr, search_info_p);
		if (prbstat)
			break;
	}

	(void) elf_end(elf);

	return (prbstat);

}				/* end prb_traverse_object */


/*
 * prb_traverse_rela() - this function traverses a .rela section calling the
 * supplied function on each relocation record.
 */

/*ARGSUSED*/
prb_status_t
prb_traverse_rela(Elf * elf,
	char *strs,
	Elf_Scn * rel_scn,
	Elf32_Shdr * rel_shdr,
	Elf_Data * rel_data,
	caddr_t baseaddr,
	prb_elf_search_t * search_info_p)
{
	char		   *scn_name;
	Elf_Scn		*sym_scn;
	Elf32_Shdr	 *sym_shdr;
	Elf_Data	   *sym_data;
	Elf32_Sym	  *sym_table;
	Elf_Scn		*str_scn;
	Elf32_Shdr	 *str_shdr;
	Elf_Data	   *str_data;
	char		   *str_table;
	unsigned		nrels;
	unsigned		i;
	boolean_t	   isrela;
	char		   *ptr;

	/* bail if this isn't a rela (or rel) section */
	scn_name = strs + rel_shdr->sh_name;

#ifdef DEBUG
	if (__prb_verbose >= 4)
		(void) fprintf(stderr,
			"	  prb_traverse_rela: section "
			"named \"%s\"\n", scn_name);
#endif

	if (rel_shdr->sh_type == SHT_RELA &&
		(strcmp(scn_name, ".rela.data") == 0)) {
		isrela = B_TRUE;
	} else if (rel_shdr->sh_type == SHT_REL &&
		(strcmp(scn_name, ".rel.data") == 0)) {
		isrela = B_FALSE;
	} else
		return (PRB_STATUS_OK);

	/* find the symbol table section associated with this rela section */
	sym_scn = elf_getscn(elf, rel_shdr->sh_link);
	if (sym_scn == NULL) {
		DBG((void) fprintf(stderr,
			"prb_traverse_rela:elf_getscn (sym) failed\n"));
		return (PRB_STATUS_BADELFOBJ);
	}
	sym_shdr = elf32_getshdr(sym_scn);
	if (sym_shdr == NULL) {
		DBG((void) fprintf(stderr,
			"prb_traverse_rela:elf32_getshdr (sym) failed\n"));
		return (PRB_STATUS_BADELFOBJ);
	}
	sym_data = elf_getdata(sym_scn, NULL);
	if (sym_data == NULL) {
		DBG((void) fprintf(stderr,
			"prb_traverse_rela:elf_getdata (sym) failed\n"));
		return (PRB_STATUS_BADELFOBJ);
	}
	sym_table = (Elf32_Sym *) sym_data->d_buf;

	/* find the string table associated with the symbol table */
	str_scn = elf_getscn(elf, sym_shdr->sh_link);
	if (str_scn == NULL) {
		DBG((void) fprintf(stderr,
			"prb_traverse_rela:elf_getscn (str) failed\n"));
		return (PRB_STATUS_BADELFOBJ);
	}
	str_shdr = elf32_getshdr(str_scn);
	if (str_shdr == NULL) {
		DBG((void) fprintf(stderr,
			"prb_traverse_rela:elf32_getshdr (str) failed\n"));
		return (PRB_STATUS_BADELFOBJ);
	}
	str_data = elf_getdata(str_scn, NULL);
	if (str_data == NULL) {
		DBG((void) fprintf(stderr,
			"prb_traverse_rela: elf_getdata (str) failed\n"));
		return (PRB_STATUS_BADELFOBJ);
	}
	str_table = (char *) str_data->d_buf;

	/* loop over each relocation record */
	nrels = rel_shdr->sh_size / rel_shdr->sh_entsize;
#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"	  prb_traverse_rela: found %d relocations\n",
			nrels);
#endif

	ptr = rel_data->d_buf;
	for (i = 0; i < nrels; i++, ptr += (isrela) ? sizeof (Elf32_Rela) :
		sizeof (Elf32_Rel)) {
		Elf32_Word	  syminfo;
		Elf32_Sym	  *sym;
		Elf32_Addr	  offset;
		char		   *name;
		caddr_t		 addr;
		prb_status_t	prbstat;

		/* decode the r_info field of the relocation record */
		if (isrela) {
			Elf32_Rela	 *rela_p;

			/*LINTED pointer cast may result in improper alignment*/
			rela_p = (Elf32_Rela *) ptr;
			syminfo = ELF32_R_SYM(rela_p->r_info);
			offset = rela_p->r_offset;
		} else {
			Elf32_Rel	  *rel_p;

			/*LINTED pointer cast may result in improper alignment*/
			rel_p = (Elf32_Rel *) ptr;
			syminfo = ELF32_R_SYM(rel_p->r_info);
			offset = rel_p->r_offset;
		}

		/* find the associated symbol table entry */
		if (!syminfo)
			continue;
		sym = sym_table + syminfo;

		/* find the associated string table entry */
		if (!sym->st_name)
			continue;
		name = str_table + sym->st_name;
		addr = offset + baseaddr;

		prbstat = search_info_p->record_func(name,
			addr, ptr, search_info_p);
		if (prbstat)
			break;
	}

	return (PRB_STATUS_OK);

}				/* end prb_traverse_rela */


/*
 * prb_traverse_dynsym() - this function traverses a dynsym section calling
 * the supplied function on each symbol.
 */

/*ARGSUSED*/
prb_status_t
prb_traverse_dynsym(Elf * elf,
			char *elfstrs,
			Elf_Scn * scn,
			Elf32_Shdr * shdr,
			Elf_Data * data,
			caddr_t baseaddr,
			prb_elf_search_t * search_info_p)
{
	int			 nsyms;
	char		   *strs;
	int			 i;
	prb_status_t	prbstat;
	Elf32_Sym	  *syms;

	/* bail if this isn't a dynsym section */
	if (shdr->sh_type != SHT_DYNSYM)
		return (PRB_STATUS_OK);

	syms = data->d_buf;
	nsyms = shdr->sh_size / shdr->sh_entsize;
	strs = elf_strptr(elf, shdr->sh_link, 0);

#ifdef DEBUG
	if (__prb_verbose >= 3)
		(void) fprintf(stderr,
			"	  prb_traverse_dynsyn: found %d symbols\n",
			nsyms);
#endif

	for (i = 0; i < nsyms; i++) {
		Elf32_Sym	  *sym = &syms[i];
		char		   *name;
		caddr_t		 addr;

		name = strs + sym->st_name;
		addr = baseaddr + sym->st_value;

		prbstat = search_info_p->record_func(name,
			addr, sym, search_info_p);
		if (prbstat)
			break;
	}

	return (prbstat);

}				/* end prb_traverse_dynsym */


/*
 * prb_fill_search_info - fills up a search block with some default
 * functions.
 */

prb_status_t
prb_fill_search_info(prb_elf_search_t * search_p)
{
	(void) memset(search_p, 0, sizeof (*search_p));
	search_p->process_func = prb_traverse_process;
	search_p->mapping_func = prb_traverse_mapping;
	search_p->object_func = prb_traverse_object;

	return (PRB_STATUS_OK);
}
