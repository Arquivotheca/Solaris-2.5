/*
 *	Copyright (c) 1995 by Sun Microsystems, Inc.
 *	All rights reserved.
 */
#pragma ident	"@(#)files.c	1.17	95/06/20 SMI"

/* LINTLIBRARY */

#include	<dlfcn.h>
#include	<string.h>
#include	<unistd.h>
#include	<fcntl.h>
#include	"_debug.h"
#include	"libld.h"

void
Dbg_file_generic(Ifl_desc * ifl)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print("file=%s  [ %s ]", ifl->ifl_name,
		conv_etype_str(ifl->ifl_ehdr->e_type));
}

void
Dbg_file_skip(const char * nname, const char * oname)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	if (oname)
		dbg_print("file=%s  skipped: already processed as %s",
			nname, oname);
	else
		dbg_print("file=%s  skipped: already processed", nname);
}

void
Dbg_file_reuse(const char * nname, const char * oname)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print("file=%s  reusing: originally processed as %s",
		nname, oname);
}

/*
 * This function doesn't test for any specific debugging category, thus it will
 * be generated for any debugging family (even `detail' is sufficient).
 */
void
Dbg_file_unused(const char * name)
{
	dbg_print(Str_empty);
	dbg_print("file=%s  unused: does not satisfy any references", name);
	dbg_print(Str_empty);
}

void
Dbg_file_archive(const char * name, int found)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print("file=%s  [ archive ] %s", name,
		found ? Str_again : Str_empty);
}

void
Dbg_file_analyze(const char * name, int mode)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print(Str_empty);
	dbg_print("file=%s;  analyzing  %s", name, conv_dlmode_str(mode));
}

static const char
	* Str_dyn = "    dynamic:  %#10x  base:  %#10x  size:   %#10x";

void
Dbg_file_aout(const char * name, unsigned long dynamic, unsigned long base,
	unsigned long size)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print("file=%s  [ AOUT ]; generating link map", name);
	dbg_print(Str_dyn, dynamic, base, size);
	dbg_print(Str_empty);
}

void
Dbg_file_elf(const char * name, unsigned long dynamic, unsigned long base,
	unsigned long size, unsigned long entry, unsigned long phdr,
	unsigned int phnum)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print("file=%s  [ ELF ]; generating link map", name);
	dbg_print(Str_dyn, dynamic, base, size);
	dbg_print("    entry:    %#10x  phdr:  %#10x  phnum:  %10d", entry,
	    phdr, phnum);
	dbg_print(Str_empty);
}

void
Dbg_file_ldso(const char * name, unsigned long dynamic, unsigned long base,
	unsigned long envp, unsigned long auxv)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print(Str_empty);
	dbg_print("file=%s  [ ELF ]", name);
	dbg_print("    dynamic:  %#10x  base:  %#10x", dynamic, base);
	dbg_print("    envp:     %#10x  auxv:  %#10x", envp, auxv);
	dbg_print(Str_empty);
}

void
Dbg_file_prot(const char * name, int prot)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print(Str_empty);
	dbg_print("file=%s;  modifying memory protections (%c PROT_WRITE)",
		name, (prot ? '+' : '-'));
}

void
Dbg_file_delete(const char * name)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print(Str_empty);
	dbg_print("file=%s;  deleting", name);
}

static const char
	* Str_ref = "    refcnt:   %10d  for referenced file=%s";

void
Dbg_file_ref(const char * ref, int count, Permit * permit, int prom)
{
	unsigned long	_cnt, cnt;
	unsigned long *	value;

	if (DBG_NOTCLASS(DBG_FILES))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print(Str_ref, count, ref);

	if (permit) {
		const char *	str;

		if (prom)
			str = "[ PROMISCUOUS ]";
		else
			str = "";

		cnt = permit->p_cnt;
		value = &permit->p_value[0];

		dbg_print("    permit:   0x%8.8x  %s", *value, str);
		for (_cnt = 1, value++; _cnt < cnt; _cnt++, value++)
			dbg_print("              0x%8.8x", *value);
	} else
		dbg_print("    permit:       (free)");
}

void
Dbg_file_bound(const char * ref, const char * def, int count)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print("file=%s;  references external symbols", ref);
	dbg_print(Str_ref, count, def);
}

void
Dbg_file_dlopen(const char * name, const char * from, int mode)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print(Str_empty);
	dbg_print("file=%s;  dlopen() called from file=%s  %s", name, from,
		conv_dlmode_str(mode));
}

void
Dbg_file_dlclose(const char * name)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print(Str_empty);
	dbg_print("file=%s;  dlclose()", name);
}

void
Dbg_file_nl()
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;
	if (DBG_NOTDETAIL())
		return;

	dbg_print(Str_empty);
}

void
Dbg_file_preload(const char * name)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print("file=%s;  preloaded", name);
}

void
Dbg_file_needed(const char * name, const char * parent)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print(Str_empty);
	dbg_print("file=%s;  needed by %s", name, parent);
}

void
Dbg_file_filter(const char * name, const char * filter)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print(Str_empty);
	dbg_print("file=%s;  filtered by %s", name, filter);
}

void
Dbg_file_fixname(const char * oname, const char * nname)
{
	if (DBG_NOTCLASS(DBG_FILES))
		return;

	dbg_print(Str_empty);
	dbg_print("file=%s;  required name=%s", oname, nname);
}

void
Dbg_file_output(Ofl_desc * ofl)
{
	const char *	prefix = "/tmp/ld.so-OBJECT-";
	char	*	oname, * nname, * ofile;
	int		fd;

	if (DBG_NOTCLASS(DBG_FILES))
		return;
	if (DBG_NOTDETAIL())
		return;

	/*
	 * Obtain the present input object filename for concatenation to the
	 * prefix name.
	 */
	oname = (char *)ofl->ofl_name;
	if ((ofile = strrchr(oname, '/')) == NULL)
		ofile = oname;
	else
		ofile++;

	/*
	 * Concatenate the prefix with the object filename, open the file and
	 * write out the present Elf memory image.  As this is debugging we
	 * ignore all errors.
	 */
	if ((nname = (char *)malloc(strlen(prefix) + strlen(ofile) + 1)) != 0) {
		(void) strcpy(nname, prefix);
		(void) strcat(nname, ofile);
		if ((fd = open(nname, O_RDWR | O_CREAT | O_TRUNC, 0666)) != -1)
			(void) write(fd, ofl->ofl_image, ofl->ofl_size);
	}
}
