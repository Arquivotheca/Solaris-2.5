#include <sys/param.h>
#include <sys/types.h>
#include <sys/bootconf.h>

bootobj
./"fstype"16t{bo_fstype,16C}
+/"name"16t{bo_name,128C}
+/"flags"16t"size"16t"vp"n{bo_flags,X}{bo_size,X}{bo_vp,X}{END}
