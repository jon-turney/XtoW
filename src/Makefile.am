AM_CFLAGS = $(XCWM_CFLAGS)

bin_PROGRAMS = xtow

xtow_SOURCES = \
	main.c \
	wndproc.c \
	wndproc.h \
	winmessages.h \
	debug.c \
	debug.h \
	winkeybd.c \
	winkeybd.h \
	winvkmap.h \
	scancodes.h

xtow_LDADD = $(XCWM_LIBS) -lgdi32 -ldwmapi
