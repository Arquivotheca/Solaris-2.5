/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

#ident	"@(#)sysdef.c	1.17	94/03/03 SMI"       /* SVr4.0 1.37 */

	/*
	** This command can now print the value of data items
	** from (1) /dev/kmem is the default, and (2) a named
	** file passed with the -n argument.  If the read is from
	** /dev/kmem, we also print the value of BSS symbols.
	** The logic to support this is: if read is from file,
	** (1) find the section number of .bss, (2) look through
	** nlist for symbols that are in .bss section and zero
	** the n_value field.  At print time, if the n_value field
	** is non-zero, print the info.
	**
	** This protects us from trying to read a bss symbol from
	** the file and, possibly, dropping core.
	**
	** When reading from /dev/kmem, the n_value field is the
	** seek address, and the contents are read from that address.
	**
	** NOTE: when reading from /dev/kmem, the actual, incore
	** values will be printed, for example: the current nodename
	** will be printed, etc.
	**
	** the cmn line usage is: sysdef -i -n namelist -h -d
	** (-i for incore, though this is now the default, the option
	** is left in place for SVID compatibility)
	**
	** XXX This file has been changed to not rely on the existance of
	**     of /kernel/unix.  The -n option can be used to specify a
	**     file to be read, though its usefulness in the future will be
	**     very limited, unless the suggestion ("XXX") below is heeded.
	**     The changes were purposefully made minimal, to keep the
	**     external interface the same.
	**
	** XXX	This program should be converted to use libkvm so that
	**	it would also work on crash dumps!
	*/
#include	<stdio.h>
#include	<nlist.h>
#include	<string.h>
#include	<sys/types.h>
#include	<sys/sysmacros.h>
#include	<sys/var.h>
#include	<sys/tuneable.h>
#include	<sys/ipc.h>
#include	<sys/msg.h>
#include	<sys/sem.h>
#include	<sys/shm.h>
#include	<sys/fcntl.h>
#include	<sys/utsname.h>
#include	<sys/resource.h>
#include	<sys/conf.h>
#include	<sys/stat.h>
#include	<sys/signal.h>
#include	<sys/priocntl.h>
#include	<sys/procset.h>
#include	<sys/systeminfo.h>
#include	<dirent.h>
#include	<ctype.h>

#include	<libelf.h>

extern char *ctime();
extern char *strcat();
extern char *strcpy();
extern char *strncpy();
extern char *strncat();
extern char *malloc();

extern void sysdef_devinfo();

extern char *optarg;
extern int optind;


#define	SYM_VALUE(sym)	(nl[(sym)].n_value)
#define MEMSEEK(sym)	memseek(sym)
#define MEMREAD(var)	fread((char*)&var, sizeof(var), 1, \
				(incore ? memfile : sysfile))

struct	var	v;
struct  tune	tune;
struct	msginfo	minfo;
struct	seminfo	sinfo;
struct	shminfo	shinfo;

int incore = 1;		/* The default is "incore" */
int bss;		/* if read from file, don't read bss symbols */
int hostidf = 0;        /* 0 == print hostid with other info,
                           1 == print just the hostid */
int devflag = 0;	/* SunOS4.x devinfo compatible output */
char	*os ="/dev/ksyms";	/* Wont always have a /kernel/unix */
				/* This wont fully replace it funtionally */
				/* but is a reasonable default/placeholder */
/*
char	buf[256];			** for ICL port ** 
char	*os =buf;
*/

char	*LS_MODULES      = "ls -R -p -i -1 /kernel";
char	*MODULES_TMPFILE = "/tmp/sysdef.sort.XXXXXX";

char	*mem = "/dev/kmem";
char 	*rlimitnames[] = {
	"cpu time",
	"file size",
	"heap size",
	"stack size",
	"core file size",
	"file descriptors",
	"mapped memory"
};

char	line[256], flag[8], pre[8], pre_addr[20], rtn[20];
int	nsp;
long	strthresh;
int	nstrpush, strmsgsz, strctlsz;
int	aio_size, min_aio_servers, max_aio_servers, aio_server_timeout;
short	naioproc, ts_maxupri;
char 	sys_name[10], intcls[10];
dev_t	root, dump;
char	MAJ[256];
daddr_t	spl;
int	nlsize, lnsize;
FILE	*sysfile, *mastf, *memfile;
DIR	*mast;

void	memseek(), getnlist(), setln();

struct nlist	*nl, *nlptr;
int rootdev, dumpdev, MAJOR, sys3bboot,
	vs, tu, msginfo, seminfoo,
	shminfo, FLckinfo, utsnm, bdev, evcinfo,
	pnstrpush, pstrthresh, pstrmsgsz, pstrctlsz,
	endnm, pnaiosys, pminaios, 
	pmaxaios, paiotimeout, pnaioproc, pts_maxupri, 
	psys_name, pinitclass, prlimits;

#define ADDR	0	/* index for _addr array */
#define OPEN	1	/* index for open routine */
#define CLOSE	2	/* index for close routine */
#define READ	3	/* index for read routine */
#define WRITE	4	/* index for write routine */
#define IOCTL	5	/* index for ioctl routine */
#define STRAT	6	/* index for strategy routine */
#define MMAP	7	/* index for mmap routine */
#define SEGMAP	8	/* index for segmap routine */
#define POLL	9	/* index for poll routine */
#define SIZE	10	/* index for size routine */

#define EQ(x,y)		(strcmp(x, y)==0)  

#define	MAXI	300
#define	MAXL	MAXI/11+10
#define EXPAND	99

struct	link {
	char	*l_cfnm;	/* config name from master table */
	int l_funcidx;		/* index into name list structure */
	unsigned int l_soft :1;	/* software driver flag from master table */
	unsigned int l_dtype:1;	/* set if block device */
	unsigned int l_used :1;	/* set when device entry is printed */
} *ln, *lnptr, *majsrch();

	/* ELF Items */
Elf *elfd = NULL;
Elf32_Ehdr *ehdr = NULL;

main(argc, argv)
	int	argc;
	char	**argv;
{

	struct stat mfbuf;
	char mfname[MAXPATHLEN];
	struct	utsname utsname;
	struct	dirent *dp;
	struct rlimit rlimit;
	Elf_Scn *scn;
	Elf32_Shdr *shdr;
	char *name;
	int ndx;
	int i;
	FILE *lspipe, *srtpipe, *fp;
	char srtbuf[100], *sorted_fname, *mktemp(), *strtok();
        char hostid[256], *end;
        unsigned long hostval;

	while ((i = getopt(argc, argv, "dihm:n:?")) != EOF) {
		switch (i) {
		case 'd':
			devflag++;
			break;
		case 'h':
                        hostidf++;
                        break;
		case 'i':
			incore++;	/* In case "-i and -n" passed */
			break;		/* Not logical, but not disallowed */
		case 'n':
			incore--;	/* Not incore, use specified file */
			os = optarg;
			break;
		default:
			fprintf(stderr,
				"usage: %s [-d -i -h -n namelist]\n",
					argv[0]);
			exit (1);
		}
	}

/*
 * Prints hostid of machine.
 */
        if (sysinfo(SI_HW_SERIAL, hostid, sizeof(hostid)) == -1) {
                fprintf(stderr,"hostid: sysinfo failed\n");
                exit(1);
        }
        hostval = strtoul(hostid, &end, 10);
        if (hostval == 0 && end == hostid) {
                fprintf(stderr, "hostid: hostid string returned by sysinfo \
not numeric: \"%s\"\n", hostid);
                exit(1);
        }
	if (!devflag)
		fprintf(stdout,"*\n* Hostid\n*\n  %08x\n", hostval);

        if (hostidf)
                exit(0);

/* **
	strcpy(buf,"/stand/");
	if (os == buf && sysunicorn(UNICORN_KERNEL,&buf[7]) < 0) {
		fprintf(stderr,"cannot find kernel file\n");
		exit(1);
	}
	else
		printf("kernel file is %s\n",os);
** */
	if((sysfile = fopen(os,"r")) == NULL) {
		fprintf(stderr,"cannot open %s\n",os);
		exit(1);
	}

	if (incore) {
		if((memfile = fopen(mem,"r")) == NULL) {
			fprintf(stderr,"cannot open %s\n",mem);
			exit(1);
		}
	}

	/*
	**	Use libelf to read both COFF and ELF namelists
	*/

        if ((elf_version(EV_CURRENT)) == EV_NONE) {
                fprintf(stderr, "ELF Access Library out of date\n");
		exit (1);
        }

        if ((elfd = elf_begin (fileno(sysfile), ELF_C_READ, NULL)) == NULL) {
                fprintf(stderr, "Unable to elf begin %s (%s)\n",
			os, elf_errmsg(-1));
		exit (1);
        }

	if ((ehdr = elf32_getehdr(elfd)) == NULL) {
		fprintf(stderr, "%s: Can't read Exec header (%s)\n",
			os, elf_errmsg(-1));
		exit (1);
	}

	if ( (((elf_kind(elfd)) != ELF_K_ELF) &&
			((elf_kind(elfd)) != ELF_K_COFF))
			|| (ehdr->e_type != ET_EXEC) )
	{
		fprintf(stderr, "%s: invalid file\n", os);
		elf_end(elfd);
		exit (1);
	}

	/*
	**	If this is a file read, look for .bss section
	*/

	if (!incore) {
		ndx = 1;
		scn = NULL;
		while ((scn = elf_nextscn(elfd, scn)) != NULL) {
			if ((shdr = elf32_getshdr(scn)) == NULL) {
				fprintf(stderr, "%s: Error reading Shdr (%s)\n",
					os, elf_errmsg(-1));
				exit (1);
			}
			name =
			elf_strptr(elfd, ehdr->e_shstrndx, (size_t)shdr->sh_name);
			if ((name) && ((strcmp(name, ".bss")) == 0)) {
				bss = ndx;
			}
			ndx++;
		}
	} /* (!incore) */

	uname(&utsname);
	if (!devflag)
		printf("*\n* %s Configuration\n*\n",utsname.machine);

	nlsize = MAXI;
	lnsize = MAXL;
	nl=(struct nlist *)(calloc(nlsize, sizeof(struct nlist)));
	ln=(struct link *)(calloc(lnsize, sizeof(struct link)));
	nlptr = nl;
	lnptr = ln;

	bdev = setup("bdevsw");
	endnm = setup("");

	getnlist();

	if (!devflag)
		printf("*\n* Devices\n*\n");
	devices();
	if (devflag)
		exit(0);

	printf("*\n* Loadable Objects\n*\n");

	/*
	 * List the loadable objects in the /kernel tree, sorting them
	 * by inode so as to note any hard links.  A temporary file in
	 *  /tmp  is used to store output from sort before listing.
	 */
	if ((lspipe = popen (LS_MODULES, "r")) == (FILE *)NULL) {
			fprintf(stderr,"sysdef: cannot open ls pipe\n");
			exit(1);
	}
	if ((sorted_fname = mktemp (MODULES_TMPFILE)) == (char *)NULL ||
	    !strcmp(sorted_fname, "")) {
		fprintf(stderr,"sysdef: cannot create unique tmp file name\n");
		exit(1);
	}
	sprintf (srtbuf, "sort -y0 - > %s",   sorted_fname);
	if ((srtpipe = popen (srtbuf, "w")) == (FILE *)NULL) {
			fprintf(stderr,"sysdef: cannot open sort pipe\n");
			exit(1);
	}
	{
	  char	line [100],  *tab,  path [100], *pathptr;
	  int   len, inode, ino;

	  pathptr = "";
	  while (fgets (line, 99, lspipe) != (char *)NULL) {
							/* 'line' has <cr>  */
		if (((len = strlen (line)) <= 1)  ||   	/* skip blank lines */
		    ((line [len-2] == '/')))		/* skip dir entries */
			continue;
	
		/* remember path of each subdirectory */

		if ((line [0] == '/')) {		
						
			char *tmp;

			if ((tmp = strchr(&line[1], (int) '/')) != NULL) {
				(void) strcpy (path, tmp);
				if ((pathptr = strtok (&path[1], ":")) == NULL)
					pathptr = &path[1];
				(void) strcat(pathptr, "/");
			}
			continue;
		} else {
			/*
			 * Printing the (inode, path, module) tripple.
			 */
			fprintf (srtpipe, "%s %s",  
				 strtok (line, " "), pathptr);
			fprintf (srtpipe, "%s\n", strtok ((char *)NULL, ".\n"));  
		}
	  }
	  (void) pclose (lspipe);
	  (void) pclose (srtpipe);

	  if ((fp = fopen (sorted_fname, "r")) == (FILE *)NULL) {
			fprintf(stderr, "sysdef: cannot open sorted file: %s",
						sorted_fname);
			exit(1);
	  }
	  inode = -1;
	  while (fgets (line, 99, fp) != (char *)NULL) {

		sscanf (line, "%d %s",  &ino, path);
		if (ino == inode) 
			printf ("\thard link:  ");
		printf ("%s\n", path);
		inode = ino;
	  }
	  (void)fclose (fp);
	  (void)unlink (sorted_fname);
	}

	printf("*\n* System Configuration\n*\n");

	sysdev();

	/* easy stuff */
	printf("*\n* Tunable Parameters\n*\n");
	nlptr = nl;
	vs = setup("v");
	tu = setup("tune");
	utsnm = setup("utsname");
	prlimits = setup("rlimits");
	FLckinfo = setup("flckinfo");
	endnm = msginfo = setup("msginfo");
	pnstrpush = setup("nstrpush");
	pstrthresh = setup("strthresh");
	pstrmsgsz = setup("strmsgsz");
	pstrctlsz = setup("strctlsz");
	seminfoo = setup("seminfo");
	shminfo = setup("shminfo");
	pnaiosys = setup("aio_size");
	pminaios = setup("min_aio_servers");
	pmaxaios = setup("max_aio_servers");
	paiotimeout = setup("aio_server_timeout");
	pnaioproc = setup("naioproc");
	pts_maxupri = setup("ts_maxupri");
	psys_name = setup("sys_name");
	pinitclass = setup("intcls");
	evcinfo = setup("evcinfo");
	setup("");

	getnlist();

	for(nlptr = &nl[vs]; nlptr != &nl[endnm]; nlptr++) {
		if(nlptr->n_value == 0 &&
		    (incore || nlptr->n_scnum != bss)) {
			fprintf(stderr, "namelist error on <%s>\n",
			    nlptr->n_name);
			/* exit(1); */
		}
	}
	if (SYM_VALUE(vs)) {
		MEMSEEK(vs);	
		MEMREAD(v);
	}
	printf("%8d	maximum memory allowed in buffer cache (bufhwm)\n",
		v.v_bufhwm * 1024);
	printf("%8d	maximum number of processes (v.v_proc)\n",v.v_proc);
	printf("%8d	maximum global priority in sys class (MAXCLSYSPRI)\n",
		v.v_maxsyspri);
	printf("%8d	maximum processes per user id (v.v_maxup)\n",v.v_maxup);
	printf("%8d	auto update time limit in seconds (NAUTOUP)\n",
		v.v_autoup);
	if (SYM_VALUE(tu)) {
		MEMSEEK(tu);	
		MEMREAD(tune);
	}
	printf("%8d	page stealing low water mark (GPGSLO)\n", tune.t_gpgslo);
	printf("%8d	fsflush run rate (FSFLUSHR)\n", tune.t_fsflushr);
	printf("%8d	minimum resident memory for avoiding deadlock (MINARMEM)\n",
		tune.t_minarmem);
	printf("%8d	minimum swapable memory for avoiding deadlock (MINASMEM)\n",
		tune.t_minasmem);

	printf("*\n* Utsname Tunables\n*\n");
	if (SYM_VALUE(utsnm)) {
		MEMSEEK(utsnm);
		MEMREAD(utsname);
	}
	printf("%8s  release (REL)\n",utsname.release);
	printf("%8s  node name (NODE)\n",utsname.nodename);
	printf("%8s  system name (SYS)\n",utsname.sysname);
	printf("%8s  version (VER)\n",utsname.version);

	printf("*\n* Process Resource Limit Tunables (Current:Maximum)\n*\n");
	if (SYM_VALUE(prlimits)) {
		MEMSEEK(prlimits);	
	}
	for (i = 0; i < RLIM_NLIMITS; i++) {
		if (SYM_VALUE(prlimits)) {
			MEMREAD(rlimit);
		}
		if (rlimit.rlim_cur == RLIM_INFINITY)
			printf("Infinity:");
		else
			printf("%8x:", rlimit.rlim_cur);
		if (rlimit.rlim_max == RLIM_INFINITY)
			printf("Infinity");
		else
			printf("%8x", rlimit.rlim_max);
		printf("\t%s\n", rlimitnames[i]);
	}

	printf("*\n* Streams Tunables\n*\n");
	if (SYM_VALUE(pnstrpush)) {
		MEMSEEK(pnstrpush);	MEMREAD(nstrpush);
		printf("%6d	maximum number of pushes allowed (NSTRPUSH)\n",
			nstrpush);
	}
	if (SYM_VALUE(pstrthresh)) {
		MEMSEEK(pstrthresh);	MEMREAD(strthresh);
		if (strthresh) {
			printf("%6ld	streams threshold in bytes (STRTHRESH)\n",
				strthresh);
		}
		else {
			printf("%6ld	no streams threshold (STRTHRESH)\n",
				strthresh);
		}
	}
	if (SYM_VALUE(pstrmsgsz)) {
		MEMSEEK(pstrmsgsz);	MEMREAD(strmsgsz);
		printf("%6d	maximum stream message size (STRMSGSZ)\n",
			strmsgsz);
	}
	if (SYM_VALUE(pstrctlsz)) {
		MEMSEEK(pstrctlsz);	MEMREAD(strctlsz);
		printf("%6d	max size of ctl part of message (STRCTLSZ)\n",
			strctlsz);
	}

	if (SYM_VALUE(msginfo))
		{
		MEMSEEK(msginfo);	MEMREAD(minfo);
		printf("*\n* IPC Messages\n*\n");
		printf("%6d	entries in msg map (MSGMAP)\n",minfo.msgmap);
		printf("%6d	max message size (MSGMAX)\n",minfo.msgmax);
		printf("%6d	max bytes on queue (MSGMNB)\n",minfo.msgmnb);
		printf("%6d	message queue identifiers (MSGMNI)\n",minfo.msgmni);
		printf("%6d	message segment size (MSGSSZ)\n",minfo.msgssz);
		printf("%6d	system message headers (MSGTQL)\n",minfo.msgtql);
		printf("%6u	message segments (MSGSEG)\n",minfo.msgseg);
		}

	if (SYM_VALUE(seminfoo))
		{
		MEMSEEK(seminfoo);	MEMREAD(sinfo);
		printf("*\n* IPC Semaphores\n*\n");
		printf("%6d	entries in semaphore map (SEMMAP)\n",sinfo.semmap);
		printf("%6d	semaphore identifiers (SEMMNI)\n",sinfo.semmni);
		printf("%6d	semaphores in system (SEMMNS)\n",sinfo.semmns);
		printf("%6d	undo structures in system (SEMMNU)\n",sinfo.semmnu);
		printf("%6d	max semaphores per id (SEMMSL)\n",sinfo.semmsl);
		printf("%6d	max operations per semop call (SEMOPM)\n",sinfo.semopm);
		printf("%6d	max undo entries per process (SEMUME)\n",sinfo.semume);
		printf("%6d	semaphore maximum value (SEMVMX)\n",sinfo.semvmx);
		printf("%6d	adjust on exit max value (SEMAEM)\n",sinfo.semaem);
		}

	if (SYM_VALUE(shminfo))
		{
		MEMSEEK(shminfo);	MEMREAD(shinfo);
		printf("*\n* IPC Shared Memory\n*\n");
		printf("%6d	max shared memory segment size (SHMMAX)\n",shinfo.shmmax);
		printf("%6d	min shared memory segment size (SHMMIN)\n",shinfo.shmmin);
		printf("%6d	shared memory identifiers (SHMMNI)\n",shinfo.shmmni);
		printf("%6d	max attached shm segments per process (SHMSEG)\n",shinfo.shmseg);
		}

	
	if (SYM_VALUE(pts_maxupri)) {
		printf("*\n* Time Sharing Scheduler Tunables\n*\n");
		MEMSEEK(pts_maxupri);	MEMREAD(ts_maxupri);
		printf("%d	maximum time sharing user priority (TSMAXUPRI)\n", ts_maxupri);
	}

	if (SYM_VALUE(psys_name)) {
		MEMSEEK(psys_name);	MEMREAD(sys_name);
		printf("%s	system class name (SYS_NAME)\n", sys_name);
	}

	if (SYM_VALUE(pinitclass)) {
		MEMSEEK(pinitclass);	MEMREAD(intcls);
		printf("%s	class of init process (INITCLASS)\n", intcls);
	}

	if (SYM_VALUE(pnaiosys)) {
		printf("*\n* Async I/O Tunables\n*\n");
		MEMSEEK(pnaiosys);	MEMREAD(aio_size);
		printf("%6d	outstanding async system calls(NAIOSYS)\n", aio_size);
	}
	if (SYM_VALUE(pminaios)) {
		MEMSEEK(pminaios);	MEMREAD(min_aio_servers);
		printf("%6d	minimum number of servers (MINAIOS)\n", min_aio_servers);
	}
	if (SYM_VALUE(pmaxaios)) {
		MEMSEEK(pmaxaios);	MEMREAD(max_aio_servers);
		printf("%6d	maximum number of servers (MAXAIOS)\n", max_aio_servers);
	}
	if (SYM_VALUE(paiotimeout)) {
		MEMSEEK(paiotimeout);	MEMREAD(aio_server_timeout);
		printf("%6d	number of secs an aio server will wait (AIOTIMEOUT)\n", aio_server_timeout);
	}
	if (SYM_VALUE(pnaioproc)) {
		MEMSEEK(pnaioproc);	MEMREAD(naioproc);
		printf("%6d	number of async requests per process (NAIOPROC)\n", naioproc);
	}

	if (elfd)
		elf_end(elfd);
	exit(0);
}

/*
 * setup - add an entry to a namelist structure array
 */
int
setup(nam)
	char	*nam;
{
	int idx;

	if(nlptr >= &nl[nlsize]) { 
		if ((nl=(struct nlist *)realloc(nl,(nlsize+EXPAND)*(sizeof(struct nlist)))) == NULL) {
			fprintf(stderr, "Namelist space allocation failed\n");
			exit(1);
		}
		nlptr=&nl[nlsize];
		nlsize+=EXPAND;
	}

	nlptr->n_name = malloc((unsigned)(strlen(nam)+1));	/* initialize pointer to next string */
	strcpy(nlptr->n_name,nam);	/* move name into string table */
	nlptr->n_type = 0;
	nlptr->n_value = 0;
	idx = nlptr++ - nl;
	return(idx);
}

/*
 * setln - set up internal link structure for later
 * function look-up.  Records useful information from the
 * /etc/master table description.
 */
void
setln(cf, nidx, block, software)
	char	*cf;
	int nidx;
	int block, software;
{
	if(lnptr >= &ln[lnsize]) {
		lnsize = lnptr - ln;
		if (( ln=(struct link *)realloc(ln, (lnsize+EXPAND)*(sizeof(struct link)))) == NULL ) {
			fprintf(stderr, "Internal Link space allocation failed\n");
			exit(1);
		}
		lnptr = &ln[lnsize];
		lnsize += EXPAND;
	}

	lnptr->l_cfnm = malloc((unsigned)strlen(cf)+2);	/* add space and null */
	if (!lnptr->l_cfnm)
		fprintf(stderr, "Internal Link name space allocation failed\n");
	strcat(strcpy(lnptr->l_cfnm, " "), cf);
	lnptr->l_funcidx = nidx;
	lnptr->l_soft = software;
	lnptr->l_dtype = block;
	lnptr->l_used = 0;
	lnptr++;
}

/*
 * Handle the configured devices
 */
devices()
{
	register struct link *lnkptr;
	register int idx;

	(void)sysdef_devinfo ();

}

sysdev()
{
	register struct link *lptr;
	major_t m;

	/*
	 * ICL port follows
	 *
	printf("  root device\n");
	fflush(stdout);
	if (system("/sbin/root") < 0)
		fprintf(stderr, "unknown root device\n");
	printf("\n");
	 *
	 */
	printf("  swap files\n");
	fflush(stdout);
	if (system("/usr/sbin/swap -l") < 0)
		fprintf(stderr, "unknown swap file(s)\n");
}


/*
 * return true if the flags from /etc/master contain the character "c"
 */
test( flags, c )
	register char *flags;
	char c;
	{
	char t;

	while( t = *flags++ )
		if ( t == c )
			return( 1 );
	return( 0 );
	}


void
memseek(sym)
int sym;
{
	Elf_Scn *scn;
	Elf32_Shdr *eshdr;
	long eoff;

	if (incore) {
		if ((fseek(memfile, nl[sym].n_value, 0)) != 0) {
			fprintf(stderr, "%s: fseek error (in memseek)\n", mem);
			exit (1);
		}
	} else {
		if ((scn = elf_getscn(elfd, nl[sym].n_scnum)) == NULL) {
			fprintf(stderr, "%s: Error reading Scn %d (%s)\n",
				os, nl[sym].n_scnum, elf_errmsg(-1));
			exit (1);
		}

		if ((eshdr = elf32_getshdr(scn)) == NULL) {
			fprintf(stderr, "%s: Error reading Shdr %d (%s)\n",
				os, nl[sym].n_scnum, elf_errmsg(-1));
			exit (1);
		}

		eoff = (long)(nl[sym].n_value - eshdr->sh_addr + eshdr->sh_offset);

		if ((fseek(sysfile, eoff, 0)) != 0) {
			fprintf(stderr, "%s: fseek error (in memseek)\n", os);
			exit (1);
		}
	}
}

/*
**	filter out bss symbols if the reads are from the file
*/
void
getnlist()
{
	register struct nlist *p;

	nlist(os, nl);

		/*
		**	The nlist is done.  If any symbol is a bss
		**	and we are not reading from incore, zero
		**	the n_value field.  (Won't be printed if
		**	n_value == 0.)
		*/
	if (!incore) {
		for (p = nl; p->n_name && p->n_name[0]; p++) {
			if (p->n_scnum == bss) {
				p->n_value = 0;
			}
		}
	}
}
