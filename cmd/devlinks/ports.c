#ident	"@(#)ports.c	1.14	95/02/27 SMI"
/*
 * Copyright (c) 1991 by Sun Microsystems, Inc.
 */

/* Portions  Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sac.h>
#include <sys/sunddi.h>
#include <locale.h>
#include <libdevinfo.h>

#include "porthdr.h"

static const char cusfx[] = ",cu";

struct port nm_port[MAX_NMPORTS];

struct port co_port[MAX_SYSPORTS];

struct {
    struct port *port;
    const int max_ports;
    int current_max;
} pp[2] = { 
    {co_port, MAX_SYSPORTS, -1},
    {nm_port, MAX_NMPORTS, -1} };

static const char *term_dir;
static const char *cu_dir;

void
addpnode(const unsigned int devnum,
	 const unsigned int pt,	/* Port type */
	 const char *devfsnm,
	 const int type
	 )
{
	char devnbuf[PATH_MAX+1];
	const char *format_string;

#ifdef DEBUG
	fprintf(stderr, "Adding entry %s/%c (%d)\n", 
		type ? "/dev/cua" : "/dev/term",
		devnum + (pt == P_SYSPORT ? 'a' : '0'), devnum);
#endif DEBUG

    if (devnum >= pp[pt].max_ports) {
	format_string = pt == P_SYSPORT ? "%s/%c" : "%s/%d";
	sprintf(devnbuf, format_string, type == P_DELAY ? term_dir : cu_dir,
		devnum + (pt == P_SYSPORT ? 'a' : 0));
	wmessage("Terminal device %s is beyond the capacity of ports\n\
		 -- only %d terminals permitted\n", devnbuf, pp[pt].max_ports);
	if (unlink(devnbuf) != 0) {
		wmessage("Could not unlink %s because: %s\n",
			 devnbuf, strerror(errno));
	}
	return;
    }

    pp[pt].port[devnum].devfsnm = s_strdup(devfsnm);
    pp[pt].port[devnum].opt[type].state = LN_DANGLING;

    if (pp[pt].current_max < (int)devnum)
	pp[pt].current_max = devnum;

    return;
}

/*
 * is_cu_devfs -- check if name contains the ",cu" modifier
 */
static int
is_cu_devfs(const char *name)
{
    int i;

    if (strlen(name) <= (sizeof(cusfx) - 1)) /* Dont forget '-1' for the null */
	return(0);

    name += strlen(name) - (sizeof(cusfx)-1);

    for (i = 0; i < sizeof(cusfx)-1; i++) {
	if (name[i] != cusfx[i])
	    return(0);
    }

    return(1);
}

void
do_dev_dir(const char *dirname,	/* Directory name to search */
	   const int devtype	/* Type of entries directory contains */
	   )
{
    DIR *dp;
    int linksize;
    struct dirent *entp;
    char *endp;
    unsigned int devnum;
    unsigned int port_type;
    struct stat sb;
    char namebuf[PATH_MAX+1];
    char linkbuf[PATH_MAX+1];

    /*
     * Here we only have to deal with one directory -- /dev/term
     */
    /*
     * Search a directory for special names
     */
    dp = opendir(dirname);

    if (dp == NULL) {
	fmessage(1, "Could not open directory %s because: %s\n", dirname,
		 strerror(errno));
	return;
    }

    while ((entp = readdir(dp)) != NULL) {
	if (strcmp(entp->d_name, ".") == 0 || strcmp(entp->d_name, "..") == 0)
	    continue;

	sprintf(namebuf, "%s/%s", dirname, entp->d_name);

	if (lstat(namebuf, &sb) < 0) {
	    wmessage ("Cannot stat %s\n", namebuf);
	    continue;
	}

	switch(sb.st_mode & S_IFMT) {
	case S_IFLNK:
	    linksize = readlink(namebuf,linkbuf, PATH_MAX);

	    if (linksize <= 0) {
		wmessage("Could not read symbolic link %s\n", namebuf);
		continue;
	    }

	    linkbuf[linksize] = '\0';
	    break;

	default:
	    wmessage("Should only have symbolic links in %s; \
%s is not a symbolic link\n", dirname, namebuf);
	    continue;
	}
	
	/*
	 * Check this is a valid name -- essentially a decimal number, or
	 * if a system-board port, a single lower-case letter.
	 * BugID 1178667 - (see comment below)
	 */
	if (strlen(entp->d_name) == 1 &&
	    *entp->d_name >= 'a' &&
	    *entp->d_name <= 'z') {
	    devnum = (int)(*entp->d_name - 'a');
	    port_type = P_SYSPORT;
	}
	else {
	    devnum = strtoul(entp->d_name, &endp, 10);
	    if (endp != entp->d_name + strlen(entp->d_name)) {
#ifdef	DEBUG
		/*
		 * BugID 1178667
		 * Don't complain about invalid terminal names since
		 * some drivers (specifically PCMCIA drivers) don't
		 * create device names with a last numeric component.
		 */
		wmessage("Invalid terminal name %s -- last component should be numeric\n",
			 namebuf);
#endif
		continue;
	    }
	    port_type = P_NORMPORT;
	}

	/*
	 * Check link points to /devices
	 */
	if (strncmp("../../devices/", linkbuf, 12) != 0) {
	    wmessage("Link %s is a symbolic link to %s, not into ../../devices - ignored\n",
		     namebuf, linkbuf);
	    continue;
	}


	if (devtype == P_NODELAY) {
		if (!is_cu_devfs(linkbuf)) {
#ifdef	DEBUG
			wmessage("Invalid link found in cua entry\n");
#endif
			continue;
		}
		linkbuf[strlen(linkbuf)-(sizeof(cusfx)-1)] = '\0';
	}

	/*
	 * Add new entry to device node list
	 */
	addpnode(devnum, port_type, linkbuf, devtype);
    }

    closedir(dp);
    return;
}

void
get_dev_entries(void)
{
    create_dirs(term_dir);	/* Make sure it exists */
    do_dev_dir(term_dir, P_DELAY);
    create_dirs(cu_dir);	/* Make sure it exists */
    do_dev_dir(cu_dir, P_NODELAY);
}

/*
 * devfs_entry -- routine called when a 'PORT' devfs entry is found
 *
 * This routine is called by devfs_find() when a matching devfs entry is found.
 * It is passwd the name of the devfs entry.
 */
void
devfs_entry(const char *devfsnm, const char *devfstype,
    const dev_info_t *dip, struct ddi_minor_data *dmip,
    struct ddi_minor_data *dmap)
{
    int i;			/* Temporary */
    int devtype;		/* Device link type */
    int minfree = -1;		/* First free port entry */
    unsigned int pt;		/* Port type */

    char fulldevfsnm[PATH_MAX];	/* Holds full devfs name */

#ifdef DEBUG
	fprintf(stderr, "devfs_entry: '%s' of type '%s'\n", 
		devfsnm, devfstype);
#endif DEBUG

    /*
     * Construct device name  - we have either a normal device or a
     * dialout (cu) device.
     */
    sprintf(fulldevfsnm, "../../devices/%s", devfsnm);

    if (is_cu_devfs(devfsnm)) {
	devtype = P_NODELAY;
	fulldevfsnm[strlen(fulldevfsnm)-(sizeof(cusfx)-1)] = '\0';
				/* Kill off the ',cu' suffix */
    }
    else {
	devtype = P_DELAY;
    }

	/*
	 * BugID 1178667
	 * If this is a PCMCIA device handled by the pcser driver,
	 * then don't attempt to log it in the database since the
	 * PCMCIA device links are created by the PCMCIA daemon.
	 */
	if (strstr(fulldevfsnm, "pcser")) {
#ifdef	DEBUG
		fprintf(stderr,"devfs_entry: NOT creating node for [%s]\n",
								fulldevfsnm);
#endif	DEBUG
		return;
	}

    /*
     * Set port_type from devfs type
     */
    if (strcmp(DDI_NT_SERIAL_MB, devfstype) == 0 ||
	strcmp(DDI_NT_SERIAL_MB_DO, devfstype) == 0)
	    pt = P_SYSPORT;
    else
	    pt = P_NORMPORT;
    
    /*
     * search the database looking for a corresponding 'port' entry.
     * If not found, create it.
     */
    for (i = 0; i <= pp[pt].current_max; i++) {
	if (pp[pt].port[i].devfsnm != 0) {
	    if (strcmp(pp[pt].port[i].devfsnm, fulldevfsnm) == 0)
		break;
	}
	else if ( minfree < 0)
	    minfree = i;
    }

    if (i <= pp[pt].current_max) {
	/* Entry found; so do insertions and validations */
	if (pp[pt].port[i].opt[devtype].state == LN_DANGLING) {
	    pp[pt].port[i].opt[devtype].state = LN_VALID;
	}
	else {
	    pp[pt].port[i].opt[devtype].state = LN_MISSING;
	}
    }
    else {
	/* No previous entry, so add one */
	if (minfree < 0) {
	    if ((pp[pt].current_max+1) >= pp[pt].max_ports) {
		wmessage("Too many ports entries -- only %d allowed\n",
			 pp[pt].max_ports);
		return;
	    }
	    minfree = ++pp[pt].current_max;
	}

	pp[pt].port[minfree].devfsnm = s_strdup(fulldevfsnm);
	pp[pt].port[minfree].opt[devtype].state = LN_MISSING;
    }
}
void
get_devfs_entries(void)
{
    devfs_find(DDI_NT_SERIAL ":", devfs_entry, 0);
}

void
remove_links(void)
{
    int i;
    unsigned int pt;
    const char *format_string;
    char devnbuf[PATH_MAX+1];

    for (pt=P_SYSPORT; pt <= P_NORMPORT; pt++) {
	format_string = pt == P_SYSPORT ? "%s/%c" : "%s/%d";
#ifdef DEBUG
	fprintf(stderr,"remove_links: current_max = %d\n", 
		pp[pt].current_max);
#endif DEBUG
	for (i=0; i<=pp[pt].current_max; i++) {
	    if (pp[pt].port[i].devfsnm != NULL) {
		if (pp[pt].port[i].opt[P_DELAY].state == LN_INVALID ||
		    pp[pt].port[i].opt[P_DELAY].state == LN_DANGLING) {
		    sprintf(devnbuf, format_string, term_dir,
			    pt == P_SYSPORT ? i + 'a' : i);
		    if (unlink(devnbuf) != 0) {
			wmessage("Could not unlink %s because: %s\n",
				 devnbuf, strerror(errno));
		    }
		    else if (pp[pt].port[i].opt[P_DELAY].state == LN_INVALID)
			pp[pt].port[i].opt[P_DELAY].state = LN_MISSING;
		}

		if (pp[pt].port[i].opt[P_NODELAY].state == LN_INVALID ||
		    pp[pt].port[i].opt[P_NODELAY].state == LN_DANGLING) {
		    sprintf(devnbuf, format_string, cu_dir,
			    pt == P_SYSPORT ? i + 'a' : i);
		    if (unlink(devnbuf) != 0) {
			wmessage("Could not unlink %s because: %s\n",
				 devnbuf, strerror(errno));
		    }
		    else if (pp[pt].port[i].opt[P_NODELAY].state == LN_INVALID)
			pp[pt].port[i].opt[P_NODELAY].state = LN_MISSING;
		}
	    }
	}
    }
}

void
add_links(void)
{
    int i;
    unsigned int pt;
    const char *format_string;
    char devnbuf[PATH_MAX+1];
    char devfsnbuf[PATH_MAX+1];

    for (pt=P_SYSPORT; pt <= P_NORMPORT; pt++) {
	format_string = pt == P_SYSPORT ? "%s/%c" : "%s/%d";
	for (i=0; i <= pp[pt].current_max; i++) {
	    if (pp[pt].port[i].devfsnm != NULL) {
		if (pp[pt].port[i].opt[P_DELAY].state == LN_MISSING) {
		    sprintf(devnbuf, format_string, term_dir,
			    pt == P_SYSPORT ? i + 'a' : i);
		    sprintf(devfsnbuf, "%s", pp[pt].port[i].devfsnm);

		    if (symlink(devfsnbuf, devnbuf) != 0) {
			wmessage("Could not create symlink %s because: %s\n",
				 devnbuf, strerror(errno));
		    }
		    else
			pp[pt].port[i].opt[P_DELAY].state = LN_VALID;

		}

		if (pp[pt].port[i].opt[P_NODELAY].state == LN_MISSING) {
		    sprintf(devnbuf, format_string, cu_dir,
			    pt == P_SYSPORT ? i + 'a' : i);
		    sprintf(devfsnbuf, "%s%s", pp[pt].port[i].devfsnm, cusfx);

		    if (symlink(devfsnbuf, devnbuf) != 0) {
			wmessage("Could not create symlink %s because: %s\n",
				 devnbuf, strerror(errno));
		    }
		    else
			pp[pt].port[i].opt[P_NODELAY].state = LN_VALID;
		}
	    }
	}
    }
}

/*
 * Get the name of a tty device from a pmtab entry.
 * Note the /dev/term/ part is taken off.
 */
static char *
get_tty_name(
	     const char * buffer) /* Pointer to a typical pmtab entry */
{
    int	i;
    const char	*ptr;
    static char	tty_name[21];


    /*
     * Note the eighth field( ':' seperated) is the tty name field
     */
    for ( i = 1, ptr = strchr( buffer, ':'); i < 8; i++, ptr = strchr( ptr, ':')) 
	ptr++;

    ptr += strlen(term_dir) + 2; /* + 2 to get rid of the '/' as well */

    for (i=0; i<20 && *ptr != '\0' && *ptr != ':'; i++) {
	if (!isdigit(*ptr))
	    return(NULL);
	else
	    tty_name[i] = *ptr++;
    }

    if (i == 0)
	return(NULL);
    else {
	tty_name[i] = '\0';
	return( tty_name);
    }
}

int
execute(const char *s)
{
	int	status;
	int	fd;
	pid_t	pid;
	pid_t	w;

  
	if (( pid = fork()) == 0) {
		close( 0);
		close( 1);
		close( 2);
		fd = open( "/dev/null", O_RDWR);
		dup( fd);
		dup( fd);
		(void) execl( "/sbin/sh", "sh", "-c", s, 0);
		_exit(127);
	}
	while (( w = wait( &status)) != pid && w != (pid_t)-1)
		;

	return(( w == (pid_t)-1)? w: status);
}

#define PM_GRPSZ	64

/*
 * pm_delete() checks for the existence of a port monitor
 * number pm_number.  If a port monitor exists, it is 
 * removed. 
 */

void
pm_delete(int pm_number)
{
	char	cmdline[256];
	int	sys_ret;
#ifdef DEBUG
	fprintf(stderr, "pm_delete: pm_number = %d\n", pm_number);
#endif DEBUG
	sprintf( cmdline, "/usr/sbin/sacadm -L -p ttymon%d", pm_number);
	sys_ret = execute( cmdline);
	if (( sys_ret>>8) != E_NOEXIST) {
#ifdef DEBUG
		fprintf(stderr, "pm_delete: removing port monitor"
		    "ttymon%d\n", pm_number);
#endif DEBUG
	    /*
	     * If port monitors do exist for a
	     * board not installed then need to clean
	     * them up
	     */
	    sprintf( cmdline, "/usr/sbin/sacadm -r -p ttymon%d", pm_number);
	    execute( cmdline);
	}
}

void
update_admin_db(void)
{
    char	cmdline[256];
    int	sys_ret;
    FILE	*fs_popen;
    char	temp_buffer[512];
    char	*tty_num_str;	/* pointer to a tty name */
    int tty_num;
    char	*ptr;
    int	i;
    int	j;
    unsigned int pt;
    int portseen = 0;

    /*
     * Following code from AT&T ports command
     */
    /*
     * Determine which pmtab entries already exist.
     */
    sprintf( cmdline, "/usr/sbin/pmadm -L -t ttymon");
    sys_ret = execute( cmdline);

    /*
	* Add entries for any existing port monitor entries.
	* Port monitor entries which do not have a corresponding
	* device will be removed below.
     * At present this does not automatically add entries for the on-bord
     * ports.   Should it? (see bug number 1109487)
     */
    pt = P_NORMPORT;

    if (( sys_ret>>8) != E_NOEXIST) 
	if (( fs_popen = popen( cmdline, "r")) != NULL) {
	    while ( fgets( temp_buffer, 512, fs_popen) != NULL) 
		if (( tty_num_str = get_tty_name( temp_buffer)) != NULL) {
#ifdef DEBUG
			fprintf(stderr, "pmtab device name %s\n", tty_num_str);
#endif DEBUG
		    sscanf( tty_num_str, "%d", &tty_num);
		    if ((tty_num >= pp[P_NORMPORT].max_ports) || (tty_num < 0)) {
			wmessage( "tty device number %d is outside valid range (0 - %d)\n",
				 tty_num, pp[pt].max_ports-1);
			continue;
		    }
		    pp[pt].port[tty_num].pmstate = PM_PRESENT;
		    ptr = strtok( temp_buffer, ":");
		    pp[pt].port[tty_num].pmtag = s_strdup(ptr);
		    if (tty_num > pp[pt].current_max)
			pp[pt].current_max = tty_num;
		}
	}
    for ( i = 0; i <= pp[pt].current_max; i++) {
	/*
	 * AT&T code started one port monitor per board;
	 * since here (at present) we do not know how many boards we have
	 * we start a port monitor for every group of 64 ports.
	 *
	 * It may be a good idea to replace this with code that starts one pm
	 * per devfs-group of ports, where "devfs group" is defined to be a
	 * group of ports whose devfs entries all occur in the same devfs
	 * directory.
	 *
	 * Algorithm is to step through ports; checking for unneeded PM entries
	 * entries that should be there but are not.  Every PM_GRPSZ entries
	 * check to see if there are any entries for the port monitor group;
	 * if not, delete the group.
	 */

	/*
	 * Check if this port has an unneeded port monitor entry
	 */
	if (pp[pt].port[i].devfsnm == NULL &&
	    (pp[pt].port[i].pmstate == PM_PRESENT)) {
	    sprintf(cmdline, "/usr/sbin/pmadm -r -p %s -s %d",
		    pp[pt].port[i].pmtag, i);
	    execute( cmdline);
	    pp[pt].port[i].pmstate = PM_ABSENT;
	}

	/*
	 * Now check if there should be a port monitor entry: if so, create
	 * one if needed
	 */
	if (pp[pt].port[i].devfsnm != NULL &&
	    pp[pt].port[i].opt[P_DELAY].state == LN_VALID) {

	    /*
	     * Check if the port monitor exists, and if not, create it
	     */
	    if (! portseen) {
		sprintf( cmdline, "/usr/sbin/sacadm -l -p ttymon%d", i/PM_GRPSZ);
		sys_ret = execute( cmdline);
		if (( sys_ret>>8) == E_NOEXIST) {
		    sprintf( cmdline, "/usr/sbin/sacadm -a -n 2 -p ttymon%d -t ttymon -c /usr/lib/saf/ttymon -v \"`/usr/sbin/ttyadm -V`\" -y \"Ports %d-%d\"",
			    i/PM_GRPSZ, (i/PM_GRPSZ)*PM_GRPSZ,
			    (i/PM_GRPSZ)*PM_GRPSZ + PM_GRPSZ - 1);
		    sys_ret = execute( cmdline);
		}
		portseen = 1;
	    }

	    /*
	     * Now add port monitor entry (if absent)
	     */
	    if (pp[pt].port[i].pmstate != PM_PRESENT) {
		sprintf( cmdline, "/usr/sbin/pmadm -a -p ttymon%d -s %d -i root -v `/usr/sbin/ttyadm -V` -fux -y\"/dev/term/%d\" -m \"`/usr/sbin/ttyadm -d /dev/term/%d -s /usr/bin/login -l 9600 -p \\\"login: \\\"`\"",
			i/PM_GRPSZ, i, i, i);
		execute( cmdline);
	    }
	}

	/*
	 * After every PM_GRPSZ ports, delete unneeded port monitor
	 */
	if ((i + 1) % PM_GRPSZ == 0) {
	    if (! portseen) {
		j = i / PM_GRPSZ;
		pm_delete(j);
	    }
	    else
		portseen = 0;
	}

    }
    /*
	* Check to see if the last batch of ports we looked at
	* have an unneeded port monitor associated with them.
	*/
    if ((!portseen) && ((i % PM_GRPSZ) != 0)) {
    		j = i / PM_GRPSZ;
    		pm_delete(j);
    }
}

main(int argc, char **argv)
{
    extern int optind;
    char *rootdir = "";
    int c;

    (void) setlocale(LC_ALL, "");
    (void) textdomain(TEXT_DOMAIN); /* TEXT_DOMAIN defined in Makefile */

    while ((c = getopt(argc, argv, "r:")) != EOF)
	switch (c) {
	case 'r':
	    rootdir = optarg;
	    break;
	case '?':
	    fmessage(1, "Usage: ports [-r root_directory]\n");
	}

    if (optind < argc)
	fmessage(1, "Usage: ports [-r root_directory]\n");

    /*
     * Set address of term and cu dir
     */
    term_dir = s_malloc(strlen(rootdir) + sizeof("/dev/term"));
    sprintf((char *)term_dir, "%s%s", rootdir, "/dev/term");
				/* Explicitly override const attribute */
    cu_dir = s_malloc(strlen(rootdir) + sizeof("/dev/cua"));
    sprintf((char *)cu_dir, "%s%s", rootdir, "/dev/cua");
				/* Explicitly override const attribute */

    /*
     * Start building list of port devices by looking through /dev/term
     */
    get_dev_entries();

    /*
     * Now add to this real device configuration from /devices
     */
    get_devfs_entries();

    /*
     * Delete unwanted or incorrect nodes
     */
    remove_links();

    /*
     * Make new links
     */
    add_links();

    /*
     * Finally add admin database info -- actally mess with the port monitor
     */

    /* For now, don't do it if not in root */
    if (strlen(rootdir) == 0)
	update_admin_db();

    return(0);
}
