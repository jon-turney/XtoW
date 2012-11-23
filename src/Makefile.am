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
	scancodes.h \
	winicons.c \
	winicons.h \
	wincursor.c \
	wincursor.h \
	resource.rc

.rc.o:
	$(AM_V_GEN)$(WINDRES) --use-temp-file -i $< --input-format=rc -o $@ -O coff

xtow_LDADD = -lXWinWMUtil $(XCWM_LIBS) -lgdi32
