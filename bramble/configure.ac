AC_INIT([bramble],
        m4_esyscmd([git describe --always | awk '/.*/ {sub(/^v/, ""); printf "%s",$1; exit}']))
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
AC_CANONICAL_SYSTEM

AM_INIT_AUTOMAKE([subdir-objects tar-pax foreign])
AM_SILENT_RULES([yes])
AM_CONFIG_HEADER([config/config.h])
AM_MAINTAINER_MODE([enable])

AC_DEFINE([_GNU_SOURCE], 1,
  [Define _GNU_SOURCE so that we get all necessary prototypes])

AC_PROG_CC_C99
AM_PROG_CC_C_O

LT_INIT
AC_HEADER_STDC

AC_SEARCH_LIBS([ev_run], [ev], [], [
  AC_MSG_ERROR([Please install libev development package])
])

AC_CONFIG_FILES( \
  Makefile \
)

AC_OUTPUT
