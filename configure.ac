AC_INIT([MTPfs], [1.0], [Chris Debenham <chris@adebenham.com>],[mtpfs])
AM_INIT_AUTOMAKE
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_INSTALL

PKG_CHECK_MODULES(FUSE, fuse >= 2.2 \
                        glib-2.0 >= 2.6 \
                        gthread-2.0 >= 1.2 \
                        libmtp >= 0.0.9)
AC_SUBST(FUSE_CFLAGS)
AC_SUBST(FUSE_LIBS)

PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.6 \
                        gthread-2.0 >= 1.2 \
                        gio-2.0 >= 2.6)
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

AC_ARG_ENABLE([mad],
    [AS_HELP_STRING([--disable-mad], [disable libmad handling of mp3 files])])

AS_IF([test "x$enable_mad" != "xno"] , [
    PKG_CHECK_MODULES(MAD, mad >= 0.15 \
                           id3tag >= 0.15)
    AC_SUBST(MAD_CFLAGS)
    AC_SUBST(MAD_LIBS)
])
AM_CONDITIONAL([USEMAD], [test "x$enable_mad" != "xno"])

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
