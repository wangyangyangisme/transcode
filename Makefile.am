# # Process this file with automake to produce Makefile.in.

AUTOMAKE_OPTIONS = foreign

if USE_DLDARWIN
LIBDLDARWIN = libdldarwin
endif

if HAVE_PVM3
PVM3 = pvm3
endif

SUBDIRS = \
	aclib \
	avilib \
	$(LIBDLDARWIN) \
	libtc \
	libtcaudio \
	libtcvideo \
	encode \
	export \
	filter \
	import \
	multiplex \
	$(PVM3) \
	src \
	testsuite \
	tools \
	docs

EXTRA_DIST = STYLE transcode.spec

docsdir = $(datadir)/doc/transcode
docs_DATA = ChangeLog
