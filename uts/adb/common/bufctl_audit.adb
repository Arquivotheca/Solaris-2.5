#include <sys/kmem.h>
#include <sys/kmem_impl.h>

kmem_bufctl_audit
./n"next"16t"addr"16t"slab"16t"cache"n{bc_next,X}{bc_addr,X}{bc_slab,X}{bc_cache,X}
+/n"lastlog"16t"thread"16t"timestamp"n{bc_lastlog,X}{bc_thread,X}{bc_timestamp,2X}
+/n"stackdepth"n{bc_depth,X}
+/n{bc_stack,15p}
