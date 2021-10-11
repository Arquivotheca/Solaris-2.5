/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ifndef _SYS_MCPZSDEV_H
#define	_SYS_MCPZSDEV_H

#pragma ident	"@(#)mcpzsdev.h	1.4	94/01/06 SMI"

#include <sys/zsdev.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern int mcp_zs_usec_delay;
#define	MCP_ZSDELAY() drv_usecwait(mcp_zs_usec_delay)

#define	MCP_SCC_WRITE(reg, val)  { \
	zs->zs_addr->zscc_control = reg; \
	MCP_ZSDELAY(); \
	zs->zs_addr->zscc_control = val; \
	MCP_ZSDELAY(); \
	zs->zs_wreg[reg] = val; \
}

#define	MCP_SCC_READ(reg, var) { \
	zs->zs_addr->zscc_control = reg; \
	MCP_ZSDELAY(); \
	var = zs->zs_addr->zscc_control; \
	MCP_ZSDELAY(); \
}

#define	MCP_SCC_BIS(reg, val) { \
	zs->zs_addr->zscc_control = reg; \
	MCP_ZSDELAY(); \
	zs->zs_wreg[reg] |= val; \
	zs->zs_addr->zscc_control = zs->zs_wreg[reg]; \
	MCP_ZSDELAY(); \
}

#define	MCP_SCC_BIC(reg, val) { \
	zs->zs_addr->zscc_control = reg; \
	MCP_ZSDELAY(); \
	zs->zs_wreg[reg] &= ~val; \
	zs->zs_addr->zscc_control = zs->zs_wreg[reg]; \
	MCP_ZSDELAY(); \
}

#define	MCP_SCC_WRITE0(val) { \
	zs->zs_addr->zscc_control = val; \
	MCP_ZSDELAY(); \
}

#define	MCP_SCC_WRITEDATA(val) { \
	zs->zs_addr->zscc_data = val; \
	MCP_ZSDELAY(); \
}

#define	MCP_SCC_READ0()		zs->zs_addr->zscc_control
#define	MCP_SCC_READDATA()	zs->zs_addr->zscc_data

#ifdef	__cplusplus
}
#endif

#endif /* !_SYS_MCPZSDEV_H */
