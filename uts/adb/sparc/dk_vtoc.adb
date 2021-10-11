#ifndef _KERNEL
#define _KERNEL
#endif
#include <sys/scsi/scsi.h>
#include <sys/dklabel.h>

dk_vtoc
./n"version"16t"volume"n{v_version,X}{v_volume,8c}
+/n"nparts"n{v_nparts,x}
+/n"part"n{v_part,16x}
+/n"bootinfo"64t"sanity"n{v_bootinfo,3X}{v_sanity,X}
+/n"reserved"n{v_reserved,10X}
+/n"timestamp"n{v_timestamp,8X}{END}
