#ident	"@(#)devfswalk.c	1.10	94/07/29 SMI"
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <nlist.h>
#include <fcntl.h>
#include <kvm.h>
#include <sys/devops.h>
#include <sys/sunddi.h>
#include <sys/ddi_impldefs.h>
#include <dlfcn.h>

#include "libdevinfo.h"

const char *kvmname = "libkvm.so.1";

/*
 * Some module-global data to avoid passing parameters around through
 * many rouines.
 */
static dev_info_t *top_devinfo;	/* Top devinfo node */
static int km_fd = -1;		/* fd of /dev/kmem */

static int hitcnt = 0;
static int misscnt = 0;

#define	CACHESIZE 10
/*
 * local_addr -- mmap kmem address into local address space
 *
 * We don't know ahead of time how big a chunk we need to mmap since we are
 * typically passed a pointer to a string say, and we don't know the len
 * of the string.  So we have to mmap 2 pages so that if the string crosses
 * a page boundary we can access the whole string.  Note that currently we
 * don't unmap anything and in some cases we may map things more than once.
 * There is much less bookkeeping we have to do that way.  Since this is a
 * very transient application that won't get run very often I think we can
 * optimize for simplicity rather than completeness.
 *
 * Depends on /dev/kmem having been successfully opened on file descriptor
 * km_fd.
 */
const char *
local_addr(caddr_t addr)
{
	caddr_t pa;
	int i;

	static int pagesize, pageoffset, pagemask;

	static struct {
	    caddr_t va;
	    caddr_t pa;
	} cache[CACHESIZE];		/* Initial primitive cache */

	static int cp;

	if (pagesize == 0) {
		pagesize = sysconf(_SC_PAGESIZE);
		pageoffset = pagesize - 1;
		pagemask = ~(pageoffset);
	}

	if (addr == NULL)
		return (NULL);

	/*
	 * check cache -- just last entry for now
	 */
	for (i = 0; i < CACHESIZE; i++)
	    if (cache[i].va != NULL &&
		(caddr_t)((int)addr & pagemask) == cache[i].va) {
		hitcnt++;
		return (const caddr_t)(cache[i].pa + ((int)addr & pageoffset));
	}

	if ((pa = mmap(0, 2*pagesize, PROT_READ, MAP_SHARED, km_fd,
	    (int)(addr) & pagemask)) == (caddr_t)-1) {
		if ((pa = mmap(0, pagesize, PROT_READ, MAP_SHARED, km_fd,
		    (int)(addr) & pagemask)) == (caddr_t)-1) {
			perror("mmap");
			exit(1);
		}
	}

	cache[cp].va = (caddr_t)((int)addr & pagemask);
	cache[cp].pa = pa;
	cp = (cp + 1) % CACHESIZE;
	misscnt++;

	return (const caddr_t)(pa + ((int)addr & pageoffset));
}

/*
 * This general-purpose routine walks one layer of the dev_info tree,
 * calling the given function for each dev_info it finds, and passing
 * the pointer arg which can point to a structure of information that
 * the function needs.
 * It is useful when searches need to be made.
 * It walks all devices, attached or not.  The function called should
 * check if the device is attached if it cares about such things.
 */
static void
walk_layer(dev_info_t *dev_info,
    void(*f)(const dev_info_t *, const caddr_t),
    caddr_t arg)
{
	dev_info_t *dev;

	/*
	 * Call the function for this dev_info.
	 * Go to the next device.
	 */
	for (dev = dev_info; dev != (dev_info_t *)NULL;
	    dev = (dev_info_t *)
	    local_addr((caddr_t)(DEVI(dev)->devi_sibling))) {
		(*f)(dev, arg);
	}
}

/*
 * This general-purpose routine walks one layer of the dev_info tree,
 * calling the given function for each dev_info it finds, and passing
 * the pointer arg which can point to a structure of information that
 * the function needs.
 * It is useful when searches need to be made.
 * It walks all devices, attached or not.  The function called should
 * check if the device is attached if it cares about such things.
 */
static void
walk_devs(dev_info_t *dev_info, void(*f)(const dev_info_t *, const caddr_t),
    caddr_t arg, void(*cd)(const char *))
{
	dev_info_t *dev;
	char node[256];

	/*
	 * Call the function for all the devices on this layer.
	 * Then, for each device that has a slave, call walk_devs on
	 * the slave.
	 */
	walk_layer(dev_info, f, arg);
	for (dev = dev_info; dev != (dev_info_t *)NULL;
	    dev = (dev_info_t *)
	    local_addr((caddr_t)(DEVI(dev)->devi_sibling))) {
		if (DEVI(dev)->devi_child) {
			if (cd != NULL && dev != top_devinfo) {
				sprintf(node, "%s",
				    local_addr(DEVI(dev)->devi_name));
				if (DEVI(dev)->devi_addr != NULL &&
				    *local_addr(DEVI(dev)->devi_addr) != '\0') {
					strcat(node, "@");
					strcat(node,
					    local_addr(DEVI(dev)->devi_addr));
				}
				(*cd)(node);
			}
			walk_devs((dev_info_t *)
			    local_addr((caddr_t)(DEVI(dev)->devi_child)),
			    f, arg, cd);
			if (cd)
				(*cd)("..");
		}
	}
}

static struct nlist nl[] = {
	{"top_devinfo"},
	{NULL}
};

static const char *devicetype;	/* Device type string */

static
int
modwalk(void (*make_tree)(const dev_info_t *, const caddr_t),
    void (*new_dir)(const char *))
{
	char node[256];
	dev_info_t *dev_info;
	kvm_t *kfd;
	void *dlhandle;
	kvm_t *(*kopen)(char *, char *, char *, int, char *);
	const char *kopen_name = "kvm_open";
	int (*kclose)(kvm_t *);
	const char *kclose_name = "kvm_close";
	int (*kread)(kvm_t *, unsigned long, char *, unsigned);
	const char *kread_name = "kvm_read";
	int (*klist)(kvm_t *, struct nlist *);
	const char *klist_name = "kvm_nlist";

	if((dlhandle = dlopen(kvmname, RTLD_LAZY)) == (void *)0) {
		return (-1);
	}
	if((kopen = (kvm_t *(*)(char *, char *, char *, int, char *))
	    dlsym(dlhandle, kopen_name)) ==
	    (kvm_t *(*)(char *, char *, char *, int, char *))0) {
		fprintf(stderr, "Unable to find sym %s in %s\n",
		    kopen_name, kvmname);
		exit(1);
	}

	if((kclose = (int (*)(kvm_t *)) dlsym(dlhandle, kclose_name)) ==
	    (int (*)(kvm_t *))0) {
		fprintf(stderr, "Unable to find sym %s in %s\n",
		    kclose_name, kvmname);
		exit(1);
	}

	if((kread = (int (*)(kvm_t *, unsigned long, char *, unsigned))
	    dlsym(dlhandle, kread_name)) ==
	    (int (*)(kvm_t *, unsigned long, char *, unsigned))0) {
		fprintf(stderr, "Unable to find sym %s in %s\n",
		    kread_name, kvmname);
		exit(1);
	}
	if((klist = (int (*)(kvm_t *, struct nlist *)) 
	    dlsym(dlhandle, klist_name)) ==
	    (int (*)(kvm_t *, struct nlist *))0) {
		fprintf(stderr, "Unable to find sym %s in %s\n",
		    klist_name, kvmname);
		exit(1);
	}
	if ((kfd = (*kopen)(NULL, NULL, NULL, O_RDONLY, "modwalk")) ==
	    (kvm_t *)0) {
		perror(kopen_name);
		exit(1);
	}
	if ((km_fd = open("/dev/kmem", O_RDONLY)) < 0) {
		perror("open");
		exit(1);
	}
	if ((*klist)(kfd, nl) < 0) {
		perror(klist_name);
		exit(1);
	}
	if (nl[0].n_type == 0) {
		perror("symbol not found");
		exit(1);
	}
	if ((*kread)(kfd, nl[0].n_value, (char *)&top_devinfo,
	    sizeof (top_devinfo)) < 0) {
		perror(kread_name);
		exit(1);
	}
	top_devinfo = (dev_info_t *)local_addr((caddr_t)top_devinfo);

	walk_devs(top_devinfo, make_tree, (caddr_t)devicetype, new_dir);

	close(km_fd);

	km_fd = -1;		/* Just in case */

	(*kclose)(kfd);

	return(0);

}

static char curdir[256];

static void
change_dir(const char *dir)
{
	char *ep;

	if (strcmp(dir, "..") == 0) {
		if ((ep = strrchr(curdir, '/')) != NULL)
			*ep = '\0';
		else
			curdir[0] = '\0';
	} else {
		if (curdir[0] != 0)
			strcat(curdir, "/");
		strcat(curdir, dir);
	}
}

/*
 * Routine to perform node type match.  node_types are parsed strings,
 * with a ':' separating the major from the first minor, and ',' separating
 * further minor values.
 */
static boolean_t
node_type_match(const char *desired,
		const char *node)
{
	if (desired == NULL || node == NULL || *desired == '\0')
		return (B_FALSE);

	while (*desired == *node) {
		if (*desired == '\0')
			return (B_TRUE);
		desired++, node++;
	}

	/*
	* If desired string ends with separator, and node string matches to,
	* and continues after, separator: MATCH
	*/
	if (*desired == '\0') {
		desired--;

		if (*desired == ':' || *desired == ',')
			return (B_TRUE);
	}
	/*
	 * else if desired string ends with separator, and node string ends
	 * just before separator: MATCH
	 */
	else if (*node == '\0') {
		if ((*desired == ':' || *desired == ',') &&
		    *(desired+1) == '\0')
			return (B_TRUE);
	}

	return (B_FALSE);
}

/* Address of func to call when node found */
static void (*foundit)(const char *, const char *,
	const dev_info_t *, struct ddi_minor_data *, struct ddi_minor_data *);

static int check_aliases = 0;

/*
 * nodecheck - check node for device type match
 */
static void
nodecheck(const dev_info_t *dev_info, const caddr_t desired_type)
{
	char node[256];
	char attr_node[256];
	struct ddi_minor_data *dmdp, *dmdap;
	int i;
	const char *node_type;
	int nfd;
	char *p;

	if (dev_info == top_devinfo) {
		return;
	}

	if (devfs_iscbdriver(dev_info)) {
		sprintf(node, "%s%s%s",
			curdir, (*curdir ? "/" : ""),
			local_addr(DEVI(dev_info)->devi_name));
		if (DEVI(dev_info)->devi_addr != NULL &&
		    *local_addr(DEVI(dev_info)->devi_addr) != '\0') {
			strcat(node, "@");
			strcat(node, local_addr(DEVI(dev_info)->devi_addr));
		}
		p = node + strlen(node);

		for (dmdp = ((struct ddi_minor_data *)
		    local_addr((caddr_t)(DEVI(dev_info)->devi_minor)));
		    dmdp != NULL;
		    dmdp = (struct ddi_minor_data *)
		    local_addr((caddr_t)dmdp->next)) {

			if (!(dmdp->type == DDM_MINOR ||
			    (check_aliases && dmdp->type == DDM_ALIAS)))
				continue;
			if (dmdp->type == DDM_MINOR && dmdp->ddm_node_type)
				node_type = (const char *)
				    local_addr(dmdp->ddm_node_type);
			else {
#ifdef TEMP_KLUDGE /* XXX */
				node_type = NULL;
#else
				node_type = DDI_PSEUDO;
#endif
			}

			if ((desired_type == NULL && node_type != NULL) ||
			    node_type_match(desired_type,
			    node_type) == B_TRUE) {
				strcat(node, ":");
				strcat(node, local_addr(dmdp->ddm_name));
				foundit(node, node_type,
					    dev_info, dmdp, NULL);
				*p = '\0';
			}
			/*
			 * Now, in the case of foundit_nobc, check for
			 * aliased ddi_minor_data nodes
			 */
			if (check_aliases && dmdp->type == DDM_ALIAS) {
				dmdap = (struct ddi_minor_data *)
				    local_addr((caddr_t)dmdp->ddm_admp);
				if (dmdap->type != DDM_MINOR)
					continue;
				if (dmdap->ddm_node_type)
					node_type = (const char *)
					    local_addr(dmdap->ddm_node_type);
				else {
#ifdef TEMP_KLUDGE /* XXX */
					node_type = NULL;
#else
					node_type = DDI_PSEUDO;
#endif
				}
				if ((desired_type == NULL &&
				    node_type != NULL) ||
				    node_type_match(desired_type,
				    node_type) == B_TRUE) {
					strcat(node, ":");
					strcat(node,
					    local_addr(dmdap->ddm_name));
					foundit(node, node_type,
					    dev_info, dmdp, dmdap);
					*p = '\0';
				}
			}
		}
	}
}

/*
 * devfs_find - find devfs entry with DEVTYPE property
 *
 * This routine walks the devinfo tree, looking for devices with
 * DEVTYPE property
 * string of 'devtype'.  For each entry found, it constructs the devfs name
 * of the corresponding device node, then calls the 'found' routine passing
 * it the devfs name string.
 *
 * This means a caller can find the required devfs nodes without having to
 * understand anything about devinfo.
 */
int
devfs_find(const char *devtype, void (*found)(const char *, const char *,
    const dev_info_t *dip, struct ddi_minor_data *minor_data, 
    struct ddi_minor_data *alias_data),
    int aliases)
{
	/*
	 * Make 'found' function address known to node routine
	 * without having to pass it down
	 */
	foundit = found;
	check_aliases = aliases;
	/*
	 * Make device type string available to 'node' routine
	 */
	devicetype = devtype;

	/*
	 * init curdir
	 */
	curdir[0] = '\0';

	return(modwalk(nodecheck, change_dir));
}
