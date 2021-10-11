/*
 * Copyright (c) 1992 Sun Microsystems, Inc.
 */

#pragma	ident  "@(#)display.c 1.20     94/08/10 SMI"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <kvm.h>
#include <varargs.h>
#include <time.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/openpromio.h>
#include <sys/clock.h>
#include <libintl.h>
#include "pdevinfo.h"
#include "display.h"
#include "anlyzerr.h"

/*
 * following are the defines for the various sizes of reset-state
 * properties that we expect to receive. We key off of these sizes
 * in order to process the error log data.
 */
#define	RST_CPU_XD1_SZ		24
#define	RST_CPU_XD2_SZ		40
#define	RST_MEM_XD1_SZ		16
#define	RST_MEM_XD2_SZ		32
#define	RST_IO_XD1_SZ		24
#define	RST_IO_XD2_SZ		40
#define	RST_BIF_XD1_SZ		68
#define	RST_BIF_XD2_SZ		136
#define	RST_BIF_NP_XD1_SZ	12
#define	RST_BIF_NP_XD2_SZ	24

/* value in uninitialized NVRAM */
#define	UN_INIT_NVRAM		0x55555555

/* should really get from sys headers but can't due to bug 1120641 */
#define	MQH_GROUP_PER_BOARD	4
#define	MX_SBUS_SLOTS		4

/*
 * The following macros for dealing with raw output from the Mostek 48T02
 * were borrowed from the kernel. Openboot passes the raw Mostek data
 * thru the device tree, and there are no library routines to deal with
 * this data.
 */

/*
 * Tables to convert a single byte from binary-coded decimal (BCD).
 */
u_char bcd_to_byte[256] = {		/* CSTYLED */
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  0,  0,  0,  0,  0,  0,
	10, 11, 12, 13, 14, 15, 16, 17, 18, 19,  0,  0,  0,  0,  0,  0,
	20, 21, 22, 23, 24, 25, 26, 27, 28, 29,  0,  0,  0,  0,  0,  0,
	30, 31, 32, 33, 34, 35, 36, 37, 38, 39,  0,  0,  0,  0,  0,  0,
	40, 41, 42, 43, 44, 45, 46, 47, 48, 49,  0,  0,  0,  0,  0,  0,
	50, 51, 52, 53, 54, 55, 56, 57, 58, 59,  0,  0,  0,  0,  0,  0,
	60, 61, 62, 63, 64, 65, 66, 67, 68, 69,  0,  0,  0,  0,  0,  0,
	70, 71, 72, 73, 74, 75, 76, 77, 78, 79,  0,  0,  0,  0,  0,  0,
	80, 81, 82, 83, 84, 85, 86, 87, 88, 89,  0,  0,  0,  0,  0,  0,
	90, 91, 92, 93, 94, 95, 96, 97, 98, 99,
};

#define	BCD_TO_BYTE(x)	bcd_to_byte[(x) & 0xff]

static int days_thru_month[64] = {
	0, 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366, 0, 0,
	0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0, 0,
	0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0, 0,
	0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365, 0, 0,
};

/*
 * Define a structure to contain both the DRAM SIMM and NVRAM
 * SIMM memory totals in MBytes.
 */
struct mem_total {
	int dram;
	int nvsimm;
};

static int disp_fail_parts(Sys_tree *, Prom_node *);
static void disp_powerfail(Sys_tree *, Prom_node *);
static void disp_err_log(Sys_tree *, Prom_node *);

static void display_board(Board_node *);
static void display_sbus_cards(Board_node *);

static Prom_node *find_cpu(Board_node *, int);
static int get_devid(Prom_node *);
static int get_cpu_freq(Prom_node *);
static void get_mem_total(Sys_tree *, struct mem_total *);
static int get_group_size(Prom_node *, int);
static Prom_node *find_card(Prom_node *, int);
static Prom_node *next_card(Prom_node *, int);
static char *get_card_name(Prom_node *);
static char *get_card_model(Prom_node *);
static char *get_time(u_char *);
static int all_boards_fail(Sys_tree *, char *);
static int prom_has_cache_size(Sys_tree *);
static int get_cache_size(Prom_node *pnode);

static int analyze_bif(Prom_node *, int, int);
static int analyze_iounit(Prom_node *, int, int);
static int analyze_memunit(Prom_node *, int, int);
static int analyze_cpu(Prom_node *, int, int);

/*
 * This routine displays one of two types of system information trees
 * On sun4d systems, it displays data from a board based tree. On
 * a non-sun4d system , it uses a user copy of the OpenpBoot device
 * tree.
 */
int
display(Sys_tree *tree, Prom_node *root, int syserrlog)
{
	void *value;		/* PROM value */
	struct utsname uts_buf;
	Board_node *bnode;
	int exit_code;
	struct mem_total memory_total;

	(void) uname(&uts_buf);

	(void) printf(
		gettext("System Configuration:  Sun Microsystems  %s %s\n"),
		uts_buf.machine,
		get_prop_val(find_prop(root, "banner-name")));

	/* display system clock frequency */
	value = get_prop_val(find_prop(root, "clock-frequency"));
	if (value != NULL)
		(void) printf(gettext("System clock frequency: %d MHz\n"),
			((*((int *)value)) + 500000) / 1000000);
	else
		(void) printf(gettext("System clock frequency not found\n"));

	/* display total usable installed memory */
	get_mem_total(tree, &memory_total);

	(void) printf(gettext("Memory size: %4dMb\n"), memory_total.dram);

	if (memory_total.nvsimm != 0) {
		(void) printf(gettext("NVSIMM size: %4dMb\n"),
			memory_total.nvsimm);
	}

	/* display number of XDBuses */
	value = get_prop_val(find_prop(root, "n-xdbus"));
	(void) printf(gettext("Number of XDBuses: %d\n"), *(int *)value);

	/*
	 * display the CPU frequency and cache size, and memory group
	 * sizes on all the boards
	 */

	/* only print cache size header line if new PROM */
	if (prom_has_cache_size(tree)) {
		(void) printf(
			gettext("       CPU Units: Frequency Cache-Size"));
		(void) printf(gettext("        Memory Units: Group Size\n"));
		(void) printf("            ");
		(void) printf("A: MHz MB   B: MHz MB           "
			"0: MB   1: MB   2: MB   3: MB\n");
		(void) printf("            ");
		(void) printf("---------   ---------           "
			"-----   -----   -----   -----\n");
	} else {
		(void) printf(gettext("           CPU Units: Frequency"));
		(void) printf(
			gettext("               Memory Units: Group Size\n"));
		(void) printf("            ");
		(void) printf("A: MHz      B: MHz              "
			"0: MB   1: MB   2: MB   3: MB\n");
		(void) printf("            ");
		(void) printf("------      ------              "
			"-----   -----   -----   -----\n");
	}
	bnode = tree->bd_list;
	while (bnode != NULL) {
		display_board(bnode);
		bnode = bnode->next;
	}

	/* display the SBus cards on all the boards */
	/*
	 * TRANSLATION_NOTE
	 *	Following string is used as a table header.
	 *	Please maintain the current alignment in
	 *	translation.
	 */
	(void) printf(gettext("======================SBus Cards"));
	(void) printf("=========================================\n");
	bnode = tree->bd_list;
	while (bnode != NULL) {
		display_sbus_cards(bnode);
		bnode = bnode->next;
	}

	/* display failed units */
	exit_code = disp_fail_parts(tree, root);

	if (syserrlog) {
		/* display time of latest powerfail */
		disp_powerfail(tree, root);
	}

	if (syserrlog) {
		/* display POST hardware error log analysis */
		disp_err_log(tree, root);
	}

	return (exit_code);
}

/*
 * Display failed components from a sun4d system. Failures are indicated
 * by a PROM node having a status property. The value of this property
 * is a string. In some cases the string is parsed to yield more specific
 * information as to the type of the failure.
 */
static int
disp_fail_parts(Sys_tree *tree, Prom_node *root)
{
	int system_failed = 0;
	int system_degraded = 0;
	Board_node *bnode = tree->bd_list;
	Prom_node *pnode;
	Prop *prop;
	void *value;

	/* look in the root node for a disabled XDBus (SC2000 only) */
	if ((value = get_prop_val(find_prop(root, "disabled-xdbus"))) !=
		NULL) {
		system_degraded = 1;
		(void) printf(
	gettext("\nSystem running in degraded mode, XDBus %d disabled\n"),
			*(int *)value);
	}

	/* look in the /boards node for a failed system controller */
	if ((pnode = dev_find_node(root, "boards")) != NULL) {
		if ((value = get_prop_val(find_prop(pnode,
			"status"))) != NULL) {
			if (!system_failed) {
				system_failed = 1;
				(void) printf(
	gettext("\nFailed Field Replaceable Units (FRU) in System:\n"));
				(void) printf("=========================="
					"====================\n");
			}
			(void) printf(
	gettext("System control board failed: %s\n"), (char *)value);
			(void) printf(
	gettext("Failed Field Replaceable Unit: System control board\n"));
		}
	}

	/* go through all of the boards */
	while (bnode != NULL) {
		/* find failed chips */
		pnode = find_failed_node(bnode->nodes);
		if (pnode != NULL) {
			if (!system_failed) {
				system_failed = 1;
				(void) printf(
	gettext("\nFailed Field Replaceable Units (FRU) in System:\n"));
				(void) printf("=========================="
					"====================\n");
			}
		}

		while (pnode != NULL) {
			void *value;
			char *name;

			value = get_prop_val(find_prop(pnode, "status"));

			/* sanity check of data retreived from PROM */
			if (value == NULL) {
				pnode = next_failed_node(pnode);
				continue;
			}

			if (((name = get_node_name(pnode->parent)) != NULL) &&
			    (strstr(name, "sbi"))) {
				(void) printf(gettext("SBus Card "));
			} else if (((name = get_node_name(pnode)) != NULL) &&
			    (strstr(name, "bif")) && value) {
				char *device;

				/* parse string to get rid of "fail-" */
				device = strchr((char *)value, '-');

				if (device != NULL)
					(void) printf("%s", device+1);
				else
					(void) printf("%s ", (char *)value);
			} else {
				(void) printf("%s ", get_node_name(pnode));
			}

			(void) printf(
				gettext("unavailable on System Board #%d\n"),
				bnode->board_num);

			(void) printf(
				gettext("Failed Field Replaceable Unit is "));

			/*
			 * In this loop we want to print failures for FRU's
			 * This includes SUPER-SPARC modules, SBus cards,
			 * system boards, and system backplane.
			 */

			/* Look for failed SuperSPARC module */
			if (strstr(get_node_name(pnode), "cpu-unit") &&
			    (strstr((char *)value, "VikingModule") ||
			    strstr((char *)value, "CPUModule"))) {
				int devid = get_devid(pnode);
				char cpu;

				if ((devid >> 3) & 0x1)
					cpu = 'B';
				else
					cpu = 'A';

				(void) printf(
					gettext("SuperSPARC Module %c\n\n"),
					cpu);

			} else if (((name = get_node_name(pnode->parent)) !=
			    NULL) && (strstr(name, "sbi"))) {
				/* Look for failed SBUS card */
				int card;
				void *value;

				value = get_prop_val(find_prop(pnode, "reg"));

				if (value != NULL) {
					card = *(int *)value;
					(void) printf(
						gettext("SBus card %d\n\n"),
						card);
				} else {
					(void) printf(
						gettext("System Board #%d\n\n"),
						bnode->board_num);
				}
			} else if (strstr(get_node_name(pnode), "bif") &&
			    all_boards_fail(tree, value)) {
				(void) printf(gettext("System Backplane\n\n"));
			} else {
				(void) printf(gettext("System Board #%d\n\n"),
					bnode->board_num);
			}

			pnode = next_failed_node(pnode);
		}

		/* find the memory node */
		pnode = dev_find_node(bnode->nodes, "mem-unit");

		/* look for failed memory SIMMs */
		value = NULL;
		prop = find_prop(pnode, "bad-parts");
		if ((value = get_prop_val(prop)) != NULL) {
			int size;

			if (!system_failed) {
				system_failed = 1;
				(void) printf(
	gettext("\nFailed Field Replaceable Units (FRU) in System:\n"));
				(void) printf("=========================="
					"====================\n");
			}

			(void) printf(
	gettext("Memory SIMM group unavailable on System Board #%d\n"),
				bnode->board_num);
			/*
			 * HACK for OBP bug in old PROMs. String
			 * was not NULL terminated.
			 */
			size = prop->value.opp.oprom_size;
			prop->value.opp.oprom_array[size-1] = 0;
			(void) printf(
		gettext("Failed Field Replaceable Unit: SIMM(s) %s\n\n"),
				(char *)value);
		}

		bnode = bnode->next;
	}

	if (!system_failed) {
		(void) printf(gettext("\nNo failures found in System\n"));
		(void) printf("===========================\n\n");
	}

	if (system_degraded || system_failed)
		return (1);
	else
		return (0);
}	/* end of disp_fail_parts() */

static void
disp_powerfail(Sys_tree *tree, Prom_node *root)
{
	Prom_node *pnode;
	char *option_str = "options";
	char *pf_str = "powerfail-time";
	char *value_str;
	time_t value;

	pnode = dev_find_node(root, option_str);
	if (pnode == NULL) {
		printf("No '%s' node!\n", option_str);
		return;
	}

	value_str = get_prop_val(find_prop(pnode, pf_str));
	if (value_str == NULL) {
		printf("No '%s' option!\n", pf_str);
		return;
	}

	value = (time_t)atoi(value_str);
	if (value == 0)
		return;

	(void) printf(
		gettext("Most recent AC Power Failure:\n"));
	(void) printf("=============================\n");
	(void) printf("%s", ctime(&value));
	(void) printf("\n");
}

/*
 * For each cpu-unit, mem-unit, io-unit, sbi, and bif in the system tree
 * dump any errors found in that unit. This is done in a board by board
 * manner.
 */
static void
disp_err_log(Sys_tree *tree, Prom_node *root)
{
	Board_node *bnode = tree->bd_list;
	Prom_node *pnode;
	u_char *mostek;

	pnode = dev_find_node(root, "boards");
	mostek = (u_char *) get_prop_val(find_prop(pnode, "reset-time"));

	/*
	 * Log date is stored in BCD. Convert to decimal, then load into
	 * time structure.
	 */
	/* LINTED */
	if ((mostek != NULL) && (*(int *)mostek != 0)) {
		(void) printf(
			gettext("Analysis of most recent System Watchdog:\n"));
		(void) printf("========================================\n");

		(void) printf("Log Date: %s\n", get_time(mostek));
	} else {
		(void) printf(gettext("No System Watchdog Log found\n"));
		(void) printf("============================\n");
		return;
	}


	/* go through all of the boards */
	while (bnode != NULL) {
		int hdr_printed = 0;

		/* find the cpu-units and analyze */
		pnode = dev_find_node(bnode->nodes, "cpu-unit");
		while (pnode != NULL) {
			/* analyze this CPU node */
			hdr_printed = analyze_cpu(pnode, bnode->board_num,
				hdr_printed);

			/* get the next CPU node */
			pnode = dev_next_node(pnode, "cpu-unit");
		}

		/* find the mem-units and analyze */
		pnode = dev_find_node(bnode->nodes, "mem-unit");
		hdr_printed = analyze_memunit(pnode, bnode->board_num,
			hdr_printed);

		/* find the io-units and analyze */
		pnode = dev_find_node(bnode->nodes, "io-unit");
		hdr_printed = analyze_iounit(pnode, bnode->board_num,
			hdr_printed);

		/* get the bif node for this board and analyze */
		pnode = dev_find_node(bnode->nodes, "bif");
		hdr_printed = analyze_bif(pnode, bnode->board_num, hdr_printed);

		/* move to next board in list */
		bnode = bnode->next;
	}
}	/* end of disp_err_log() */

/*
 * Display the CPU frequencies and cache sizes and memory group
 * sizes on a system board.
 */
void
display_board(Board_node *board)
{
	int dev_id;		/* device ID of CPU unit */
	int group;		/* index of memory group */
	Prom_node *pnode;
	/* print the board number first */
	/*
	 * TRANSLATION_NOTE
	 *	Following string is used as a table header.
	 *	Please maintain the current alignment in
	 *	translation.
	 */
	(void) printf(gettext("Board%d:     "), board->board_num);

	/* display the CPUs and their operating frequencies */
	for (dev_id = 0; dev_id < 0x10; dev_id += 0x8) {
		int freq;	/* CPU clock frequency */
		int cachesize;	/* MXCC cache size */

		freq = (get_cpu_freq(find_cpu(board, dev_id)) + 500000) /
			1000000;

		cachesize = get_cache_size(find_cpu(board, dev_id));

		if (freq != 0) {
			(void) printf("   %d ", freq);
			if (cachesize == 0) {
				(void) printf("      ");
			} else {
				(void) printf("%0.1f   ", (float)cachesize/
					(float)(1024*1024));
			}
		} else {
			(void) printf("            ");
		}
	}

	(void) printf("        ");
	/* display the memory group sizes for this board */
	pnode = dev_find_node(board->nodes, "mem-unit");
	for (group = 0; group < MQH_GROUP_PER_BOARD; group++) {
		(void) printf("  %3d   ",
			get_group_size(pnode, group)/(1024*1024));
	}
	(void) printf("\n");
}	/* end of display_board() */

/*
 * Display all of the SBus Cards present on a system board. The display
 * is oriented for one line per PROM node. This translates to one line
 * per logical device on the card. The name property of the device is
 * displayed, along with the child device name, if found. If the child
 * device has a device-type that is displayed as well.
 */
static void
display_sbus_cards(Board_node *board)
{
	Prom_node *pnode;
	Prom_node *sbi_node;
	int card;
	void *value;

	/* display the SBus cards present on this board */
	/* find the io-unit node */
	pnode = dev_find_node(board->nodes, "io-unit");
	/* now find the SBI node */
	sbi_node = dev_find_node(pnode, "sbi");

	/* get sbus clock frequency */
	value = get_prop_val(find_prop(sbi_node, "clock-frequency"));

	for (card = 0; card < MX_SBUS_SLOTS; card++) {
		Prom_node *card_node;
		int device = 0;		/* # of device on card */

		card_node = find_card(sbi_node, card);

		/* format for no card or failed card plugged in */
		if ((card_node == NULL) ||
		    (find_prop(card_node, "status") != NULL)) {
			if (card == 0) {
				/* display sbus clock frequency */
				if (value != NULL) {
					(void) printf(
						gettext("\nBoard%d:        "
						"SBus clock frequency: "
						"%d MHz\n"),
						board->board_num,
						((*((int *) value))
						+ 500000) / 1000000);
				} else {
					(void) printf(
						gettext("\nBoard%d:        "
						"SBus clock frequency not "
						"found\n"),
						board->board_num);
				}
			}
			/*
			 * TRANSLATION_NOTE
			 * Following string is used as a table header.
			 * Please maintain the current alignment in
			 * translation.
			 */
			(void) printf(
				gettext("               %d: <empty>\n"),
				card);
			continue;
		}

		/* now display all of the node names for that card */
		while (card_node != NULL) {
			char *model;
			char *name;
			char fmt_str[(OPROMMAXPARAM*3)+1];
			char tmp_str[OPROMMAXPARAM+1];

			model = get_card_model(card_node);
			name = get_card_name(card_node);

			if ((card == 0) && (device == 0)) {
				/* display sbus clock frequency */
				if (value != NULL) {
					(void) printf(
						gettext("\nBoard%d:        "
						"SBus clock frequency: "
						"%d MHz\n"),
						board->board_num,
						((*((int *) value))
						+ 500000) / 1000000);
				} else {
					(void) printf(
						gettext("\nBoard%d:        "
						"SBus clock frequency not "
						"found\n"),
						board->board_num);
				}
			}
			/*
			 * TRANSLATION_NOTE
			 * Following string is used as a table header.
			 * Please maintain the current alignment in
			 * translation.
			 */
			if (device == 0) {
				(void) printf("               %d: ", card);
				(void) sprintf(fmt_str, "%s", name);
			} else {
				(void) printf("                  ");
				(void) sprintf(fmt_str, "%s", name);
			}

			if (card_node->child != NULL) {
				void *value;

				(void) sprintf(tmp_str, "/%s",
					get_node_name(card_node->child));
				(void) strcat(fmt_str, tmp_str);

				if ((value = get_prop_val(find_prop(
				    card_node->child,
				    "device_type"))) != NULL) {
					(void) sprintf(tmp_str, "(%s)",
						(char *)value);
					(void) strcat(fmt_str, tmp_str);
				}
			}

			(void) printf("%-20s", fmt_str);

			if (model != NULL) {
				(void) printf("\t'%s'\n", model);
			} else {
				(void) printf("\n");
			}
			card_node = next_card(card_node, card);
			device++;
		}
	}

}	/* end of display_sbus_cards() */

/*
 * This function returns the device ID of a Prom node for sun4d. It returns
 * -1 on an error condition. This was done because 0 is a legal device ID
 * in sun4d, whereas -1 is not.
 */
static int
get_devid(Prom_node *pnode)
{
	Prop *prop;
	void *val;

	if ((prop = find_prop(pnode, "device-id")) == NULL)
		return (-1);

	if ((val = get_prop_val(prop)) == NULL)
		return (-1);

	return (*(int *)val);
}	/* end of get_devid() */

/*
 * Find the CPU on the current board with the requested device ID. If this
 * rountine is passed a NULL pointer, it simply returns NULL.
 */
static Prom_node *
find_cpu(Board_node *board, int dev_id)
{
	Prom_node *pnode;

	/* find the first cpu node */
	pnode = dev_find_node(board->nodes, "cpu-unit");

	while (pnode != NULL) {
		if ((get_devid(pnode) & 0xF) == dev_id)
			return (pnode);

		pnode = dev_next_node(pnode, "cpu-unit");
	}
	return (NULL);
}	/* end of find_cpu() */

/*
 * Return the operating frequency of a processor in Hertz. This function
 * requires as input a legal "cpu-unit" node pointer. If a NULL pointer
 * is passed in or the clock-frequency property does not exist, the function
 * returns 0.
 */
static int
get_cpu_freq(Prom_node *pnode)
{
	Prop *prop;		/* property pointer for "clock-frequency" */
	Prom_node *node;	/* node of "cpu" device */
	void *val;		/* value of "clock-frequency" */

	/* first find the "TI,TMS390Z55" device under "cpu-unit" */
	if ((node = dev_find_node(pnode, "TI,TMS390Z55")) == NULL) {
		return (0);
	}

	/* now find the property */
	if ((prop = find_prop(node, "clock-frequency")) == NULL) {
		return (0);
	}

	if ((val = get_prop_val(prop)) == NULL) {
		return (0);
	}

	return (*(int *)val);
}	/* end of get_cpu_freq() */

/*
 * returns the size of the given processors external cache in
 * bytes. If the properties required to determine this are not
 * present, then the function returns 0.
 */
static int
get_cache_size(Prom_node *pnode)
{
	Prom_node *node;	/* node of "cpu" device */
	int *nlines_p;		/* pointer to number of cache lines */
	int *linesize_p;	/* pointer to data for linesize */

	/* first find the "TI,TMS390Z55" device under "cpu-unit" */
	if ((node = dev_find_node(pnode, "TI,TMS390Z55")) == NULL) {
		return (0);
	}

	/* now find the properties */
	if ((nlines_p = (int *)get_prop_val(find_prop(node,
		"ecache-nlines"))) == NULL) {
		return (0);
	}

	if ((linesize_p = (int *)get_prop_val(find_prop(node,
		"ecache-line-size"))) == NULL) {
		return (0);
	}

	return (*nlines_p * *linesize_p);
}

/*
 * Total up all of the configured memory in the system and update the
 * mem_total structure passed in by the caller.
 */
void
get_mem_total(Sys_tree *tree, struct mem_total *mem_total)
{
	Board_node *bnode;

	mem_total->dram = 0;
	mem_total->nvsimm = 0;

	/* loop thru boards */
	bnode = tree->bd_list;
	while (bnode != NULL) {
		Prom_node *pnode;
		int group;

		/* find the memory node */
		pnode = dev_find_node(bnode->nodes, "mem-unit");

		/* add in all groups on this board */
		for (group = 0; group < MQH_GROUP_PER_BOARD; group++) {
			int group_size = get_group_size(pnode, group)/0x100000;

			/*
			 * 32 MByte is the smallest DRAM group on sun4d.
			 * If the memory size is less than 32 Mbytes,
			 * then the only legal SIMM size is 4 MByte,
			 * and that is an NVSIMM.
			 */
			if (group_size >= 32) {
				mem_total->dram += group_size;
			} else if (group_size == 4) {
				mem_total->nvsimm += group_size;
			}
		}
		bnode = bnode->next;
	}
}	/* end of get_mem_total() */

/*
 * Return group size in bytes of a memory group on a sun4d.
 * If any errors occur during reading of properties or an
 * illegal group number is input, 0 is returned.
 */
static int
get_group_size(Prom_node *pnode, int group)
{
	Prop *prop;
	void *val;
	int *grp;

	if ((prop = find_prop(pnode, "size")) == NULL)
		return (0);

	if ((val = get_prop_val(prop)) == NULL)
		return (0);
	if ((group < 0) || (group > 3))
		return (0);

	grp = (int *)val;

	/*
	 * If we read a group size of 8 MByte, then we have run
	 * into the PROM bug 1148961. We must then divide the
	 * size by 2.
	 */
	if (grp[group] == 8*1024*1024) {
		return (grp[group]/2);
	} else {
		return (grp[group]);
	}
}	/* end of get_group_size() */

/*
 * This function finds the mode of the first device with reg number word 0
 * equal to the requested card number. A sbi-unit node is passed in
 * as the root node for this function.
 */
static Prom_node *
find_card(Prom_node *node, int card)
{
	Prom_node *pnode;

	if (node == NULL)
		return (NULL);

	pnode = node->child;

	while (pnode != NULL) {
		void *value;

		if ((value = get_prop_val(find_prop(pnode, "reg"))) != NULL)
			if (*(int *)value == card)
				return (pnode);
		pnode = pnode->sibling;
	}
	return (NULL);
}	/* end of find_card() */

/*
 * Find the next sibling node on the requested SBus card.
 */
static Prom_node *
next_card(Prom_node *node, int card)
{
	Prom_node *pnode;

	if (node == NULL)
		return (NULL);

	pnode = node->sibling;
	while (pnode != NULL) {
		void *value;

		if ((value = get_prop_val(find_prop(pnode, "reg"))) != NULL)
			if (*(int *)value == card)
				return (pnode);
		pnode = pnode->sibling;
	}
	return (NULL);
}	/* end of next_card() */

/*
 * returns the address of the value of the name property for this PROM
 * node.
 */
static char *
get_card_name(Prom_node *node)
{
	char *name;

	if (node == NULL)
		return (NULL);

	/* get the model number of this card */
	name = (char *)get_prop_val(find_prop(node, "name"));
	return (name);
}	/* end of get_card_name() */

/*
 * returns the address of the value of the model property for this PROM
 * node.
 */
static char *
get_card_model(Prom_node *node)
{
	char *model;

	if (node == NULL)
		return (NULL);

	/* get the model number of this card */
	model = (char *)get_prop_val(find_prop(node, "model"));

	return (model);
}	/* end of get_card_model() */

/*
 * This function takes the raw Mostek data from the device tree translates
 * it into UNIXC time (secs since Jan 1, 1970) and returns a string from
 * ctime(3c).
 */
static char *
get_time(u_char *mostek)
{
	time_t utc;
	int sec, min, hour, day, month, year;

	year	= BCD_TO_BYTE(mostek[6]) + YRBASE;
	month	= BCD_TO_BYTE(mostek[5] & 0x1f) + ((year & 3) << 4);
	day	= BCD_TO_BYTE(mostek[4] & 0x3f);
	hour	= BCD_TO_BYTE(mostek[2] & 0x3f);
	min	= BCD_TO_BYTE(mostek[1] & 0x7f);
	sec	= BCD_TO_BYTE(mostek[0] & 0x7f);

	utc = (year - 70);		/* next 3 lines: utc = 365y + y/4 */
	utc += (utc << 3) + (utc << 6);
	utc += (utc << 2) + ((year - 69) >> 2);
	utc += days_thru_month[month] + day - 1;
	utc = (utc << 3) + (utc << 4) + hour;	/* 24 * day + hour */
	utc = (utc << 6) - (utc << 2) + min;	/* 60 * hour + min */
	utc = (utc << 6) - (utc << 2) + sec;	/* 60 * min + sec */

	return (ctime((time_t *)&utc));
}

/*
 * Check the first CPU in the device tree and see if it has the
 * properties 'ecache-line-size' and 'ecache-nlines'. If so,
 * return 1, else return 0.
 */
static int
prom_has_cache_size(Sys_tree *tree)
{
	Board_node *bnode;

	if (tree == NULL) {
		return (0);
	}

	/* go through the boards until you find a CPU to check */
	for (bnode = tree->bd_list; bnode != NULL; bnode = bnode->next) {
		Prom_node *pnode;

		/* find the first cpu node */
		pnode = dev_find_node(bnode->nodes, "cpu-unit");

		/* if no CPUs on board, skip to next */
		if (pnode != NULL) {
			if (get_cache_size(pnode) == 0) {
				return (0);
			} else {
				return (1);
			}
		}
	}
	/* never found the props, so return 0 */
	return (0);
}

/*
 * Check and see if all boards in the system fail on one of the XDBuses
 * failing in the current string. If this is true and there is more
 * than one board in the system, then the backplane is most likely at
 * fault.
 */
static int
all_boards_fail(Sys_tree *tree, char *value)
{
	char temp[OPROMMAXPARAM];
	char *token;

	if ((tree == NULL) || (value == NULL))
		return (0);

	if (tree->board_cnt == 1)
		return (0);

	(void) strcpy(temp, value);

	/* toss the first token in the string */
	(void) strtok(temp, "-");
	token = strtok(NULL, "- ");

	/* loop through all the tokens in this string */
	while (token != NULL) {
		Board_node *bnode = tree->bd_list;
		int match = 1;

		/* loop thru all the boards looking for a match */
		while (bnode != NULL) {
			Prom_node *bif = dev_find_node(bnode->nodes, "bif");
			char *bif_status = get_prop_val(find_prop(bif,
				"status"));

			if ((bif == NULL) || (bif_status == NULL)) {
				match = 0;
				break;
			}

			if (!strstr(bif_status, token)) {
				match = 0;
				break;
			}

			bnode = bnode->next;

			/*
			 * if all boards fail with this token, then
			 * return 1
			 */
			if (match)
				return (1);
		}
		token = strtok(NULL, " -");
	}

	return (0);
}	/* end of all_boards_fail() */

/*
 * The following functions are all for reading reset-state information
 * out of prom nodes and calling the appropriate error analysis code
 * that has been ported from sun4d POST.
 */
static int
analyze_cpu(Prom_node *node, int board, int hdr_printed)
{
	void *value;
	Prop *prop;
	unsigned long long cc_err;
	unsigned long long ddr;
	unsigned long long dcsr;
	int n_xdbus;
	int devid;
	int i;

	/* get the value pointer from the reset-state property */
	prop = find_prop(node, "reset-state");

	if (prop == NULL)
		return (hdr_printed);

	value = get_prop_val(prop);

	/* exit if error finding property */
	if (value == NULL)
		return (hdr_printed);

	/* copy the reset_state info out of the Prom node */

	/* check for uninitialized NVRAM/TOD data */
	if (*(int *)value == UN_INIT_NVRAM)
		return (hdr_printed);

	/* read the device ID from the node */
	if ((devid = get_devid(node)) == -1)
		return (hdr_printed);

	/* first 8 bytes are MXCC Error register */
	cc_err = *(unsigned long long *)value;
	hdr_printed = ae_cc(devid, cc_err, board, hdr_printed);

	/* if length == (MXCC + 1 BW) => 1 XDBus */
	if (prop->value.opp.oprom_size == RST_CPU_XD1_SZ) {
		n_xdbus = 1;
	}
	/* else if length == (MXCC + 2 BW) => 2 XDBus */
	else if (prop->value.opp.oprom_size == RST_CPU_XD2_SZ) {
		n_xdbus = 2;
	} else {
		(void) printf("Prom node %s has incorrect status"
			" property length : %d\n",
			get_node_name(node), prop->value.opp.oprom_size);
		return (hdr_printed);
	}

	/* move Prom value pointer to first BW data */
	value = (void *)((char *)value + 8);

	/* now analyze the error logs */
	for (i = 0; i < n_xdbus; i++) {
		ddr = *(unsigned long long *)value;
		value = (void *)((char *)value + 8);
		dcsr = *(unsigned long long *)value;
		value = (void *)((char *)value + 8);
		hdr_printed = ae_bw(devid, i, dcsr, ddr, board, hdr_printed);
	}
	return (hdr_printed);
}	/* end of analyze_cpu() */

static int
analyze_memunit(Prom_node *node, int board, int hdr_printed)
{
	void *value;
	Prop *prop;
	unsigned long long ddr;
	unsigned long long dcsr;
	int n_xdbus;
	int i;

	/* get the value pointer from the reset-state property */
	prop = find_prop(node, "reset-state");

	if (prop == NULL)
		return (hdr_printed);

	value = get_prop_val(prop);

	/* exit if error finding property */
	if (value == NULL)
		return (hdr_printed);

	/* check for uninitialized NVRAM/TOD data */
	if (*(int *)value == UN_INIT_NVRAM)
		return (hdr_printed);

	/* if length == (1 MQH) => 1 XDBus */
	if (prop->value.opp.oprom_size == RST_MEM_XD1_SZ) {
		n_xdbus = 1;
	}
	/* else if length == (2 MQH) => 2 XDBus */
	else if (prop->value.opp.oprom_size == RST_MEM_XD2_SZ) {
		n_xdbus = 2;
	} else {
		(void) printf("Prom node %s has incorrect "
			"status property length : %d\n",
			get_node_name(node), prop->value.opp.oprom_size);
		return (hdr_printed);
	}

	/* now analyze the error logs */
	for (i = 0; i < n_xdbus; i++) {
		ddr = *(unsigned long long *)value;
		value = (void *)((char *)value + 8);
		dcsr = *(unsigned long long *)value;
		value = (void *)((char *)value + 8);
		hdr_printed = ae_mqh(i, dcsr, ddr, board, hdr_printed);
	}

	return (hdr_printed);
}	/* end of analyze_memunit() */

static int
analyze_iounit(Prom_node *node, int board, int hdr_printed)
{
	void *value;
	Prop *prop;
	unsigned long long ddr;
	unsigned long long dcsr;
	int n_xdbus;
	int devid;
	int i;
	int sr;

	/* get the value pointer from the reset-state property */
	prop = find_prop(node, "reset-state");

	if (prop == NULL)
		return (hdr_printed);

	value = get_prop_val(prop);

	/* exit if error finding property */
	if (value == NULL)
		return (hdr_printed);

	/* check for uninitialized NVRAM/TOD data */
	if (*(int *)value == UN_INIT_NVRAM)
		return (hdr_printed);

	/* if length == (SBI + 1 IOC) => 1 XDBus */
	if (prop->value.opp.oprom_size == RST_IO_XD1_SZ) {
		n_xdbus = 1;
	}
	/* else if length == (SBI + 2 IOC) => 2 XDBus */
	else if (prop->value.opp.oprom_size == RST_IO_XD2_SZ) {
		n_xdbus = 2;
	} else {
		(void) printf("Prom node %s has incorrect "
			"status property length : %d\n",
			get_node_name(node), prop->value.opp.oprom_size);
		return (hdr_printed);
	}

	/* read the device ID from the node */
	if ((devid = get_devid(node)) == -1)
		return (hdr_printed);

	/* read out the SBI data first */
	sr = *(int *)value;
	value = (void *)((char *)value + 4);

	/* skip the SBI control register */
	value = (void *)((char *)value + 4);

	/* now analyze the error logs */
	for (i = 0; i < n_xdbus; i++) {
		ddr = *(unsigned long long *)value;
		value = (void *)((char *)value + 8);
		dcsr = *(unsigned long long *)value;
		value = (void *)((char *)value + 8);
		hdr_printed = ae_ioc(i, dcsr, ddr, board, hdr_printed);
	}
	hdr_printed = ae_sbi(devid, sr, board, hdr_printed);

	return (hdr_printed);
}	/* end of analyze_iounit() */

static int
analyze_bif(Prom_node *node, int board, int hdr_printed)
{
	void *value;
	Prop *prop;
	int n_xdbus;
	int i;
	int length;	/* length of Openprom property */

	/* get the value pointer from the reset-state property */
	prop = find_prop(node, "reset-state");

	if (prop == NULL)
		return (hdr_printed);

	value = get_prop_val(prop);

	/* exit if error finding property */
	if (value == NULL)
		return (hdr_printed);

	/* check for uninitialized NVRAM/TOD data */
	if (*(int *)value == UN_INIT_NVRAM)
		return (hdr_printed);

	length = prop->value.opp.oprom_size;

	/* is this a processor or non-processor board? */
	if ((length == RST_BIF_XD1_SZ) || (length == RST_BIF_XD2_SZ)) {
		bus_interface_ring_status bic_status;

		/* 1 or 2 XBbus? */
		if (length == RST_BIF_XD1_SZ)
			n_xdbus = 1;
		else
			n_xdbus = 2;

		/* copy the data out of the node */
		(void) memcpy((void *)&bic_status,
			(void *)prop->value.opp.oprom_array, length);

		/* now analyze the BIC errror logs */
		for (i = 0; i < n_xdbus; i++) {
			hdr_printed = analyze_bics(&bic_status, i, board,
				hdr_printed);
		}
	} else if ((length == RST_BIF_NP_XD1_SZ) ||
	    (length == RST_BIF_NP_XD2_SZ)) {
		/* analyze a non-processor bif node */
		cmp_db_state comp_bic_state;

		/* 1 or 2 XBbus? */
		if (length == RST_BIF_XD1_SZ)
			n_xdbus = 1;
		else
			n_xdbus = 2;

		/* copy the data out of the node */
		(void) memcpy((void *) &comp_bic_state,
			(void *) prop->value.opp.oprom_array, length);

		hdr_printed = dump_comp_bics(&comp_bic_state, n_xdbus, board,
			hdr_printed);
	} else {
		(void) printf("Node %s has bad reset-state prop length : %d\n",
			get_node_name(node), prop->value.opp.oprom_size);
		return (hdr_printed);
	}
	return (hdr_printed);
}	/* end of analyze_bif() */
