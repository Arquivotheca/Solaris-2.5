#define FASDEBUG
#include <sys/note.h>
#include <sys/scsi/scsi.h>
#include <sys/scsi/adapters/fasreg.h>
#include <sys/scsi/adapters/fasvar.h>

fas
./"instance"16t"tran"16t"dev"n{f_instance,X}{f_tran,X}{f_dev,X}
+/"fas mutex:"
.$<<mutex{OFFSETOK}
+/"iblock"16t"next"16t"type"8t"dmarev"8t"conf"8t"conf2"n{f_iblock,X}{f_next,X}{f_type,B}{f_dma_rev,B}{f_fasconf,B}{f_fasconf2,B}
+/"conf3"n{f_fasconf3,16B}
+/"conf3_l"8t"conv"8t"cycle"n{f_fasconf3_reg_last,B}{f_clock_conv,B}{f_clock_cycle,x}
+/"stval"8t"sdtr"8t"wdtr"8t"stat"8t"stat2"n{f_stval,B}{f_sdtr_sent,B}{f_wdtr_sent,B}{f_stat,B}{f_stat2,B}
+/"intr"8t"step"8t"abort"8t"reset"n{f_intr,B}{f_step,B}{f_abort_msg_sent,B}{f_reset_msg_sent,B}
+/"lastcmd"8t"state"8t"lstate"8t"susp"8t"dslot"8t"idcode"n{f_last_cmd,B}{f_state,x}{f_laststate,x}{f_suspended,B}{f_dslot,B}{f_idcode,B}
+/"cur_msgout"n{f_cur_msgout,12B}
+/"lastmsg"8t"omsgln"8tn{f_last_msgout,B}{f_omsglen,B}
+/"imsg"n{f_imsgarea,8B}
+/"imsgln"8t"index"8t"lastmsg"n{f_imsglen,B}{f_imsgindex,B}{f_last_msgin,B}
+/"nxtslot"8t"resel slot"n{f_next_slot,B}{f_resel_slot,B}
+/"offset"n{f_offset,16B}
+/"period"n{f_sync_period,16B}
+/"neg_period"n{f_neg_period,16B}
+/"backoff"8t"reqack"8t"loffset"8t"lperiod"8t"fifolen"n{f_backoff,x}{f_req_ack_delay,B}{f_offset_reg_last,B}{f_period_reg_last,B}{f_fifolen,B}
+/"fifo"n{f_fifo,32B}
+/"widekn"8t"nowide"8t"wide_enabled"n{f_wide_known,x}{f_nowide,x}{f_wide_enabled,x}
+/"synckn"8t"nosync"8t"sync_enabled"16t"notag"8t"props"n{f_sync_known,x}{f_nosync,x}{f_sync_enabled,x}8t{f_notag,x}{f_props_update,x}
+/"scsi_opt"n{f_scsi_options,X}
+/"tgt scsi options"n{f_target_scsi_options,16X}
+/"tag age lim"16t"rst delay"16t"cmd area"n{f_scsi_tag_age_limit,X}{f_scsi_reset_delay,X}{f_cmdarea,X}
+/"dma csr"16t"dma cookie"n{f_dma_csr,X}{f_dmacookie,4X}
+/"dma hndle"16t"fasattr"16t"ncmds"8t"ndisc"n{f_dmahandle,X}{f_dma_attr,X}{f_ncmds,x}{f_ndisc,x}
+/"fasreg"16t"dmareg"16t"lastdma"16t"lastcount"n{f_reg,X}{f_dma,X}{f_lastdma,X}{f_lastcount,X}
+/"current sp"n{f_current_sp,X}
+/"active slots"n{f_active,128X}
+/"readyf"n{f_readyf,128X}
+/"readyb"n{f_readyb,128X}
+/"throttle"n{f_throttle,128x}
+/"tcmds"n{f_tcmds,128x}
+/"reset delay"n{f_reset_delay,16X}
+/"arq pkt ptrs"n{f_arq_pkt,128X}
+/"cb info"n{f_callback_info,X}
+/"waitQ mutex:"
.$<<mutex{OFFSETOK}
+/"waitf"16t"waitb"n{f_waitf,X}{f_waitb,X}
+/"reset notification"n{f_reset_notify_listf,X}
+/"qfull retries"n{f_qfull_retries,16B}
+/"qfull retry interval"n{f_qfull_retry_interval,16x}
+/"restart"16t"kmem cache"n{f_restart_cmd_timeid,X}{f_kmem_cache,X}
+/"reg_trace_index"n{f_reg_trace_index,X}
+/"reg access trace"n{f_reg_trace,1025X}{END}
