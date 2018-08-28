#ifndef __FFMPEG_CFG_H
#define __FFMPEG_CFG_H

#include "libtc/cfgfile.h"
#include "libtc/tcavcodec.h"

//char *lavc_param_vcodec = "mpeg4";
//extern int lavc_param_vbitrate;
extern int lavc_param_vrate_tolerance;
extern int lavc_param_mb_decision;
extern int lavc_param_v4mv;
extern int lavc_param_vme;
//extern int lavc_param_vqscale;
//extern int lavc_param_vqmin;
//extern int lavc_param_vqmax;
extern int lavc_param_mb_qmin;
extern int lavc_param_mb_qmax;
extern int lavc_param_lmin;
extern int lavc_param_lmax;
extern int lavc_param_vqdiff;
extern float lavc_param_vqcompress;
extern float lavc_param_vqblur;
extern float lavc_param_vb_qfactor;
extern float lavc_param_vb_qoffset;
extern float lavc_param_vi_qfactor;
extern float lavc_param_vi_qoffset;
extern int lavc_param_vmax_b_frames;
//extern int lavc_param_keyint;
//extern int lavc_param_vpass;
extern int lavc_param_vrc_strategy;
extern int lavc_param_vb_strategy;
extern int lavc_param_luma_elim_threshold;
extern int lavc_param_chroma_elim_threshold;
extern int lavc_param_packet_size;
extern int lavc_param_strict;
extern int lavc_param_data_partitioning;
extern int lavc_param_gray;
extern float lavc_param_rc_qsquish;
extern float lavc_param_rc_qmod_amp;
extern int lavc_param_rc_qmod_freq;
extern char *lavc_param_rc_override_string;
extern char *lavc_param_rc_eq;
extern int lavc_param_rc_buffer_size;
extern float lavc_param_rc_buffer_aggressivity;
extern int lavc_param_rc_max_rate;
extern int lavc_param_rc_min_rate;
extern float lavc_param_rc_initial_cplx;
extern int lavc_param_mpeg_quant;
extern int lavc_param_fdct;
extern int lavc_param_idct;
extern float lavc_param_lumi_masking;
extern float lavc_param_dark_masking;
extern float lavc_param_temporal_cplx_masking;
extern float lavc_param_spatial_cplx_masking;
extern float lavc_param_p_masking;
extern int lavc_param_normalize_aqp;
// ildct
extern int lavc_param_prediction_method;
extern char *lavc_param_format;
extern int lavc_param_debug;
extern int lavc_param_psnr;
extern int lavc_param_me_pre_cmp;
extern int lavc_param_me_cmp;
extern int lavc_param_me_sub_cmp;
extern int lavc_param_mb_cmp;
extern int lavc_param_ildct_cmp;
extern int lavc_param_pre_dia_size;
extern int lavc_param_dia_size;
extern int lavc_param_qpel;
extern int lavc_param_trell;
extern int lavc_param_aic;//CONF_TYPE_FLAG, 0, 0, CODEC_FLAG_H263P_AIC, NULL},
extern int lavc_param_umv;// CONF_TYPE_FLAG, 0, 0, CODEC_FLAG_H263P_UMV, NULL},
extern int lavc_param_last_pred;
extern int lavc_param_pre_me;
extern int lavc_param_me_subpel_quality;
extern int lavc_param_me_range;
extern int lavc_param_ibias;
extern int lavc_param_pbias;
extern int lavc_param_coder;
extern int lavc_param_context;
extern char *lavc_param_intra_matrix;
extern char *lavc_param_inter_matrix;
extern int lavc_param_cbp;
extern int lavc_param_mv0;
extern int lavc_param_noise_reduction;
extern int lavc_param_qp_rd;
extern int lavc_param_inter_threshold;
extern int lavc_param_sc_threshold;
extern int lavc_param_ss;
extern int lavc_param_top;
extern int lavc_param_alt;
extern int lavc_param_ilme;
extern int lavc_param_scan_offset;
extern int lavc_param_threads;
extern int lavc_param_gmc;
extern int lavc_param_trunc;
extern int lavc_param_fps_code;
extern int lavc_param_closedgop;
extern int lavc_param_intra_dc_precision;
extern int lavc_param_skip_top;
extern int lavc_param_skip_bottom;

extern char *lavc_param_video_preset;
extern char *lavc_param_ffmpeg_datadir;

extern TCConfigEntry lavcopts_conf[];

#endif
