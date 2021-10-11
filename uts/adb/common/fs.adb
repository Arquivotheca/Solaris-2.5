#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include <vm/seg.h>
#include <sys/fs/ufs_fs.h>

fs
./"link"16t"rlink"16t"sblkno"16t"cblkno"n{fs_link,X}{fs_rlink,X}{fs_sblkno,X}{fs_cblkno,X}n
+/"iblkno"16t"dblkno"16t"cgoffset"16t"cgmask"n{fs_iblkno,X}{fs_dblkno,X}{fs_cgoffset,X}{fs_cgmask,X}n
+/"time"n{fs_time,Y}n
+/"size"16t"dsize"16t"ncg"n{fs_size,X}{fs_dsize,X}{fs_ncg,X}n
+/"bsize"16t"fsize"16t"frag"n{fs_bsize,X}{fs_fsize,X}{fs_frag,X}n
+/"minfree"16t"rotdelay"16t"rps"n{fs_minfree,X}{fs_rotdelay,X}{fs_rps,X}n
+/"bmask"16t"fmask"16t"bshift"16t"fshift"n{fs_bmask,X}{fs_fmask,X}{fs_bshift,X}{fs_fshift,X}n
+/"maxcontig"16t"maxbpg"16t"fragshift"16t"fsbtodb"n{fs_maxcontig,X}{fs_maxbpg,X}{fs_fragshift,X}{fs_fsbtodb,X}n
+/"sbsize"16t"csmask"16t"csshift"16t"nindir"n{fs_sbsize,X}{fs_csmask,X}{fs_csshift,X}{fs_nindir,X}n
+/"inopb"16t"nspf"16t"optim"n{fs_inopb,X}{fs_nspf,X}{fs_optim,X}n
#if defined(_LITTLE_ENDIAN)
+/"state"n{fs_state,X}n
#elif defined(_BIG_ENDIAN)
+/"npsect"n{fs_npsect,X}n
#else
#error Byte ordering not defined!
#endif
+/"interleave"16t"trackskew"16t"id"n{fs_interleave,X}{fs_trackskew,X}{fs_id[0],X}{fs_id[1],X}
+/"csaddr"16t"cssize"16t"cgsize"16tn{fs_csaddr,X}{fs_cssize,X}{fs_cgsize,X}n
+/"ntrak"16t"nsect"16t"spc"16t"ncyl"n{fs_ntrak,X}{fs_nsect,X}{fs_spc,X}{fs_ncyl,X}n
+/"cpg"16t"ipg"16t"fpg"n{fs_cpg,X}{fs_ipg,X}{fs_fpg,X}n
+/"cstotal"n
.$<<csum{OFFSETOK}
+/"fmod"8t"clean"8t"ronly"8t"flags"n{fs_fmod,b}{fs_clean,b}{fs_ronly,b}{fs_flags,b}n
+/"fsmnt"n{fs_fsmnt,512C}n{END}
+/"cgrotor"16t"cpc"n{fs_cgrotor,X}{fs_cpc,X}n
#if defined(_LITTLE_ENDIAN)
+/"npsect"n{fs_npsect,X}n
#elif defined(_BIG_ENDIAN)
+/"state"n{fs_state,X}n
#else
#error Byte ordering not defined!
#endif
+/"nrpos"16t"postbloff"16t"rotbloff"16t"magic"n{fs_nrpos,X}{fs_postbloff,X}{fs_rotbloff,X}{fs_magic,X}n
