#include <sys/scsi/scsi.h>
#include <sys/dkio.h>
#include <sys/scsi/targets/sddef.h>

scsi_disk
./n"devp"16t"rqs pkt"16t"rqs bp"n{un_sd,X}{un_rqs,X}{un_rqs_bp,X}
+/"rqs sema:"n
.$<<sema{OFFSETOK}
+/n"drivetype"16t"sbufp"16t"srqbufp"16t"sbuf cv"n{un_dp,X}{un_sbufp,X}{un_srqbufp,X}{un_sbuf_cv,x}
+/n"ocmap"n{un_ocmap,36B}
+/n"map"n{un_map,16X}
+/n"offset"n{un_offset,8X}
+/"geom:"n
.$<<dk_geom{OFFSETOK}
+/n"arq enabled"16t"last pkt reason"n{un_arq_enabled,B}16t{un_last_pkt_reason,B}
+/"vtoc:"n
.$<<dk_vtoc{OFFSETOK}
+/n"diskhd"n{un_utab,6X}
+/n"stats"n{un_stats,X}
+/n"oclose sema:"n
.$<<sema{OFFSETOK}
+/n"err blkno"16t"capacity"16t"lbasize"16t"secsize"n{un_err_blkno,X}{un_capacity,X}{un_lbasize,X}{un_secsize,X}
+/n"secdiv"16t"exclopen"8t"gvalid"8t"state"8t"last state"n{un_secdiv,X}{un_exclopen,B}16t{un_gvalid,B}{un_state,B}{un_last_state,B}
+/n"suspended"8t"format progress"32t"timestamp"n{un_suspended,B}16t{un_format_in_progress,B}32t{un_timestamp,X}
+/n"asciilabel"n{un_asciilabel,128c}
+/n"throttle"8t"save throttle"8t"ncmds"8t"tagflags"n{un_throttle,x}8t{un_save_throttle,x}8t{un_ncmds,x}{un_tagflags,X}
+/n"sbuf busy"8t"resvd status"16t"state cv"n{un_sbuf_busy,x}16t{un_resvd_status,x}8t{un_state_cv,x}
+/n"mediastate"16t"specified mediastate"n{un_mediastate,X}{un_specified_mediastate,X}
+/n"mhd token"16t"cmd flags"16t"cmd stat size"16t"resvd timeid"n{un_mhd_token,X}{un_cmd_flags,X}{un_cmd_stat_size,X}{un_resvd_timeid,X}
+/n"reset throttle timeid"n{un_reset_throttle_timeid,X}{END}
