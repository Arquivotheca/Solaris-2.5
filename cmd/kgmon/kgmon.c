

#pragma ident	"@(#)kgmon.c	1.13	93/08/04 SMI"

/*
 * Copyright (c) 1991, 1993 by Sun Microsystems, Inc.
 */

/*
 * This file contains functions that duplicate the image presented by
 * /dev/ksyms, and change the st_value fields in the symbol table in
 * order to create a pseudo "contiguous" text range for gprof to use.
 */

#include <sys/types.h>
#include <sys/unistd.h>
#include <sys/errno.h>
#include <sys/elf.h>
#include <stdio.h>
#include <sys/fcntl.h>
#include <nlist.h>
#include <macros.h>
#include <sys/cpuvar.h>
#include <kvm.h>
#include <sys/ioccom.h>
#include <sys/gprof.h>
#include <fcntl.h>

#define	dprintf if (debug) printf
#define	NO_MOD1 "make sure there is a symbolic link from /dev/profile "
#define	NO_MOD2 "to the profile device\n"
#define	NO_MOD3 "e.g. ln -s /devices/obio/profile:profile /dev/profile\n"
#define	PROFILER	"/dev/profile"
#define	SYMBOLS		"/dev/ksyms"
#define	OUTPUT		"gmon.syms"
#define	DISABLE_UNLOADING	0x10
#define	ENABLE_UNLOADING	0x0
#define	KERNELBASE	0
#define	CPU_STRUCT	1
#define	MCOUNT		2
#define	MODDEBUG	3
#define	GMON_NAME_SIZE	80
#define	NCPU_SIZE	7
#define	MAX_CPUS	128

static void get_info();

static struct nlist nl[] = {
	{ "s_text", 0, 0, 0, 0, 0 },	/* start of kernel text space */
	{ "cpu", 0, 0, 0, 0, 0 },	/* cpu struct (array of) */
	{ "kernel_profiling", 0, 0, 0, 0, 0 },
			/* compiled with GPROF? */
	{ "moddebug", 0, 0, 0, 0, 0 },	/* module unloading variable */
	{ "", 0, 0, 0, 0, 0 }
};

static	int	disable_unloading = DISABLE_UNLOADING;
static	int	enable_unloading = ENABLE_UNLOADING;
static	struct	cpu 	*first_cpu = NULL;
static 	struct	cpu	*this_cpu;
static	kernp_t	*first_pr = NULL;
static	kernp_t	*this_pr;
static 	long	max_cpu_id;	/* highest cpu id in the system */
static 	long	how_many_cpus;
static 	char	*image;
static 	boolean_t	status_only;
static 	int	image_size;
static 	u_int	kernel_base;
extern 	int 	errno;
static	int 	rc;
static	char	*system = NULL;
static	char	*kmemf  = NULL;
static	kvm_t	*kd;
static	int 	fd;
static	int 	fromindex;
static	int 	endfrom;
static	int	buffer_size;
static	int	fromssize;
static	u_int	tossize;
static	u_long	magic_point;
static	u_long	relocation_factor;
static	kp_call_t	*top;
static	int	kernel_textsize;
static	int 	module_textsize;
static	u_long 	frompc;
static	u_long	topc;
static	char	*kernel_lowpc;
static	char	*module_lowpc;
static	char	*module_highpc;
static	char	*kernel_highpc;
static	kp_call_t	**froms;
static	kp_call_t	**kfroms;
static	char	*local_buf;
static	kp_call_t 	*tos, *ktos;
static	struct	phdr	header;
static	struct	rawarc rawarc;
static	char 	per_cpu[NCPU_SIZE];
static	char 	gmon_name[GMON_NAME_SIZE];
static	char 	*gmon_prefix = "gmon";
static	char 	*gmon_suffix = ".out";
static 	int	debug = 0;

/*
 * Returns 0 for success, 1 if failure.
 */
static int
gprof_reloc(u_long sysbase, u_long relocation_factor)
{
	static void error_exit(char *);
	static int symbol_reloc(Elf32_Sym *, int, u_long, u_long);

	register int		i;
	int 			id, od;
	Elf32_Ehdr		*ehdr;
	Elf32_Shdr		*shdr;
	Elf32_Sym		*symtab;
	struct stat		statbuf;

	/* open our symbol image */
	if ((id = open(SYMBOLS, O_RDONLY)) < 0)
		error_exit("cannot open ksyms driver.");

	/* get the size of the image. */
	if ((fstat(id, &statbuf)) < 0)
		error_exit("fstat of ksyms driver failed.");

	image_size = statbuf.st_size;

	if ((image = (char *)malloc(image_size)) == (char *)0)
		error_exit("cannot malloc memory.");

	if (read(id, image, image_size) < 0)
		error_exit("read of ksyms driver failed.");

	/* get the elf hdr. */
	/* LINTED pointer alignment */
	ehdr = (Elf32_Ehdr *)image;

	/*
	 * Run thru the section headers, looking for the symbol table's
	 * section hdr. Zap the st_value fields.
	 */
	shdr = (Elf32_Shdr *)((u_int)image + (u_int)ehdr->e_shoff);
	for (i = 0; (u_short)i < ehdr->e_shnum; i++, shdr =
	    (Elf32_Shdr *)((u_int)shdr + sizeof (Elf32_Shdr))) {
		if (shdr->sh_type == SHT_SYMTAB) {
			symtab = (Elf32_Sym *)((u_int)image +
			    (u_int)shdr->sh_offset);
			if (symbol_reloc(symtab, shdr->sh_size,
			    sysbase, relocation_factor) < 0)
				error_exit("symbol relocation failed.");
			break;
		}
	}

	/* open the output file. */
	if ((od = open(OUTPUT, O_WRONLY | O_CREAT, 0664)) < 0)
		error_exit("cannot open output file.");

	if (write(od, image, image_size) < 0)
		error_exit("cannot write to output file.");

	fprintf(stdout, "a.out symbols for the kernel and modules ");
	fprintf(stdout, "have been dumped to gmon.syms\n");
	free(image);
	return (0);
}

/*
 * Zap all the st_value fields that fall in the module address space so
 * that the module text looks like it starts right after the kernel etext.
 */
static int
symbol_reloc(Elf32_Sym *sym, int size, u_long sysbase, u_long relocation_factor)
{
	register Elf32_Sym *wk;

	for (wk = sym; wk < (Elf32_Sym *)((u_int)sym + (u_int)size); wk++) {
		if (wk->st_value >= (Elf32_Addr)sysbase)  {
			wk->st_value -= (Elf32_Addr)relocation_factor;
		}
	}
	return (0);
}

static void
error_exit(char *s)
{
	fprintf(stdout, "gprof_reloc: %s\n", s);
	perror("gprof_reloc");
	if (image != (char *)0)
		free(image);
	exit(-1);
}

static void
debug_prdump(kernp_t *this_pr) {
	dprintf("profile_lock = %x\n", this_pr->profiling_lock);
	dprintf("profiling = %d\n", this_pr->profiling);
	dprintf("kernel_textsize = %u\n", this_pr->kernel_textsize);
	dprintf("kernel_lowpc = %x\n", this_pr->kernel_lowpc);
	dprintf("kernel_highpc = %x\n", this_pr->kernel_highpc);
	dprintf("module_texsize = %u\n", this_pr->module_textsize);
	dprintf("module_lowpc = %x\n", this_pr->module_lowpc);
	dprintf("module_highpc = %x\n", this_pr->module_highpc);
	dprintf("s_hash = %x\n", this_pr->s_hash);
	dprintf("samples = %x\n", this_pr->samples);
	dprintf("froms = %x\n", this_pr->froms);
	dprintf("tos = %x\n", this_pr->tos);
	dprintf("s_hashsize = %d\n", this_pr->s_hashsize);
	dprintf("samplessize = %d\n", this_pr->samplessize);
	dprintf("fromssize = %d\n", this_pr->fromssize);
	dprintf("tossize = %d\n", this_pr->tossize);
	dprintf("samplesnext = %x\n", this_pr->samplesnext);
	dprintf("tosnext = %x\n", this_pr->tosnext);
	fflush(stdout);
}

static	int
dumpstate(int snapping)
{
	int 		j;
	int 		cpu_index;
	HISTCOUNTER	*kcount;
	int 		kcountsize;
	kp_sample_t	*samples;
	int		samplessize;
	int		slimit;
	int		sindex;
	u_long		spc;
	int		kcount_index;
	long		newcount;
	int 		mode;
	boolean_t	out_lost = B_FALSE;

	this_cpu = first_cpu;
	this_pr = first_pr;
	dprintf("dfirstcpu %d first pr %d\n", first_cpu, first_pr);
	for (cpu_index = 0; cpu_index <= max_cpu_id; cpu_index++) {
		if (this_cpu->cpu_flags == 0) {
			this_cpu++;
			this_pr++;
			continue;
		}
		if (this_cpu->cpu_profiling == NULL) {
			fprintf(stdout, "%s%s",
			    "profiling is not initialized\n",
			    "please type 'kgmon -i'\n");
			exit(1);
		}
		for (j = 0; j < GMON_NAME_SIZE; j++) {
			gmon_name[j] = '\0';
		}
		strcpy(gmon_name, gmon_prefix);
		if (how_many_cpus > 1) {
			sprintf(per_cpu, "%d", cpu_index);
			strcat(gmon_name, per_cpu);
		}
		strcat(gmon_name, gmon_suffix);
		if ((fd = creat(gmon_name, 0666)) == NULL) {
			fprintf(stdout, "error %d creating %s file\n", errno,
			    gmon_name);
			return (-1);
		}
		mode = this_pr->profiling;
		if (mode == PROFILE_OFF && snapping) {
			fprintf(stdout, "%s",
			    "profiling is not running, you cannot snapshot\n");
			return (-1);
		}
		if (mode == PROFILE_ON && (snapping == 0)) {
			fprintf(stdout, "%s%s",
			    "profiling is on, you must halt ",
			    "before dumping the data\n");
			return (-1);
		}
		fprintf(stdout, "dumping cpu %d - one moment ", cpu_index);
		fflush(stdout);
		fprintf(stdout, "\n");
		kernel_lowpc = this_pr->kernel_lowpc;
		if ((u_int)kernel_lowpc != kernel_base) {
			fprintf(stdout,
				"profiling structure has garbage in it!\n");
			dprintf("kernel_base is 0x%x\n", kernel_base);
			debug_prdump(this_pr);
			exit(6);
		}
		debug_prdump(this_pr);
		module_textsize = this_pr->module_textsize;
		kernel_textsize = this_pr->kernel_textsize;
		module_lowpc = this_pr->module_lowpc;
		module_highpc = this_pr->module_highpc;
		kernel_lowpc = this_pr->kernel_lowpc;
		kernel_highpc = this_pr->kernel_highpc;
		samplessize = this_pr->samplessize;
		kcountsize = (kernel_textsize + module_textsize +
			HIST_GRANULARITY - 1) / HIST_GRANULARITY;
		if (module_lowpc == 0) {
			magic_point = (u_long)0xFFFFFFFF;
			relocation_factor = 0;
		} else {
			magic_point = (u_long)module_lowpc;
			relocation_factor =
				(u_long)(module_lowpc - kernel_highpc);
		}
		dprintf("magic point is %x\n", magic_point);
		dprintf("relocation factor is %x\n",
			relocation_factor);
		header.lpc = this_pr->kernel_lowpc;
		if (relocation_factor) {
			header.hpc = this_pr->module_highpc -
					relocation_factor;
		} else {
			header.hpc = kernel_highpc;
		}
		buffer_size = kcountsize * sizeof (HISTCOUNTER);
		header.ncnt = buffer_size + sizeof (struct phdr);
		if ((write(fd, &header, (sizeof (struct phdr)))) !=
		    sizeof (struct phdr)) {
			fprintf(stdout, "write error %d writing header to ",
				errno);
			fprintf(stdout, "gmon.out\n");
			exit(2);
		}
		if ((kcount = (HISTCOUNTER *)calloc(kcountsize,
		    sizeof (HISTCOUNTER))) == NULL) {
			fprintf(stdout, "kcount calloc error %d\n", errno);
			return (-1);
		}
		if ((samples = (kp_sample_t *)calloc(samplessize, 1)) == NULL) {
			fprintf(stdout, "samples calloc error %d\n", errno);
			return (-1);
		}
		if ((rc = kvm_read(kd, (off_t)this_pr->samples,
				(char *)samples, samplessize)) != samplessize) {
			fprintf(stdout, "kvm_read error %d read %d bytes\n",
				errno, rc);
			return (-1);
		}
		slimit = this_pr->samplesnext -
			this_pr->samples;
		for (sindex = 0; sindex < slimit; sindex++) {
			spc = (u_long) samples[sindex].pc;
			if (spc >= magic_point) {
				spc -= relocation_factor;
			}
			if ((spc < (u_long) header.lpc) ||
			    (spc >= (u_long) header.hpc)) {
				fprintf(stdout, "out of range sample\n");
				return (-1);
			}
			kcount_index = (spc - (u_long) header.lpc)
				/ HIST_GRANULARITY;
			newcount = (long) kcount[kcount_index] +
				samples[sindex].count;
			kcount[kcount_index] = newcount;
			if (kcount[kcount_index] != newcount) {
				fprintf(stdout, "sample overflow\n");
				return (-1);
			}
		}
		free(samples);
		if ((rc = write(fd, (char *)kcount, buffer_size))
		    != buffer_size) {
			fprintf(stdout, "write error %d wrote %d bytes\n",
				errno, rc);
			return (-1);
		}
		free(kcount);
		fromssize = this_pr->fromssize;
		if (fromssize != 0) {
			kfroms = this_pr->froms;
			if ((froms = (kp_call_t **)calloc(fromssize, 1))
			    == (kp_call_t **)NULL) {
				fprintf(stdout, "buf calloc error %d\n", errno);
				return (-1);
			}
			if ((rc = kvm_read(kd, (off_t)kfroms, (char *)froms,
			    fromssize)) != fromssize) {
				fprintf(stdout, "kvm_read error %d froms: ",
				    errno);
				fprintf(stdout, "requested %d, got %d",
				    fromssize, rc);
				exit(3);
			}
			tossize = this_pr->tossize;
			dprintf("allocating %d bytes for tos\n", tossize);
			if ((tos = (kp_call_t *)calloc(tossize, 1)) == NULL) {
				fprintf(stdout, "tos calloc error %d\n",
				    errno);
				return (-1);
			}
			ktos = this_pr->tos;
			if ((rc = kvm_read(kd, (off_t)ktos, (char *)tos,
			    tossize)) != tossize) {
				fprintf(stdout, "kvm_read error %d tos: i",
				    errno);
				fprintf(stdout, "request %d, got %d",
				    tossize, rc);
				exit(4);
			}
			endfrom = (fromssize / sizeof (*froms));
			dprintf("DUMPING FROM 0 to %d\n", endfrom);
			for (fromindex = 0; fromindex < endfrom; fromindex++) {
				if (froms[fromindex] == NULL) {
					continue;
				}
				top = froms[fromindex];
				while (top != NULL) {
					dprintf("froms[%d] -> tos[%d]\n",
						fromindex, top - ktos);
						fflush(stdout);
					top = tos + (top - ktos);
					frompc = (u_long)top->frompc;
					topc = (u_long)top->topc;
					dprintf("    frompc %x topc %x",
						frompc, topc);
					dprintf(" count %d\n",
						top->count);
					if (frompc >= magic_point) {
						frompc -= relocation_factor;
						dprintf("reloc frompc = %x\n",
							frompc);
					}
					if (topc >= magic_point) {
						topc -= relocation_factor;
						dprintf("reloc topc = %x\n",
							topc);
					}
					if (!out_lost &&
						((frompc <
							(u_long) header.lpc) ||
						(frompc >=
							(u_long) header.hpc) ||
						(topc <
							(u_long) header.lpc) ||
						(topc >=
							(u_long) header.hpc))) {
						fprintf(stdout,
						    "out of range calls "
							"lost\n");
						out_lost = B_TRUE;
						top = top->link;
						continue;
					}
					rawarc.raw_frompc = frompc;
					rawarc.raw_topc = topc;
					rawarc.raw_count = top->count;
					top = top->link;
					if ((rc = write(fd, &rawarc,
					    (sizeof (rawarc)))) !=
					    (sizeof (rawarc))) {
						fprintf(stdout,
						    "write error %d\n",
						    errno);
						exit(5);
					}
				}
			}
			free(froms);
			free(tos);
		}
		close(fd);
		this_cpu++;
		this_pr++;
	}
	if (relocation_factor) {
		if (gprof_reloc((u_long)module_lowpc, relocation_factor) < 0) {
			fprintf(stdout, "couldn't relocate symbols\n");
			return (0);
		}
	}
	return (0);
}

static	int
resetstate()
{
	int 	cpu_index;
	off_t	buf;
	off_t	to;
	char 	*from;
	int 	mode;

	this_pr = first_pr;
	this_cpu = first_cpu;
	for (cpu_index = 0; cpu_index <= max_cpu_id; cpu_index++) {
		if (this_cpu->cpu_flags == 0) {
			this_pr++;
			this_cpu++;
			continue;
		}
		if (this_cpu->cpu_profiling == NULL) {
			fprintf(stdout, "profiling is not initialized\n");
			fprintf(stdout, "please type 'kgmon -i'\n");
			exit(5);
		}
		mode = this_pr->profiling;
		if (mode == PROFILE_ON) {
			fprintf(stdout, "%s%s",
			    "profiling is on, you must halt before",
			    " resetting the buffers\n");
			return (-1);
		}
		fprintf(stdout, "reseting cpu %d - one moment ", cpu_index);
		fflush(stdout);
		fprintf(stdout, "\n");
		kernel_lowpc = this_pr->kernel_lowpc;
		if ((u_int)kernel_lowpc != kernel_base) {
			fprintf(stdout,
				"profiling structure has garbage in it!\n");
			dprintf("kernel_lowpc is 0x%x\n", kernel_lowpc);
			debug_prdump(this_pr);
			exit(10);
		}
		buffer_size = this_pr->s_hashsize;
		if ((local_buf = (char *)calloc(buffer_size, 1))
		    == (char *)NULL) {
			fprintf(stdout, "error %d allocing %d bytes\n",
				errno, buffer_size);
			return (0);
		}
		buf = (off_t)this_pr->s_hash;
		dprintf("about to write %d bytes to s_hash %x\n",
			buffer_size, (int) buf);
		if ((kvm_write(kd, buf, local_buf, buffer_size))
		    != buffer_size) {
			fprintf(stdout, "kvm_write error %d on s_hash array\n",
				errno);
			exit(7);
		}
		free(local_buf);
		buffer_size = this_pr->samplessize;
		if ((local_buf = (char *)calloc(buffer_size, 1))
		    == (char *)NULL) {
			fprintf(stdout, "error %d allocing %d bytes\n",
				errno, buffer_size);
			return (0);
		}
		buf = (off_t)this_pr->samples;
		dprintf("about to write %d bytes to samples %x\n",
			buffer_size, (int) buf);
		if ((kvm_write(kd, buf, local_buf, buffer_size))
		    != buffer_size) {
			fprintf(stdout, "kvm_write error %d on s_hash array\n",
				errno);
			exit(7);
		}
		free(local_buf);
		fromssize = this_pr->fromssize;
		if (fromssize != 0) {
			kfroms = this_pr->froms;
			if ((local_buf = (char *)calloc(fromssize, 1))
			    == (char *)NULL) {
				fprintf(stdout,
				    "error %d allocing %d bytes\n",
				    errno, fromssize);
				return (0);
			}
			dprintf("about to write %d bytes to froms %x\n",
			    fromssize, kfroms);
			if ((kvm_write(kd, (off_t)kfroms, local_buf,
			    fromssize)) != fromssize) {
				fprintf(stdout,
				    "kvm_write error %d on froms array\n",
				    errno);
				exit(8);
			}
			free(local_buf);
			tossize = this_pr->tossize;
			ktos = this_pr->tos;
			if ((local_buf = (char *)calloc(tossize, 1))
			    == (char *)NULL) {
				fprintf(stdout,
				    "error %d allocing %d bytes\n",
				    errno, tossize);
				return (0);
			}
			dprintf("writing %d bytes to tos %x\n",
			    tossize, ktos);
			if ((kvm_write(kd, (off_t)ktos, local_buf, tossize)) !=
			    tossize) {
				fprintf(stdout, "kvm_write error %d on tos\n",
				    errno);
				exit(9);
			}
			free(local_buf);
		}
		this_pr->samplesnext = this_pr->samples;
		this_pr->tosnext = this_pr->tos;
		to = (off_t)this_cpu->cpu_profiling;
		from = (char *)this_pr;
		if (kvm_write(kd, to, from, sizeof (kernp_t))
		    != sizeof (kernp_t)) {
			fprintf(stdout, "error %d writing nexts\n", errno);
			return (-1);
		}
		this_cpu++;
		this_pr++;
	}
	return (0);
}

static	int
turnonoff(int onoff, int tell_me, int just_checking)
{
	int 	mode;
	int 	cpu_index;
	off_t	to;
	char 	*from;
	boolean_t	first_time;
	boolean_t 	last_time;

	first_time = B_TRUE;
	last_time = B_FALSE;
	this_cpu = first_cpu;
	this_pr = first_pr;
	dprintf("first_cpu is %d first_pr is %d\n", first_cpu, first_pr);
	for (cpu_index = 0; cpu_index <= max_cpu_id; cpu_index++) {
		if (cpu_index == max_cpu_id) {
			last_time = B_TRUE;
		}
		dprintf("this_cpu %d cpu_flags is %x\n", this_cpu,
			this_cpu->cpu_flags);
		if (this_cpu->cpu_flags == 0) {
			dprintf("cpu %d isn't valid\n", cpu_index);
			fflush(stdout);
			this_cpu++;
			this_pr++;
			continue;
		}
		if (this_cpu->cpu_profiling == NULL) {
			fprintf(stdout, "profiling is not initialized\n");
			fprintf(stdout, "please type 'kgmon -i'\n");
			exit(5);
		}
		mode = this_pr->profiling;
		dprintf("this_pr %d this_cpu %d\n", this_pr, this_cpu);
		if (tell_me)  {
			if (first_time) {
				first_time = B_FALSE;
				fprintf(stdout, "kernel profiling ");
				if (how_many_cpus > 1) {
					fprintf(stdout, "for cpu %d, ",
						cpu_index);
				}
			} else {
				fprintf(stdout, "%d, ", cpu_index);
			}
			if ((onoff == mode) && (! just_checking) && last_time) {
				fprintf(stdout, "already ");
			}
			if (! just_checking) {
				mode = onoff;
			}
			if (last_time) {
				switch (mode) {
				case PROFILE_NCPUS:
					fprintf(stdout, "%s%s",
					    "has not been initialized, ",
					    "type 'kgmon -i'\n");
					break;
				case PROFILE_ON:
					fprintf(stdout, "is running\n");
					break;
				case PROFILE_OFF:
					fprintf(stdout, "is off\n");
					break;
				case PROFILE_INIT:
					fprintf(stdout, "is initialized\n");
					break;
				case PROFILE_DUMP:
					fprintf(stdout, "%s%s",
					    "data has been dumped to ",
					    "the gmon.out file\n");
					break;
				case PROFILE_SNAP:
					fprintf(stdout, "%s%s",
					    "has been snap-shotted to ",
					    " the gmon.out file\n");
					break;
				case PROFILE_RESET:
					fprintf(stdout,
						"buffers have been reset\n");
					break;
				case PROFILE_DEALLOC:
					fprintf(stdout, "%s%s",
						"buffers have been ",
						"deallocated\n");
					break;
				default:
					fprintf(stdout,
						"in an unknown state = %x\n",
						onoff);
				}
			}
		}
		fflush(stdout);
		if (! just_checking) {
			dprintf("this_pr = %d\n", this_pr);
			this_pr->profiling = onoff;
			this_pr->profiling_lock = 0;
			to = (off_t)this_cpu->cpu_profiling;
			from = (char *)this_pr;
			dprintf("to %x from %x\n", to, from);
			fflush(stdout);
			if ((kvm_write(kd, (off_t)to, (char *)from,
			    sizeof (kernp_t))) != sizeof (kernp_t)) {
				fprintf(stdout,
				    "error %d writing profiling flag\n",
				    errno);
				fprintf(stdout, "from %x to %x\n",
					from, to);
				return (-1);
			}
		}
		this_cpu++;
		this_pr++;
	}
	return (0);
}


static void
ioctl_error(int code)
{
	switch (code) {
	case (EALREADY) :
		fprintf(stdout, "profiling is already initialized\n");
		break;
	case (ENOTINIT) :
		fprintf(stdout, "profiling is not initialized\n");
		break;
	case (ENOMEM) :
		fprintf(stdout, "%s%s",
			"there is not enough memory (in Sysmap?) ",
			"to allocate profiling buffers\n");
		break;
	case (EPROFILING) :
		fprintf(stdout, "profiling is on, please turn it off first\n");
		break;
	default:
		fprintf(stdout, "profile ioctl error %d\n", code);
		if (errno == ENOENT) {
			fprintf(stdout, NO_MOD1);
			fprintf(stdout, NO_MOD2);
			fprintf(stdout, NO_MOD3);
		}
	}
	exit(5);
}

static void
open_error()
{

	fprintf(stdout, "profile open error %d\n", errno);
	if (errno == ENOENT) {
		fprintf(stdout, NO_MOD1);
		fprintf(stdout, NO_MOD2);
		fprintf(stdout, NO_MOD3);
	}
	exit(5);
}

static void
get_info()
{
	int 	i;
	off_t 	off;
	int	retval;
	int	value;
	struct	cpu	*cpu_ptr;

	for (i = 0; i < MAX_CPUS; i++) {
		if ((retval = p_online(i, P_STATUS)) == P_ONLINE) {
			max_cpu_id = i;
			how_many_cpus++;
		}
	}

	dprintf("max_cpu_id = %d\n", max_cpu_id);
	dprintf("how_many_cpus = %d\n", how_many_cpus);

	if (first_cpu) {
		free(first_cpu);
		first_cpu = NULL;
	}
	if (first_pr) {
		free(first_pr);
		first_pr = NULL;
	}
	first_cpu = (struct cpu *) calloc((max_cpu_id + 1),
		sizeof (struct cpu));
	if (first_cpu == NULL) {
		fprintf(stdout, "calloc error %d for %d cpu structs\n",
			errno, max_cpu_id);
		exit(12);
	}
	first_pr = (kernp_t *) calloc((max_cpu_id + 1), sizeof (kernp_t));
	if (first_pr == NULL) {
		fprintf(stdout, "calloc error %d for %d profiling structs\n",
			errno, max_cpu_id);
		exit(12);
	}
	off = (off_t)nl[CPU_STRUCT].n_value;
	this_cpu = first_cpu;
	this_pr = first_pr;
	for (i = 0; i <= max_cpu_id; i++) {
		if ((retval = p_online(i, P_STATUS)) == P_ONLINE) {
			dprintf("cpu %d on line\n", i);
			if ((kvm_read(kd, off, (char *)&value,
			    sizeof (value))) != sizeof (value)) {
				fprintf(stdout,
				    "kgmon: get_info read error\n");
			}
			cpu_ptr = (struct cpu *)value;
			if (kvm_read(kd, (off_t)cpu_ptr, (char *)this_cpu,
			    (sizeof (struct cpu))) != sizeof (struct cpu)) {
				fprintf(stdout,
				    "kvm_read error %d reading cpu struct\n",
				    errno);
			}
			dprintf("cpu_ptr = %x i = %d this_cpus = %d ",
				cpu_ptr, i, this_cpu);
			dprintf("flags is %x ptr %x\n", this_cpu->cpu_flags,
				this_cpu->cpu_profiling);
			if (this_cpu->cpu_profiling != NULL) {
				if (kvm_read(kd, (off_t)this_cpu->cpu_profiling,
					(char *)this_pr, (sizeof (kernp_t))) !=
					sizeof (kernp_t)) {
					fprintf(stdout,
					    "kvm_read error %d reading ",
					    errno);
					fprintf(stdout, "profiling struct\n");
				}
				debug_prdump(this_pr);
			}
		} else {
			this_cpu->cpu_flags = 0;
		}
		this_cpu++;
		this_pr++;
		off += sizeof (struct cpu *);
	}
}


main(argc, argv)
int argc;
char *argv[];
{
	int	mode;
	uint	offset;
	int	disp;
	int	defined;
	int 	id;
	int	rc;
	int	retval;
	char    *cred;
	char    *data;
	char 	*cmdname;
	boolean_t	snapshotting;
	boolean_t report = B_TRUE;
	int	openmode = O_RDWR;
	int	moddebug;

	cmdname = argv[0];
	if ((kd = kvm_open(system, kmemf, NULL, openmode, cmdname))
	    == NULL) {
		fprintf(stdout, "error %d opening %s\n", errno,
		    system);
		exit(12);
	}
	if (kvm_nlist(kd, nl) < 0) {
		fprintf(stdout, "%s: no namelist\n",
		    system == NULL ? SYMBOLS : system);
		exit(13);
	}
	moddebug = (uint)nl[MODDEBUG].n_value;
	if ((id = open(PROFILER, O_RDONLY)) < 0) {
		open_error();
	}
	dprintf("sizeof cpu is %d kernp_t %d\n", sizeof (struct cpu),
		sizeof (kernp_t));
	offset = (u_int)nl[KERNELBASE].n_value;
	if ((kvm_read(kd, offset, (char *)&kernel_base, sizeof (kernel_base)))
	    != sizeof (kernel_base)) {
		fprintf(stdout, "kgmon: get_info read error\n");
	}
	dprintf("kernel_base = 0x%x\n", kernel_base);
	cred = data = (char *)NULL;
	status_only = B_FALSE;
	if (argc == 1) {
		get_info();
		status_only = B_TRUE;
		report = B_TRUE;
		mode = PROFILE_QUERY;
		turnonoff(mode, report, status_only);
		exit(16);
	}
	argc--, argv++;
	while (argc > 0 && argv[0][0] == '-') {
		get_info();
		snapshotting = B_FALSE;
		switch (argv[0][1]) {
		case 'i' :
			{
				fprintf(stdout, "one moment ");
				fflush(stdout);
				if (kvm_write(kd, moddebug,
				    (char *)&disable_unloading,
				    sizeof (int)) != sizeof (int)) {
					fprintf(stdout, "%s%s %d\n",
					    "can't disable module unloading!",
					    "results may be suspect! error ",
					    errno);
				} else {
					fprintf(stdout, "%s%s%s",
					    "\ndisabling module unloading.\n",
					    "NOTE:modules loaded after this",
					    " point will not be profiled.\n");
				}
				fprintf(stdout, "\n");
				if ((rc = ioctl(id, INIT_PROFILING, data,
				    cred, &retval)) != 0) {
					if ((kvm_write(kd, moddebug,
					    (char *)&enable_unloading,
					    sizeof (int)) != sizeof (int))
					    == 0) {
						fprintf(stdout, "%s%s",
						    "enabling module ",
						    "unloading.\n");
					}
					ioctl_error(errno);
				}
				fprintf(stdout,
					"profiling has been initialized\n");
				defined = (int)nl[MCOUNT].n_value;
				if (!defined) {
fprintf(stdout,
	"WARNING: call graph profiling is not compiled into this kernel\n");
fprintf(stdout,
	"WARNING: only clock sample profiling is available\n");
				}
				break;
			}
		case 'b':
			{
				disp = PROFILE_ON;
				report = B_TRUE;
				status_only = B_FALSE;
				turnonoff(disp, report, status_only);
				break;
			}
		case 'h':
			{
				disp = PROFILE_OFF;
				report = B_TRUE;
				status_only = B_FALSE;
				turnonoff(disp, report, status_only);
				break;
			}
		case 'r':
			{
				if ((rc = resetstate()) == 0) {
					fprintf(stdout, "%s%s",
						"profiling buffers have ",
						"been reset.\n");
				}
				break;
			}
		case 's':
			{
				snapshotting = B_TRUE;
			}
			/* FALLTHROUGH */
		case 'p':
			{
				if ((rc = dumpstate(snapshotting)) != 0) {
					fprintf(stdout,
						"profiling data not dumped\n");
				} else {
					fprintf(stdout, "%s%s",
					    "profiling data has been ",
					    "dumped to gmon[n].out\n");
				}
				break;
			}
		case 'd' :
			{
				if (kvm_write(kd, moddebug,
				    (char *)&enable_unloading,
				    sizeof (int)) != sizeof (int)) {
					fprintf(stdout,
					    "error %d setting moddebug",
					    errno);
				} else {
					fprintf(stdout,
					    "enabling module unloading.\n");
				}
				if ((rc = ioctl(id, DEALLOC_PROFILING, data,
				    cred, &retval)) != 0) {
					ioctl_error(errno);
				} else {
					fprintf(stdout, "%s%s",
					    "profiling buffers have ",
						"been deallocated\n");
				}
				break;
			}
		default:
			{
				fprintf(stdout,
				    "Usage: kgmon [ -i -b -h -r -p -s -d ]\n");
				fprintf(stdout,
				    "	-i = initialize profiling buffers\n");
				fprintf(stdout,
				    "	-b = begin profiling\n");
				fprintf(stdout,
				    "	-h = halt profiling\n");
				fprintf(stdout,
				    "	-r = reset the profiling buffers\n");
				fprintf(stdout,
				    "	-p = dump the profiling buffers\n");
				fprintf(stdout,
				    "	-s = snap shot the profiling data\n");
				fprintf(stdout,
				    "	-d = deallocate profiling buffers\n");
				exit(5);
			}
		}
		argc--, argv++;
	}
	if ((rc = close(id)) != 0) {
		fprintf(stdout, "error %d closing profile\n", errno);
	}
	kvm_close(kd);
	exit(0);
	/* NOTREACHED */
}
