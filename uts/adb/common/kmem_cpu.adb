#include <sys/kmem.h>
#include <sys/kmem_impl.h>

kmem_cpu_cache
./n"lock"
+$<<mutex{OFFSETOK}
+/n"alloc"16t"free"16t"rounds"16t"magsize"n{cc_alloc,D}{cc_free,D}{cc_rounds,D}{cc_magsize,D}
+/n"loaded_mag"16t"full_mag"16t"empty_mag"n{cc_loaded_mag,X}{cc_full_mag,X}{cc_empty_mag,X}
