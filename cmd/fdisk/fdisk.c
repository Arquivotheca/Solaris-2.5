/*
 * Copyright (c) 1992-1994, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#pragma ident	"@(#)fdisk.c	1.33	94/12/19 SMI"

/*
 *	FILE:	fdisk.c 
 *	Description:
 *		This file will read the current Partition table on the
 *		given device and will read the drive parmeters. 
 *		The user can then select various operations from a
 *		supplied menu.
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systeminfo.h>

#include <sys/dktp/fdisk.h>
#include <sys/dkio.h>
#include <sys/vtoc.h>

#define CLR_SCR "[1;1H[0J"
#define CLR_LIN "[0K"
#define HOME "[1;1H[0K[2;1H[0K[3;1H[0K[4;1H[0K[5;1H[0K[6;1H[0K[7;1H[0K[8;1H[0K[9;1H[0K[10;1H[0K[1;1H"
#define Q_LINE "[22;1H[0K[21;1H[0K[20;1H[0K"
#define W_LINE "[12;1H[0K[11;1H[0K"
#define E_LINE "[24;1H[0K[23;1H[0K"
#define M_LINE "[13;1H[0K[14;1H[0K[15;1H[0K[16;1H[0K[17;1H[0K[18;1H[0K[19;1H[0K[13;1H"
#define T_LINE "[1;1H[0K"

char Usage[]= "Usage:\n\
[-o offset] [-s size] [-P fill_patt] [-S geom_file]\n\
[-w|r|d|n|I|B|g|G|R|t|T]\n\
[-F fdisk_file] [-W creat_fdisk_file ] [ -h ]\n\
[-b masterboot]\n\
[-A id:act:bhead:bsect:bcyl:ehead:esect:ecyl:rsect:numsect]\n\
[-D id:act:bhead:bsect:bcyl:ehead:esect:ecyl:rsect:numsect]\n\
rdevice";

char Usage1[]= "\
	   Partition options:\n\
	     -t    Check and adjust vtoc to be consistent with fdisk table;\n\
		   This will truncate vtoc slices that exceed partition size.\n\
	     -T    Check and adjust vtoc to be consistent with fdisk table;\n\
		   This will remove slices that exceed partition size.\n\
	     -I    Forego device checks. This generates a\n\
		   file image of what would go on a disk. Note that\n\
		   you must use -S with this option.\n\
	     -B    Default to one Solaris partition that uses the entire\n\
		   disk.\n\
	     -b    [master_boot] The file to use for master boot.\n\
	     -n    Do not run in interactive mode.\n\
             -R    Open disk read-only.\n\
	     -F    [fdisk_file] use fdisk_file to initialize table.\n\
	     -W    [fdisk_file] create fdisk_file from on-disk table.\n\
	     -A    Add a partition as follows:\n\
		   id:act:bhead:bsect:bcyl:ehead:esect:ecyl:rsect:numsect\n\
			id      = is the system id number from fdisk.h for partition type\n\
			act     = active partition flag (0 or 128).\n\
			bhead   = begin head for start of partition\n\
			bsect   = begin sector for start of partition\n\
			bcyl    = begin cylinder for start of partition\n\
			ehead   = end head for end of partition\n\
			esect   = end sector for end of partition\n\
			ecyl    = end cylinder for end of partition\n\
			rsect   = sector number from start of disk for start of part.\n\
			numsect = size in sectors for partition\n\
	     -D    Delete a partition using same format as -A.\n\
	   Diagnostic options:\n\
	     -r    Read from a disk to stdout (see -o and -s).\n\
	     -w    Write to a disk from stdin (see -o and -s).\n\
	     -P    [fill_patt] Fill disk with pattern fill_patt.\n\
		   fill_patt can be decimal or hex and is used as\n\
		   number for constant long word pattern.\n\
		   If fill_patt is \"#\" then pattern of block #\n\
		   for each block .  Pattern is put in each\n\
		   block as long words and fills each block.\n\
		   (see -o and -s).\n\
	     -o    [Offset] Block offset from start of disk (default 0).\n\
		   Ignored if -P # is given. \n\
	     -s    [Size] Number of blocks to do operation on (see -o).\n\
	     -g    get label geometry and write it to stdout\n\
		    PCYL NCYL ACYL BCYL NHEADS NSECTORS SECTSIZ\n\
			PCYL      = is the number of phys cylinders\n\
			NCYL      = is the number of usable cylinders\n\
			ACYL      = is the number of alternate cylinders\n\
			BCYL      = is the cylinder offset\n\
			NHEADS     = is the number of heads\n\
			NSECTORS   = is the number of sectors per track\n\
			SECTSIZ    = is size (in bytes) of a sector\n\
	     -G    Get physical geometry and write it to stdout (see -g)\n\
	     -S    [geom_file] set the label geometry to the content\n\
		   of geom_file which has the same form as -g output.\n\
	     -d    Turn on debug information about progress.\n\
	     -h    Issue this verbose message.";


char Ostr[] = "Other";
char Dstr[] = "DOS";
char D16str[] = "DOS16";
char DDstr[]= "DOS-DATA";
char EDstr[]= "EXT-DOS";
char DBstr[]= "DOS-BIG";
char PCstr[]= "PCIX";
char Ustr[] = "UNIX System";
char SUstr[] = "SOLARIS System";
char Actvstr[] = "Active";
char NAstr[]   = "      ";

/**************** All the user opts and flags ************************/
char *Dfltdev;			/* fixed disk drive */

/* diag flags */
int	io_wrt;			/* write to disk from stdin */
int	io_rd;			/* read disk to stdout */
char	*io_fatt;		/* user supplied pattern */
int	io_patt;		/* write a pattern to disk */
int	io_lgeom;		/* get label geometry */
int	io_pgeom;		/* get drive phys geometry */
char	*io_sgeom = 0;		/* set label geometry */
int	io_readonly;		/* do not write to disk */

int	io_offset;		/* leek to offset sector for opt */
int	io_size;		/* size in sectors of opt */

/* partition table flags */
int	io_fdisk;		/* do fdisk opt */
int	io_ifdisk;		/* interactive partition  */
int	io_nifdisk;		/* interactive partition  */

int	io_adjt;		/* check and adjt the vtoc table */
int	io_ADJT;		/* check and adjt the vtoc table (more agressive*/
char	*io_ffdisk = 0;		/* name of file for input partition file */
char	*io_Wfdisk = 0;		/* name of output fdisk file */
char	*io_Afdisk = 0;		/* entry to add to partition table */
char	*io_Dfdisk = 0;		/* entry to delete to partition table */

char	*io_mboot = 0;		/* master boot record */

struct mboot BootCod;		/* buffer for master boot record */

int	io_wholedisk;		/* use whole disk for Solaris part. */
int     io_debug;		/* do everything in verbose mode */
int     io_image;		/* do not do device checking for image creat*/

struct mboot *Bootblk;		/* ptr to cut and past sector zero */
char	*Bootsect;		/* ptr to sector zero buffer */
char	*Nullsect;
struct vtoc     disk_vtoc;	/* used to verify vtoc table */
int	vt_inval;

struct ipart	Table[FD_NUMPART];
struct ipart Old_Table[FD_NUMPART];

/* disk geometry info */
struct dk_geom	disk_geom;

int Dev;			/* fd for open device */
/* phys geometry */
int Numcyl;			/* number of cylinders the drive has */
int heads;			/* number of heads this drive has */
int sectors;			/* number of sectors per track */

/* HBA (virtual) geometry */
int hba_Numcyl;			/* number of cylinders the drive has */
int hba_heads;			/* number of heads this drive has */
int hba_sectors;			/* number of sectors per track */

int sectsiz;			/* sector size for this drive */
int drtype;			/* Type of drive this is (ie scsi,floppy...) */

/* load functions */
#define	LOADFILE	0	/* load function to load fdisk from file */
#define	LOADDEL		1	/* load function to delete a fdisk entry */
#define	LOADADD		2	/* load function to add a fdisk entry */

#define CBUFLEN 80
char s[CBUFLEN];


main(argc,argv)
int argc;
char *argv[];
{
int c, i, j;
int unixstart;
int unixend;
extern	int optind;
extern	char *optarg;
int	errflg = 0;
int	diag_cnt = 0;
struct	stat	statbuf;
int openmode;

	setbuf (stderr, 0);	/* so all output gets out on exit */
	setbuf (stdout, 0);
/*
 * Process the options.
 */
while( (c=getopt(argc,argv,"o:s:P:F:b:A:D:W:S:tTIhwrndgGRB")) != EOF ) {
	switch(c) {

	case 'o':
		io_offset = strtoul(optarg, 0, 0);
		continue;
	case 's':
		io_size = strtoul(optarg, 0, 0);
		continue;
	case 'P':
		diag_cnt++;
		io_patt++;
		io_fatt = optarg;
		continue;
	case 'w':
		diag_cnt++;
		io_wrt++;
		continue;
	case 'r':
		diag_cnt++;
		io_rd++;
		continue;
	case 'd':
		io_debug++;
		continue;
	case 'I':
		io_image++;
		continue;
	case 'R':
		io_readonly++;
		continue;
	case 'S':
		diag_cnt++;
		io_sgeom = optarg;
		continue;
	case 'T':
		io_ADJT++;
	case 't':
		io_adjt++;
		continue;
	case 'B':
		io_wholedisk++;
		io_fdisk++;
		continue;
	case 'g':
		diag_cnt++;
		io_lgeom++;
		continue;
	case 'G':
		diag_cnt++;
		io_pgeom++;
		continue;
#ifdef XXX_BH
	case 'i':
		io_ifdisk++;
		io_fdisk++;
		continue;
#endif
	case 'n':
		io_nifdisk++;
		io_fdisk++;
		continue;
	case 'F':
		io_fdisk++;
		io_ffdisk = optarg;
		continue;
	case 'b':
		io_mboot = optarg;
		continue;
	case 'W':
		io_Wfdisk = optarg;
		io_fdisk++;
		continue;
	case 'A':
		io_fdisk++;
		io_Afdisk = optarg;
		continue;
	case 'D':
		io_fdisk++;
		io_Dfdisk = optarg;
		continue;
	case 'h':
		fprintf(stderr,"%s\n",Usage);
		fprintf(stderr,"%s\n",Usage1);
		exit (0);
	case '?':
		errflg++;
		break;
	}
	break;
}

if (io_image && io_sgeom && diag_cnt == 1) {
	diag_cnt = 0;
}
/*********** user option checking ************/
/* Make the default option interractive */
if (!io_fdisk && !diag_cnt && !io_nifdisk) {
	io_ifdisk++;
	io_fdisk++;
}
if (((io_fdisk || io_adjt) && diag_cnt) || (diag_cnt > 1)) {
	errflg++;
}

/* was there any error */
if (errflg || argc == optind) {
	fprintf(stderr,"%s\n",Usage);
	fprintf(stderr,"\nUse -h to get more detailed help information\n");
	exit(2);
}


/******* make sure this is a raw device *******/
if (!io_image) {
	if (stat(argv[optind], (struct stat *) &statbuf) == -1) {
		fprintf(stderr, "fdisk:  Cannot stat device %s\n", argv[optind]);
		exit(1);
	}
															       
	if ((statbuf.st_mode & S_IFMT) != S_IFCHR) {
		fprintf(stderr, "fdisk:  %s must be a raw device.\n", argv[optind]);
		exit(1);
	}
}


/******* get name of special file to setup and try to open it ********/
Dfltdev = argv[optind];
if (io_readonly)
	openmode = O_RDONLY;
else
	openmode = O_RDWR|O_CREAT;

if ( (Dev = open(Dfltdev, openmode, 0666)) == -1){
	fprintf(stderr,"fdisk: Device (%s) cannot be opened\n",Dfltdev);
	exit(1);
}	



/********* get the disk geometry *********/
if (!io_image) {
	/*
	 * get disk's HBA (virtual) geometry
	 */
		if (ioctl(Dev, DKIOCG_VIRTGEOM, &disk_geom)) {
			(void) fprintf(stderr, "%s: Cannot get virtual (HBA) disk geometry\n",
				       argv[optind]);
			exit(1);
		}
		hba_Numcyl  = disk_geom.dkg_ncyl;
		hba_heads  = disk_geom.dkg_nhead;
		hba_sectors = disk_geom.dkg_nsect;

		if (ioctl(Dev, DKIOCG_PHYGEOM, &disk_geom)) {
			(void) fprintf(stderr, "%s: Cannot get physical disk geometry\n",
				       argv[optind]);
			exit(1);
		}
		Numcyl  = disk_geom.dkg_ncyl;
		heads  = disk_geom.dkg_nhead;
		sectors = disk_geom.dkg_nsect;
		sectsiz = 512;
		if (io_debug) {
			fprintf(stderr,"fdisk physical geometry:\n");
			fprintf(stderr,"cylinders[%d] heads[%d] sectors[%d] sector size[%d] blocks[%d] mbytes[%d]\n",
				Numcyl,
				heads,
				sectors,
				sectsiz,
				Numcyl*heads*sectors,
				(Numcyl*heads*sectors*sectsiz)/1048576 ) ;
			fprintf(stderr,"fdisk virtual (HBA) geometry:\n");
			fprintf(stderr,"cylinders[%d] heads[%d] sectors[%d] sector size[%d] blocks[%d] mbytes[%d]\n",
				hba_Numcyl,
				hba_heads,
				hba_sectors,
				sectsiz,
				hba_Numcyl*hba_heads*hba_sectors,
				(hba_Numcyl*hba_heads*hba_sectors*sectsiz)/1048576 ) ;
		}
}



/************** if this is a geometry report just do it and exit *************/
	if (io_lgeom) {
	if (ioctl(Dev, DKIOCGGEOM, &disk_geom)) {
		(void) fprintf(stderr, "%s: Cannot get disk label geometry\n",
			       argv[optind]);
		exit(1);
	}
        Numcyl  = disk_geom.dkg_ncyl;
	heads  = disk_geom.dkg_nhead;
	sectors = disk_geom.dkg_nsect;
	sectsiz = 512;
		printf("* Label geometry for device %s\n",Dfltdev);
		printf("* PCYL     NCYL     ACYL     BCYL     NHEAD NSECT SECSIZ\n");
		printf("  %-8d %-8d %-8d %-8d %-5d %-5d %-6d\n",
		       Numcyl,
		       disk_geom.dkg_ncyl,
		       disk_geom.dkg_acyl,
		       disk_geom.dkg_bcyl,
		       heads,
		       sectors,
		       sectsiz
		       );
		exit (0);
		
	} else if (io_pgeom) {
		if (ioctl(Dev, DKIOCG_PHYGEOM, &disk_geom)) {
			(void) fprintf(stderr, "%s: Cannot get physical disk geometry\n",
				       argv[optind]);
			exit(1);
		}
		printf("* Physical geometry for device %s\n",Dfltdev);
		printf("* PCYL     NCYL     ACYL     BCYL     NHEAD NSECT SECSIZ\n");
		printf("  %-8d %-8d %-8d %-8d %-5d %-5d %-6d\n",
		       disk_geom.dkg_pcyl,
		       disk_geom.dkg_ncyl,
		       disk_geom.dkg_acyl,
		       disk_geom.dkg_bcyl,
		       disk_geom.dkg_nhead,
		       disk_geom.dkg_nsect,
		       sectsiz
		       );
		exit (0);
	} else if (io_sgeom) {
		if (read_geom(io_sgeom)) {
			exit (1);
		} else if (!io_image) {
			exit (0);
		}
	}
	
	
	
/********* Allocate memory to hold 3 complete sectors: *********/
	Bootsect = (char *) malloc (3 * sectsiz);
	if (Bootsect == NULL) {
		fprintf (stderr,"fdisk: Unable to obtain %d bytes of buffer memory.\n",
			3*sectsiz);
		exit(1);
	}

	Nullsect = Bootsect + sectsiz;
	/* Zero out the "NULL" sector */
	for (i=0; i < sectsiz; i++) {
		Nullsect[i] = 0;
	}

/******** find out what the user wants from us *********/
	if (io_rd) {		/* ads disk read */
		abs_read();	/* will not return */
	}else if (io_wrt && !io_readonly) {
		abs_write();	/* will not return */
	} else if (io_patt && !io_readonly) {
		fill_patt();	/* will not return */
	} 


/*****************************************************************
 *****************************************************************
 * This is an fdisk edit  (THE REAL RES. FOR THIS PROGRAM)
 *****************************************************************
 *****************************************************************/

/***** get our new BOOT program incase we write an new disk table *****/
	mboot_read();		/* will exit on an error */


/***** read the dev master boot record and fdisk table ******/

	/* read from disk master boot */
	dev_mboot_read();	/* will exit on error */
	
	/* verify and copy the dev fdisk table */
	/* this will be our prototype mboot if the dev mboot looks */
	/* invalid. */
	Bootblk = (struct mboot *) Bootsect;
	copytbl();

/**** if the user specified use of a fdisk table from a file then load it ****/
	if (io_ffdisk) {
		load(LOADFILE,io_ffdisk);		/* load and verify user spec table parms */
	} 	

/***** did user want to delete or add an entry *****/
	if (io_Dfdisk) {
		load(LOADDEL,io_Dfdisk);
	}
	if (io_Afdisk) {
		load(LOADADD,io_Afdisk);
	}
	
/********** check if there is no fdisk table ************/
	if ( Table[0].systid == UNUSED || io_wholedisk) {
		if (io_ifdisk) {
			printf("The recommended default partitioning for your disk is:\n\n");
			printf("  a 100%% \"SOLARIS System\" partition. \n\n");
		}


                /******** ask the user if they want the default/use 
						  if wholedisk spec ********/
		if (!io_Afdisk && !io_wholedisk && io_ifdisk) { 
			printf("To select this, please type \"y\".  To partition your disk\n");
			printf("differently, type \"n\" and the \"fdisk\" program will let you\n");
			printf("select other partitions. ");
			gets(s);
			rm_blanks(s);
			while ( !( ((s[0] == 'y') || (s[0] == 'Y') ||
				    (s[0] == 'n') || (s[0] == 'N')) &&
				  (s[1] == 0))) {
				printf(" Please answer \"y\" or \"n\": ");
				gets(s);
				rm_blanks(s);
			}
		}
		
                /****** do what the user specified ******/
		if ( s[0] == 'y' || s[0] == 'Y' || io_wholedisk) {
			/* Default scenario ! */
	        	nulltbl();
	        	/* now set up UNIX System partition */
	        	Table[0].bootid = ACTIVE;
			Table[0].begsect = 1;
#if XXXTHE_OLD_WAY
	        	unixend = Numcyl - 1;
			unixstart = 2;
			Table[0].relsect = unixstart * heads * sectors;
			Table[0].numsect = (long)((Numcyl-2) * heads * sectors);
	        	Table[0].systid = UNIXOS;   /* UNIX */
#else
			unixstart = heads * sectors;
	        	unixend = Numcyl - 1;
			Table[0].relsect = heads * sectors;
			Table[0].numsect = (long)((Numcyl-1) * heads * sectors);
	        	Table[0].systid = SUNIXOS;   /* UNIX */
#endif
	        	Table[0].beghead = 0;
	        	Table[0].begcyl = (char)(unixstart & 0x00ff);
			Table[0].begsect |= (char)((unixstart >> 2) & 0x0c0);
	        	Table[0].endhead = hba_heads - 1;
	        	Table[0].endsect = (hba_sectors & 0x3f) | (char)((unixend >> 2) & 0x00c0);
	        	Table[0].endcyl = (char)(unixend & 0x00ff);


/****** copy our new table back to the sector buff and write to disk ******/
	        	cpybtbl();
			if (io_debug) {
				fprintf(stderr,"About to write fdisk table:\n");
				prtfdisk();
				if (io_ifdisk) {
					fprintf(stderr,"\n");
					fprintf(stderr,"Press <ENTER> to continue.....\n");
					gets(s);
				}
			}
			dev_mboot_write(0, Bootsect, sectsiz);

/****** If the vtoc table is wrong fix it ******/
			if (io_adjt)
				fix_slice();
	        	exit(0);
	    	}
	}


/***** display fdisk table for debug *****/
	if (io_debug) {
		fprintf(stderr,"fdisk table on entry:\n");
		prtfdisk();
		if (io_ifdisk) {
			fprintf(stderr,"\n");
			fprintf(stderr,"Press <ENTER> to continue.....\n");
			gets(s);
		}
	}

/***** save a copy before we change it so we can tell it changed on exit *****/
	if (!io_ffdisk && !io_Afdisk && !io_Dfdisk) { /* But these opts have */
						      /* already chnged it */
		cpyoldtbl();
	}

	/* this is interactive fdisk mode*/
	if (io_ifdisk) {
		printf(CLR_SCR);
		disptbl();
		while (1) {
			stage0(argv[1]);
			copytbl();
			disptbl();
		}
	}
#ifdef XXX_BSH
	if (chk_ptable() == 1 || io_mboot)
#else
	chk_ptable();
	if (io_readonly == 0)
#endif
		dev_mboot_write(0, Bootsect, sectsiz);

/****** If the vtoc table is wrong fix it ******/
	if (io_adjt)
		fix_slice();
	exit(0);
}

/****************************************/
/* read geom from file                  */
/****************************************/
read_geom(sgeom)
char	*sgeom;
{
		char	line[256];
		FILE *fp;
		/* open the prototype file */
		if ((fp = fopen(sgeom, "r")) == NULL) {
			(void) fprintf(stderr, "fdisk: Cannot open file %s\n",
				       io_sgeom);
			return(1);
		}

		/* read a line from the file */
		while (fgets(line, sizeof (line) - 1, fp)) {
			if (line[0] == '\0' || line[0] == '\n' || line[0] == '*')
			continue;
			else {
				line[strlen(line)] = '\0';
				if (sscanf(line, "%d %d %d %d %d %d %d",
					   &disk_geom.dkg_pcyl, 
					   &disk_geom.dkg_ncyl, 
					   &disk_geom.dkg_acyl, 
					   &disk_geom.dkg_bcyl, 
					   &disk_geom.dkg_nhead, 
					   &disk_geom.dkg_nsect, 
					   &sectsiz
					   ) != 7) {
					(void) fprintf(stderr, "Syntax error: \"%s\"\n",
						       line);
					return(1);
				}
				break;
			} /* else */
		} /*while (fgets(line, sizeof (line) - 1, fp)) {*/
		if (!io_image) {
			if (ioctl(Dev, DKIOCSGEOM, &disk_geom)) {
				(void) fprintf(stderr, "Cannot set label geometry\n");
				return(1);
			}
		}else {
			Numcyl = hba_Numcyl = disk_geom.dkg_ncyl;
			heads = hba_heads = disk_geom.dkg_nhead;
			sectors = hba_sectors = disk_geom.dkg_nsect;
		}
		
		fclose (fp);
		return(0);
}



/************************************************/
/* read the master boot sector from device      */
/************************************************/
dev_mboot_read()
{
	if (lseek (Dev, 0, 0) == -1) {
		fprintf(stderr,"fdisk: Error seeking to partition table on (%s)\n",
			Dfltdev );
		if (!io_image)
			exit (1);
	}
	if (read (Dev, Bootsect, sectsiz) != sectsiz) {
		fprintf(stderr,"fdisk: Error reading partition table from (%s)\n",
			Dfltdev );
		if (!io_image)
			exit (1);
	}
}



/************************************************/
/* write the master boot sector to device       */
/************************************************/
dev_mboot_write(int sect, char *buff, int bootsiz)
{
	if (io_readonly) {
		return (0);
	}
	/* write to disk drive */
	if (lseek (Dev, sect, 0) == -1) {
		fprintf(stderr,"fdisk: Error seeking to master boot record on (%s)\n",
			Dfltdev );
		exit (1);
	}
	if (write (Dev, buff, bootsiz) != bootsiz) {
		fprintf(stderr,"fdisk: Error writing master boot record on (%s)\n",
			Dfltdev );
		exit (1);
	}
}



/***********************************************/
/* read the prototype  boot records from files */
/***********************************************/
mboot_read()
{
	int mDev,i;
	struct	stat	statbuf;
	struct ipart *part;

	/*
	 * If the master boot file hasn't been specified, use the
	 * implementation architecture name to generate the default
	 * one.
	 */
	if (io_mboot == (char *)0) {
		static char mpath[MAXPATHLEN];
		char platname[MAXNAMELEN];

		if (sysinfo(SI_PLATFORM, platname, sizeof (platname)) == -1) {
			(void) fprintf(stderr,
			    "fdisk: cannot determine platform name\n");
			exit(1);
		}
		(void) sprintf(mpath,
			"/usr/platform/%s/lib/fs/ufs/mboot", platname);
		io_mboot = mpath;
	}
		
/****** First read in the master boot record ******/
	/* open the master boot proto file */
	if ((mDev = open(io_mboot, O_RDONLY, 0666)) == -1) {
		fprintf(stderr,"fdisk: Master boot file (%s) cannot be opened\n",io_mboot);
		exit(1);
	}

	/* read the master boot program */
	if (read (mDev, &BootCod, sizeof (struct mboot)) != sizeof
	    (struct mboot)) {
		fprintf(stderr,"fdisk: Master boot file (%s) cannot be read\n",io_mboot);
		exit (1);
	}

	/* is this really a master boot record */
	if (BootCod.signature != MBB_MAGIC)  {
		fprintf(stderr,"fdisk: Master boot file (%s) is not valid\n",io_mboot);
		fprintf(stderr,"       Bad magic number (%x) should be (%x)\n",BootCod.signature,MBB_MAGIC);
		exit (1);
	}

	close (mDev);

	/* zero out the partitions part of this record */
	part = (struct ipart *)BootCod.parts;
	for (i=0;i<FD_NUMPART;i++,part++) {
		memset (part, 0, sizeof (struct ipart));
	}

}



/*************************************************/
/* fill the disk with user/sector number pattern.*/
/*************************************************/
fill_patt()
{
	int	*buff_ptr,i,c;
	int	io_fpatt = 0;
	int	io_ipatt = 0;

	if (strncmp(io_fatt, "#", 1) != 0){
		io_fpatt++;
		io_ipatt = strtoul(io_fatt, 0, 0);
		buff_ptr = (int *)Bootsect;
		for (i=0; i<sectsiz; i += 4, buff_ptr++)
		     *buff_ptr = io_ipatt;
	}
		
	/*
	 * Fill disk with parttern based on block number
	 * Write to the disk at abs relative block io_offset
	 * for io_size blocks.
	 */
	while (io_size--) {
		buff_ptr = (int *)Bootsect;
		if (!io_fpatt) {
			for (i=0; i<sectsiz; i += 4, buff_ptr++)
			     *buff_ptr = io_offset;
		}
		/* write the data to disk */
		if (lseek (Dev, sectsiz * io_offset++, 0) == -1) {
			fprintf(stderr,"fdisk: Error seeking on (%s)\n",
				Dfltdev );
			exit (1);
		}
		if (write (Dev, Bootsect, sectsiz) != sectsiz) {
			fprintf(stderr,"fdisk: Error writing (%s)\n",
				Dfltdev );
			exit (1);
		}
	} /*while (--io_size);*/
}


/********************************************************/
/*
 * read from the disk at abs relative block io_offset
 * for io_size blocks.
 * Write the data to stdout (this is user spec. -r opt)
 */
/********************************************************/
abs_read() {
	int c;
	
	while (io_size--) {
		if (lseek (Dev, sectsiz * io_offset++, 0) == -1) {
			fprintf(stderr,"fdisk: Error seeking on (%s)\n",
				Dfltdev );
			exit (1);
		}
		if (read (Dev, Bootsect, sectsiz) != sectsiz) {
			fprintf(stderr,"fdisk: Error reading (%s)\n",
				Dfltdev );
			exit (1);
		}

		/* write to stdout */
		if ((c = write(1,Bootsect,(unsigned)sectsiz)) != sectsiz)
		{
			if ( c>= 0) {
				if (io_debug)
				fprintf(stderr, 
					"fdisk: Output warning (%d of %d characters written)\n", 
					   c, sectsiz);
				exit(2);
			} else {
				perror("write error on output file");
				exit (2)   ;
			}
		} /*if ((c = write(1,Bootsect,(unsigned)sectsiz)) != sectsiz)*/
	} /*while (--io_size);*/
	exit (0);
}



/*******************************************************/
/* Read the data from stdin
 * Write to the disk at abs relative block io_offset
 * for io_size blocks. (user spec -w opt)
 */
/*******************************************************/
abs_write()
{
	int c,i;
	
	while (io_size--) {
		int part_exit = 0;
		/* read from stdin */
		if ((c = read(0,Bootsect,(unsigned)sectsiz)) != sectsiz) {
			if ( c>= 0) {
				if (io_debug)
				fprintf(stderr, 
					   "fdisk: WARNING: Incomplete read (%d of %d characters read) on input file\n", 
					c, sectsiz);
				/* fill pattern to mark partial sector in buf */
				for (i = c; i <sectsiz;)
				{
					Bootsect[i++] = 0x41;
					Bootsect[i++] = 0x62;
					Bootsect[i++] = 0x65;
					Bootsect[i++] = 0;
				}
				part_exit++;
			} else {
				perror("read error on input file");
				exit (2);
			}
			   
		}
		/* write to disk drive */
		if (lseek (Dev, sectsiz * io_offset++, 0) == -1) {
			fprintf(stderr,"fdisk: Error seeking on (%s)\n",
				Dfltdev );
			exit (1);
		}
		if (write (Dev, Bootsect, sectsiz) != sectsiz) {
			fprintf(stderr,"fdisk: Error writing (%s)\n",
				Dfltdev );
			exit (1);
		}
		if (part_exit)
		exit(0);
	} /*while (--io_size);*/
	exit (1);
}




/***************************************************************************/
/* load will either read the fdisk table from a file or add or delete an   */
/* entry                                                                   */
/***************************************************************************/
load(funct,file)
int	funct;
char	*file;			/* This is either file name or del/inst line */
{
	int	id;
	int	act;
	int	bhead;
	int	bsect;
	int	bcyl;
	int	ehead;
	int	esect;
	int	ecyl;
	int	rsect;
	int	numsect;
	char	line[256];
	int	i = 0;
	int	j;
	FILE *fp;

	switch (funct) {

	      case LOADFILE:
		/* zero out the table before we load it */
		/* (This will force it to be updated on disk latter) */
		nulltbl();

		/* open the prototype file */
		if ((fp = fopen(file, "r")) == NULL) {
			(void) fprintf(stderr, "fdisk: Cannot open prototype partition file %s\n",
				       file);
			exit(1);
		}
		
		/* read a line from the file */
		while (fgets(line, sizeof (line) - 1, fp)) {
			if (pars_fdisk(line,  &id, &act, &bhead, &bsect, &bcyl, &ehead,
				       &esect, &ecyl, &rsect, &numsect)) {
				continue;
			}
			
			/* find an unused entry for use to use and put the 
			   entry in table */
			if (insert_tbl(id, act, bhead, bsect, bcyl, ehead, esect,
				       ecyl, rsect, numsect) < 0) {
				(void) fprintf(stderr, "fdisk: Error on entry \"%s\"\n",
					       line);
				exit(1);
			}
		}/* while (fgets(line, sizeof (line) - 1, fp)) { */
		
		if (verify_tbl()) {
			fprintf(stderr,"fdisk: Partition cannot be created because entries overlap or exceed disk capacity.\n");
			exit (1);
		}

		fclose (fp);
		return;

	      case LOADDEL:

	       /* pars the user line */
		pars_fdisk(file,  &id, &act, &bhead, &bsect, &bcyl, &ehead,
			   &esect, &ecyl, &rsect, &numsect);

		/* look for the exact entry in the table */
		for (i=0;i<FD_NUMPART;i++) {
			if (Table[i].systid == id &&
			    Table[i].bootid == act &&
			    Table[i].beghead == bhead &&

			    Table[i].begsect == ((bsect & 0x3f) | (unsigned char)((bcyl>>2) & 0xc0)) &&
			    Table[i].begcyl == (unsigned char)(bcyl & 0xff) &&
			    Table[i].endhead == ehead &&
			    Table[i].endsect == ((esect & 0x3f) | (unsigned char)((ecyl>>2) & 0xc0)) &&
			    Table[i].endcyl == (unsigned char)(ecyl & 0xff) &&
			    Table[i].relsect == rsect &&
			    Table[i].numsect == numsect ) {

				/* we found the entry now everything after it up*/
				for (j=i; j<FD_NUMPART-1; j++) {
					Table[j].systid = Table[j+1].systid;
					Table[j].bootid = Table[j+1].bootid;
					Table[j].beghead = Table[j+1].beghead;
					Table[j].begsect = Table[j+1].begsect;
					Table[j].begcyl = Table[j+1].begcyl;
					Table[j].endhead = Table[j+1].endhead;
					Table[j].endsect = Table[j+1].endsect;
					Table[j].endcyl = Table[j+1].endcyl;
					Table[j].relsect = Table[j+1].relsect;
					Table[j].numsect = Table[j+1].numsect;
				}

				/* mark the last entry as unused incase all */
				/* table entries where inuse before the delet */
				Table[FD_NUMPART-1].systid = UNUSED;
				Table[FD_NUMPART-1].bootid = 0;
				return;
			}
		}
		fprintf(stderr,"fdisk: Entry \"%s\" does not match any existing partition\n",file );
		exit (1);


	      case LOADADD:
		pars_fdisk(file,  &id, &act, &bhead, &bsect, &bcyl, &ehead,
			   &esect, &ecyl, &rsect, &numsect);

		/* find an unused entry for use to use and put the 
		   entry in table */
		if (insert_tbl(id, act, bhead, bsect, bcyl, ehead, esect,
			       ecyl, rsect, numsect) < 0) {
			(void) fprintf(stderr, "fdisk: Invalid entry \"%s\"could not be inserted.\n",
				       file);
			exit(1);
		}

		/* make sure new entry does not overlap old entry */
		if (verify_tbl()) {
			fprintf(stderr,"fdisk: Partition \"%s\" cannot be created because it overlaps an existing partition, or is too big.\n",file);
			exit (1);
		}
	} /*switch funct*/
}



/*****************************************************/
/* insert an entry in fdisk table                    */
/* this will check all the user supplied values for  */
/* the entry but not the validity rel to other table */
/* entries!                                          */
/*****************************************************/
insert_tbl(id, act, bhead, bsect, bcyl, ehead, esect, ecyl, rsect, numsect)
int	id,act,bhead,bsect,bcyl,ehead,esect,ecyl,rsect,numsect;
{
	int i,k;

	/* Check for a full table */
	if (Table[3].systid != UNUSED) {
		fprintf(stderr,"fdisk: Partition table is full.\n");
		return (-1);
	}

/***** Do error checking of all those values *****/
	/* Note that a user supplied value of zero means do not check the */
	/* user value since user is not suppling one (Ie we should gen the */
	/* value ) This is a hack that will not check zero but */

	/* check value against drive size */
	if (rsect+numsect > (Numcyl * heads * sectors)) {
		fprintf(stderr,"fdisk: Partition table exceeds size of drive.\n");
		return (-1);
	}

	/*** verify that head,cyl,sect agree with relsect... ***/
	if (bcyl) {
		if (bcyl != (rsect/(hba_sectors*hba_heads) & 0x3ff)) {
			fprintf(stderr,"fdisk: Invalid beginning cylinder number.\n");
			return (-1);
		}
	}else
	        bcyl = rsect/(hba_sectors*hba_heads);
	

	if (bhead) {
		if (bhead != ((rsect-(bcyl*hba_heads*hba_sectors))/hba_sectors)) {
			fprintf(stderr,"fdisk: Invalid beginning head number.\n");
			return (-1);
		}
	}else
	        bhead = ((rsect-(bcyl*hba_heads*hba_sectors))/hba_sectors);

							     
	if (bsect) {
		if (bsect != (((rsect%hba_sectors)+1 ) & 0x3f)) {
			fprintf(stderr,"fdisk: Invalid beginning sector number.\n");
			return (-1);
		}
	}else
	        bsect = ((rsect%hba_sectors)+1 );

	
	if (ecyl) {
		if (ecyl != ((((rsect+numsect)-1)/(hba_sectors*hba_heads)) & 0x3ff)) {
			fprintf(stderr,"fdisk: Invalid ending cylinder number.\n");
			return (-1);
		}
	}else
	        ecyl = ((rsect+numsect)-1)/(hba_sectors*hba_heads);

	
	if (ehead) {
		if (ehead != (((rsect+numsect)-1)-(ecyl*hba_heads*hba_sectors))/hba_sectors) {
			fprintf(stderr,"fdisk: Invalid ending head number.\n");
			return (-1);
		}	
	}else
	        ehead = ((((rsect+numsect)-1)-(ecyl*hba_heads*hba_sectors))/hba_sectors);


	if (esect) {
		if (esect != (((((rsect+numsect)-1)%hba_sectors)+1 ) & 0x3f)) {
			fprintf(stderr,"fdisk: Invalid ending sector number.\n");
			return (-1);
		}
	}else
	        esect = (((rsect+numsect)-1)%hba_sectors)+1;


/***** Put our valid entry in the table *****/
	for (i=0; i< FD_NUMPART; i++) {
		if (Table[i].systid == UNUSED) { /* unused entries at end */
			Table[i].systid = id;
			Table[i].begsect = ((bsect & 0x3f) | (unsigned char)((bcyl>>2) & 0xc0));
			Table[i].endsect = ((esect & 0x3f) | (unsigned char)((ecyl>>2) & 0xc0));
			Table[i].begcyl = (unsigned char)(bcyl & 0xff);
			Table[i].endcyl = (unsigned char)(ecyl & 0xff);
			Table[i].beghead = bhead;
			Table[i].endhead = ehead;
			Table[i].numsect = numsect;
			Table[i].relsect = rsect;
			Table[i].bootid = act;
			return (i);
		}
		else
			continue;
	}
}




/*****************************************************/
/* verify that there are no overlaping table entries */
/* and that no entries exceeds disk size             */
/*****************************************************/
verify_tbl()
{
	int i,j,rsect,numsect;
	
	/* make sure new entry does not overlap old entry */
	for (i=0; i<FD_NUMPART-1; i++) {
		if (Table[i].systid != UNUSED) {
			rsect = Table[i].relsect;
			numsect = Table[i].numsect;
			if ((rsect + numsect) > (Numcyl * heads * sectors))
			{
				return (-1);
			}
			for (j=i+1; j<FD_NUMPART; j++) {
				if (Table[j].systid != UNUSED) {
					int t_relsect = Table[j].relsect;
					int t_numsect = Table[j].numsect;
					if ( (rsect >= (t_relsect + t_numsect)) ||
					    ((rsect+numsect) <= t_relsect) ) { 
						continue;
					} else {
						return (-1);
					}
				}
			}
		}
	}
	if (Table[i].systid != UNUSED) {
		if ( (Table[i].relsect + Table[i].numsect) >  
		                  (Numcyl * heads * sectors) )
		{
			return (-1);
		}
	}
	return (0);
}

		

/********************************/
/* parse user line for fdisk    */
/********************************/
pars_fdisk(line,id,act,bhead,bsect,bcyl,ehead,esect,ecyl,rsect,numsect)
char	*line;
char    *id,*act,*bhead,*bsect,*bcyl,*ehead,*esect,*ecyl,*rsect,*numsect;
{
	int	i;
	if (line[0] == '\0' || line[0] == '\n' || line[0] == '*')
	    return (1);
	line[strlen(line)] = '\0';
	for (i=0;i<strlen(line);i++) {
		if (line[i]=='\0') {
			break;
		} else if (line[i]==':') {
			line[i] = ' ';
		}
	}
	if (sscanf(line, "%d %d %d %d %d %d %d %d %ld %ld",
		   id, act, bhead, bsect, bcyl, ehead, esect, ecyl,
		   rsect, numsect) != 10) {
		(void) fprintf(stderr, "Syntax error: \"%s\"\n",
			       line);
		exit(1);
	}
	return (0);
}



/***************************************************/
/* full interactive mode                           */
/***************************************************/
stage0(file)
char *file;
{
	dispmenu(file);
	while (1) {
	    printf(Q_LINE);
	    printf("Enter Selection: ");
	    gets(s);
	    rm_blanks(s);
	    while ( !((s[0] > '0') && (s[0] < '6') && (s[1] == 0))) {
	    	printf(E_LINE); /* clear any previous error */
		printf("Please enter a one digit number between 1 and 5");
		printf(Q_LINE);
		printf("Enter Selection: ");
		gets(s);
		rm_blanks(s);
	    }
	    printf(E_LINE);
	    switch(s[0]) {
	    case '1':
		if (pcreate() == -1)
		    return;
		break;
	    case '2':
		if (pchange() == -1)
		    return;
		break;
	    case '3':
		if (pdelete() == -1)
		    return;
		break;
            case '4':
		if (chk_ptable() == 1)  /* updates disk part. table if it has changed */
			dev_mboot_write(0, Bootsect, sectsiz);
/****** If the vtoc table is wrong fix it ******/
		if (io_adjt)
			fix_slice();
                close(Dev);
                exit(0);
	    case '5':
/****** If the vtoc table is wrong fix it ******/
		if (io_adjt)
			fix_slice();
	   	close(Dev);
		exit(0);
	    default:
		break;
	    }
            cpybtbl();
	    disptbl();
	    dispmenu(file);
	}
}


/******************************/
/* creat an fdisk entry       */
/******************************/
pcreate()
{
	unsigned char tsystid = 'z';
	int i,j;
	int startcyl, endcyl;

	i = 0;
	while (1) {
		if (i == FD_NUMPART) {
			printf(E_LINE);
			printf("The partition table is full! \n");
			printf("you must delete an old partition before creating a new one\n");
			return(-1);
		}
		if ( Table[i].systid == UNUSED ) {
			break;
		}
		i++;
	}

	j = 0;
	for (i=0; i<FD_NUMPART; i++) {
		if (Table[i].systid != UNUSED) {
			j += Table[i].numsect;
		}
		if (j >= Numcyl * heads * sectors) {
			printf(E_LINE);
			printf("There is no more room on the disk for another partition.\n");
			printf("You must delete a partition before creating a new one.\n");
			return(-1);
		}
	}
	while (tsystid == 'z') {
		printf(Q_LINE);
		printf("Indicate the type of partition you want to create\n");
		printf("  (1=SOLARIS, 2=UNIX, 3=PCIXOS, 4=Other, 8=DOSBIG)\n");
		printf("  (5=DOS12, 6=DOS16, 7=DOSEXT, 0=Exit) ?");
		gets(s);
		rm_blanks(s);
		if (s[1] != 0) {
			printf(E_LINE);
	    		printf("Invalid selection, try again");
			continue;
		}
		switch(s[0]) {
		case '0':		/* exit */
		    printf(E_LINE);
		    return(-1);
		case '1':		/* Solaris System partition */
		    tsystid = SUNIXOS;
		    break;
		case '2':		/* UNIX partition */
		    tsystid = UNIXOS;
		    break;
		case '3':		/* PCIXOS partition */
		    tsystid = PCIXOS;
		    break;
		case '4':		/* OTHEROS System partition */
		    tsystid = OTHEROS;
		    break;
		case '5':
		    tsystid = DOSOS12; /* DOS 12 bit fat */
		    break;
		case '6':
		    tsystid = DOSOS16; /* DOS 16 bit fat */
		    break;
		case '7':
		    tsystid = EXTDOS;
		    break;
		case '8':
		    tsystid = DOSHUGE;
		    break;
		default:
		    printf(E_LINE);
		    printf("Invalid selection, try again.");
		    continue;
		}
	}
	printf(E_LINE);
	i = specify(tsystid);
	if ( i == -1 ) return(-1);
	printf(E_LINE);
	printf(Q_LINE);

        printf("Do you want this to become the Active partition? If so, it will be activated\n");
        printf("each time you reset your computer or when you turn it on again.\n");
	printf("Please type \"y\" or \"n\". ");
	gets(s);
	rm_blanks(s);
	while ( (s[1] != 0) &&
		((s[0] != 'y')&&(s[0] != 'Y')&&(s[0] != 'n')&&(s[0] != 'N')))
	{
	    printf(E_LINE);
	    printf(" Please answer \"y\" or \"n\": ");
	    gets(s);
	    rm_blanks(s);
	}
	printf(E_LINE);
	if (s[0] == 'y' || s[0] == 'Y' ) {
	    for ( j=0; j<FD_NUMPART; j++)
		if ( j == i ) {
	    	    Table[j].bootid = ACTIVE;
		    printf(E_LINE);
		    printf("Partition %d is now the Active partition",j+1);
		}
		else
	    	    Table[j].bootid = 0;
	}
	else
	    Table[i].bootid = 0;
	return(1);
}


/****************************************/
/* quary the user for the new partition */
/****************************************/
specify(tsystid)
unsigned char tsystid;
{
	int	i, j,
		percent = -1;
	int	cyl, cylen, first_free, size_free;

	printf(Q_LINE);
	printf("Indicate the percentage of the disk you want this partition \n");
	printf("to use (or enter \"c\" to specify in cylinders). ");
	gets(s);
	rm_blanks(s);
	if ( s[0] != 'c' ){	/* specifying size in percentage of disk */
	    i=0;
	    while(s[i] != '\0') {
		if ( s[i] < '0' || s[i] > '9' ) {
		    printf(E_LINE);
		    printf("Invalid Percentage value specified\n");
		    printf("Please retry the operation.");
		    return(-1);
		}
		i++;
		if ( i > 3 ) {
		    printf(E_LINE);
		    printf("Invalid Percentage value specified\n");
		    printf("Please retry the operation.");
		    return(-1);
		}
	    }
	    if ( (percent = atoi(s)) > 100 ) {
		printf(E_LINE);
		printf("Percentage specified is too large, enter a value between 1 and 100\n");
		printf("Please retry the operation.");
		return(-1);
	    }
	    if (percent < 1 ) {
		printf(E_LINE);
		printf("Percentage specified is too small, enter a value between 1 and 100\n");
		printf("Please retry the operation.");
		return(-1);
	    }
 
            cylen = (Numcyl * percent) / 100;
	    if ((percent < 100) && (((Numcyl * percent) % 10) > 5))
		cylen++;

	    /* verify not exceeded maximum DOS partition size (32MB) */
	    if ((tsystid == DOSOS12) && ((long)((long)cylen*heads*sectors) > MAXDOS)) {
		int n;
		n =(int)(MAXDOS*100/(int)(heads*sectors)/Numcyl);
		printf(E_LINE);
		printf("Maximum size for a DOS partition is %d%%.\n",
			n <= 100 ? n : 100);
		printf("Please retry the operation.");
		return(-1);
	    }

	    for (i=0; i <FD_NUMPART; i++) {
		    int last_ent = 0;

		    /* find start of current check area */
		    if (i) { /* not an empty table */
			    first_free = Table[i-1].relsect +
			    Table[i-1].numsect ;
		    } else {
			    first_free = heads * sectors; 
		    }

		    /* find out size of current check area */
		    if (Table[i].systid == UNUSED) {
			    /* Special case hack for whole unused disk */
			    if (percent == 100 && i == 0)
				cylen--;
			    size_free = (Numcyl*heads*sectors) -
			    first_free ;
			    last_ent++;
		    } else {
			    if (i && ((Table[i-1].relsect+Table[i-1].numsect)
				 != Table[i].relsect)) { 
				    /* there is a hole in table */
				    size_free = Table[i].relsect -
				    (Table[i-1].relsect+Table[i-1].numsect);
			    }else if( i == 0 ){
				    size_free = Table[i].relsect -
				    heads*sectors;
			    }else {
				    size_free = 0;
			    }
		    }
		    
		    if ((cylen*heads*sectors) <= size_free) {
			    /* we found a place to use */
			    break;
		    } else if (last_ent) {
			    size_free = 0;
			    break;
		    }
	    }
	    if (i<FD_NUMPART && size_free) {
		    printf(E_LINE);
		    if ((i = insert_tbl(tsystid, 0, 0, 0, 0, 0, 0, 0, first_free,
				   cylen*heads*sectors)) < 0 )  { 
			    fprintf(stderr,"fdisk: Partition entry too big.\n");
			    return (-1);
		    }	
	    }else {
		    printf(E_LINE);
		    fprintf(stderr,"fdisk: Partition entry too big.\n");
		    i = -1;
	    }
	    return (i);
	} else {	/* specifying size in cylinders */

	    printf(E_LINE);
	    printf(Q_LINE);
	    printf("Enter starting cylinder number: ");
	    if ( (cyl = getcyl()) == -1 ) {
		printf(E_LINE);
		printf("Invalid number, please retry the operation.");
	        return(-1);
	    }
	    if (cyl >= (unsigned int)Numcyl) {
		printf(E_LINE);
		printf("Cylinder %d out of bounds, maximum is %d\n", 
		       cyl,Numcyl - 1);
		return(-1);
	    }

#if	0			/* XXX bh fornow */
	    /* cyl is starting cylinder */
	    if (cyl >= 1024) {
		    printf(E_LINE);
		    printf("Starting cylinder of a partition must be less than 1024\n");
		    printf("Please retry the operation.");
		    return(-1);
	    }
#endif
	    printf(Q_LINE);
	    printf("Enter partition size in cylinders: ");
	    if ( (cylen = getcyl()) == -1 ) {
		printf(E_LINE);
		printf("Invalid number, please retry the operation.");
	        return(-1);
	    }

	    /* verify not exceeded maximum DOS partition size (32MB) */
	    if ((tsystid == DOSOS12) && ((long)((long)cylen*hba_heads*hba_sectors) > MAXDOS)) {
		printf(E_LINE);
		printf("Maximum size for a %s partition is %ld cylinders.\n",
			Dstr, MAXDOS/(int)(heads*sectors));
		printf("Please retry the operation.");
		return(-1);
	    }

	    i = insert_tbl(tsystid, 0, 0, 0, 0, 0, 0, 0, cyl*heads*sectors,
			   cylen*heads*sectors);

	    if (verify_tbl()) {
		printf(E_LINE);
		printf("\07Partition cannot be created because"
                        " entries overlap\n");
		printf("or exceed disk capacity.\n");
		return (-1);
	    }

	    return(i);
    }
}

/*************************************************/
/* show what the user can do in interactive mode */
/*************************************************/
dispmenu(file)
char *file;
{
	printf(M_LINE);
	printf("SELECT ONE OF THE FOLLOWING: \n\n");
	printf("     1.   Create a partition\n");
	printf("     2.   Change Active (Boot from) partition\n");
	printf("     3.   Delete a partition\n");
	printf("     4.   Exit (Update disk configuration and exit)\n");
	printf("     5.   Cancel (Exit without updating disk configuration)");
}


/*********************************************/
/* change an entry in interactive mode       */
/*********************************************/
pchange()
{
	char s[80];
	int i,j;

	while (1) {
		printf(Q_LINE);
		     {
			printf("Enter the number of the partition you want to boot from\n");
			printf("(or enter 0 for none): ");
			}
		gets(s);
		rm_blanks(s);
		if ( (s[1] != 0) || (s[0] < '0') || (s[0] > '4') ) {
			printf(E_LINE);
			printf("Invalid response, please give a number between 0 and 4\n");
		}
		else {
			break;
		}
	}
	if ( s[0] == '0' ) {	/* no active partitions */
		for ( i=0; i<FD_NUMPART; i++) {
			if (Table[i].systid != UNUSED && Table[i].bootid == ACTIVE)
		    		Table[i].bootid = 0;
	    	}
	    	printf(E_LINE);
	    		printf("There is currently no Active partition");
	    	return(0);
		}
	else {	/* user has selected a partition to be active */
	    	i = s[0] - '1';
	    	if ( Table[i].systid == UNUSED ) {
	        	printf(E_LINE);
	        	printf("Partition does not exist");
	        	return(-1);
	    	}
		/* a DOS-DATA or EXT-DOS partition cannot be active */
		else if ((Table[i].systid == DOSDATA) || (Table[i].systid == EXTDOS)) {
			printf(E_LINE);
			printf("A DOS-DATA or EXT_DOS partition may not be made active.\n");
			printf("Select another partition.");
			return(-1);
		}
	    	Table[i].bootid = ACTIVE;
	    	for ( j=0; j<FD_NUMPART; j++) {
			if ( j != i )
		    	Table[j].bootid = 0;
	    	}
	}
	printf(E_LINE);
             {
		printf("Partition %d is now Active.  The system will start up from this\n",i+1);
		printf("partition after the next reboot.");
		}
	return(1);
}


/***************************************/
/* remove an entry in interactive mode */
/***************************************/
pdelete()
{
	char s[80];
	int i,j;
	char pactive;

DEL1:	printf(Q_LINE);
	printf("Enter the number of the partition you want to delete\n");
	printf(" (or enter 0 to exit ): ");
	gets(s);
	rm_blanks(s);
	if ( (s[0] == '0') ) {	/* exit delete cmd */
		printf(E_LINE);	/* clr error msg */
		return(1);
	}
	/* accept only a single digit between 1 and 4 */
	if (s[1] != 0 || (i=atoi(s)) < 1 || i > FD_NUMPART) {
		printf(E_LINE);
		printf("Invalid response, try again\n");
		goto DEL1;
	}
	else {		/* found a digit between 1 and 4 */
		--i;	/* structure begins with element 0 */
	}
	if ( Table[i].systid == UNUSED ) {
		printf(E_LINE);
		printf("Partition %d does not exist.",i+1);
		return(-1);
	}
	while (1) {
		printf(Q_LINE);
		printf("Do you want to delete partition %d?  This will make all files and \n",i+1);
		printf("programs in this partition inaccessible (type \"y\" or \"n\"). ");
		gets(s);
		rm_blanks(s);
		if ( (s[1] != 0) ||
		     ((s[0] != 'y') && (s[0] != 'n')))
		{
			printf(E_LINE);
			printf("Please answer \"y\" or \"n\"");
		}
		else break;
	}
	printf(E_LINE);
	if ( s[0] != 'y' && s[0] != 'Y' )
		return(1);
	if ( Table[i].bootid != 0 )
		pactive = 1;
	else
		pactive = 0;
	for ( j=i; j<3; j++ ) {
	    if(Table[j+1].systid == UNUSED) {
		Table[j].systid = UNUSED;
		break;
	    }
	    Table[j] = Table[j+1];
	}
	Table[j].systid = UNUSED;
	Table[j].numsect = 0;
	Table[j].relsect = 0;
        Table[j].bootid = 0;
	printf(E_LINE);
	printf("Partition %d has been deleted.",i+1);
	if ( pactive )
	    printf("  This was the active partition.");
	return(1);
}


/**********************/
/* filter out blanks  */
/**********************/
rm_blanks(s)
char *s;
{
	register int i,j;

	for (i=0; i<CBUFLEN; i++) {
		if ((s[i] == ' ') || (s[i] == '\t'))
			continue;
		else 
			/* found 1st non-blank char of string */
			break;
	}
	for (j=0; i<CBUFLEN; j++,i++) {
		if ((s[j] = s[i]) == '\0') {
			/* reached end of string */
			return;
		}
	}
}


/***********************************/
/* get the user spec cyl num       */
/***********************************/
getcyl()
{
int slen, i, j;
unsigned int cyl;
	gets(s);
	rm_blanks(s);
	slen = strlen(s);
	j = 1;
	cyl = 0;
	for ( i=slen-1; i>=0; i--) {
		if ( s[i] < '0' || s[i] > '9' ) {
			return(-1);
		}
		cyl += (j*(s[i]-'0'));
		j*=10;
	}
	return(cyl);
}


/****************************************/
/* display the current fdisk table      */
/****************************************/
disptbl()
{
	int i;
	unsigned int startcyl, endcyl, length, percent, remainder;
	char *stat, *type;
	unsigned char *t;

	printf(HOME);
	printf(T_LINE);
	printf("             Total disk size is %d cylinders\n", Numcyl);
	printf("             Cylinder size is %d (512 byte) blocks\n",
	                                      heads*sectors);
	printf("                                           Cylinders\n");
	printf("      Partition   Status    Type      Start   End   Length    %%\n");
	printf("      =========   ======    ========  =====   ===   ======   ===");
	for ( i=0; i<FD_NUMPART; i++) {
		if ( Table[i].systid == UNUSED ) {
		    	printf("\n");
			printf(CLR_LIN);
			continue;
		}
		if ( Table[i].bootid == ACTIVE )
		    stat = Actvstr;
		else
		     stat = NAstr;
		switch(Table[i].systid) {
		case UNIXOS:
		     type = Ustr;
		     break;
		case SUNIXOS:
		     type = SUstr;
		     break;
		case DOSOS12:
		     type = Dstr;
		     break;
		case DOSOS16:
		     type = D16str;
		     break;
		case EXTDOS:
		     type = EDstr;
		     break;
		case DOSDATA:
		     type = DDstr;
		     break;
		case DOSHUGE:
		     type = DBstr;
		     break;
		case PCIXOS:
		     type = PCstr;
		     break;
		default:
		     type = Ostr;
		     break;
		}
		t = &Table[i].bootid;
		startcyl = Table[i].relsect/(heads*sectors);
		length = Table[i].numsect / (long)(heads * sectors);
		if (Table[i].numsect % (long)(heads * sectors))
			length++;
	        endcyl =  startcyl + length - 1;
		percent = length * 100 / Numcyl;
		if ((remainder = (length*100 % Numcyl)) != 0) {
			if ((remainder * 100 / Numcyl) > 50) {
				/* round up */
				percent++;
			}
			/* ELSE leave percent as is since it's 
				already rounded down */
		}
		if (percent > 100) 
			percent = 100;
			
	        printf("\n          %d       %s    %-8.8s   %4d  %4d    %4d    %3d",
			i+1, stat, type, startcyl, endcyl, length,  percent);
	}
	/* print warning message if table is empty */
	if (Table[0].systid == UNUSED) {
		printf(W_LINE);
		printf("THERE ARE NO PARTITIONS CURRENTLY DEFINED");
	}
	else {
		/* clr the warning line */
		printf(W_LINE);
	}
}

/**************************************************************/
/* This will print the fdisk table to stderr for disk selected*/
/**************************************************************/
prtfdisk() {
	int i;

fprintf(stderr,
"SYSID ACT BHEAD BSECT BEGCYL   EHEAD ESECT ENDCYL   RELSECT   NUMSECT\n");
	
	for (i=0;i<FD_NUMPART;i++) {
		fprintf(stderr,"%-5d ",Table[i].systid);
		fprintf(stderr,"%-3d ",Table[i].bootid);
		fprintf(stderr,"%-5d ",Table[i].beghead);
		fprintf(stderr,"%-5d ",Table[i].begsect);
/*		fprintf(stderr,"%-8d ",((Table[i].begsect & 0xc0)<<2) + 
			Table[i].begcyl);*/
		fprintf(stderr,"%-8d ",Table[i].begcyl);

		fprintf(stderr,"%-5d ",Table[i].endhead);
		fprintf(stderr,"%-5d ",Table[i].endsect);
/*		fprintf(stderr,"%-8d ",((Table[i].endsect & 0xc0)<<2) + 
			Table[i].endcyl);*/
		fprintf(stderr,"%-8d ",Table[i].endcyl);
		
		fprintf(stderr,"%-9d ",Table[i].relsect);
		fprintf(stderr,"%-9d\n",Table[i].numsect);
		
	}
}



/*********************************************************************/
/* This function copies Table into Old_Table. It copies only systid, */
/* numsect, relsect and bootid because these are the only fields     */
/* needed for comparing to determine if Table changed.               */
/*********************************************************************/
cpyoldtbl()
{
int i;
	for (i=0; i<FD_NUMPART; i++)  {
	    Old_Table[i].systid = Table[i].systid;
	    Old_Table[i].numsect = Table[i].numsect;
	    Old_Table[i].relsect = Table[i].relsect;
	    Old_Table[i].bootid = Table[i].bootid;
        }
}


/************************************************/
/* zero out our fdisk table                     */
/************************************************/
nulltbl()
{
int i;
	for (i=0; i<FD_NUMPART; i++)  {
	    Table[i].systid = UNUSED;
	    Table[i].numsect = UNUSED;
	    Table[i].relsect = UNUSED;
	    Table[i].bootid = 0;
        }
}


/**********************************************************************/
/* This function copies the bytes from the boot record to an internal */
/* "Table".                                                           */
/* - all unused are padded with zeros starting at offset 446.	      */
/**********************************************************************/
copytbl()
{
	int i, j;
	char *bootptr, *temp_ptr;
	unsigned char *tbl_ptr;
	int tblpos;

	tbl_ptr = &Table[0].bootid;
	bootptr = Bootblk->parts;	/* start of partition table */
	if (Bootblk->signature != MBB_MAGIC)  {
		/* signature is missing */
		nulltbl();
		memcpy(Bootblk->bootinst, &BootCod, BOOTSZ);
		return;
	}
	/* 
	 * When DOS fdisk deletes a partition, it is not recognized
	 * by the old algorithm.  The algorithm that follows looks
	 * at each entry in the Bootrec and copies all those that
	 * are valid.
	 */
	j=0;
	for (i=0; i<FD_NUMPART; i++) {
            temp_ptr = bootptr;
	    if((*temp_ptr == 0) && (*(++temp_ptr) == 0) && (*(++temp_ptr) == 0)) {
		/* null entry */
		bootptr += sizeof(struct ipart);
   	    }
	    else {
		Table[j] = *(struct ipart *)bootptr;
		j++;
		bootptr += sizeof(struct ipart);
	    }
	}
	for (i=j; i<FD_NUMPART; i++) {
	    Table[i].systid = UNUSED;
	    Table[i].numsect = UNUSED;
	    Table[i].relsect = UNUSED;
	    Table[i].bootid = 0;
	
    }
	/* fornow we will always replace the bootcode with ours */
	memcpy(Bootblk->bootinst, &BootCod, BOOTSZ);
	cpybtbl();
}



/************************************************************/
/* This function copies the table into the 512 boot record. */
/* Note that the entries unused will always be the last     */
/* ones and they are marked with 100 in sysind.		    */
/* The the unused portion of the table is padded with       */
/* zeros in the bytes after the used entries.		    */
/************************************************************/
cpybtbl()
{
	struct ipart *boot_ptr,*tbl_ptr;

	boot_ptr = (struct ipart *)Bootblk->parts;
	tbl_ptr = (struct ipart *)&Table[0].bootid;
	for (; tbl_ptr < (struct ipart *)&Table[FD_NUMPART].bootid; tbl_ptr++, boot_ptr++) {
	    if ( tbl_ptr->systid == UNUSED )
		memset(boot_ptr, 0, sizeof(struct ipart));
	    else
		memcpy(boot_ptr, tbl_ptr, sizeof(struct ipart));
	}
	Bootblk->signature = MBB_MAGIC;
}


/*****************************************************************/
/* chk_ptable checks for any changes in the partition table. If  */
/* there are any they are written out to the partition table on  */
/* the disk.                                                     */
/*****************************************************************/
chk_ptable()
{
	int i, j, chng_flag, old_pt, new_pt;
	
 	chng_flag = 0;
        for (i=0; i<FD_NUMPART; i++) {
	   if ((Old_Table[i].systid != Table[i].systid) ||
	       (Old_Table[i].relsect != Table[i].relsect) ||
               (Old_Table[i].numsect != Table[i].numsect))  {
              chng_flag = 1;  /* partition table changed write back to disk */
           }
           if (Old_Table[i].bootid != Table[i].bootid)
	      chng_flag = 1;
	}
        if (chng_flag == 1) {
	   cpybtbl();
	   if (io_debug) {
		   fprintf(stderr,"About to write fdisk table:\n");
		   prtfdisk();
		   if (io_ifdisk) {
			   fprintf(stderr,"Press <ENTER> to continue.....\n");
			   gets(s);
		   }
	   }
	   dev_mboot_write(0, Bootsect, sectsiz);

        }
	for (new_pt = 0; new_pt < FD_NUMPART; new_pt++) {
		if (Table[new_pt].systid != SUNIXOS)
			continue;
		for (old_pt = 0; old_pt < FD_NUMPART; old_pt++) {
			if (
			(Old_Table[old_pt].systid == Table[new_pt].systid) &&
			(Old_Table[old_pt].relsect == Table[new_pt].relsect) &&
			(Old_Table[old_pt].numsect == Table[new_pt].numsect)
)
				break;
		}
		if (old_pt == FD_NUMPART && Table[new_pt].begcyl != 0)
			clear_vtoc(Table[new_pt].begcyl);
		break;
	}
	/* if user wants use to write a file for our table do it */
	if (io_Wfdisk) {
		ffile_write(io_Wfdisk);
	}
	return (chng_flag);
}


/************************************************************
 * ffile_write ()
 *
 * display contents of partition table without writing it to disk 
 *************************************************************/
ffile_write(file)
char	*file;
{
	register int	i;
	register int	c;
	FILE *fp;


	if ((fp = fopen(file, "w")) == NULL) {
		(void) fprintf(stderr, "fdisk: Cannot open output file %s\n",
			       file);
		exit(1);
	}

	/*
	 * Print out the FDISK
	 */
	fprintf(fp,"\n* %s default fdisk table\n", Dfltdev);
	fprintf(fp,"* Dimensions:\n");
	fprintf(fp,"*     %d bytes/sector\n", sectsiz);
	fprintf(fp,"*      %d sectors/track\n", sectors);
	fprintf(fp,"*       %d tracks/cylinder\n", heads);
	fprintf(fp,"*     %d cylinders\n", Numcyl);
	fprintf(fp,"*\n");
	fprintf(fp,"* systid:\n");
	fprintf(fp,"*   1:  DOSOS12\n");
	fprintf(fp,"*   2:  PCIXOS\n");
	fprintf(fp,"*   4:  DOSOS16\n");
	fprintf(fp,"*   5:  EXTDOS\n");
	fprintf(fp,"*   6:  DOSBIG\n");
	fprintf(fp,"*   86: DOSDATA\n");
	fprintf(fp,"*   98: OTHEROS\n");
	fprintf(fp,"*   99: UNIXOS\n");
	fprintf(fp,"*  130: SUNIXOS\n");
	fprintf(fp,"*\n");
	fprintf(fp,
"\n* Id    Act  Bhead  Bsect  Bcyl    Ehead  Esect  Ecyl    Rsect    Numsect\n");
	for (i = 0; i < FD_NUMPART; i++) {
		if (Table[i].systid != UNUSED)
			fprintf(fp,
"  %-5d %-4d %-6d %-6d %-7d %-6d %-6d %-7d %-8d %-8d\n",
				Table[i].systid,
				Table[i].bootid,
				Table[i].beghead,
				Table[i].begsect & 0x3f,
				((Table[i].begcyl & 0xff) | ((Table[i].begsect & 0xc0) << 2)),
				Table[i].endhead,
				Table[i].endsect & 0x3f,
				((Table[i].endcyl & 0xff) | ((Table[i].endsect & 0xc0) << 2)),
				Table[i].relsect,
				Table[i].numsect);
	}
	fclose (fp);
}

/***********************************************************************/
/* This will read the vtoc table and do a check that there are correct */
/* values in the solaris vtoc that do not extend past the end of the   */
/* partition. If there is no Solaris partition nothing is done.        */
/***********************************************************************/
fix_slice()
{
	int	i;
	int	ret;
	int	numsect;
	if (io_image)
		return (0);
	for (i = 0; i < FD_NUMPART; i++) 
		if (Table[i].systid == SUNIXOS) {
			/* we only care about size not starting point since
			 * vtoc entries a relative to start of partition
			 */
			numsect = Table[i].numsect;
			break;
		}
	if (i >= FD_NUMPART) {
		if (!io_nifdisk)
			(void) fprintf(stderr, 
			      "fdisk: No solaris partition found - vtoc not checked\n");
		       return (1);
		
	}
	if ((ret = readvtoc()) == 2)
			exit (1);		/* we failed to read vtoc */
	else if (ret != 1) {
		for (i=0; i<V_NUMPAR; i++) {
			/* special case for slice two */
			if ( i == 2 ){
				if ( disk_vtoc.v_part[i].p_start != 0) {
					(void) fprintf(stderr
						,"slice %d starts at %d, is not at start of partition",i, disk_vtoc.v_part[i].p_start );
					if (!io_nifdisk) {
						printf(" adjust ?:");
						if (yesno())
							disk_vtoc.v_part[i].p_start=0;
					}else{
						disk_vtoc.v_part[i].p_start=0;
						(void) fprintf(stderr ," adjusted!\n");
					}

				}
				if ( disk_vtoc.v_part[i].p_size != numsect) {
					(void) fprintf(stderr
					,"slice %d size %d does not cover complete partition",i, disk_vtoc.v_part[i].p_size);
					if (!io_nifdisk) {
						printf(" adjust ?:");
						if (yesno())
							disk_vtoc.v_part[i].p_size=numsect;
					}else{
						disk_vtoc.v_part[i].p_size=numsect;
						(void) fprintf(stderr ," adjusted!\n");
					}
				}
				if (disk_vtoc.v_part[i].p_tag != V_BACKUP) {
					(void) fprintf(stderr
					 ,"slice %d tag was %d should be %d",i,disk_vtoc.v_part[i].p_tag,V_BACKUP);
					if (!io_nifdisk) {
						printf(" fix ?:");
						if (yesno())
							disk_vtoc.v_part[i].p_tag=V_BACKUP;
					}else{
						disk_vtoc.v_part[i].p_tag=V_BACKUP;
						(void) fprintf(stderr ," fixed!\n");
					}
				}
			}else{
				if (io_ADJT) {
					if (disk_vtoc.v_part[i].p_start > numsect || disk_vtoc.v_part[i].p_start + disk_vtoc.v_part[i].p_size > numsect ) {
						(void) fprintf(stderr
						,"slice %d  start %d end %d is bigger then partition",i, disk_vtoc.v_part[i].p_start,disk_vtoc.v_part[i].p_start + disk_vtoc.v_part[i].p_size);
						if (!io_nifdisk) {
							printf(" remove ?:");
							if (yesno()){
								disk_vtoc.v_part[i].p_size=0;
								disk_vtoc.v_part[i].p_start=0;
								disk_vtoc.v_part[i].p_tag=0;
								disk_vtoc.v_part[i].p_flag=0;
							}
						}else{
							disk_vtoc.v_part[i].p_size=0;
							disk_vtoc.v_part[i].p_start=0;
							disk_vtoc.v_part[i].p_tag=0;
							disk_vtoc.v_part[i].p_flag=0;
							(void) fprintf(stderr ," removed!\n");
						}
					}
				}else{
					if (disk_vtoc.v_part[i].p_start > numsect) {
						(void) fprintf(stderr
						,"slice %d  start %d is bigger then partition",i, disk_vtoc.v_part[i].p_start);
						if (!io_nifdisk) {
							printf(" remove ?:");
							if (yesno()){
								disk_vtoc.v_part[i].p_size=0;
								disk_vtoc.v_part[i].p_start=0;
								disk_vtoc.v_part[i].p_tag=0;
								disk_vtoc.v_part[i].p_flag=0;
							}
						}else{
							disk_vtoc.v_part[i].p_size=0;
							disk_vtoc.v_part[i].p_start=0;
							disk_vtoc.v_part[i].p_tag=0;
							disk_vtoc.v_part[i].p_flag=0;
							(void) fprintf(stderr ," removed!\n");
						}
					}else if (disk_vtoc.v_part[i].p_start + disk_vtoc.v_part[i].p_size > numsect ) {
						(void) fprintf(stderr
						,"slice %d  end %d is bigger then partition",i, disk_vtoc.v_part[i].p_start + disk_vtoc.v_part[i].p_size);
						if (!io_nifdisk) {
							printf(" adjust ?:");
							if (yesno()){
								disk_vtoc.v_part[i].p_size=numsect;
							}
						}else{
							disk_vtoc.v_part[i].p_size=numsect;
							(void) fprintf(stderr ," adjusted!\n");
							}
					}
				}
			}
		}
	}
#if 1		/* bh fornow */
	/* make the vtoc look sane - ha ha */
	disk_vtoc.v_version = V_VERSION;
	disk_vtoc.v_sanity = VTOC_SANE;
	disk_vtoc.v_nparts = V_NUMPAR;
	if (disk_vtoc.v_sectorsz == 0)
		disk_vtoc.v_sectorsz = NBPSCTR;
#endif

	/* write the vtoc back to the disk */
	if (!io_readonly)
		writevtoc();

}

/*************************************/
/* get yes or no answer              */
/* return 1 for yes and 0 for no     */
/*************************************/
yesno()
{
	char	s[80];

	for (;;) {
		gets(s);
		rm_blanks(s);
		if ( (s[1] != 0) || ((s[0] != 'y') && (s[0] != 'n'))) {
			printf(E_LINE);
			printf("Please answer \"y\" or \"n\"");
			continue;
		}
		if (s[0] == 'y')
			return (1);
		else
			return (0);
	}
}
/******************************************/
/* read the vtoc from the device          */
/******************************************/
readvtoc()
{
	int i;
	if ((i = read_vtoc(Dev, &disk_vtoc)) < 0) {
		if (i == VT_EINVAL) {
			(void) fprintf(stderr, "fdisk: Invalid VTOC\n");
			vt_inval++;
			return (1);
		} else {
			(void) fprintf(stderr, "fdisk: Cannot read VTOC\n");
			return (2);
		}
	}
	return (0);
}

/******************************************/
/* write the vtoc to the device           */
/******************************************/
writevtoc()
{
	int	i;
	if ((i = write_vtoc(Dev, &disk_vtoc)) != 0) {
		if (i == VT_EINVAL) {
			(void) fprintf(stderr, "fdisks: invalid entry exists in vtoc\n");
		 } else {
			(void) fprintf(stderr, "fdisk: Cannot write VTOC\n");
		 }
		return(1);
	}
	return (0);

}
clear_vtoc(int begcyl)
{
	struct dk_label disk_label;
	
	memset (&disk_label, 0, sizeof (struct dk_label));
        
	if (lseek (Dev, (begcyl * sectsiz * heads * sectors) + 512, 0) == -1) {
		fprintf(stderr,"fdisk: Error seeking to label on (%s)\n",
			Dfltdev );
		return;
	}

	write (Dev, &disk_label , sizeof (struct dk_label)); 


}

