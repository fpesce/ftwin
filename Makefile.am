AUTOMAKE_OPTIONS = foreign dist-bzip2
CLEANFILES = *~ check_test_log.xml check_log.xml
MAINTAINERCLEANFILES = aclocal.m4 Makefile.in compile config.guess config.sub \
                       configure depcomp install-sh ltmain.sh missing

SUBDIRS = third-party/libpuzzle
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
	     check/tests/truerand check/tests/copyrand check/tests/testrand

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
		  src/lookup3.h \
		  src/ft_file.h \
		  src/napr_threadpool.h

ftwin_SOURCES = src/ftwin.c \
		   src/napr_hash.c \
		   src/napr_heap.c \
		   src/checksum.c \
		   src/lookup3.c \
		   src/ft_file.c \
		   src/napr_threadpool.c

check_ftwin_SOURCES = check/check_ftwin.c check/check_napr_heap.c src/napr_heap.c \
		      check/check_apr_hash.c check/check_ft_file.c src/ft_file.c \
		      src/checksum.c

# CFLAGS is for additional C compiler flags
ftwin_CFLAGS = @APR_CFLAGS@ @PCRE_CFLAGS@ -Wall -Werror -g -ggdb -I$(top_srcdir)/src -O0 @COVERAGE_CFLAGS@
# -O3 -funroll-loops -fomit-frame-pointer -pipe -ffast-math
check_ftwin_CFLAGS = @APR_CFLAGS@ @PCRE_CFLAGS@ -Wall -g -ggdb -I$(top_srcdir)/src/ @COVERAGE_CFLAGS@

# CPPFLAGS is for -I and -D options (involving C preprocessor)
check_ftwin_CPPFLAGS = @CHECK_CFLAGS@ @APR_CPPFLAGS@ @ARCHIVE_CPPFLAGS@ @ZLIB_CPPFLAGS@ @BZ2_CPPFLAGS@ -DCHECK_DIR=\"$(top_srcdir)/check\" -I$(top_srcdir)/third-party/libpuzzle/src
ftwin_CPPFLAGS = @APR_CPPFLAGS@ @ARCHIVE_CPPFLAGS@ @ZLIB_CPPFLAGS@ @BZ2_CPPFLAGS@ -I$(top_srcdir)/third-party/libpuzzle/src

# LDADD and LIBADD are for linking libraries, -L, -l, -dlopen and -dlpreopen options
check_ftwin_LDADD = @CHECK_LIBS@ @APR_LIBS@ @APU_LIBS@ @PCRE_LIBS@ @ZLIB_LDADD@ @BZ2_LDADD@ @ARCHIVE_LDADD@ @COVERAGE_LIBS@ -lgd third-party/libpuzzle/src/libpuzzle.la
ftwin_LDADD = @APR_LIBS@ @APU_LIBS@ @PCRE_LIBS@ @ZLIB_LDADD@ @BZ2_LDADD@ @ARCHIVE_LDADD@ @COVERAGE_LIBS@ -lgd third-party/libpuzzle/src/libpuzzle.la

# LDFLAGS is for additional linker flags
check_ftwin_LDFLAGS = @ARCHIVE_LDFLAGS@ @ZLIB_LDFLAGS@ @BZ2_LDFLAGS@
ftwin_LDFLAGS = @ARCHIVE_LDFLAGS@ @ZLIB_LDFLAGS@ @BZ2_LDFLAGS@

bench_ftwin_SOURCES = check/bench_ftwin.c src/checksum.c src/ft_file.c
bench_ftwin_CFLAGS = @APR_CFLAGS@ -Wall -g -ggdb -I$(top_srcdir)/src
bench_ftwin_CPPFLAGS = @APR_CPPFLAGS@
bench_ftwin_LDADD = @APR_LIBS@ @APU_LIBS@

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

.PHONY: benchmark junit coverage vendor

vendor:
	./setup_vendor.sh

# Hook to clean up generated report files and directories
clean-local:
	rm -rf $(REPORTS_DIR)
