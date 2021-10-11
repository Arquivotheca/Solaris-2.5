#include <sys/types.h>
#include <sys/dditypes.h>
#include <sys/ddidmareq.h>
#include <sys/ddi_impldefs.h>

dev_info
.>t
{*devi_name,<t}/"name:"8ts
<t/"parent"16t"child"16t"sibling"n{devi_parent,X}{devi_child,X}{devi_sibling,X}
+/"addr"16t"nodeid"16t"instance"n{devi_addr,X}{devi_nodeid,X}{devi_instance,X}
+/"ops"16t"pdata"16t"ddata"n{devi_ops,p}{devi_parent_data,X}{devi_driver_data,X}
+/"drvprop"16t"sysprop"16t"minor"n{devi_drv_prop_ptr,X}{devi_sys_prop_ptr,X}{devi_minor,X}
+/"next"n{devi_next,X}{END}
