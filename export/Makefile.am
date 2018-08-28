# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DMOD_PATH=\"$(MOD_PATH)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

pkgdir = $(MOD_PATH)

if HAVE_LIBDV
EXPORT_DV = export_dv.la
EXPORT_DVRAW = export_dvraw.la
endif

if HAVE_FFMPEG
EXPORT_FFMPEG = export_ffmpeg.la
endif

if HAVE_IMAGEMAGICK
EXPORT_IM = export_im.la
endif

if HAVE_LIBJPEG
EXPORT_JPG = export_jpg.la
endif

if HAVE_LZO
EXPORT_LZO = export_lzo.la
endif

if HAVE_LIBQUICKTIME
EXPORT_MOV = export_mov.la
endif

if HAVE_MJPEGTOOLS
EXPORT_MP2ENC = export_mp2enc.la
EXPORT_MPEG2ENC = export_mpeg2enc.la
EXPORT_YUV4MPEG = export_yuv4mpeg.la
endif

if HAVE_PVM3
EXPORT_PVM = export_pvm.la
endif

pkg_LTLIBRARIES = \
	export_ac3.la \
	export_divx5.la \
	$(EXPORT_DV) \
	$(EXPORT_DVRAW) \
	$(EXPORT_FFMPEG) \
	$(EXPORT_IM) \
	$(EXPORT_JPG) \
	export_lame.la \
	$(EXPORT_LZO) \
	$(EXPORT_MOV) \
	export_mp2.la \
	$(EXPORT_MP2ENC) \
	$(EXPORT_MPEG2ENC) \
	export_null.la \
	export_ogg.la \
	export_pcm.la \
	export_ppm.la \
	$(EXPORT_PVM) \
	export_pvn.la \
	export_raw.la \
	export_tcaud.la \
	export_toolame.la \
	export_wav.la \
	export_xvid4.la \
	$(EXPORT_YUV4MPEG)

export_ac3_la_SOURCES = export_ac3.c
export_ac3_la_LDFLAGS = -module -avoid-version

export_divx5_la_SOURCES = export_divx5.c aud_aux.c divx4_vbr.c
export_divx5_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBAVCODEC_CFLAGS) $(LAME_CFLAGS)
export_divx5_la_LDFLAGS = -module -avoid-version
export_divx5_la_LIBADD = $(LIBAVCODEC_LIBS) $(LAME_LIBS)

export_dv_la_SOURCES = export_dv.c aud_aux.c
export_dv_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBDV_CFLAGS) $(LIBAVCODEC_CFLAGS) $(LAME_CFLAGS)
export_dv_la_LDFLAGS = -module -avoid-version
export_dv_la_LIBADD = $(LIBDV_LIBS) $(LIBAVCODEC_LIBS) $(LAME_LIBS)

export_dvraw_la_SOURCES = export_dvraw.c
export_dvraw_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBDV_CFLAGS)
export_dvraw_la_LDFLAGS = -module -avoid-version
export_dvraw_la_LIBADD = $(LIBDV_LIBS) -lm

export_ffmpeg_la_SOURCES = export_ffmpeg.c aud_aux.c ffmpeg_cfg.c
export_ffmpeg_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBAVCODEC_CFLAGS) $(LAME_CFLAGS)
export_ffmpeg_la_LDFLAGS = -module -avoid-version
export_ffmpeg_la_LIBADD = $(LIBAVCODEC_LIBS) $(LAME_LIBS)

export_im_la_SOURCES = export_im.c
export_im_la_CPPFLAGS = $(AM_CPPFLAGS) $(IMAGEMAGICK_CFLAGS)
export_im_la_LDFLAGS = -module -avoid-version
export_im_la_LIBADD = $(IMAGEMAGICK_LIBS) -lm

export_jpg_la_SOURCES = export_jpg.c
export_jpg_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBJPEG_CFLAGS)
export_jpg_la_LDFLAGS = -module -avoid-version
export_jpg_la_LIBADD = $(LIBJPEG_LIBS) -lm

export_lame_la_SOURCES = export_lame.c
export_lame_la_LDFLAGS = -module -avoid-version

export_lzo_la_SOURCES = export_lzo.c aud_aux.c
export_lzo_la_CPPFLAGS = $(AM_CPPFLAGS) $(LZO_CFLAGS) $(LIBAVCODEC_CFLAGS) $(LAME_CFLAGS)
export_lzo_la_LDFLAGS = -module -avoid-version
export_lzo_la_LIBADD = $(LZO_LIBS) $(LIBAVCODEC_LIBS) $(LAME_LIBS)

export_mov_la_SOURCES = export_mov.c
export_mov_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBQUICKTIME_CFLAGS)
export_mov_la_LDFLAGS = -module -avoid-version
export_mov_la_LIBADD = $(LIBQUICKTIME_LIBS) -lm

export_mp2_la_SOURCES = export_mp2.c
export_mp2_la_LDFLAGS = -module -avoid-version

export_mp2enc_la_SOURCES = export_mp2enc.c
export_mp2enc_la_CPPFLAGS = $(AM_CPPFLAGS) $(MJPEGTOOLS_CFLAGS)
export_mp2enc_la_LDFLAGS = -module -avoid-version
export_mp2enc_la_LIBADD = -lm

export_mpeg2enc_la_SOURCES = export_mpeg2enc.c
export_mpeg2enc_la_CPPFLAGS = $(AM_CPPFLAGS) $(MJPEGTOOLS_CFLAGS)
export_mpeg2enc_la_LDFLAGS = -module -avoid-version
export_mpeg2enc_la_LIBADD = $(MJPEGTOOLS_LIBS) -lm

export_null_la_SOURCES = export_null.c
export_null_la_LDFLAGS = -module -avoid-version

export_ogg_la_SOURCES = export_ogg.c
export_ogg_la_LDFLAGS = -module -avoid-version

export_pcm_la_SOURCES = export_pcm.c
export_pcm_la_LDFLAGS = -module -avoid-version

export_ppm_la_SOURCES = export_ppm.c aud_aux.c
export_ppm_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBAVCODEC_CFLAGS) $(LAME_CFLAGS)
export_ppm_la_LDFLAGS = -module -avoid-version
export_ppm_la_LIBADD = $(LIBAVCODEC_LIBS) $(LAME_LIBS)

export_pvm_la_SOURCES = export_pvm.c ../pvm3/pvm_interface.c ../pvm3/pvm_parser.c ../pvm3/external_codec.c ../pvm3/vob_pack_unpack.c
export_pvm_la_CPPFLAGS = $(AM_CPPFLAGS) $(PVM3_CFLAGS)
export_pvm_la_LDFLAGS = -module -avoid-version

export_pvn_la_SOURCES = export_pvn.c
export_pvn_la_LDFLAGS = -module -avoid-version

export_raw_la_SOURCES = export_raw.c aud_aux.c
export_raw_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBAVCODEC_CFLAGS) $(LAME_CFLAGS)
export_raw_la_LDFLAGS = -module -avoid-version
export_raw_la_LIBADD = $(LIBAVCODEC_LIBS) $(LAME_LIBS)

export_tcaud_la_SOURCES = export_tcaud.c aud_aux.c
export_tcaud_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBAVCODEC_CFLAGS) $(LAME_CFLAGS)
export_tcaud_la_LDFLAGS = -module -avoid-version
export_tcaud_la_LIBADD = $(LIBAVCODEC_LIBS) $(LAME_LIBS)

export_toolame_la_SOURCES = export_toolame.c
export_toolame_la_LDFLAGS = -module -avoid-version

export_wav_la_SOURCES = export_wav.c
export_wav_la_LDFLAGS = -module -avoid-version
export_wav_la_LIBADD = -lm

export_xvid4_la_SOURCES = export_xvid4.c aud_aux.c
export_xvid4_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBAVCODEC_CFLAGS) $(LAME_CFLAGS)
export_xvid4_la_LDFLAGS = -module -avoid-version
export_xvid4_la_LIBADD = $(LIBAVCODEC_LIBS) $(LAME_LIBS)

export_yuv4mpeg_la_SOURCES = export_yuv4mpeg.c aud_aux.c
export_yuv4mpeg_la_CPPFLAGS = $(AM_CPPFLAGS) $(MJPEGTOOLS_CFLAGS) $(LIBAVCODEC_CFLAGS) $(LAME_CFLAGS)
export_yuv4mpeg_la_LDFLAGS = -module -avoid-version
export_yuv4mpeg_la_LIBADD = $(MJPEGTOOLS_LIBS) $(LIBAVCODEC_LIBS) $(LAME_LIBS)

EXTRA_DIST = \
	aud_aux.h \
	divx5_encore2.h \
	export_def.h \
	ffmpeg_cfg.h \
	vbr.h \
	xvid4.h \
	xvid4.cfg

if ENABLE_EXPERIMENTAL
PROFILES = \
	dvd-pal.cfg \
	dvd-ntsc.cfg \
	vcd-pal.cfg \
	vcd-ntsc.cfg \
	svcd-pal.cfg \
	svcd-ntsc.cfg \
	xvcd-pal.cfg \
	xvcd-ntsc.cfg
endif

pkg_DATA = xvid4.cfg $(PROFILES)

if ENABLE_EXPERIMENTAL
install-exec-hook:
	@mkdir -p $(DESTDIR)$(MOD_PATH)
	@rm -f $(DESTDIR)$(MOD_PATH)/export_xvid.so
	@$(LN_S) export_xvid4.so $(DESTDIR)$(MOD_PATH)/export_xvid.so
	@mkdir -p $(DESTDIR)$(PROF_PATH)
	@cp $(PROFILES) $(DESTDIR)$(PROF_PATH)
else
install-exec-hook:
	@mkdir -p $(DESTDIR)$(MOD_PATH)
	@rm -f $(DESTDIR)$(MOD_PATH)/export_xvid.so
	@$(LN_S) export_xvid4.so $(DESTDIR)$(MOD_PATH)/export_xvid.so
endif

