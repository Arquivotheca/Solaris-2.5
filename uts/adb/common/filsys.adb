#include <sys/param.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/fs/ufs_fs.h>

fs
./"link"16t"rlink"16tn{fs_link,X}{fs_rlink,X}
+/"sblkno"16t"cblkno"16t"iblkno"16t"dblkno"n{fs_sblkno,D}{fs_cblkno,D}{fs_iblkno,D}{fs_dblkno,D}
+/"cgoffset"16t"cgmask"n{fs_cgoffset,D}{fs_cgmask,X}
./"time"16tn{fs_time,Y}
+/"size"16t"dsize"16t"ncg"n{fs_size,D}{fs_dsize,D}{fs_ncg,D}
+/"bsize"16t"fsize"16t"frag"n{fs_bsize,X}{fs_fsize,X}{fs_frag,X}
+/"mod"8t"clean"8t"ronly"8t"mnt"n{fs_fmod,B}{fs_clean,B}{fs_ronly,B}{fs_fsmnt,512C}
+/"rotor"8t{fs_cgrotor,D}{END}
