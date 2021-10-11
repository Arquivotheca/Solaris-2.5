
#ident	"@(#)pdevinfo.c	1.19	93/04/15 SMI"

/*
 * Copyright (c) 1990 Sun Microsystems, Inc.
 */

/*
 * For machines that support the openprom, fetch and print the list
 * of devices that the kernel has fetched from the prom or conjured up.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <varargs.h>
#include <errno.h>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/openpromio.h>
#include "pdevinfo.h"
#include "display.h"

/*
 * function declarations
 */
static void 		dump_node(Prom_node *);
static void 		promclose();
static Prom_node 	*walk(Sys_tree *, Prom_node *, int, int);
static void 		add_node(Sys_tree *, Prom_node *);
char 			*get_node_name(Prom_node *);
static int		get_board_num(Prom_node *);
static Board_node 	*find_board(Sys_tree *root, int board);
static Board_node 	*insert_board(Sys_tree *root, int board);
static int 		has_board_num(Prom_node *);
static int child(), next(), promopen();
static void getpropval(struct openpromio *);

static int _error(char *, ...);

static int prom_fd;
extern char *progname;
extern char *promdev;
extern void getppdata();
extern void printppdata();

static char *badarchmsg =
	"System architecture does not support this option of this command.\n";

/*
 * Define DPRINT for run-time debugging printf's...
 * #define DPRINT	1
 */

/* #define DPRINT	1 */
#ifdef	DPRINT
static	char    vdebug_flag = 1;
#define	dprintf	if (vdebug_flag) printf
static void dprint_dev_info(caddr_t, dev_info_t *);
#endif	DPRINT

extern int _doprnt(char *, va_list, FILE   *);

/*VARARGS1*/
int
_error(char *fmt, ...)
{
	int saved_errno;
	va_list ap;
	extern int errno;
	saved_errno = errno;

	if (progname)
		(void) fprintf(stderr, "%s: ", progname);

	va_start(ap);

	(void) _doprnt(fmt, ap, stderr);

	va_end(ap);

	(void) fprintf(stderr, ": ");
	errno = saved_errno;
	perror("");

	return (1);
}

static int
is_openprom()
{
	Oppbuf	oppbuf;
	register struct openpromio *opp = &(oppbuf.opp);
	register unsigned int i;

	opp->oprom_size = MAXVALSIZE;
	if (ioctl(prom_fd, OPROMGETCONS, opp) < 0)
		exit(_error("OPROMGETCONS"));

	i = (unsigned int)((unsigned char)opp->oprom_array[0]);
	return ((i & OPROMCONS_OPENPROM) == OPROMCONS_OPENPROM);
}

int
do_prominfo(int syserrlog)
{
	Sys_tree sys_tree;		/* system information */
	Prom_node *root_node;

	/* set the the system tree fields */
	sys_tree.sys_mem = NULL;
	sys_tree.boards = NULL;
	sys_tree.bd_list = NULL;
	sys_tree.board_cnt = 0;

	if (promopen(O_RDONLY))  {
		exit(_error("openeepr device open failed"));
	}

	if (is_openprom() == 0)  {
		(void) fprintf(stderr, badarchmsg);
		return (1);
	}

	if (next(0) == 0)
		return (1);

	root_node = walk(&sys_tree, NULL, next(0), 0);
	promclose();

	return (display(&sys_tree, root_node, syserrlog));
}

/*
 * Walk the PROM device tree and build the system tree and root tree.
 * Nodes that have a board number property are placed in the board
 * structures for easier processing later. Child nodes are placed
 * under their parents. 'bif' nodes are placed under board structs
 * even if they do not contain board# properties, because of a
 * bug in early sun4d PROMs.
 */
static Prom_node *
walk(Sys_tree *tree, Prom_node *root, int id, int level)
{
	register int curnode;
	Prom_node *pnode;
	int board_node = 0;

	/* allocate a node for this level */
	if ((pnode = (Prom_node *) malloc(sizeof  (struct prom_node))) == NULL)
	{
		perror("malloc");
		exit(1);
	}
	/* assign parent Prom_node */
	pnode->parent = root;

	/* read and print properties for this node */
	dump_node(pnode);

	if (has_board_num(pnode)) {
		add_node(tree, pnode);
		board_node = 1;
	} else if (!strcmp(get_node_name(pnode), "memory")) {
		tree->sys_mem = pnode;
		board_node = 1;
	} else if (!strcmp(get_node_name(pnode), "bif")) {
		add_node(tree, pnode);
		board_node = 1;
	}

	if (curnode = child(id))
		pnode->child = walk(tree, pnode, curnode, level+1);
	else
		pnode->child = NULL;

	if (curnode = next(id)) {
		if (board_node)
		{
			return (walk(tree, root, curnode, level));
		} else {
			pnode->sibling = walk(tree, root, curnode, level);
		}
	} else if (!board_node)  {
		pnode->sibling = NULL;
	}

	if (board_node)
		return (NULL);
	else
		return (pnode);
}

/*
 * Read all properties and values from nodes.
 * Copy the properties read into the prom_node passsed in.
 */
static void
dump_node(Prom_node *node)
{
	Oppbuf oppbuf;
	register struct openpromio *opp = &oppbuf.opp;
	Prop *prop = NULL;	/* tail of properties list */

	/* clear out pointers in pnode */
	node->props = NULL;

	/* get first prop by asking for null string */
	(void) memset((void *) oppbuf.buf, 0, BUFSIZE);

	opp->oprom_size = MAXPROPSIZE;
	while (opp->oprom_size != 0) {
		Prop *temp;	/* newly allocated property */

		/* allocate space for the property */
		if ((temp = (Prop *) malloc (sizeof (Prop))) == NULL) {
		    perror("malloc");
		    exit(1);
		}

		/*
		 * get property
		 */
		opp->oprom_size = MAXPROPSIZE;

		if (ioctl(prom_fd, OPROMNXTPROP, opp) < 0)
			exit(_error("OPROMNXTPROP"));

		if (opp->oprom_size == 0) {
			free(temp);
		} else {
			temp->name.opp.oprom_size = opp->oprom_size;
			(void) strcpy(temp->name.opp.oprom_array,
				opp->oprom_array);

			(void) strcpy(temp->value.opp.oprom_array,
				temp->name.opp.oprom_array);
			getpropval(&temp->value.opp);

			/* everything worked so link the property list */
			if (node->props == NULL)
				node->props = temp;
			else if (prop != NULL)
				prop->next = temp;
			prop = temp;
			prop->next = NULL;
		}
	}
}

static int
promopen(oflag)
register int oflag;
{
	/*CONSTCOND*/
	while (1)  {
		if ((prom_fd = open(promdev, oflag)) < 0)  {
			if (errno == EAGAIN)   {
				(void) sleep(5);
				continue;
			}
			if (errno == ENXIO)
				return (-1);
			exit(_error("cannot open %s", promdev));
		} else
			return (0);
	}
	/*NOTREACHED*/
}

static void
promclose()
{
	if (close(prom_fd) < 0)
		exit(_error("close error on %s", promdev));
}

/*
 * Read the value of the property from the PROM deivde tree
 */
static void
getpropval(struct openpromio *opp)
{
	opp->oprom_size = MAXVALSIZE;

	if (ioctl(prom_fd, OPROMGETPROP, opp) < 0)
		exit(_error("OPROMGETPROP"));
}

static int
next(int id)
{
	Oppbuf	oppbuf;
	register struct openpromio *opp = &(oppbuf.opp);
	/* LINTED */
	int *ip = (int *)(opp->oprom_array);

	(void) memset((void *) oppbuf.buf, 0, BUFSIZE);

	opp->oprom_size = MAXVALSIZE;
	*ip = id;
	if (ioctl(prom_fd, OPROMNEXT, opp) < 0)
		return (_error("OPROMNEXT"));
	/* LINTED */
	return (*(int *)opp->oprom_array);
}

static int
child(int id)
{
	Oppbuf	oppbuf;
	register struct openpromio *opp = &(oppbuf.opp);
	/* LINTED */
	int *ip = (int *)(opp->oprom_array);

	(void) memset((void *) oppbuf.buf, 0, BUFSIZE);
	opp->oprom_size = MAXVALSIZE;
	*ip = id;
	if (ioctl(prom_fd, OPROMCHILD, opp) < 0)
		return (_error("OPROMCHILD"));
	/* LINTED */
	return (*(int *)opp->oprom_array);
}

/*
 * Check if the Prom node passed in contains a property called
 * "board#". All children of the XDBus and all nodes which
 * have board spoecific information will have a board# property
 */
static int
has_board_num(Prom_node *node)
{
	Prop *prop = node->props;

    	/*
	 * walk thru all properties in this PROM node and look for
	 * board# prop
	 */
	while (prop != NULL) {
		if (!strcmp(prop->name.opp.oprom_array, "board#"))
		    return (1);

		prop = prop->next;
	}

	return (0);
}	/* end of has_board_num() */

/*
 * Retrieve the value of the board number property from this Prom
 * node. It has the type of int.
 */
static int
get_board_num(Prom_node *node)
{
	Prop *prop = node->props;

    	/*
	 * walk thru all properties in this PROM node and look for
	 * board# prop
	 */
	while (prop != NULL) {
		if (!strcmp(prop->name.opp.oprom_array, "board#"))
			/* LINTED */
			return (*((int *)prop->value.opp.oprom_array));

		prop = prop->next;
	}

	return (-1);
}	/* end of get_board_num() */

/*
 * This function adds a board node to the board structure where that
 * that node's physical component lives.
 */
static void
add_node(Sys_tree *root, Prom_node *pnode)
{
	int board;
	Board_node *bnode;

	/* add this node to the Board list of the appropriate board */
	if ((board = get_board_num(pnode)) == -1) {
		void *value;

		if ((value = get_prop_val(find_prop(pnode, "reg"))) == NULL) {
			(void) printf("add_node() passed non-board# node\n");
			exit (1);
		}
		board = *(int *)value;
	}

	/* find the node with the same board number */
	if ((bnode = find_board(root, board)) == NULL)
	    bnode = insert_board(root, board);

	/* now attach this prom node to the board list */
	if (bnode->nodes != NULL)
	    pnode->sibling = bnode->nodes;
	bnode->nodes = pnode;
}	/* end of add_node() */

/*
 * Find the requested board struct in the system device tree.
 */
static Board_node *
find_board(Sys_tree *root, int board)
{
	Board_node *bnode = root->bd_list;

	while ((bnode != NULL) && (board != bnode->board_num))
		bnode = bnode->next;

	return (bnode);
}	/* end of find_board() */

/*
 * Add a board to the system list in order. Initialize all pointer
 * fields to NULL.
 */
static Board_node *
insert_board(Sys_tree *root, int board)
{
	Board_node *bnode;
	Board_node *temp = root->bd_list;

	if ((bnode = (Board_node *) malloc(sizeof (Board_node))) == NULL) {
		perror("malloc");
		exit (1);
	}
	bnode->nodes = NULL;
	bnode->next = NULL;
	bnode->board_num = board;

	if (temp == NULL)
		root->bd_list = bnode;
	else if (temp->board_num > board) {
		bnode->next = temp;
		root->bd_list = bnode;
	} else {
		while ((temp->next != NULL) && (board > temp->next->board_num))
			temp = temp->next;
		bnode->next = temp->next;
		temp->next = bnode;
	}
	root->board_cnt++;

	return (bnode);
}	/* end of insert_board() */

/*
 * This function searches through the properties of the node passed in
 * and returns a pointer to the value of the name property.
 */
char *
get_node_name(Prom_node *pnode)
{
	Prop *prop = pnode->props;

	while (prop != NULL) {
		if (!strcmp("name", prop->name.opp.oprom_array))
			return (prop->value.opp.oprom_array);
		prop = prop->next;
	}
	return (NULL);
}	/* end of get_node_name() */

/*
 * Do a depth-first walk of a device tree and
 * return the first node with the name matching.
 */

Prom_node *
dev_find_node(Prom_node *root, char *name)
{
	char *node_name;
	Prom_node *node;

	if (root == NULL)
		return (NULL);

	/* search the local node */
	if ((node_name = get_node_name(root)) != NULL)
	{
		if (!strcmp(node_name, name)) {
			return (root);
		}
	}

	/* look at your children first */
	if ((node = dev_find_node(root->child, name)) != NULL)
		return (node);

	/* now look at your siblings */
	if ((node = dev_find_node(root->sibling, name)) != NULL)
		return (node);

	return (NULL);	/* not found */
}	/* end of dev_find_node() */

/*
 * Start from the current node and return the next node besides
 * the current one which has the requested name property.
 */
Prom_node *
dev_next_node(Prom_node *root, char *name)
{
	if (root == NULL)
		return (NULL);

	/* look at your children first */
	if (dev_find_node(root->child, name) != NULL)
		return (root->child);

	/* now look at your siblings */
	if (dev_find_node(root->sibling, name) != NULL)
		return (root->sibling);

	return (NULL);  /* not found */
}	/* end of dev_next_node() */

/*
 * Search a device tree and return the first failed node that is found.
 * (has a 'status' property)
 */
Prom_node *
find_failed_node(Prom_node * root)
{
	Prom_node *pnode;
	void *value;

	if (root == NULL)
		return (NULL);

	/* search the local node */
	if ((value = get_prop_val(find_prop(root, "status"))) != NULL) {

		if ((value != NULL) && strstr((char *)value, "fail"))
			return (root);
	}

	/* search the child */
	if ((pnode = find_failed_node(root->child)) != NULL)
		return (pnode);

	/* search the siblings */
	if ((pnode = find_failed_node(root->sibling)) != NULL)
		return (pnode);

	return (NULL);
}	/* end of find_failed_node() */

/*
 * Start from the current node and return the next node besides
 * the current one which is failed. (has a 'status' property)
 */
Prom_node *
next_failed_node(Prom_node * root)
{
	Prom_node *pnode;

	if (root == NULL)
		return (NULL);

	/* search the child */
	if ((pnode = find_failed_node(root->child)) != NULL) {
		return (pnode);
	}

	/* search the siblings */
	if ((pnode = find_failed_node(root->sibling)) != NULL) {
		return (pnode);
	}

	return (NULL);
}	/* end of find_failed_node() */

/*
 * Get a property's value. Must be void * since the property can
 * be any data type. Caller must know the *PROPER* way to use this
 * data.
 */
void *
get_prop_val(Prop *prop)
{
	if (prop == NULL)
		return (NULL);

	return ((void *)(&prop->value.opp.oprom_array));
}	/* end of get_prop_val() */

/*
 * Search a Prom node and retrieve the property with the correct
 * name.
 */
Prop *
find_prop(Prom_node *pnode, char *name)
{
	Prop *prop;

	if (pnode  == NULL)
	{
		return (NULL);
	}

	if (pnode->props == NULL)
	{
		(void) printf("Prom node has no properties\n");
		return (NULL);
	}

	prop = pnode->props;
	while ((prop != NULL) && (strcmp(prop->name.opp.oprom_array, name)))
		prop = prop->next;

	return (prop);
}	/* end of find_prop() */
