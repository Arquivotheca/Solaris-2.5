/*
 *	Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#ifndef _SYS_TNF_PROBE_H
#define	_SYS_TNF_PROBE_H

#pragma ident	"@(#)tnf_probe.h	1.7	95/01/20 SMI"

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

#define	TNF_DECLARE_RECORD(ctype, record)				\
	typedef tnf_reference_t record##_t

#else

#define	TNF_DECLARE_RECORD(ctype, record)				\
	typedef tnf_reference_t record##_t;				\
	extern tnf_tag_data_t *record##_tag_data;			\
	extern record##_t record(tnf_ops_t *, ctype *, tnf_reference_t)

#endif	/* NPROBE */

#ifdef NPROBE

/* CSTYLED */
#define	TNF_DEFINE_RECORD_1(ctype, ctype_record, t1, n1)

#else

/* CSTYLED */
#define	TNF_DEFINE_RECORD_1(ctype, ctype_record, t1, n1) \
typedef struct {						\
	tnf_tag_t	tag;					\
	t1##_t		data_1;				\
} ctype_record##_prototype_t;					\
static tnf_tag_data_t **ctype_record##_type_slots[] = {		\
	&tnf_tag_tag_data,					\
	&t1##_tag_data,					\
	0 };							\
static char *ctype_record##_slot_names[] = {			\
	"tnf_tag",						\
	""#n1,						\
	0 };							\
static tnf_tag_data_t ctype_record##_tag_data_rec = {		\
	TNF_TAG_VERSION, &tnf_struct_tag_1,			\
	0, #ctype_record, &tnf_user_struct_properties,		\
	sizeof (ctype_record##_prototype_t),			\
	TNF_ALIGN(tnf_ref32_t),					\
	sizeof (ctype_record##_t), TNF_STRUCT, 0,		\
	ctype_record##_type_slots, ctype_record##_slot_names	\
};								\
tnf_tag_data_t *ctype_record##_tag_data =			\
			&ctype_record##_tag_data_rec;		\
ctype_record##_t						\
ctype_record(tnf_ops_t *ops, ctype * the_ctype,			\
				tnf_reference_t reference)	\
{								\
	tnf_tag_data_t			*metatag_data;		\
	tnf_record_p			metatag_index;		\
	ctype_record##_prototype_t	*buffer;		\
								\
	if (the_ctype == NULL)					\
		return (0);					\
	buffer = tnf_allocate(ops, sizeof (*buffer));		\
	if (buffer == NULL)					\
		return (0);					\
								\
	metatag_data = ctype_record##_tag_data;			\
	metatag_index = metatag_data->tag_index ?		\
		metatag_data->tag_index:			\
		metatag_data->tag_desc(ops, metatag_data);	\
	buffer->tag = tnf_tag(ops, metatag_index,		\
		(tnf_reference_t) &buffer->tag);		\
	buffer->data_1 = t1(ops, the_ctype->n1,		\
			(tnf_reference_t) &(buffer->data_1));	\
	return (tnf_ref32(ops, (tnf_record_p) buffer, reference)); \
}

#endif /* NPROBE */

#ifdef NPROBE

/* CSTYLED */
#define	TNF_DEFINE_RECORD_2(ctype, ctype_record, t1, n1, t2, n2)

#else

/* CSTYLED */
#define	TNF_DEFINE_RECORD_2(ctype, ctype_record, t1, n1, t2, n2) \
typedef struct {						\
	tnf_tag_t	tag;					\
	t1##_t		data_1;				\
	t2##_t		data_2;				\
} ctype_record##_prototype_t;					\
static tnf_tag_data_t **ctype_record##_type_slots[] = {		\
	&tnf_tag_tag_data,					\
	&t1##_tag_data,					\
	&t2##_tag_data,					\
	0 };							\
static char *ctype_record##_slot_names[] = {			\
	"tnf_tag",						\
	""#n1,						\
	""#n2,						\
	0 };							\
static tnf_tag_data_t ctype_record##_tag_data_rec = {		\
	TNF_TAG_VERSION, &tnf_struct_tag_1,			\
	0, #ctype_record, &tnf_user_struct_properties,		\
	sizeof (ctype_record##_prototype_t),			\
	TNF_ALIGN(tnf_ref32_t),					\
	sizeof (ctype_record##_t), TNF_STRUCT, 0,		\
	ctype_record##_type_slots, ctype_record##_slot_names	\
};								\
tnf_tag_data_t *ctype_record##_tag_data =			\
			&ctype_record##_tag_data_rec;		\
ctype_record##_t						\
ctype_record(tnf_ops_t *ops, ctype * the_ctype,			\
				tnf_reference_t reference)	\
{								\
	tnf_tag_data_t			*metatag_data;		\
	tnf_record_p			metatag_index;		\
	ctype_record##_prototype_t	*buffer;		\
								\
	if (the_ctype == NULL)					\
		return (0);					\
	buffer = tnf_allocate(ops, sizeof (*buffer));		\
	if (buffer == NULL)					\
		return (0);					\
								\
	metatag_data = ctype_record##_tag_data;			\
	metatag_index = metatag_data->tag_index ?		\
		metatag_data->tag_index:			\
		metatag_data->tag_desc(ops, metatag_data);	\
	buffer->tag = tnf_tag(ops, metatag_index,		\
		(tnf_reference_t) &buffer->tag);		\
	buffer->data_1 = t1(ops, the_ctype->n1,		\
			(tnf_reference_t) &(buffer->data_1));	\
	buffer->data_2 = t2(ops, the_ctype->n2,		\
			(tnf_reference_t) &(buffer->data_2));	\
	return (tnf_ref32(ops, (tnf_record_p) buffer, reference)); \
}

#endif /* NPROBE */

#ifdef NPROBE

/* CSTYLED */
#define	TNF_DEFINE_RECORD_3(ctype, ctype_record, t1, n1, t2, n2, t3, n3)

#else

/* CSTYLED */
#define	TNF_DEFINE_RECORD_3(ctype, ctype_record, t1, n1, t2, n2, t3, n3) \
typedef struct {						\
	tnf_tag_t	tag;					\
	t1##_t		data_1;				\
	t2##_t		data_2;				\
	t3##_t		data_3;				\
} ctype_record##_prototype_t;					\
static tnf_tag_data_t **ctype_record##_type_slots[] = {		\
	&tnf_tag_tag_data,					\
	&t1##_tag_data,					\
	&t2##_tag_data,					\
	&t3##_tag_data,					\
	0 };							\
static char *ctype_record##_slot_names[] = {			\
	"tnf_tag",						\
	""#n1,						\
	""#n2,						\
	""#n3,						\
	0 };							\
static tnf_tag_data_t ctype_record##_tag_data_rec = {		\
	TNF_TAG_VERSION, &tnf_struct_tag_1,			\
	0, #ctype_record, &tnf_user_struct_properties,		\
	sizeof (ctype_record##_prototype_t),			\
	TNF_ALIGN(tnf_ref32_t),					\
	sizeof (ctype_record##_t), TNF_STRUCT, 0,		\
	ctype_record##_type_slots, ctype_record##_slot_names	\
};								\
tnf_tag_data_t *ctype_record##_tag_data =			\
			&ctype_record##_tag_data_rec;		\
ctype_record##_t						\
ctype_record(tnf_ops_t *ops, ctype * the_ctype,			\
				tnf_reference_t reference)	\
{								\
	tnf_tag_data_t			*metatag_data;		\
	tnf_record_p			metatag_index;		\
	ctype_record##_prototype_t	*buffer;		\
								\
	if (the_ctype == NULL)					\
		return (0);					\
	buffer = tnf_allocate(ops, sizeof (*buffer));		\
	if (buffer == NULL)					\
		return (0);					\
								\
	metatag_data = ctype_record##_tag_data;			\
	metatag_index = metatag_data->tag_index ?		\
		metatag_data->tag_index:			\
		metatag_data->tag_desc(ops, metatag_data);	\
	buffer->tag = tnf_tag(ops, metatag_index,		\
		(tnf_reference_t) &buffer->tag);		\
	buffer->data_1 = t1(ops, the_ctype->n1,		\
			(tnf_reference_t) &(buffer->data_1));	\
	buffer->data_2 = t2(ops, the_ctype->n2,		\
			(tnf_reference_t) &(buffer->data_2));	\
	buffer->data_3 = t3(ops, the_ctype->n3,		\
			(tnf_reference_t) &(buffer->data_3));	\
	return (tnf_ref32(ops, (tnf_record_p) buffer, reference)); \
}

#endif /* NPROBE */

#ifdef NPROBE

/* CSTYLED */
#define	TNF_DEFINE_RECORD_4(ctype, ctype_record, t1, n1, t2, n2, t3, n3, t4, n4)

#else

/* CSTYLED */
#define	TNF_DEFINE_RECORD_4(ctype, ctype_record, t1, n1, t2, n2, t3, n3, t4, n4) \
typedef struct {						\
	tnf_tag_t	tag;					\
	t1##_t		data_1;				\
	t2##_t		data_2;				\
	t3##_t		data_3;				\
	t4##_t		data_4;				\
} ctype_record##_prototype_t;					\
static tnf_tag_data_t **ctype_record##_type_slots[] = {		\
	&tnf_tag_tag_data,					\
	&t1##_tag_data,					\
	&t2##_tag_data,					\
	&t3##_tag_data,					\
	&t4##_tag_data,					\
	0 };							\
static char *ctype_record##_slot_names[] = {			\
	"tnf_tag",						\
	""#n1,						\
	""#n2,						\
	""#n3,						\
	""#n4,						\
	0 };							\
static tnf_tag_data_t ctype_record##_tag_data_rec = {		\
	TNF_TAG_VERSION, &tnf_struct_tag_1,			\
	0, #ctype_record, &tnf_user_struct_properties,		\
	sizeof (ctype_record##_prototype_t),			\
	TNF_ALIGN(tnf_ref32_t),					\
	sizeof (ctype_record##_t), TNF_STRUCT, 0,		\
	ctype_record##_type_slots, ctype_record##_slot_names	\
};								\
tnf_tag_data_t *ctype_record##_tag_data =			\
			&ctype_record##_tag_data_rec;		\
ctype_record##_t						\
ctype_record(tnf_ops_t *ops, ctype * the_ctype,			\
				tnf_reference_t reference)	\
{								\
	tnf_tag_data_t			*metatag_data;		\
	tnf_record_p			metatag_index;		\
	ctype_record##_prototype_t	*buffer;		\
								\
	if (the_ctype == NULL)					\
		return (0);					\
	buffer = tnf_allocate(ops, sizeof (*buffer));		\
	if (buffer == NULL)					\
		return (0);					\
								\
	metatag_data = ctype_record##_tag_data;			\
	metatag_index = metatag_data->tag_index ?		\
		metatag_data->tag_index:			\
		metatag_data->tag_desc(ops, metatag_data);	\
	buffer->tag = tnf_tag(ops, metatag_index,		\
		(tnf_reference_t) &buffer->tag);		\
	buffer->data_1 = t1(ops, the_ctype->n1,		\
			(tnf_reference_t) &(buffer->data_1));	\
	buffer->data_2 = t2(ops, the_ctype->n2,		\
			(tnf_reference_t) &(buffer->data_2));	\
	buffer->data_3 = t3(ops, the_ctype->n3,		\
			(tnf_reference_t) &(buffer->data_3));	\
	buffer->data_4 = t4(ops, the_ctype->n4,		\
			(tnf_reference_t) &(buffer->data_4));	\
	return (tnf_ref32(ops, (tnf_record_p) buffer, reference)); \
}

#endif /* NPROBE */

#ifdef NPROBE

/* CSTYLED */
#define	TNF_DEFINE_RECORD_5(ctype, ctype_record, t1, n1, t2, n2, t3, n3, t4, n4, t5, n5)

#else

/* CSTYLED */
#define	TNF_DEFINE_RECORD_5(ctype, ctype_record, t1, n1, t2, n2, t3, n3, t4, n4, t5, n5) \
typedef struct {						\
	tnf_tag_t	tag;					\
	t1##_t		data_1;				\
	t2##_t		data_2;				\
	t3##_t		data_3;				\
	t4##_t		data_4;				\
	t5##_t		data_5;				\
} ctype_record##_prototype_t;					\
static tnf_tag_data_t **ctype_record##_type_slots[] = {		\
	&tnf_tag_tag_data,					\
	&t1##_tag_data,					\
	&t2##_tag_data,					\
	&t3##_tag_data,					\
	&t4##_tag_data,					\
	&t5##_tag_data,					\
	0 };							\
static char *ctype_record##_slot_names[] = {			\
	"tnf_tag",						\
	""#n1,						\
	""#n2,						\
	""#n3,						\
	""#n4,						\
	""#n5,						\
	0 };							\
static tnf_tag_data_t ctype_record##_tag_data_rec = {		\
	TNF_TAG_VERSION, &tnf_struct_tag_1,			\
	0, #ctype_record, &tnf_user_struct_properties,		\
	sizeof (ctype_record##_prototype_t),			\
	TNF_ALIGN(tnf_ref32_t),					\
	sizeof (ctype_record##_t), TNF_STRUCT, 0,		\
	ctype_record##_type_slots, ctype_record##_slot_names	\
};								\
tnf_tag_data_t *ctype_record##_tag_data =			\
			&ctype_record##_tag_data_rec;		\
ctype_record##_t						\
ctype_record(tnf_ops_t *ops, ctype * the_ctype,			\
				tnf_reference_t reference)	\
{								\
	tnf_tag_data_t			*metatag_data;		\
	tnf_record_p			metatag_index;		\
	ctype_record##_prototype_t	*buffer;		\
								\
	if (the_ctype == NULL)					\
		return (0);					\
	buffer = tnf_allocate(ops, sizeof (*buffer));		\
	if (buffer == NULL)					\
		return (0);					\
								\
	metatag_data = ctype_record##_tag_data;			\
	metatag_index = metatag_data->tag_index ?		\
		metatag_data->tag_index:			\
		metatag_data->tag_desc(ops, metatag_data);	\
	buffer->tag = tnf_tag(ops, metatag_index,		\
		(tnf_reference_t) &buffer->tag);		\
	buffer->data_1 = t1(ops, the_ctype->n1,		\
			(tnf_reference_t) &(buffer->data_1));	\
	buffer->data_2 = t2(ops, the_ctype->n2,		\
			(tnf_reference_t) &(buffer->data_2));	\
	buffer->data_3 = t3(ops, the_ctype->n3,		\
			(tnf_reference_t) &(buffer->data_3));	\
	buffer->data_4 = t4(ops, the_ctype->n4,		\
			(tnf_reference_t) &(buffer->data_4));	\
	buffer->data_5 = t5(ops, the_ctype->n5,		\
			(tnf_reference_t) &(buffer->data_5));	\
	return (tnf_ref32(ops, (tnf_record_p) buffer, reference)); \
}

#endif /* NPROBE */

/*
 * Probe Macros
 */

#ifdef NPROBE

/* CSTYLED */
#define	TNF_PROBE_0(namearg, keysarg, detail) \
		((void)0)

#else

/* CSTYLED */
#define	TNF_PROBE_0(namearg, keysarg, detail)	\
{								\
	struct tnf_buf_0 {					\
		tnf_probe_event_t	probe_event;		\
		tnf_time_delta_t	time_delta;		\
	};							\
	static tnf_tag_data_t ** namearg##_info[] = {		\
		&tnf_probe_event_tag_data,			\
		&tnf_time_delta_tag_data,			\
		0 };						\
	static struct tnf_probe_control namearg##_probe = {	\
		&__tnf_probe_version_1,				\
		(tnf_probe_control_t *) TNF_NEXT_INIT,		\
		(tnf_probe_test_func_t) 0,			\
		(tnf_probe_alloc_func_t) 0,			\
		(tnf_probe_func_t) 0,				\
		(tnf_probe_func_t) 0,				\
		(tnf_uint32_t) 0,				\
			/* attribute string */			\
			"name " TNF_STRINGVALUE(namearg) ";" \
			"slots "				\
			";"					\
			"keys " keysarg ";"			\
			"file " __FILE__ ";"		\
			"line " TNF_STRINGVALUE(__LINE__) ";" \
			detail,					\
		namearg##_info,					\
		sizeof (struct tnf_buf_0)			\
	};							\
	tnf_probe_control_t	*probe_p = &namearg##_probe;	\
	tnf_probe_test_func_t	probe_test = probe_p->test_func; \
	tnf_probe_setup_t	set_p;				\
	struct tnf_buf_0	*probe_buffer;			\
								\
	if (probe_test) {					\
		probe_buffer = (struct tnf_buf_0 *)		\
		    probe_test(0, probe_p, &set_p);		\
		if (probe_buffer) {				\
		    (probe_p->probe_func)(&set_p);		\
		}						\
	}							\
}

#endif /* NPROBE */

#ifdef NPROBE

/* CSTYLED */
#define	TNF_PROBE_1(namearg, keysarg, detail, type_1, namearg_1, valarg_1) \
		((void)0)

#else

/* CSTYLED */
#define	TNF_PROBE_1(namearg, keysarg, detail, type_1, namearg_1, valarg_1)	\
{								\
	struct tnf_buf_1 {					\
		tnf_probe_event_t	probe_event;		\
		tnf_time_delta_t	time_delta;		\
		type_1##_t		data_1;		\
	};							\
	static tnf_tag_data_t ** namearg##_info[] = {		\
		&tnf_probe_event_tag_data,			\
		&tnf_time_delta_tag_data,			\
		&type_1##_tag_data,				\
		0 };						\
	static struct tnf_probe_control namearg##_probe = {	\
		&__tnf_probe_version_1,				\
		(tnf_probe_control_t *) TNF_NEXT_INIT,		\
		(tnf_probe_test_func_t) 0,			\
		(tnf_probe_alloc_func_t) 0,			\
		(tnf_probe_func_t) 0,				\
		(tnf_probe_func_t) 0,				\
		(tnf_uint32_t) 0,				\
			/* attribute string */			\
			"name " TNF_STRINGVALUE(namearg) ";" \
			"slots "				\
			""#namearg_1" "			\
			";"					\
			"keys " keysarg ";"			\
			"file " __FILE__ ";"		\
			"line " TNF_STRINGVALUE(__LINE__) ";" \
			detail,					\
		namearg##_info,					\
		sizeof (struct tnf_buf_1)			\
	};							\
	tnf_probe_control_t	*probe_p = &namearg##_probe;	\
	tnf_probe_test_func_t	probe_test = probe_p->test_func; \
	tnf_probe_setup_t	set_p;				\
	struct tnf_buf_1	*probe_buffer;			\
								\
	if (probe_test) {					\
		probe_buffer = (struct tnf_buf_1 *)		\
		    probe_test(0, probe_p, &set_p);		\
		if (probe_buffer) {				\
		    probe_buffer->data_1 = type_1(		\
			set_p.tpd_p, valarg_1,			\
			(tnf_reference_t) &(probe_buffer->data_1)); \
		    (probe_p->probe_func)(&set_p);		\
		}						\
	}							\
}

#endif /* NPROBE */

#ifdef NPROBE

/* CSTYLED */
#define	TNF_PROBE_2(namearg, keysarg, detail, type_1, namearg_1, valarg_1, type_2, namearg_2, valarg_2) \
		((void)0)

#else

/* CSTYLED */
#define	TNF_PROBE_2(namearg, keysarg, detail, type_1, namearg_1, valarg_1, type_2, namearg_2, valarg_2)	\
{								\
	struct tnf_buf_2 {					\
		tnf_probe_event_t	probe_event;		\
		tnf_time_delta_t	time_delta;		\
		type_1##_t		data_1;		\
		type_2##_t		data_2;		\
	};							\
	static tnf_tag_data_t ** namearg##_info[] = {		\
		&tnf_probe_event_tag_data,			\
		&tnf_time_delta_tag_data,			\
		&type_1##_tag_data,				\
		&type_2##_tag_data,				\
		0 };						\
	static struct tnf_probe_control namearg##_probe = {	\
		&__tnf_probe_version_1,				\
		(tnf_probe_control_t *) TNF_NEXT_INIT,		\
		(tnf_probe_test_func_t) 0,			\
		(tnf_probe_alloc_func_t) 0,			\
		(tnf_probe_func_t) 0,				\
		(tnf_probe_func_t) 0,				\
		(tnf_uint32_t) 0,				\
			/* attribute string */			\
			"name " TNF_STRINGVALUE(namearg) ";" \
			"slots "				\
			""#namearg_1" "			\
			""#namearg_2" "			\
			";"					\
			"keys " keysarg ";"			\
			"file " __FILE__ ";"		\
			"line " TNF_STRINGVALUE(__LINE__) ";" \
			detail,					\
		namearg##_info,					\
		sizeof (struct tnf_buf_2)			\
	};							\
	tnf_probe_control_t	*probe_p = &namearg##_probe;	\
	tnf_probe_test_func_t	probe_test = probe_p->test_func; \
	tnf_probe_setup_t	set_p;				\
	struct tnf_buf_2	*probe_buffer;			\
								\
	if (probe_test) {					\
		probe_buffer = (struct tnf_buf_2 *)		\
		    probe_test(0, probe_p, &set_p);		\
		if (probe_buffer) {				\
		    probe_buffer->data_1 = type_1(		\
			set_p.tpd_p, valarg_1,			\
			(tnf_reference_t) &(probe_buffer->data_1)); \
		    probe_buffer->data_2 = type_2(		\
			set_p.tpd_p, valarg_2,			\
			(tnf_reference_t) &(probe_buffer->data_2)); \
		    (probe_p->probe_func)(&set_p);		\
		}						\
	}							\
}

#endif /* NPROBE */

#ifdef NPROBE

/* CSTYLED */
#define	TNF_PROBE_3(namearg, keysarg, detail, type_1, namearg_1, valarg_1, type_2, namearg_2, valarg_2, type_3, namearg_3, valarg_3) \
		((void)0)

#else

/* CSTYLED */
#define	TNF_PROBE_3(namearg, keysarg, detail, type_1, namearg_1, valarg_1, type_2, namearg_2, valarg_2, type_3, namearg_3, valarg_3)	\
{								\
	struct tnf_buf_3 {					\
		tnf_probe_event_t	probe_event;		\
		tnf_time_delta_t	time_delta;		\
		type_1##_t		data_1;		\
		type_2##_t		data_2;		\
		type_3##_t		data_3;		\
	};							\
	static tnf_tag_data_t ** namearg##_info[] = {		\
		&tnf_probe_event_tag_data,			\
		&tnf_time_delta_tag_data,			\
		&type_1##_tag_data,				\
		&type_2##_tag_data,				\
		&type_3##_tag_data,				\
		0 };						\
	static struct tnf_probe_control namearg##_probe = {	\
		&__tnf_probe_version_1,				\
		(tnf_probe_control_t *) TNF_NEXT_INIT,		\
		(tnf_probe_test_func_t) 0,			\
		(tnf_probe_alloc_func_t) 0,			\
		(tnf_probe_func_t) 0,				\
		(tnf_probe_func_t) 0,				\
		(tnf_uint32_t) 0,				\
			/* attribute string */			\
			"name " TNF_STRINGVALUE(namearg) ";" \
			"slots "				\
			""#namearg_1" "			\
			""#namearg_2" "			\
			""#namearg_3" "			\
			";"					\
			"keys " keysarg ";"			\
			"file " __FILE__ ";"		\
			"line " TNF_STRINGVALUE(__LINE__) ";" \
			detail,					\
		namearg##_info,					\
		sizeof (struct tnf_buf_3)			\
	};							\
	tnf_probe_control_t	*probe_p = &namearg##_probe;	\
	tnf_probe_test_func_t	probe_test = probe_p->test_func; \
	tnf_probe_setup_t	set_p;				\
	struct tnf_buf_3	*probe_buffer;			\
								\
	if (probe_test) {					\
		probe_buffer = (struct tnf_buf_3 *)		\
		    probe_test(0, probe_p, &set_p);		\
		if (probe_buffer) {				\
		    probe_buffer->data_1 = type_1(		\
			set_p.tpd_p, valarg_1,			\
			(tnf_reference_t) &(probe_buffer->data_1)); \
		    probe_buffer->data_2 = type_2(		\
			set_p.tpd_p, valarg_2,			\
			(tnf_reference_t) &(probe_buffer->data_2)); \
		    probe_buffer->data_3 = type_3(		\
			set_p.tpd_p, valarg_3,			\
			(tnf_reference_t) &(probe_buffer->data_3)); \
		    (probe_p->probe_func)(&set_p);		\
		}						\
	}							\
}

#endif /* NPROBE */

#ifdef NPROBE

/* CSTYLED */
#define	TNF_PROBE_4(namearg, keysarg, detail, type_1, namearg_1, valarg_1, type_2, namearg_2, valarg_2, type_3, namearg_3, valarg_3, type_4, namearg_4, valarg_4) \
		((void)0)

#else

/* CSTYLED */
#define	TNF_PROBE_4(namearg, keysarg, detail, type_1, namearg_1, valarg_1, type_2, namearg_2, valarg_2, type_3, namearg_3, valarg_3, type_4, namearg_4, valarg_4)	\
{								\
	struct tnf_buf_4 {					\
		tnf_probe_event_t	probe_event;		\
		tnf_time_delta_t	time_delta;		\
		type_1##_t		data_1;		\
		type_2##_t		data_2;		\
		type_3##_t		data_3;		\
		type_4##_t		data_4;		\
	};							\
	static tnf_tag_data_t ** namearg##_info[] = {		\
		&tnf_probe_event_tag_data,			\
		&tnf_time_delta_tag_data,			\
		&type_1##_tag_data,				\
		&type_2##_tag_data,				\
		&type_3##_tag_data,				\
		&type_4##_tag_data,				\
		0 };						\
	static struct tnf_probe_control namearg##_probe = {	\
		&__tnf_probe_version_1,				\
		(tnf_probe_control_t *) TNF_NEXT_INIT,		\
		(tnf_probe_test_func_t) 0,			\
		(tnf_probe_alloc_func_t) 0,			\
		(tnf_probe_func_t) 0,				\
		(tnf_probe_func_t) 0,				\
		(tnf_uint32_t) 0,				\
			/* attribute string */			\
			"name " TNF_STRINGVALUE(namearg) ";" \
			"slots "				\
			""#namearg_1" "			\
			""#namearg_2" "			\
			""#namearg_3" "			\
			""#namearg_4" "			\
			";"					\
			"keys " keysarg ";"			\
			"file " __FILE__ ";"		\
			"line " TNF_STRINGVALUE(__LINE__) ";" \
			detail,					\
		namearg##_info,					\
		sizeof (struct tnf_buf_4)			\
	};							\
	tnf_probe_control_t	*probe_p = &namearg##_probe;	\
	tnf_probe_test_func_t	probe_test = probe_p->test_func; \
	tnf_probe_setup_t	set_p;				\
	struct tnf_buf_4	*probe_buffer;			\
								\
	if (probe_test) {					\
		probe_buffer = (struct tnf_buf_4 *)		\
		    probe_test(0, probe_p, &set_p);		\
		if (probe_buffer) {				\
		    probe_buffer->data_1 = type_1(		\
			set_p.tpd_p, valarg_1,			\
			(tnf_reference_t) &(probe_buffer->data_1)); \
		    probe_buffer->data_2 = type_2(		\
			set_p.tpd_p, valarg_2,			\
			(tnf_reference_t) &(probe_buffer->data_2)); \
		    probe_buffer->data_3 = type_3(		\
			set_p.tpd_p, valarg_3,			\
			(tnf_reference_t) &(probe_buffer->data_3)); \
		    probe_buffer->data_4 = type_4(		\
			set_p.tpd_p, valarg_4,			\
			(tnf_reference_t) &(probe_buffer->data_4)); \
		    (probe_p->probe_func)(&set_p);		\
		}						\
	}							\
}

#endif /* NPROBE */

#ifdef NPROBE

/* CSTYLED */
#define	TNF_PROBE_5(namearg, keysarg, detail, type_1, namearg_1, valarg_1, type_2, namearg_2, valarg_2, type_3, namearg_3, valarg_3, type_4, namearg_4, valarg_4, type_5, namearg_5, valarg_5) \
		((void)0)

#else

/* CSTYLED */
#define	TNF_PROBE_5(namearg, keysarg, detail, type_1, namearg_1, valarg_1, type_2, namearg_2, valarg_2, type_3, namearg_3, valarg_3, type_4, namearg_4, valarg_4, type_5, namearg_5, valarg_5)	\
{								\
	struct tnf_buf_5 {					\
		tnf_probe_event_t	probe_event;		\
		tnf_time_delta_t	time_delta;		\
		type_1##_t		data_1;		\
		type_2##_t		data_2;		\
		type_3##_t		data_3;		\
		type_4##_t		data_4;		\
		type_5##_t		data_5;		\
	};							\
	static tnf_tag_data_t ** namearg##_info[] = {		\
		&tnf_probe_event_tag_data,			\
		&tnf_time_delta_tag_data,			\
		&type_1##_tag_data,				\
		&type_2##_tag_data,				\
		&type_3##_tag_data,				\
		&type_4##_tag_data,				\
		&type_5##_tag_data,				\
		0 };						\
	static struct tnf_probe_control namearg##_probe = {	\
		&__tnf_probe_version_1,				\
		(tnf_probe_control_t *) TNF_NEXT_INIT,		\
		(tnf_probe_test_func_t) 0,			\
		(tnf_probe_alloc_func_t) 0,			\
		(tnf_probe_func_t) 0,				\
		(tnf_probe_func_t) 0,				\
		(tnf_uint32_t) 0,				\
			/* attribute string */			\
			"name " TNF_STRINGVALUE(namearg) ";" \
			"slots "				\
			""#namearg_1" "			\
			""#namearg_2" "			\
			""#namearg_3" "			\
			""#namearg_4" "			\
			""#namearg_5" "			\
			";"					\
			"keys " keysarg ";"			\
			"file " __FILE__ ";"		\
			"line " TNF_STRINGVALUE(__LINE__) ";" \
			detail,					\
		namearg##_info,					\
		sizeof (struct tnf_buf_5)			\
	};							\
	tnf_probe_control_t	*probe_p = &namearg##_probe;	\
	tnf_probe_test_func_t	probe_test = probe_p->test_func; \
	tnf_probe_setup_t	set_p;				\
	struct tnf_buf_5	*probe_buffer;			\
								\
	if (probe_test) {					\
		probe_buffer = (struct tnf_buf_5 *)		\
		    probe_test(0, probe_p, &set_p);		\
		if (probe_buffer) {				\
		    probe_buffer->data_1 = type_1(		\
			set_p.tpd_p, valarg_1,			\
			(tnf_reference_t) &(probe_buffer->data_1)); \
		    probe_buffer->data_2 = type_2(		\
			set_p.tpd_p, valarg_2,			\
			(tnf_reference_t) &(probe_buffer->data_2)); \
		    probe_buffer->data_3 = type_3(		\
			set_p.tpd_p, valarg_3,			\
			(tnf_reference_t) &(probe_buffer->data_3)); \
		    probe_buffer->data_4 = type_4(		\
			set_p.tpd_p, valarg_4,			\
			(tnf_reference_t) &(probe_buffer->data_4)); \
		    probe_buffer->data_5 = type_5(		\
			set_p.tpd_p, valarg_5,			\
			(tnf_reference_t) &(probe_buffer->data_5)); \
		    (probe_p->probe_func)(&set_p);		\
		}						\
	}							\
}

#endif /* NPROBE */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_TNF_PROBE_H */
