# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DMOD_PATH=\"$(MOD_PATH)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

if HAVE_OGG
if HAVE_VORBIS
VORBISFILE = -lvorbisfile
endif
endif

if HAVE_A52
IMPORT_A52 = a52_decore.la
endif

if HAVE_ALSA
IMPORT_ALSA = import_alsa.la
endif

if HAVE_BKTR
IMPORT_BKTR = import_bktr.la
endif

IMPORT_BSDAV = import_bsdav.la

if HAVE_LIBDVDREAD
IMPORT_DVD = import_dvd.la
endif

if HAVE_FFMPEG
IMPORT_FFMPEG = import_ffmpeg.la
endif

if HAVE_IMAGEMAGICK
IMPORT_IM = import_im.la
IMPORT_IMLIST = import_imlist.la
endif

if HAVE_LZO
IMPORT_LZO = import_lzo.la
endif

if HAVE_LIBQUICKTIME
IMPORT_MOV = import_mov.la
endif

if HAVE_OSS
IMPORT_OSS = import_oss.la
endif

if ENABLE_PV3
IMPORT_PV3 = import_pv3.la
endif

if HAVE_SUNAU
IMPORT_SUNAU = import_sunau.la
endif

if HAVE_LIBXML2
IMPORT_XML = import_xml.la
endif

if HAVE_X11
IMPORT_X11 = import_x11.la
X11SOURCE_LIBS = $(X_LIBS) $(X_PRE_LIBS) -lXext -lX11 $(X_EXTRA_LIBS)
endif

if HAVE_MJPEGTOOLS
IMPORT_YUV4MPEG = import_yuv4mpeg.la
endif

pkgdir = $(MOD_PATH)

if ENABLE_NUV
NUV = nuv
endif
SUBDIRS = framegen $(NUV) v4l

pkg_LTLIBRARIES = \
	$(IMPORT_A52) \
	import_ac3.la \
	import_avi.la \
	$(IMPORT_ALSA) \
	$(IMPORT_BKTR) \
	$(IMPORT_BSDAV) \
	import_dv.la \
	$(IMPORT_DVD) \
	$(IMPORT_FFMPEG) \
	$(IMPORT_IM) \
	$(IMPORT_IMLIST) \
	$(IMPORT_LZO) \
	$(IMPORT_MOV) \
	import_mp3.la \
	import_mpeg2.la \
	import_mplayer.la \
	import_null.la \
	import_ogg.la \
	$(IMPORT_OSS) \
	$(IMPORT_PV3) \
	import_pvn.la \
	import_raw.la \
	$(IMPORT_SUNAU) \
	import_vag.la \
	import_vnc.la \
	import_vob.la \
	$(IMPORT_XML) \
	import_xvid.la \
	import_x11.la \
	$(IMPORT_YUV4MPEG)

a52_decore_la_SOURCES = a52_decore.c
a52_decode_la_CPPFLAGS = $(AM_CPPFLAGS) $(A52_CFLAGS)
a52_decore_la_LDFLAGS = -module -avoid-version
a52_decore_la_LIBADD = $(A52_LIBS) $(XIO_LIBS)

import_ac3_la_SOURCES = import_ac3.c
import_ac3_la_LDFLAGS = -module -avoid-version

import_alsa_la_SOURCES = import_alsa.c
import_alsa_la_LDFLAGS = -module -avoid-version
import_alsa_la_LIBADD = -lasound

import_avi_la_SOURCES = import_avi.c
import_avi_la_LDFLAGS = -module -avoid-version

import_bktr_la_SOURCES = import_bktr.c
import_bktr_la_LDFLAGS = -module -avoid-version

import_bsdav_la_SOURCES = import_bsdav.c
import_bsdav_la_CPPFLAGS = $(AM_CPPFLAGS) $(BSDAV_CFLAGS)
import_bsdav_la_LDFLAGS = -module -avoid-version

import_dv_la_SOURCES = import_dv.c
import_dv_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBDV_CFLAGS)
import_dv_la_LDFLAGS = -module -avoid-version

import_dvd_la_SOURCES = import_dvd.c ac3scan.c dvd_reader.c clone.c ioaux.c frame_info.c ivtc.c
import_dvd_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBDVDREAD_CFLAGS)
import_dvd_la_LDFLAGS = -module -avoid-version
import_dvd_la_LIBADD = $(LIBDVDREAD_LIBS)

import_ffmpeg_la_SOURCES = import_ffmpeg.c
import_ffmpeg_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBAVFORMAT_CFLAGS)
import_ffmpeg_la_LDFLAGS = -module -avoid-version
import_ffmpeg_la_LIBADD = $(LIBAVFORMAT_LIBS)

import_im_la_SOURCES = import_im.c
import_im_la_CPPFLAGS = $(AM_CPPFLAGS) $(IMAGEMAGICK_CFLAGS)
import_im_la_LDFLAGS = -module -avoid-version
import_im_la_LIBADD = $(IMAGEMAGICK_LIBS) -lm

import_imlist_la_SOURCES = import_imlist.c
import_imlist_la_CPPFLAGS = $(AM_CPPFLAGS) $(IMAGEMAGICK_CFLAGS)
import_imlist_la_LDFLAGS = -module -avoid-version
import_imlist_la_LIBADD = $(IMAGEMAGICK_LIBS) -lm

import_lzo_la_SOURCES = import_lzo.c
import_lzo_la_CPPFLAGS = $(AM_CPPFLAGS) $(LZO_CFLAGS)
import_lzo_la_LDFLAGS = -module -avoid-version
import_lzo_la_LIBADD  = $(LZO_LIBS) -lm

import_mov_la_SOURCES = import_mov.c
import_mov_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBQUICKTIME_CFLAGS)
import_mov_la_LDFLAGS = -module -avoid-version
import_mov_la_LIBADD = $(LIBQUICKTIME_LIBS) -lm

import_mp3_la_SOURCES = import_mp3.c
import_mp3_la_LDFLAGS = -module -avoid-version

import_mpeg2_la_SOURCES = import_mpeg2.c
import_mpeg2_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBMPEG2_CFLAGS) $(LIBMPEG2CONVERT_CFLAGS)
import_mpeg2_la_LDFLAGS = -module -avoid-version

import_mplayer_la_SOURCES = import_mplayer.c
import_mplayer_la_LDFLAGS = -module -avoid-version

import_null_la_SOURCES = import_null.c
import_null_la_LDFLAGS = -module -avoid-version

import_ogg_la_SOURCES = import_ogg.c
import_ogg_la_CPPFLAGS = $(AM_CPPFLAGS) $(OGG_CFLAGS) $(THEORA_CFLAGS) $(VORBIS_CFLAGS)
import_ogg_la_LDFLAGS = -module -avoid-version

import_oss_la_SOURCES = import_oss.c
import_oss_la_LDFLAGS = -module -avoid-version

import_pv3_la_SOURCES = import_pv3.c w32dll.c w32dll-emu.c
import_pv3_la_LDFLAGS = -module -avoid-version

import_pvn_la_SOURCES = import_pvn.c
import_pvn_la_LDFLAGS = -module -avoid-version

import_raw_la_SOURCES = import_raw.c
import_raw_la_LDFLAGS = -module -avoid-version

import_sunau_la_SOURCES = import_sunau.c
import_sunau_la_LDFLAGS = -module -avoid-version

import_vag_la_SOURCES = import_vag.c
import_vag_la_LDFLAGS = -module -avoid-version

import_vnc_la_SOURCES = import_vnc.c
import_vnc_la_LDFLAGS = -module -avoid-version

import_vob_la_SOURCES = import_vob.c ac3scan.c clone.c ioaux.c frame_info.c ivtc.c
import_vob_la_CPPFLAGS = $(AM_CPPFLAGS)
import_vob_la_LDFLAGS =	-module -avoid-version

import_xml_la_SOURCES = import_xml.c ioxml.c probe_xml.c
import_xml_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBXML2_CFLAGS)
import_xml_la_LDFLAGS = -module -avoid-version
import_xml_la_LIBADD = $(LIBXML2_LIBS) -lm

import_xvid_la_SOURCES = import_xvid.c
import_xvid_la_LDFLAGS = -module -avoid-version

import_x11_la_SOURCES = import_x11.c x11source.c
import_x11_la_CPPFLAGS = $(AM_CPPFLAGS) $(X_CFLAGS)
import_x11_la_LDFLAGS = -module -avoid-version
import_x11_la_LIBADD = $(X11SOURCE_LIBS)

import_yuv4mpeg_la_SOURCES = import_yuv4mpeg.c
import_yuv4mpeg_la_CPPFLAGS = $(AM_CPPFLAGS) $(MJPEGTOOLS_CFLAGS)
import_yuv4mpeg_la_LDFLAGS = -module -avoid-version
import_yuv4mpeg_la_LIBADD = $(MJPEGTOOLS_LIBS)


EXTRA_DIST = \
	ac3scan.h \
	aux_pes.h \
	clone.h \
	demuxer.h \
	dvd_reader.h \
	frame_info.h \
	import_def.h \
	ioaux.h \
	ioxml.h \
	ivtc.h \
	magic.h \
	mpg123.h \
	ogmstreams.h \
	packets.h \
	probe_xml.h \
	seqinfo.h \
	putvlc.h \
	getvlc.h \
	tc.h \
	probe_stream.h \
	w32dll.h \
	x11source.h 

if ENABLE_DEPRECATED
TCREQUANT = tcrequant
endif

bin_PROGRAMS = \
	tccat \
	tcdecode \
	tcdemux \
	tcextract \
	tcprobe \
	$(TCREQUANT) \
	tcscan \
	tcxmlcheck \
	tcxpm2rgb


#  The _CFLAGS lines below keep libtool from overwriting
#  objects that have been compiled with flags for shared
#  objects with objects that were compiled for executables.

# --------- #
# T C C A T #
# --------- #

tccat_SOURCES = \
	tccat.c \
	dvd_reader.c \
	extract_avi.c \
	fileinfo.c \
	ioaux.c \
	ioxml.c \
	ts_reader.c

tccat_LDADD = \
	$(AVILIB_LIBS) \
	$(LIBDV_LIBS) \
	$(LIBDVDREAD_LIBS) \
	$(XIO_LIBS) \
	$(LIBXML2_LIBS) \
	$(DLDARWIN_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	-lm

tccat_CFLAGS = $(AM_CFLAGS) \
	$(LIBDV_CFLAGS) \
	$(LIBDVDREAD_CFLAGS) \
	$(LIBXML2_CFLAGS)


# --------------- #
# T C D E C O D E #
# --------------- #

tcdecode_SOURCES = \
	tcdecode.c \
	fileinfo.c \
	ioaux.c \
	mpg123.c \
	decode_a52.c \
	decode_dv.c \
	decode_lavc.c \
	decode_lzo.c \
	decode_mov.c \
	decode_mp3.c \
	decode_mpeg2.c \
	decode_ogg.c \
	decode_yuv.c

tcdecode_LDADD = \
	$(LIBAVCODEC_LIBS) \
	$(LIBDV_LIBS) \
	$(LAME_LIBS) \
	$(LZO_LIBS) \
	$(LIBQUICKTIME_LIBS) \
	$(LIBMPEG2_LIBS) \
	$(LIBMPEG2CONVERT_LIBS) \
	$(OGG_LIBS) \
	$(THEORA_LIBS) \
	$(VORBIS_LIBS) \
	$(VORBISFILE) \
	$(XIO_LIBS) \
	$(DLDARWIN_LIBS) \
	$(LIBTCVIDEO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS)

tcdecode_CFLAGS = $(AM_CFLAGS) \
	$(LIBAVCODEC_CFLAGS) \
	$(LIBDV_CFLAGS) \
	$(LAME_CFLAGS) \
	$(LZO_CFLAGS) \
	$(LIBQUICKTIME_CFLAGS) \
	$(LIBMPEG2_CFLAGS) \
	$(LIBMPEG2CONVERT_CFLAGS) \
	$(OGG_CFLAGS) \
	$(THEORA_CFLAGS) \
	$(VORBIS_CFLAGS)

tcdecode_LDFLAGS = -export-dynamic


# ------------- #
# T C D E M U X #
# ------------- #

tcdemux_SOURCES = \
	aux_pes.c \
	tcdemux.c \
	demux_pass.c \
	demuxer.c \
	dvd_reader.c \
	fileinfo.c \
	ioaux.c \
	packets.c \
	scan_pack.c \
	seqinfo.c

tcdemux_LDADD = \
	$(AVILIB_LIBS) \
	$(LIBDV_LIBS) \
	$(LIBDVDREAD_LIBS) \
	$(DLDARWIN_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	$(PTHREAD_LIBS) \
	-lm

tcdemux_CFLAGS = $(AM_CFLAGS) \
	$(LIBDV_CFLAGS) \
	$(LIBDVDREAD_CFLAGS)


# ----------------- #
# T C E X T R A C T #
# ----------------- #

tcextract_SOURCES = \
	tcextract.c \
	aux_pes.c \
	fileinfo.c \
	ioaux.c \
	extract_ac3.c \
	extract_avi.c \
	extract_dv.c \
	extract_lzo.c \
	extract_mp3.c \
	extract_mpeg2.c \
	extract_mxf.c \
	extract_ogm.c \
	extract_pcm.c \
	extract_rgb.c \
	extract_yuv.c

tcextract_LDADD = \
	$(AVILIB_LIBS) \
	$(WAVLIB_LIBS) \
	$(LIBDV_LIBS) \
	$(MJPEGTOOLS_LIBS) \
	$(OGG_LIBS) \
	$(THEORA_LIBS) \
	$(VORBIS_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	-lm

tcextract_CFLAGS = $(AM_CFLAGS) \
	$(LIBDV_CFLAGS) \
	$(MJPEGTOOLS_CFLAGS) \
	$(OGG_CFLAGS) \
	$(THEORA_CFLAGS) \
	$(VORBIS_CFLAGS)


# ------------- #
# T C P R O B E #
# ------------- #

tcprobe_SOURCES = \
	tcprobe.c \
	ac3scan.c \
	aux_pes.c \
	dvd_reader.c \
	fileinfo.c \
	ioaux.c \
	ioxml.c \
	mpg123.c \
	scan_pes.c \
	decode_dv.c \
	extract_avi.c \
	extract_mxf.c \
	extract_yuv.c \
	probe_bktr.c \
	probe_bsdav.c \
	probe_dvd.c \
	probe_im.c \
	probe_mov.c \
	probe_nuv.c \
	probe_ogg.c \
	probe_oss.c \
	probe_pv3.c \
	probe_stream.c \
	probe_sunau.c \
	probe_v4l.c \
	probe_vnc.c \
	probe_wav.c \
	probe_xml.c \
	probe_mplayer.c \
	probe_ffmpeg.c \
	probe_x11.c \
	x11source.c \
	probe_pvn.c

tcprobe_LDADD = \
	$(LIBAVFORMAT_LIBS) \
	$(LIBAVCODEC_LIBS) \
	$(AVILIB_LIBS) \
	$(WAVLIB_LIBS) \
	$(LIBDV_LIBS) \
	$(LIBDVDREAD_LIBS) \
	$(IMAGEMAGICK_LIBS) \
	$(LAME_LIBS) \
	$(LIBQUICKTIME_LIBS) \
	$(MJPEGTOOLS_LIBS) \
	$(OGG_LIBS) \
	$(THEORA_LIBS) \
	$(VORBIS_LIBS) \
	$(XIO_LIBS) \
	$(LIBXML2_LIBS) \
	$(BSDAV_LIBS) \
	$(X11SOURCE_LIBS) \
	$(DLDARWIN_LIBS) \
	$(LIBTCVIDEO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	-lm

tcprobe_CFLAGS = $(AM_CFLAGS) \
	$(LIBAVFORMAT_CFLAGS) \
	$(LIBAVCODEC_CFLAGS) \
	$(LIBDV_CFLAGS) \
	$(LIBDVDREAD_CFLAGS) \
	$(IMAGEMAGICK_CFLAGS) \
	$(LAME_CFLAGS) \
	$(LIBQUICKTIME_CFLAGS) \
	$(MJPEGTOOLS_CFLAGS) \
	$(OGG_CFLAGS) \
	$(THEORA_CFLAGS) \
	$(VORBIS_CFLAGS) \
	$(LIBXML2_CFLAGS) \
	$(BSDAV_CFLAGS)


# ----------------- #
# T C R E Q U A N T #
# ----------------- #

tcrequant_SOURCES = tcrequant.c
tcrequant_LDADD = \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	-lm

tcrequant_CFLAGS = $(AM_CFLAGS)


# ----------- #
# T C S C A N #
# ----------- #

tcscan_SOURCES = \
	tcscan.c \
	ac3scan.c \
	aux_pes.c \
	fileinfo.c \
	ioaux.c \
	mpg123.c \
	scan_pes.c

tcscan_LDADD = \
	$(AVILIB_LIBS) \
	$(LIBDV_LIBS) \
	$(LAME_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	-lm

tcscan_CFLAGS = $(AM_CFLAGS) \
	$(LIBDV_CFLAGS) \
	$(LAME_CFLAGS)


# ------------------- #
# T C X M L C H E C K #
# ------------------- #

tcxmlcheck_SOURCES = \
	tcxmlcheck.c \
	fileinfo.c \
	ioaux.c \
	ioxml.c \
	probe_xml.c

tcxmlcheck_LDADD = \
	$(LIBDV_LIBS) \
	$(XIO_LIBS) \
	$(LIBXML2_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS) \
	-lm

tcxmlcheck_CFLAGS = $(AM_CFLAGS) \
	$(LIBDV_CFLAGS) \
	$(LIBXML2_CFLAGS)


# ----------------- #
# T C X P M 2 R G B #
# ----------------- #

tcxpm2rgb_SOURCES = tcxpm2rgb.c

tcxpm2rgb_LDADD = $(LIBTC_LIBS)

tcxpm2rgb_CFLAGS = $(AM_CFLAGS)

