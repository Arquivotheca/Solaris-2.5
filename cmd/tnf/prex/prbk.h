/*
 * Copyright (c) 1994, by Sun Microsytems, Inc.
 */

#pragma ident  "@(#)prbk.h 1.1 94/10/16 SMI"

/*
 * Declarations
 */

prb_status_t prbk_init(void);
prb_status_t prbk_refresh(void);
prb_status_t prbk_flush(prbctlref_t *p);
prb_status_t prbk_test_func(caddr_t *outp);
void prbk_set_other_funcs(caddr_t *allocp, caddr_t *commitp,
	caddr_t *rollbackp, caddr_t *endp);
void prbk_buffer_list(void);
void prbk_buffer_alloc(int size);
void prbk_buffer_dealloc(void);
void *prbk_pidlist_add(void *, int);
void prbk_pfilter_add(void *);
void prbk_pfilter_drop(void *);
void prbk_pfilter_sync(void);
void prbk_set_pfilter_mode(int);
void prbk_show_pfilter_mode(void);
void prbk_set_tracing(int);
void prbk_show_tracing(void);
prb_status_t prbk_tracing_sync(void);
