//
// Copyright © Jon TURNEY 2012
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

//
// Lots of this stuff is ripped off from the existing integrated WM
// in XWin
//

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include <xcwm/xcwm.h>

#include "debug.h"
#include "winmessages.h"
#include "winkeybd.h"
#include "winicons.h"
#include "wndproc.h"

#define WINDOW_CLASS_X "xtow"
#define WINDOW_TITLE_X "X"
#define WIN_WID_PROP "cyg_wid_prop"
#define WIN_XCWM_PROP "cyg_xcwm_prop"
#define WIN_HDWP_PROP "cyg_hdwp_prop"

int blur = 0;
PFNDWMENABLEBLURBEHINDWINDOW pDwmEnableBlurBehindWindow = NULL;
extern xcwm_context_t *context;

/*
 * ValidateSizing - Ensures size request respects hints
 */
static int
ValidateSizing (HWND hwnd, xcwm_window_t *window,
		WPARAM wParam, LPARAM lParam)
{
  RECT *rect = (RECT *)lParam;
  int iWidth, iHeight;
  RECT rcClient, rcWindow;
  int iBorderWidthX, iBorderWidthY;

  /* Invalid input checking */
  if (lParam==0)
    return FALSE;

  iWidth = rect->right - rect->left;
  iHeight = rect->bottom - rect->top;

  /* Now remove size of any borders and title bar */
  GetClientRect(hwnd, &rcClient);
  GetWindowRect(hwnd, &rcWindow);
  iBorderWidthX = (rcWindow.right - rcWindow.left) - (rcClient.right - rcClient.left);
  iBorderWidthY = (rcWindow.bottom - rcWindow.top) - (rcClient.bottom - rcClient.top);
  iWidth -= iBorderWidthX;
  iHeight -= iBorderWidthY;

  /* Constrain the size to legal values */
  xcwm_window_constrain_sizing(window, &iWidth, &iHeight);

  /* Add back the size of borders and title bar */
  iWidth += iBorderWidthX;
  iHeight += iBorderWidthY;

  /* Adjust size according to where we're dragging from */
  switch (wParam) {
  case WMSZ_TOP:
  case WMSZ_TOPRIGHT:
  case WMSZ_BOTTOM:
  case WMSZ_BOTTOMRIGHT:
  case WMSZ_RIGHT:
    rect->right = rect->left + iWidth;
    break;
  default:
    rect->left = rect->right - iWidth;
    break;
  }

  switch(wParam) {
  case WMSZ_BOTTOM:
  case WMSZ_BOTTOMRIGHT:
  case WMSZ_BOTTOMLEFT:
  case WMSZ_RIGHT:
  case WMSZ_LEFT:
    rect->bottom = rect->top + iHeight;
    break;
  default:
    rect->top = rect->bottom - iHeight;
    break;
  }
  return TRUE;
}

/*
  Convert coordinates pt from client area of hWnd to X server
 */
static void
ClientToXCoord(HWND hWnd, POINT *pt)
{
  /* Translate the client area mouse coordinates to screen (virtual desktop) coordinates */
  ClientToScreen(hWnd, pt);

  /* Translate from screen coordinates to X coordinates */
  pt->x -= GetSystemMetrics(SM_XVIRTUALSCREEN);
  pt->y -= GetSystemMetrics(SM_YVIRTUALSCREEN);
}

/*
 * winAdjustXWindow
 *
 * Move and resize X window with respect to corresponding Windows window.
 *
 * This is when the user performs any windowing operation which might change
 * it's size or position (create, move, resize, minimize, maximize, restore).
 *
 * The functionality is the inverse of winPositionWindowMultiWindow, which
 * adjusts Windows window with respect to X window.
 */
static int
winAdjustXWindow (xcwm_window_t *window, HWND hWnd)
{
  RECT rcWin;
  LONG w, h;

#define WIDTH(rc) (rc.right - rc.left)
#define HEIGHT(rc) (rc.bottom - rc.top)

  if (IsIconic(hWnd))
    {
      DEBUG("Immediately return because the window is iconized\n");

      /* XXX: match WM_STATE.state to state of Windows window */
      /* XXX: send a WM_CHANGE_STATE message ? */

      return 0;
    }

  /* Get size of client area */
  GetClientRect(hWnd, &rcWin);
  w = WIDTH(rcWin);
  h = HEIGHT(rcWin);

  /* Transform virtual desktop coordinates of top-left of client area,
     to X screen coordinates */
  POINT p = {0,0};
  ClientToXCoord(hWnd, &p);

  DEBUG("ConfigureWindow to %ldx%ld @ (%ld, %ld)\n", w, h, p.x, p.y);

  xcwm_window_configure(window, p.x, p.y, w, h);

 return 1;

#undef WIDTH
#undef HEIGHT
}

/*
 * Updates the name of a HWND according to its X WM_NAME property
 */
void
UpdateName(xcwm_window_t *window)
{
  HWND hWnd = xcwm_window_get_local_data(window);
  if (!hWnd)
    return;

  /* If window isn't override-redirect */
  if (xcwm_window_is_override_redirect(window))
    return;

  char *name = xcwm_window_copy_name(window);
  if (name)
    {
      DEBUG("UpdateName: '%s'\n", name);

      /* Convert from UTF-8 to wide char */
      int iLen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
      wchar_t *pwszWideWindowName = (wchar_t *) malloc(sizeof(wchar_t) * (iLen + 1));
      MultiByteToWideChar(CP_UTF8, 0, name, -1, pwszWideWindowName, iLen);

      /* Set the Windows window name */
      SetWindowTextW(hWnd, pwszWideWindowName);

      free(pwszWideWindowName);
      free(name);
    }
}

static void
CheckForAlpha(HWND hWnd, xcwm_image_t *image)
{
  /* image has alpha and we can do something useful with it? */
  if ((image->image->depth == 32) && pDwmEnableBlurBehindWindow)
    {
      /* XXX: only do this once, for each window */
      HRGN dummyRegion = NULL;

      /* restricting the blur effect to a dummy region means the rest is unblurred */
      if (!blur)
        dummyRegion = CreateRectRgn(-1, -1, 0, 0);

      DWM_BLURBEHIND bbh;
      bbh.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION |DWM_BB_TRANSITIONONMAXIMIZED;
      bbh.fEnable = TRUE;
      bbh.hRgnBlur = dummyRegion;
      bbh.fTransitionOnMaximized = TRUE; /* What does this do ??? */

      // need to re-issue this on WM_DWMCOMPOSITIONCHANGED
      DEBUG("enabling alpha, blur %s\n", blur ? "on" : "off");
      // This terribly-named function actually controls if DWM looks at the alpha channel of this window
      HRESULT rc = pDwmEnableBlurBehindWindow(hWnd, &bbh);
      if (rc != S_OK)
        fprintf(stderr, "DwmEnableBlurBehindWindow failed: %d\n", (int) GetLastError());

      if (dummyRegion)
        DeleteObject(dummyRegion);
    }
}

static void
BitBltFromImage(xcwm_image_t *image, HDC hdcUpdate,
                int nXDest, int nYDest, int nWidth, int nHeight,
                int nXSrc, int nYSrc)
{
  // DEBUG("drawing %dx%d @ %d,%d from %d,%d in image\n", nWidth, nHeight, nXDest,  nYDest,  nXSrc,  nYSrc);
  //  DEBUG("image has bpp %d, depth %d\n", image->image->bpp, image->image->depth);

  assert(image->image->scanline_pad = 32); // DIBs are always 32 bit aligned
  assert(((int)image->image->data % 4) == 0); // ?

  // describe the bitmap format we are given
  BITMAPINFO diBmi;
  memset(&diBmi, 0, sizeof(diBmi));
  diBmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  diBmi.bmiHeader.biWidth = image->width;
  diBmi.bmiHeader.biHeight = -image->height; // top-down bitmap
  diBmi.bmiHeader.biPlanes = 1;
  diBmi.bmiHeader.biBitCount = image->image->bpp;
  diBmi.bmiHeader.biCompression = BI_RGB;
  diBmi.bmiHeader.biSizeImage = 0;
  diBmi.bmiHeader.biClrUsed = 0;
  diBmi.bmiHeader.biClrImportant = 0;
  // diBmi.bmiColors unused unless biBitCount is 8 or less, or biCompression is BI_BITFIELDS, or biClrUsed is non-zero...

  // create compatible DC, create a device compatible bitmap from the image, and select it into the DC
  HDC hdcMem = CreateCompatibleDC(hdcUpdate);
  HBITMAP hBitmap = CreateDIBitmap(hdcUpdate, &(diBmi.bmiHeader), CBM_INIT, image->image->data, &diBmi, DIB_RGB_COLORS);
  HBITMAP hStockBitmap = SelectObject(hdcMem, hBitmap);

  // transfer the bits from our compatible DC to the display
  if (!BitBlt(hdcUpdate, nXDest, nYDest, nWidth, nHeight, hdcMem, nXSrc, nYSrc, SRCCOPY))
    {
      fprintf(stderr, "BitBltFromImage: BitBlt failed: 0x%08lx\n", GetLastError());
    }

  SelectObject(hdcMem, hStockBitmap);
  DeleteObject(hBitmap);
  DeleteDC(hdcMem);
}

void
UpdateImage(xcwm_window_t *window)
{
  /* stop xcwm processing events so it can't change
     this damage while we are using it */
  xcwm_event_get_thread_lock();

  /*
    We may not have a hWnd yet, if the window is still being created when this
     damage arrives.  Just discard the damage as we will draw the whole thing after
     being created...
  */
  HWND hWnd = xcwm_window_get_local_data(window);
  if (hWnd)
    {
      xcwm_image_t *image;
      image = xcwm_image_copy_damaged(window);
      if (image)
        {
          xcwm_rect_t *dmgRect = xcwm_window_get_damaged_rect(window);
          // DEBUG("damaged rect is %ldx%ld @ (%ld, %ld)\n", dmgRect->width, dmgRect->height, dmgRect->x, dmgRect->y);

          CheckForAlpha(hWnd, image);

          /* Update the region asked for */
          HDC hdcUpdate = GetDC(hWnd);
          BitBltFromImage(image, hdcUpdate,
                          dmgRect->x, dmgRect->y,
                          dmgRect->width, dmgRect->height,
                          0, 0);
          ReleaseDC(hWnd,hdcUpdate);

          // useful?
          RECT damage;
          damage.left = dmgRect->x;
          damage.top = dmgRect->y;
          damage.right = dmgRect->x + dmgRect->width;
          damage.bottom = dmgRect->y + dmgRect->height;
          ValidateRect(hWnd, &damage);

          xcwm_image_destroy(image);
        }
      else
        {
          DEBUG("image_copy failed\n");
        }
    }
  else
    {
      DEBUG("UpdateImage: discarding damage, no hWnd\n");
    }

  // Remove the damage
  xcwm_window_remove_damage(window);

  xcwm_event_release_thread_lock();
}

static xcb_atom_t
atom_get(xcwm_context_t *context, const char *atomName)
{
  xcb_intern_atom_reply_t *atom_reply;
  xcb_intern_atom_cookie_t atom_cookie;
  xcb_atom_t atom = XCB_NONE;

  atom_cookie = xcb_intern_atom(context->conn,
                                0,
                                strlen(atomName),
                                atomName);
  atom_reply = xcb_intern_atom_reply(context->conn,
                                     atom_cookie,
                                     NULL);
  if (atom_reply) {
    atom = atom_reply->atom;
    free(atom_reply);
  }
  return atom;
}

/* Windows window styles */
#define HINT_FRAME	 (1<<0) /* any decoration */
#define HINT_SIZEFRAME	 (1<<2) /* a resizing frame */
#define HINT_CAPTION	 (1<<3) /* a title bar */
#define HINT_MAXIMIZE    (1<<4)
#define HINT_MINIMIZE    (1<<5)
#define HINT_SYSMENU     (1<<6)
#define HINT_SKIPTASKBAR (1<<7)

#define		MwmHintsDecorations	(1L << 1)

#define		MwmDecorAll		(1L << 0)
#define		MwmDecorBorder		(1L << 1)
#define		MwmDecorHandle		(1L << 2)
#define		MwmDecorTitle		(1L << 3)
#define		MwmDecorMenu		(1L << 4)
#define		MwmDecorMinimize	(1L << 5)
#define		MwmDecorMaximize	(1L << 6)

/* This structure only contains 3 elements... the Motif 2.0 structure
contains 5... we only need the first 3... so that is all we will define */
typedef struct MwmHints {
  unsigned long flags, functions, decorations;
} MwmHints;

#define		PropMwmHintsElements	3

static void
winApplyStyle(xcwm_window_t *window)
{
  HWND hWnd = xcwm_window_get_local_data(window);
  xcwm_window_type_t window_type = xcwm_window_get_window_type(window);
  HWND zstyle = HWND_NOTOPMOST;
  unsigned int hint = 0;

  if (!hWnd)
    return;

  DEBUG("winApplyStyle: id 0x%08x window_type %d\n", window->window_id, window_type);

  switch (window_type)
    {
    case XCWM_WINDOW_TYPE_UNKNOWN:
    case XCWM_WINDOW_TYPE_NORMAL:
      hint = HINT_FRAME | HINT_MAXIMIZE | HINT_MINIMIZE | HINT_SYSMENU | HINT_SIZEFRAME | HINT_CAPTION;
      break;

    case XCWM_WINDOW_TYPE_DIALOG:
    case XCWM_WINDOW_TYPE_NOTIFICATION:
      hint = HINT_FRAME | HINT_MAXIMIZE | HINT_MINIMIZE | HINT_SYSMENU | HINT_SIZEFRAME | HINT_CAPTION;
      break;

    case XCWM_WINDOW_TYPE_TOOLTIP:
      break;

    case XCWM_WINDOW_TYPE_SPLASH:
    case XCWM_WINDOW_TYPE_DESKTOP:
      zstyle = HWND_TOPMOST;
      break;

    case XCWM_WINDOW_TYPE_DND:
    case XCWM_WINDOW_TYPE_DOCK:
      /* questionable correctness */
      hint = HINT_FRAME | HINT_SIZEFRAME;
      zstyle = HWND_TOPMOST;
      break;

    case XCWM_WINDOW_TYPE_POPUP_MENU:
    case XCWM_WINDOW_TYPE_DROPDOWN_MENU:
    case XCWM_WINDOW_TYPE_COMBO:
      hint |= HINT_SKIPTASKBAR;
      break;

    case XCWM_WINDOW_TYPE_UTILITY:
    case XCWM_WINDOW_TYPE_TOOLBAR:
    case XCWM_WINDOW_TYPE_MENU:
      hint |= HINT_SKIPTASKBAR;
      break;
    }

  /* Allow explicit style specification in _MOTIF_WM_HINTS to override the semantic style specified by _NET_WM_WINDOW_TYPE */
  /* XXX: we also need to get told if _MOTIF_WM_HINTS property changes */
  static xcb_atom_t motif_wm_hints = 0;

  if (!motif_wm_hints)
    motif_wm_hints = atom_get(window->context, "_MOTIF_WM_HINTS");

  xcb_get_property_cookie_t cookie_mwm_hint = xcb_get_property(window->context->conn, FALSE, window->window_id, motif_wm_hints, motif_wm_hints, 0L, sizeof(MwmHints));
  xcb_get_property_reply_t *reply =  xcb_get_property_reply(window->context->conn, cookie_mwm_hint, NULL);
  if (reply)
    {
      int nitems = xcb_get_property_value_length(reply);
      MwmHints *mwm_hint = xcb_get_property_value(reply);

      if (mwm_hint && (nitems >= (int)sizeof(MwmHints)) && (mwm_hint->flags & MwmHintsDecorations))
        {
          if (!mwm_hint->decorations)
            hint &= ~HINT_FRAME;
          else
            {
              if (mwm_hint->decorations & MwmDecorAll)
                {
                  /*
                    MwmDecorAll means all decorations *except* those specified by other flag
                    bits that are set.
                  */
                  mwm_hint->decorations = ~(mwm_hint->decorations);
                }

              if (!(mwm_hint->decorations & MwmDecorBorder))
                hint &= ~HINT_FRAME;
              if (!(mwm_hint->decorations & MwmDecorHandle))
                hint &= ~HINT_SIZEFRAME;
              if (!(mwm_hint->decorations & MwmDecorTitle))
                hint &= ~HINT_CAPTION;
              if (!(mwm_hint->decorations & MwmDecorMenu))
                hint &= ~HINT_SYSMENU;
              if (!(mwm_hint->decorations & MwmDecorMinimize))
                hint &= ~HINT_MINIMIZE;
              if (!(mwm_hint->decorations & MwmDecorMaximize))
                hint &= ~HINT_MAXIMIZE;
            }
        }

      free(reply);
    }

  /* _NET_WM_WINDOW_STATE */
  /* XXX: we also need to get told the _NET_WM_WINDOW_STATE property changes */
  static xcb_atom_t windowState, belowState, aboveState, skiptaskbarState;
  if (!windowState)
    windowState = atom_get(window->context, "_NET_WM_STATE");
  if (!belowState)
    belowState = atom_get(window->context, "_NET_WM_STATE_BELOW");
  if (!aboveState)
    aboveState = atom_get(window->context, "_NET_WM_STATE_ABOVE");
  if (!skiptaskbarState)
    skiptaskbarState = atom_get(window->context, "_NET_WM_STATE_SKIP_TASKBAR");

  xcb_get_property_cookie_t cookie_wm_state = xcb_get_property(window->context->conn, FALSE, window->window_id, windowState, XCB_ATOM, 0L, INT_MAX);
  reply = xcb_get_property_reply(window->context->conn, cookie_wm_state, NULL);
  if (reply)
    {
      int i;
      int nitems = xcb_get_property_value_length(reply);
      xcb_atom_t *pAtom = xcb_get_property_value(reply);

      for (i = 0; i < nitems; i++)
        {
          if (pAtom[i] == skiptaskbarState)
            hint |= HINT_SKIPTASKBAR;
          if (pAtom[i] == belowState)
            zstyle = HWND_BOTTOM;
          else if (pAtom[i] == aboveState)
            zstyle = HWND_TOPMOST;
        }

      free(reply);
    }

  /* We also need to check sizing hint ... */
  if (window->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE)
    {
      /* Not maximizable if a maximum size is specified */
      hint &= ~HINT_MAXIMIZE;

      if (window->size_hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)
        {
          /*
            (per EWMH Implementation Notes, section "Fixed Size windows")
            If both minimum size and maximum size are specified and are the same,
            don't bother with a resizing frame
          */
          if ((window->size_hints.min_width == window->size_hints.max_width)
              && (window->size_hints.min_height == window->size_hints.max_height))
            hint = (hint & ~HINT_SIZEFRAME);
        }
    }

  /* Now apply styles to window */
  DWORD style, exStyle;
  style = GetWindowLongPtr(hWnd, GWL_STYLE);
  exStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
  /* GetWindowLongPointer returns 0 on failure, we hope this isn't a valid style */

  style = style & ~WS_CAPTION & ~WS_SIZEBOX;

  if (hint & HINT_FRAME)
    style = style | ((hint & HINT_FRAME) ? WS_BORDER : 0) |
      ((hint & HINT_SIZEFRAME) ? WS_SIZEBOX : 0) |
      ((hint & HINT_CAPTION) ? WS_CAPTION : 0);

  if (hint & HINT_MAXIMIZE)
    style = style | WS_MAXIMIZEBOX;

  if (hint & HINT_MINIMIZE)
    style = style | WS_MINIMIZEBOX;

  if (hint & HINT_SYSMENU)
    style = style | WS_SYSMENU;

  if (hint & HINT_SKIPTASKBAR)
    style = style & ~WS_MINIMIZEBOX; /* otherwise, window will become lost if minimized */

  if (hint & HINT_SKIPTASKBAR)
    exStyle = (exStyle & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW;
  else
    exStyle = (exStyle & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW;

  SetWindowLongPtr(hWnd, GWL_STYLE, style);
  SetWindowLongPtr(hWnd, GWL_EXSTYLE, exStyle);

  DEBUG("winApplyStyle: id 0x%08x hints 0x%08x style 0x%08x exstyle 0x%08x\n", window->window_id, hint, style, exStyle);

  /* Apply the updated window style, without changing it's show or activation state */
  UINT flags = SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE;
  if (zstyle == HWND_NOTOPMOST)
    flags |= SWP_NOZORDER | SWP_NOOWNERZORDER;
  SetWindowPos(hWnd, NULL, 0, 0, 0, 0, flags);
}

/*
 * Updates the style of a HWND according to its X style properties
 */
void
UpdateStyle(xcwm_window_t *window)
{
  HWND hWnd = xcwm_window_get_local_data(window);
#if 0
  bool onTaskbar;
#endif

  /* Determine the Window style */
  winApplyStyle(window);

  /* Update window opacity */
  BYTE bAlpha = xcwm_window_get_opacity(window) >> 24;
  SetLayeredWindowAttributes(hWnd, RGB(0,0,0), bAlpha, LWA_ALPHA);

#if 0
  /*
    Use the WS_EX_TOOLWINDOW style to remove window from Alt-Tab window switcher

    According to MSDN, this is supposed to remove the window from the taskbar as well,
    if we SW_HIDE before changing the style followed by SW_SHOW afterwards.

    But that doesn't seem to work reliably, so also use iTaskbarList interface to
    tell the taskbar to show or hide this window.
  */
  onTaskbar = GetWindowLongPtr(hWnd, GWL_EXSTYLE) & WS_EX_APPWINDOW;
  wintaskbar(hWnd, onTaskbar);

  /* Check urgency hint */
  winApplyUrgency(pWMInfo->pDisplay, iWindow, hWnd);
#endif
}

/* Don't allow window decoration to disappear off to top-left */
static void
BumpWindowPosition(HWND hWnd)
{
  WINDOWINFO wi;
  int iDx, iDy;

  wi.cbSize = sizeof(WINDOWINFO);
  GetWindowInfo(hWnd, &wi);

  if (wi.rcWindow.left < GetSystemMetrics(SM_XVIRTUALSCREEN))
    {
      iDx = GetSystemMetrics(SM_XVIRTUALSCREEN) - wi.rcWindow.left;
      wi.rcWindow.left += iDx;
      wi.rcWindow.right += iDx;
    }

  if (wi.rcWindow.top < GetSystemMetrics(SM_YVIRTUALSCREEN))
    {
      iDy = GetSystemMetrics(SM_YVIRTUALSCREEN) - wi.rcWindow.top;
      wi.rcWindow.top += iDy;
      wi.rcWindow.bottom += iDy;
    }

  /* Position the Windows window */
  SetWindowPos(hWnd, 0, wi.rcWindow.left, wi.rcWindow.top, wi.rcWindow.right - wi.rcWindow.left, wi.rcWindow.bottom - wi.rcWindow.top,
               SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
}

#define WIN_POLLING_MOUSE_TIMER_ID	2
#define MOUSE_POLLING_INTERVAL		50

static UINT_PTR g_uipMousePollingTimerID = 0;

static VOID CALLBACK
winMousePollingTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
  static POINT last_point;
  POINT point;

  /* Get the current position of the mouse cursor */
  GetCursorPos(&point);

  /* Translate from screen coordinates to X coordinates */
  point.x -= GetSystemMetrics(SM_XVIRTUALSCREEN);
  point.y -= GetSystemMetrics(SM_YVIRTUALSCREEN);

  /* If the mouse pointer has moved, deliver absolute cursor position to X Server */
  if (last_point.x != point.x || last_point.y != point.y)
    {
      xcwm_input_mouse_motion(context, point.x, point.y);
      last_point.x = point.x;
      last_point.y = point.y;
    }
}

static void
winStartMousePolling(void)
{
  /*
   * Timer to poll mouse position.  This is needed to make
   * programs like xeyes follow the mouse properly when the
   * mouse pointer is outside of any X window.
   */
  if (g_uipMousePollingTimerID == 0)
    {
      g_uipMousePollingTimerID = SetTimer(NULL,
                                          WIN_POLLING_MOUSE_TIMER_ID,
                                          MOUSE_POLLING_INTERVAL,
                                          winMousePollingTimerProc);
      DEBUG("started mouse polling timer, id %d\n", g_uipMousePollingTimerID);
    }
}

static void
winStopMousePolling(void)
{
  /* Kill the timer used to poll mouse position */
  if (g_uipMousePollingTimerID != 0)
    {
      KillTimer(NULL, g_uipMousePollingTimerID);
      DEBUG("stopped mouse polling timer, id %d\n", g_uipMousePollingTimerID);
      g_uipMousePollingTimerID = 0;
    }
}

static bool g_fButton[3] = { FALSE, FALSE, FALSE };

static int
winMouseButtonsHandle(xcwm_window_t *window, bool press, int iButton, HWND hWnd)
{
  /* 3 button emulation code would go here, if we thought anyone actually needed it anymore... */

  g_fButton[iButton-1] = press;

  if (press)
    {
      SetCapture(hWnd);
    }
  else
    {
      /* XXX: this looks like it would do the wrong thing when multiple mouse buttons are pressed and
         only one released */
      ReleaseCapture();
      winStartMousePolling();
    }

  xcwm_input_mouse_button_event(window, iButton, press);

  return 0;
}

static void
winDebugWin32Message(const char* function, HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  static int force = 1;

  if (message >= WM_USER)
    {
      if (force || getenv("WIN_DEBUG_MESSAGES") || getenv("WIN_DEBUG_WM_USER"))
      {
        DEBUG("%s: hWnd 0x%08x wParam 0x%08x lParam 0x%08x message WM_USER + %d\n",
                 function, hWnd, wParam, lParam, message - WM_USER);
      }
    }
  else if (message < MESSAGE_NAMES_LEN && MESSAGE_NAMES[message])
    {
      const char *msgname = MESSAGE_NAMES[message];
      char buffer[64];
      snprintf(buffer, sizeof(buffer), "WIN_DEBUG_%s", msgname);
      buffer[63] = 0;
      if (force || getenv("WIN_DEBUG_MESSAGES") || getenv(buffer))
      {
        DEBUG("%s: hWnd 0x%08x wParam 0x%08x lParam 0x%08x message %s\n",
                 function, hWnd, wParam, lParam, MESSAGE_NAMES[message]);
      }
    }
}

/*
 * winTopLevelWindowProc - Window procedure for all top-level Windows windows.
 */

static LRESULT CALLBACK
winTopLevelWindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  static bool hasEnteredSizeMove = FALSE;
  /* XXX: a global is wrong if WM_MOUSEMOVE of the new window is delivered before WM_MOUSELEAVE of the old? Use HWND instead? */
  static bool s_fTracking = FALSE;

  // winDebugWin32Message("winTopLevelWindowProc", hWnd, message, wParam, lParam);

  if (message == WM_CREATE)
    {
      /* Store the xcwm window handle and X window XID in a Windows window properties */
      xcwm_window_t *window = ((LPCREATESTRUCT)lParam)->lpCreateParams;
      SetProp(hWnd, WIN_XCWM_PROP, window);
      SetProp(hWnd, WIN_WID_PROP, (HANDLE)window->window_id);
      return 0;
    }

  xcwm_window_t *window = (xcwm_window_t *)GetProp(hWnd, WIN_XCWM_PROP);

  switch (message)
    {
    case WM_ACTIVATE:
      /* If we are being activated, acquire the input focus */
      if (LOWORD(wParam) != WA_INACTIVE)
	{
          xcwm_window_set_input_focus(window);
        }
      return 0;

    case WM_ACTIVATEAPP:
      /* If focus is going to another Windows app, no X window has the X input focus */
      if (!wParam)
        {
          xcb_set_input_focus(window->context->conn, XCB_INPUT_FOCUS_NONE,
                              XCB_WINDOW_NONE, XCB_CURRENT_TIME);
        }
      return 0;

    case WM_SETFOCUS:
        {
          /* Get the parent window for transient handling */
          HWND hParent = GetParent(hWnd);

          if (hParent && IsIconic(hParent))
            ShowWindow(hParent, SW_RESTORE);
        }

        /* Synchronize all latching key states */
        // winRestoreModeKeyStates();

        return 0;

    case WM_KILLFOCUS:
        /* Pop any pressed keys since we are losing keyboard focus */
        winKeybdReleaseKeys();

        /* Drop the X focus as well, but only if the Windows focus is going to another window */
        if (!wParam)
          xcb_set_input_focus(window->context->conn, XCB_INPUT_FOCUS_NONE,
                              XCB_WINDOW_NONE, XCB_CURRENT_TIME);

        return 0;

    case WM_MOUSEACTIVATE:
      /* ??? override-redirect windows should not mouse-activate */
      break;

    case WM_CLOSE:
      xcwm_window_request_close(window);
      // XXX: RACE!!! window will become invalid when we get XCB_DESTROY_NOTIFY back from server
      // but this wndproc may continue to touch it until we get WM_DESTROY..
      return 0;

    case WM_DESTROY:
      RemoveProp(hWnd, WIN_WID_PROP);
      RemoveProp(hWnd, WIN_XCWM_PROP);
      RemoveProp(hWnd, WIN_HDWP_PROP);
      break;

    case WM_MOVE:
      /* Adjust the X Window to the moved Windows window */
      if (!hasEnteredSizeMove)
        winAdjustXWindow(window, hWnd);
      /* else: Wait for WM_EXITSIZEMOVE */
      return 0;

    case WM_SIZE:
      if (!hasEnteredSizeMove)
        {
          /* Adjust the X Window to the moved Windows window */
          winAdjustXWindow(window, hWnd);
        }
        /* else: wait for WM_EXITSIZEMOVE */
      return 0;

    case WM_SIZING:
      /* Need to legalize the size according to WM_NORMAL_HINTS */
      return ValidateSizing(hWnd, window, wParam, lParam);

    case WM_ENTERSIZEMOVE:
      hasEnteredSizeMove = TRUE;
      return 0;

    case WM_EXITSIZEMOVE:
      /* Adjust the X Window to the moved Windows window */
      hasEnteredSizeMove = FALSE;
      winAdjustXWindow(window, hWnd);
      return 0;

    case WM_SHOWWINDOW:
      /* If window is shown, start mouse polling in case focus isn't on it */
      if (wParam)
        winStartMousePolling();

      break;

    case WM_WINDOWPOSCHANGED:
      /* ??? handle Z-order changes by reflecting to X */

      /*
       * Pass the message to DefWindowProc to let the function
       * break down WM_WINDOWPOSCHANGED to WM_MOVE and WM_SIZE.
      */
      break;

    case WM_STYLECHANGING:
      /*
        When the style changes, adjust the Windows window size so the client area remains the same size,
        and adjust the Windows window position so that the client area remains in the same place.
      */
      {
        RECT newWinRect;
        DWORD dwExStyle;
        DWORD dwStyle;
        DWORD newStyle = ((STYLESTRUCT *) lParam)->styleNew;
        WINDOWINFO wi;
        HDWP hDwp;

        dwExStyle = GetWindowLongPtr(hWnd, GWL_EXSTYLE);
        dwStyle = GetWindowLongPtr(hWnd, GWL_STYLE);

        DEBUG("winTopLevelWindowProc: WM_STYLECHANGING from %08x %08x\n", dwStyle, dwExStyle);

        if (wParam == (WPARAM)GWL_EXSTYLE)
          dwExStyle = newStyle;

        if (wParam == (WPARAM)GWL_STYLE)
          dwStyle = newStyle;

        DEBUG("winTopLevelWindowProc: WM_STYLECHANGING to %08x %08x\n", dwStyle, dwExStyle);

        /* Get client rect in screen coordinates */
        wi.cbSize = sizeof(WINDOWINFO);
        GetWindowInfo(hWnd, &wi);

        DEBUG("winTopLevelWindowProc: WM_STYLECHANGING client area {%d, %d, %d, %d}, {%d x %d}\n",
                 wi.rcClient.left, wi.rcClient.top, wi.rcClient.right,
                 wi.rcClient.bottom, wi.rcClient.right - wi.rcClient.left,
                 wi.rcClient.bottom - wi.rcClient.top);

        newWinRect = wi.rcClient;
        if (!AdjustWindowRectEx(&newWinRect, dwStyle, FALSE, dwExStyle))
          DEBUG("winTopLevelWindowProc: WM_STYLECHANGING AdjustWindowRectEx failed\n");

        DEBUG("winTopLevelWindowProc: WM_STYLECHANGING window area should be {%d, %d, %d, %d}, {%d x %d}\n",
                 newWinRect.left, newWinRect.top, newWinRect.right,
                 newWinRect.bottom, newWinRect.right - newWinRect.left,
                 newWinRect.bottom - newWinRect.top);

        /*
          Style change hasn't happened yet, so we can't adjust the window size yet, as the winAdjustXWindow()
          which WM_SIZE does will use the current (unchanged) style.  Instead make a note to change it when
          WM_STYLECHANGED is received...
        */
        hDwp = BeginDeferWindowPos(1);
        hDwp = DeferWindowPos(hDwp, hWnd, NULL, newWinRect.left,
                              newWinRect.top, newWinRect.right - newWinRect.left,
                              newWinRect.bottom - newWinRect.top,
                              SWP_NOACTIVATE | SWP_NOZORDER);
        SetProp(hWnd, WIN_HDWP_PROP, (HANDLE)hDwp);
      }
      return 0;

    case WM_STYLECHANGED:
      {
        HDWP hDwp = GetProp(hWnd, WIN_HDWP_PROP);
        if (hDwp)
          {
            EndDeferWindowPos(hDwp);
            RemoveProp(hWnd, WIN_HDWP_PROP);
          }
        DEBUG("winTopLevelWindowProc: WM_STYLECHANGED done\n");
      }
      return 0;

    case WM_ERASEBKGND:
      /* Since we are going to blit to the entire invalidated region,
         there's no point erasing to background first */
      return TRUE;

    case WM_PAINT:
      {
        HDC hdcUpdate;
        PAINTSTRUCT ps;
        hdcUpdate = BeginPaint(hWnd, &ps);

        DEBUG("WM_PAINT XID 0x%08x, hWnd 0x%08x\n", window->window_id, hWnd);
        DEBUG("invalidated rect is %ldx%ld @ (%ld, %ld)\n", ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, ps.rcPaint.left, ps.rcPaint.top);

        /* Don't do anything if the PAINTSTRUCT is bogus */
        if (ps.rcPaint.right == 0 && ps.rcPaint.bottom == 0 &&
            ps.rcPaint.left == 0 && ps.rcPaint.top == 0)
          {
            DEBUG("empty invalidated rect\n");
          }
        else
          {
            /* XXX: would be more efficient if we could just ask for the part we need */
            xcwm_image_t *image = xcwm_image_copy_full(window);
            if (image)
              {
                CheckForAlpha(hWnd, image);

                /* Update the region asked for */
                BitBltFromImage(image, hdcUpdate,
                                ps.rcPaint.left, ps.rcPaint.top,
                                ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top,
                                ps.rcPaint.left, ps.rcPaint.top);

                xcwm_image_destroy(image);
              }
            else
              {
                DEBUG("image_copy failed\n");
              }
          }

        EndPaint(hWnd, &ps);
        return 0;
      }

    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    case WM_SYSKEYUP:
    case WM_KEYUP:
      {
        bool press = (message == WM_SYSKEYDOWN) || (message == WM_KEYDOWN);

        /*
         * Don't do anything for the Windows keys, as focus will soon
         * be returned to Windows.
         */
        if (wParam == VK_LWIN || wParam == VK_RWIN)
          break;

        /* Discard fake Ctrl_L events that precede AltGR on non-US keyboards */
        if (winIsFakeCtrl_L(message, wParam, lParam))
          return 0;

        /*
         * Discard presses generated from Windows auto-repeat
         */
        if (press && (lParam & (1 << 30)))
          {
            switch (wParam) {
              /* ago: Pressing LControl while RControl is pressed is
               * Indicated as repeat. Fix this!
               */
            case VK_CONTROL:
            case VK_SHIFT:
              if (winCheckKeyPressed(wParam, lParam))
                return 0;
              break;
            default:
              return 0;
            }
          }

        /* Translate Windows key code to X scan code */
        int iScanCode = winTranslateKey(wParam, lParam);

        /* Ignore press repeats for CapsLock */
        if (press && (wParam == VK_CAPITAL))
          lParam = 1;

        /* Send the key event(s) */
        int i;
        for (i = 0; i < LOWORD(lParam); ++i)
          winSendKeyEvent(iScanCode, press);

        /* Release all pressed shift keys */
        if (!press && (wParam == VK_SHIFT))
          winFixShiftKeys(iScanCode);
      }

      return 0;

    case WM_MOUSEMOVE:
      {
        /* Have we requested WM_MOUSELEAVE when mouse leaves window? */
        if (!s_fTracking)
          {
            TRACKMOUSEEVENT tme;

            /* Setup data structure */
            memset(&tme, 0, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hWnd;

            /* Call the tracking function */
            if (!TrackMouseEvent(&tme))
              fprintf(stderr, "winTopLevelWindowProc: TrackMouseEvent failed\n");

            /* Flag that we are tracking now */
            s_fTracking = TRUE;
          }

        winStopMousePolling();

        /* Unpack the client area mouse coordinates */
        POINT ptMouse;
        ptMouse.x = GET_X_LPARAM(lParam);
        ptMouse.y = GET_Y_LPARAM(lParam);

        /* Translate the client area mouse coordinates to X coordinates */
        ClientToXCoord(hWnd, &ptMouse);

        /* Deliver absolute cursor position to X Server */
        xcwm_input_mouse_motion(xcwm_window_get_context(window), ptMouse.x, ptMouse.y);

        return 0;
      }

    case WM_MOUSELEAVE:
        /* Mouse has left our client area */
        /* Flag that we need to request WM_MOUSELEAVE again when mouse returns to client area */
        s_fTracking = FALSE;
        winStartMousePolling();
        return 0;

    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN:
      return winMouseButtonsHandle(window, TRUE, 1, hWnd);

    case WM_LBUTTONUP:
      return winMouseButtonsHandle(window, FALSE, 1, hWnd);

    case WM_MBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
      return winMouseButtonsHandle(window, TRUE, 2, hWnd);

    case WM_MBUTTONUP:
      return winMouseButtonsHandle(window, FALSE, 2, hWnd);

    case WM_RBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
      return winMouseButtonsHandle(window, TRUE, 3, hWnd);

    case WM_RBUTTONUP:
      return winMouseButtonsHandle(window, FALSE, 3, hWnd);

    case WM_XBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
      return winMouseButtonsHandle(window, TRUE, HIWORD(wParam) + 5, hWnd);

    case WM_XBUTTONUP:
      return winMouseButtonsHandle(window, FALSE, HIWORD(wParam) + 5, hWnd);
    }

  return DefWindowProc(hWnd, message, wParam, lParam);
}

/*
 * winInitMultiWindowClass - Register our Window class
 */
static
void winInitMultiWindowClass(void)
{
  static ATOM atomXWinClass = 0;
  WNDCLASSEX wcx;

  if (atomXWinClass == 0)
    {
      wcx.cbSize = sizeof(WNDCLASSEX);
      wcx.style = CS_HREDRAW | CS_VREDRAW;
      wcx.lpfnWndProc = winTopLevelWindowProc;
      wcx.cbClsExtra = 0;
      wcx.cbWndExtra = 0;
      wcx.hInstance = GetModuleHandle(NULL);
      wcx.hIcon = NULL;
      wcx.hCursor = 0;
      wcx.hbrBackground = (HBRUSH) GetStockObject(WHITE_BRUSH);
      wcx.lpszMenuName = NULL;
      wcx.lpszClassName = WINDOW_CLASS_X;
      wcx.hIconSm = NULL;

      DEBUG("Creating class: %s\n", WINDOW_CLASS_X);

      atomXWinClass = RegisterClassEx (&wcx);
    }
}

/*
 * winCreateWindowsWindow - Create a Windows window associated with an X window
 */
void
winCreateWindowsWindow(xcwm_window_t *window)
{
  int iX, iY, iWidth, iHeight;
  HWND hWnd;
  HWND hFore = NULL;
  DWORD dwStyle, dwExStyle;
  RECT rc;

  winInitMultiWindowClass();
  assert(xcwm_window_get_local_data(window) == NULL);

  DEBUG("winCreateWindowsWindow: window 0x%08x XID 0x%08x\n", window, window->window_id);

  xcwm_rect_t *windowSize = xcwm_window_get_full_rect(window);
  iX = windowSize->x + GetSystemMetrics(SM_XVIRTUALSCREEN);
  iY = windowSize->y + GetSystemMetrics(SM_YVIRTUALSCREEN);
  iWidth = windowSize->width;
  iHeight = windowSize->height;

#if 0
  // XXX: xcwm can probably just ignore input only windows...
  xcb_get_window_attributes_reply_t *attr;
  attr = xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, pWin->id), NULL);

  /* If it's an InputOutput window, and so is going to end up being made visible,
     make sure the window actually ends up somewhere where it will be visible */
  if (attr->_class != XCB_WINDOW_CLASS_INPUT_ONLY)
    {
#endif
      if ((iX < GetSystemMetrics (SM_XVIRTUALSCREEN)) || (iX > GetSystemMetrics (SM_CXVIRTUALSCREEN)))
        iX = CW_USEDEFAULT;

      if ((iY < GetSystemMetrics (SM_YVIRTUALSCREEN)) || (iY > GetSystemMetrics (SM_CYVIRTUALSCREEN)))
        iY = CW_USEDEFAULT;
#if 0
    }
#endif

  DEBUG("winCreateWindowsWindow: %dx%d @ %dx%d\n", iWidth, iHeight, iX, iY);

  const xcwm_window_t *parent = xcwm_window_get_transient_for(window);
  if (parent)
    {
      /*
         If we are transient-for another window, get the parent windows HWND
       */
      hFore = xcwm_window_get_local_data(parent);
    }
  else
    {
      /* Default positions if none specified */
      if (!xcwm_window_is_override_redirect(window))
        {
          if (!(window->size_hints.flags & (XCB_ICCCM_SIZE_HINT_US_POSITION | XCB_ICCCM_SIZE_HINT_P_POSITION)))
            {
              iX = CW_USEDEFAULT;
              iY = CW_USEDEFAULT;
            }
        }
    }

  /* Make it WS_OVERLAPPED in create call since WS_POPUP doesn't support */
  /* CW_USEDEFAULT, change back to popup after creation */
  dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
  dwExStyle = WS_EX_TOOLWINDOW | WS_EX_LAYERED;

  /*
    Calculate the window coordinates containing the requested client area,
    being careful to preseve CW_USEDEFAULT
  */
  rc.top = (iY != (int)CW_USEDEFAULT) ? iY : 0;
  rc.left = (iX != (int)CW_USEDEFAULT) ? iX : 0;
  rc.bottom = rc.top + iHeight;
  rc.right = rc.left + iWidth;
  AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);
  if (iY != (int)CW_USEDEFAULT) iY = rc.top;
  if (iX != (int)CW_USEDEFAULT) iX = rc.left;
  iHeight = rc.bottom - rc.top;
  iWidth = rc.right - rc.left;

  DEBUG("winCreateWindowsWindow: %dx%d @ %dx%d\n", iWidth, iHeight, iX, iY);

  /* Create the window */
  hWnd = CreateWindowExA(dwExStyle,		/* Extended styles */
                         WINDOW_CLASS_X,	/* Class name */
                         WINDOW_TITLE_X,	/* Window name */
                         dwStyle,		/* Styles */
                         iX,			/* Horizontal position */
                         iY,			/* Vertical position */
                         iWidth,		/* Right edge */
                         iHeight,		/* Bottom edge */
                         hFore,		        /* Null or Parent window if transient-for */
                         (HMENU) NULL,		/* No menu */
                         GetModuleHandle(NULL), /* Instance handle */
                         (LPVOID)window);       /* xcwm window handle */
  if (hWnd == NULL)
    {
      fprintf(stderr, "CreateWindowExA() failed: %d\n", (int) GetLastError());
    }

  /* save the HWND into the context */
  xcwm_window_set_local_data(window, hWnd);

  /* Set icons */
  HICON	hIcon;
  HICON hIconSmall;
  winSelectIcons(&hIcon, &hIconSmall);
  if (hIcon) SendMessage (hWnd, WM_SETICON, ICON_BIG, (LPARAM) hIcon);
  if (hIconSmall) SendMessage (hWnd, WM_SETICON, ICON_SMALL, (LPARAM) hIconSmall);

  /* Change style back to popup, already placed... */
  SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
  SetWindowPos(hWnd, 0, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

  /* Adjust the X window to match the window placement we actually got... */
  winAdjustXWindow(window, hWnd);

  /* Make sure it gets the proper system menu for a WS_POPUP, too */
  GetSystemMenu(hWnd, TRUE);

  /* Establish the default style */
  if (!xcwm_window_is_override_redirect(window))
    {
      if (parent)
        {
          /* Set the transient style flags */
          SetWindowLongPtr(hWnd, GWL_STYLE, WS_POPUP | WS_OVERLAPPED | WS_SYSMENU | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        }
      else
        {
          /* Set the window standard style flags */
          SetWindowLongPtr(hWnd, GWL_STYLE, (WS_POPUP | WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS) & ~WS_CAPTION & ~WS_SIZEBOX);
        }
    }
  else
    {
      /* override-redirect window, remains un-decorated, but put it on top of all other X windows */
      /* zstyle = SWP_TOPMOST; */
    }

  /* Apply all properties which effect the window appearance or behaviour */
  UpdateName(window);
  /* UpdateIcon(); */
  if (!xcwm_window_is_override_redirect(window))
    UpdateStyle(window);

  BumpWindowPosition(hWnd);

#if 0
  /* Display the window without activating it */
  if (attr->_class != XCB_WINDOW_CLASS_INPUT_ONLY)
#endif
    {
      ShowWindow(hWnd, SW_SHOWNOACTIVATE);
    }
}

void
winDestroyWindowsWindow(xcwm_window_t *window)
{
  DEBUG("winDestroyWindowsWindow: window 0x%08x XID 0x%08x\n", window, window->window_id);

  HWND hWnd = xcwm_window_get_local_data(window);

  /* Bail out if the Windows window handle is invalid */
  if (hWnd == NULL)
    return;

#if 0
  /* Store the info we need to destroy after this window is gone */
  hIcon = (HICON)SendMessage(pWin->hWnd, WM_GETICON, ICON_BIG, 0);
  hIconSm = (HICON)SendMessage(pWin->hWnd, WM_GETICON, ICON_SMALL, 0);
#endif

  /* Destroy the Windows window */
  DestroyWindow(hWnd);

  /* Null our handle to the Window so referencing it will cause an error */
  xcwm_window_set_local_data(window, NULL);

#if 0
  /* Destroy any icons we created for this window */
  winDestroyIcon(hIcon);
  winDestroyIcon(hIconSm);
#endif
}
