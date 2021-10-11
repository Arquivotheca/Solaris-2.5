#pragma ident	"@(#)uname.c	1.4	92/07/20 SMI" 

#include	<errno.h>
#include	<string.h>
#include	<sys/utsname.h>
#include	<sys/syscall.h>

/* 
 * utsname structure has a different format in SVr4/SunOS 5.0.
 * The data needs to be mapped before returning to the user.
 */

/* 
 * The following values and structure are from the SVR4 utsname.h.
 */
#define		NEW_SYS_NMLN	257
#define 	SYS_NMLN	9
#define		SYS_NDLN	65

struct n_utsname {
	char sysname[NEW_SYS_NMLN];
	char nodename[NEW_SYS_NMLN];
	char release[NEW_SYS_NMLN];
	char version[NEW_SYS_NMLN];
	char machine[NEW_SYS_NMLN];
};

int uname( uts )
register struct utsname *uts;		/* where to put results */
{
	return(bc_uname(uts));
}

int bc_uname( uts )
struct utsname *uts;
{
	struct n_utsname n_uts;
	int    ret;

	if ((ret = _syscall(SYS_uname, &n_uts)) != -1) {
		memcpy(uts->sysname, n_uts.sysname, SYS_NMLN);
		if (strlen(n_uts.sysname) > SYS_NMLN)
			uts->sysname[SYS_NMLN-1] = '\0';
		memcpy(uts->nodename, n_uts.nodename, SYS_NDLN);
		if (strlen(n_uts.nodename) > SYS_NDLN)
			uts->nodename[SYS_NDLN-1] = '\0';
		memcpy(uts->release, n_uts.release, SYS_NMLN);
		if (strlen(n_uts.release) > SYS_NMLN)
			uts->release[SYS_NMLN-1] = '\0';
		memcpy(uts->version, n_uts.version, SYS_NMLN);
		if (strlen(n_uts.version) > SYS_NMLN)
			uts->version[SYS_NMLN-1] = '\0';
		memcpy(uts->machine, n_uts.machine, SYS_NMLN);
		if (strlen(n_uts.machine) > SYS_NMLN)
			uts->machine[SYS_NMLN-1] = '\0';
	}

	return(ret);
}
