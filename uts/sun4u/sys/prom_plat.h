/*
 * Copyright (c) 1991-1995, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_PROM_PLAT_H
#define	_SYS_PROM_PLAT_H

#pragma ident	"@(#)prom_plat.h	1.17	95/06/05 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * This file contains external platform-specific promif interface definitions.
 * There may be none.  This file is included by reference in <sys/promif.h>
 *
 * This version of the file is for the IEEE 1275-1994 compliant Fusion prom.
 */

/*
 * Max pathname size, in bytes:
 */
#define	OBP_MAXPATHLEN		256

/*
 * Memory allocation plus memory/mmu interfaces:
 *
 * Routines with fine-grained memory and MMU control are platform-dependent.
 *
 * MMU node virtualized "mode" arguments and results for Spitfire MMU:
 * XXX The MMU virtualized "mode"s are a proposal.
 * XXX Verify the default settings.
 *
 * The default virtualized "mode" for client program mappings created
 * by the firmware is as follows:
 *
 * G (global)		Clear
 * L (locked)		Clear
 * W (write)		Set
 * R (read - soft)	Set (Prom is not required to implement soft bits)
 * X (exec - soft)	Set (Prom is not required to implement soft bits)
 * CV,CP (Cacheable)	Set if memory page, Clear if IO page
 * E (side effects)	Clear if memory page; Set if IO page
 * IE (Invert endian.)	Clear
 *
 * The following fields are initialized as follows in the TTE-data for any
 * mappings created by the firmware on behalf of the client program:
 *
 * P (Priviledged)	Set
 * V (Valid)		Set
 * NFO (No Fault Only)	Clear
 * Context		0
 * Soft bits		< private to the firmware implementation >
 *
 * Page size of Prom mappings are typically 8k, "modify" cannot change
 * page sizes. Mappings created by "map" are 8k pages.
 *
 * If the virtualized "mode" is -1, the defaults as shown above are used,
 * otherwise the virtualized "mode" is set (and returned) based on the
 * following virtualized "mode" abstractions. The mmu node "translations"
 * property contains the actual tte-data, not the virtualized "mode".
 *
 * Note that client programs may not create locked mappings by setting
 * the LOCKED bit. There are Spitfire specific client interfaces to create
 * and remove locked mappings. (SUNW,{i,d}tlb-load).
 * The LOCKED bit is defined here since it may be returned by the
 * "translate" method.
 *
 * The PROM is not required to implement the Read and eXecute soft bits,
 * and is not required to track them for the client program. They may be
 * set on calls to "map" and "modfify" and may be ignored by the firmware,
 * and are not necessarily returned from "translate".
 *
 * The TTE soft bits are private to the firmware.  No assumptions may
 * be made regarding the contents of the TTE soft bits.
 *
 * Changing a mapping from cacheable to non-cacheable implies a flush
 * or invalidate operation, if necessary.
 *
 * NB: The "map" MMU node method should NOT be used to create IO device
 * mappings. The correct way to do this is to call the device's parent
 * "map-in" method using the CALL-METHOD client interface service.
 */

#define	PROM_MMU_MODE_DEFAULT	((int)-1)	/* Default "mode", see above */

/*
 * NB: These are not implemented in PROM version P1.0 ...
 */
#define	PROM_MMU_MODE_WRITE	0x0001	/* Translation is Writable */
#define	PROM_MMU_MODE_READ	0x0002	/* Soft: Readable, See above */
#define	PROM_MMU_MODE_EXEC	0x0004	/* Soft: eXecutable, See above */
#define	PROM_MMU_MODE_RWX_MASK	0x0007	/* Mask for R-W-X bits */

#define	PROM_MMU_MODE_LOCKED	0x0010	/* Read-only: Locked; see above */
#define	PROM_MMU_MODE_CACHED	0x0020	/* Set means both CV,CP bits */
#define	PROM_MMU_MODE_EFFECTS	0x0040	/* side Effects bit in MMU */
#define	PROM_MMU_MODE_GLOBAL	0x0080	/* Global bit */
#define	PROM_MMU_MODE_INVERT	0x0100	/* Invert Endianness */

/*
 * prom_alloc is platform dependent and has historical semantics
 * associated with the align argument and the return value.
 * prom_malloc is the generic memory allocator.
 */
extern	caddr_t		prom_malloc(caddr_t virt, u_int size, u_int align);

extern	caddr_t		prom_allocate_virt(u_int align, u_int size);
extern	caddr_t		prom_claim_virt(u_int size, caddr_t virt);
extern	void		prom_free_virt(u_int size, caddr_t virt);

extern	int		prom_allocate_phys(u_int size, u_int align,
			    u_int *base_hi, u_int *base_lo);
extern	int		prom_claim_phys(u_int size, u_int phys_hi,
			    u_int phys_lo);
extern	void		prom_free_phys(u_int size, u_int phys_hi,
			    u_int phys_lo);

extern	int		prom_map_phys(int mode, u_int size, caddr_t virt,
			    u_int phys_hi, u_int phys_lo);
extern	void		prom_unmap_phys(u_int size, caddr_t virt);
extern	void		prom_unmap_virt(u_int size, caddr_t virt);

/*
 * prom_retain allocates or returns retained physical memory
 * identified by the arguments of name string "id", "size" and "align".
 */
extern	int		prom_retain(char *id, u_int size, u_int align,
			    u_int *phys_hi, u_int *phys_lo);

/*
 * prom_translate_virt returns the physical address and virtualized "mode"
 * for the given virtual address. After the call, if *valid is non-zero,
 * a mapping to 'virt' exists and the physical address and virtualized
 * "mode" were returned to the caller.
 */
extern	int		prom_translate_virt(caddr_t virt, int *valid,
			    u_int *phys_hi, u_int *phys_lo, int *mode);

/*
 * prom_modify_mapping changes the "mode" of an existing mapping or
 * repeated mappings. virt is the virtual address whose "mode" is to
 * be changed; size is some multiple of the fundamental pagesize.
 * This method cannot be used to change the pagesize of an MMU mapping,
 * nor can it be used to Lock a translation into the i or d tlb.
 */
extern	int		prom_modify_mapping(caddr_t virt, u_int size, int mode);

/*
 * Client interfaces for managing the {i,d}tlb handoff to client programs.
 */
extern	int		prom_itlb_load(int index,
			    unsigned long long tte_data, caddr_t virt);

extern	int		prom_dtlb_load(int index,
			    unsigned long long tte_data, caddr_t virt);

/*
 * Administrative group: OBP only and SMCC platform specific.
 * XXX: IDPROM related stuff should be replaced with specific data-oriented
 * XXX: functions.
 */

extern	int		prom_heartbeat(int msecs);
extern	int		prom_get_unum(int syn_code, u_int physlo, u_int physhi,
				char *buf, u_int buflen, int *ustrlen);

extern	int		prom_getidprom(caddr_t addr, int size);
extern	int		prom_getmacaddr(ihandle_t hd, caddr_t ea);

/*
 * CPU Control Group: MP's only.
 */
extern	int		prom_startcpu(dnode_t node, caddr_t pc, int arg);
extern	int		prom_stop_self(void);
extern	int		prom_idle_self(void);
extern	int		prom_resumecpu(dnode_t node);

/*
 * Set trap table
 */
extern	void		prom_set_traptable(void *tba_addr);

/*
 * Power-off
 */
extern	void		prom_power_off(void);

/*
 * The client program implementation is required to provide a wrapper
 * to the client handler, for the 32 bit client program to 64 bit cell-sized
 * client interface handler (switch stack, etc.).  This function is not
 * to be used externally!
 */

extern	int		client_handler(void *cif_handler, void *arg_array);

/*
 * The 'format' of the "translations" property in the 'mmu' node ...
 */

struct translation {
	u_int	virt_hi;		/* uppper 32 bits of vaddr */
	u_int	virt_lo;		/* lower 32 bits of vaddr */
	u_int	size_hi;		/* upper 32 bits of size in bytes */
	u_int	size_lo;		/* lower 32 bits of size in bytes */
	u_int	tte_hi;			/* higher 32 bites of tte */
	u_int	tte_lo;			/* lower 32 bits of tte */
};

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_PROM_PLAT_H */
