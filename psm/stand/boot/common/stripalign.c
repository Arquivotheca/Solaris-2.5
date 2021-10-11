#ident	"@(#)stripalign.c	1.11	92/05/20 SMI"

/*
 * Copyright (c) 1992 by Sun Microsystems, Inc
 */

/*
 * inetboot contains a fake a.out header in srtinet.o,
 * so this program does NOT write an a.out header.  It basically
 * strips the elf stuff and writes the file so it looks like an
 * a.out file, with no real size information in the header.
 */

#include <sys/exechdr.h>
#include <sys/elf.h>
#include <sys/fcntl.h>

Elf32_Ehdr elfhdr;
Elf32_Shdr shdr;
Elf32_Phdr phdr, dphdr;
char machine[] = {0, 0, 3, 0, 2};
char strtab[] = "\0.text\0.data\0.bss\0.shstrtab";
int fd;
int sizestr = 28;
char buf[4096];
long lalignbuf[2];

main(argc, argv)
	int argc;
	char **argv;
{
	int ifd;
	struct exec exec;
	int count;
	int tot_write = 0;
	long lalign;
	int lbytes;
	int text_written = 0;
	int data_written = 0;
	char *prog = *argv;

	if (argc < 3) {
		printf("usage: mkaout elf_file a.outfile \n");
		exit(1);
	}
	if ((ifd = open(argv[1], O_RDONLY)) ==  -1) {
		perror("open input");
		exit(1);
	}
	if (read(ifd, &elfhdr, sizeof(elfhdr)) < sizeof(elfhdr)) {
		perror("read elfhdr");
		exit(1);
	}
	if ((fd = open(argv[2], O_RDWR | O_TRUNC | O_CREAT, 0777)) ==  -1) {
		perror("open aout");
		exit(1);
	}

	if (*(int *)(elfhdr.e_ident) != *(int *)(ELFMAG)){
		perror("elfmag");
		exit(1);
	}
	if (lseek(ifd, elfhdr.e_phoff, 0) == -1) {
		perror("lseek");
		exit(1);
	}
	if (read(ifd, &phdr, sizeof(phdr)) < sizeof(phdr)) {
		perror("read phdr");
		exit(1);
	}
	if (read(ifd, &dphdr, sizeof(dphdr)) < sizeof(dphdr)) {
		perror("read dphdr");
		exit(1);
	}

	exec.a_toolversion = 1;
	exec.a_machtype = machine[elfhdr.e_machine];
	exec.a_magic = OMAGIC;
	exec.a_text = dphdr.p_vaddr - phdr.p_vaddr;
	exec.a_data = dphdr.p_filesz;
	exec.a_bss = dphdr.p_memsz - dphdr.p_filesz;
	exec.a_entry = elfhdr.e_entry;

#ifdef	notdef
	if (write(fd, &exec, sizeof(exec)) != sizeof(exec)) {
		perror("write exec");	
		exit(1);
	}
	tot_write += sizeof(exec);
#endif	notdef

	if (lseek(ifd, phdr.p_offset, 0) == -1) {
		perror("lseek text");
		exit(1);
	}
	/* do text section first */
	while (text_written < exec.a_text && 
	    (count = read(ifd, buf, sizeof(buf))) > 0) {
		if (count > exec.a_text - text_written)
			count = (exec.a_text - text_written);
		if (write(fd, buf, count) < count) {
			perror("write text file");
			exit(1);
		}
		text_written += count;
	}
	tot_write += text_written;
	if (lseek(ifd, dphdr.p_offset, 0) == -1) {
		perror("lseek data");
		exit(1);
	}
	while (data_written < exec.a_data && 
	    (count = read(ifd, buf, sizeof(buf))) > 0) {
		if (count > exec.a_data - data_written)
			count = (exec.a_data - data_written);
		if (write(fd, buf, count) < count) {
			perror("write data file");
			exit(1);
		}
		data_written += count;
	}
	tot_write += data_written;

	/*
	 * Round file size out to long word boundary.
 	 * If the longword boundary is a multiple of 512 bytes,
 	 * add another longword.
	 */
	lbytes = 0;
 	if ((lalign = tot_write % sizeof (long)) != 0)
		lbytes = sizeof(long) - lalign;
 	if (((lbytes + tot_write) % 512) == 0)
 		lbytes += sizeof (long);
 
 	if (lbytes != 0) {
		printf("%s: (Align) %d + %d = %d bytes\n", prog,
		    tot_write, lbytes, (tot_write + lbytes));
 		if (write(fd, (char *)lalignbuf, lbytes) < lbytes) {
 			perror("write (alignment) data file");
			exit(1);
		}
	}
	close(fd);
	close(ifd);
	exit(0);
}
