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
#pragma ident	"@(#)place.c	1.17	95/03/03 SMI"

/* LINTLIBRARY */

/*
 * Map file parsing and input section to output segment mapping.
 */
#include	<string.h>
#include	"debug.h"
#include	"_libld.h"

static const char
	* Errmsg_toaf = "%s: non-allocatable section %s directed to a "
			"loadable segment";

/*
 * This function is used to determine if section ordering is turned
 * on and if so it will return the appropriate os_txtndx.
 * This information is derived from the Sg_desc->sg_segorder
 * List that was built up from the Mapfile.
 */
static int
set_os_txtndx(Is_desc *isp, Sg_desc * sgp)
{
	Listnode *	lnp;
	Sec_order *	scop;

	for (LIST_TRAVERSE(&sgp->sg_secorder, lnp, scop)) {
		if (strcmp(scop->sco_secname, isp->is_name) == 0) {
			scop->sco_flags |= FLG_SGO_USED;
			return (scop->sco_index);
		} /* if */
	} /* for */
	return (0);
} /* set_os_txtndx() */



/*
 * Place a section into the appropriate segment.
 */
Os_desc *
place_section(Ofl_desc * ofl, Is_desc * isp, int ident)
{
	Listnode *	lnp1, * lnp2;
	Ent_desc *	enp;
	Sg_desc	*	sgp;
	Os_desc	*	osp;
	Shdr *		shdr;
	int		os_ndx = 0;

	/*
	 * Traverse the entrance criteria list searching for a segment that
	 * matches the input section we have.  If an entrance criterion is set
	 * then there must be an exact match.  If we complete the loop without
	 * finding a segment, then sgp will be NULL.
	 */
	DBG_CALL(Dbg_sec_in(isp));
	sgp = NULL;
	shdr = isp->is_shdr;
	for (LIST_TRAVERSE(&ofl->ofl_ents, lnp1, enp)) {
		if (enp->ec_type && (enp->ec_type != shdr->sh_type))
			continue;
		if (enp->ec_attrmask &&
		    (enp->ec_attrmask & enp->ec_attrbits) !=
		    (enp->ec_attrmask & shdr->sh_flags))
			continue;
		if (enp->ec_name && (strcmp(enp->ec_name, isp->is_name) != 0))
			continue;
		if (enp->ec_files.head) {
			char *	file;
			int	found = 0;

			if (isp->is_file == 0)
				continue;

			for (LIST_TRAVERSE(&(enp->ec_files), lnp2, file)) {
				const char *	name = isp->is_file->ifl_name;

				if (file[0] == '*') {
					const char *	basename;

					basename = strrchr(name, '/');
					if (basename == NULL)
						basename = name;
					else if (basename[1] != '\0')
						basename++;

					if (strcmp(&file[1], basename) == 0) {
						found++;
						break;
					}
				} else {
					if (strcmp(file, name) == 0) {
						found++;
						break;
					}
				}
			}
			if (!found)
				continue;
		}
		break;
	}

	if ((sgp = enp->ec_segment) == 0)
		sgp = ((Ent_desc *)(ofl->ofl_ents.tail->data))->ec_segment;

	/*
	 * Strip out the % from the section name in all cases except when '-r'
	 * is used without '-M', and '-r' is used with '-M' without
	 * the ?O flag.
	 */
	if (((ofl->ofl_flags & FLG_OF_RELOBJ) &&
	    (sgp->sg_flags & FLG_SG_ORDER)) ||
	    !(ofl->ofl_flags & FLG_OF_RELOBJ)) {
		char *	cp;

		if ((cp = strchr(isp->is_name, '%')) != NULL) {
			char *	name;
			int	size = cp - isp->is_name;

			if ((name = malloc(size + 1)) == 0)
				return ((Os_desc *)S_ERROR);
			(void) strncpy(name, isp->is_name, size);
			cp = name + size;
			*cp = '\0';
			isp->is_name = name;
		}
		isp->is_txtndx = enp->ec_ndx;
	}
	if (sgp->sg_flags & FLG_SG_ORDER)
		enp->ec_flags |= FLG_EC_USED;

	/*
	 * call the function set_os_txtndx() to set the
	 * os_txtndx field based upon the sg_segorder list that
	 * was built from a Mapfile.  If there is no match then
	 * ox_txtndx will be set to 0.
	 *
	 * for now this value will be held in os_ndx.
	 */
	os_ndx = set_os_txtndx(isp, sgp);

	/*
	 * Traverse the input section list for the output section we have been
	 * assigned.  If we find a matching section simply add this new section.
	 */
	lnp2 = NULL;
	for (LIST_TRAVERSE(&(sgp->sg_osdescs), lnp1, osp)) {
		Shdr *	_shdr = osp->os_shdr;

		if ((ident == osp->os_scnsymndx) &&
		    (shdr->sh_type == _shdr->sh_type) &&
		    (shdr->sh_flags == _shdr->sh_flags) &&
		    (ident != M_ID_REL) &&
		    (strcmp(isp->is_name, osp->os_name) == 0)) {
			/*
			 * If is_txtndx is 0 then this section was not
			 * seen in mapfile, so put it at the end.
			 * If is_txtndx is not 0 and ?O is turned on
			 * then check to see where this section should
			 * be inserted.
			 */
			if ((sgp->sg_flags & FLG_SG_ORDER) && isp->is_txtndx) {
				Listnode *	tlist;

				tlist = list_where(&(osp->os_isdescs),
				    isp->is_txtndx);
				if (tlist != NULL) {
					if (list_insert(&(osp->os_isdescs),
					    isp, tlist) == 0)
						return ((Os_desc *)S_ERROR);
				} else {
					if (list_prepend(&(osp->os_isdescs),
					    isp) == 0)
						return ((Os_desc *)S_ERROR);
				}
			} else
				if (list_append(&(osp->os_isdescs), isp) == 0)
					return ((Os_desc *)S_ERROR);
			isp->is_osdesc = osp;
			DBG_CALL(Dbg_sec_added(osp, sgp));
			return (osp);
		}

		/*
		 * check to see if we need to worry about section
		 * ordering.
		 */
		if (os_ndx) {
			if (osp->os_txtndx) {
				if (os_ndx < osp->os_txtndx)
					/* insert section here. */
					break;
				else {
					lnp2 = lnp1;
					continue;
				} /* else */
			} else {
				/* insert section here. */
				break;
			}
		} else if (osp->os_txtndx) {
			lnp2 = lnp1;
			continue;
		}

		/*
		 * If the new sections identifier is less than that of the
		 * present input section we need to insert the new section
		 * at this point.
		 */
		if (ident < osp->os_scnsymndx)
			break;
		lnp2 = lnp1;
	} /* for */

	/*
	 * We are adding a new output section.  Update the section header
	 * count and associated string size.
	 */
	ofl->ofl_shdrcnt++;
	ofl->ofl_shdrstrsz += strlen(isp->is_name) + 1;

	/*
	 * Create a new output section descriptor.
	 */
	if ((osp = (Os_desc *)calloc(sizeof (Os_desc), 1)) == 0)
		return ((Os_desc *)S_ERROR);
	if ((osp->os_shdr = (Shdr *)calloc(sizeof (Shdr), 1)) == 0)
		return ((Os_desc *)S_ERROR);
	osp->os_shdr->sh_type = shdr->sh_type;
	osp->os_shdr->sh_flags = shdr->sh_flags;
	osp->os_shdr->sh_entsize = shdr->sh_entsize;
	osp->os_name = isp->is_name;
	osp->os_txtndx = os_ndx;
	osp->os_sgdesc = sgp;

	/*
	 * If a non-allocatable section is going to be put into a loadable
	 * segment then turn on the allocate bit for this section and warn the
	 * user that we have done so.  This could only happen through the use
	 * of a mapfile.
	 */
	if (sgp->sg_phdr.p_type == PT_LOAD) {
		if (!(osp->os_shdr->sh_flags & SHF_ALLOC)) {
			eprintf(ERR_WARNING, Errmsg_toaf, ofl->ofl_name,
				osp->os_name);
			osp->os_shdr->sh_flags |= SHF_ALLOC;
		}
	}

	/*
	 * Retain this sections identifier for future comparisons when placing
	 * a section (after all sections have been processed this variable will
	 * be used to hold the sections symbol index as we don't need to retain
	 * the identifier any more).
	 */
	osp->os_scnsymndx = ident;

	if (list_append(&(osp->os_isdescs), isp) == 0)
		return ((Os_desc *)S_ERROR);
	DBG_CALL(Dbg_sec_created(osp, sgp));
	isp->is_osdesc = osp;
	if (lnp2) {
		if (list_insert(&(sgp->sg_osdescs), osp, lnp2) == 0)
			return ((Os_desc *)S_ERROR);
	} else {
		if (list_prepend(&(sgp->sg_osdescs), osp) == 0)
			return ((Os_desc *)S_ERROR);
	}
	return (osp);
}
