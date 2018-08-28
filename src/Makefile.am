# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DMOD_PATH=\"$(MOD_PATH)\" \
	-DPROF_PATH=\"$(PROF_PATH)\" \
	-I$(top_srcdir) \
	$(LIBAVCODEC_CFLAGS) \
	$(DLDARWIN_CFLAGS) \
	$(X_CFLAGS)

AM_LDFLAGS = -export-dynamic

bin_PROGRAMS = transcode

if HAVE_X11
TC_X_LIBS = $(X_LIBS) $(X_PRE_LIBS) -lXext -lX11 $(X_EXTRA_LIBS)
endif

transcode_LDADD = \
	$(DLDARWIN_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCAUDIO_LIBS) \
	$(LIBTCVIDEO_LIBS) \
	$(ACLIB_LIBS) \
	$(AVILIB_LIBS) \
	$(WAVLIB_LIBS) \
	$(XIO_LIBS) \
	$(LIBXML2_LIBS) \
	$(PTHREAD_LIBS) \
	$(TC_X_LIBS) \
	-lm

EXTRA_DIST = \
	audio_trans.h \
	cmdline.h \
	cmdline_def.h \
	counter.h \
	decoder.h \
	dl_loader.h \
	encoder.h \
	encoder-common.h \
	export_profile.h \
	filter.h \
	frame_threads.h \
	framebuffer.h \
	probe.h \
	socket.h \
	split.h \
	tc_defaults.h \
	tcinfo.h \
	transcode.h \
	video_trans.h 

transcode_SOURCES = \
	transcode.c \
	audio_trans.c \
	cmdline.c \
	counter.c \
	decoder.c \
	dl_loader.c \
	encoder.c \
	encoder-common.c \
	encoder-buffer.c \
	filter.c \
	frame_threads.c \
	framebuffer.c \
	probe.c \
	socket.c \
	split.c \
	video_trans.c
