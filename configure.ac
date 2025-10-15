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

# Check for Jansson library
PKG_CHECK_MODULES([JANSSON], [jansson >= 2.7], [have_jansson=yes], [have_jansson=no])
if test "x$have_jansson" = "xyes"; then
    AC_DEFINE([HAVE_JANSSON], 1, [Define if Jansson library is available])
else
    AC_MSG_WARN([Jansson library not found. JSON output support will be disabled.])
fi
AM_CONDITIONAL([HAVE_JANSSON], [test "x$have_jansson" = "xyes"])

# Checking doxygen argument and presence
PATH_DOXYGEN
PATH_DOT

# Allow running test suites if check was found on system
PKG_CHECK_MODULES([CHECK], [check >= 0.9.4], [have_check=yes], [have_check=no])
AM_CONDITIONAL([HAVE_CHECK], [test "x$have_check" = "xyes"])

AM_PROG_LIBTOOL
AC_CONFIG_SUBDIRS([third-party/libpuzzle third-party/libarchive])

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


AC_MSG_RESULT([
** Configuration summary for $PACKAGE $VERSION:

   Support for puzzle library:       yes (vendored)
   Support for archive library:      yes (vendored)
   Support for zlib library:         $with_zlib
   Support for bz2 library:          $with_bz2
   Support for JSON output:          $have_jansson
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
