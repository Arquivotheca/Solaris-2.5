/*
 *	Copyright (c) 1988 AT&T
 *	  All Rights Reserved
 *
 *	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T
 *	The copyright notice above does not evidence any
 *	actual or intended publication of such source code.
 *
 *	Copyright (c) 1991,1992 by Sun Microsystems, Inc.
 */
#pragma ident	"@(#)entry.c	1.8	94/11/09 SMI"

#include	<stdio.h>
#include	<string.h>
#include	"_ld.h"


/*
 * Print a virtual address map of input and output sections together with
 * multiple symbol definitions (if they exist).
 */
static Boolean	symbol_title = TRUE;

static void
sym_mulref_title()
{
	(void) printf("\n\n\t\tMULTIPLY DEFINED SYMBOLS\n\n\n");
	(void) printf("symbol\t\t\t\t  definition used"
		"     also defined in\n\n");

	symbol_title = FALSE;
}

void
ldmap_out(Ofl_desc * ofl)
{
	Listnode *	lnp1, * lnp2, * lnp3;
	Os_desc *	osp;
	Sg_desc *	sgp;
	Is_desc *	isp;
	Word		bkt;
	Sym_desc *	sdp;

	(void) printf("\t\tLINK EDITOR MEMORY MAP\n\n");
	if (ofl->ofl_flags & FLG_OF_RELOBJ) {
		(void) printf("\noutput\t\tinput\t\tnew");
		(void) printf("\nsection\t\tsection\t\tdisplacement\tsize\n\n");
	} else {
		(void) printf("\noutput\t\tinput\t\tvirtual");
		(void) printf("\nsection\t\tsection\t\taddress\t\tsize\n\n");
	}

	for (LIST_TRAVERSE(&ofl->ofl_segs, lnp1, sgp)) {
		if (sgp->sg_phdr.p_type != PT_LOAD)
			continue;
		for (LIST_TRAVERSE(&(sgp->sg_osdescs), lnp2, osp)) {
			(void) printf("%-8.8s\t\t\t%08.2lx\t%08.2lx\n",
				osp->os_name, osp->os_shdr->sh_addr,
				osp->os_shdr->sh_size);
			for (LIST_TRAVERSE(&(osp->os_isdescs), lnp3, isp)) {
			    Addr	addr;

			    addr = isp->is_indata->d_off;
			    if (!(ofl->ofl_flags & FLG_OF_RELOBJ))
				addr += isp->is_osdesc->os_shdr->sh_addr;

			    (void) printf("\t\t%-8.8s\t%08.2lx\t%08.2lx %s\n",
				isp->is_name, addr, isp->is_shdr->sh_size,
				((isp->is_file != NULL) ?
				(char *)(isp->is_file->ifl_name) :
				(const char *)"(null)"));
			}
		}
	}

	if (ofl->ofl_flags & FLG_OF_RELOBJ)
		return;

	/*
	 * Check for any multiply referenced symbols (ie. symbols that have
	 * been overridden from a shared library).
	 */
	for (bkt = 0; bkt < ofl->ofl_symbktcnt; bkt++) {
		Sym_cache *	scp;

		for (scp = ofl->ofl_symbkt[bkt]; scp; scp = scp->sc_next) {
			for (sdp = (Sym_desc *)(scp + 1);
			    sdp < scp->sc_free; sdp++) {
				const char *	name, * ducp, * adcp;
				List *		dfiles;
				const char *	fform = "%-35s %s\n";
				const char *	cform = "\t\t\t\t\t\t\t%s\n";

				name = sdp->sd_name;
				dfiles = &sdp->sd_aux->sa_dfiles;
				/*
				 * Files that define a symbol are saved on the
				 * `sa_dfiles' list, if the head and tail of
				 * this list differ there must have been more
				 * than one symbol definition.  Ignore symbols
				 * that aren't needed, and any special symbols
				 * that the link editor may produce (symbols of
				 * type ABS and COMMON are not recorded in the
				 * first place, however functions like _init()
				 * and _fini() commonly have multiple
				 * occurrances).
				 */
				if ((sdp->sd_ref == REF_DYN_SEEN) ||
				    (dfiles->head == dfiles->tail) ||
				    (sdp->sd_aux && sdp->sd_aux->sa_symspec) ||
				    (strcmp(Fini_usym, name) == 0) ||
				    (strcmp(Init_usym, name) == 0) ||
				    (strcmp(Libv_usym, name) == 0))
					continue;

				if (symbol_title)
					sym_mulref_title();

				ducp = sdp->sd_file->ifl_name;
				(void) printf(fform, name, ducp);
				for (LIST_TRAVERSE(dfiles, lnp2, adcp)) {
					/*
					 * Ignore the referenced symbol.
					 */
					if (strcmp(adcp, ducp) != 0)
						(void) printf(cform, adcp);
				}
			}
		}
	}
}

/*
 * Traverse the entrance criteria list searching for those sections that haven't
 * been met and print error message.  (only in the case of reordering)
 */
void
ent_check(Ofl_desc * ofl)
{
	Listnode *	lnp;
	Ent_desc *	enp;

	/*
	 *  Try to give as much information to the user about the specific
	 *  line in the mapfile.  If the line contains a file name then
	 *  output the filename too.  Hence we have two warning lines -
	 *  one for criterias where a filename is used and the other
	 *  for those without a filename.
	 */
	for (LIST_TRAVERSE(&ofl->ofl_ents, lnp, enp)) {
		if ((enp->ec_segment->sg_flags & FLG_SG_ORDER) &&
		    !(enp->ec_flags & FLG_EC_USED) && enp->ec_ndx) {
			Listnode *	_lnp = enp->ec_files.head;

			if ((_lnp != NULL) && (_lnp->data != NULL) &&
			    (char *)(_lnp->data) != NULL) {
				eprintf(ERR_WARNING, "mapfile: %s segment: "
				    "section `%s' does not appear in file `%s'",
				    enp->ec_segment->sg_name, enp->ec_name,
				    (const char *)(_lnp->data));
			} else {
				eprintf(ERR_WARNING, "mapfile: %s segment: "
				    "section `%s' does not appear in any "
				    "input file", enp->ec_segment->sg_name,
				    enp->ec_name);
			}
		}
	}
}
