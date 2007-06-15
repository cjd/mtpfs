AC_INIT([MTPfs], [0.6], [Chris Debenham <chris@adebenham.com>],[mtpfs])
AM_INIT_AUTOMAKE
AC_PROG_CC
AC_PROG_INSTALL
PKG_CHECK_MODULES(FUSE, fuse >= 2.2 \
                        glib-2.0 >= 2.6 \
                        gthread-2.0 >= 1.2 \
                        mad >= 0.15 \
                        id3tag >= 0.15 \
                        libmtp >= 0.0.9)
AC_SUBST(FUSE_CFLAGS)
AC_SUBST(FUSE_LIBS)

AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--enable-debug],
                             [enable debugging features]),
              , enable_debug=no)
if test "x$enable_debug" != xyes -a "x$enable_debug" != xno; then
   AC_MSG_ERROR(You may not pass an argument to --enable-debug)
fi

if test "x$enable_debug" = xyes; then
   AC_DEFINE(DEBUG,1,[Define if debug logging is enabled])
else
   AC_DEFINE(DEBUG,0,[Define if debug logging is enabled])
fi

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
