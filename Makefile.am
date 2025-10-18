AUTOMAKE_OPTIONS = foreign dist-bzip2
CLEANFILES = *~ check_test_log.xml check_log.xml
MAINTAINERCLEANFILES = aclocal.m4 Makefile.in compile config.guess config.sub \
                       configure depcomp install-sh ltmain.sh missing

SUBDIRS = third-party/libpuzzle third-party/libarchive
bin_PROGRAMS = ftwin

check_PROGRAMS =
if HAVE_CHECK
TESTS = check_ftwin
check_PROGRAMS += check_ftwin
endif
check_PROGRAMS += bench_ftwin

DISTCHECK_CONFIGURE_FLAGS = "--with-apr-config=@apr_config@" "--with-pcre-config=@pcre_config@"

EXTRA_DIST = TODO CHANGES EXAMPLES README LICENSE KNOWN_BUGS \
	     docs/ftwin.8 check/tests/ check/tests/second \
	     check/tests/truerand check/tests/copyrand check/tests/testrand \
	     setup.sh patches/

man8_MANS = docs/ftwin.8

## For doxygen building
dox: Doxyfile
if HAVE_DOXYGEN
	@echo "Running doxygen..."
	@doxygen
else
	@echo "doxygen was not found during configure. Aborting."
endif

## Define the source files
noinst_HEADERS = src/debug.h \
		  src/napr_hash.h \
		  src/napr_heap.h \
		  src/checksum.h \
		  src/ft_file.h \
		  src/ftwin.h \
		  src/human_size.h \
		  src/napr_threadpool.h \
		  src/ft_system.h \
		  src/ft_ignore.h \
		  src/key_hash.h \
		  src/ft_types.h \
		  src/ft_archive.h \
		  src/ft_report.h \
		  src/ft_report_json.h \
		  src/ft_image.h

ftwin_SOURCES = src/ftwin.c \
		   src/ft_config.c \
		   src/ft_traverse.c \
		   src/ft_process.c \
		   src/ft_archive.c \
		   src/ft_report.c \
		   src/ft_report_json.c \
		   src/ft_image.c \
		   src/napr_hash.c \
		   src/napr_heap.c \
		   src/vendor/xxhash/xxhash.c \
		   src/ft_file.c \
		   src/human_size.c \
		   src/napr_threadpool.c \
		   src/ft_system.c \
		   src/ft_ignore.c \
		   src/key_hash.c

check_ftwin_SOURCES = check/check_ftwin.c check/check_napr_heap.c src/napr_heap.c \
		      check/check_napr_hash.c check/check_ft_file.c src/ft_file.c \
		      src/vendor/xxhash/xxhash.c src/ftwin.c \
		      src/napr_hash.c src/napr_threadpool.c \
		      check/check_human_size.c src/human_size.c src/ft_system.c \
		      check/check_ft_system.c check/check_parallel_hashing.c src/ft_ignore.c \
		      check/check_ft_ignore.c src/key_hash.c src/ft_config.c src/ft_traverse.c \
		      src/ft_process.c src/ft_archive.c src/ft_report.c src/ft_report_json.c \
		      src/ft_image.c check/check_ft_archive.c check/check_ft_image.c

# CFLAGS is for additional C compiler flags
ftwin_CFLAGS = @APR_CFLAGS@ @PCRE_CFLAGS@ $(JANSSON_CFLAGS) -Wall -Werror -g -ggdb -I$(top_srcdir)/src -O0 @COVERAGE_CFLAGS@
# -O3 -funroll-loops -fomit-frame-pointer -pipe -ffast-math
check_ftwin_CFLAGS = @APR_CFLAGS@ @PCRE_CFLAGS@ $(JANSSON_CFLAGS) -Wall -g -ggdb -I$(top_srcdir)/src/ @COVERAGE_CFLAGS@

# CPPFLAGS is for -I and -D options (involving C preprocessor)
check_ftwin_CPPFLAGS = @CHECK_CFLAGS@ @APR_CPPFLAGS@ @ZLIB_CPPFLAGS@ @BZ2_CPPFLAGS@ -DCHECK_DIR=\"$(top_srcdir)/check\" -DFTWIN_TEST_BUILD -I$(top_srcdir)/third-party/libpuzzle/src -I$(top_srcdir)/third-party/libarchive/libarchive -I$(top_srcdir)/src/vendor/xxhash
ftwin_CPPFLAGS = @APR_CPPFLAGS@ @ZLIB_CPPFLAGS@ @BZ2_CPPFLAGS@ -I$(top_srcdir)/third-party/libpuzzle/src -I$(top_srcdir)/third-party/libarchive/libarchive -I$(top_srcdir)/src/vendor/xxhash

# LDADD and LIBADD are for linking libraries, -L, -l, -dlopen and -dlpreopen options
check_ftwin_LDADD = @CHECK_LIBS@ $(JANSSON_LIBS) @APR_LIBS@ @APU_LIBS@ @PCRE_LIBS@ @ZLIB_LDADD@ @BZ2_LDADD@ @COVERAGE_LIBS@ -lgd third-party/libpuzzle/src/libpuzzle.la third-party/libarchive/.libs/libarchive.la
ftwin_LDADD = $(JANSSON_LIBS) @APR_LIBS@ @APU_LIBS@ @PCRE_LIBS@ @ZLIB_LDADD@ @BZ2_LDADD@ @COVERAGE_LIBS@ -lgd third-party/libpuzzle/src/libpuzzle.la third-party/libarchive/.libs/libarchive.la

# LDFLAGS is for additional linker flags
check_ftwin_LDFLAGS = @ZLIB_LDFLAGS@ @BZ2_LDFLAGS@
ftwin_LDFLAGS = @ZLIB_LDFLAGS@ @BZ2_LDFLAGS@

bench_ftwin_SOURCES = check/bench_ftwin.c src/vendor/xxhash/xxhash.c src/ft_file.c \
		      src/ftwin.c src/napr_hash.c src/napr_heap.c \
		      src/napr_threadpool.c src/human_size.c src/ft_system.c src/ft_ignore.c \
		      src/key_hash.c src/ft_config.c src/ft_traverse.c src/ft_process.c \
		      src/ft_archive.c src/ft_report.c src/ft_report_json.c src/ft_image.c
bench_ftwin_CFLAGS = @APR_CFLAGS@ @PCRE_CFLAGS@ $(JANSSON_CFLAGS) -Wall -g -ggdb -I$(top_srcdir)/src
bench_ftwin_CPPFLAGS = @APR_CPPFLAGS@ @ZLIB_CPPFLAGS@ @BZ2_CPPFLAGS@ -DFTWIN_TEST_BUILD \
		       -I$(top_srcdir)/src/vendor/xxhash -I$(top_srcdir)/third-party/libpuzzle/src \
		       -I$(top_srcdir)/third-party/libarchive/libarchive
bench_ftwin_LDADD = $(JANSSON_LIBS) @APR_LIBS@ @APU_LIBS@ @PCRE_LIBS@ @ZLIB_LDADD@ @BZ2_LDADD@ -lgd \
		    third-party/libpuzzle/src/libpuzzle.la third-party/libarchive/.libs/libarchive.la
bench_ftwin_LDFLAGS = @ZLIB_LDFLAGS@ @BZ2_LDFLAGS@

# --- CI/CD Targets ---

REPORTS_DIR = reports
DISTCLEANFILES = $(REPORTS_DIR)/benchmarks.json $(REPORTS_DIR)/junit.xml

# By default, the check framework creates a `check_test_log.xml` file.
# We can set a timeout for the tests.
TESTS_ENVIRONMENT = CK_DEFAULT_TIMEOUT=60

benchmark: bench_ftwin
	@echo "--- Running Benchmarks ---"
	mkdir -p $(REPORTS_DIR)
	./bench_ftwin > $(REPORTS_DIR)/benchmarks.json
	@echo "Benchmark JSON report generated at $(REPORTS_DIR)/benchmarks.json"

junit:
	@echo "--- Generating JUnit XML Report ---"
	$(MAKE) check
	mkdir -p $(REPORTS_DIR)
	xsltproc check_unittest.xslt check_log.xml > reports/junit.xml
	@echo "JUnit XML report generated at $(REPORTS_DIR)/junit.xml"

coverage:
	@echo "--- Generating Code Coverage Report ---"
	lcov --zerocounters --directory .
	rm -f `find . -name "*.gcda"`
	$(MAKE) check
	mkdir -p $(REPORTS_DIR)/coverage
	lcov --capture --directory . --output-file $(REPORTS_DIR)/coverage.info
	lcov --remove $(REPORTS_DIR)/coverage.info '*/check/*' --output-file $(REPORTS_DIR)/coverage.info
	genhtml $(REPORTS_DIR)/coverage.info --output-directory $(REPORTS_DIR)/coverage
	@echo "HTML coverage report generated in $(REPORTS_DIR)/coverage/"

.PHONY: benchmark junit coverage setup build-subdirs cppcheck

setup:
	./setup.sh

cppcheck:
	@echo "--- Running cppcheck static analysis ---"
	@if [ ! -f scripts/run-cppcheck.sh ]; then \
		echo "Error: scripts/run-cppcheck.sh not found"; \
		exit 1; \
	fi
	@./scripts/run-cppcheck.sh

# Define a phony target to ensure subdirectories are built (make all)
build-subdirs:
	@echo "Building dependencies in subdirectories..."
	@set -e; for subdir in $(SUBDIRS); do \
	  echo "Making all in $$subdir..."; \
	  $(MAKE) $(AM_MAKEFLAGS) -C "$$subdir" all; \
	done

# Override check-recursive to skip third-party subdirectory tests
# We depend on 'build-subdirs' to ensure libraries are ready,
# then run local tests via check-am.
check-recursive: build-subdirs
	@echo "Skipping third-party subdirectory tests..."
	@$(MAKE) $(AM_MAKEFLAGS) check-am

# Hook to clean up generated report files and directories
clean-local:
	rm -rf $(REPORTS_DIR)
