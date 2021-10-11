/*
 * Copyright (c) 1990 by Sun Microsystems, Inc.
 */

#pragma ident	"@(#)iocache.c	1.17	95/06/14 SMI"

#include <sys/types.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <sys/cmn_err.h>
#include <vm/hat_sfmmu.h>

#include <sys/iommu.h>
#include <sys/iocache.h>
#include <sys/sysiosbus.h>

#include <sys/nexusdebug.h>
#define	IOCACHE_REGISTERS_DEBUG		0x1
#define	IOCACHE_SYNC_DEBUG		0x2

/* Flag which enables the streaming buffer */
int stream_buf_on = 1;

int
stream_buf_init(struct sbus_soft_state *softsp, int address)
{
/*LINTED warning: set but not used in function */
	volatile u_ll_t tmpctrlreg;
	u_char version;
#ifdef DEBUG
	debug_info = 1;
	debug_print_level = 0;
#endif
	version = (u_char) (*softsp->sysio_ctrl_reg >> SYSIO_VER_SHIFT);
	version &= 0xf;

	if (stream_buf_on == 0 || version == 0) {
		softsp->stream_buf_off = STREAM_BUF_OFF;
		if (version == 0)
			cmn_err(CE_CONT, "Disabling streaming buffer due to "
			    "SYSIO Rev %d.\n", version);
		return (DDI_SUCCESS);
	}

	/*
	 * Simply add each registers offset to the base address
	 * to calculate the already mapped virtual address of
	 * the device register...
	 *
	 * define a macro for the pointer arithmetic; all registers
	 * are 64 bits wide and are defined as u_ll_t's.
	 */

#define	REG_ADDR(b, o)	(u_ll_t *)(unsigned)((unsigned)(b) + (unsigned)(o))

	softsp->str_buf_ctrl_reg = REG_ADDR(address, OFF_STR_BUF_CTRL_REG);
	softsp->str_buf_flush_reg = REG_ADDR(address, OFF_STR_BUF_FLUSH_REG);
	softsp->str_buf_sync_reg = REG_ADDR(address, OFF_STR_BUF_SYNC_REG);

#undef	REG_ADDR

	DPRINTF(IOCACHE_REGISTERS_DEBUG, ("Streaming buffer control reg: 0x%x, "
	    "Streaming buffer flush reg: 0x%x, Streaming buffer sync reg: 0x%x",
	    softsp->str_buf_ctrl_reg, softsp->str_buf_flush_reg,
	    softsp->str_buf_sync_reg));

	/* Initialize stream buffer sync reg mutex */
	mutex_init(&softsp->sync_reg_lock, "stream buffer sync reg lock",
	    MUTEX_DEFAULT, NULL);

	/* Turn on per instance streaming buffer flag */
	softsp->stream_buf_off = 0;

	/* Turn on the streaming buffer */
	*softsp->str_buf_ctrl_reg = STREAM_BUF_ENABLE;
	tmpctrlreg = *softsp->str_buf_ctrl_reg;

	return (DDI_SUCCESS);
}

/*
 * Initialize stream buf hardware when the system is being resumed.
 * (Subset of stream_buf_init())
 */
int
stream_buf_resume_init(struct sbus_soft_state *softsp)
{
	u_char version;

	version = (u_char) (*softsp->sysio_ctrl_reg >> SYSIO_VER_SHIFT);
	version &= 0xf;

	if (stream_buf_on == 0 || version == 0) {
		softsp->stream_buf_off = STREAM_BUF_OFF;
		return (DDI_SUCCESS);
	}

	/* Turn on the streaming buffer */
	*softsp->str_buf_ctrl_reg = STREAM_BUF_ENABLE;

	return (DDI_SUCCESS);
}

void
sync_stream_buf(struct sbus_soft_state *softsp, u_long addr, u_int size)
{
	extern clock_t lbolt;
	clock_t start_bolt;
	u_int npages;
	u_long offset;
	u_long savaddr = addr;
	int loopcnt = 0;
	int sync_flag = 0;
#ifndef lint
	volatile u_ll_t tmp;
#endif

	if (softsp->stream_buf_off != 0)
		return;

	/* Prepare to sync the flush with the uPA coherence domain */
	mutex_enter(&softsp->sync_reg_lock);

	/*
	 * Cause the flush on all virtual pages of the transfer.
	 */
again:
	offset = addr & IOMMU_PAGEOFFSET;
	addr &= ~IOMMU_PAGEOFFSET;
	npages = iommu_btopr(offset + size);

	DPRINTF(IOCACHE_SYNC_DEBUG, ("sync_stream_buf: ioaddr 0x%x, size "
	    "0x%x, page cnt 0x%x\n", addr, size, npages));

	while (npages) {
		*softsp->str_buf_flush_reg = (u_ll_t) addr;
		addr += IOMMU_PAGESIZE;
		npages--;
	}

	/* Ask the hardware to flag when the flush is complete */
	*softsp->str_buf_sync_reg = va_to_pa((caddr_t) &sync_flag);
#ifndef lint
	tmp = *softsp->sbus_ctrl_reg;
#endif

	/*
	 * spin till the hardware is done or till it timeouts
	 */
	start_bolt = lbolt;
	while (!sync_flag &&
	    (ulong) (lbolt - start_bolt < STREAM_BUF_SYNC_WAIT));

	/* The hardware didn't complete. It timed out. */
	if (!sync_flag) {
		if (loopcnt >= 10)
			cmn_err(CE_PANIC, "Streaming buffer timed out %d "
			    "times\n", loopcnt);

		loopcnt++;
		cmn_err(CE_WARN, "Stream buffer on sbus%d timed out.  Trying "
		    "%d more iterations.\n",
		    ddi_get_instance(softsp->dip), 10 - loopcnt);

		addr = savaddr;
		goto again;
	}

	mutex_exit(&softsp->sync_reg_lock);
}
