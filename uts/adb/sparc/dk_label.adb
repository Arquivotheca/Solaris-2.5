#ifndef _KERNEL
#define _KERNEL
#endif
#include <sys/scsi/scsi.h>
#include <sys/dklabel.h>

dk_label
./n"asciilabel"n{dkl_asciilabel,128c}
+$<<dk_vtoc{OFFSETOK}
+/n"write reinstruct"16t"read reinstruct"n{dkl_write_reinstruct,x}48t{dkl_read_reinstruct,x}
+/n"pad"n{dkl_pad,152B}
+/n"rpm"8t"pcyl"8t"apc"8t"gap1"8t"gap2"n{dkl_rpm,x}{dkl_pcyl,x}{dkl_apc,x}{dkl_gap1,x}{dkl_gap2,x}
+/n"intrlv"8t"ncyl"8t"acyl"8t"nhead"n{dkl_intrlv,x}{dkl_ncyl,x}{dkl_acyl,x}{dkl_nhead,x}
+/n"nsect"8t"bhead"8t"ppart"n{dkl_nsect,x}{dkl_bhead,x}{dkl_ppart,x}
+/n"map"n{dkl_map,16X}
+/n"magic"8t"cksum"n{dkl_magic,x}{dkl_cksum,x}{END}
