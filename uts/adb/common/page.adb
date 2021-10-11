#include <sys/types.h>
#include <sys/t_lock.h>
#include <vm/page.h>
#include <sys/sema_impl.h>

page
./"flags"8t"nrm"8t"cv"8t"mapping         selock"nB{OFFSETOK}{p_nrm,B}{p_cv,x}{p_mapping,X}{p_selock,X}
+/"vnode"16t"offset"16t"hash"n{p_vnode,X}{p_offset,X}{p_hash,X}
+/"vpnext"16t"vpprev"n{p_vpnext,X}{p_vpprev,X}
+/"iolock"
.$<<sema{OFFSETOK}
+/"next"16t"prev"n{p_next,X}{p_prev,X}
+/"lckcnt"8t"cowcnt"8t"pagenum"n{p_lckcnt,x}{p_cowcnt,x}{p_pagenum,X}
+/"fsdata"8t"vcolor"8t"index"8t"share"n{p_fsdata,B}{p_vcolor,B}{p_index,B}{p_share,x}{END}
