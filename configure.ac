AC_PREREQ(2.61)
AC_INIT([XtoW], m4_esyscmd([date -u +%Y%m%d | tr -d '\n']))

AM_INIT_AUTOMAKE([foreign dist-bzip2])
AC_CONFIG_HEADERS([config.h])
AM_SILENT_RULES([yes])

# Initialize libtool
AC_PROG_LIBTOOL

# Require xorg-macros: XORG_DEFAULT_OPTIONS BASE_CFLAGS
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.16 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.16)
XORG_DEFAULT_OPTIONS

CFLAGS="-std=c99 -Wall -Wextra -Wmissing-prototypes $CFLAGS"

# check for programs
AC_PROG_CC
AC_PROG_INSTALL
AC_CHECK_TOOL(WINDRES, windres)

# Checks for libraries.
PKG_CHECK_MODULES(XCWM, xcwm xcb xcb-xfixes xcb-icccm xcb-image)

AC_CONFIG_FILES([Makefile
                 man/Makefile
                 src/Makefile])
AC_OUTPUT
