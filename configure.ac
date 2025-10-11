AC_PREREQ([2.71])

# Required initializer
AC_INIT([ftwin],[m4_normalize(m4_include(VERSION))],[francois.pesce@gmail.com])


dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([src/ftwin.c])
AC_CONFIG_HEADERS([src/config.h])

AM_INIT_AUTOMAKE([subdir-objects])

# Add a test for a compiler.
AC_PROG_CC

# APR Checking
APR_CONFIG_CHECK
APR_UTIL_CONFIG_CHECK

# PCRE Checking
PCRE_CONFIG_CHECK

# Checking doxygen argument and presence
PATH_DOXYGEN
PATH_DOT

# Allow running test suites if check was found on system
PKG_CHECK_MODULES(CHECK, check >= 0.9.4,
                  AM_CONDITIONAL(HAVE_CHECK, true),
                  AM_CONDITIONAL(HAVE_CHECK, false)
                  )

AM_PROG_LIBTOOL
AC_CONFIG_SUBDIRS([third-party/libpuzzle])
AC_DEFINE([HAVE_PUZZLE], 1, [for image comparison mode])

# Check libarchive
AC_ARG_WITH([archive],
            [AS_HELP_STRING([--with-archive], [/absolute/path/to/libarchive-base])],
            [ARCHIVE_LDFLAGS="-L$with_archive/lib";
             ARCHIVE_LDADD="-larchive";
             ARCHIVE_CPPFLAGS="-I$with_archive/include/";
             AC_SUBST(ARCHIVE_LDFLAGS)
             AC_SUBST(ARCHIVE_LDADD)
             AC_SUBST(ARCHIVE_CPPFLAGS)
             AC_MSG_RESULT([$with_archive])
             AC_DEFINE(HAVE_ARCHIVE, 1, [libarchive available in the system])
             ],
            [AC_MSG_RESULT([no])])

# Check zlib
ZLIB

# Check bz2
BZ2

USER_CFLAGS=$CFLAGS
CFLAGS=""
AC_SUBST(USER_CFLAGS)

PWD=`pwd`
ABS_TOP_SRCDIR=$PWD$top_srcdir
AC_SUBST(ABS_TOP_SRCDIR)

case $host in
x86_64-*-*) CFLAGS="-march=nocona -mfpmath=sse,387 -DCPU=64" ;;
i686-*-*)   CFLAGS="-march=i686 -malign-double -DCPU=686" ;;
*)          CFLAGS= ;;
esac

AC_MSG_RESULT([
** Configuration summary for $PACKAGE $VERSION:

   Support for puzzle library:       $with_puzzle
   Support for archive library:      $with_archive
   Support for zlib library:         $with_zlib
   Support for bz2 library:          $with_bz2
])

# Coverage
AC_ARG_ENABLE([coverage],
    [AS_HELP_STRING([--enable-coverage], [Enable code coverage])],
    [
        if test "$enableval" = "yes"; then
            COVERAGE_CFLAGS="-fprofile-arcs -ftest-coverage"
            COVERAGE_LIBS="-lgcov"
        fi
    ]
)
AC_SUBST(COVERAGE_CFLAGS)
AC_SUBST(COVERAGE_LIBS)

# Write config.status and the Makefile
AC_CONFIG_FILES([Doxyfile Makefile])
AC_OUTPUT
