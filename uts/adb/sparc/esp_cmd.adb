#include <sys/scsi/scsi.h>
#include <sys/kmem.h>
#include <sys/kmem_impl.h>
#include <sys/scsi/adapters/espvar.h>
#include <sys/scsi/adapters/espreg.h>
#include <sys/scsi/adapters/espcmd.h>

esp_cmd
.$<<scsi_pkt{OFFSETOK}
+/"cmd_forw"16t"cmd_cdbp"16t"cmd_scbp"16t"cmd_flags"n{cmd_forw,X}{cmd_cdbp,X}{cmd_scbp,X}{cmd_flags,X}
+/"cmd_data_count"16t"cmd_cur_addr"16t"nwin"8t"cur_win"8t"saved_win"n{cmd_data_count,X}{cmd_cur_addr,X}{cmd_nwin,x}{cmd_cur_win,x}{cmd_saved_win,x}
+/"saved_data_cnt"16t"saved_cur_adddr"n{cmd_saved_data_count,X}{cmd_saved_cur_addr,X}
+/"cmd_dmahandle"16t"cmd_dmacookie"n{cmd_dmahandle,X}{cmd_dmacookie,4X}
+/"cmd_dmacount"16t"cmd_timeout"n{cmd_dmacount,X}{cmd_timeout,X}
+/"cmd_cdb_un"n{cmd_cdb_un,12B}
+/"cmd_pkt_private"n{cmd_pkt_private,2X}
+/"cdblen"8t"alloc"8t"qfull_retries"8t"scblen"16t"privlen"n{cmd_cdblen,B}{cmd_cdblen_alloc,B}8t{cmd_qfull_retries,B}{cmd_scblen,X}{cmd_privlen,X}
+/"cmd_scb"n{cmd_scb,32B}
+/"cmd_age"8t"cmd_tag"n{cmd_age,x}{cmd_tag,2B}{END}
