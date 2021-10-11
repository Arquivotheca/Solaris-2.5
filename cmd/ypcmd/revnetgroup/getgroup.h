
#ident	"@(#)getgroup.h	1.2	92/07/14 SMI"        /* SMI4.1 1.4 */

struct grouplist {		
	char *gl_machine;
	char *gl_name;
	char *gl_domain;
	struct grouplist *gl_nxt;
};

struct grouplist *my_getgroup();

			
