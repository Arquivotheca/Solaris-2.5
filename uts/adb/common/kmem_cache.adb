#include <sys/kmem.h>
#include <sys/kmem_impl.h>

kmem_cache
./n"lock"
+$<<mutex{OFFSETOK}
+/n"flags"16t"freelist"n{cache_flags,X}{cache_freelist,X}
+/n"offset"16t"alloc"16t"alloc_fail"n{cache_offset,D}{cache_alloc,D}{cache_alloc_fail,D}
+/n"hash_shift"16t"hash_mask"16t"hash_table"n{cache_hash_shift,X}{cache_hash_mask,X}{cache_hash_table,X}
+/n"nullslab"
+$<<slab{OFFSETOK}
+/n"constructor"16t"destructor"16t"reclaim"n{cache_constructor,p}{cache_destructor,p}{cache_reclaim,p}
+/n"bufsize"16t"align"16t"chunksize"16t"slabsize"n{cache_bufsize,D}{cache_align,D}{cache_chunksize,D}{cache_slabsize,D}
+/n"color"16t"maxcolor"16t"slab_create"16t"slab_destroy"n{cache_color,D}{cache_maxcolor,D}{cache_slab_create,D}{cache_slab_destroy,D}
+/n"buftotal"16t"bufmax"16t"rescale"16t"lookup_depth"n{cache_buftotal,D}{cache_bufmax,D}{cache_rescale,D}{cache_lookup_depth,D}
+/n"kstat"16t"cv"16t"next"16t"prev"n{cache_kstat,X}{cache_cv,x}16t{cache_next,X}{cache_prev,X}
+/n"name"n{cache_name,32c}
+/n"magazine_cache"16t"magazine_size"16t"magazine_max"n{cache_magazine_cache,X}{cache_magazine_size,D}{cache_magazine_maxsize,D}
+/n"depot_lock"
+$<<mutex{OFFSETOK}
+/n"cpu_rotor"16t"ncpus"16t"depot_cont"16t"depot_last"n{cache_cpu_rotor,D}{cache_ncpus,D}{cache_depot_contention,D}{cache_depot_contention_last,D}
+/n"depot_alloc"16t"depot_free"n{cache_depot_alloc,D}{cache_depot_free,D}
+/n"fmag_list"16t"fmag_total"16t"fmag_min"16t"fmag_reaplimit"n{cache_fmag_list,X}{cache_fmag_total,D}{cache_fmag_min,D}{cache_fmag_reaplimit,D}
+/n"emag_list"16t"emag_total"16t"emag_min"16t"emag_reaplimit"n{cache_emag_list,X}{cache_emag_total,D}{cache_emag_min,D}{cache_emag_reaplimit,D}
+>k
*ncpus>n
$<kmem_cpu.nxt{OFFSETOK}
