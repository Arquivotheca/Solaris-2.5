/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MCPZ8536_H
#define	_SYS_MCPZ8536_H

#pragma ident	"@(#)mcpz8536.h	1.4	94/01/06 SMI"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Register and Bit definition for the Zilog Z8536 Counter/Timer and
 *	Parallel I/O Unit.
 */

typedef struct	_ciochip_ {
	u_char	portc_data;	/* port C data register. */
	u_char	res1;

	u_char	portb_data;	/* port B data register. */
	u_char	res2;

	u_char	porta_data;	/* port A data register. */
	u_char	res3;

	u_char	cntrl;		/* Pointer register. */
	u_char	res4;
} cio_chip_t;

/*
 * Counter/Timer Chip Register Addresses
 *
 * Main Control Registers
 */

#define	CIO_MICR	0x00	/* Master Interrupt Control. */
#define	CIO_MCCR	0x01	/* Master Configuration Control. */
#define	CIO_PA_IVR	0x02	/* Port A IVR. */
#define	CIO_PB_IVR	0x03	/* Port B IVR. */
#define	CIO_CT_IVR	0x04	/* Counter/Timer IVR. */
#define	CIO_PC_DPPR	0x05	/* Counter/Timer IVR. */
#define	CIO_PC_DDR	0x06	/* Counter/Timer IVR. */
#define	CIO_PC_SIOCR	0x07	/* Counter/Timer IVR. */

#define	CIO_PA_CSR	0x08	/* Port A Command and Status */
#define	CIO_PB_CSR	0x09	/* Port B Command and Status */
#define	CIO_CT1_CSR	0x0A	/* CT 1 Command and Status */
#define	CIO_CT2_CSR	0x0B	/* CT 2 Command and Status */
#define	CIO_CT3_CSR	0x0C	/* Ct 3 Command and Status */
#define	CIO_PA_DATA	0x0D	/* Port A Data */
#define	CIO_PB_DATA	0x0E	/* Port A Data */
#define	CIO_PC_DATA	0x0F	/* Port A Data */

/*
 * Port A Specification Registers.
 */

#define	CIO_PA_MODE		0x20	/* Port A Mode Spec. */
#define	CIO_PA_HANDSHAKE	0x21	/* Port A Handshake Spec. */
#define	CIO_PA_DPPR		0x22	/* Port A Data Path Polarity */
#define	CIO_PA_DDR		0x23	/* Port A data direction */
#define	CIO_PA_SIOCR		0x24	/* Port A special I/O Control */
#define	CIO_PA_PP		0x25	/* Port A Pattern Polarity */
#define	CIO_PA_PT		0x26	/* Port A Pattern Transition */
#define	CIO_PA_PM		0x27	/* Port A Pattern Mask. */

/*
 * Port B Specification Registers.
 */

#define	CIO_PB_MODE		0x28	/* Port B Mode Spec. */
#define	CIO_PB_HANDSHAKE	0x29	/* Port B Handshake Spec. */
#define	CIO_PB_DPPR		0x2A	/* Port B Data Path Polarity */
#define	CIO_PB_DDR		0x2B	/* Port B data direction */
#define	CIO_PB_SIOCR		0x2C	/* Port B special I/O Control */
#define	CIO_PB_PP		0x2D	/* Port B Pattern Polarity */
#define	CIO_PB_PT		0x2E	/* Port B Pattern Transition */
#define	CIO_PB_PM		0x2F	/* Port B Pattern Mask. */

/*
 * CIO Chip Register Vals.
 */

#define	CIO_ALL_INPUT		0xFF	/* Port Data Direction Register */
#define	CIO_ALL_ONE		0xFF	/* Port Pattern Mask Register */
#define	CIO_BIT_PORT_MODE	0x06	/* Port Mode Spec register. */

#define	CIO_CLRIP		0x20	/* Port Command and Status Reg */
#define	CIO_FIFO_EMPTY		0x80	/* Port A Data Reg */
#define	CIO_FIFO_EPTY		0x10
#define	CIO_FIFO_FULL		0x40	/* Port A Data Reg */
#define	CIO_IP			0x90	/* Port command and status reg */
#define	CLR_IP			0xDF	/* Clear IP in port c/s reg. */
#define	CLR_RESET		0x00	/* Master Intr Cntrl Reg */

#define	EOP_INVERT		0x3F	/* Port B data polarity */
#define	EOP_ONE			0x3F	/* Port B pattern reg */
#define	EOP_ONES_CATCHER	0x1F	/* Port A SIOCR */

#define	FIFO_EMPTY_INTR_ONLY	0x10	/* FIFO status in intr. */
#define	FIFO_NON_INVERT		0x0F	/* Port A data path polarity. */
#define	FIFO_NOT_ONE		0xEF	/* FIFO !empty in pattern polarity. */

#define	INPUT_BIT		0x1	/* Port Data Direction Register */
#define	INVERTING		0x1	/* Port Data Path Polarity Register */
#define	MASTER_ENABLE		0x94	/* Master Config Control Register */
#define	MASTER_INT_ENABLE	0x9E	/* Master Interrupt Control Register */
#define	MCP_IE			0x08	/* set VMEINTEN to enable interrupts */
#define	NON_INVERTING		0x0	/* Port Data Path Polarity Register */
#define	NORMAL_IO		0x0	/* Port Special I/O Control Register */
#define	ONES_CATCHER		0x1	/* Port Special I/O Control Register */
#define	OUTPUT_BIT		0x0	/* Port Data Direction Register */
#define	PORT0_RS232_SEL		0x1	/* Port C Data Register masks for */
#define	PORT1_RS232_SEL		0x2	/* RS232/449 selection...0 == 449 */
#define	PORT_INT_ENABLE		0xC0	/* Port Command and Status Register */

/*
 * Macros to access the registers
 */

#define	CIO_RX_DMARESET(x)	((x)->portb_data = 0xEF)
#define	CIO_DMARESET(x, y)	((x)->portb_data = ~(1 << (((y) & 0x0e) >> 1)))
#define	CIO_MCPBASE(x)		(((x) & 0x0e) << 1)

#define	CIO_READ(cp, reg, var) { \
	(cp)->cntrl = reg; \
	var = (cp)->cntrl; \
}

#define	CIO_WRITE(cp, reg, val) { \
	(cp)->cntrl = (char)reg; \
	(cp)->cntrl = (char)val; \
}

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_MCPZ8536_H */
