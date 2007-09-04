AUTOMAKE_OPTIONS = foreign dist-bzip2
CLEANFILES= *~ check_test_log.xml check_log.xml
MAINTAINERCLEANFILES=aclocal.m4 Makefile.in compile config.guess config.sub configure depcomp \
	install-sh ltmain.sh missing

bin_PROGRAMS = ftwin

if HAVE_CHECK
TESTS = check_ftwin
check_PROGRAMS = check_ftwin
endif

DISTCHECK_CONFIGURE_FLAGS = "--with-apr-config=@apr_config@" "--with-pcre-config=@pcre_config@"

EXTRA_DIST =

## For doxygen building
dox: Doxyfile
if HAVE_DOXYGEN
	@echo "Running doxygen..."
	@doxygen
else
	@echo "doxygen was not found during configure. Aborting."
endif

## Define the source files
include_HEADERS = src/debug.h \
		  src/napr_hash.h \
		  src/napr_heap.h \
		  src/checksum.h \
		  src/lookup3.h

ftwin_SOURCES = src/ftwin.c \
		   src/napr_hash.c \
		   src/napr_heap.c \
		   src/checksum.c \
		   src/lookup3.c

check_ftwin_SOURCES = check/check_ftwin.c check/check_napr_heap.c src/napr_heap.c

# CFLAGS is for additional C compiler flags
ftwin_CFLAGS = @APR_CFLAGS@ @PCRE_CFLAGS@ -Wall -Werror -g -ggdb -I$(top_srcdir)/src -O0
# -O3 -funroll-loops -fomit-frame-pointer -pipe -ffast-math
check_ftwin_CFLAGS = @APR_CFLAGS@ @PCRE_CFLAGS@ -Wall -Werror -g -ggdb -I$(top_srcdir)/src/

# CPPFLAGS is for -I and -D options (involving C preprocessor)
check_ftwin_CPPFLAGS = @CHECK_CFLAGS@ @APR_CPPFLAGS@
ftwin_CPPFLAGS = @APR_CPPFLAGS@

# LDADD and LIBADD are for linking libraries, -L, -l, -dlopen and -dlpreopen options
check_ftwin_LDADD = @CHECK_LIBS@ @APR_LIBS@ @APU_LIBS@ @PCRE_LIBS@
ftwin_LDADD = @APR_LIBS@ @APU_LIBS@ @PCRE_LIBS@

# LDFLAGS is for additional linker flags
check_ftwin_LDFLAGS = 
ftwin_LDFLAGS = 