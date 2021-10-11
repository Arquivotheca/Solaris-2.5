#include <sys/param.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#include <vm/seg.h>
#include <vm/seg_dev.h>

segdev_data
./"lock"
.$<<mutex{OFFSETOK}
+/"mapfunc"16t"offset"16t"vnode"n{mapfunc,p}{offset,X}{vp,X}
+/"pageprot"8t"prot"8t"maxprot"8t"vpage"n{pageprot,b}16t{prot,b}{maxprot,b}{vpage,X}
