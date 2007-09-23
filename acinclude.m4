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
