/*
 *	Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident	"@(#)tnf_probe.c	1.3	95/03/21 SMI"

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/ddi.h>		/* strchr */
#include <sys/sunddi.h>		/* strchr */
#include <sys/tnf_com.h>
#include <sys/tnf_writer.h>
#include <sys/tnf_probe.h>
#include "tnf_types.h"
#include "tnf_trace.h"
#else  /* _KERNEL */
#include <stdlib.h>
#include <string.h>
#include <tnf/com.h>
#include <tnf/writer.h>
#include <tnf/probe.h>
#include "tnf_types.h"
#include <tnf_trace.h>
#endif	/* _KERNEL */


/*
 * Defines
 */

#define	NAME_LIMIT	128
#define	ARRAY_LIMIT	5
#define	NAME_START	5
#define	SLOT_OFFSET	7
#define	CONST_SLOTS	2

/*
 * probe version 1
 */

struct tnf_probe_version  __tnf_probe_version_1_info =  {
	sizeof (struct tnf_probe_version),
	sizeof (tnf_probe_control_t)
};

/*
 * write instances of tnf_probe_type (i.e probe records)
 */

tnf_uint32_t
tnf_probe_tag(tnf_ops_t *ops, tnf_probe_control_t *probe_p)
{
	tnf_tag_data_t		*metatag_data;
	tnf_record_p		metatag_index;
	tnf_probe_prototype_t 	*buffer;
	enum tnf_alloc_mode	saved_mode;
	tnf_uint32_t		*fwp;
	char			probe_name[NAME_LIMIT];
	char			slot_array[ARRAY_LIMIT][NAME_LIMIT];
	char			*slot_args[ARRAY_LIMIT + CONST_SLOTS + 1];
	const char		*nm_start, *nm_end, *slot_start, *slot_end;
	int			nm_len, separator, count;

	saved_mode = ops->mode;
	ops->mode = TNF_ALLOC_FIXED;
	ALLOC2(ops, sizeof (*buffer), buffer, saved_mode);
	probe_p->index = (tnf_uint32_t)buffer;
	fwp = tnfw_b_fw_alloc(&(ops->wcb));
	if (fwp) {
		/* REMIND: can make the next call more efficient */
		*fwp = tnf_ref32(ops, (tnf_record_p) buffer,
						(tnf_reference_t)fwp);
		/* fwp - filestart < 64K */
		probe_p->index = (char *)fwp - _tnfw_b_control->tnf_buffer;
		probe_p->index |= TNF_TAG16_T_ABS;
		probe_p->index = probe_p->index << PROBE_INDEX_SHIFT;
		probe_p->index |= PROBE_INDEX_FILE_PTR;
	}

	metatag_data = TAG_DATA(tnf_probe_type);
	metatag_index = metatag_data->tag_index ?
		metatag_data->tag_index :
		metatag_data->tag_desc(ops, metatag_data);

	/* find the name of the probe */
	nm_start = &(probe_p->attrs[NAME_START]);
	separator = ATTR_SEPARATOR;
	nm_end = strchr(probe_p->attrs, separator);
	nm_len = nm_end - nm_start;
	slot_start = nm_end + SLOT_OFFSET;
	nm_len = (nm_len > (NAME_LIMIT - 1)) ? (NAME_LIMIT - 1) : nm_len;
	(void) strncpy(probe_name, nm_start, nm_len);
	probe_name[nm_len] = '\0';

	/* initialize constant part of slot names */
	slot_args[0] = TNF_N_TAG;
	slot_args[1] = TNF_N_TIME_DELTA;

	/*
	 * initialize rest of slot names, if any. This parsing routine is
	 * dependant on a space after "slots" (even for TNF_PROBE_0 and a
	 * space after the last slot name.  It truncates any values that
	 * are larger than 127 chars to 127 chars.  It handles missing slot
	 * names.
	 */
	separator = ATTR_SEPARATOR;
	slot_end = strchr(slot_start, separator);
	nm_start = slot_start;
	separator = VAL_SEPARATOR;
	for (count = 0; nm_start < slot_end; count++) {
		nm_end = strchr(nm_start, separator);
		nm_len = nm_end - nm_start;
		nm_len = (nm_len > (NAME_LIMIT - 1)) ? (NAME_LIMIT - 1) :
							nm_len;
		(void) strncpy(slot_array[count], nm_start, nm_len);
		slot_array[count][nm_len] = '\0';
		slot_args[count+CONST_SLOTS] = slot_array[count];
		/* get next name */
		nm_start = nm_end + 1;
	}
	/* null terminate string vector */
	slot_args[count+CONST_SLOTS] = NULL;

	ASSIGN(buffer, tag, 		metatag_index);
	ASSIGN(buffer, name, 		probe_name);
	/* XXX Fix these properties sometime */
	ASSIGN(buffer, properties, 	&tnf_struct_properties);
	ASSIGN(buffer, slot_types,	probe_p->slot_types);
	ASSIGN(buffer, type_size,	probe_p->tnf_event_size);
	ASSIGN(buffer, slot_names,	slot_args);
	ASSIGN(buffer, string,		(slot_end + 1));

	ops->mode = saved_mode;
	return (probe_p->index);
}
