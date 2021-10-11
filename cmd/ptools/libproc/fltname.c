#ident	"@(#)fltname.c	1.1	94/11/10 SMI"

#include <stdio.h>
#include <sys/types.h>
#include <sys/fault.h>

static char flt_name[20];

char *
rawfltname(flt)		/* return the name of the fault */
int flt;		/* return NULL if unknown fault */
{
	register char * name;

	switch (flt) {
	case FLTILL:	name = "FLTILL";	break;
	case FLTPRIV:	name = "FLTPRIV";	break;
	case FLTBPT:	name = "FLTBPT";	break;
	case FLTTRACE:	name = "FLTTRACE";	break;
	case FLTACCESS:	name = "FLTACCESS";	break;
	case FLTBOUNDS:	name = "FLTBOUNDS";	break;
	case FLTIOVF:	name = "FLTIOVF";	break;
	case FLTIZDIV:	name = "FLTIZDIV";	break;
	case FLTFPE:	name = "FLTFPE";	break;
	case FLTSTACK:	name = "FLTSTACK";	break;
	case FLTPAGE:	name = "FLTPAGE";	break;
	default:	name = NULL;		break;
	}

	return name;
}

char *
fltname(flt)		/* return the name of the fault */
int flt;		/* manufacture a name if fault unknown */
{
	register char * name = rawfltname(flt);

	if (name == NULL) {			/* manufacture a name */
		(void) sprintf(flt_name, "FLT#%d", flt);
		name = flt_name;
	}

	return name;
}
