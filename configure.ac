AC_PREREQ(2.52)

# Required initializer
AC_INIT([ftwin], m4_normalize(m4_include([VERSION])), [francois.pesce@gmail.com])

# Automake initialization
AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([src/ftwin.c])
AC_CONFIG_HEADER([src/config.h])

# Add a test for a compiler.
AC_HEADER_STDC

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
PATH_CHECK

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

# Write config.status and the Makefile
AC_CONFIG_FILES([Doxyfile Makefile])
AC_OUTPUT
