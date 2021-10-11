/*
 * Copyright (c) 1992-1993, by Sun Microsystems Inc.
 */

#pragma	ident	"@(#)instance.c	1.15	94/03/31 SMI"

/*
 * Instance number assignment code
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/kobj.h>
#include <sys/t_lock.h>
#include <sys/kmem.h>
#include <sys/cmn_err.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/autoconf.h>
#include <sys/hwconf.h>		/* XXX wrong name here */
#include <sys/reboot.h>
#include <sys/ddi_impldefs.h>
#include <sys/instance.h>
#include <sys/debug.h>

static int in_get_infile(int);
static u_int in_next_instance(struct devnames *dnp);
static void in_removenode(struct devnames *dnp, in_node_t *mp, in_node_t *ap);
static in_node_t *in_alloc_node(char *name, char *addr);
static int in_eqstr(char *a, char *b);
static char *in_name_addr(char **cpp, char **addrp);
static in_node_t *in_devwalk(dev_info_t *dip, in_node_t **ap);
static void in_dealloc_node(in_node_t *np);
static void in_hash_walk(in_node_t *np);
static void in_pathin(char *cp, u_int instance);
static in_node_t *in_make_path(char *path);
static void in_enlist(in_node_t *ap, in_node_t *np);
static int in_inuse(u_int instance, char *name);
static void in_hashin(in_node_t *np);
static int in_ask_rebuild(void);

in_softstate_t e_ddi_inst_state;

/*
 * State transition information:
 * e_ddi_inst_state contains, among other things, the root of a tree of
 * nodes used to track instance number assignment.
 * Each node can be in one of 3 states, indicated by in_state:
 * IN_UNKNOWN:	Each node is created in this state.  The instance number of
 *	this node is not known.  in_instance is set to -1.
 * IN_PROVISIONAL:  When a node is assigned an instance number in
 *	e_ddi_assign_instance(), its state is set to IN_PROVISIONAL.
 *	Subsequently, the framework will always call either
 *	e_ddi_keep_instance() which makes the node IN_PERMANENT,
 *	or e_ddi_free_instance(), which deletes the node.
 * IN_PERMANENT:
 *	If e_ddi_keep_instance() is called on an IN_PROVISIONAL node,
 *	its state is set to IN_PERMANENT.
 *
 *	During the processing of the /etc/path_to_inst file in
 *	e_ddi_instance_init(), after all nodes that have been explicitly
 *	assigned instance numbers in path_to_inst have been processed,
 *	all inferred nodes (nexus nodes that have only been encountered in
 *	the path to an explicitly assigned node) are assigned instance
 *	numbers in in_hashin() and their state changed from IN_UNKNOWN
 *	to IN_PERMANENT.
 */

/*
 * This can be undefined (and the code removed as appropriate)
 * when the real devfs is done.
 */
#define	INSTANCE_TRANSITION_MODE

#ifdef	INSTANCE_TRANSITION_MODE
/*
 * This flag generates behaviour identical to that before persistent
 * instance numbers, allowing us to boot without having installed a
 * correct /etc/path_to_inst file.
 */
static int in_transition_mode;

#endif	/* INSTANCE_TRANSITION_MODE */

/*
 * Someday when we are convinced that no one has set path_to_inst_bootstrap
 * in their /etc/system file, we can undefine this.
 */
#define	PTI_TRANSITION_MODE

#ifdef PTI_TRANSITION_MODE
/*
 * path_to_inst_bootstrap is no longer used.  We leave it here so that there
 * won't be a booting problem with systems that get upgraded but don't remove
 * this variable from their /etc/system files.  In e_ddi_instance_init, we
 * reset this variable to 0 and print out a message to the user if they had
 * set it to something other than 0 in their /etc/system file.
 */
int path_to_inst_bootstrap = 0;

#endif /* PTI_TRANSITION_MODE */

static char *instance_file = INSTANCE_FILE;

/*
 * XXX this is copied from sun/os/modsubr.c--it should be moved out into
 * XXX an include file and included both places (modctl.h?)
 * XXX or better yet, it should be allocated and freed in read_binding_file
 */
#define	HASHTABSIZE 64
#define	HASHMASK (HASHTABSIZE-1)

/*
 * Return values for in_get_infile().
 */
#define	PTI_FOUND	0
#define	PTI_NOT_FOUND	1
#define	PTI_REBUILD	2

/*
 * Path to instance file magic string used for first time boot after
 * an install.  If this is the first string in the file we will
 * automatically rebuild the file.
 */
#define	PTI_MAGIC_STR		"#path_to_inst_bootstrap_1"
#define	PTI_MAGIC_STR_LEN	25

void
e_ddi_instance_init(void)
{
	int hshndx;
	struct bind *bp, *bp1;
	struct bind **in_hashtab = NULL;
	struct in_node *np, *nnp;

	extern int read_binding_file(char *, struct bind **);

	mutex_init(&e_ddi_inst_state.in_serial, "instance serial",
	    MUTEX_DEFAULT, NULL);
	cv_init(&e_ddi_inst_state.in_serial_cv, "instance serial",
	    CV_DEFAULT, NULL);

	/*
	 * Only one thread is allowed to change the state of the instance
	 * number assignments on the system at any given time.
	 * Note that this is not really necessary, as we are single-threaded
	 * here, but it won't hurt, and it allows us to keep ASSERTS for
	 * our assumptions in the code.
	 */
	mutex_enter(&e_ddi_inst_state.in_serial);
	while (e_ddi_inst_state.in_busy)
		cv_wait(&e_ddi_inst_state.in_serial_cv,
		    &e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 1;
	mutex_exit(&e_ddi_inst_state.in_serial);

#ifdef PTI_TRANSITION_MODE
	/*
	 * Check to see if path_to_inst_bootstrap was set in the /etc/system
	 * file.  If path_to_inst_bootstrap != 0, then it was set.  So set it
	 * back to 0 since we don't use it anymore and print out a message
	 */
	if (path_to_inst_bootstrap != 0) {
		cmn_err(CE_WARN, "path_to_inst_bootstrap, set in /etc/system"
		    " no longer used.");
		path_to_inst_bootstrap = 0;
	}
#endif /* PTI_TRANSITION_MODE */

	/*
	 * Create the root node, instance zallocs to 0.
	 * The name and address of this node never get examined, we always
	 * start searching with its first child.
	 */
	ASSERT(e_ddi_inst_state.in_root == NULL);
	e_ddi_inst_state.in_root = in_alloc_node(NULL, NULL);

	switch (in_get_infile((boothowto & RB_ASKNAME))) {
	case PTI_REBUILD:
		/*
		 * The file does not exist, but user booted -a and wants
		 * to rebuild the file, or this is the first boot after
		 * an install and the magic string is all that is in the
		 * file.
		 */
#ifdef	INSTANCE_TRANSITION_MODE
		in_transition_mode = 1;
#endif	/* INSTANCE_TRANSITION_MODE */

		/*
		 * Need to set the reconfigure flag so that the /devices
		 * and /dev directories gets rebuild.
		 */
		boothowto |= RB_RECONFIG;

		cmn_err(CE_CONT,
			"?Using default device instance data\n");
		break;

	case PTI_FOUND:
		/*
		 * Assertion:
		 *	We've got a readable file
		 */
		in_hashtab = kmem_zalloc(sizeof (struct bind) * HASHTABSIZE,
		    KM_SLEEP);
		(void) read_binding_file(instance_file, in_hashtab);
		/*
		 * This really needs to be a call into the mod code
		 */
		for (hshndx = 0; hshndx < HASHTABSIZE; hshndx++) {
			bp = in_hashtab[hshndx];
			while (bp) {
				if (*bp->b_name == '/')
					in_pathin(bp->b_name, bp->b_num);
				else
					cmn_err(CE_WARN,
					    "invalid instance file entry %s %d",
					    bp->b_name, bp->b_num);
				bp1 = bp;
				bp = bp->b_next;
				kmem_free(bp1->b_name, strlen(bp1->b_name) + 1);
				kmem_free(bp1, sizeof (struct bind));
			}
		}
		kmem_free(in_hashtab, sizeof (struct bind) * HASHTABSIZE);

		/*
		 * Walk the tree, calling in_hashin() on every node that
		 * already has an instance number assigned
		 */
		in_hash_walk(e_ddi_inst_state.in_root->in_child);

		/*
		 * Now do the ones that did not
		 */
		np = e_ddi_inst_state.in_no_instance;
		e_ddi_inst_state.in_no_instance = NULL;
		while (np) {
			nnp = np->in_next;
			np->in_next = NULL;
			in_hashin(np);
			np = nnp;
		}
		break;

	default:
	case PTI_NOT_FOUND:
		/*
		 * .. else something is terribly wrong.
		 *
		 * We're paranoid here because the loss of this file
		 * is potentially damaging to user data e.g. a
		 * controller slips and we swap on someones database..
		 * Oops.
		 *
		 * This is the rather vicious and cruel version
		 * favoured by some.  If you can't find path_to_inst
		 * and you're not booting with '-a' then just halt
		 * the system.
		 *
		 */
		cmn_err(CE_CONT, "Cannot open '%s'\n", instance_file);
		halt((char *)NULL);
		/*NOTREACHED*/
		break;

	}

	mutex_enter(&e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 0;
	cv_broadcast(&e_ddi_inst_state.in_serial_cv);
	mutex_exit(&e_ddi_inst_state.in_serial);
}

/*
 * Checks to see if the /etc/path_to_inst file exists and whether or not
 * it has the magic string in it.
 *
 * Returns one of the following:
 *
 *	PTI_FOUND	- We have found the /etc/path_to_inst file
 *	PTI_REBUILD	- We did not find the /etc/path_to_inst file, but
 *			  the user has booted with -a and wants to rebuild it
 *			  or we have found the /etc/path_to_inst file and the
 *			  first line was PTI_MAGIC_STR.
 *	PTI_NOT_FOUND	- We did not find the /etc/path_to_inst file
 *
 */
static int
in_get_infile(int ask_rebuild)
{
	int file;
	int return_val;
	char buf[PTI_MAGIC_STR_LEN];

	/*
	 * Try to open the file.  If the user booted -a (ask_rebuild == TRUE if
	 * the user booted -a) and the file was not found, then ask the
	 * user if they want to rebuild the path_to_inst file.  If not,
	 * then return file not found, else return rebuild.
	 */
	if ((file = kobj_open(instance_file)) == -1) {
		if (ask_rebuild && in_ask_rebuild())
			return (PTI_REBUILD);
		else
			return (PTI_NOT_FOUND);
	} else {
		return_val = PTI_FOUND;
	}

	/*
	 * Read the first PTI_MAGIC_STR_LEN bytes from the file to see if
	 * it contains the magic string.  If there aren't that many bytes
	 * in the file, then assume file is correct and no magic string
	 * and move on.
	 */
	switch (kobj_read(file, buf, PTI_MAGIC_STR_LEN, 0)) {

	case PTI_MAGIC_STR_LEN:
		/*
		 * If the first PTI_MAGIC_STR_LEN bytes are the magic string
		 * then return PTI_REBUILD.
		 */
		if (strncmp(PTI_MAGIC_STR, buf, PTI_MAGIC_STR_LEN) == 0)
			return_val = PTI_REBUILD;
		break;

	case 0:
		/*
		 * If the file is zero bytes in length, then consider the
		 * file to not be found and ask the user if they want to
		 * rebuild it (if ask_rebuild is true).
		 */
		if (ask_rebuild && in_ask_rebuild())
			return_val = PTI_REBUILD;
		else
			return_val = PTI_NOT_FOUND;
		break;

	default: /* Do nothing we have a good file */
		break;
	}

	kobj_close(file);
	return (return_val);
}

static int
in_ask_rebuild(void)
{
	char answer[32];

	extern void gets(char *);

	do {
		answer[0] = '\0';
		printf(
		    "\nThe %s on your system does not exist or is empty.\n"
		    "Do you want to rebuild this file [n]? ",
		    instance_file);
		gets(answer);
		if ((answer[0] == 'y') || (answer[0] == 'Y'))
			return (1);
	} while ((answer[0] != 'n') && (answer[0] != 'N') &&
	    (answer[0] != '\0'));
	return (0);
}

static void
in_hash_walk(in_node_t *np)
{
	while (np) {
		if (np->in_state == IN_UNKNOWN) {
			np->in_next = e_ddi_inst_state.in_no_instance;
			e_ddi_inst_state.in_no_instance = np;
		} else
			in_hashin(np);
		if (np->in_child)
			in_hash_walk(np->in_child);
		np = np->in_sibling;
	}
}

int
is_pseudo_device(dev_info_t *dip)
{
	dev_info_t	*pdip;

	for (pdip = ddi_get_parent(dip); pdip && pdip != ddi_root_node();
	    pdip = ddi_get_parent(pdip)) {
		if (strcmp(ddi_get_name(pdip), DEVI_PSEUDO_NEXNAME) == 0)
			return (1);
	}
	return (0);
}

/*
 * Look up an instance number for a dev_info node, and assign one if it does
 * not have one (the dev_info node has devi_name and devi_addr already set).
 */
u_int
e_ddi_assign_instance(dev_info_t *dip)
{
	char *name;
	in_node_t *ap, *np;
	u_int major;
	struct devnames *dnp;
	u_int ret;

	/*
	 * If this is a pseudo-device, use the instance number
	 * assigned by the pseudo nexus driver. The mutex is
	 * not needed since the instance tree is not used.
	 */
	if (is_pseudo_device(dip)) {
		return (ddi_get_instance(dip));
	}

	/*
	 * Only one thread is allowed to change the state of the instance
	 * number assignments on the system at any given time.
	 */
	mutex_enter(&e_ddi_inst_state.in_serial);
	while (e_ddi_inst_state.in_busy)
		cv_wait(&e_ddi_inst_state.in_serial_cv,
		    &e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 1;
	mutex_exit(&e_ddi_inst_state.in_serial);

	np = in_devwalk(dip, &ap);
	if (np) {
		/*
		 * found a matching instance node, we're done
		 */
		ret = np->in_instance;
		mutex_enter(&e_ddi_inst_state.in_serial);
		e_ddi_inst_state.in_busy = 0;
		cv_broadcast(&e_ddi_inst_state.in_serial_cv);
		mutex_exit(&e_ddi_inst_state.in_serial);
		return (ret);
	}
	name = ddi_get_name(dip);
	major = ddi_name_to_major(name);
	ASSERT(major != (u_int) -1);
	dnp = &devnamesp[major];

	np = in_alloc_node(name, ddi_get_name_addr(dip));
	if (ap == NULL)
		cmn_err(CE_PANIC, "instance initialization");
	in_enlist(ap, np);	/* insert into tree */

#ifdef	INSTANCE_TRANSITION_MODE
	if (in_transition_mode) {
		/*
		 * If a hint is provided by .conf file processing and it does
		 * not conflict with a previous assignment, use it
		 */
		if (DEVI(dip)->devi_instance != -1 &&
		    !in_inuse(DEVI(dip)->devi_instance, name))
			np->in_instance = DEVI(dip)->devi_instance;
		else
			np->in_instance = in_next_instance(dnp);
	} else {
		np->in_instance = in_next_instance(dnp);
	}
#else	/* INSTANCE_TRANSITION_MODE */
	np->in_instance = in_next_instance(dnp);
#endif	/* INSTANCE_TRANSITION_MODE */
	np->in_state = IN_PROVISIONAL;

	in_hashin(np);

	ASSERT(np == in_devwalk(dip, &ap));

	ret = np->in_instance;

	mutex_enter(&e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 0;
	cv_broadcast(&e_ddi_inst_state.in_serial_cv);
	mutex_exit(&e_ddi_inst_state.in_serial);
	return (ret);
}

/*
 * This depends on the list being sorted in ascending instance number
 * sequence.  dn_instance contains the next available instance no.
 * or IN_SEARCHME, indicating (a) hole(s) in the sequence.
 */
static u_int
in_next_instance(struct devnames *dnp)
{
	in_node_t *np;
	u_int prev;

	ASSERT(e_ddi_inst_state.in_busy);
	if (dnp->dn_instance != IN_SEARCHME)
		return (dnp->dn_instance++);
	np = dnp->dn_inlist;
	if (np == NULL) {
		dnp->dn_instance = 1;
		return (0);
	}
	if (np->in_next == NULL) {
		if (np->in_instance != 0)
			return (0);
		else {
			dnp->dn_instance = 2;
			return (1);
		}
	}
	prev = np->in_instance;
	if (prev != 0)	/* hole at beginning of list */
		return (0);
	/* search the list for a hole in the sequence */
	for (np = np->in_next; np; np = np->in_next) {
		if (np->in_instance != prev + 1)
			return (prev + 1);
		else
			prev++;
	}
	/*
	 * If we got here, then the hole has been patched
	 */
	dnp->dn_instance = ++prev + 1;
	return (prev);
}

/*
 * This call causes us to *forget* the instance number we've generated
 * for a given device if it was not permanent.
 */
void
e_ddi_free_instance(dev_info_t *dip)
{
	char *name;
	in_node_t *np;
	in_node_t *ap;
	u_int major;
	struct devnames *dnp;

	name = ddi_get_name(dip);
	major = ddi_name_to_major(name);
	ASSERT(major != (u_int) -1);
	dnp = &devnamesp[major];
	/*
	 * Only one thread is allowed to change the state of the instance
	 * number assignments on the system at any given time.
	 */
	mutex_enter(&e_ddi_inst_state.in_serial);
	while (e_ddi_inst_state.in_busy)
		cv_wait(&e_ddi_inst_state.in_serial_cv,
		    &e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 1;
	mutex_exit(&e_ddi_inst_state.in_serial);
	np = in_devwalk(dip, &ap);
	ASSERT(np);
	if (np->in_state == IN_PROVISIONAL) {
		in_removenode(dnp, np, ap);
	}
	mutex_enter(&e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 0;
	cv_broadcast(&e_ddi_inst_state.in_serial_cv);
	mutex_exit(&e_ddi_inst_state.in_serial);
}

/*
 * This makes our memory of an instance assignment permanent
 */
void
e_ddi_keep_instance(dev_info_t *dip)
{
	in_node_t *np;
	in_node_t *ap;

	/*
	 * Nothing to do for pseudo devices.
	 */
	if (is_pseudo_device(dip))
		return;

	/*
	 * Only one thread is allowed to change the state of the instance
	 * number assignments on the system at any given time.
	 */
	mutex_enter(&e_ddi_inst_state.in_serial);
	while (e_ddi_inst_state.in_busy)
		cv_wait(&e_ddi_inst_state.in_serial_cv,
		    &e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 1;
	mutex_exit(&e_ddi_inst_state.in_serial);
	np = in_devwalk(dip, &ap);
	ASSERT(np);
	if (np->in_state == IN_PROVISIONAL)
		np->in_state = IN_PERMANENT;
	mutex_enter(&e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 0;
	cv_broadcast(&e_ddi_inst_state.in_serial_cv);
	mutex_exit(&e_ddi_inst_state.in_serial);
}

/*
 * The devnames struct for this driver is about to vanish.
 * Put the instance tracking nodes on the orphan list
 */
void
e_ddi_orphan_instance_nos(in_node_t *np)
{
	in_node_t *nnp;

	/*
	 * Only one thread is allowed to change the state of the instance
	 * number assignments on the system at any given time.
	 */
	mutex_enter(&e_ddi_inst_state.in_serial);
	while (e_ddi_inst_state.in_busy)
		cv_wait(&e_ddi_inst_state.in_serial_cv,
		    &e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 1;
	mutex_exit(&e_ddi_inst_state.in_serial);
	while (np) {
		nnp = np->in_next;
		np->in_next = e_ddi_inst_state.in_no_major;
		e_ddi_inst_state.in_no_major = np;
		np = nnp;
	}
	mutex_enter(&e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 0;
	cv_broadcast(&e_ddi_inst_state.in_serial_cv);
	mutex_exit(&e_ddi_inst_state.in_serial);
}

/*
 * A new major has been added to the system.  Run through the orphan list
 * and try to attach each one to a driver's list.
 */
void
e_ddi_unorphan_instance_nos()
{
	in_node_t *np, *nnp;

	/*
	 * disconnect the orphan list, and call in_hashin for each item
	 * on it
	 */

	/*
	 * Only one thread is allowed to change the state of the instance
	 * number assignments on the system at any given time.
	 */
	mutex_enter(&e_ddi_inst_state.in_serial);
	while (e_ddi_inst_state.in_busy)
		cv_wait(&e_ddi_inst_state.in_serial_cv,
		    &e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 1;
	mutex_exit(&e_ddi_inst_state.in_serial);
	if (e_ddi_inst_state.in_no_major == NULL) {
		mutex_enter(&e_ddi_inst_state.in_serial);
		e_ddi_inst_state.in_busy = 0;
		cv_broadcast(&e_ddi_inst_state.in_serial_cv);
		mutex_exit(&e_ddi_inst_state.in_serial);
		return;
	}
	/*
	 * Make two passes through the data, skipping over those without
	 * instance no assignments the first time, then making the
	 * assignments the second time.  List should be shorter second
	 * time.  Note that if there is not a valid major number for the
	 * node, in_hashin will put it back on the no_major list without
	 * assigning an instance number.
	 */
	np = e_ddi_inst_state.in_no_major;
	e_ddi_inst_state.in_no_major = NULL;
	while (np) {
		nnp = np->in_next;
		if (np->in_state == IN_UNKNOWN) {
			np->in_next = e_ddi_inst_state.in_no_instance;
			e_ddi_inst_state.in_no_instance = np;
		} else {
			np->in_next = NULL;
			in_hashin(np);
		}
		np = nnp;
	}
	np = e_ddi_inst_state.in_no_instance;
	e_ddi_inst_state.in_no_instance = NULL;
	while (np) {
		nnp = np->in_next;
		np->in_next = NULL;
		in_hashin(np);
		np = nnp;
	}
	mutex_enter(&e_ddi_inst_state.in_serial);
	e_ddi_inst_state.in_busy = 0;
	cv_broadcast(&e_ddi_inst_state.in_serial_cv);
	mutex_exit(&e_ddi_inst_state.in_serial);
}

static void
in_removenode(struct devnames *dnp, in_node_t *mp, in_node_t *ap)
{
	in_node_t *prevp, *np;

	ASSERT(e_ddi_inst_state.in_busy);
	/*
	 * Assertion: parents are always instantiated by the framework
	 * before their children, destroyed after them
	 */
	ASSERT(mp->in_child == NULL);
	/*
	 * Take the node out of the tree
	 */
	if (ap->in_child == mp)
		ap->in_child = mp->in_sibling;
	else {
		for (np = ap->in_child; np; np = np->in_sibling) {
			if (np->in_sibling == mp) {
				np->in_sibling = mp->in_sibling;
				break;
			}
		}
	}
	/*
	 * Take the node out of the instance list
	 */
	if (dnp->dn_inlist == mp) {	/* head of list */
		dnp->dn_inlist = mp->in_next;
		dnp->dn_instance = IN_SEARCHME;
		in_dealloc_node(mp);
		return;
	}
	prevp = dnp->dn_inlist;
	for (np = prevp->in_next; np; np = np->in_next) {
		if (np == mp) {		/* found it */
			dnp->dn_instance = IN_SEARCHME;
			prevp->in_next = mp->in_next;
			in_dealloc_node(mp);
			return;
		}
		prevp = np;
	}
	cmn_err(CE_PANIC, "in_removenode dnp %x, mp %x", (int)dnp, (int)mp);
}

/*
 * Recursive ascent
 */
static in_node_t *
in_devwalk(dev_info_t *dip, in_node_t **ap)
{
	in_node_t *np;
	char *name;
	char *addr;

	ASSERT(dip);
	ASSERT(e_ddi_inst_state.in_busy);
	if (dip == ddi_root_node()) {
		*ap = NULL;
		return (e_ddi_inst_state.in_root);
	}
	/*
	 * call up to find parent, then look through the list of kids
	 * for a match
	 */
	np = in_devwalk(ddi_get_parent(dip), ap);
	if (np == NULL)
		return (np);
	*ap = np;
	np = np->in_child;
	name = ddi_get_name(dip);
	addr = ddi_get_name_addr(dip);

	while (np) {
		if (in_eqstr(np->in_name, name) &&
		    in_eqstr(np->in_addr, addr)) {
			return (np);
		}
		np = np->in_sibling;
	}
	return (np);
}

/*
 * Create a node specified by cp and assign it the given instance no.
 */
static void
in_pathin(char *cp, u_int instance)
{
	in_node_t *np;

	ASSERT(e_ddi_inst_state.in_busy);

#define	IGNORE_STORED_PSEUDO_INSTANCES
#ifdef IGNORE_STORED_PSEUDO_INSTANCES
	/*
	 * We should prevent pseudo devices from being placed in the
	 * instance tree by omitting all names beginning with /pseudo/.
	 * If they aren't removed, a new kernel with an old path_to_inst
	 * file will contain unnecessary entries in the instance tree,
	 * wasting memory, and they will be written out when the
	 * system is reconfigured (thus never going away). Upgrading the
	 * system might cause this to happen. Functionally, this isn't
	 * a problem, since the instance tree is never examined for
	 * pseudo devices.
	 *
	 * This uses ANSI C string concatenation to add the slashes
	 * to the nexus name, so DEVI_PSEUDO_NEXNAME better always be
	 * a macro for a constant string until this is no longer needed.
	 */
	if (strncmp(cp, "/" DEVI_PSEUDO_NEXNAME "/",
	    strlen(DEVI_PSEUDO_NEXNAME) + 2) == 0) {
		return;
	}
#endif

	np = in_make_path(cp);
	if (in_inuse(instance, np->in_name)) {
		cmn_err(CE_WARN,
		    "instance %d already in use, cannot be assigned to '%s'",
		    instance, cp);
		return;
	}
	if (np->in_state == IN_PERMANENT) {
		cmn_err(CE_WARN,
		    "multiple instance number assignments for '%s', %d used",
		    cp, np->in_instance);
	} else {
		np->in_instance = instance;
		np->in_state = IN_PERMANENT;
	}
}

/*
 * Create (or find) the node named by path by recursively decending from the
 * root's first child (we ignore the root, which is never named)
 */
static in_node_t *
in_make_path(char *path)
{
	in_node_t *ap;		/* ancestor pointer */
	in_node_t *np;		/* working node pointer */
	in_node_t *rp;		/* return node pointer */
	char buf[MAXPATHLEN];	/* copy of string so we can change it */
	char *cp, *name, *addr;

	ASSERT(e_ddi_inst_state.in_busy);
	if (path == NULL || path[0] != '/')
		return (NULL);
	strcpy(buf, path);
	cp = buf + 1;	/* skip over initial '/' in path */
	name = in_name_addr(&cp, &addr);
	ap = e_ddi_inst_state.in_root;
	rp = np = e_ddi_inst_state.in_root->in_child;
	while (name) {
		while (name && np) {
			if (in_eqstr(name, np->in_name) &&
			    in_eqstr(addr, np->in_addr)) {
				name = in_name_addr(&cp, &addr);
				if (name == NULL)
					return (np);
				ap = np;
				np = np->in_child;
				continue;
			} else {
				np = np->in_sibling;
			}
		}
		np = in_alloc_node(name, addr);
		in_enlist(ap, np);	/* insert into tree */
		rp = np;	/* value to return if we quit */
		ap = np;	/* new parent */
		np = NULL;	/* can have no children */
		name = in_name_addr(&cp, &addr);
	}
	return (rp);
}

/*
 * Insert node np into the tree as one of ap's children.
 */
static void
in_enlist(in_node_t *ap, in_node_t *np)
{
	in_node_t *mp;
	ASSERT(e_ddi_inst_state.in_busy);
	/*
	 * Make this node some other node's child or child's sibling
	 */
	ASSERT(ap && np);
	if (ap->in_child == NULL) {
		ap->in_child = np;
	} else {
		for (mp = ap->in_child; mp; mp = mp->in_sibling)
			if (mp->in_sibling == NULL) {
				mp->in_sibling = np;
				break;
			}
	}
}

/*
 * Parse the next name out of the path, null terminate it and update cp.
 * caller has copied string so we can mess with it.
 * Upon return *cpp points to the next section to be parsed, *addrp points
 * to the current address substring (or NULL if none) and we return the
 * current name substring (or NULL if none).  name and address substrings
 * are null terminated in place.
 */

static char *
in_name_addr(char **cpp, char **addrp)
{
	char *namep;	/* return value holder */
	char *ap;	/* pointer to '@' in string */
	char *sp;	/* pointer to '/' in string */

	if (*cpp == NULL || **cpp == '\0') {
		*addrp = NULL;
		return (NULL);
	}
	namep = *cpp;
	sp = strchr(*cpp, '/');
	if (sp != NULL) {	/* more to follow */
		*sp = '\0';
		*cpp = sp + 1;
	} else {		/* this is last component. */
		*cpp = NULL;
	}
	ap = strchr(namep, '@');
	if (ap == NULL) {
		*addrp = NULL;
	} else {
		*ap = '\0';		/* terminate the name */
		*addrp = ap + 1;
	}
	return (namep);
}

/*
 * Put this node on the "by instance number" list
 * sorted in increasing instance number order.
 * This assumes that it will be called with nodes of type IN_UNKNOWN
 * only after all the other type nodes for a given major have been processed.
 * We do not assign instance numbers to nodes without a major number because
 * we depend on major numbers to handle driver aliases.
 */
void
in_hashin(in_node_t *np)
{
	struct devnames *dnp;
	in_node_t *mp, *pp;
	int major;

	ASSERT(e_ddi_inst_state.in_busy);
	major = ddi_name_to_major(np->in_name);
	if (major == -1) {
		np->in_next = e_ddi_inst_state.in_no_major;
		e_ddi_inst_state.in_no_major = np;
		return;
	}
	dnp = &devnamesp[major];

	if (np->in_state == IN_UNKNOWN) {
		np->in_instance = in_next_instance(dnp);
		np->in_state = IN_PERMANENT;
	}

	dnp->dn_instance = IN_SEARCHME;
	pp = mp = dnp->dn_inlist;
	if (mp == NULL || np->in_instance < mp->in_instance) {
		np->in_next = mp;
		dnp->dn_inlist = np;
	} else {
		ASSERT(mp->in_instance != np->in_instance);
		while (mp->in_instance < np->in_instance && mp->in_next) {
			pp = mp;
			mp = mp->in_next;
			ASSERT(mp->in_instance != np->in_instance);
		}
		if (mp->in_instance < np->in_instance) { /* end of list */
			np->in_next = NULL;
			mp->in_next = np;
		} else {
			np->in_next = pp->in_next;
			pp->in_next = np;
		}
	}
}

/*
 * Allocate a node and storage for name and addr strings, and fill them in.
 */
static in_node_t *
in_alloc_node(char *name, char *addr)
{
	in_node_t *np;
	char *cp;
	u_int namelen;

	ASSERT(e_ddi_inst_state.in_busy);
	/*
	 * Has name or will become root
	 */
	ASSERT(name || e_ddi_inst_state.in_root == NULL);
	if (addr == NULL)
		addr = "";
	if (name == NULL)
		namelen = 0;
	else
		namelen = strlen(name) + 1;
	cp = kmem_zalloc(sizeof (in_node_t) + namelen + strlen(addr) + 1,
	    KM_SLEEP);
	np = (in_node_t *)cp;
	if (name) {
		np->in_name = cp + sizeof (in_node_t);
		strcpy(np->in_name, name);
	}
	np->in_addr = cp + sizeof (in_node_t) + namelen;
	strcpy(np->in_addr, addr);
	np->in_state = IN_UNKNOWN;
	np->in_instance = -1;
	return (np);
}

static void
in_dealloc_node(in_node_t *np)
{
	/*
	 * The root node can never be de-allocated
	 */
	ASSERT(np->in_name && np->in_addr);
	ASSERT(e_ddi_inst_state.in_busy);
	kmem_free(np, sizeof (in_node_t) + strlen(np->in_name)
	    + strlen(np->in_addr) + 2);
}

/*
 * Handle the various possible versions of "no address"
 */
static int
in_eqstr(char *a, char *b)
{
	if (a == b)	/* covers case where both are nulls */
		return (1);
	if (a == NULL && *b == 0)
		return (1);
	if (b == NULL && *a == 0)
		return (1);
	if (a == NULL || b == NULL)
		return (0);
	return (strcmp(a, b) == 0);
}

/*
 * Returns true if instance no. is already in use by named driver
 */
static int
in_inuse(u_int instance, char *name)
{
	int major;
	in_node_t *np;
	struct devnames *dnp;

	ASSERT(e_ddi_inst_state.in_busy);
	major = ddi_name_to_major(name);
	/*
	 * For now, if we've never heard of this device we assume it is not
	 * in use, since we can't tell
	 * XXX could to the weaker search through the nomajor list checking
	 * XXX for the same name
	 */
	if (major == -1)
		return (0);
	dnp = &devnamesp[major];

	np = dnp->dn_inlist;
	while (np) {
		if (np->in_instance == instance)
			return (1);
		np = np->in_next;
	}
	return (0);
}
