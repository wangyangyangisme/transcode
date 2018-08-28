# # Process this file with automake to produce Makefile.in.

EXTRA_DIST = \
	newtest.pl test.pl \
	test-tcmodchain.sh test-cfg-filelist.sh

AM_CPPFLAGS = \
	$(PTHREAD_CFLAGS) \
	-DMOD_PATH=\"$(MOD_PATH)\" \
	-DPROF_PATH=\"$(PROF_PATH)\" \
	-DFBUF_TEST \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	$(DLDARWIN_CFLAGS) \
	$(XIO_CFLAGS)

if HAVE_PVM3
PVM3_TEST = test-pvmparser
endif

noinst_PROGRAMS = \
	test-acmemcpy \
	test-acmemcpy-speed \
	test-average \
	test-bufalloc \
	test-cfg-filelist \
	test-export-profile \
	test-framecode \
	test-framealloc \
	test-imgconvert \
	test-iodir \
	test-mangle-cmdline \
	$(PVM3_TEST) \
	test-ratiocodes \
	test-resize-values \
	test-tclist \
	test-tclog \
	test-tcglob \
	test-tcmodule \
	test-tcmoduleinfo \
	test-tcstrdup

test_acmemcpy_SOURCES = test-acmemcpy.c
test_acmemcpy_LDADD = $(ACLIB_LIBS)

test_acmemcpy_speed_SOURCES = test-acmemcpy-speed.c
test_acmemcpy_speed_LDADD = $(ACLIB_LIBS)

test_average_SOURCES = test-average.c
test_average_LDADD = $(ACLIB_LIBS)

test_bufalloc_SOURCES = test-bufalloc.c
test_bufalloc_LDADD = $(LIBTC_LIBS)

test_framealloc_SOURCES = test-framealloc.c
test_framealloc_LDADD = $(LIBTC_LIBS)

test_framecode_SOURCES = test-framecode.c
test_framecode_LDADD = $(LIBTC_LIBS)

test_imgconvert_SOURCES = test-imgconvert.c
test_imgconvert_LDADD = $(ACLIB_LIBS)

test_cfg_filelist_SOURCES = test-cfg-filelist.c
test_cfg_filelist_LDADD = $(LIBTC_LIBS)

test_iodir_SOURCES = test-iodir.c
test_iodir_LDADD = $(LIBTC_LIBS)

test_ratiocodes_SOURCES = test-ratiocodes.c
test_ratiocodes_LDADD = $(LIBTC_LIBS)

test_tclist_SOURCES = test-tclist.c
test_tclist_LDADD = $(LIBTC_LIBS)

test_tclog_SOURCES = test-tclog.c
test_tclog_LDADD = $(LIBTC_LIBS)

test_tcglob_SOURCES = test-tcglob.c
test_tcglob_LDADD = $(LIBTC_LIBS)

test_tcmodule_SOURCES = test-tcmodule.c
test_tcmodule_LDADD = $(LIBTC_LIBS)
test_tcmodule_LDFLAGS = -export-dynamic

test_tcmoduleinfo_SOURCES = test-tcmoduleinfo.c
test_tcmoduleinfo_LDADD = $(LIBTC_LIBS)

test_tcstrdup_SOURCES = test-tcstrdup.c
test_tcstrdup_LDADD = $(LIBTC_LIBS)

test_mangle_cmdline_SOURCES = test-mangle-cmdline.c
test_mangle_cmdline_LDADD = $(LIBTC_LIBS)

test_export_profile_SOURCES = test-export-profile.c ../src/export_profile.c
test_export_profile_LDADD = $(LIBTC_LIBS)

test_pvmparser_SOURCES = test-pvmparser.c ../pvm3/pvm_parser.c
test_pvmparser_CFLAGS = $(PVM3_CFLAGS) -I../pvm3/
test_pvmparser_LDADD = $(LIBTC_LIBS) $(PVM3_LIBS)

test_resize_values_SOURCES = test-resize-values.c
test_resize_values_LDADD = $(LIBTC_LIBS)

# Avoid warnings on intentional empty strings in test-tclog
test-tclog$(EXEEXT): CFLAGS := $(CFLAGS) -Wno-format-zero-length
# Automake interprets that line as a rule overriding the default,
# so put the default back
test-tclog$(EXEEXT): $(test_tclog_OBJECTS) $(test_tclog_DEPENDENCIES) 
	@rm -f test-tclog$(EXEEXT)
	$(LINK) $(test_tclog_LDFLAGS) $(test_tclog_OBJECTS) $(test_tclog_LDADD) $(LIBS)


### Targets to run the tests

.PHONY: test-low test-high test-all

# Low-level tests for specific routines or functionality
LOWTESTS = test-acmemcpy test-bufalloc test-average test-framealloc \
           test-framecode test-imgconvert test-iodir test-ratiocodes \
           test-resize-values test-tcmoduleinfo test-tcstrdup
test-low: $(LOWTESTS)
	./test-acmemcpy
	./test-average
	./test-bufalloc
	./test-framealloc
	./test-framecode
	./test-imgconvert -C -v
	./test-iodir
	./test-mangle-cmdline
	./test-ratiocodes
	./test-resize-values
	./test-tcmoduleinfo
	./test-tcstrdup

# High-level tests for transcode as a whole
# FIXME xvid broken?
test-high: newtest.pl test-tcmodchain.sh
	perl newtest.pl -T'-y xvid4'
	bash test-tchmodchain.sh

# Run all tests
test-all: test-low test-high
