# # Process this file with automake to produce Makefile.in.

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src

noinst_LTLIBRARIES = libtc.la

if !HAVE_GETOPT_LONG_ONLY
GETOPT_FILES = getopt.c getopt1.c
endif

if HAVE_IBP
XIO_FILES = libxio.c
endif

if HAVE_GETTIMEOFDAY
TIMER_FILES = tctimer.c
endif

libtc_la_SOURCES = \
	cfgfile.c \
	framecode.c \
	iodir.c \
	optstr.c \
	ratiocodes.c \
	strlcat.c \
	strlcpy.c \
	tc_functions.c \
	tccodecs.c \
	tcframes.c \
	tcglob.c \
	tclist.c \
	tcmodule.c \
	tcmoduleinfo.c \
	$(GETOPT_FILES) \
	$(TIMER_FILES) \
	$(XIO_FILES)

EXTRA_DIST = \
	cfgfile.h \
	framecode.h \
	getopt.h \
	iodir.h \
	libtc.h \
	optstr.h \
	ratiocodes.h \
	static_xio.h \
	static_optstr.h \
	static_tclist.h \
	static_tctimer.h \
	tc_lzo.h \
	tcavcodec.h \
	tccodecs.h \
	tcformats.h \
	tcframes.h \
	tcglob.h \
	tclist.h \
	tcmodule-core.h \
	tcmodule-data.h \
	tcmodule-info.h \
	tcmodule-plugin.h \
	tctimer.h \
	xio.h
