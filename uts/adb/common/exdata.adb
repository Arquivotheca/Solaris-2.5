#ifndef _KERNEL
#define _KERNEL
#endif
#include <sys/user.h>

exdata
./n"vp"16t"tsize"16t"dsize"16t"bsize"n{vp,X}{ux_tsize,X}{ux_dsize,X}{ux_bsize,X}
+/n"lsize"16t"nshlibs"16t"mach"8t"mag"8t"toffset"n{ux_lsize,X}{ux_nshlibs,U}{ux_mach,x}{ux_mag,x}{ux_toffset,X}
+/n"doffset"16t"loffset"16t"txtorg"16t"datorg"n{ux_doffset,X}{ux_loffset,X}{ux_txtorg,X}{ux_datorg,X}
+/n"entloc"n{ux_entloc,X}{END}
