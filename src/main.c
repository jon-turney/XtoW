//
// Copyright Jon © TURNEY 2012
//
// This file is part of XtoW.
//
// XtoW is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// XtoW is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with XtoW.  If not, see <http://www.gnu.org/licenses/>.
//

#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <xcwm/xcwm.h>
#include <semaphore.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "config.h"
#include "debug.h"
#include "global.h"
#include "wndproc.h"
#include "wincursor.h"

#define WM_XCWM_CREATE  WM_USER
#define WM_XCWM_DESTROY (WM_USER+1)
#define WM_XCWM_CURSOR (WM_USER+2)
#define WM_XCWM_EXIT (WM_USER+3)

#define XCWM_EVENT_WINDOW_ICON 100

xcwm_context_t *context;
xcb_atom_t motif_wm_hints = 0;
xcb_atom_t windowState = 0;
DWORD msgPumpThread;
int serverGeneration = 1;

static sem_t semaphore;

static void
eventHandler(const xcwm_event_t *event)
{
    xcwm_window_t *window = xcwm_event_get_window(event);
    xcwm_event_type_t type = xcwm_event_get_type(event);

    //    DEBUG("event %d, window 0x%08x, XID 0x%08x\n", type, window, window->window_id);

    switch (type)
      {
      case XCWM_EVENT_WINDOW_CREATE:
        /*
          Windows windows are owned by the thread they are created by,
          and only the thread which owns the window can receive messages for it.
          So, we must arrange for the thread which we want to run the Windows
          message pump in to create the window...
        */
        PostThreadMessage(msgPumpThread, WM_XCWM_CREATE, 0, (LPARAM)window);
        sem_wait(&semaphore);
        break;

      case XCWM_EVENT_WINDOW_DESTROY:
        /*
          Only the owner thread is allowed to destroy a window
         */
        PostThreadMessage(msgPumpThread, WM_XCWM_DESTROY, 0, (LPARAM)window);
        sem_wait(&semaphore);
        break;

      case XCWM_EVENT_WINDOW_NAME:
        UpdateName(window);
        break;

      case XCWM_EVENT_WINDOW_DAMAGE:
        UpdateImage(window);
        break;

      case XCWM_EVENT_WINDOW_EXPOSE: // I don't think this event could ever be needed and is a mistake
        break;

      case XCWM_EVENT_WINDOW_APPEARANCE:
        UpdateStyle(window);
        break;

      case XCWM_EVENT_WINDOW_SHAPE:
        UpdateShape(window);
        break;

      case XCWM_EVENT_WINDOW_ICON:
        UpdateIcon(window);
        break;

      case XCWM_EVENT_WINDOW_CONFIGURE:
        winAdjustWindowsWindow(window);
        break;

      case XCWM_EVENT_WINDOW_STATE:
        UpdateState(window);

      case XCWM_EVENT_CURSOR:
        /*
          Only the 'GUI thread' is allowed to SetCursor()
        */
        PostThreadMessage(msgPumpThread, WM_XCWM_CURSOR, 0, 0);
        break;

      case XCWM_EVENT_EXIT:
        PostThreadMessage(msgPumpThread, WM_XCWM_EXIT, 0, 0);
        break;
      }
}

static void
help(void)
{
  fprintf(stderr, "usage: xtow [options]\n");
  fprintf(stderr, "--blur        use glass effect to blur the image beneath transparent areas\n");
  fprintf(stderr, "--display dpy display to manage windows on\n");
  fprintf(stderr, "--help\n");
  fprintf(stderr, "--nodwm       do not use DWM, even if available\n");
  fprintf(stderr, "--noshm       do not use SHM, even if available\n");
  exit(0);
}

static void
version(void)
{
  fprintf(stderr, PACKAGE " " PACKAGE_VERSION "\n");
  exit(0);
}

int main(int argc, char **argv)
{
  char *screen = NULL;
  int nodwm = 0;
  int noshm = 0;

  while (1)
    {
      static struct option long_options[] =
        {
          { "version", no_argument, 0, 'v' },
          { "display", required_argument, 0, 'd' },
          { "help",    no_argument, 0, 'h' },
          { "blur",    no_argument, 0, 'b' },
          { "nodwm",   no_argument, 0, 'n' },
          { "noshm",   no_argument, 0, 's' },
          {0, 0, 0, 0 },
        };

      int option_index = 0;
      int c = getopt_long_only(argc, argv, "d:hv", long_options, &option_index);

      if (c == -1)
        break;

      switch (c)
        {
        case 'd':
          screen = optarg;
          break;
        case 'b':
          blur = 1;
          break;
        case 'n':
          nodwm = 1;
          break;
        case 's':
          noshm = 1;
          break;
        case 'v':
          version();
          break;
        case 'h':
        default:
          help();
        }
    }

  if (optind < argc)
      help();

  DEBUG("screen is '%s'\n", screen ? screen : getenv("DISPLAY"));

  // ensure this thread has a message queue
  MSG msg;
  PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);
  msgPumpThread = GetCurrentThreadId();

  // initialize semaphore used for synchronization
  sem_init(&semaphore, 0, 0);

  // Get access to DwmEnableBlurBehindWindow, if available, so we can take advantage of it
  // on Vista and later, but still run on earlier versions of Windows
  HMODULE hDwmApiLib = LoadLibraryEx("dwmapi.dll", NULL, 0);
  if (hDwmApiLib)
    pDwmEnableBlurBehindWindow = (PFNDWMENABLEBLURBEHINDWINDOW)GetProcAddress(hDwmApiLib, "DwmEnableBlurBehindWindow");
  DEBUG("DwmEnableBlurBehindWindow %s\n", pDwmEnableBlurBehindWindow ? "found" : "not found");
  if (nodwm)
    {
      DEBUG("DWM disabled by --nodwm option\n");
      pDwmEnableBlurBehindWindow = NULL;
    }

  xcwm_context_flags_t flags = 0;
  if (noshm)
    {
      fprintf(stderr, "Use of MIT-SHM disabled by --noshm option\n");
      flags = XCWM_DISABLE_SHM;
    }

  // create the global xcwm context
  context = xcwm_context_open(screen, flags);
  if (!context)
    {
      fprintf(stderr, "Could not create xcwm context\n");
      exit(0);
    }

  //
  xcb_connection_t *connection = xcwm_context_get_connection(context);

  // Check that the X screen size is at least as big as the virtual desktop size
  ///
  // (We don't really care about this much, as our composited windows never actually
  // get drawn onto the X screen)
  //
  // But, unfortunately it seems that XTEST mouse click events outside the X screen
  // size are always delivered to the root window, so the X screen size needs to be
  // at least as big as the virtual desktop to ensure that mouse click events are
  // delivered correctly.
  //
  // Sizes should match so that X applications that look at the X screen size get
  // useful information.
  //
  // XXX: ideally we would RANDR resize the X screen to the needed size...
  {
    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    xcb_screen_t *screen = iter.data;

    unsigned int x_width = screen->width_in_pixels;
    unsigned int x_height = screen->height_in_pixels;

    unsigned int win_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    unsigned int win_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    printf("X screen has size %d x %d\n", x_width, x_height);
    printf("Windows virtual desktop has size %d x %d\n", win_width, win_height);

    if ((x_width < win_width) || (x_height < win_height))
      {
        fprintf(stderr, "X screen size should match or exceed Windows virtual desktop size\n");
        exit(0);
    }
  }

  // register interest in some atoms
  motif_wm_hints = xcwm_atom_register(context, "_MOTIF_WM_HINTS", XCWM_EVENT_WINDOW_APPEARANCE);
  windowState = xcwm_atom_register(context, "_NET_WM_STATE", XCWM_EVENT_WINDOW_APPEARANCE);
  xcwm_atom_register(context, " _NET_WM_ICON", XCWM_EVENT_WINDOW_ICON);
  xcwm_atom_register(context, " WM_HINTS", XCWM_EVENT_WINDOW_ICON);

  // spawn the event loop thread, and set the callback function
  xcwm_event_start_loop(context, eventHandler);

  // set the root window cursor to left_ptr (XXX: should probably be in libxcwm)
  // (this controls the cursor an application gets over it's windows when it doesn't set one)
#define XC_left_ptr 68

  xcb_cursor_t cursor = xcb_generate_id(connection);
  xcb_window_t window = xcwm_window_get_window_id(xcwm_context_get_root_window(context));
  xcb_font_t font = xcb_generate_id(connection);
  xcb_font_t *mask_font = &font; /* An alias to clarify */
  int shape = XC_left_ptr;

  xcb_open_font(connection, font, sizeof("cursor"), "cursor");

  static const uint16_t fgred = 0, fggreen = 0, fgblue = 0;
  static const uint16_t bgred = 0xFFFF, bggreen = 0xFFFF, bgblue = 0xFFFF;

  xcb_create_glyph_cursor(connection, cursor, font, *mask_font, shape, shape + 1,
                          fgred, fggreen, fgblue, bgred, bggreen, bgblue);

  uint32_t mask = XCB_CW_CURSOR;
  uint32_t value_list = cursor;
  xcb_change_window_attributes(connection, window, mask, &value_list);

  xcb_free_cursor(connection, cursor);
  xcb_close_font(connection, font);

  // .. and convert to native cursor
  InitCursor();

  // pump windows message queue
  // (Use select on /dev/windows rather than GetMessage() so that cygwin signals
  // like a SIGINT sent from another process can reach us...)
  int fdMessageQueue = open("/dev/windows", O_RDONLY);
  while (1) {
    fd_set fdsRead;
    FD_ZERO(&fdsRead);
    FD_SET(fdMessageQueue, &fdsRead);

    /* Wait for Windows event */
    if (select(fdMessageQueue + 1, &fdsRead, NULL, NULL, NULL) < 0)
      {
        if (errno == EINTR)
          continue;

        break;
      }

    if (FD_ISSET(fdMessageQueue, &fdsRead)) {
      if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
          if (msg.message == WM_XCWM_CREATE)
            {
              winCreateWindowsWindow((xcwm_window_t *)msg.lParam);
              sem_post(&semaphore);
            }
          else if (msg.message == WM_XCWM_DESTROY)
            {
              winDestroyWindowsWindow((xcwm_window_t *)msg.lParam);
              sem_post(&semaphore);
            }
          else if (msg.message == WM_XCWM_CURSOR)
            UpdateCursor();
        else if (msg.message == WM_XCWM_EXIT)
          PostQuitMessage(0);
        else if (msg.message == WM_QUIT)
          break;
        else
          DispatchMessage(&msg);
        }
    }
  }

  close(fdMessageQueue);

  // Shutdown:
  // if server died, we get a error from xcb, which causes XCWM_EVENT_EXIT to be sent
  // if we got WM_QUIT, message loop exits
  // if window station is shutting down, we get WM_ENDSESSION
  // if we got a signal...

  xcwm_context_close(context);

  FreeLibrary(hDwmApiLib);

  sem_destroy(&semaphore);
}
