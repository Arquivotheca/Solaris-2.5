#include <sys/elf_notes.h>

#pragma ident  "@(#)notes.s  1.1     95/02/10 SMI"
 
#ifndef lint

!
! Sun defined names for modules to be packed
!

#define	PACKSPECFS	"specfs"
#define PACKTS		"TS"
#define PACKTS_DPTBL	"TS_DPTBL"
#define PACKRPCMOD	"rpcmod"
#define PACKIP		"ip"
#define PACKROOTNEX	"rootnex"
#define PACKOPTIONS	"options"
#define PACKDMA		"dma"
#define PACKSBUS	"sbus"
#define PACKIOMMU	"iommu"
#define PACKSAD		"sad"
#define PACKPSEUDO	"pseudo"
#define PACKESP		"esp"
#define PACKSCSI	"scsi"
#define PACKPROCFS	"procfs"
#define PACKCLONE	"clone"
#define PACKTCP		"tcp"
#define PACKUDP		"udp"
#define PACKICMP	"icmp"
#define PACKARP		"arp"
#define PACKTIMOD	"timod"
#define PACKZS		"zs"
#define PACKOBIO	"obio"
#define PACKMS		"ms"
#define PACKCONSMS	"consms"
#define PACKKB		"kb"
#define PACKCONSKBD	"conskbd"
#define PACKWC		"wc"
#define PACKELFEXEC	"elfexec"
#define PACKMM		"mm"
#define PACKFIFOFS	"fifofs"
#define PACKLDTERM	"ldterm"
#define PACKTTCOMPAT	"ttcompat"
#define PACKPTSL	"ptsl"
#define PACKPTC		"ptc"
#define PACKCN		"cn"
#define PACKINTPEXEC	"intpexec"
#define PACKSOCKMOD	"sockmod"
#define PACKPIPE	"pipe"
#define PACKLE		"le"
#define PACKLEDMA	"ledma"
#define PACKFDFS	"fdfs"
#define PACKTMPFS	"tmpfs"
#define PACKDOORFS	"doorfs"
#define PACKLOG		"log"
#define PACKKSTAT	"kstat"
#define PACKTL		"tl"
#define PACKSEMSYS	"semsys"
#define PACKIPC		"ipc"
#define PACKTLIMOD	"tlimod"
#define PACKAUTOFS	"autofs"
#define PACKNAMEFS	"namefs"
#define PACKCONNLD	"connld"
#define PACKSY		"sy"
#define PACKPTM		"ptm"
#define PACKPTS		"pts"
#define PACKPTEM	"ptem"
#define PACKREDIRMOD	"redirmod"
#define PACKBUFMOD	"bufmod"
#define PACKPCKT	"pckt"
#define PACKIA		"IA"
#define PACKSEGMAPDEV	"seg_mapdev"

!
! Define the size for the packing pool
!
	.section        ".note"
	.align          4
	.word           .name1_end - .name1_begin ! namesz
	.word           .desc1_end - .desc1_begin ! descsz
	.word		ELF_NOTE_MODULE_SIZE
.name1_begin:
	.asciz          ELF_NOTE_PACKSIZE
.name1_end:
	.align          4
!
! The size is 186 pages
!
.desc1_begin:
	.word		0xba
.desc1_end:
	.align		4
!
! Tag a group of modules as necessary for packing
!
	.section        ".note"
	.align          4
	.word           .name2_end - .name2_begin ! namesz
	.word           .desc2_end - .desc2_begin ! descsz
	.word		ELF_NOTE_MODULE_PACKING
.name2_begin:
	.asciz          ELF_NOTE_PACKHINT
.name2_end:
	.align          4
!
! These are the modules defined above
!
.desc2_begin:
	.asciz	PACKSPECFS
	.asciz	PACKTS	
	.asciz	PACKTS_DPTBL
	.asciz	PACKRPCMOD
	.asciz	PACKIP
	.asciz	PACKROOTNEX
	.asciz	PACKOPTIONS
	.asciz	PACKDMA	
	.asciz	PACKSBUS
	.asciz	PACKIOMMU
	.asciz	PACKESP
	.asciz	PACKSCSI
	.asciz	PACKPROCFS
	.asciz	PACKSAD
	.asciz	PACKPSEUDO
	.asciz	PACKCLONE
	.asciz	PACKTCP
	.asciz	PACKUDP
	.asciz	PACKICMP
	.asciz	PACKARP
	.asciz	PACKTIMOD
	.asciz	PACKZS
	.asciz	PACKOBIO
	.asciz	PACKMS
	.asciz	PACKCONSMS
	.asciz	PACKKB
	.asciz	PACKCONSKBD
	.asciz	PACKWC
	.asciz	PACKELFEXEC
	.asciz	PACKMM
	.asciz	PACKFIFOFS
	.asciz	PACKLDTERM
	.asciz	PACKTTCOMPAT
	.asciz	PACKPTSL
	.asciz	PACKPTC
	.asciz	PACKCN
	.asciz	PACKINTPEXEC
	.asciz	PACKSOCKMOD
	.asciz	PACKPIPE
	.asciz	PACKLE
	.asciz	PACKLEDMA
	.asciz	PACKFDFS
	.asciz	PACKTMPFS
	.asciz	PACKDOORFS
	.asciz	PACKLOG
	.asciz	PACKKSTAT
	.asciz	PACKTL
	.asciz	PACKSEMSYS
	.asciz	PACKIPC
	.asciz	PACKTLIMOD
	.asciz	PACKAUTOFS
	.asciz	PACKNAMEFS
	.asciz	PACKCONNLD
	.asciz	PACKSY
	.asciz	PACKPTM
	.asciz	PACKPTS
	.asciz	PACKPTEM
	.asciz	PACKREDIRMOD
	.asciz	PACKBUFMOD
	.asciz	PACKPCKT
	.asciz	PACKIA
	.asciz	PACKSEGMAPDEV
.desc2_end:
	.align          4
 
#endif  /* lint */

