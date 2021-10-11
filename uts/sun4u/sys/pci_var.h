/*
 * Copyright (c) 1991-1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef _SYS_PCI_VAR_H
#define	_SYS_PCI_VAR_H

#pragma ident	"@(#)pci_var.h	1.11	95/06/01 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct pci_devstate pci_devstate_t;
typedef struct psycho_devstate psycho_devstate_t;
typedef struct psycho_ino_info psycho_ino_info_t;
typedef struct psycho_intr_req psycho_intr_req_t;

typedef enum { NEW = 0, ATTACHED, RESUMED, DETACHED,
		SUSPENDED, PM_SUSPENDED } driver_state_t;

typedef enum { INO_FREE = 0, INO_SINGLE, INO_SHARED } ino_state_t;

/*
 * The following typedef is used to represent a
 * 1275 "bus-range" property of a PCI Bus node.
 */
typedef struct {
	u_int lo;
	u_int hi;
} pci_bus_range_t;


/*
 * The following structure represents an entry in an INO's
 * device list.
 */
struct psycho_intr_req {
	dev_info_t *dip;		/* devinfo structure */
	u_int (*handler)();		/* interrupt handler */
	caddr_t handler_arg;		/* interrupt handler argument */
	psycho_intr_req_t *next;	/* next entry in list */
};

/*
 * per ino structure
 *
 * Each psycho ino has one of the following structures associate
 * with it.
 */
struct psycho_ino_info {
	struct intrspec intrspec;
	u_longlong_t *clear_reg;	/* ino's clear interrupt register */
	u_longlong_t *map_reg;		/* ino's interrupt map register */
	ino_state_t state;		/* state of ino handler list */
	psycho_intr_req_t *head;	/* interrupt handler list head */
	psycho_intr_req_t *tail;	/* interrupt handler list tail */
	psycho_intr_req_t *start;	/* staring point in handler list */
	u_int size;			/* size of list */
	kmutex_t mutex1;		/* for locking adds/deletes */
	kmutex_t mutex2;		/* for locking adds/deletes/intrs */
	u_int iblock_cookie;
	u_int idevice_cookie;
	u_int ino;
	psycho_devstate_t *psycho_p;
};

/*
 * per-psycho soft state structure:
 */
struct psycho_devstate {

	dev_info_t *dip;		/* devinfo structure */
	u_int upa_id;			/* upa id */
	driver_state_t state;

	/*
	 * pointers back to the PBM A & B soft state structures:
	 */
	pci_devstate_t *pci_a_p;
	pci_devstate_t *pci_b_p;

	/*
	 * virtual addresses of common registers:
	 */
	u_longlong_t *reg_base;
	volatile u_longlong_t *ctrl_reg;
	volatile u_longlong_t *iommu_ctrl_reg;
	volatile u_longlong_t *tsb_base_addr_reg;
	volatile u_longlong_t *iommu_flush_reg;
	volatile u_longlong_t *imap_reg;
	volatile u_longlong_t *obio_imap_reg;
	volatile u_longlong_t *cleari_reg;
	volatile u_longlong_t *obio_cleari_reg;
	volatile u_longlong_t *intr_retry_timer_reg;
	volatile u_longlong_t *ecc_ctrl_reg;
	volatile u_longlong_t *ue_afsr;
	volatile u_longlong_t *ue_afar;
	volatile u_longlong_t *ce_afsr;
	volatile u_longlong_t *ce_afar;
	volatile u_longlong_t *tlb_tag_diag_acc;
	volatile u_longlong_t *tlb_data_diag_acc;
	volatile u_longlong_t *pci_int_state_diag_reg;
	volatile u_longlong_t *obio_int_state_diag_reg;

	/*
	 * Interrupts structures for correctable errors,
	 * uncorrectable errors and power management
	 * interrupts.
	 */
#if defined(USE_DDI_ADD_INTR)
	ddi_iblock_cookie_t ue_iblock_cookie;
	ddi_iblock_cookie_t ce_iblock_cookie;
#endif
	u_int ue_mondo;
	u_int ce_mondo;

	/*
	 * iommu data:
	 */
	u_longlong_t tsb_paddr;		/* physical address iommu tsb */
	struct map *dvma_map;		/* resource map for DVMA space */
	int dvma_call_list_id;		/* DVMA space call back list id */
	u_int iommu_tsb_entries;	/* max entries for tsb */
	u_long iommu_dvma_base;		/* dvma base address */
	u_long iommu_dvma_end;		/* largest dvma address */
	u_long dvma_reserve;		/* for fast dvma interfaces */

	/*
	 * ino structures:
	 */
#define	PSYCHO_MAX_INO		0x32
	psycho_ino_info_t ino_info[PSYCHO_MAX_INO + 1];
	kmutex_t ino_mutex;
};

/*
 * per-pci (PBM) soft state structure:
 */
struct pci_devstate {

	psycho_devstate_t *psycho_p;	/* pointer per-psycho soft state */
	dev_info_t *dip;		/* devinfo structure */
	u_int upa_id;			/* upa id */
	driver_state_t state;

	/*
	 * generic pci bus node information:
	 */
	pci_bus_range_t bus_range;	/* bus range property */

	/*
	 * device node address property:
	 */
	caddr_t address[3];

	/*
	 * access handles in case we need to map the registers ourself:
	 */
	ddi_acc_handle_t ac[3];

	/*
	 * virtual addresses of PBM registers:
	 */
	u_longlong_t *reg_base;
	volatile u_longlong_t *ctrl_reg;
	volatile u_longlong_t *sbuf_ctrl_reg;
	volatile u_longlong_t *sbuf_invl_reg;
	volatile u_longlong_t *sbuf_sync_reg;
	volatile u_longlong_t *afsr;
	volatile u_longlong_t *afar;
	volatile caddr_t config_space_base;
	volatile u_short *config_command_reg;
	volatile u_short *config_status_reg;
	volatile u_char *config_latency_reg;
	volatile u_longlong_t *sbuf_data_diag_acc;
	volatile u_longlong_t *sbuf_tag_diag_acc;

	/*
	 * address spaces:
	 */
	u_longlong_t base;		/* lowest chip address */
	u_longlong_t last;		/* highest chip address */
	u_longlong_t mem_base;		/* base of pci bus memory space */
	u_longlong_t io_base;		/* base of pci bus i/o space */
	u_longlong_t config_base;	/* base of pci bus config space */

	/*
	 * Interrupt cookie and pokefault mutex for async bus error
	 * interrupts.
	 */
#if defined(USE_DDI_ADD_INTR)
	ddi_iblock_cookie_t be_iblock_cookie;
#endif
	u_int be_mondo;
	kmutex_t pokefault_mutex;
	int pokefault;

	/*
	 * dma handle data:
	 */
	caddr_t handle_pool_base;	/* base for kmem_fast_alloc */
	kmutex_t handle_pool_mutex;	/* mutex for allocator */
	int handle_call_list_id;	/* call back list id */

	/*
	 * streaming cache information:
	 */
	kmutex_t sync_mutex;		/* mutex for flush/sync register */
	caddr_t sync_flag_base;		/* base va of sync flag block */
	u_longlong_t *sync_flag_vaddr;	/* va of sync flag */
	u_longlong_t sync_flag_addr;	/* pa of sync flag */
};


#define	PCI_SBUF_ENTRIES	16	/* number of i/o cache line */
#define	PCI_SBUF_LINE_SIZE	64	/* size of i/o cache line */
#define	PCI_SYNC_FLAG_SIZE	64	/* size of i/o cache sync buffer */

/*
 * PSYCHO INO macros:
 */
#define	OBIO_INO(ino)			((ino) & 0x20)
#define	MONDO_TO_INO(mondo)		((mondo) & 0x3f)
#define	MONDO_TO_UPA_ID(mondo)		(((mondo) >> 6) & 0x3f)
#define	MAKE_MONDO(upa_id, bus, dev, intr)	\
		(((upa_id) << 6) | (((bus) == 0 ? 1 : 0) << 4) | \
		(((dev) - 1) << 2) | ((intr) - 1))
#define	INO_TO_BUS(ino)			((((ino) & 0x30) == 0x0) ? 'A' : 'B')
#define	INO_TO_SLOT(ino)		(((ino) >> 2) & 0x3)

/*
 * PSYCHO and PBM soft state macros:
 */
#define	get_pci_soft_state(i)	\
	((pci_devstate_t *)ddi_get_soft_state(per_pci_state, (i)))

#define	alloc_pci_soft_state(i)	\
	ddi_soft_state_zalloc(per_pci_state, (i))

#define	free_pci_soft_state(i)	\
	ddi_soft_state_free(per_pci_state, (i))

#define	get_psycho_soft_state(i)	\
	((psycho_devstate_t *)ddi_get_soft_state(per_psycho_state, (i)))

#define	alloc_psycho_soft_state(i)	\
	ddi_soft_state_zalloc(per_psycho_state, (i))

#define	free_psycho_soft_state(i)	\
	ddi_soft_state_free(per_psycho_state, (i))

/*
 * misc macros for dma handles and cookies:
 */
#define	MAKE_DMA_COOKIE(cp, address, size)	\
	{					\
		(cp)->dmac_notused = 0;		\
		(cp)->dmac_type = 0;		\
		(cp)->dmac_address = (address);	\
		(cp)->dmac_size = (size);	\
	}

#define	HAS_REDZONE(mp)	(((mp)->dmai_rflags & DDI_DMA_REDZONE) ? 1 : 0)

/*
 * flags for overloading dmai_inuse field of the dma request
 * structure:
 */
#define	dmai_flags		dmai_inuse
#define	DMAI_FLAGS_INUSE	0x1
#define	DMAI_FLAGS_BYPASS	0x2
#define	DMAI_FLAGS_PEER_TO_PEER	0x4

/*
 * debugging definitions:
 */
#if defined(DEBUG)
#define	D_IDENTIFY	0x00000001
#define	D_ATTACH	0x00000002
#define	D_DETACH	0x00000004
#define	D_MAP		0x00000008
#define	D_CTLOPS	0x00000010
#define	D_G_ISPEC	0x00000020
#define	D_A_ISPEC	0x00000040
#define	D_R_ISPEC	0x00000080
#define	D_DMA_MAP	0x00000100
#define	D_DMA_CTL	0x00000200
#define	D_DMA_ALLOCH	0x00000400
#define	D_DMA_FREEH	0x00000800
#define	D_DMA_BINDH	0x00001000
#define	D_DMA_UNBINDH	0x00002000
#define	D_DMA_FLUSH	0x00004000
#define	D_DMA_WIN	0x00008000

#define	D_INTR		0x00010000
#define	D_RMAP		0x00020000
#define	D_MAP_WIN	0x00040000
#define	D_UNMAP_WIN	0x00080000
#define	D_CHK_TAR	0x00100000
#define	D_SBUF		0x00200000
#define	D_INIT_CLD	0x00400000
#define	D_RM_CLD	0x00800000
#define	D_GET_REG	0x01000000
#define	D_XLATE_REG	0x02000000

#define	D_BYPASS	0x04000000

#define	D_ERR_INTR	0x08000000

#define	D_CONFIG_ADDR	0x20000000

#define	D_DVMA_KADDR_LOAD	0x40000000

#define	D_DMA_REQ	0x80000000

#define	D_CONT		0x10000000

#define	DBG(flag, psp, fmt)	\
	pci_debug(flag, psp, fmt, 0, 0, 0, 0, 0);
#define	DBG1(flag, psp, fmt, a1)	\
	pci_debug(flag, psp, fmt, (int)(a1), 0, 0, 0, 0);
#define	DBG2(flag, psp, fmt, a1, a2)	\
	pci_debug(flag, psp, fmt, (int)(a1), (int)(a2), 0, 0, 0);
#define	DBG3(flag, psp, fmt, a1, a2, a3)	\
	pci_debug(flag, psp, fmt, (int)(a1), (int)(a2), (int)(a3), 0, 0);
#define	DBG4(flag, psp, fmt, a1, a2, a3, a4)	\
	pci_debug(flag, psp, fmt, (int)(a1), (int)(a2), (int)(a3), \
		(int)(a4), 0);
#define	DBG5(flag, psp, fmt, a1, a2, a3, a4, a5)	\
	pci_debug(flag, psp, fmt, (int)(a1), (int)(a2), (int)(a3), \
		(int)(a4), (int)(a5));

static void pci_debug(u_int, pci_devstate_t *, char *, int, int, int, int, int);
static void dump_dma_handle(u_int, pci_devstate_t *, ddi_dma_impl_t *);
#else
#define	DBG(flag, psp, fmt)
#define	DBG1(flag, psp, fmt, a1)
#define	DBG2(flag, psp, fmt, a1, a2)
#define	DBG3(flag, psp, fmt, a1, a2, a3)
#define	DBG4(flag, psp, fmt, a1, a2, a3, a4)
#define	DBG5(flag, psp, fmt, a1, a2, a3, a4, a5)
#define	dump_dma_handle(flag, psp, h)
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PCI_VAR_H */
