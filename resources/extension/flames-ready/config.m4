dnl config.m4 - Flames Ready PHP Extension build configuration
dnl
dnl Usage:
dnl   phpize
dnl   ./configure --enable-flames-ready
dnl   make
dnl   make install

PHP_ARG_ENABLE(
    [flames_ready],
    [whether to enable Flames Ready support],
    [AS_HELP_STRING(
        [--enable-flames-ready],
        [Enable Flames Ready persistent worker extension])],
    [no])

if test "$PHP_FLAMES_READY" != "no"; then
    AC_DEFINE(HAVE_FLAMES_READY, 1, [Whether Flames Ready is enabled])
    PHP_NEW_EXTENSION(flames_ready, flames_ready.c flames_ready_fcgi.c, $ext_shared)
    PHP_ADD_MAKEFILE_FRAGMENT
fi
