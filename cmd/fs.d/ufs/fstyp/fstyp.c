/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*	THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T	*/
/*	The copyright notice above does not evidence any   	*/
/*	actual or intended publication of such source code.	*/

/*
 * +++++++++++++++++++++++++++++++++++++++++++++++++++++++++
 * 		PROPRIETARY NOTICE (Combined)
 * 
 * This source code is unpublished proprietary information
 * constituting, or derived under license from AT&T's UNIX(r) System V.
 * In addition, portions of such source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 * 
 * 
 * 
 * 		Copyright Notice 
 * 
 * Notice of copyright on this source code product does not indicate 
 * publication.
 * 
 * 	(c) 1986,1987,1988,1989  Sun Microsystems, Inc
 * 	(c) 1983,1984,1985,1986,1987,1988,1989  AT&T.
 * 	          All rights reserved.
 *  
 */

#ident	"@(#)fstyp.c	1.19	94/01/04 SMI"	/* SVr4.0 1.5 */

/*
 * fstyp
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/mntent.h>
#include <sys/errno.h>
#include <sys/fs/ufs_fs.h>
#include <sys/stat.h>
#include <sys/vfs.h>

#include <stdio.h>
#include <sys/mnttab.h>

#include <locale.h>

extern offset_t llseek();

#define MAXLABELS	20
#define LINEMAX		256
#define NRPOS		8	/* for pre FFFS compatibility */

int	vflag = 0;		/* verbose output */
int	errflag = 0;
extern	int	optind;
char	*basename;
char	*special;
char	*fstype;

int
main(argc, argv)
	int argc;
	char *argv[];
{
	int	c;

	(void)setlocale(LC_ALL,"");        
#if !defined(TEXT_DOMAIN)
#define TEXT_DOMAIN "SYS_TEST"
#endif
	(void) textdomain(TEXT_DOMAIN);

	basename = argv[0];
	while ((c = getopt (argc, argv, "v")) != EOF) {
		switch (c) {

		case 'v':		/* dump super block */
			vflag++;
			break;

		case '?':
			errflag++;
		}
	}
	if (errflag) {
		usage ();
		exit (31+1);
	}
	if (argc < optind) {
		usage ();
		exit (31+1);
	}
	special = argv[optind];
	dumpfs (special);

	/* NOTREACHED */
}


usage ()
{
	(void) fprintf (stderr, gettext("ufs usage: fstyp [-v] special\n"));
}

union {
	struct fs fs;
	char pad[MAXBSIZE];
} fsun;
#define	afs	fsun.fs

union {
	struct cg cg;
	char pad[MAXBSIZE];
} cgun;
#define	acg	cgun.cg

dumpfs(name)
	char *name;
{
	int c, i, j, k, size, nrpos;
	struct fs *fsp;
	offset_t offset;

	close(0);
	if (open(name, 0) != 0) {
		perror(name);
		exit(1);
	}
	llseek(0, (offset_t)SBLOCK * DEV_BSIZE, 0);
	if (read(0, &afs, SBSIZE) != SBSIZE) {
		perror(name);
		exit(1);
	}
	if (afs.fs_magic != FS_MAGIC)
		exit (31+1);
	printf ("%s\n", "ufs");
	if (!vflag)
		exit (0);
	fsp = &afs;
	printf("magic\t%x\tformat\t%s\ttime\t%s", afs.fs_magic,
		afs.fs_postblformat == FS_42POSTBLFMT ? "static" : "dynamic",
		ctime(&afs.fs_time));
	printf("sblkno\t%d\tcblkno\t%d\tiblkno\t%d\tdblkno\t%d\n",
	    afs.fs_sblkno, afs.fs_cblkno, afs.fs_iblkno, afs.fs_dblkno);
	printf("sbsize\t%d\tcgsize\t%d\tcgoffset %d\tcgmask\t0x%08x\n",
	    afs.fs_sbsize, afs.fs_cgsize, afs.fs_cgoffset, afs.fs_cgmask);
	printf("ncg\t%d\tsize\t%d\tblocks\t%d\n",
	    afs.fs_ncg, afs.fs_size, afs.fs_dsize);
	printf("bsize\t%d\tshift\t%d\tmask\t0x%08x\n",
	    afs.fs_bsize, afs.fs_bshift, afs.fs_bmask);
	printf("fsize\t%d\tshift\t%d\tmask\t0x%08x\n",
	    afs.fs_fsize, afs.fs_fshift, afs.fs_fmask);
	printf("frag\t%d\tshift\t%d\tfsbtodb\t%d\n",
	    afs.fs_frag, afs.fs_fragshift, afs.fs_fsbtodb);
	printf("minfree\t%d%%\tmaxbpg\t%d\toptim\t%s\n",
	    afs.fs_minfree, afs.fs_maxbpg,
	    afs.fs_optim == FS_OPTSPACE ? "space" : "time");
	printf("maxcontig %d\trotdelay %dms\trps\t%d\n",
	    afs.fs_maxcontig, afs.fs_rotdelay, afs.fs_rps);
	printf("csaddr\t%d\tcssize\t%d\tshift\t%d\tmask\t0x%08x\n",
	    afs.fs_csaddr, afs.fs_cssize, afs.fs_csshift, afs.fs_csmask);
	printf("ntrak\t%d\tnsect\t%d\tspc\t%d\tncyl\t%d\n",
	    afs.fs_ntrak, afs.fs_nsect, afs.fs_spc, afs.fs_ncyl);
	printf("cpg\t%d\tbpg\t%d\tfpg\t%d\tipg\t%d\n",
	    afs.fs_cpg, afs.fs_fpg / afs.fs_frag, afs.fs_fpg, afs.fs_ipg);
	printf("nindir\t%d\tinopb\t%d\tnspf\t%d\n",
	    afs.fs_nindir, afs.fs_inopb, afs.fs_nspf);
	printf("nbfree\t%d\tndir\t%d\tnifree\t%d\tnffree\t%d\n",
	    afs.fs_cstotal.cs_nbfree, afs.fs_cstotal.cs_ndir,
	    afs.fs_cstotal.cs_nifree, afs.fs_cstotal.cs_nffree);
	printf("cgrotor\t%d\tfmod\t%d\tronly\t%d\n",
	    afs.fs_cgrotor, afs.fs_fmod, afs.fs_ronly);
	if (afs.fs_reclaim & (FS_RECLAIM | FS_RECLAIMING))
		printf("fs_reclaim%s%s\n",
		   (afs.fs_reclaim & FS_RECLAIM)    ? " FS_RECLAIM"    : "",
		   (afs.fs_reclaim & FS_RECLAIMING) ? " FS_RECLAIMING" : "");
	else
		printf("fs_reclaim is not set\n");
	if (afs.fs_state + (long) afs.fs_time == FSOKAY) {
		printf(gettext("file system state is valid, fsclean is %d\n"),
			afs.fs_clean);
	}
	else {
		printf(gettext("file system state is not valid\n"));
	}
	if (afs.fs_cpc != 0)
		printf(gettext("blocks available in each rotational position"));
	else
		printf( gettext(
			"insufficient space to maintain rotational tables\n"));
	for (c = 0; c < afs.fs_cpc; c++) {
		printf(gettext("\ncylinder number %d:"), c);
		nrpos = (((fsp)->fs_postblformat == FS_DYNAMICPOSTBLFMT)
		    ? (fsp)->fs_nrpos : NRPOS);
		for (i = 0; i < nrpos; i++) {
			if (fs_postbl(fsp, c)[i] == -1)
				continue;
			printf(gettext("\n   position %d:\t"), i);
			for (j = fs_postbl(fsp, c)[i], k = 1; ;
			     j += fs_rotbl(fsp)[j], k++) {
				printf("%5d", j);
				if (k % 12 == 0)
					printf("\n\t\t");
				if ((fs_rotbl(fsp))[j] == 0)
					break;
			}
		}
	}
	printf("\ncs[].cs_(nbfree,ndir,nifree,nffree):\n\t");
	for (i = 0, j = 0; i < afs.fs_cssize; i += afs.fs_bsize, j++) {
		size = afs.fs_cssize - i < afs.fs_bsize ?
		    afs.fs_cssize - i : afs.fs_bsize;
		afs.fs_csp[j] = (struct csum *)calloc(1, size);
		offset = (offset_t)fsbtodb(
		    &afs, (afs.fs_csaddr + j * afs.fs_frag)) * DEV_BSIZE;
		llseek(0, offset, 0);
		if (read(0, afs.fs_csp[j], size) != size) {
			perror(name);
			exit(1);
		}
	}
	for (i = 0; i < afs.fs_ncg; i++) {
		struct csum *cs = &afs.fs_cs(&afs, i);
		if (i && i % 4 == 0)
			printf("\n\t");
		printf("(%d,%d,%d,%d) ",
		    cs->cs_nbfree, cs->cs_ndir, cs->cs_nifree, cs->cs_nffree);
	}
	printf("\n");
	if (afs.fs_ncyl % afs.fs_cpg) {
		printf(gettext("cylinders in last group %d\n"),
		    i = afs.fs_ncyl % afs.fs_cpg);
		printf(gettext("blocks in last group %d\n"),
		    i * afs.fs_spc / NSPB(&afs));
	}
	printf("\n");
	for (i = 0; i < afs.fs_ncg; i++)
		dumpcg(name, i);
	close(0);
	exit(0);
}

dumpcg(name, c)
	char *name;
	int c;
{
	int i,j;
	offset_t	off;
	struct cg	*cgp;
	struct ocg	*ocgp;
	struct fs	*fsp;

	printf("\ncg %d:\n", c);
	off = llseek(0, (offset_t)fsbtodb(&afs, cgtod(&afs, c)) * DEV_BSIZE, 0);
	if (read(0, (char *)&acg, afs.fs_bsize) != afs.fs_bsize) {
		printf(gettext("dumpfs: %s: error reading cg\n"), name);
		return;
	}
	cgp =  (struct cg *) &acg;
	ocgp = (struct ocg *) &acg;
	fsp = &afs;
	if (!cg_chkmagic(cgp)) 
	    printf(gettext("Invalid Cylinder grp magic fffs:%x  4.2 fs:%x\n"),
		cgp->cg_magic, ocgp->cg_magic); 
	if (cgp->cg_magic == CG_MAGIC) {
		/* print FFFS 4.3 cyl grp format. */
		printf("magic\t%x\ttell\t%x\ttime\t%s",
		    cgp->cg_magic, (off_t)off, ctime(&cgp->cg_time)); /*******/
		printf("cgx\t%d\tncyl\t%d\tniblk\t%d\tndblk\t%d\n",
		    cgp->cg_cgx, cgp->cg_ncyl, cgp->cg_niblk, cgp->cg_ndblk);
		printf("nbfree\t%d\tndir\t%d\tnifree\t%d\tnffree\t%d\n",
		    cgp->cg_cs.cs_nbfree, cgp->cg_cs.cs_ndir,
		    cgp->cg_cs.cs_nifree, cgp->cg_cs.cs_nffree);
		printf("rotor\t%d\tirotor\t%d\tfrotor\t%d\nfrsum",
		    cgp->cg_rotor, cgp->cg_irotor, cgp->cg_frotor);
		for (i = 1, j = 0; i < afs.fs_frag; i++) {
			printf("\t%d", cgp->cg_frsum[i]);
			j += i * cgp->cg_frsum[i];
		}
		printf(gettext("\nsum of frsum: %d\niused:\t"), j);
		pbits(cg_inosused(cgp), afs.fs_ipg);
		printf(gettext("free:\t"));
		pbits(cg_blksfree(cgp), afs.fs_fpg);
		printf("b:\n");
		for (i = 0; i < afs.fs_cpg; i++) {
			printf("   c%d:\t(%d)\t", i, cg_blktot(cgp)[i]);
			for (j = 0; j < fsp->fs_nrpos; j++)	/**********/
				printf(" %d", cg_blks(fsp, cgp, i)[j]);
			printf("\n");
		}

	} else if (ocgp->cg_magic == CG_MAGIC) {
		/* print Old cyl grp format. */
		printf("magic\t%x\ttell\t%x\ttime\t%s",
		    ocgp->cg_magic, (off_t) off, ctime(&ocgp->cg_time));
		printf("cgx\t%d\tncyl\t%d\tniblk\t%d\tndblk\t%d\n",
		    ocgp->cg_cgx, ocgp->cg_ncyl, ocgp->cg_niblk, ocgp->cg_ndblk);
		printf("nbfree\t%d\tndir\t%d\tnifree\t%d\tnffree\t%d\n",
		    ocgp->cg_cs.cs_nbfree, ocgp->cg_cs.cs_ndir,
		    ocgp->cg_cs.cs_nifree, ocgp->cg_cs.cs_nffree);
		printf("rotor\t%d\tirotor\t%d\tfrotor\t%d\nfrsum",
		    ocgp->cg_rotor, ocgp->cg_irotor, ocgp->cg_frotor);
		for (i = 1, j = 0; i < afs.fs_frag; i++) {
			printf("\t%d", ocgp->cg_frsum[i]);
			j += i * ocgp->cg_frsum[i];
		}
		printf(gettext("\nsum of frsum: %d\niused:\t"), j);
		pbits(ocgp->cg_iused, afs.fs_ipg);
		printf(gettext("free:\t"));
		pbits(ocgp->cg_free, afs.fs_fpg);
		printf("b:\n");
		for (i = 0; i < afs.fs_cpg; i++) {
			printf("   c%d:\t(%d)\t", i, ocgp->cg_btot[i]);
			for (j = 0; j < NRPOS; j++)
				printf(" %d", ocgp->cg_b[i][j]);
			printf("\n");
		}
	}
}

pbits(cp, max)
	register char *cp;
	int max;
{
	register int i;
	int count = 0, j;

	for (i = 0; i < max; i++)
		if (isset(cp, i)) {
			if (count)
				printf(",%s", count %9 == 8 ? "\n\t" : " ");
			count++;
			printf("%d", i);
			j = i;
			while ((i+1)<max && isset(cp, i+1))
				i++;
			if (i != j)
				printf("-%d", i);
		}
	printf("\n");
}
