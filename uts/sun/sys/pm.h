/*
 * Copyright (c) 1993 by Sun Microsystems, Inc.
 */

#ifndef	_SYS_PM_H
#define	_SYS_PM_H

#pragma ident	"@(#)pm.h	1.9	94/07/01 SMI"

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum {
	PM_SCHEDULE,
	PM_GET_IDLE_TIME,
	PM_GET_NUM_CMPTS,
	PM_GET_THRESHOLD,
	PM_SET_THRESHOLD,
	PM_GET_POWER,
	PM_SET_POWER,
	PM_GET_CUR_PWR,
	PM_GET_NUM_DEPS,
	PM_GET_DEP,
	PM_ADD_DEP,
	PM_REM_DEP,
	PM_REM_DEVICE,
	PM_REM_DEVICES
} pm_cmds;


typedef struct {
	char	*who;		/* Device to configure */
	int	select;		/* Selects the component or dependent */
				/* of the device */
	int	level;		/* Power or threshold level */
	char	*dependent;	/* Buffer to hold name of dependent */
	int	size;		/* Size of dependent buffer */
} pm_request;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_PM_H */
