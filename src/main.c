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

#include "debug.h"
#include "global.h"

#define WM_XCWM_CREATE  WM_USER
#define WM_XCWM_DESTROY (WM_USER+1)

xcwm_context_t *context;
DWORD msgPumpThread;

void
eventHandler(const xcwm_event_t *event)
{
    xcwm_window_t *window = xcwm_event_get_window(event);
    xcwm_event_type_t type = xcwm_event_get_type(event);

    DEBUG("event %d, window 0x%08x, XID 0x%08x\n", type, window, window->window_id);

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
        break;

      case XCWM_EVENT_WINDOW_DESTROY:
        /*
          Only the owner thread is allowed to destroy a window
         */
        PostThreadMessage(msgPumpThread, WM_XCWM_DESTROY, 0, (LPARAM)window);
        break;

      case XCWM_EVENT_WINDOW_NAME:
        UpdateName(window);
        break;

      case XCWM_EVENT_WINDOW_DAMAGE:
        UpdateImage(window);
        break;

      case XCWM_EVENT_WINDOW_EXPOSE: // I don't think this event could ever be needed and is a mistake
        break;
      }

    free(event);
}

static void
help(void)
{
  fprintf(stderr, "usage: xtow [options]\n");
  fprintf(stderr, "-display dpy display to manage windows on\n");
  fprintf(stderr, "-help\n");
  exit(0);
}

int main(int argc, char **argv)
{
  char *screen = NULL;

  while (1)
    {
      static struct option long_options[] =
        {
          { "display", required_argument, 0, 'd' },
          { "help",    no_argument, 0, 'h' },
          {0, 0, 0, 0 },
        };

      int option_index = 0;
      int c = getopt_long_only(argc, argv, "d:h", long_options, &option_index);

      if (c == -1)
        break;

      switch (c)
        {
        case 'd':
          screen = optarg;
          break;
        case 'h':
        default:
          help();
        }
    }

  if (optind < argc)
      help();

  DEBUG("screen is '%s'\n", screen);

  // ensure this thread has a message queue
  MSG msg;
  PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE);
  msgPumpThread = GetCurrentThreadId();

  // create the global xcwm context
  context = xcwm_context_open(screen);

  // spawn the event loop thread, and set the callback function
  xcwm_event_start_loop(context, eventHandler);

  // pump windows message queue
  while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
      if (msg.message == WM_XCWM_CREATE)
        winCreateWindowsWindow(msg.lParam);
      else if (msg.message == WM_XCWM_DESTROY)
        winDestroyWindowsWindow(msg.lParam);
      else
        DispatchMessage(&msg);
    }

  // Shutdown:
  // if we got WM_QUIT, message loop exits
  // if server died, we get a error from xcb
  // if window station is shutting down, we get WM_ENDSESSION
  // if we got a signal...

  xcwm_context_close(context);
}
