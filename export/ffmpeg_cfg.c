/* ------------------------------------------------------------
 *
 * read ffmpeg configuration parameters from a file
 *
 * ------------------------------------------------------------*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#include "ffmpeg_cfg.h"

/*
 * Default values as taken from MPlayer's libmpcodecs/ve_lavc.c
 */

/* video options */
//char *lavc_param_vcodec = "mpeg4";
//int lavc_param_vbitrate = -1;
int lavc_param_vrate_tolerance = 1000*8;
int lavc_param_mb_decision = 0;
int lavc_param_v4mv = 0;
int lavc_param_vme = 4;
//int lavc_param_vqscale = 0;
//int lavc_param_vqmin = 2;
//int lavc_param_vqmax = 31;
int lavc_param_mb_qmin = 2;
int lavc_param_mb_qmax = 31;
int lavc_param_lmin = 2;
int lavc_param_lmax = 31;
int lavc_param_vqdiff = 3;
float lavc_param_vqcompress = 0.5;
float lavc_param_vqblur = 0.5;
float lavc_param_vb_qfactor = 1.25;
float lavc_param_vb_qoffset = 1.25;
float lavc_param_vi_qfactor = 0.8;
float lavc_param_vi_qoffset = 0.0;
int lavc_param_vmax_b_frames = 0;
//int lavc_param_keyint = -1;
//int lavc_param_vpass = 0;
int lavc_param_vrc_strategy = 2;
int lavc_param_vb_strategy = 0;
int lavc_param_luma_elim_threshold = 0;
int lavc_param_chroma_elim_threshold = 0;
int lavc_param_packet_size= 0;
int lavc_param_strict= 0;
int lavc_param_data_partitioning= 0;
int lavc_param_gray=0;
float lavc_param_rc_qsquish=1.0;
float lavc_param_rc_qmod_amp=0;
int lavc_param_rc_qmod_freq=0;
char *lavc_param_rc_override_string=NULL;
char *lavc_param_rc_eq="tex^qComp";
int lavc_param_rc_buffer_size=0;
float lavc_param_rc_buffer_aggressivity=1.0;
int lavc_param_rc_max_rate=0;
int lavc_param_rc_min_rate=0;
float lavc_param_rc_initial_cplx=0.0;
int lavc_param_mpeg_quant=0;
int lavc_param_fdct=0;
int lavc_param_idct=0;
float lavc_param_lumi_masking= 0.0;
float lavc_param_dark_masking= 0.0;
float lavc_param_temporal_cplx_masking= 0.0;
float lavc_param_spatial_cplx_masking= 0.0;
float lavc_param_p_masking= 0.0;
int lavc_param_normalize_aqp= 0;
//int lavc_param_interlaced_dct= 0;
int lavc_param_prediction_method= FF_PRED_LEFT;
char *lavc_param_format="YV12";
int lavc_param_debug= 0;
int lavc_param_psnr= 0;
int lavc_param_me_pre_cmp= 0;
int lavc_param_me_cmp= 0;
int lavc_param_me_sub_cmp= 0;
int lavc_param_mb_cmp= 0;
int lavc_param_ildct_cmp= FF_CMP_VSAD;
int lavc_param_pre_dia_size= 0;
int lavc_param_dia_size= 0;
int lavc_param_qpel= 0;
int lavc_param_trell= 0;
int lavc_param_aic=0;
int lavc_param_umv=0;
int lavc_param_last_pred= 0;
int lavc_param_pre_me= 1;
int lavc_param_me_subpel_quality= 8;
int lavc_param_me_range=0;
int lavc_param_ibias=FF_DEFAULT_QUANT_BIAS;
int lavc_param_pbias=FF_DEFAULT_QUANT_BIAS;
int lavc_param_coder=0;
int lavc_param_context=0;
char *lavc_param_intra_matrix = NULL;
char *lavc_param_inter_matrix = NULL;
int lavc_param_cbp= 0;
int lavc_param_mv0= 0;
int lavc_param_noise_reduction= 0;
int lavc_param_qp_rd= 0;
int lavc_param_inter_threshold= 0;
int lavc_param_sc_threshold= 0;
int lavc_param_ss= 0;
int lavc_param_top= -1;
int lavc_param_alt= 0;
int lavc_param_ilme= 0;

int lavc_param_scan_offset = 0;
int lavc_param_threads = 1;
int lavc_param_intra_dc_precision = 0;
int lavc_param_skip_top = 0;
int lavc_param_fps_code = 0;
int lavc_param_skip_bottom = 0;
int lavc_param_closedgop = 0;
int lavc_param_trunc = 0;
int lavc_param_gmc = 0;

//char *lavc_param_acodec = "mp2";
//int lavc_param_atag = 0;
//int lavc_param_abitrate = 224;

char *lavc_param_video_preset = "medium";
char *lavc_param_ffmpeg_datadir = "/usr/share/ffmpeg";

TCConfigEntry lavcopts_conf[]={
//    {"acodec", &lavc_param_acodec, TCCONF_TYPE_STRING, 0, 0, 0},
//    {"abitrate", &lavc_param_abitrate, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 1000},
//    {"atag", &lavc_param_atag, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 0xffff},
//    {"vcodec", &lavc_param_vcodec, TCCONF_TYPE_STRING, 0, 0, 0},
//    {"vbitrate", &lavc_param_vbitrate, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 4, 24000000},
    {"vratetol", &lavc_param_vrate_tolerance, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 4, 24000000},
    {"vhq", &lavc_param_mb_decision, TCCONF_TYPE_FLAG, 0, 0, 1},
    {"mbd", &lavc_param_mb_decision, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 9},
    {"v4mv", &lavc_param_v4mv, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_4MV},
    {"vme", &lavc_param_vme, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 5},
//    {"vqscale", &lavc_param_vqscale, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
//    {"vqmin", &lavc_param_vqmin, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
//    {"vqmax", &lavc_param_vqmax, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
    {"mbqmin", &lavc_param_mb_qmin, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
    {"mbqmax", &lavc_param_mb_qmax, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
    {"lmin", &lavc_param_lmin, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.01, 255.0},
    {"lmax", &lavc_param_lmax, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.01, 255.0},
    {"vqdiff", &lavc_param_vqdiff, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 31},
    {"vqcomp", &lavc_param_vqcompress, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 1.0},
    {"vqblur", &lavc_param_vqblur, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 1.0},
    {"vb_qfactor", &lavc_param_vb_qfactor, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, -31.0, 31.0},
    {"vmax_b_frames", &lavc_param_vmax_b_frames, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, FF_MAX_B_FRAMES},
//    {"vpass", &lavc_param_vpass, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2},
    {"vrc_strategy", &lavc_param_vrc_strategy, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2},
    {"vb_strategy", &lavc_param_vb_strategy, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 10},
    {"vb_qoffset", &lavc_param_vb_qoffset, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 31.0},
    {"vlelim", &lavc_param_luma_elim_threshold, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -99, 99},
    {"vcelim", &lavc_param_chroma_elim_threshold, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -99, 99},
    {"vpsize", &lavc_param_packet_size, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100000000},
    {"vstrict", &lavc_param_strict, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -99, 99},
    {"vdpart", &lavc_param_data_partitioning, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_PART},
//    {"keyint", &lavc_param_keyint, TCCONF_TYPE_INT, 0, 0, 0},
    {"gray", &lavc_param_gray, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_PART},
    {"mpeg_quant", &lavc_param_mpeg_quant, TCCONF_TYPE_FLAG, 0, 0, 1},
    {"vi_qfactor", &lavc_param_vi_qfactor, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, -31.0, 31.0},
    {"vi_qoffset", &lavc_param_vi_qoffset, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 31.0},
    {"vqsquish", &lavc_param_rc_qsquish, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 99.0},
    {"vqmod_amp", &lavc_param_rc_qmod_amp, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 99.0},
    {"vqmod_freq", &lavc_param_rc_qmod_freq, TCCONF_TYPE_INT, 0, 0, 0},
    {"vrc_eq", &lavc_param_rc_eq, TCCONF_TYPE_STRING, 0, 0, 0},
    {"vrc_override", &lavc_param_rc_override_string, TCCONF_TYPE_STRING, 0, 0, 0},
    {"vrc_maxrate", &lavc_param_rc_max_rate, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 24000000},
    {"vrc_minrate", &lavc_param_rc_min_rate, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 24000000},
    {"vrc_buf_size", &lavc_param_rc_buffer_size, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 4, 24000000},
    {"vrc_buf_aggressivity", &lavc_param_rc_buffer_aggressivity, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 99.0},
    {"vrc_init_cplx", &lavc_param_rc_initial_cplx, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, 0.0, 9999999.0},
    {"vfdct", &lavc_param_fdct, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 10},
    {"lumi_mask", &lavc_param_lumi_masking, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, -1.0, 1.0},
    {"tcplx_mask", &lavc_param_temporal_cplx_masking, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, -1.0, 1.0},
    {"scplx_mask", &lavc_param_spatial_cplx_masking, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, -1.0, 1.0},
    {"p_mask", &lavc_param_p_masking, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, -1.0, 1.0},
    {"naq", &lavc_param_normalize_aqp, TCCONF_TYPE_FLAG, 0, 0, 1},
    {"dark_mask", &lavc_param_dark_masking, TCCONF_TYPE_FLOAT, TCCONF_FLAG_RANGE, -1.0, 1.0},
    //{"ildct", &lavc_param_interlaced_dct, TCCONF_TYPE_FLAG, 0, 0, 1},
    {"idct", &lavc_param_idct, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 20},
    {"pred", &lavc_param_prediction_method, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 20},
    {"format", &lavc_param_format, TCCONF_TYPE_STRING, 0, 0, 0},
    {"debug", &lavc_param_debug, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 100000000},
    {"psnr", &lavc_param_psnr, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_PSNR},
    {"precmp", &lavc_param_me_pre_cmp, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000},
    {"cmp", &lavc_param_me_cmp, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000},
    {"subcmp", &lavc_param_me_sub_cmp, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000},
    {"mbcmp", &lavc_param_mb_cmp, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000},
    {"ildctcmp", &lavc_param_ildct_cmp, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000},
    {"predia", &lavc_param_pre_dia_size, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -2000, 2000},
    {"dia", &lavc_param_dia_size, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -2000, 2000},
    {"qpel", &lavc_param_qpel, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_QPEL},
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)   
    {"trell", &lavc_param_trell, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_TRELLIS_QUANT},
#else
    {"trell", &lavc_param_trell, TCCONF_TYPE_FLAG, 0, 0, 1},
#endif
    {"last_pred", &lavc_param_last_pred, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000},
    {"preme", &lavc_param_pre_me, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 2000},
    {"subq", &lavc_param_me_subpel_quality, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 8},
    {"me_range", &lavc_param_me_range, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 16000},
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)   
    {"aic", &lavc_param_aic, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_H263P_AIC},
#else
    {"aic", &lavc_param_aic, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_AC_PRED},
#endif    
    {"umv", &lavc_param_umv, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_H263P_UMV},
    {"ibias", &lavc_param_ibias, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -512, 512},
    {"pbias", &lavc_param_pbias, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -512, 512},
    {"coder", &lavc_param_coder, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 10},
    {"context", &lavc_param_context, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 10},
    {"intra_matrix", &lavc_param_intra_matrix, TCCONF_TYPE_STRING, 0, 0, 0},
    {"inter_matrix", &lavc_param_inter_matrix, TCCONF_TYPE_STRING, 0, 0, 0},
    {"cbp", &lavc_param_cbp, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_CBP_RD},
    {"mv0", &lavc_param_mv0, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_MV0},
    {"nr", &lavc_param_noise_reduction, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 1000000},
    {"qprd", &lavc_param_qp_rd, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_QP_RD},
    {"threads", &lavc_param_threads, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 1, 16},
    {"ss", &lavc_param_ss, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_H263P_SLICE_STRUCT},
    {"svcd_sof", &lavc_param_scan_offset, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_SVCD_SCAN_OFFSET},
    {"alt", &lavc_param_alt, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_ALT_SCAN},
    {"ilme", &lavc_param_ilme, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_INTERLACED_ME},
    {"inter_threshold", &lavc_param_inter_threshold, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -1000000, 1000000},
    {"sc_threshold", &lavc_param_sc_threshold, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -1000000, 1000000},
    {"top", &lavc_param_top, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, -1, 1},
    {"gmc", &lavc_param_gmc, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_GMC},
    {"trunc", &lavc_param_trunc, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_TRUNCATED},
    {"closedgop", &lavc_param_closedgop, TCCONF_TYPE_FLAG, 0, 0, CODEC_FLAG_CLOSED_GOP},
    {"intra_dc_precision", &lavc_param_intra_dc_precision, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 16},
    {"skip_top", &lavc_param_skip_top, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 1000},
    {"skip_bottom", &lavc_param_skip_bottom, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 1000},
    {"fps_code", &lavc_param_fps_code, TCCONF_TYPE_INT, TCCONF_FLAG_RANGE, 0, 9},
    {"vpre", &lavc_param_video_preset, TCCONF_TYPE_STRING, 0, 0, 0},
    {"ffmpeg_datadir", &lavc_param_ffmpeg_datadir, TCCONF_TYPE_STRING, 0, 0, 0},
    {NULL, NULL, 0, 0, 0, 0}
};
