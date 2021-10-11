#include <sys/modctl.h>

modinfo
./"mi_id"16t"mi_nextid"16t"base_addr"n{mi_id,D}{mi_nextid,D}{mi_base,X}
+/"mi_size"16t"mi_rev"16t"mi_name"n{mi_size,U}{mi_rev,D}{mi_name,32C}
+/"mods_info"n{mi_msinfo[0].msi_linkinfo,32C}
