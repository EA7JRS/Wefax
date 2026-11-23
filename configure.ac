AC_INIT([wefax], [0.4.0])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_PROG_CC
AC_CHECK_LIB(m,atan2,,[AC_MSG_ERROR([the maths library is missing])])
AC_CHECK_LIB(z,compress2,,[AC_MSG_ERROR([zlib is missing: install zlib1g-dev])])
AC_CHECK_LIB(cjson,cJSON_CreateArray,,[AC_MSG_ERROR([cJSON is missing: install libcjson-dev])])
AC_CHECK_LIB(sqlite3,sqlite3_open,,[AC_MSG_ERROR([SQLite is missing: install libsqlite3-dev])])
# The SDRplay API installs under /usr/local
CPPFLAGS="$CPPFLAGS -I/usr/local/include"
LDFLAGS="$LDFLAGS -L/usr/local/lib"
# Receivers: each one is optional, but at least one is needed to receive
AC_CHECK_LIB(sdrplay_api,sdrplay_api_Open,[have_sdrplay=yes],[have_sdrplay=no])
AC_CHECK_LIB(rtlsdr,rtlsdr_open,[have_rtlsdr=yes],[have_rtlsdr=no])
AC_CHECK_LIB(hackrf,hackrf_start_rx,[have_hackrf=yes],[have_hackrf=no])
RECEIVER_LIBS=""
if test "$have_sdrplay" = yes; then
  AC_DEFINE([HAVE_LIBSDRPLAY_API],[1],[SDRplay receivers])
  RECEIVER_LIBS="$RECEIVER_LIBS -lsdrplay_api"
fi
if test "$have_rtlsdr" = yes; then
  AC_DEFINE([HAVE_LIBRTLSDR],[1],[RTL-SDR receivers])
  RECEIVER_LIBS="$RECEIVER_LIBS -lrtlsdr"
  AC_CHECK_LIB(rtlsdr,rtlsdr_set_bias_tee,[AC_DEFINE([HAVE_RTLSDR_BIAS_TEE],[1],[bias T control])])
fi
if test "$have_hackrf" = yes; then
  AC_DEFINE([HAVE_LIBHACKRF],[1],[HackRF One receivers])
  RECEIVER_LIBS="$RECEIVER_LIBS -lhackrf"
fi
AC_SUBST([RECEIVER_LIBS])
if test "$have_sdrplay" = no && test "$have_rtlsdr" = no && test "$have_hackrf" = no; then
  AC_MSG_WARN([none of the SDRplay API, librtlsdr and libhackrf were found: wefaxreceiver will only decode recordings or use a KiwiSDR])
fi
AC_CHECK_LIB(microhttpd,MHD_start_daemon,[true],[AC_MSG_ERROR([libmicrohttpd is missing: install libmicrohttpd-dev])])
AC_CONFIG_FILES([Makefile receiver/Makefile web_server/Makefile])
AC_OUTPUT
AC_MSG_NOTICE([receivers: SDRplay $have_sdrplay, RTL-SDR $have_rtlsdr, HackRF $have_hackrf])
