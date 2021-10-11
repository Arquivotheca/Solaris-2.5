/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)elf.c	1.2	94/09/17 SMI"

/* LINTLIBRARY */

/*
 * String conversion routines for ELF header attributes.
 */
#include	<stdio.h>
#include	"_libconv.h"

const char *
conv_eclass_str(Byte class)
{
	static char		string[STRSIZE] = { '\0' };
	static const char *	classes[] = {
		"ELFCLASSNONE",		"ELFCLASS32",	"ELFCLASS64"
		};

	if (class > ELFCLASSNUM) {
		(void) sprintf(string, format, class);
		return ((const char *) string);
	} else
		return (classes[class]);

}

const char *
conv_edata_str(Byte data)
{
	static char		string[STRSIZE] = { '\0' };
	static const char *	datas[] = {
		"ELFDATANONE",		"ELFDATA2LSB", 	"ELFDATA2MSB"
		};

	if (data > ELFDATANUM) {
		(void) sprintf(string, format, data);
		return ((const char *) string);
	} else
		return (datas[data]);

}

const char *
conv_emach_str(Half machine)
{
	static char		string[STRSIZE] = { '\0' };
	static const char *	machines[] = {
		"EM_NONE",	"EM_M32",	"EM_SPARC",
		"EM_386",	"EM_68K",	"EM_88K",
		"EM_486",	"EM_860",	"EM_MIPS",
		"EM_UNKNOWN9",	"EM_MIPS_RS3_LE", "EM_RS6000",
		"EM_UNKNOWN12",	"EM_UNKNOWN13",	"EM_UNKNOWN14",
		"EM_PA_RISC",	"EM_nCUBE",	"EM_VPP500",
		"EM_SPARC32PLUS"
		};

	if (machine > EM_NUM) {
		(void) sprintf(string, format, machine);
		return ((const char *) string);
	} else
		return (machines[machine]);

}

const char *
conv_etype_str(Half etype)
{
	static char		string[STRSIZE] = { '\0' };
	static const char *	etypes[] = {
		"ET_NONE",	"ET_REL",	"ET_EXEC",
		"ET_DYN",	"ET_CORE",	"ET_NUM"
		};

	if (etype > ET_NUM) {
		(void) sprintf(string, format, etype);
		return ((const char *) string);
	} else
		return (etypes[etype]);
}

const char *
conv_ever_str(Word version)
{
	static char		string[STRSIZE] = { '\0' };
	static const char *	versions[] = {
		"EV_NONE",	"EV_CURRENT"
		};

	if (version > EV_NUM) {
		(void) sprintf(string, format, (int)version);
		return ((const char *) string);
	} else
		return (versions[version]);
}
