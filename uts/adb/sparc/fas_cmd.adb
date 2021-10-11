#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/fascmd.h>

fas_cmd
./"pkt"n{cmd_pkt,X}
+/"forw"16t"cdbp"16t"data_count"16t"cur_addr"n{cmd_forw,X}{cmd_cdbp,X}{cmd_data_count,X}{cmd_cur_addr,X}
+/"qfull_retries"8t"nwin"8t"cur_win"8t"saved_win"n{cmd_qfull_retries,x}8t{cmd_nwin,x}{cmd_cur_win,x}{cmd_saved_win,x}
+/"sav_data_count"16t"sav_cur_addr"16t"cmd_pkt_flags"n{cmd_saved_data_count,X}{cmd_saved_cur_addr,X}{cmd_pkt_flags,X}
+/"dmahandle"16t"dmacookie"n{cmd_dmahandle,X}{cmd_dmacookie,4X}
+/"dmacount"n{cmd_dmacount,X}
+/"cdb_un"n{cmd_cdb_un,12B}
+/"flags"16t"arq status"n{cmd_flags,X}{cmd_scb,8X}
+/"scblen"8t"slot"8t"age"n{cmd_scblen,B}{cmd_slot,B}{cmd_age,B}
+/"cdblen"8t"privlen"n{cmd_cdblen,B}{cmd_privlen,B}
+/"tpkt_private"n{cmd_pkt_private,2X}
+/"tag"16t"actcdb"n{cmd_tag,2B}{cmd_actual_cdblen,B}{END}
