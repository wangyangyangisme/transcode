# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DMOD_PATH=\"$(MOD_PATH)\" \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

pkgdir = $(MOD_PATH)

if HAVE_MJPEGTOOLS
MODS_MJPEGTOOLS = \
	multiplex_y4m.la
endif

if ENABLE_EXPERIMENTAL
pkg_LTLIBRARIES = \
	multiplex_raw.la \
	multiplex_avi.la \
	$(MODS_MJPEGTOOLS) \
	multiplex_null.la
endif

multiplex_avi_la_SOURCES = multiplex_avi.c
multiplex_avi_la_LDFLAGS = -module -avoid-version

multiplex_raw_la_SOURCES = multiplex_raw.c
multiplex_raw_la_LDFLAGS = -module -avoid-version

multiplex_null_la_SOURCES = multiplex_null.c
multiplex_null_la_LDFLAGS = -module -avoid-version

multiplex_y4m_la_SOURCES = multiplex_y4m.c
multiplex_y4m_la_CPPFLAGS = $(AM_CPPFLAGS) $(MJPEGTOOLS_CFLAGS)
multiplex_y4m_la_LDFLAGS = -module -avoid-version
multiplex_y4m_la_LIBADD = $(MJPEGTOOLS_LIBS)

