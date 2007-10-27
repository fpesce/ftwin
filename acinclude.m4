AC_DEFUN([APR_CONFIG_CHECK],[
	AC_ARG_WITH( apr-config, AC_HELP_STRING([--with-apr-config=PATH], [prefix where libapr is installed default=auto]), [apr_config=$withval],[apr_config=])
	if test "x$apr_config" != "x"
	    then
	    # If we passed a apr-config
	    if test -f $apr_config
	        then
	        APR_CFLAGS="`$apr_config --cflags` -DHAVE_APR" 
	        APR_CPPFLAGS="`$apr_config --cppflags --includes`"
	        APR_LTLIBS="`$apr_config --libs --link-libtool`"
	        APR_LIBS="`$apr_config --libs --link-ld`"
	    else
		AC_MSG_ERROR([apr-config program not found (1), please make sure you installed devel files for libapr])
	    fi
	else
	    # else check apr install with apr-config
	    AC_PATH_PROG(apr_config, apr-config)
	    if test "x$apr_config" != "x"
		then
		APR_CFLAGS="`$apr_config --cflags` -DHAVE_APR" 
		APR_CPPFLAGS="`$apr_config --cppflags --includes`"
		APR_LTLIBS="`$apr_config --libs --link-libtool`"
		APR_LIBS="`$apr_config --libs --link-ld`"
	    else
		# Try a mandriva standard name
		AC_PATH_PROG(apr_config, apr-1-config)
		if test "x$apr_config" != "x"
		    then
		    APR_CFLAGS="`$apr_config --cflags` -DHAVE_APR" 
		    APR_CPPFLAGS="`$apr_config --cppflags --includes`"
		    APR_LTLIBS="`$apr_config --libs --link-libtool`"
		    APR_LIBS="`$apr_config --libs --link-ld`"
		else
		    AC_MSG_ERROR([apr-config program not found (2), please make sure you installed devel files for libapr])
		fi
	    fi
	fi

	AC_SUBST([apr_config])
	AC_SUBST([APR_CFLAGS])
	AC_SUBST([APR_CPPFLAGS])
	AC_SUBST([APR_LTLIBS])
	AC_SUBST([APR_LIBS])

    ])

AC_DEFUN([APR_UTIL_CONFIG_CHECK], [
	AC_ARG_WITH( apr-util-config, AC_HELP_STRING([--with-apr-util-config=PATH], [prefix where apu-config is installed default=auto]), [apr_util_config=$withval],[apr_util_config=])

	if test "x$apr_util_config" != "x"
	    then
	    # If we passed a apr-util-config
	    if test -f $apr_util_config
	        then
		APU_LTLIBS="`$apr_util_config --libs --link-libtool`"
		APU_LIBS="`$apr_util_config --libs --link-ld`"
	    else
		AC_MSG_ERROR([apu-config program not found (1), please make sure you installed devel files for libaprutil])
	    fi
	else
	    AC_PATH_PROG(apr_util_config, apu-config)
	    if test "x$apr_util_config" != "x"
		then
		APU_LTLIBS="`$apr_util_config --libs --link-libtool`"
		APU_LIBS="`$apr_util_config --libs --link-ld`"
	    else
		# else check apr install with apr-config
		AC_PATH_PROG(apr_util_config, apu-1-config)
		if test "x$apr_util_config" != "x"
		    then
		    APU_LTLIBS="`$apr_util_config --libs --link-libtool`"
		    APU_LIBS="`$apr_util_config --libs --link-ld`"
		else
		    AC_MSG_ERROR([apu-config program not found (2), please make sure you installed devel files for libaprutil])
		fi
	    fi
	fi
	AC_SUBST([APU_LTLIBS])
	AC_SUBST([APU_LIBS])

    ])

AC_DEFUN([PATH_DOXYGEN],[
	AC_ARG_VAR([DOXYGEN], [Full path to doxygen binary.])
	AC_PATH_PROG([DOXYGEN], [doxygen],,)

	if test "x$DOXYGEN" = 'x'; then
	    AC_MSG_WARN([*** doxygen not found, docs will not be available])
	fi

	AM_CONDITIONAL(HAVE_DOXYGEN, test "x$DOXYGEN" != 'x')

	AC_SUBST([DOXYGEN])
	])

AC_DEFUN([PATH_DOT],[
	AC_ARG_VAR([DOT], [Full path to dot binary.])
	AC_PATH_PROG([DOT], [dot],,)

	if test "x$DOT" = 'x'; then
	    AC_MSG_WARN([*** dot not found, graphs will not be available. Please install graphviz wich includes dot.])
	    HAVE_DOT="NO"
	else
	    HAVE_DOT="YES"
	fi

	AC_SUBST([DOT])
	AC_SUBST([HAVE_DOT])
	])

#
# PCRE_CHECK
#
AC_DEFUN([PCRE_CONFIG_CHECK],[
	AC_ARG_WITH( pcre-config, AC_HELP_STRING([--with-pcre-config=PATH], [prefix where libpcre is installed default=auto]), [pcre_config=$withval],[pcre_config=])
	if test "x$pcre_config" != "x"
	    then
	    # If we passed a pcre-config
	    if test -f $pcre_config
		then
		PCRE_CFLAGS="`$pcre_config --cflags`"
		PCRE_LIBS="`$pcre_config --libs`"
	    else
		AC_MSG_ERROR([pcre-config program not found (1), please make sure you installed devel files for libpcre])
	    fi
	else
	    # else check pcre install with pcre-config
	    AC_PATH_PROG(pcre_config, pcre-config)
	    if test "x$pcre_config" != "x"
		then
		PCRE_CFLAGS="`$pcre_config --cflags`"
		PCRE_LIBS="`$pcre_config --libs`"
	    else
		AC_MSG_ERROR([pcre-config program not found (2), please make sure you installed devel files for libpcre])
	    fi
	fi
	AC_SUBST([pcre_config])
	AC_SUBST([PCRE_CFLAGS])
	AC_SUBST([PCRE_LIBS])

    ])

AC_DEFUN([PATH_CHECK], [
      m4_ifdef([AM_PATH_CHECK],[
              AM_PATH_CHECK(0.9.2, 
                  [
                      HAVE_CHECK=yes
                      ], 
                  [
                      HAVE_CHECK=no
                      ])
              ])

      AM_CONDITIONAL(HAVE_CHECK, test x$HAVE_CHECK = xyes)

      if test "x$HAVE_CHECK" != "xyes"; then
          if test x$HAVE_CHECK = xno; then
              AC_MSG_WARN([*** Invalid check version, you can download the latest one at http://check.sf.net])
          else
              AC_MSG_WARN([*** Check not found, you can download the latest version at http://check.sf.net])
          fi
      fi
      ])

#
# libpuzzle is used to compare two images
#
AC_DEFUN([PUZZLE],[
	AC_ARG_WITH( puzzle, AC_HELP_STRING([--with-puzzle=PATH], [prefix where libpuzzle is installed default=/usr/local/]), [puzzle=$withval],[puzzle=/usr/local/])
	if test "x$puzzle" != "x"
	    then
	    #
	    # Make sure we have "puzzle.h".  If we don't, it means we probably
	    # don't have libpuzzle, so don't use it.
	    #
	    AC_CHECK_HEADER(puzzle.h,
		[
		# Check if the lib is OK
		AC_CHECK_LIB(puzzle, puzzle_init_context,
		    [
		     AC_DEFINE([HAVE_PUZZLE], 1, [for image comparison mode])
		     with_puzzle=yes
		     PUZZLE_CPPFLAGS="-I$puzzle/include"
		     PUZZLE_LDFLAGS="-L$puzzle/lib"
		     PUZZLE_LDADD="-lpuzzle"
		    ],
		    [
		     with_puzzle=no
		     AC_DEFINE([HAVE_PUZZLE], 0, [for image comparison mode])
		    ])	
		],
		[
		 with_puzzle=no
		 AC_DEFINE([HAVE_PUZZLE], 0, [for image comparison mode])
		])

	else
	    with_puzzle=no
	    AC_DEFINE([HAVE_PUZZLE], 0, [for image comparison mode])
	fi
	AC_SUBST([with_puzzle])
	AC_SUBST([PUZZLE_CPPFLAGS])
	AC_SUBST([PUZZLE_LDFLAGS])
	AC_SUBST([PUZZLE_LDADD])
    ])


#
# libtar is used to compare two images
#
AC_DEFUN([TAR],[
	AC_ARG_WITH( tar, AC_HELP_STRING([--with-tar=PATH], [prefix where libtar is installed default=/usr/local/]), [tar=$withval],[tar=/usr/local/])
	if test "x$tar" != "x"
	    then
	    #
	    # Make sure we have "libtar.h".  If we don't, it means we probably
	    # don't have libtar, so don't use it.
	    #
	    AC_CHECK_HEADER(libtar.h,
		[
		# Check if the lib is OK
		AC_CHECK_LIB(tar, tar_open,
		    [
		     AC_DEFINE([HAVE_TAR], 1, [for image comparison mode])
		     with_tar=yes
		     TAR_CPPFLAGS="-I$tar/include"
		     TAR_LDFLAGS="-L$tar/lib"
		     TAR_LDADD="-ltar"
		    ],
		    [
		     with_tar=no
		     AC_DEFINE([HAVE_TAR], 0, [for image comparison mode])
		    ])	
		],
		[
		 with_tar=no
		 AC_DEFINE([HAVE_TAR], 0, [for image comparison mode])
		])

	else
	    with_tar=no
	    AC_DEFINE([HAVE_TAR], 0, [for image comparison mode])
	fi
	AC_SUBST([with_tar])
	AC_SUBST([TAR_CPPFLAGS])
	AC_SUBST([TAR_LDFLAGS])
	AC_SUBST([TAR_LDADD])
    ])

#
# libz used to uncompress .tar.gz for the moment.
#
AC_DEFUN([ZLIB],[
	AC_ARG_WITH(zlib, AC_HELP_STRING([--with-zlib=PATH], [prefix where zlib is installed default=/usr]), [zlib=$withval],[zlib=/usr/])
	if test "x$zlib" != "x"
	    then
	    #
	    # Make sure we have "zlib.h".  If we don't, it means we probably
	    # don't have libzlib, so don't use it.
	    #
	    AC_CHECK_HEADER(zlib.h,
		[
		# Check if the lib is OK
		AC_CHECK_LIB(z, gzread,
		    [
		     AC_DEFINE([HAVE_LIBZ], 1, [for image comparison mode])
		     with_zlib=yes
		     ZLIB_CPPFLAGS="-I$zlib/include"
		     ZLIB_LDFLAGS="-L$zlib/lib"
		     ZLIB_LDADD="-lz"
		    ],
		    [
		     with_zlib=no
		     AC_DEFINE([HAVE_ZLIB], 0, [for image comparison mode])
		    ])	
		],
		[
		 with_zlib=no
		 AC_DEFINE([HAVE_ZLIB], 0, [for image comparison mode])
		])

	else
	    with_zlib=no
	    AC_DEFINE([HAVE_ZLIB], 0, [for image comparison mode])
	fi
	AC_SUBST([with_zlib])
	AC_SUBST([ZLIB_CPPFLAGS])
	AC_SUBST([ZLIB_LDFLAGS])
	AC_SUBST([ZLIB_LDADD])
    ])
