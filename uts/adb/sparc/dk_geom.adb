#ifndef _KERNEL
#define _KERNEL
#endif
#include <sys/scsi/scsi.h>
#include <sys/dkio.h>

dk_geom
./n"ncyl"8t"acyl"8t"bcyl"8t"nhead"8t"bhead"n{dkg_ncyl,x}{dkg_acyl,x}{dkg_bcyl,x}{dkg_nhead,x}{dkg_obs1,x}
+/n"nsect"8t"intrlv"8t"gap1"8t"gap2"8t"apc"n{dkg_nsect,x}{dkg_intrlv,x}{dkg_obs2,x}{dkg_obs3,x}{dkg_apc,x}
+/n"rpm"8t"pcyl"8t"write reinstruct"8t"read reinstruct"n{dkg_rpm,x}{dkg_pcyl,x}{dkg_write_reinstruct,x}32t{dkg_read_reinstruct,x}
+/n"extra"n{dkg_extra,7x}{END}
