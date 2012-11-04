AC_PREREQ(2.61)
AC_INIT([XtoW], [0.1])

AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_CONFIG_HEADER([config.h])
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
PKG_CHECK_MODULES(XCWM, xcwm)

AC_CONFIG_FILES([Makefile
                 man/Makefile
                 src/Makefile])
AC_OUTPUT
