# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DMOD_PATH=\"$(MOD_PATH)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

pkgdir = $(MOD_PATH)

if HAVE_FAAC
ENCODE_FAAC = encode_faac.la
endif

if HAVE_LAME
ENCODE_LAME = encode_lame.la
endif

if HAVE_FFMPEG
ENCODE_LAVC = encode_lavc.la
endif

if HAVE_LZO
ENCODE_LZO = encode_lzo.la
endif

if HAVE_X264
ENCODE_X264 = encode_x264.la
endif

if HAVE_XVID
ENCODE_XVID = encode_xvid.la
endif

if ENABLE_EXPERIMENTAL
pkg_LTLIBRARIES = \
	encode_copy.la \
	$(ENCODE_FAAC) \
	$(ENCODE_LAME) \
	$(ENCODE_LAVC) \
	$(ENCODE_LZO) \
	encode_null.la \
	$(ENCODE_X264) \
	$(ENCODE_XVID)
endif

encode_copy_la_SOURCES = encode_copy.c
encode_copy_la_LDFLAGS = -module -avoid-version

encode_faac_la_SOURCES = encode_faac.c
encode_faac_la_CPPFLAGS = $(AM_CPPFLAGS) $(FAAC_CFLAGS)
encode_faac_la_LDFLAGS = -module -avoid-version
encode_faac_la_LIBADD = $(FAAC_LIBS)

encode_lavc_la_SOURCES = encode_lavc.c
encode_lavc_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBAVCODEC_CFLAGS)
encode_lavc_la_LDFLAGS = -module -avoid-version
encode_lavc_la_LIBADD = $(LIBAVCODEC_LIBS)

encode_lame_la_SOURCES = encode_lame.c
encode_lame_la_CPPFLAGS = $(AM_CPPFLAGS) $(LAME_CFLAGS)
encode_lame_la_LDFLAGS = -module -avoid-version
encode_lame_la_LIBADD = $(LAME_LIBS)

encode_lzo_la_SOURCES = encode_lzo.c
encode_lzo_la_CPPFLAGS = $(AM_CPPFLAGS) $(LZO_CFLAGS)
encode_lzo_la_LDFLAGS = -module -avoid-version
encode_lzo_la_LIBADD = $(LZO_LIBS)

encode_null_la_SOURCES = encode_null.c
encode_null_la_LDFLAGS = -module -avoid-version

encode_x264_la_SOURCES = encode_x264.c
encode_x264_la_CPPFLAGS = $(AM_CPPFLAGS) $(X264_CFLAGS)
encode_x264_la_LDFLAGS = -module -avoid-version
encode_x264_la_LIBADD = $(X264_LIBS)

encode_xvid_la_SOURCES = encode_xvid.c
encode_xvid_la_LDFLAGS = -module -avoid-version
encode_xvid_la_LIBADD = $(XVID_LIBS)

EXTRA_DIST = \
	lavc.cfg \
	x264.cfg \
	xvid.cfg
