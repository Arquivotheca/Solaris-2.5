/*
 *	Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma	ident	"@(#)ref.c	1.8	94/10/04 SMI"

#include "libtnf.h"

/*
 * Unoptimized versions, always dereference a cell through _GET_INT32()
 *
 */

#define	LONG_SIGN_BIT	0x80000000

static tnf_ref32_t *vaddr_to_phys(TNF *, tnf_ref32_t *, tnf_ref32_t);

/*
 * Maps a V-address to a physical address in the file.  Also checks
 * that reference is pointing at a target that is valid - i.e. the target
 * block has not been re-used.  Returns TNF_NULL if target is invalid.
 * Note: We don't have to check if the destination is within the
 * valid_bytes because the writer writes the destination first and then
 * the reference.  If the target program aborted in the middle of the
 * commitment, then the destination block may not be marked as committed,
 * but it is really there.
 */
static tnf_ref32_t *
vaddr_to_phys(TNF *tnf, tnf_ref32_t *src_cell, tnf_ref32_t src_val)
{
	tnf_ref32_t	v_addr;
	tnf_ref32_t	*phys_addr;
	tnf_int32_t	gen_delta;
	tnf_uint32_t	src_gen, dest_gen;

	v_addr = (tnf_ref32_t) (((char *)src_cell + src_val) -
					(char *)tnf->file_start);

	/*
	 * Calculate the generation delta to check the expected
	 * generation of destination.  ANSI C doesn't have a signed right
	 * shift, so it's slightly more convoluted
	 */
	gen_delta = ((unsigned)v_addr) >> tnf->generation_shift;
	if ((v_addr & LONG_SIGN_BIT) == LONG_SIGN_BIT) {
		/* sign bit was a 1 - so restore sign */
		gen_delta |= ((unsigned)tnf->address_mask <<
						(32 - tnf->generation_shift));
	}

	/* Map the V-address back to a physical address */
	phys_addr = (tnf_ref32_t *)((char *)tnf->file_start +
		/* LINTED pointer cast may result in improper alignment */
			(v_addr & tnf->address_mask));

	/* Get the generation numbers of src and dest block */
	/* LINTED pointer cast may result in improper alignment */
	src_gen = _GET_BLOCK_GENERATION(tnf, _GET_BLOCK(tnf, src_cell));
	/* LINTED pointer cast may result in improper alignment */
	dest_gen = _GET_BLOCK_GENERATION(tnf, _GET_BLOCK(tnf, phys_addr));

	/*
	 * if either the src or the destination is a tag block, there is
	 * no need to check for a valid pointer.
	 */
	if ((src_gen == (unsigned) TNF_TAG_GENERATION_NUM) ||
			(dest_gen == (unsigned) TNF_TAG_GENERATION_NUM))
		return (phys_addr);

	/* Make sure expected generation number is correct */
	if (((tnf_int32_t)src_gen + gen_delta) != dest_gen) {
		return (TNF_NULL);
	}

	return (phys_addr);
}


/*
 * Return the target referent of a cell, chasing forwarding references.
 * Return TNF_NULL if cell is a TNF_NULL forwarding reference.
 */

tnf_ref32_t *
_tnf_get_ref32(TNF *tnf, tnf_ref32_t *cell)
{
	tnf_ref32_t 	ref32, reftemp;

	ref32 = _GET_INT32(tnf, cell);

	if (TNF_REF32_IS_NULL(ref32))
		return (TNF_NULL);

	if (TNF_REF32_IS_RSVD(ref32)) {
		_tnf_error(tnf, TNF_ERR_BADREFTYPE);
		return (TNF_NULL);
	}

	if (TNF_REF32_IS_PAIR(ref32)) {
		/* We chase the high (tag) half */
		tnf_ref16_t	tag16;

		tag16 = TNF_REF32_TAG16(ref32);

		if (TNF_TAG16_IS_ABS(tag16)) {
			cell = (tnf_ref32_t *)
				((char *)tnf->file_start
/* LINTED pointer cast may result in improper alignment */
				+ TNF_TAG16_ABS16(tag16));
			ref32 = _GET_INT32(tnf, cell);

		} else if (TNF_TAG16_IS_REL(tag16)) {
			cell = vaddr_to_phys(tnf, cell,
					(tnf_ref32_t) TNF_TAG16_REF16(tag16));
			if (cell == TNF_NULL)
				return (TNF_NULL);
			ref32 = _GET_INT32(tnf, cell);

		} else {
			_tnf_error(tnf, TNF_ERR_BADREFTYPE);
			return (TNF_NULL);
		}

	} else if (TNF_REF32_IS_PERMANENT(ref32)) {
		/* permanent space pointer */
		reftemp = TNF_REF32_VALUE(ref32);
		reftemp = TNF_REF32_SIGN_EXTEND(reftemp);
		/* LINTED pointer cast may result in improper alignment */
		cell = (tnf_ref32_t *) ((char *)tnf->file_start + reftemp);
		ref32 = _GET_INT32(tnf, cell);

	} else {		/* full/tag reclaimable space reference */
		cell = vaddr_to_phys(tnf, cell, TNF_REF32_VALUE(ref32));
		if (cell == TNF_NULL)
			return (TNF_NULL);
		ref32 = _GET_INT32(tnf, cell);
	}

	/* chase intermediate forwarding references */
	while (ref32 && TNF_REF32_IS_FWD(ref32)) {
		if (TNF_REF32_IS_PERMANENT(ref32)) {
			reftemp = TNF_REF32_VALUE(ref32);
			reftemp = TNF_REF32_SIGN_EXTEND(reftemp);
			cell = (tnf_ref32_t *) ((char *)tnf->file_start +
		/* LINTED pointer cast may result in improper alignment */
							reftemp);

		} else {
			cell = vaddr_to_phys(tnf, cell, TNF_REF32_VALUE(ref32));
			if (cell == TNF_NULL)
				return (TNF_NULL);
		}
		ref32 = _GET_INT32(tnf, cell);
	}

	return (cell);
}

/*
 * Return the target referent of ref16 contained in cell.
 * Return TNF_NULL if cell doesn't have a ref16.
 */

tnf_ref32_t *
_tnf_get_ref16(TNF *tnf, tnf_ref32_t *cell)
{
	tnf_ref32_t 	ref32, reftemp;

	ref32 = _GET_INT32(tnf, cell);

	if (TNF_REF32_IS_PAIR(ref32)) {
		tnf_ref16_t	ref16;

		ref16 = TNF_REF32_REF16(ref32);

		if (TNF_REF16_VALUE(ref16) == TNF_NULL)
			/* No ref16 was stored */
			return (TNF_NULL);
		else {
			cell = vaddr_to_phys(tnf, cell,
					(tnf_ref32_t) TNF_REF16_VALUE(ref16));
			if (cell == TNF_NULL)
				return (TNF_NULL);
			ref32 = _GET_INT32(tnf, cell);
		}
	} else			/* not a pair pointer */
		return (TNF_NULL);

	/* chase intermediate forwarding references */
	while (ref32 && TNF_REF32_IS_FWD(ref32)) {
		if (TNF_REF32_IS_PERMANENT(ref32)) {
			reftemp = TNF_REF32_VALUE(ref32);
			reftemp = TNF_REF32_SIGN_EXTEND(reftemp);
			cell = (tnf_ref32_t *) ((char *)tnf->file_start +
		/* LINTED pointer cast may result in improper alignment */
							reftemp);

		} else {
			cell = vaddr_to_phys(tnf, cell, TNF_REF32_VALUE(ref32));
			if (cell == TNF_NULL)
				return (TNF_NULL);
		}
		ref32 = _GET_INT32(tnf, cell);
	}

	return (cell);
}
