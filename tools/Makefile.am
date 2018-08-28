# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DMOD_PATH=\"$(MOD_PATH)\" \
	-DPROF_PATH=\"$(PROF_PATH)\" \
	-DTC_ENCODER_NG=1 \
	-DTCEXPORT_PROFILE=1 \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

if ENABLE_EXPERIMENTAL
NMS_PROGS = tcexport tcmodchain
endif

bin_PROGRAMS = \
	avifix \
	aviindex \
	avimerge \
	avisplit \
	avisync \
	tcmodinfo \
	tcmp3cut \
	tcyait \
	$(NMS_PROGS)

avisync_SOURCES = \
	avisync.c \
	avimisc.c \
	buffer.c \
	buffer.h \
	aud_scan.c \
	aud_scan_avi.c \
	aud_scan_avi.h \
	aud_scan.h
avisync_LDADD = \
	$(AVILIB_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS)

avimerge_SOURCES = \
	avimerge.c \
	avimisc.c \
	aud_scan.c \
	aud_scan_avi.c \
	aud_scan_avi.h \
	aud_scan.h
avimerge_LDADD = \
	$(AVILIB_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS)

avifix_SOURCES = \
	avifix.c \
	avimisc.c
avifix_LDADD = \
	$(AVILIB_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS)

aviindex_SOURCES = \
	aviindex.c \
	avimisc.c \
	aud_scan.c \
	aud_scan.h
aviindex_LDADD = \
	$(AVILIB_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS)

avisplit_SOURCES = \
	avisplit.c \
	avimisc.c \
	aud_scan.c \
	aud_scan_avi.c \
	aud_scan.h
	aud_scan_avi.h
avisplit_LDADD = \
	$(AVILIB_LIBS) \
	$(XIO_LIBS) \
	$(ACLIB_LIBS) \
	$(LIBTC_LIBS)

tcmodinfo_SOURCES = tcmodinfo.c tcstub.c
tcmodinfo_CPPFLAGS = $(AM_CPPFLAGS) \
	$(DLDARWIN_CFLAGS)
tcmodinfo_LDADD = \
	$(DLDARWIN_LIBS) \
	$(ACLIB_LIBS) \
	$(AVILIB_LIBS) \
	$(WAVLIB_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCVIDEO_LIBS) \
	$(PTHREAD_LIBS) \
	-lm
tcmodinfo_LDFLAGS = -export-dynamic

tcmodchain_SOURCES = tcmodchain.c tcstub.c
tcmodchain_CPPFLAGS = $(AM_CPPFLAGS) \
	$(DLDARWIN_CFLAGS)
tcmodchain_LDADD = \
	$(DLDARWIN_LIBS) \
	$(ACLIB_LIBS) \
	$(AVILIB_LIBS) \
	$(WAVLIB_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCVIDEO_LIBS) \
	$(PTHREAD_LIBS) \
	-lm
tcmodchain_LDFLAGS = -export-dynamic

tcexport_SOURCES = tcexport.c \
	tcstub.c \
	rawsource.c \
	rawsource.h \
	../src/probe.c \
	../src/dl_loader.c \
	../src/counter.c \
	../src/encoder.c \
	../src/encoder-common.c \
	../src/export_profile.c \
	../src/framebuffer.c
tcexport_CPPFLAGS = $(AM_CPPFLAGS) \
	$(DLDARWIN_CFLAGS)
tcexport_LDADD = \
	$(DLDARWIN_LIBS) \
	$(ACLIB_LIBS) \
	$(AVILIB_LIBS) \
	$(WAVLIB_LIBS) \
	$(LIBTC_LIBS) \
	$(LIBTCVIDEO_LIBS) \
	$(LIBTCAUDIO_LIBS) \
	$(XIO_LIBS) \
	$(PTHREAD_LIBS) \
	-lm
tcexport_LDFLAGS = -export-dynamic

tcmp3cut_SOURCES = \
	tcmp3cut.c \
	aud_scan.c \
	aud_scan.h
tcmp3cut_LDADD = \
	$(LIBTC_LIBS)

tcyait_SOURCES = \
	tcyait.c
tcyait_LDADD = \
	-lm

EXTRA_DIST = \
        tcstub.h 