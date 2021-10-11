/*
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)segments.c	1.11	94/07/18 SMI"

/* LINTLIBRARY */

#include	"_debug.h"
#include	"libld.h"

/*
 * Print out a single `segment descriptor' entry.
 */
void
_Dbg_sg_desc_entry(int ndx, Sg_desc * sgp)
{
	dbg_print("");
	dbg_print("segment[%d] sg_name:  %s", ndx, (sgp->sg_name ?
		(*sgp->sg_name ? sgp->sg_name : Str_null) : Str_null));

	Elf_phdr_entry(&sgp->sg_phdr);

	dbg_print("    sg_length:    %#x", sgp->sg_length);
	dbg_print("    sg_flags:     %s",
		conv_segaflg_str(sgp->sg_flags));
	if (sgp->sg_sizesym && sgp->sg_sizesym->sd_name)
		dbg_print("    sg_sizesym:   %s",
			sgp->sg_sizesym->sd_name);
	if (sgp->sg_secorder.head) {
		Listnode *	lnp;
		Sec_order *	scop;

		dbg_print("    sec_order:");
		for (LIST_TRAVERSE(&sgp->sg_secorder, lnp, scop)) {
			dbg_print("       sec_name:    %-8s  sec_index:   %d",
			    scop->sco_secname, scop->sco_index);
		}
	}
}

void
Dbg_seg_title()
{
	if (DBG_NOTCLASS(DBG_SEGMENTS))
		return;

	dbg_print(Str_empty);
	dbg_print("Segment Descriptor List (in use)");
}

void
Dbg_seg_entry(int ndx, Sg_desc * sgp)
{
	if (DBG_NOTCLASS(DBG_SEGMENTS))
		return;

	_Dbg_sg_desc_entry(ndx, sgp);
}

/*
 * Print out the available segment descriptors.
 */
void
Dbg_seg_list(List * lsg)
{
	Listnode *	lnp;
	Sg_desc *	sgp;
	int		ndx = 1;

	if (DBG_NOTCLASS(DBG_SEGMENTS))
		return;

	dbg_print(Str_empty);
	dbg_print("Segment Descriptor List (available)");
	for (LIST_TRAVERSE(lsg, lnp, sgp))
		_Dbg_sg_desc_entry(ndx++, sgp);
}

/*
 * Print the output section information.  This includes the section header
 * information and the output elf buffer information.  If the detail flag is
 * set, traverse the input sections displaying all the input buffers that
 * have been concatenated to form this output buffer.
 */
void
Dbg_seg_os(Os_desc * osp, int ndx)
{
	Listnode *	lnp;
	Is_desc *	isp;

	if (DBG_NOTCLASS(DBG_SEGMENTS))
		return;

	dbg_print("  section[%d] os_name:  %s", ndx, osp->os_name);
	Elf_shdr_entry(osp->os_shdr);
	Elf_elf_data_title();
	_Dbg_elf_data_out(osp);

	if (DBG_NOTDETAIL())
		return;

	for (LIST_TRAVERSE(&(osp->os_isdescs), lnp, isp))
		_Dbg_elf_data_in(osp, isp);
}
