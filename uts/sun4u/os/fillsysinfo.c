/*
 * Copyright (c) 1992, by Sun Microsystems, Inc.
 */

#ident	"@(#)fillsysinfo.c	1.34	95/09/26 SMI"

#include <sys/errno.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/cpuvar.h>
#include <sys/clock.h>

#include <sys/promif.h>
#include <sys/bt.h>
#include <sys/systm.h>
#include <sys/machsystm.h>
#include <sys/debug.h>
#include <sys/sysiosbus.h>
#include <sys/sunddi.h>
#include <sys/modctl.h>

/*
 * The OpenBoot Standalone Interface supplies the kernel with
 * implementation dependent parameters through the devinfo/property mechanism
 */
typedef enum { XDRBOOL, XDRINT, XDRSTRING } xdrs;

/*
 * structure describing properties that we are interested in querying the
 * OBP for.
 */
struct getprop_info {
	char   *name;
	xdrs	type;
	u_int  *var;
};

/*
 * structure used to convert between a string returned by the OBP & a type
 * used within the kernel. We prefer to paramaterize rather than type.
 */
struct convert_info {
	char *name;
	u_int var;
	char *realname;
};

/*
 * structure describing nodes that we are interested in querying the OBP for
 * properties.
 */
struct node_info {
	char			*name;
	int			size;
	struct getprop_info	*prop;
	struct getprop_info	*prop_end;
	unsigned int		*value;
};

/*
 * macro definitions for routines that form the OBP interface
 */
#define	NEXT			prom_nextnode
#define	CHILD			prom_childnode
#define	GETPROP			prom_getprop
#define	GETPROPLEN		prom_getproplen


/* 0=quiet; 1=verbose; 2=debug */
int	debug_fillsysinfo = 0;
#define	VPRINTF if (debug_fillsysinfo) prom_printf

#define	CLROUT(a, l)			\
{					\
	register int c = l;		\
	register char *p = (char *)a;	\
	while (c-- > 0)			\
		*p++ = 0;		\
}

#define	CLRBUF(a)	CLROUT(a, sizeof (a))

int ncpunode;
struct cpu_node cpunodes[NCPU];
char cpu_info_buf[NCPU][CPUINFO_SZ];

static void	check_cpus(void);
static void	fill_cpu(dnode_t);
static void	have_counter();


extern u_longlong_t iommu_tsb_physaddr[];
extern int sbus_iommu_tsb_alloc_size;
extern int pci_iommu_tsb_alloc_size;

/*
 * list of well known devices that must be mapped, and the variables that
 * contain their addresses.
 */
extern caddr_t		v_clk_regs_addr;
extern caddr_t		v_clk_clr_regs_addr;
caddr_t			v_auxio_addr;
caddr_t			v_eeprom_addr = (caddr_t)0;
int			has_central = 0;
static int		found_iobus = 0;


/*
 * Some nodes have functions that need to be called when they're seen.
 */
static void	have_sbus();
static void	have_pci();
static void	have_eeprom();
static void	have_auxio();
static void	have_ac();
static void	have_central();
static void	have_clock_board();
static void	have_environment();
static void	have_fhc();
static void	have_simm_status();
static void	have_sram();

static struct wkdevice {
	char *wk_namep;
	void (*wk_func)();
	caddr_t *wk_vaddrp;
	u_short wk_flags;
#define	V_OPTIONAL	0x0000
#define	V_MUSTHAVE	0x0001
#define	V_MAPPED	0x0002
#define	V_MULTI		0x0003	/* optional, may be more than one */
} wkdevice[] = {
	{ "sbus", have_sbus, NULL, V_MULTI },
	{ "pci", have_pci, NULL, V_MULTI },
	{ "eeprom", have_eeprom, NULL, V_MULTI },
	{ "auxio", have_auxio, &v_auxio_addr, V_OPTIONAL },
	{ "ac", have_ac, NULL, V_OPTIONAL },
	{ "central", have_central, NULL, V_OPTIONAL },
	{ "clock-board", have_clock_board, NULL, V_OPTIONAL },
	{ "counter-timer", have_counter, NULL, V_MULTI },
	{ "environment", have_environment, NULL, V_OPTIONAL },
	{ "fhc", have_fhc, NULL, V_OPTIONAL },
	{ "simm-status", have_simm_status, NULL, V_OPTIONAL },
	{ "sram", have_sram, NULL, V_OPTIONAL },
	{ 0, },
};

static void map_wellknown(dnode_t);

void
map_wellknown_devices()
{
	struct wkdevice *wkp;

	map_wellknown(NEXT((dnode_t)0));

	/*
	 * See if it worked
	 */
	for (wkp = wkdevice; wkp->wk_namep; ++wkp) {
		if (wkp->wk_flags == V_MUSTHAVE) {
			cmn_err(CE_PANIC, "map_wellknown_devices: required "
				"device %s not mapped\n", wkp->wk_namep);
		}
	}

	/*
	 * all sun4u systems must have an IO bus, i.e. sbus or pcibus
	 */
	if (found_iobus == 0)
	    cmn_err(CE_PANIC, "map_wellknown_devices: no i/o bus node found");

#ifndef	MPSAS
	/*
	 * all sun4u systems must have an eeprom
	 */
	if (v_eeprom_addr == (caddr_t)0)
	    cmn_err(CE_PANIC, "map_wellknown_devices: no eeprom node found");
#endif

	/*
	 * all sun4u systems must have a counter-timer node
	 */
	if (v_clk_regs_addr == (caddr_t)0)
	    cmn_err(CE_PANIC,
		"map_wellknown_devices: no counter-timer node found");

	check_cpus();
}

/*
 * map_wellknown - map known devices & registers
 */
static void
map_wellknown(dnode_t curnode)
{
	extern int status_okay(int, char *, int);
	char tmp_name[MAXSYSNAME];
	static void fill_address(dnode_t, char *);

#ifdef	VPRINTF
	VPRINTF("map_wellknown(%x)\n", curnode);
#endif	VPRINTF

	for (curnode = CHILD(curnode); curnode; curnode = NEXT(curnode)) {
		/*
		 * prune subtree if status property indicating not okay
		 */
		if (!status_okay((int)curnode, (char *)NULL, 0)) {
			char devtype_buf[OBP_MAXPROPNAME];
			int size;

#ifdef	VPRINTF
			VPRINTF("map_wellknown: !okay status property\n");
#endif	VPRINTF
			/*
			 * a status property indicating bad memory will be
			 * associated with a node which has a "device_type"
			 * property with a value of "memory-controller"
			 */
			if ((size = GETPROPLEN(curnode, OBP_DEVICETYPE))
			    == -1) {
				continue;
			}
			if (size > OBP_MAXPROPNAME) {
				cmn_err(CE_CONT, "node %x '%s' prop too "
					"big\n", curnode, OBP_DEVICETYPE);
				continue;
			}
			if (GETPROP(curnode, OBP_DEVICETYPE, devtype_buf)
			    == -1) {
				cmn_err(CE_CONT, "node %x '%s' get failed\n",
					curnode, OBP_DEVICETYPE);
				continue;
			}
			if (strcmp(devtype_buf, "memory-controller") != 0)
				continue;
			/*
			 * ...else fall thru and process the node...
			 */
		}
		CLRBUF(tmp_name);
		if (GETPROP(curnode, OBP_NAME, (caddr_t) tmp_name) != -1)
			fill_address(curnode, tmp_name);
		if (GETPROP(curnode, OBP_DEVICETYPE, tmp_name) != -1 &&
		    strcmp(tmp_name, "cpu") == 0)
			fill_cpu(curnode);
		map_wellknown(curnode);
	}
}

static void
fill_address(dnode_t curnode, char *namep)
{
	struct wkdevice *wkp;
	int size;

	for (wkp = wkdevice; wkp->wk_namep; ++wkp) {
		if (strcmp(wkp->wk_namep, namep) != 0)
			continue;
		if (wkp->wk_flags == V_MAPPED)
			return;
		if (wkp->wk_vaddrp != NULL) {
			if ((size = GETPROPLEN(curnode, OBP_ADDRESS)) == -1) {
				cmn_err(CE_CONT, "device %s size %d\n",
					namep, size);
				continue;
			}
			if (size > sizeof (caddr_t)) {
				cmn_err(CE_CONT, "device %s address prop too "
					"big\n", namep);
				continue;
			}
			if (GETPROP(curnode, OBP_ADDRESS,
				    (caddr_t) wkp->wk_vaddrp) == -1) {
				cmn_err(CE_CONT, "device %s not mapped\n",
					namep);
				continue;
			}
#ifdef	VPRINTF
			VPRINTF("fill_address: %s mapped to %x\n", namep,
				*wkp->wk_vaddrp);
#endif	/* VPRINTF */
		}
		if (wkp->wk_func != NULL)
			(*wkp->wk_func)(curnode);
		/*
		 * If this one is optional and there may be more than
		 * one, don't set V_MAPPED, which would cause us to skip it
		 * next time around
		 */
		if (wkp->wk_flags != V_MULTI)
			wkp->wk_flags = V_MAPPED;
	}
}

static void
fill_cpu(dnode_t node)
{
	void fiximp_obp();
	struct cpu_node *cpunode;
	u_int cpu_clock_Mhz;
	int upaid;
	char *cpu_info;

	/*
	 * use upa port id as the index to cpunodes[]
	 */
	(void) GETPROP(node, "upa-portid", (caddr_t)&upaid);
	cpunode = &cpunodes[upaid];
	cpunode->upaid = upaid;
	(void) GETPROP(node, "name", cpunode->name);
	(void) GETPROP(node, "implementation#",
		(caddr_t)&cpunode->implementation);
	(void) GETPROP(node, "mask#", (caddr_t)&cpunode->version);
	(void) GETPROP(node, "clock-frequency", (caddr_t)&cpunode->clock_freq);

	/*
	 * If we didn't find it in the CPU node, look in the root node.
	 */
	if (cpunode->clock_freq == 0) {
		dnode_t root = prom_nextnode((dnode_t) 0);
		(void) GETPROP(root, "clock-frequency",
		    (caddr_t)&cpunode->clock_freq);
	}

	cpu_clock_Mhz = (cpunode->clock_freq + 500000) / 1000000;

	cpunode->nodeid = node;

	cpu_info = &cpu_info_buf[upaid][0];
	sprintf(cpu_info,
	    "cpu%d: %s (upaid %d impl 0x%x ver 0x%x clock %d MHz)\n",
	    ncpunode, cpunode->name, cpunode->upaid, cpunode->implementation,
	    cpunode->version, cpu_clock_Mhz);
	cmn_err(CE_CONT,
	    "?cpu%d: %s (upaid %d impl 0x%x ver 0x%x clock %d MHz)\n",
	    ncpunode, cpunode->name, cpunode->upaid, cpunode->implementation,
	    cpunode->version, cpu_clock_Mhz);
	if (ncpunode == 0)
		(void) fiximp_obp(node);
	ncpunode++;
}

static void
check_cpus()
{
	int i;
	int impl, cpuid = getprocessorid();
	char *msg = NULL;
	extern use_mp;

	ASSERT(cpunodes[cpuid].nodeid != 0);

	/*
	 * We check here for illegal cpu combinations.
	 * Currently, we check that the implementations are the same.
	 */
	impl = cpunodes[cpuid].implementation;
	for (i = 0; i < NCPU; i++) {
		if (cpunodes[i].nodeid == 0)
			continue;
		if (cpunodes[i].implementation != impl) {
			msg = " on mismatched modules";
			break;
		}
	}
	if (msg != NULL) {
		cmn_err(CE_NOTE, "MP not supported%s, booting UP only\n", msg);
		for (i = 0; i < NCPU; i++) {
			if (cpunodes[i].nodeid == 0)
				continue;
			cmn_err(CE_NOTE, "cpu%d: %s version 0x%x\n",
				    cpunodes[i].upaid,
				    cpunodes[i].name, cpunodes[i].version);
		}
		use_mp = 0;
	}
	/*
	 * Set max cpus we can have based on ncpunode and use_mp
	 * (revisited when dynamic attach of cpus becomes possible).
	 */
	if (use_mp)
		max_ncpus = ncpunode;
	else
		max_ncpus = 1;
}

/*
 * The first sysio must always programmed up for the system clock and error
 * handling purposes, referenced by v_sysio_addr in machdep.c.
 */
static void
have_sbus(dnode_t node)
{
	int size;
	u_int portid;

	size = GETPROPLEN(node, "upa-portid");
	if (size == -1 || size > sizeof (portid))
		cmn_err(CE_PANIC, "upa-portid size");
	if (GETPROP(node, "upa-portid", (caddr_t)&portid) == -1)
		cmn_err(CE_PANIC, "upa-portid");

	found_iobus = 1;

	/*
	 * mark each entry that needs a physical TSB
	 */
	iommu_tsb_physaddr[portid] = (u_longlong_t) sbus_iommu_tsb_alloc_size;
}

/*
 *	Programmed up the system clock when we see a valid counter-timer
 *	node
 */
static void
have_counter(dnode_t node)
{
	int size;
	u_int counter_addr[3];
	u_int inr[2];

	if (v_clk_regs_addr != 0)
		return;

	size = GETPROPLEN(node, OBP_ADDRESS);

	/*
	 * Multiple timer-counter nodes may exist, but must have at least
	 * one that is mapped.
	 */
	if (size == -1)
		return;

	if (size != sizeof (counter_addr))
		cmn_err(CE_PANIC, "counter-timer address size");

	if (GETPROP(node, OBP_ADDRESS, (caddr_t)counter_addr) == -1) {
		cmn_err(CE_PANIC, "counter-timer");
	}
	size = GETPROPLEN(node, OBP_INTERRUPTS);
	if (size == -1 || size > sizeof (inr))
		cmn_err(CE_PANIC, "counter-timer INO size");
	if (GETPROP(node, OBP_INTERRUPTS, (caddr_t)inr) == -1) {
		cmn_err(CE_PANIC, "counter-timer INO");
	}
	clks[0].clk_count_addr = (u_longlong_t *)(counter_addr[0] +
					CLK0_CNT_OFFSET);
	clks[0].clk_limit_addr = (u_longlong_t *)(counter_addr[0] +
						CLK0_LMT_OFFSET);
	clks[0].clk_clrintr_addr = (u_longlong_t *)(counter_addr[1] +
						CLK0_CLR_OFFSET);
	clks[0].clk_mapintr_addr = (u_longlong_t *)(counter_addr[2] +
						CLK0_MAP_OFFSET);
	clks[0].clk_inum = inr[0];

	v_clk_regs_addr = (caddr_t)clks[0].clk_count_addr;
	v_clk_clr_regs_addr = (caddr_t)clks[0].clk_clrintr_addr;

	clks[1].clk_count_addr = (u_longlong_t *)(counter_addr[0] +
						CLK1_CNT_OFFSET);
	clks[1].clk_limit_addr = (u_longlong_t *)(counter_addr[0] +
						CLK1_LMT_OFFSET);
	clks[1].clk_clrintr_addr = (u_longlong_t *)(counter_addr[1] +
						CLK1_CLR_OFFSET);
	clks[1].clk_mapintr_addr = (u_longlong_t *)(counter_addr[2] +
						CLK1_MAP_OFFSET);
	clks[1].clk_inum = inr[1];

}

/*
 * The first psycho must always programmed up for the system clock and error
 * handling purposes.
 */
static void
have_pci(dnode_t node)
{
	int size;
	u_int portid;

	/*
	 * If there is not upa-portid, this must not be a psycho pci node.
	 */
	size = GETPROPLEN(node, "upa-portid");
	if (size == -1 || size > sizeof (portid))
		return;
	if (GETPROP(node, "upa-portid", (caddr_t)&portid) == -1)
		cmn_err(CE_PANIC, "upa-portid");

	found_iobus = 1;

	/*
	 * mark each entry that needs a physical TSB
	 */
	iommu_tsb_physaddr[portid] = (u_longlong_t) pci_iommu_tsb_alloc_size;
}

/*
 * The first eeprom is used as the TOD clock, referenced
 * by v_eeprom_addr in locore.s.
 */
static void
have_eeprom(dnode_t node)
{
	int size;

	/*
	 * multiple eeproms may exist but at least
	 * one must an "address" property
	 */
	if ((size = GETPROPLEN(node, OBP_ADDRESS)) == -1)
		return;
	if (size > sizeof (v_eeprom_addr))
		cmn_err(CE_PANIC, "eeprom addr size");
	if (GETPROP(node, OBP_ADDRESS, (caddr_t)&v_eeprom_addr) == -1)
		cmn_err(CE_PANIC, "eeprom address");
}

static void
have_auxio()
{
}

/*
 * The following functions create a list of driver names to load in
 * post_startup(). When each node is seen, it causes the following
 * function to get run.
 */

#define	MAX_DRIVERS 16

static char *sf_driver_load_list[MAX_DRIVERS];

static void
add_driver_to_list(char *name)
{
	int i = 0;

	while ((sf_driver_load_list[i] != 0) && (i < MAX_DRIVERS)) {
		/*
		 * If driver is already in list, then do not add
		 * it in.
		 */
		if (strcmp(name, sf_driver_load_list[i]) == 0) {
			return;
		}
		i++;
	}

	if (i < MAX_DRIVERS) {
		sf_driver_load_list[i] = name;
	}
}

void
load_central_modules(void)
{
	int i = 0;

	while ((sf_driver_load_list[i] != 0) && (i < MAX_DRIVERS)) {
		(void) modload("drv", sf_driver_load_list[i]);
		ddi_install_driver(sf_driver_load_list[i]);
		i++;
	}
}

static void
have_ac(void)
{
	add_driver_to_list("ac");
}

static void
have_central(void)
{
	has_central = 1;
	add_driver_to_list("central");
}

static void
have_clock_board(void)
{
	add_driver_to_list("sysctrl");
}

static void
have_environment(void)
{
	add_driver_to_list("environ");
}

static void
have_fhc(void)
{
	add_driver_to_list("fhc");
}

static void
have_simm_status(void)
{
	add_driver_to_list("simmstat");
}

static void
have_sram(void)
{
	add_driver_to_list("sram");
}
