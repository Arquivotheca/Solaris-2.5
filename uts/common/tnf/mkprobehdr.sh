#!/usr/bin/sh

cat <<ENDSTR
/*
 *	Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _SYS_TNF_PROBE_H
#define	_SYS_TNF_PROBE_H

#pragma ident	"@(#)tnf_probe.h	1.10	95/01/20 SMI"

#include <sys/tnf_writer.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * These macros are used to convert the __LINE__ directive to a
 * string in the probe macros below.
 */

#define	TNF_STRINGIFY(x) #x
#define	TNF_STRINGVALUE(x) TNF_STRINGIFY(x)

/*
 * Probe versioning
 */

struct tnf_probe_version {
	size_t	version_size;		/* sizeof(struct tnf_probe_version) */
	size_t	probe_control_size;	/* sizeof(tnf_probe_control_t) */
};

extern struct tnf_probe_version __tnf_probe_version_1;
#pragma weak __tnf_probe_version_1

/*
 * Typedefs
 */

typedef struct tnf_probe_control tnf_probe_control_t;
typedef struct tnf_probe_setup tnf_probe_setup_t;

/* returns pointer to buffer */
typedef void * (*tnf_probe_test_func_t)(void *,
					tnf_probe_control_t *,
					tnf_probe_setup_t *);

/* returns buffer pointer */
typedef void * (*tnf_probe_alloc_func_t)(tnf_ops_t *,	/* tpd	*/
					tnf_probe_control_t *,
					tnf_probe_setup_t *);

typedef void (*tnf_probe_func_t)(tnf_probe_setup_t *);

/*
 * Probe argument block
 */

struct tnf_probe_setup {
	tnf_ops_t		*tpd_p;
	void			*buffer_p;
	tnf_probe_control_t	*probe_p;
};

/*
 * Probe control block
 */

struct tnf_probe_control {
	const struct tnf_probe_version	*version;
	tnf_probe_control_t	*next;
	tnf_probe_test_func_t	test_func;
	tnf_probe_alloc_func_t	alloc_func;
	tnf_probe_func_t	probe_func;
	tnf_probe_func_t	commit_func;
	tnf_uint32_t		index;
	const char		*attrs;
	tnf_tag_data_t		***slot_types;
	unsigned long		tnf_event_size;
};

#ifdef _KERNEL

#define	TNF_NEXT_INIT	0

#else

#define	TNF_NEXT_INIT	-1

#endif	/* _KERNEL */

/*
 * TNF Type extension
 */

#ifdef NPROBE

#define	TNF_DECLARE_RECORD(ctype, record)				\\
	typedef tnf_reference_t record##_t

#else

#define	TNF_DECLARE_RECORD(ctype, record)				\\
	typedef tnf_reference_t record##_t;				\\
	extern tnf_tag_data_t *record##_tag_data;			\\
	extern record##_t record(tnf_ops_t *, ctype *, tnf_reference_t)

#endif	/* NPROBE */

ENDSTR

#
# The following code generates the five type extension macros
#
for i in 1 2 3 4 5; do
  echo "#ifdef NPROBE\n"
  echo "/* CSTYLED */"
  echo "#define	TNF_DEFINE_RECORD_$i(ctype, ctype_record\c"
  j=1; while [ $j -le $i ]; do
    echo ", t$j, n$j\c"
    j=`expr $j + 1`
  done
  echo ")\n"
  echo "#else\n"
  echo "/* CSTYLED */"
  echo "#define	TNF_DEFINE_RECORD_$i(ctype, ctype_record\c"
  j=1; while [ $j -le $i ]; do
    echo ", t$j, n$j\c"
    j=`expr $j + 1`
  done
  echo ") \\"
  echo "typedef struct {						\\"
  echo "	tnf_tag_t	tag;					\\"
  j=1; while [ $j -le $i ]; do
    echo "	t$j##_t		data_$j;				\\"
    j=`expr $j + 1`
  done
  echo "} ctype_record##_prototype_t;					\\"
  echo "static tnf_tag_data_t **ctype_record##_type_slots[] = {		\\"
  echo "	&tnf_tag_tag_data,					\\"
  j=1; while [ $j -le $i ]; do
    echo "	&t$j##_tag_data,					\\"
    j=`expr $j + 1`
  done
  echo "	0 };							\\";
  echo "static char *ctype_record##_slot_names[] = {			\\";
  echo "	\"tnf_tag\",						\\"
  j=1; while [ $j -le $i ]; do
    echo "	\"\"#n$j,						\\"
    j=`expr $j + 1`
  done
  echo "	0 };							\\"
  echo "static tnf_tag_data_t ctype_record##_tag_data_rec = {		\\"
  echo "	TNF_TAG_VERSION, &tnf_struct_tag_1,			\\"
  echo "	0, #ctype_record, &tnf_user_struct_properties,		\\"
  echo "	sizeof (ctype_record##_prototype_t),			\\"
  echo "	TNF_ALIGN(tnf_ref32_t),					\\"
  echo "	sizeof (ctype_record##_t), TNF_STRUCT, 0,		\\"
  echo "	ctype_record##_type_slots, ctype_record##_slot_names	\\"
  echo "};								\\"
  echo "tnf_tag_data_t *ctype_record##_tag_data =			\\"
  echo "			&ctype_record##_tag_data_rec;		\\"
  echo "ctype_record##_t						\\"
  echo "ctype_record(tnf_ops_t *ops, ctype * the_ctype,			\\"
  echo "				tnf_reference_t reference)	\\"
  echo "{								\\"
  echo "	tnf_tag_data_t			*metatag_data;		\\"
  echo "	tnf_record_p			metatag_index;		\\"
  echo "	ctype_record##_prototype_t	*buffer;		\\"
  echo "								\\"
  echo "	if (the_ctype == NULL)					\\"
  echo "		return (0);					\\"
  echo "	buffer = tnf_allocate(ops, sizeof (*buffer));		\\"
  echo "	if (buffer == NULL)					\\"
  echo "		return (0);					\\"
  echo "								\\"
  echo "	metatag_data = ctype_record##_tag_data;			\\"
  echo "	metatag_index = metatag_data->tag_index ?		\\"
  echo "		metatag_data->tag_index:			\\"
  echo "		metatag_data->tag_desc(ops, metatag_data);	\\"
  echo "	buffer->tag = tnf_tag(ops, metatag_index,		\\"
  echo "		(tnf_reference_t) &buffer->tag);		\\"
  j=1; while [ $j -le $i ]; do
    echo "	buffer->data_$j = t$j(ops, the_ctype->n$j,		\\"
    echo "			(tnf_reference_t) &(buffer->data_$j));	\\"
    j=`expr $j + 1`
  done
  echo "	return (tnf_ref32(ops, (tnf_record_p) buffer, reference)); \\"
  echo "}\n"
  echo "#endif /* NPROBE */"
  echo ""
done

echo "/*"
echo " * Probe Macros"
echo " */"
echo ""

#
# The following code generates the six probe macros ...
#
for i in 0 1 2 3 4 5; do
  echo "#ifdef NPROBE\n"
  echo "/* CSTYLED */"
  echo "#define	TNF_PROBE_$i(namearg, keysarg, detail\c"
  j=1; while [ $j -le $i ]; do
    echo ", type_$j, namearg_$j, valarg_$j\c"
    j=`expr $j + 1`
  done
  echo ") \\"
  echo "\t\t((void)0)\n"
  echo "#else\n"
  echo "/* CSTYLED */"
  echo "#define	TNF_PROBE_$i(namearg, keysarg, detail\c"
  j=1; while [ $j -le $i ]; do
    echo ", type_$j, namearg_$j, valarg_$j\c";
    j=`expr $j + 1`
  done
  echo ")	\\"
  echo "{								\\"
  echo "	struct tnf_buf_$i {					\\"
  echo "		tnf_probe_event_t	probe_event;		\\"
  echo "		tnf_time_delta_t	time_delta;		\\"
  j=1; while [ $j -le $i ]; do
    echo "		type_$j##_t		data_$j;		\\"
    j=`expr $j + 1`
  done
  echo "	};							\\"
  echo "	static tnf_tag_data_t ** namearg##_info[] = {		\\"
  echo "		&tnf_probe_event_tag_data,			\\"
  echo "		&tnf_time_delta_tag_data,			\\"
  j=1; while [ $j -le $i ]; do
    echo "		&type_$j##_tag_data,				\\"
    j=`expr $j + 1`
  done
  echo "		0 };						\\"
  echo "	static struct tnf_probe_control namearg##_probe = {	\\"
  echo "		&__tnf_probe_version_1,				\\"
  echo "		(tnf_probe_control_t *) TNF_NEXT_INIT,		\\"
  echo "		(tnf_probe_test_func_t) 0,			\\"
  echo "		(tnf_probe_alloc_func_t) 0,			\\"
  echo "		(tnf_probe_func_t) 0,				\\"
  echo "		(tnf_probe_func_t) 0,				\\"
  echo "		(tnf_uint32_t) 0,				\\"
  echo "			/* attribute string */			\\"
  echo "			\"name \" TNF_STRINGVALUE(namearg) \";\" \\"
#  echo "			\"slots \"\c"
#  j=1; while [ $j -le $i ]; do
#    echo " #namearg_$j \" \"\c"
#    j=`expr $j + 1`
  echo "			\"slots \"				\\"
  j=1; while [ $j -le $i ]; do
    echo "			\"\"#namearg_$j\" \"			\\"
    j=`expr $j + 1`
  done
  echo "			\";\"					\\"
  echo "			\"keys \" keysarg \";\"			\\"
  echo "			\"file \" __FILE__ \";\"		\\"
  echo "			\"line \" TNF_STRINGVALUE(__LINE__) \";\" \\"
  echo "			detail,					\\"
  echo "		namearg##_info,					\\"
  echo "		sizeof (struct tnf_buf_$i)			\\"
  echo "	};							\\"
  echo "	tnf_probe_control_t	*probe_p = &namearg##_probe;	\\"
  echo "	tnf_probe_test_func_t	probe_test = probe_p->test_func; \\"
  echo "	tnf_probe_setup_t	set_p;				\\"
  echo "	struct tnf_buf_$i	*probe_buffer;			\\"
  echo "								\\"
  echo "	if (probe_test) {					\\"
  echo "		probe_buffer = (struct tnf_buf_$i *)		\\"
  echo "		    probe_test(0, probe_p, &set_p);		\\"
  echo "		if (probe_buffer) {				\\"
  j=1; while [ $j -le $i ]; do
    echo "		    probe_buffer->data_$j = type_$j(		\\"
    echo "			set_p.tpd_p, valarg_$j,			\\"
    echo "			(tnf_reference_t) &(probe_buffer->data_$j)); \\"
    j=`expr $j + 1`
  done
  echo "		    (probe_p->probe_func)(&set_p);		\\"
  echo "		}						\\"
  echo "	}							\\"
  echo "}\n"
  echo "#endif /* NPROBE */"
  echo ""
  done

  echo "#ifdef __cplusplus"
  echo "}"
  echo "#endif"
  echo ""
  echo "#endif /* _SYS_TNF_PROBE_H */"
