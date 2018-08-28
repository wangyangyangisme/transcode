# # Process this file with automake to produce Makefile.in.

if WORDS_BIGENDIAN
BG = -DBIG_ENDIAN
endif

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	$(BG) \
	$(SIMD_FLAGS) \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src 

# SIMD_FLAGS shouls contain them, but they are empty
if HAVE_ASM_SSE2	
AM_CPPFLAGS += -msse2 
endif


pkgdir = $(MOD_PATH)

pkg_LTLIBRARIES = filter_stabilize.la filter_transform.la

filter_stabilize_la_SOURCES = filter_stabilize.c transform.c 
filter_stabilize_la_LDFLAGS = -module -avoid-version

filter_transform_la_SOURCES = filter_transform.c transform.c
filter_transform_la_LDFLAGS = -module -avoid-version

EXTRA_DIST = \
        transform.h 
