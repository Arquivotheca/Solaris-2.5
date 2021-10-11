#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_inode.h>

ufsvfs
./"next"16t"root"16t"bufp"16t"devvp"n{vfs_next,X}{vfs_root,X}{vfs_bufp,X}{vfs_devvp,X}n
+/"qinod"16t"qflags"16t"btimelimit"16t"ftimelimit"n{vfs_qinod,X}{vfs_qflags,x}16t{vfs_btimelimit,X}{vfs_ftimelimit,X}n
+/"delete thread"
.$<<ufsq{OFFSETOK}
+/"reclaim thread"
.$<<ufsq{OFFSETOK}
+/"nrpos"16t"npsect"16t"interleave"16t"trackskew"n{vfs_nrpos,X}{vfs_npsect,X}{vfs_interleave,X}{vfs_trackskew,X}n
+/"vfs_lock"n
.$<<mutex{OFFSETOK}
+/"ulockfs"n
.$<<ulockfs{OFFSETOK}
+/"dio"16t"nointr"16t"nosetsec"16t"syncdir"n{vfs_dio,X}{vfs_nointr,X}{vfs_nosetsec,X}{vfs_syncdir,X}n
+/"trans"16t"domatamap"16t"maxacl"16t"smallwrite"n{vfs_trans,X}{vfs_domatamap,X}{vfs_maxacl,X}{vfs_smallwrite,X}n
+/"maxwrite"16t"dirsize"n{vfs_maxwrite,X}{vfs_dirsize,X}n
+/"nindirshift"16t"nindiroffset"16t"rdclustsz"16t"wrclustsz"n{vfs_nindirshift,X}{vfs_nindiroffset,X}{vfs_rdclustsz,X}{vfs_wrclustsz,X}n
